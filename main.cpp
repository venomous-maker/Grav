// gravc — the Grav (v0.2, OOP + namespaces) compiler driver.
//
// Usage:
//   gravc <input.grav> [-o out] [--emit c|bin|both] [-Werror]
//   gravc -            # read source from stdin, write C to stdout
//
// Pipeline: load (with imports) -> lex -> parse -> build symbols ->
//           type-check -> generate C -> optionally compile to a binary.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "codegen/codegen.h"
#include "common/diagnostics.h"
#include "lexer/lexer.h"
#include "lexer/preprocess.h"
#include "parser/parser.h"
#include "sema/monomorph.h"
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

// Recursively load a module, expanding `import "path"` lines (relative to the
// importing file) before the rest of the file. De-duplicates by canonical path
// so a module is included at most once, which also breaks import cycles.
bool loadModule(const fs::path &path, std::set<std::string> &visited,
                std::string &out, std::string &err) {
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
    while (std::getline(file, lineText)) {
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
            if (!loadModule(imported, visited, out, err)) return false;
            out += "\n"; // keep imported decls separated
        } else {
            out += lineText;
            out += '\n';
        }
    }
    return true;
}

int usage(const char *prog) {
    std::cerr <<
        "usage: " << prog << " <input.grav> [options] [-- args...]\n"
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
        "  -O0|-O1|-O2|-O3|-Os  optimization level (default: none)\n"
        "  -g | -g3             include debug info\n"
        "  --cc <compiler>      C compiler to use (default: $CC or cc)\n"
        "\n"
        "diagnostics:\n"
        "  -Werror              treat warnings as errors\n"
        "  -v, --verbose        print the C compiler command\n"
        "  -h, --help           show this help\n";
    return 2;
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
    std::vector<std::string> runArgs;
    EmitMode emit = EmitMode::C;

    for (int i = 1; i < argc; ++i) {
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
    if (useStdin) {
        source = readStream(std::cin);
    } else {
        std::set<std::string> visited;
        std::string err;
        if (!loadModule(inputPath, visited, source, err)) {
            std::cerr << "gravc: " << err << "\n";
            return 1;
        }
    }

    try {
        grav::Lexer lexer(source);
        auto tokens = lexer.tokenize();
        tokens = grav::preprocess(std::move(tokens));

        grav::Parser parser(std::move(tokens));
        grav::Program program = parser.parseProgram();

        // Expand generic structs/functions into concrete instances before symbols.
        std::vector<grav::GravError> errors = grav::monomorphize(program);

        grav::Registry registry;
        auto buildErrors = registry.build(program);
        errors.insert(errors.end(), buildErrors.begin(), buildErrors.end());

        grav::TypeChecker checker;
        const auto &typeErrors = checker.check(program, registry);
        errors.insert(errors.end(), typeErrors.begin(), typeErrors.end());

        // Warnings (e.g. unused variables). With -Werror they are fatal.
        const auto &warnings = checker.warnings();
        for (const auto &w : warnings)
            std::cerr << "gravc: " << w.what() << "\n";

        if (!errors.empty() || (werror && !warnings.empty())) {
            for (const auto &err : errors) std::cerr << "gravc: " << err.what() << "\n";
            size_t n = errors.size() + (werror ? warnings.size() : 0);
            std::cerr << "gravc: " << n << " error(s); aborting\n";
            return 1;
        }

        grav::CodeGen codegen;
        std::string c = codegen.generate(program, registry);

        if (toStdout) {
            std::cout << c;
            return 0;
        }

        // Decide output paths.
        std::string base = stem(inputPath);
        std::string cPath, exePath = outputPath;
        bool tempC = false; // the .c is an intermediate to delete afterward
        if (emit == EmitMode::C) {
            cPath = outputPath.empty() ? base + ".c" : outputPath;
        } else if (emit == EmitMode::Both) {
            if (exePath.empty()) exePath = base;
            cPath = base + ".c";
        } else { // Asm or Bin: the .c is intermediate unless --keep-c
            if (exePath.empty()) exePath = (emit == EmitMode::Asm) ? base + ".s" : base;
            cPath = base + ".gen.c";
            tempC = !keepC;
        }

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
        std::string flags = " -std=c11";
        if (!optFlag.empty()) flags += " " + optFlag;
        if (!debugFlag.empty()) flags += " " + debugFlag;
        // Link libm so inline-C `<math.h>` users (e.g. lib/mathf.grav) work; on
        // assembly output there is no link step.
        std::string libs = emit == EmitMode::Asm ? "" : " -lm";
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
            int prc = std::system(runCmd.c_str());
            if (emit == EmitMode::Bin && tempC) { /* binary kept; nothing to clean */ }
            return prc == 0 ? 0 : 1;
        }
        return 0;
    } catch (const grav::GravError &e) {
        std::cerr << "gravc: " << e.what() << "\n";
        return 1;
    }
}
