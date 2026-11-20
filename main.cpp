// gravc — the Grav compiler driver.
//
// Usage:
//   gravc <input.grav> [-o out] [--emit c|bin|both] [-Werror]
//   gravc -            # read source from stdin, write C to stdout
//
// Pipeline: load (with imports) -> lex -> parse -> build symbols ->
//           type-check -> generate C -> optionally compile to a binary.
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <algorithm>
#include <chrono>
#include <vector>

#include "codegen/codegen.h"
#include "codegen/prune.h"
#include "common/diagnostics.h"
#include "driver/build_cache.h"
#include "driver/infer_cache.h"
#include "driver/modules.h"
#include "driver/format.h"
#include "driver/packages.h"
#include "driver/self_update.h"
#include "lexer/lexer.h"
#include "lexer/preprocess.h"
#include "parser/parser.h"
#include "sema/enums.h"
#include "sema/monomorph.h"
#include "sema/cells.h"
#include "sema/sanitize.h"
#include "sema/symbols.h"
#include "sema/typechecker.h"

namespace fs = std::filesystem;

namespace {

enum class EmitMode { C, Asm, Bin, Both };

std::string readStream(std::istream &in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

using grav::SrcLoc;

// Set while compiling, so diagnostics can suggest where an unknown name lives.
const grav::ModuleLoader *gLoader = nullptr;

// "unknown ... 'Deque'" -> a hint naming the namespace that declares Deque.
std::string unknownNameHint(const std::string &msg) {
    if (!gLoader || msg.find("unknown") == std::string::npos) return "";
    auto q1 = msg.find('\'');
    auto q2 = q1 == std::string::npos ? q1 : msg.find('\'', q1 + 1);
    if (q2 == std::string::npos) return "";
    std::string name = msg.substr(q1 + 1, q2 - q1 - 1);
    name = name.substr(0, name.find('<'));
    if (name.empty() || name.find('.') != std::string::npos) return "";
    auto where = gLoader->whereIs(name);
    if (where.empty()) return "";
    const std::string &ns = where.front();
    return "\n    note: '" + name + "' is declared in " + ns + "; write `use " + ns + "." + name +
           "`, add it to the import list, or call it " + ns + "." + name;
}

// Print a diagnostic, mapping its combined-source line back to the real file:line
// and showing the offending source line with a caret.
// `grav check --json`: diagnostics are collected and printed as one JSON
// document when the driver exits (see JsonReport).
bool gJson = false;
struct JsonDiag { std::string file; int line = 0, col = 0; std::string severity, stage, message, note; };
std::vector<JsonDiag> gJsonDiags;
// `grav check --json --types`: the type of every local binding in the checked file.
bool gTypes = false;
struct JsonType { int line = 0, col = 0; std::string name, type; };
std::vector<JsonType> gJsonTypes;

std::string jsonEscape(const std::string &s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += static_cast<char>(c);
        }
    }
    return o;
}

struct JsonReport {
    ~JsonReport() {
        if (!gJson) return;
        int errors = 0, warnings = 0;
        std::cout << "{\"diagnostics\": [";
        for (size_t i = 0; i < gJsonDiags.size(); ++i) {
            const auto &d = gJsonDiags[i];
            (d.severity == "error" ? errors : warnings)++;
            std::cout << (i ? ",\n  " : "\n  ") << "{\"file\": \"" << jsonEscape(d.file) << "\", \"line\": " << d.line
                      << ", \"column\": " << d.col << ", \"severity\": \"" << d.severity << "\", \"stage\": \""
                      << d.stage << "\", \"message\": \"" << jsonEscape(d.message) << "\""
                      << (d.note.empty() ? "" : ", \"note\": \"" + jsonEscape(d.note) + "\"") << "}";
        }
        std::cout << (gJsonDiags.empty() ? "" : "\n") << "], \"errors\": " << errors << ", \"warnings\": " << warnings;
        if (gTypes) {
            std::cout << ", \"types\": [";
            for (size_t i = 0; i < gJsonTypes.size(); ++i) {
                const auto &t = gJsonTypes[i];
                std::cout << (i ? ",\n  " : "\n  ") << "{\"line\": " << t.line << ", \"column\": " << t.col
                          << ", \"name\": \"" << jsonEscape(t.name) << "\", \"type\": \"" << jsonEscape(t.type) << "\"}";
            }
            std::cout << (gJsonTypes.empty() ? "" : "\n") << "]";
        }
        std::cout << "}\n";
    }
};

