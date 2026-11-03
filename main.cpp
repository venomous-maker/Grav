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
#include <memory>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <algorithm>
#include <vector>

#include "codegen/codegen.h"
#include "common/diagnostics.h"
#include "lexer/lexer.h"
#include "lexer/preprocess.h"
#include "parser/parser.h"
#include "sema/monomorph.h"
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

// Trim leading whitespace.
std::string ltrim(const std::string &s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}

// Where a line of the combined source originally came from.
struct SrcLoc { std::string file; int line; };

// Recursively load a module, expanding `import "path"` lines (relative to the
// importing file) before the rest of the file. De-duplicates by canonical path
// so a module is included at most once, which also breaks import cycles. Fills
// `srcMap` so combined line N (1-based) maps to its original file:line.
bool loadModule(const fs::path &path, std::set<std::string> &visited,
                std::string &out, std::vector<SrcLoc> &srcMap, std::string &err) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(path, ec);
    std::string key = ec ? path.string() : canon.string();
    if (visited.count(key)) return true;
    visited.insert(key);

    std::ifstream file(path);
    if (!file) {
        err = "cannot open '" + path.string() + "'";
        return false;
    }

    std::string lineText;
    int lineNo = 0;
    while (std::getline(file, lineText)) {
        ++lineNo;
        std::string t = ltrim(lineText);
        if (t.rfind("import", 0) == 0 &&
            (t.size() == 6 || t[6] == ' ' || t[6] == '\t')) {
            auto q1 = t.find('"');
            auto q2 = t.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) {
                err = "malformed import directive: " + lineText;
                return false;
            }
            std::string rel = t.substr(q1 + 1, q2 - q1 - 1);
            fs::path imported = path.parent_path() / rel;
            if (!loadModule(imported, visited, out, srcMap, err)) return false;
            out += "\n"; // keep imported decls separated
            srcMap.push_back({path.string(), lineNo});
        } else {
            out += lineText;
            out += '\n';
            srcMap.push_back({path.string(), lineNo});
        }
    }
    return true;
}

// Print a diagnostic, mapping its combined-source line back to the real file:line
// and showing the offending source line with a caret.
void printDiag(const grav::GravError &d, const std::vector<SrcLoc> &srcMap) {
    std::string label = (d.stage() == "warning") ? "warning" : d.stage() + " error";
    int combined = d.line();
    SrcLoc loc;
    bool mapped = combined >= 1 && combined <= (int)srcMap.size();
    if (mapped) loc = srcMap[combined - 1];
    std::string where = mapped ? (loc.file + ":" + std::to_string(loc.line))
                               : ("line " + std::to_string(combined));
    std::cerr << "gravc: " << where << ":" << d.col() << ": " << label << ": "
              << grav::prettyNames(d.message()) << "\n";
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
    for (const auto &[inst, origin] : all)
        grav::setDisplayName(inst, readable(origin->first, origin->second));
}

