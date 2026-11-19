#include "codegen/prune.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace grav {

namespace {

bool identStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool identChar(char c) { return identStart(c) || (c >= '0' && c <= '9') || c == '$'; }

std::string_view trimRight(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool startsWithWord(std::string_view line, std::string_view w) {
    return line.size() > w.size() && line.compare(0, w.size(), w) == 0 && (line[w.size()] == ' ' || line[w.size()] == '\t');
}

// For a line `static|GRAV_FN <type> name(<params>) {` (a definition) or `...);` (a
// prototype): the function's name, and which it is. Data (`= {`) is neither.
enum class LineKind { Other, Definition, Prototype };
LineKind classify(std::string_view line, std::string_view &name) {
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
size_t blockEnd(const std::string &c, size_t open) {
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

struct Fn {
    size_t from, to;      // the definition: its first line up to the end of its last
    size_t bodyFrom;      // after the header line
    bool reached = false;
};

} // namespace

std::string pruneUnreachable(const std::string &c) {
    std::vector<Fn> fns;
    std::unordered_map<std::string_view, size_t> byName;
    std::vector<std::pair<size_t, size_t>> rootText;            // ranges outside definitions and prototypes
    std::vector<std::pair<std::pair<size_t, size_t>, std::string_view>> protos;

    // 1) Definitions and prototypes, line by line at the top level.
    size_t rootFrom = 0;
    for (size_t pos = 0; pos < c.size();) {
        size_t eol = c.find('\n', pos);
        size_t next = eol == std::string::npos ? c.size() : eol + 1;
        std::string_view line(c.data() + pos, (eol == std::string::npos ? c.size() : eol) - pos);
        std::string_view name;
        LineKind k = classify(line, name);
        if (k == LineKind::Definition && !byName.count(name)) {
            size_t open = pos + trimRight(line).size() - 1;
            size_t end = blockEnd(c, open);
            if (end != std::string::npos) {
                size_t lineEnd = c.find('\n', end);
                lineEnd = lineEnd == std::string::npos ? c.size() : lineEnd + 1;
                // Only when the closing brace ends its line: then the whole text is the function.
                if (trimRight(std::string_view(c.data() + end, lineEnd - end)).find_first_not_of(" \t\n") == std::string_view::npos) {
                    if (pos > rootFrom) rootText.push_back({rootFrom, pos});
                    byName.emplace(name, fns.size());
                    fns.push_back({pos, lineEnd, next});
                    pos = rootFrom = lineEnd;
                    continue;
                }
            }
        } else if (k == LineKind::Prototype) {
            if (pos > rootFrom) rootText.push_back({rootFrom, pos});
            protos.push_back({{pos, next}, name});
            rootFrom = next;
        }
        pos = next;
    }
    if (rootFrom < c.size()) rootText.push_back({rootFrom, c.size()});
    if (fns.empty()) return c;

    // 2) Reach: names mentioned in the roots, then in the bodies of what they reach.
    std::vector<size_t> work;
    auto mention = [&](size_t from, size_t to) {
        for (size_t i = from; i < to;) {
            if (!identStart(c[i]) || (i > from && identChar(c[i - 1]))) { ++i; continue; }
            size_t j = i + 1;
            while (j < to && identChar(c[j])) ++j;
            auto it = byName.find(std::string_view(c.data() + i, j - i));
            if (it != byName.end() && !fns[it->second].reached) {
                fns[it->second].reached = true;
                work.push_back(it->second);
            }
            i = j;
        }
    };
    for (const auto &[from, to] : rootText) mention(from, to);
    while (!work.empty()) {
        size_t f = work.back();
        work.pop_back();
        mention(fns[f].bodyFrom, fns[f].to);
    }

    size_t dropped = 0;
    for (const auto &f : fns) dropped += f.reached ? 0 : 1;
    if (dropped == 0) return c;

    // 3) The C without them (and without their prototypes).
    std::vector<std::pair<size_t, size_t>> cut;
    for (const auto &f : fns) if (!f.reached) cut.push_back({f.from, f.to});
    for (const auto &[range, name] : protos) {
        auto it = byName.find(name);
        if (it != byName.end() && !fns[it->second].reached) cut.push_back(range);
    }
    std::sort(cut.begin(), cut.end());
    std::string out;
    out.reserve(c.size());
    size_t at = 0;
    for (const auto &[from, to] : cut) {
        if (from < at) continue;
        out.append(c, at, from - at);
        at = to;
    }
    out.append(c, at, std::string::npos);
    return out;
}

} // namespace grav