// The bindings the checker declared in `file` (not in what it imports), with
// readable types. A generic function's body is checked once per instance: a
// binding whose type differs between instances is left out.
void collectTypes(const grav::TypeChecker &checker, const std::vector<SrcLoc> &srcMap, const fs::path &file) {
    std::error_code ec;
    const std::string want = fs::weakly_canonical(file, ec).string();
    std::map<std::string, bool> inFile; // srcMap file -> is the checked file
    std::map<std::tuple<int, int, std::string>, std::string> seen;
    for (const auto &b : checker.bindings()) {
        if (b.line < 1 || b.line > (int)srcMap.size() || b.name.rfind("__", 0) == 0 ||
            b.name.find('$') != std::string::npos || b.type.isError())
            continue;
        const SrcLoc &loc = srcMap[b.line - 1];
        auto it = inFile.find(loc.file);
        if (it == inFile.end()) it = inFile.emplace(loc.file, fs::weakly_canonical(loc.file, ec).string() == want).first;
        if (!it->second) continue;
        std::string type = grav::typeRefName(b.type);
        auto [pos, fresh] = seen.emplace(std::make_tuple(loc.line, b.col, b.name), type);
        if (fresh) gJsonTypes.push_back({loc.line, b.col, b.name, type});
        else if (pos->second != type)
            for (auto &t : gJsonTypes)
                if (t.line == loc.line && t.col == b.col && t.name == b.name) t.type.clear();
    }
    gJsonTypes.erase(std::remove_if(gJsonTypes.begin(), gJsonTypes.end(), [](const JsonType &t) { return t.type.empty(); }),
                     gJsonTypes.end());
}

// A driver-level error ("file:line: message" or plain text).
void reportError(const std::string &msg) {
    if (!gJson) { std::cerr << "gravc: " << msg << "\n"; return; }
    JsonDiag d;
    d.severity = "error";
    d.stage = "import";
    std::string first = msg.substr(0, msg.find('\n'));
    size_t c1 = first.find(':'), c2 = c1 == std::string::npos ? c1 : first.find(':', c1 + 1);
    if (c2 != std::string::npos && c2 > c1 + 1 &&
        first.find_first_not_of("0123456789", c1 + 1) == c2) {
        d.file = first.substr(0, c1);
        d.line = std::stoi(first.substr(c1 + 1, c2 - c1 - 1));
        d.col = 1;
        d.message = msg.substr(c2 + 2);
    } else {
        d.message = msg;
    }
    gJsonDiags.push_back(d);
}

void printDiag(const grav::GravError &d, const std::vector<SrcLoc> &srcMap) {
    std::string label = (d.stage() == "warning") ? "warning" : d.stage() + " error";
    int combined = d.line();
    SrcLoc loc;
    bool mapped = combined >= 1 && combined <= (int)srcMap.size();
    if (mapped) loc = srcMap[combined - 1];
    if (gJson) {
        JsonDiag j;
        j.file = mapped ? loc.file : "<input>";
        j.line = mapped ? loc.line : combined;
        j.col = d.col();
        j.severity = d.stage() == "warning" ? "warning" : "error";
        j.stage = d.stage();
        j.message = grav::prettyNames(d.message());
        std::string hint = unknownNameHint(d.message());
        if (!hint.empty()) j.note = hint.substr(hint.find("note: ") + 6);
        gJsonDiags.push_back(j);
        return;
    }
    std::string where = mapped ? (loc.file + ":" + std::to_string(loc.line))
                               : ("line " + std::to_string(combined));
    std::cerr << "gravc: " << where << ":" << d.col() << ": " << label << ": "
              << grav::prettyNames(d.message()) << unknownNameHint(d.message()) << "\n";
    if (mapped) {
        std::ifstream f(loc.file);
        std::string ln;
        for (int i = 0; i < loc.line && std::getline(f, ln); ++i) {}
        if (!ln.empty() || loc.line > 0) {
            std::cerr << "    " << ln << "\n";
            std::cerr << "    " << std::string(std::max(0, d.col() - 1), ' ') << "^\n";
        }
    }
}