int usage(const char *prog) {
    std::cerr <<
        "usage: " << prog << " <input.grav> [options] [-- args...]\n"
        "       " << prog << " run <file.grav> [-- args]   build and run\n"
        "       " << prog << " build <file.grav>           build a binary\n"
        "       " << prog << " test <file.grav> [--filter text]   run the file's test blocks\n"
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
        "  -O0|-O1|-O2|-O3|-Os|-Ofast  optimization level (default: -O2, or -O0 with -g)\n"
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
    if (argc > 1) {
        std::string sub = argv[1];
        if (sub == "run") { runAfter = true; emit = EmitMode::Bin; first = 2; }
        else if (sub == "build") { emit = EmitMode::Bin; first = 2; }
        else if (sub == "test") { testMode = true; runAfter = true; emit = EmitMode::Bin; first = 2; }
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
                   arg == "-Os" || arg == "-Ofast") {
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

    if (!useStdin && inputPath.empty()) return usage(argv[0]);

    // ---- load source (with imports) ----
    std::string source;
    std::vector<SrcLoc> srcMap; // combined line -> original file:line
    if (useStdin) {
        source = readStream(std::cin);
    } else {
        std::set<std::string> visited;
        std::string err;
        if (!loadModule(inputPath, visited, source, srcMap, err)) {
            std::cerr << "gravc: " << err << "\n";
            return 1;
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
        for (int pass = 0; pass < 8; ++pass) {
            grav::Lexer lexer(source);
            auto tokens = lexer.tokenize();
            tokens = grav::preprocess(std::move(tokens));
            grav::Parser parser(std::move(tokens));
            programPtr = std::make_unique<grav::Program>(parser.parseProgram());
            grav::applyInferences(*programPtr, inference);

            // Expand generic structs/functions into concrete instances before symbols.
            errors = grav::monomorphize(*programPtr);
            registerDisplayNames(programPtr->generics);
            // A failed expansion leaves half-made instances; checking them would only
            // bury the real error under follow-on errors from library code.
            if (!errors.empty()) {
                for (const auto &err : errors) printDiag(err, srcMap);
                std::cerr << "gravc: " << errors.size() << " error(s); aborting\n";
                return 1;
            }
            registryPtr = std::make_unique<grav::Registry>();
            auto buildErrors = registryPtr->build(*programPtr);
            errors.insert(errors.end(), buildErrors.begin(), buildErrors.end());

            checkerPtr = std::make_unique<grav::TypeChecker>();
            checkerPtr->setInference(&inference);
            inference.added = 0;
            const auto &typeErrors = checkerPtr->check(*programPtr, *registryPtr);
            errors.insert(errors.end(), typeErrors.begin(), typeErrors.end());
            if (inference.added == 0) break; // nothing new inferred: this pass is final
        }
        grav::Program &program = *programPtr;
        grav::Registry &registry = *registryPtr;
        grav::TypeChecker &checker = *checkerPtr;

        // Warnings (e.g. unused variables). With -Werror they are fatal.
        const auto &warnings = checker.warnings();
        for (const auto &w : warnings) printDiag(w, srcMap);

        if (!errors.empty() || (werror && !warnings.empty())) {
            for (const auto &err : errors) printDiag(err, srcMap);
            size_t n = errors.size() + (werror ? warnings.size() : 0);
            std::cerr << "gravc: " << n << " error(s); aborting\n";
            return 1;
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
                // sources, and parallel builds of the same file cannot collide.
                std::error_code tec;
                fs::path tmp = fs::temp_directory_path(tec);
                if (tec) tmp = ".";
                cPath = (tmp / ("gravc-" + std::to_string(getpid()) + "-" +
                                fs::path(base).filename().string() + ".c")).string();
                tempC = true;
            }
        }

        c = resolveLineResets(c, cPath);
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
        std::string flags = " -std=c11 -D_DEFAULT_SOURCE -pipe";
        // Optimize by default; an explicit -O wins, and plain -g means "debug me".
        if (optFlag.empty()) optFlag = release ? "-O3" : (debugFlag.empty() ? "-O2" : "-O0");
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
        std::string cmd = cc + flags + (emit == EmitMode::Asm ? " -S" : "") +
                          " \"" + cPath + "\" -o \"" + exePath + "\"" + libs;
        if (verbose) std::cerr << "gravc: " << cmd << "\n";
        int rc = std::system(cmd.c_str());
        if (tempC) { std::error_code ec; fs::remove(cPath, ec); }
        if (rc != 0) {
            std::cerr << "gravc: C compilation failed (" << cc << ")\n";
            return 1;
        }
        if (emit == EmitMode::Asm) {
            std::cerr << "gravc: wrote " << exePath << "\n";
            return 0;
        }
        if (!tempExe)
            std::cerr << "gravc: built " << exePath
                      << (emit == EmitMode::Both ? (" (and wrote " + cPath + ")") : "")
                      << "\n";

        // --run: execute the freshly built binary, forwarding any `-- args`.
        if (runAfter) {
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
        }
        return 0;
    } catch (const grav::GravError &e) {
        printDiag(e, srcMap);
        return 1;
    }
}
