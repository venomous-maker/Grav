// `grav fmt` — lays out and re-indents Grav source.
//
// The first pass (format_layout.cpp) spaces each line's code and puts the
// statements of a `{ a; b }` block on lines of their own. This one
//   - indents with 4 spaces per open bracket level, where brackets opened on the
//     same line count once (so `spawn(fn() {` indents its body one level);
//   - indents a continuation line (one inside an earlier line's `(` or `[`, or) one
//     extra level after a line ending in a binary
//     operator, `:` or `=>`, and every line of a case body after a bare
//     `case ...:` / `default:` line, up to the next case;
//   - strips trailing whitespace, turns tabs in indentation into spaces, keeps at
//     most one blank line in a row, uses "\n" line ends and ends with a newline.
// Strings (with `${...}`, and `"""` ones line by line), comments and inline C (`%{ ... %}`) are left as they
// are; lines inside multi-line inline C and block comments keep their own text.
#include "driver/format.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace grav {

namespace {

struct Open {
    char ch;
    int lineLevel; // indent level of the line that opened it
};

struct Scanner {
    std::vector<Open> stack;
    bool inBlockComment = false;
    bool inCBlock = false;
    bool inTriple = false; // inside a `"""` string, whose lines are its text

    static bool isCloser(char c) { return c == '}' || c == ')' || c == ']'; }

    // Scan one line, updating the bracket stack. `level` is the line's indent.
    // Returns the line's code with strings/comments blanked, for the
    // "ends with an operator" test.
    // Brackets opened on this line remember `openLevel`; a bracket from an earlier
    // line closed mid-line lowers it (`if (a &&\n    b) {` indents its body once).
    std::string scan(const std::string &line, int openLevel) {
        int level = openLevel;
        std::string code;
        size_t i = 0, n = line.size();
        while (i < n) {
            char c = line[i];
            if (inBlockComment) {
                if (c == '*' && i + 1 < n && line[i + 1] == '/') { inBlockComment = false; i += 2; }
                else ++i;
                continue;
            }
            if (inTriple) {
                size_t e = line.find("\"\"\"", i);
                if (e == std::string::npos) return code;
                inTriple = false;
                i = e + 3;
                code += "\"\"";
                continue;
            }
            if (inCBlock) {
                if (c == '%' && i + 1 < n && line[i + 1] == '}') { inCBlock = false; i += 2; code += " C "; }
                else ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && line[i + 1] == '/') break;            // line comment
            if (c == '/' && i + 1 < n && line[i + 1] == '*') { inBlockComment = true; i += 2; continue; }
            if (c == '%' && i + 1 < n && line[i + 1] == '{') { inCBlock = true; i += 2; code += " C "; continue; }
            if (line.compare(i, 3, "\"\"\"") == 0) { inTriple = true; i += 3; code += "\"\""; continue; }
            if (c == '"') { i = skipString(line, i + 1); code += "\"\""; continue; }
            if (c == '\'') {                                                   // a char literal
                size_t j = i + 1;
                while (j < n && line[j] != '\'') j += line[j] == '\\' ? 2 : 1;
                i = j + 1;
                code += ' ';
                continue;
            }
            if (c == '{' || c == '(' || c == '[') stack.push_back({c, level});
            else if (isCloser(c) && !stack.empty()) {
                if (stack.back().lineLevel < level) level = stack.back().lineLevel; // closed an earlier line's bracket
                stack.pop_back();
            }
            code += c;
            ++i;
        }
        return code;
    }

    // Past the closing quote of a string starting after `i`'s quote; `${...}`
    // interpolations may contain strings of their own.
    size_t skipString(const std::string &s, size_t i) {
        int interp = 0;
        while (i < s.size()) {
            char c = s[i];
            if (interp == 0) {
                if (c == '\\') { i += 2; continue; }
                if (c == '"') return i + 1;
                if (c == '$' && i + 1 < s.size() && s[i + 1] == '{') { interp = 1; i += 2; continue; }
                ++i;
            } else {
                if (c == '"') { i = skipString(s, i + 1); continue; }
                if (c == '{') ++interp;
                else if (c == '}') --interp;
                ++i;
            }
        }
        return i; // unterminated: the rest of the line
    }
};

std::string rtrim(const std::string &s) {
    size_t e = s.find_last_not_of(" \t\r");
    return e == std::string::npos ? "" : s.substr(0, e + 1);
}

bool endsWithContinuation(const std::string &code) {
    std::string t = rtrim(code);
    if (t.empty()) return false;
    static const char *ops[] = {"&&", "||", "=>", "==", "!=", "<=", ">=", "+", "-", "*", "/", "%",
                                "&", "|", "^", "=", "<", ">", "?", ":"};
    for (const char *op : ops) {
        std::string o = op;
        if (t.size() < o.size() || t.compare(t.size() - o.size(), o.size(), o) != 0) continue;
        if (o == "+" && t.size() >= 2 && t[t.size() - 2] == '+') return false; // x++
        if (o == "-" && t.size() >= 2 && t[t.size() - 2] == '-') return false; // x--
        if (o == ">" && t.size() >= 2 && t[t.size() - 2] == '-') return false; // -> (a return type follows)
        if (o == ">" && t.find('<') != std::string::npos) return false;        // a generic close: Array<T>
        if (o == "*" && t.size() >= 2 && t[t.size() - 2] == '/') return false; // end of /* */
        if (o == "*" && t.size() >= 2 && t[t.size() - 2] != ' ') return false; // T*, use a.*
        if (o == "&" && t.size() >= 2 && t[t.size() - 2] != ' ' && t[t.size() - 2] != '&') return false;
        if (o == "?" && t.size() >= 2 && t[t.size() - 2] != ' ') return false; // T? (a nullable type), not `c ? a`
        return true;
    }
    return false;
}

} // namespace