// Readable names for generic instances, used to un-mangle diagnostics:
// "HashMap$i64$int" -> "HashMap<long, int>", "fold$string" -> "fold<string>".
void registerDisplayNames(const grav::GenericTables &g) {
    grav::clearDisplayNames();
    auto readable = [](const std::string &name, const std::vector<grav::TypeRef> &args) {
        std::string s = name + "<";
        for (size_t i = 0; i < args.size(); ++i) s += (i ? ", " : "") + grav::typeRefName(args[i]);
        return s + ">";
    };
    // Arguments may themselves be instances, so register in order of length:
    // shorter (inner) names first, which typeRefName then resolves.
    std::vector<std::pair<std::string, const std::pair<std::string, std::vector<grav::TypeRef>> *>> all;
    for (const auto &[inst, origin] : g.instances) all.push_back({inst, &origin});
    for (const auto &[inst, origin] : g.methodInstances) all.push_back({inst, &origin});
    std::sort(all.begin(), all.end(),
              [](const auto &a, const auto &b) { return a.first.size() < b.first.size(); });
    for (const auto &[inst, origin] : all) {
        // Data enum variants read as `Option.Some<int>`, not `Option$Some<int>`.
        std::string name = origin->first;
        if (name.rfind("std.", 0) == 0 && name.find('.', 4) != std::string::npos)
            name = name.substr(name.find('.', 4) + 1); // std.collections.Array -> Array
        std::replace(name.begin(), name.end(), '$', '.');
        grav::setDisplayName(inst, readable(name, origin->second));
    }
}

// Non-generic data enum variants: `Shape$Circle` reads as `Shape.Circle`.
void registerVariantNames(const grav::Program &program) {
    for (const auto &d : program.decls)
        if (auto *c = dynamic_cast<const grav::ClassDecl *>(d.get()); c && c->variantTag >= 0 &&
                                                                    c->typeParams.empty()) {
            std::string shown = c->fqName;
            std::replace(shown.begin(), shown.end(), '$', '.');
            if (shown != c->fqName) grav::setDisplayName(c->fqName, shown);
        }
}

int usage(const char *prog) {
    std::cerr <<
        "usage: " << prog << " <input.grav> [options] [-- args...]\n"
        "       " << prog << " run <file.grav> [-- args]   build and run\n"
        "       " << prog << " build <file.grav>           build a binary\n"
        "       " << prog << " test <file.grav> [--filter text]   run the file's test blocks\n"
        "       " << prog << " new <name> | add <repo>[@version] | install | update | remove <name> | packages\n"
        "       " << prog << " check [--json] [--types] [file.grav]  type-check only (JSON diagnostics, binding types, for editors)\n"
        "       " << prog << " fmt [--check] [paths]           format source files (fmt - : stdin to stdout)\n"
        "       " << prog << " env [--json]                    where the standard library and packages are\n"
        "       " << prog << " self-update [--check]           update grav (installed by install.sh)\n"
        "       " << prog << " -                 read stdin, write C to stdout\n"
        "\n"
        "output:\n"
        "  -o <path>            output path (binary, .c, or .s)\n"
        "  --emit c|asm|bin|both   what to produce (default: c)\n"
        "  -c                   emit C            (alias for --emit c)\n"
        "  -S                   emit assembly     (alias for --emit asm)\n"
        "  -b, --bin            emit a binary     (alias for --emit bin)\n"
        "  -r, --run            build a binary and run it (args after `--`)\n"
        "  --keep-c             keep the generated .c when building a binary\n"
        "\n"
        "codegen (passed to the C compiler):\n"
        "  -O0|-O1|-O2|-O3|-Os|-Ofast|-Og  optimization level (default: -O2; -Og for grav test, -O0 with -g)\n"
        "  --release            -O3, no stack-trace bookkeeping, NDEBUG\n"
        "  --no-trace           drop call-stack tracing (exceptions report no trace)\n"
        "  --native             tune for this machine (-march=native)\n"
        "  --no-lines           omit #line directives (map generated C to itself)\n"
        "  --race               detect data races between threads (ThreadSanitizer)\n"
        "  --sanitize           detect memory errors and undefined behaviour (ASan+UBSan)\n"
        "  -g | -g3             include debug info\n"
        "  --cc <compiler>      C compiler to use (default: $CC or cc)\n"
        "\n"
        "diagnostics:\n"
        "  -Werror              treat warnings as errors\n"
        "  -v, --verbose        print the C compiler command\n"
        "  -h, --help           show this help\n";
    return 2;
}

// After each function the generated code maps back to the C file itself: turn
// the codegen's reset markers into `#line <next line> "<file.c>"`.
std::string resolveLineResets(const std::string &c, const std::string &cFile) {
    std::string out;
    out.reserve(c.size() + 1024);
    const std::string marker = grav::CodeGen::kLineReset;
    int lineNo = 1;
    size_t pos = 0;
    while (pos < c.size()) {
        size_t eol = c.find('\n', pos);
        if (eol == std::string::npos) eol = c.size();
        std::string ln = c.substr(pos, eol - pos);
        if (ln == marker) {
            std::string esc;
            for (char ch : cFile) { if (ch == '\\' || ch == '"') esc += '\\'; esc += ch; }
            out += "#line " + std::to_string(lineNo + 1) + " \"" + esc + "\"";
        } else {
            out += ln;
        }
        if (eol < c.size()) out += '\n';
        ++lineNo;
        pos = eol + 1;
    }
    return out;
}

