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
#include "parser/parser.h"
#include "sema/symbols.h"
#include "sema/typechecker.h"

namespace fs = std::filesystem;

namespace {

enum class EmitMode { C, Bin, Both };

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
    std::cerr << "usage: " << prog
              << " <input.grav> [-o out] [--emit c|bin|both] [-Werror]\n"
              << "       " << prog << " -    (read stdin, write C to stdout)\n";
    return 2;
}

std::string stem(const std::string &input) {
    auto dot = input.find_last_of('.');
    return dot == std::string::npos ? input : input.substr(0, dot);
}

} // namespace

int main(int argc, char **argv) {
    std::string inputPath, outputPath;
    bool useStdin = false, toStdout = false, werror = false;
    EmitMode emit = EmitMode::C;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc) return usage(argv[0]);
            outputPath = argv[++i];
        } else if (arg == "--emit") {
            if (i + 1 >= argc) return usage(argv[0]);
            std::string m = argv[++i];
            if (m == "c") emit = EmitMode::C;
            else if (m == "bin") emit = EmitMode::Bin;
            else if (m == "both") emit = EmitMode::Both;
            else { std::cerr << "gravc: unknown --emit mode '" << m << "'\n"; return usage(argv[0]); }
        } else if (arg == "-Werror") {
            werror = true;
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

        grav::Parser parser(std::move(tokens));
        grav::Program program = parser.parseProgram();

        grav::Registry registry;
        std::vector<grav::GravError> errors = registry.build(program);

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
        std::string cPath = outputPath;
        std::string exePath = outputPath;
        if (emit == EmitMode::C) {
            if (cPath.empty()) cPath = base + ".c";
        } else if (emit == EmitMode::Bin) {
            if (exePath.empty()) exePath = base;
            cPath = exePath + ".gen.c"; // temporary
        } else { // Both
            if (exePath.empty()) exePath = base;
            cPath = base + ".c";
        }

        std::ofstream out(cPath);
        if (!out) { std::cerr << "gravc: cannot write '" << cPath << "'\n"; return 1; }
        out << c;
        out.close();

        if (emit == EmitMode::C) {
            std::cerr << "gravc: wrote " << cPath << "\n";
            return 0;
        }

        // Compile the generated C with the system compiler.
        const char *ccEnv = std::getenv("CC");
        std::string cc = ccEnv ? ccEnv : "cc";
        std::string cmd = cc + " -std=c11 \"" + cPath + "\" -o \"" + exePath + "\"";
        int rc = std::system(cmd.c_str());
        if (emit == EmitMode::Bin) {
            std::error_code ec;
            fs::remove(cPath, ec); // drop the temporary C file
        }
        if (rc != 0) {
            std::cerr << "gravc: C compilation failed (" << cc << ")\n";
            return 1;
        }
        std::cerr << "gravc: built " << exePath
                  << (emit == EmitMode::Both ? (" (and wrote " + cPath + ")") : "")
                  << "\n";
        return 0;
    } catch (const grav::GravError &e) {
        std::cerr << "gravc: " << e.what() << "\n";
        return 1;
    }
}
