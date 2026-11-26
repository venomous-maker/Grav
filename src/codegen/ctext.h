#ifndef GRAV_CODEGEN_CTEXT_H
#define GRAV_CODEGEN_CTEXT_H

// Reading the generated C as text (shared by the passes that work on it before the
// C compiler: prune, fold): identifiers, which lines define or declare functions,
// and where a block ends.

#include <string>
#include <string_view>

namespace grav::ctext {

inline bool identStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool identChar(char c) { return identStart(c) || (c >= '0' && c <= '9') || c == '$'; }

inline std::string_view trimRight(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

inline bool startsWithWord(std::string_view line, std::string_view w) {
    return line.size() > w.size() && line.compare(0, w.size(), w) == 0 && (line[w.size()] == ' ' || line[w.size()] == '\t');
}

// For a line `static|GRAV_FN <type> name(<params>) {` (a definition) or `...);` (a
// prototype): the function's name, and which it is. Data (`= {`) is neither.
enum class LineKind { Other, Definition, Prototype };
inline LineKind classify(std::string_view line, std::string_view &name) {
    if (!startsWithWord(line, "static") && !startsWithWord(line, "GRAV_FN")) return LineKind::Other;
    // (`__attribute__((constructor))` and the like run without being named: kept)
    if (line.find("__attribute__") != std::string_view::npos) return LineKind::Other;
    std::string_view t = trimRight(line);
    bool def = !t.empty() && t.back() == '{';
    bool proto = !t.empty() && t.back() == ';';
    if (!def && !proto) return LineKind::Other;
    size_t paren = t.find('(');
    if (paren == std::string_view::npos) return LineKind::Other;
    // `=` before the parameters means data (`static const T x = {`, `... = f(...);`).
    if (t.substr(0, paren).find('=') != std::string_view::npos) return LineKind::Other;
    size_t end = paren;
    while (end > 0 && (t[end - 1] == ' ' || t[end - 1] == '\t')) --end;
    size_t start = end;
    while (start > 0 && identChar(t[start - 1])) --start;
    if (start == end || !identStart(t[start])) return LineKind::Other;
    // (a function pointer variable, `static int (*f)(int);`, has `(` right before its name)
    if (start > 0 && t[start - 1] == '*') return LineKind::Other;
    name = t.substr(start, end - start);
    if (def) return LineKind::Definition;
    // A prototype ends `...);`: its last `)` closes the parameter list.
    return t.size() >= 2 && t[t.size() - 2] == ')' ? LineKind::Prototype : LineKind::Other;
}

// The end of the block whose `{` is at `open` (just past its `}`), skipping strings,
// characters and comments; npos when it does not close.
inline size_t blockEnd(const std::string &c, size_t open) {
    int depth = 0;
    for (size_t i = open; i < c.size(); ++i) {
        char ch = c[i];
        if (ch == '"' || ch == '\'') {
            for (++i; i < c.size() && c[i] != ch; ++i)
                if (c[i] == '\\') ++i;
        } else if (ch == '/' && i + 1 < c.size() && c[i + 1] == '/') {
            while (i < c.size() && c[i] != '\n') ++i;
        } else if (ch == '/' && i + 1 < c.size() && c[i + 1] == '*') {
            size_t e = c.find("*/", i + 2);
            if (e == std::string::npos) return std::string::npos;
            i = e + 1;
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            if (--depth == 0) return i + 1;
        }
    }
    return std::string::npos;
}

} // namespace grav::ctext

#endif // GRAV_CODEGEN_CTEXT_H
