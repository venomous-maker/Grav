// Applying lint fixes (see lintfix.h).
#include "driver/lintfix.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace grav {

namespace {

std::vector<std::string> readLines(const std::string &path, bool &endsWithNewline) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string all = ss.str();
    endsWithNewline = !all.empty() && all.back() == '\n';
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= all.size()) {
        size_t nl = all.find('\n', start);
        if (nl == std::string::npos) {
            if (start < all.size()) lines.push_back(all.substr(start));
            break;
        }
        lines.push_back(all.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

bool isOperatorChar(char c) { return std::string("=!<>&|+-*/%^?:").find(c) != std::string::npos; }

// A binary operator's precedence (higher binds tighter); 0 for anything else
// (assignment, `?:`, a bracket, a comma).
int precedence(const std::string &op) {
    static const std::map<std::string, int> p = {
        {"||", 1}, {"&&", 2}, {"|", 3}, {"^", 4}, {"&", 5}, {"==", 6}, {"!=", 6}, {"<", 7}, {">", 7},
        {"<=", 7}, {">=", 7}, {"<<", 8}, {">>", 8}, {"+", 9}, {"-", 9}, {"*", 10}, {"/", 10}, {"%", 10}};
    auto it = p.find(op);
    return it == p.end() ? 0 : it->second;
}
// The loosest operator outside brackets and quotes in `text` (0: none, as in `f(a, b)` or `!x`).
int loosest(const std::string &text) {
    int depth = 0, low = 0;
    bool quote = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '"' && (i == 0 || text[i - 1] != '\\')) quote = !quote;
        if (quote) continue;
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if (depth == 0 && c == ' ' && i + 1 < text.size() && isOperatorChar(text[i + 1])) {
            size_t j = i + 1;
            while (j < text.size() && isOperatorChar(text[j])) ++j;
            int pr = precedence(text.substr(i + 1, j - i - 1));
            if (pr && (!low || pr < low)) low = pr;
        }
    }
    return low;
}
// The operator just before `before` ends / just after `after` starts ("" if none).
std::string opBefore(const std::string &before) {
    size_t e = before.find_last_not_of(' ');
    if (e == std::string::npos || !isOperatorChar(before[e])) return "";
    size_t b = e;
    while (b > 0 && isOperatorChar(before[b - 1])) --b;
    return before.substr(b, e - b + 1);
}
std::string opAfter(const std::string &after) {
    size_t b = after.find_first_not_of(' ');
    if (b == std::string::npos || !isOperatorChar(after[b])) return "";
    size_t e = b;
    while (e + 1 < after.size() && isOperatorChar(after[e + 1])) ++e;
    return after.substr(b, e - b + 1);
}
// Whether `text`, put between `before` and `after`, needs brackets to keep its meaning.
bool needsBrackets(const std::string &before, const std::string &text, const std::string &after) {
    int mine = loosest(text);
    if (!mine) return false;
    std::string l = opBefore(before), r = opAfter(after);
    if (l == "!" || l == "-" || l == "~") return true;                     // a unary operator applies to all of it
    int lp = precedence(l), rp = precedence(r);
    return (lp && lp >= mine) || (rp && rp > mine);                        // (left-associative)
}

std::string backupName(const std::string &file) {
    std::string n = file;
    for (char &c : n) if (c == '/' || c == '\\' || c == ':') c = '_';
    return n;
}

} // namespace

int applyLintEdits(std::vector<LintEdit> edits, const std::string &backupDir, std::vector<std::string> &report) {
    std::map<std::string, std::vector<LintEdit>> byFile;
    for (auto &e : edits) byFile[e.file].push_back(e);
    int applied = 0;
    for (auto &[file, list] : byFile) {
        bool trailingNewline = true;
        std::vector<std::string> lines = readLines(file, trailingNewline);
        // in source order; one that overlaps an earlier one waits for the next round
        std::stable_sort(list.begin(), list.end(), [](const LintEdit &a, const LintEdit &b) {
            return a.line != b.line ? a.line < b.line : a.col < b.col;
        });
        std::vector<LintEdit> keep;
        int lastLine = 0, lastCol = 0;
        for (auto &e : list) {
            int sl = e.line, sc = e.deleteLine ? 1 : e.col;
            int el = e.deleteLine ? e.line + 1 : e.endLine, ec = e.deleteLine ? 1 : e.endCol;
            if (sl < 1 || sl > (int)lines.size() || el < sl || (el == sl && ec < sc)) continue;
            if (sl < lastLine || (sl == lastLine && sc < lastCol)) continue;
            if (!e.deleteLine && (el > (int)lines.size() || sc > (int)lines[sl - 1].size() + 1 || ec > (int)lines[el - 1].size() + 1)) continue;
            keep.push_back(e);
            lastLine = el; lastCol = ec;
        }
        if (keep.empty()) continue;
        if (!backupDir.empty()) {
            fs::create_directories(backupDir);
            fs::path b = fs::path(backupDir) / backupName(file);
            if (!fs::exists(b)) {
                std::ofstream o(b.string() + ".path");
                o << file;
                fs::copy_file(file, b);
            }
        }
        // from the end backwards, so earlier positions stay valid
        for (auto it = keep.rbegin(); it != keep.rend(); ++it) {
            const LintEdit &e = *it;
            if (e.deleteLine) {
                lines.erase(lines.begin() + (e.line - 1));
            } else {
                std::string &first = lines[e.line - 1];
                std::string before = first.substr(0, e.col - 1);
                std::string after = lines[e.endLine - 1].substr(e.endCol - 1);
                // (an operator next to it binds tighter than the new text: brackets keep the meaning)
                std::string text = e.text;
                if (needsBrackets(before, text, after)) text = "(" + text + ")";
                std::string merged = before + text + after;
                lines.erase(lines.begin() + (e.line - 1), lines.begin() + e.endLine);
                lines.insert(lines.begin() + (e.line - 1), merged);
            }
            report.push_back(file + ":" + std::to_string(e.line) + ": " + e.lint);
            applied++;
        }
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        for (size_t i = 0; i < lines.size(); ++i) {
            out << lines[i];
            if (i + 1 < lines.size() || trailingNewline) out << '\n';
        }
    }
    return applied;
}

int restoreLintBackups(const std::string &backupDir) {
    int n = 0;
    std::error_code ec;
    if (!fs::exists(backupDir, ec)) return 0;
    for (auto &entry : fs::directory_iterator(backupDir)) {
        std::string p = entry.path().string();
        if (p.size() > 5 && p.substr(p.size() - 5) == ".path") continue;
        std::ifstream in(p + ".path");
        std::string original;
        std::getline(in, original);
        if (original.empty()) continue;
        fs::copy_file(p, original, fs::copy_options::overwrite_existing, ec);
        if (!ec) n++;
    }
    return n;
}

} // namespace grav