std::string formatGrav(const std::string &source) {
    std::string src = layoutGrav(source);
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : src) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    Scanner sc;
    std::string out;
    bool prevBlank = true, continuation = false;
    std::vector<long> cases; // bracket depths of case bodies begun by a bare `case ...:` line (nested switches)
    for (const std::string &raw : lines) {
        std::string line = rtrim(raw);
        bool verbatim = sc.inCBlock || sc.inBlockComment || sc.inTriple; // inside multi-line C, a comment or a """ string
        if (verbatim) {
            sc.scan(line, 0);
            out += line + "\n";
            prevBlank = false;
            continue;
        }
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            if (!prevBlank) out += "\n";
            prevBlank = true;
            continue;
        }
        std::string body = line.substr(first);
        // The level: the innermost open bracket's line + 1, or that bracket's own
        // line level when this line starts by closing it.
        int level = sc.stack.empty() ? 0 : sc.stack.back().lineLevel + 1;
        size_t k = 0;
        std::vector<Open> probe = sc.stack;
        while (k < body.size() && Scanner::isCloser(body[k]) && !probe.empty()) {
            level = probe.back().lineLevel;
            probe.pop_back();
            ++k;
            while (k < body.size() && body[k] == ' ') ++k;
        }
        // A case body's lines sit one level in, until the next case or its switch's `}`.
        bool label = body.compare(0, 5, "case ") == 0 || body.compare(0, 7, "default") == 0;
        while (!cases.empty() && static_cast<long>(probe.size()) < cases.back()) cases.pop_back();
        if (label && !cases.empty() && static_cast<long>(sc.stack.size()) == cases.back()) cases.pop_back();
        bool inCase = !cases.empty() && static_cast<long>(sc.stack.size()) == cases.back();
        if (inCase) { level += 1; continuation = false; }
        // A line that starts with `.` continues a method chain; one that starts with
        // `?`, `:`, `&&`, `||` or `??`, an expression.
        bool chain = (body.size() > 1 && body[0] == '.' && body[1] != '.') ||
                     ((body[0] == '?' || body[0] == ':') && body.compare(0, 2, "::") != 0) ||
                     body.compare(0, 2, "&&") == 0 || body.compare(0, 2, "||") == 0;
        // Inside ( ) or [ ] the bracket already indents continuation lines.
        bool inParens = !sc.stack.empty() && sc.stack.back().ch != '{';
        bool bumped = (continuation || chain) && k == 0 && !inParens;
        if (bumped) level += 1;
        // Brackets opened on a continuation line belong to the statement's level.
        std::string code = sc.scan(body, bumped ? level - 1 : level);
        // A continuation line set further in (lined up under an open bracket) stays there.
        size_t indent = static_cast<size_t>(level) * 4;
        if ((inParens || bumped) && k == 0 && first > indent) indent = first;
        out += std::string(indent, ' ') + body + "\n";
        continuation = endsWithContinuation(code);
        if (label && !rtrim(code).empty() && rtrim(code).back() == ':') {
            cases.push_back(static_cast<long>(sc.stack.size()));
            continuation = false;
        }
        prevBlank = false;
    }
    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out;
}

namespace {

void collect(const fs::path &p, std::vector<fs::path> &out) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        for (auto it = fs::recursive_directory_iterator(p, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::string name = it->path().filename().string();
            if (it->is_directory() && (name.rfind('.', 0) == 0 || name == "build" || name.rfind("cmake-build", 0) == 0)) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && it->path().extension() == ".grav") out.push_back(it->path());
        }
    } else {
        out.push_back(p);
    }
}

} // namespace

int runFormat(const std::vector<std::string> &args) {
    bool check = false;
    std::vector<std::string> targets;
    for (const auto &a : args) {
        if (a == "--check") check = true;
        else if (a == "-h" || a == "--help") {
            std::cerr << "usage: grav fmt [--check] [files or directories...]\n"
                         "       grav fmt -          format stdin to stdout\n"
                         "Without arguments, formats the project (or the current directory).\n"
                         "--check lists files that are not formatted and exits 1, changing nothing.\n";
            return 0;
        } else targets.push_back(a);
    }
    if (targets.size() == 1 && targets[0] == "-") {
        std::stringstream ss;
        ss << std::cin.rdbuf();
        std::cout << formatGrav(ss.str());
        return 0;
    }
    if (targets.empty()) targets.push_back(".");
    std::vector<fs::path> files;
    for (const auto &t : targets) collect(t, files);
    int unformatted = 0;
    for (const auto &f : files) {
        std::ifstream in(f, std::ios::binary);
        if (!in) { std::cerr << "grav fmt: cannot read " << f.string() << "\n"; return 2; }
        std::stringstream ss;
        ss << in.rdbuf();
        std::string before = ss.str(), after = formatGrav(before);
        if (before == after) continue;
        ++unformatted;
        if (check) { std::cout << f.string() << "\n"; continue; }
        std::ofstream(f, std::ios::binary) << after;
        std::cerr << "formatted " << f.string() << "\n";
    }
    if (check && unformatted) std::cerr << "grav fmt: " << unformatted << " file(s) need formatting\n";
    return check && unformatted ? 1 : 0;
}

} // namespace grav