std::string stem(const std::string &input) {
    auto dot = input.find_last_of('.');
    return dot == std::string::npos ? input : input.substr(0, dot);
}

} // namespace

int main(int argc, char **argv) {
    std::string inputPath, outputPath, ccOverride, optFlag, debugFlag;
    bool useStdin = false, toStdout = false, werror = false;
    bool runAfter = false, keepC = false, verbose = false;
    bool trace = true, native = false, release = false, race = false, sanitize = false;
    bool lineDirectives = true;
    bool testMode = false;
    std::string testFilter;
    std::vector<std::string> runArgs;
    EmitMode emit = EmitMode::C;

    // Subcommands: `grav run f.grav`, `grav build f.grav`, `grav test f.grav`.
    int first = 1;
    bool checkOnly = false;
    JsonReport jsonReport; // prints the --json diagnostics on every exit path
    if (argc > 1 && std::string(argv[1]) == "fmt") {
        std::vector<std::string> args(argv + 2, argv + argc);
        return grav::runFormat(args);
    }
    if (argc > 1 && std::string(argv[1]) == "self-update") {
        std::vector<std::string> args(argv + 2, argv + argc);
        return grav::runSelfUpdate(args);
    }
    if (argc > 1 && std::string(argv[1]) == "env") {
        // Where things are, for editors and scripts.
        bool json = argc > 2 && std::string(argv[2]) == "--json";
        fs::path exe = grav::currentExecutable();
        std::string stdDir = grav::stdLibDir().string(), pkgs = grav::packagesDir().string();
        std::string projErr2;
        grav::ProjectConfig pc = grav::findProject(fs::current_path(), projErr2);
        if (json) {
            std::cout << "{\"version\": \"" << GRAV_VERSION << "\", \"compiler\": \"" << jsonEscape(exe.string())
                      << "\", \"std\": \"" << jsonEscape(stdDir) << "\", \"packages\": \"" << jsonEscape(pkgs)
                      << "\", \"project\": \"" << (pc.found ? jsonEscape(pc.root.string()) : "") << "\"}\n";
        } else {
            std::cout << "GRAV_VERSION  " << GRAV_VERSION << "\nGRAV_COMPILER " << exe.string() << "\nGRAV_STD      "
                      << stdDir << "\nGRAV_PACKAGES " << pkgs << "\nproject       "
                      << (pc.found ? pc.root.string() : "(none)") << "\n";
        }
        return 0;
    }
    if (argc > 1 && grav::isPackageCommand(argv[1])) {
        std::vector<std::string> args(argv + 1, argv + argc);
        return grav::runPackageCommand(args);
    }
    if (argc > 1) {
        std::string sub = argv[1];
        if (sub == "run") { runAfter = true; emit = EmitMode::Bin; first = 2; }
        else if (sub == "build") { emit = EmitMode::Bin; first = 2; }
        else if (sub == "test") { testMode = true; runAfter = true; emit = EmitMode::Bin; first = 2; }
        else if (sub == "check") { checkOnly = true; first = 2; }
    }

    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--") {                       // remaining args go to the program
            for (int j = i + 1; j < argc; ++j) runArgs.emplace_back(argv[j]);
            break;
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else if (arg == "-o") {
            if (i + 1 >= argc) return usage(argv[0]);
            outputPath = argv[++i];
        } else if (arg == "--emit") {
            if (i + 1 >= argc) return usage(argv[0]);
            std::string m = argv[++i];
            if (m == "c") emit = EmitMode::C;
            else if (m == "asm") emit = EmitMode::Asm;
            else if (m == "bin") emit = EmitMode::Bin;
            else if (m == "both") emit = EmitMode::Both;
            else { std::cerr << "gravc: unknown --emit mode '" << m << "'\n"; return usage(argv[0]); }
        } else if (arg == "-c") {
            emit = EmitMode::C;
        } else if (arg == "-S") {
            emit = EmitMode::Asm;
        } else if (arg == "-b" || arg == "--bin") {
            emit = EmitMode::Bin;
        } else if (arg == "-r" || arg == "--run") {
            runAfter = true;
            if (emit == EmitMode::C) emit = EmitMode::Bin;
        } else if (arg == "--keep-c") {
            keepC = true;
        } else if (arg == "--cc") {
            if (i + 1 >= argc) return usage(argv[0]);
            ccOverride = argv[++i];
        } else if (arg == "-O0" || arg == "-O1" || arg == "-O2" || arg == "-O3" ||
                   arg == "-Os" || arg == "-Ofast" || arg == "-Og") {
            optFlag = arg;
        } else if (arg == "-g" || arg == "-g3" || arg == "-g0" || arg == "-g1" || arg == "-g2") {
            debugFlag = arg;
        } else if (arg == "--release") {
            release = true;
            trace = false;
        } else if (arg == "--no-trace") {
            trace = false;
        } else if (arg == "--native") {
            native = true;
        } else if (arg == "--json") {
            gJson = true;
        } else if (arg == "--types") {
            gJson = gTypes = true;
        } else if (arg == "--version") {
            std::cout << "grav " << GRAV_VERSION << "\n";
            return 0;
        } else if (arg == "--filter") {
            if (i + 1 >= argc) return usage(argv[0]);
            testFilter = argv[++i];
        } else if (arg == "--no-lines") {
            lineDirectives = false;
        } else if (arg == "--race") {
            race = true;
        } else if (arg == "--sanitize") {
            sanitize = true;
        } else if (arg == "-Werror") {
            werror = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-") {
            useStdin = true;
            toStdout = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "gravc: unknown option '" << arg << "'\n";
            return usage(argv[0]);
        } else if (inputPath.empty()) {
            inputPath = arg;
        } else {
            return usage(argv[0]);
        }
    }

    // grav.toml (searched upward from the input file, or the current directory)
    // supplies extra import roots and the default file for run/build/test.
    std::string projErr;
    fs::path projStart = inputPath.empty() ? fs::current_path() : fs::path(inputPath).parent_path();
    if (projStart.empty()) projStart = fs::current_path();
    grav::ProjectConfig project = grav::findProject(projStart, projErr);
    if (!projErr.empty()) { reportError(projErr); return 1; }
    if (!useStdin && inputPath.empty() && first == 2 && project.found && !project.main.empty())
        inputPath = project.main.string();
    if (!useStdin && inputPath.empty()) return usage(argv[0]);

    // ---- load source (with imports) ----
    std::string source;
    std::vector<SrcLoc> srcMap; // combined line -> original file:line
    if (useStdin) {
        source = readStream(std::cin);
    } else {
        static grav::ModuleLoader loader(project);
        gLoader = &loader;
        std::string err;
        if (!loader.load(inputPath, source, srcMap, err)) {
            reportError(err);
            return 1;
        }
    }

    // After a binary is built (or reused): say so, and run it for `run`/`test`.
    auto finishBuilt = [&](const std::string &exePath, bool tempExe, const std::string &also = "") -> int {
        if (!tempExe)
            std::cerr << "gravc: built " << exePath << (also.empty() ? "" : " (and wrote " + also + ")") << "\n";
        if (!runAfter) return 0;
        // A relative path with no directory needs a `./` so the shell finds it.
        std::string runPath = exePath;
        if (runPath.find('/') == std::string::npos) runPath = "./" + runPath;
        std::string runCmd = "\"" + runPath + "\"";
        for (const auto &a : runArgs) runCmd += " \"" + a + "\"";
        if (testMode) setenv("GRAV_TEST_FILTER", testFilter.c_str(), 1);
        int prc = std::system(runCmd.c_str());
        if (tempExe) { std::error_code ec; fs::remove(exePath, ec); }
        // Pass the program's own exit status through.
        if (prc == -1) return 1;
        if (WIFEXITED(prc)) return WEXITSTATUS(prc);
        return 128 + (WIFSIGNALED(prc) ? WTERMSIG(prc) : 0);
    };

    // A program built before from the same source, files, compiler and options:
    // its binary is reused without running the front end or the C compiler.
    std::unique_ptr<grav::BuildCache> whole;
    if (!checkOnly && !toStdout && emit == EmitMode::Bin && !keepC && !gJson) {
        std::vector<std::string> parts{GRAV_VERSION};
        std::error_code sec;
        fs::path self = fs::read_symlink("/proc/self/exe", sec);
        if (!sec) {
            parts.push_back(self.string() + " " + std::to_string(fs::file_size(self, sec)) + " " +
                            std::to_string(fs::last_write_time(self, sec).time_since_epoch().count()));
        }
        for (int i = 1; i < argc; ++i) parts.push_back(argv[i]);
        for (const char *v : {"CC", "GRAV_LTO", "GRAV_STD"}) parts.push_back(std::getenv(v) ? std::getenv(v) : "");
        parts.push_back(source);
        std::string files;
        const std::string *last = nullptr; // (the map has a run of lines per file)
        for (const auto &s : srcMap)
            if (!last || *last != s.file) { files += s.file + "\n"; last = &s.file; }
        parts.push_back(files);
        if (!sec) whole = std::make_unique<grav::BuildCache>(parts);
        std::string exe = outputPath;
        bool tempExe = false;
        if (exe.empty() && testMode) {
            std::error_code tec;
            fs::path tmp = fs::temp_directory_path(tec);
            if (tec) tmp = ".";
            exe = (tmp / ("gravtest-" + std::to_string(getpid()) + "-" + fs::path(stem(inputPath)).filename().string())).string();
            tempExe = true;
        }
        if (exe.empty()) exe = stem(inputPath);
        if (whole && whole->restore(exe)) {
            if (verbose) std::cerr << "gravc: reused the cached build of this program\n";
            return finishBuilt(exe, tempExe);
        }
    }

    try {
        // The front end may run more than once: when the checker infers omitted
        // generic type arguments (`maxOf(1, 2)`, `new Box(5)`), it records them and
        // the program is compiled again with them filled in.
        grav::InferenceTable inference;
        std::unique_ptr<grav::Program> programPtr;
        std::unique_ptr<grav::Registry> registryPtr;
        std::unique_ptr<grav::TypeChecker> checkerPtr;
        std::vector<grav::GravError> errors;
        // GRAV_TIMES=1: how long each phase of each pass takes (stderr).
        static const bool timesOn = std::getenv("GRAV_TIMES") != nullptr;
        auto clock0 = std::chrono::steady_clock::now();
        auto lap = [&](const char *what, int pass) {
            if (!timesOn) return;
            auto now = std::chrono::steady_clock::now();
            std::cerr << "gravc: pass " << pass << " " << what << " "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(now - clock0).count() << " ms\n";
            clock0 = now;
        };
        // The last build's inferences that still hold seed the first pass (see
        // driver/infer_cache.h): an edit usually needs one pass instead of three.
        std::string seedPath = useStdin ? "" : grav::inferCache::pathFor(inputPath, testMode);
        size_t seeded = 0;
        for (int attempt = 0;; ++attempt) {
        bool retry = false;
        for (int pass = 0; pass < 8; ++pass) {
            lap("start", pass);
            grav::Lexer lexer(source);
            auto tokens = lexer.tokenize();
            tokens = grav::preprocess(std::move(tokens));
            grav::Parser parser(std::move(tokens));
            programPtr = std::make_unique<grav::Program>(parser.parseProgram());
            if (pass == 0 && attempt == 0) {
                seeded = grav::inferCache::load(seedPath, *programPtr, inference);
                if (timesOn) std::cerr << "gravc: " << seeded << " inference(s) from the last build\n";
            }
            grav::lowerEnumConstructors(*programPtr); // Shape.Circle(r) -> new Shape$Circle(r)
            grav::boxAssignedCaptures(*programPtr);   // variables lambdas assign live in shared cells
            grav::applyInferences(*programPtr, inference);
            lap("parse", pass);

            // Expand generic structs/functions into concrete instances before symbols.
            errors = grav::monomorphize(*programPtr, &inference);
            lap("monomorphize", pass);
            registerDisplayNames(programPtr->generics);
            registerVariantNames(*programPtr);
            // A failed expansion leaves half-made instances; checking them would only
            // bury the real error under follow-on errors from library code.
            if (!errors.empty()) {
                if (seeded > 0) { retry = true; break; } // (maybe a seed's doing: try without)
                for (const auto &err : errors) printDiag(err, srcMap);
                if (!gJson) std::cerr << "gravc: " << errors.size() << " error(s); aborting\n";
                return 1;
            }
            registryPtr = std::make_unique<grav::Registry>();
            auto buildErrors = registryPtr->build(*programPtr);
            errors.insert(errors.end(), buildErrors.begin(), buildErrors.end());
            lap("registry", pass);

            checkerPtr = std::make_unique<grav::TypeChecker>();
            checkerPtr->setInference(&inference);
            checkerPtr->recordBindings(gTypes);
            inference.added = 0;
            const auto &typeErrors = checkerPtr->check(*programPtr, *registryPtr);
            errors.insert(errors.end(), typeErrors.begin(), typeErrors.end());
            lap("check", pass);
            if (inference.added == 0) break; // nothing new inferred: this pass is final
        }
        // A program that fails with seeds is compiled again without them, so a seed
        // can never be the cause of an error.
        if (!retry && !errors.empty() && seeded > 0) retry = true;
        if (!retry) break;
        if (timesOn) std::cerr << "gravc: errors with the last build's inferences: again without them\n";
        inference = grav::InferenceTable();
        errors.clear();
        seeded = 0;
        }
        if (errors.empty()) grav::inferCache::save(seedPath, *programPtr, inference);
        grav::Program &program = *programPtr;
        grav::Registry &registry = *registryPtr;
        grav::TypeChecker &checker = *checkerPtr;
        if (gTypes && !useStdin) collectTypes(checker, srcMap, inputPath);

        // Warnings (e.g. unused variables). With -Werror they are fatal.
        const auto &warnings = checker.warnings();
        for (const auto &w : warnings) printDiag(w, srcMap);

        if (!errors.empty() || (werror && !warnings.empty())) {
            for (const auto &err : errors) printDiag(err, srcMap);
            size_t n = errors.size() + (werror ? warnings.size() : 0);
            if (!gJson) std::cerr << "gravc: " << n << " error(s); aborting\n";
            return 1;
        }
        // `grav check`: the program is valid; stop before generating code.
        if (checkOnly) {
            if (!gJson) std::cerr << "gravc: ok" << (warnings.empty() ? "" : " (" + std::to_string(warnings.size()) + " warning(s))") << "\n";
            return 0;
        }

        // Locals named like C keywords/library functions get C-safe spellings.
        grav::sanitizeIdentifiers(program);

        grav::CodeGen codegen;
        codegen.setTrace(trace);
        codegen.setLineDirectives(lineDirectives);
        codegen.setTestMode(testMode);
        // Hand the line map to codegen so runtime stack traces show real file:line.
        std::vector<grav::SourceMapEntry> cgMap;
        cgMap.reserve(srcMap.size());
        for (const auto &s : srcMap) cgMap.push_back({s.file, s.line});
        std::string c = codegen.generate(program, registry, cgMap);
        // Most methods of generic instances are never called: leave them out of the C
        // (GRAV_PRUNE=0 keeps everything).
        if (const char *pr = std::getenv("GRAV_PRUNE"); !pr || std::string(pr) != "0") c = grav::pruneUnreachable(c);

        if (toStdout) {
            std::cout << resolveLineResets(c, "<stdout>");
            return 0;
        }

        // Decide output paths.
        std::string base = stem(inputPath);
        std::string cPath, exePath = outputPath;
        bool tempC = false; // the .c is an intermediate to delete afterward
        bool tempExe = false; // a `grav test` binary, deleted after it runs
        if (emit == EmitMode::C) {
            cPath = outputPath.empty() ? base + ".c" : outputPath;
        } else if (emit == EmitMode::Both) {
            if (exePath.empty()) exePath = base;
            cPath = base + ".c";
        } else { // Asm or Bin: the .c is intermediate unless --keep-c
            if (exePath.empty() && testMode) {
                std::error_code tec;
                fs::path tmp = fs::temp_directory_path(tec);
                if (tec) tmp = ".";
                exePath = (tmp / ("gravtest-" + std::to_string(getpid()) + "-" +
                                  fs::path(base).filename().string())).string();
                tempExe = true;
            }
            if (exePath.empty()) exePath = (emit == EmitMode::Asm) ? base + ".s" : base;
            if (keepC) {
                cPath = base + ".gen.c";
            } else {
                // A unique scratch file in the temp dir: no clutter next to the
                // sources, and parallel builds cannot collide. (Its #line directives
                // name a fixed file instead: see lineName below.)
                std::error_code tec;
                fs::path tmp = fs::temp_directory_path(tec);
                if (tec) tmp = ".";
                cPath = (tmp / ("gravc-" + std::to_string(getpid()) + "-" +
                                fs::path(base).filename().string() + ".c")).string();
                tempC = true;
            }
        }

        // The C names itself in its #line directives. A scratch file (deleted after the
        // build) is named the same in every build, so the same program gives the same C
        // whatever the output and temporary file are called, and the C compiler's caches
        // match it.
        std::string lineName = tempC ? "grav-generated.c" : cPath;
        c = resolveLineResets(c, lineName);
        std::ofstream out(cPath);
        if (!out) { std::cerr << "gravc: cannot write '" << cPath << "'\n"; return 1; }
        out << c;
        out.close();

        if (emit == EmitMode::C) {
            std::cerr << "gravc: wrote " << cPath << "\n";
            return 0;
        }

        // Invoke the system C compiler with any optimization/debug flags.
        const char *ccEnv = std::getenv("CC");
        std::string cc = !ccOverride.empty() ? ccOverride : (ccEnv ? ccEnv : "cc");
        // _DEFAULT_SOURCE exposes POSIX (getcwd, popen, usleep, …) under -std=c11.
        // -fwrapv: signed overflow wraps (two's complement) instead of being UB.
        std::string flags = " -std=c11 -D_DEFAULT_SOURCE -fwrapv -pipe";
        // Optimize by default; an explicit -O wins, and plain -g means "debug me".
        // `grav test` builds for a quick run: -Og compiles about three times as fast as -O2.
        if (optFlag.empty()) optFlag = release ? "-O3" : !debugFlag.empty() ? "-O0" : testMode ? "-Og" : "-O2";
        flags += " " + optFlag;
        if (release) flags += " -DNDEBUG";
        if (native) flags += " -march=native";
        // Checking builds: instrument the binary (keep frames for readable reports).
        if (race) flags += " -fsanitize=thread -g -fno-omit-frame-pointer";
        if (sanitize) flags += " -fsanitize=address,undefined -g -fno-omit-frame-pointer";
        if (race && sanitize) {
            std::cerr << "gravc: --race and --sanitize cannot be combined\n";
            return 1;
        }
        if (!debugFlag.empty()) flags += " " + debugFlag;
        // Link libm so inline-C `<math.h>` users (e.g. lib/mathf.grav) work; on
        // assembly output there is no link step.
        std::string libs = emit == EmitMode::Asm ? "" : " -lm";
        flags += " -pthread"; // async tasks run on a worker thread pool
        // A large program is optimized on every core: GCC splits the one C file into
        // partitions at link time (-flto=auto). Most of a big build is optimizing it.
        // GRAV_LTO=1 forces it, GRAV_LTO=0 turns it off.
        const char *ltoEnv = std::getenv("GRAV_LTO");
        std::string ltoWant = ltoEnv ? ltoEnv : "";
        if (emit != EmitMode::Asm && optFlag != "-O0" && !race && !sanitize && ltoWant != "0") {
            std::error_code sz;
            bool large = fs::file_size(cPath, sz) > (16u << 20) && !sz;
            if ((ltoWant == "1" || large) && grav::ccIsGcc(cc)) {
                flags += " -flto=auto";
                // GCC 15+ keeps each partition it optimized: after an edit, only the
                // partitions holding changed code are optimized again.
                std::string inc = grav::ltoIncrementalDir(cc);
                if (!inc.empty()) flags += " -flto-incremental=\"" + inc + "\"";
            }
        }
        // With GCC's partition cache, the C goes in on stdin: GCC's cache keys include
        // the input file's name, and on stdin it is always the same.
        bool viaStdin = flags.find("-flto-incremental") != std::string::npos;
        // Clang (macOS's cc) warns about patterns the generated C uses on purpose.
        if (!grav::ccIsGcc(cc)) flags += " -Wno-parentheses-equality -Wno-unused-value";
        std::string cmd = cc + flags + (emit == EmitMode::Asm ? " -S" : "") +
                          (viaStdin ? " -x c -" : " \"" + cPath + "\"") + " -o \"" + exePath + "\"" + libs +
                          (viaStdin ? " < \"" + cPath + "\"" : "");
        if (verbose) std::cerr << "gravc: " << cmd << "\n";
        // The same C built the same way before: its binary is reused.
        grav::BuildCache cache(cPath, cc, flags + libs);
        bool reused = emit != EmitMode::Asm && cache.restore(exePath);
        if (reused && verbose) std::cerr << "gravc: reused the cached build of this program\n";
        int rc = reused ? 0 : std::system(cmd.c_str());
        if (rc == 0 && !reused && emit != EmitMode::Asm) cache.store(exePath);
        if (tempC) { std::error_code ec; fs::remove(cPath, ec); }
        if (rc != 0) {
            std::cerr << "gravc: C compilation failed (" << cc << ")\n";
            return 1;
        }
        if (emit == EmitMode::Asm) {
            std::cerr << "gravc: wrote " << exePath << "\n";
            return 0;
        }
        if (whole && emit == EmitMode::Bin) whole->store(exePath);
        // --run: execute the freshly built binary, forwarding any `-- args`.
        return finishBuilt(exePath, tempExe, emit == EmitMode::Both ? cPath : "");
    } catch (const grav::GravError &e) {
        printDiag(e, srcMap);
        return 1;
    }
}
