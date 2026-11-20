// `grav fmt`'s first pass: spacing within lines, and blocks laid out one statement
// per line. It works on tokens, so strings, comments and inline C are never
// touched, and it only adds line breaks where a statement already ends:
//   - one space around binary operators, `=`, `->`, `=>` and `??`, after `,` and
//     `:` (and before a ternary's `:`), between a keyword and its `(`, and inside
//     `{ ... }` on one line; none inside `( )` and `[ ]`, around `.`, `..`, `?.`
//     and `::`, after a prefix operator, or before a call's `(`, an index's `[`,
//     `,`, `;` or a postfix `!`, `?`, `++`;
//   - `<` and `>` in type arguments, an operator method's name (`operator[]=`),
//     preprocessor lines, and gaps of two spaces or more (alignment) are kept as
//     they are;
//   - a `{ a; b }` on one line becomes one statement per line (the `;`s dropped
//     where a line break ends the statement as well), a `{` followed on its line
//     by code that runs past the line gets a line break after it, and a `}`
//     closing an earlier line's `{` after other code gets one before it.
// The second pass (formatGrav) then indents every line.
#include "driver/format.h"

#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace grav {

namespace {

enum class K { Word, Num, Str, Comment, CBlock, Punct, Newline };

struct Tok {
    K k;
    std::string text;
    std::string ws; // the whitespace before it, as written
};

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Past a string's closing quote, from just after its opening one; `${...}` may
// hold strings of its own.
size_t skipString(const std::string &s, size_t i) {
    int interp = 0;
    while (i < s.size()) {
        char c = s[i];
        if (interp == 0) {
            if (c == '\\') { i += 2; continue; }
            if (c == '"' ) return i + 1;
            if (c == '\n') return i; // unterminated
            if (c == '$' && i + 1 < s.size() && s[i + 1] == '{') { interp = 1; i += 2; continue; }
            ++i;
        } else {
            if (c == '"') { i = skipString(s, i + 1); continue; }
            if (c == '{') ++interp;
            else if (c == '}') --interp;
            ++i;
        }
    }
    return i;
}

std::vector<Tok> lex(const std::string &src) {
    static const char *puncts[] = {"...", "..=", "<<=", ">>=", "..", "::", "?.", "??", "->", "=>", "==", "!=",
                                   "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=",
                                   "|=", "^=", "<<", ">>", "**"};
    std::vector<Tok> out;
    std::string ws;
    size_t i = 0, n = src.size();
    auto push = [&](K k, size_t from, size_t to) {
        out.push_back({k, src.substr(from, to - from), ws});
        ws.clear();
    };
    while (i < n) {
        char c = src[i];
        if (c == '\n') { ws.clear(); out.push_back({K::Newline, "\n", ""}); ++i; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { ws += c == '\t' ? "    " : (c == ' ' ? " " : ""); ++i; continue; }
        size_t s = i;
        if (c == '#' && (out.empty() || out.back().k == K::Newline)) { // a preprocessor line, as written
            while (i < n && (src[i] != '\n' || src[i - 1] == '\\')) ++i;
            push(K::CBlock, s, i);
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') ++i;
            push(K::Comment, s, i);
        } else if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            size_t e = src.find("*/", i + 2);
            i = e == std::string::npos ? n : e + 2;
            push(K::Comment, s, i);
        } else if (c == '%' && i + 1 < n && src[i + 1] == '{') {
            size_t e = src.find("%}", i + 2);
            i = e == std::string::npos ? n : e + 2;
            push(K::CBlock, s, i);
        } else if (src.compare(i, 3, "\"\"\"") == 0 || src.compare(i, 4, "r\"\"\"") == 0) {
            size_t q = src[i] == 'r' ? i + 1 : i;
            size_t e = src.find("\"\"\"", q + 3);
            i = e == std::string::npos ? n : e + 3;
            push(K::Str, s, i);
        } else if (c == 'r' && i + 1 < n && src[i + 1] == '"' && (i == 0 || !identPart(src[i - 1]))) {
            size_t e = src.find('"', i + 2);
            i = e == std::string::npos ? n : e + 1;
            push(K::Str, s, i);
        } else if (c == '"') {
            i = skipString(src, i + 1);
            push(K::Str, s, i);
        } else if (c == '\'') {
            ++i;
            while (i < n && src[i] != '\'' && src[i] != '\n') i += src[i] == '\\' ? 2 : 1;
            if (i < n && src[i] == '\'') ++i;
            push(K::Str, s, i);
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            bool hex = c == '0' && i + 1 < n && (src[i + 1] == 'x' || src[i + 1] == 'X');
            while (i < n) {
                char d = src[i];
                if (identPart(d)) ++i;
                else if (d == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i + 1]))) ++i;
                else if ((d == '+' || d == '-') && !hex && (src[i - 1] == 'e' || src[i - 1] == 'E')) ++i;
                else break;
            }
            push(K::Num, s, i);
        } else if (identStart(c)) {
            while (i < n && identPart(src[i])) ++i;
            push(K::Word, s, i);
        } else {
            size_t len = 1;
            for (const char *p : puncts) {
                std::string ps = p;
                if (src.compare(i, ps.size(), ps) == 0) { len = ps.size(); break; }
            }
            i += len;
            push(K::Punct, s, i);
        }
    }
    return out;
}

bool is(const Tok *t, const char *text) { return t && t->k == K::Punct && t->text == text; }
bool isWord(const Tok *t, const char *text) { return t && t->k == K::Word && t->text == text; }

// Keywords after which an operator is a prefix one and `(` is not a call.
const std::set<std::string> &controlWords() {
    static const std::set<std::string> s = {"return", "case", "in", "yield", "throw", "else", "step", "if",
                                            "while", "for", "do", "switch", "match", "await", "let", "const",
                                            "is", "as", "catch", "default", "and", "or", "not", "defer"};
    return s;
}

// Keywords a `{` after which is a value (a map literal or a destructuring pattern).
const std::set<std::string> &literalAfter() {
    static const std::set<std::string> s = {"return", "yield", "in", "case", "let", "const", "throw", "await"};
    return s;
}

// Does `t` end an operand (so a following `-` is binary, `(` a call, `++` postfix)?
bool endsOperand(const Tok *t) {
    if (!t) return false;
    switch (t->k) {
    case K::Word: return !controlWords().count(t->text);
    case K::Num: case K::Str: return true;
    case K::Punct: return t->text == ")" || t->text == "]";
    default: return false;
    }
}

const std::set<std::string> &binaryOps() {
    static const std::set<std::string> s = {"=", "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=",
                                            "%=", "&=", "|=", "^=", "<<=", ">>=", "??", "**", "+", "-", "*",
                                            "/", "%", "|", "&", "^", "<<", "->", "=>"};
    return s;
}

// Roles that depend on the neighbours, worked out once per token.
enum class Role { Plain, Prefix, Postfix, Binary, Nullable, Ternary, TernaryColon, PointerType, Keep,
                  Literal, // the braces of a map literal, `{"a": 1}`, whose inside keeps its spacing
                  Name };  // a keyword used as a member or function name: `g.step()`, `fn match()`

// The `>` (or `>>`) closing a type argument list opened by the `<` at i, if what
// lies between looks like types; npos otherwise.
size_t genericClose(const std::vector<Tok> &t, size_t i) {
    int depth = 0;
    for (size_t k = i; k < t.size(); ++k) {
        const Tok &x = t[k];
        if (x.k == K::Word || x.k == K::Num) continue;
        if (x.k != K::Punct) return std::string::npos;
        const std::string &s = x.text;
        if (s == "<") ++depth;
        else if (s == ">") --depth;
        else if (s == ">>") depth -= 2;
        else if (s != "," && s != "." && s != "*" && s != "[" && s != "]" && s != "(" && s != ")" &&
                 s != "->" && s != "?" && s != ":")
            return std::string::npos;
        if (depth == 0) return k;
        if (depth < 0) return std::string::npos;
    }
    return std::string::npos;
}

// Is the `<` at i a type argument list's (rather than a comparison)?
bool opensGeneric(const std::vector<Tok> &t, size_t i) {
    static const std::set<std::string> decl = {"fn", "class", "struct", "interface", "trait", "enum", "type",
                                               "typedef"};
    if (i == 0) return false;
    const Tok &p = t[i - 1];
    if (p.k == K::Punct) return p.text == "::";
    if (p.k != K::Word) return false;
    if (std::isupper(static_cast<unsigned char>(p.text[0]))) return true; // Array<int>, T<...>
    size_t k = i - 1; // `fn name<T>`, `fn Point.name<T>`
    while (k >= 2 && t[k - 1].k == K::Punct && t[k - 1].text == "." && t[k - 2].k == K::Word) k -= 2;
    return k >= 1 && t[k - 1].k == K::Word && decl.count(t[k - 1].text);
}

std::vector<Role> roles(const std::vector<Tok> &t) {
    std::vector<Role> r(t.size(), Role::Plain);
    std::vector<int> ternaries(1, 0); // open `?`s per bracket depth
    auto prevOf = [&](size_t i) -> const Tok * {
        return i > 0 && t[i - 1].k != K::Newline && t[i - 1].k != K::Comment ? &t[i - 1] : nullptr;
    };
    auto nextOf = [&](size_t i) -> const Tok * { return i + 1 < t.size() ? &t[i + 1] : nullptr; };
    for (size_t i = 0; i < t.size(); ++i) {
        const Tok *p = prevOf(i), *nx = nextOf(i);
        if (t[i].k == K::Word) {
            if (controlWords().count(t[i].text) && (is(p, ".") || is(p, "?.") || isWord(p, "fn"))) r[i] = Role::Name;
            if (t[i].text == "operator") // `operator[]=(`, `operator<(`: the operator stays as written
                for (size_t k = i + 1; k < t.size() && t[k].k == K::Punct && t[k].text != "("; ++k) r[k] = Role::Keep;
            continue;
        }
        if (t[i].k != K::Punct) continue;
        const std::string &x = t[i].text;
        if (!p && (x == "-" || x == "+" || x == "*" || x == "&")) { // first on its line: maybe the last line's operand's
            r[i] = Role::Keep;
            continue;
        }
        bool operand = endsOperand(p) || (p && i > 0 && (r[i - 1] == Role::Postfix || r[i - 1] == Role::Nullable ||
                                                         r[i - 1] == Role::Name));
        if (x == "(" || x == "[" || x == "{") ternaries.push_back(0);
        else if ((x == ")" || x == "]" || x == "}") && ternaries.size() > 1) ternaries.pop_back();
        if (r[i] == Role::Keep) {
            // the end of a type argument list
        } else if (x == "<") {
            size_t close = opensGeneric(t, i) ? genericClose(t, i) : std::string::npos;
            if (close != std::string::npos) { // the list's own `<`/`>`s too
                for (size_t k = i; k <= close; ++k)
                    if (t[k].k == K::Punct && (t[k].text == "<" || t[k].text == ">" || t[k].text == ">>"))
                        r[k] = Role::Keep;
            } else {
                r[i] = operand && genericClose(t, i) == std::string::npos ? Role::Binary : Role::Keep;
            }
        } else if (x == ">" || x == ">>") {
            r[i] = operand ? Role::Binary : Role::Keep;
        } else if (x == "->") {
            r[i] = is(p, ")") ? Role::Binary : Role::Keep; // a return type, or `p->m`
        } else if (x == "{" && p && ((p->k == K::Punct && !endsOperand(p) && !is(p, ">") && !is(p, "?") &&
                                      r[i - 1] != Role::Postfix && r[i - 1] != Role::Nullable) ||
                                     (p->k == K::Word && literalAfter().count(p->text)))) {
            r[i] = Role::Literal; // after `=`, `(`, `,`, `:`, `return`, ...: a value
        } else if (x == "!") {
            r[i] = operand && t[i].ws.empty() ? Role::Postfix : Role::Prefix;
        } else if (x == "~") {
            r[i] = Role::Prefix;
        } else if (x == "++" || x == "--") {
            r[i] = operand && t[i].ws.empty() ? Role::Postfix : Role::Prefix;
        } else if (x == "?") {
            bool closes = !nx || nx->k == K::Newline ||
                          (nx->k == K::Punct && std::string(")],=>{;?|}").find(nx->text) != std::string::npos &&
                           nx->text.size() == 1);
            if (operand && t[i].ws.empty() && (closes || nx->k == K::Word)) r[i] = Role::Nullable; // `int?  next: T`
            else { r[i] = Role::Ternary; ++ternaries.back(); }
        } else if (x == ":") {
            if (ternaries.back() > 0) { r[i] = Role::TernaryColon; --ternaries.back(); }
        } else if ((x == "*" || x == "&") && operand &&
                   (!nx || nx->k == K::Newline ||
                    (nx->k == K::Punct && (nx->text == ")" || nx->text == "," || nx->text == "=" ||
                                           nx->text == ">" || nx->text == "]" || nx->text == ";" ||
                                           nx->text == "{" || nx->text == "*" || nx->text == ":" ||
                                           nx->text == "?")))) {
            r[i] = Role::PointerType; // `int*`, `p: Node*`
        } else if (x == "-" || x == "+" || x == "*" || x == "&") {
            r[i] = operand ? Role::Binary : Role::Prefix;
        } else if (binaryOps().count(x)) {
            r[i] = Role::Binary;
        }
    }
    return r;
}

// The whitespace between a and b (both on one line): "", " ", or a's original.
std::string gap(const Tok &a, Role ra, const Tok &b, Role rb) {
    const std::string &orig = b.ws;
    auto P = [](const Tok &t, const char *s) { return t.k == K::Punct && t.text == s; };
    if (b.k == K::Comment) return orig.empty() ? " " : orig;
    if ((ra == Role::Literal && P(a, "{")) || (rb == Role::Literal && P(b, "}"))) return orig.empty() ? "" : " ";
    if (P(b, "{") && P(a, ">")) return " "; // `class Box<T> {`
    if (ra == Role::Keep || rb == Role::Keep) return orig.empty() ? "" : " ";
    if (a.k == K::CBlock || b.k == K::CBlock) return orig.empty() ? "" : " ";
    // What follows decides first.
    if (P(b, ",") || P(b, ";") || P(b, ")") || P(b, "]")) return P(a, "{") ? " " : "";
    if (rb == Role::Postfix || rb == Role::Nullable || rb == Role::PointerType) return "";
    if (P(b, ".") || P(b, "?.") || P(b, "..") || P(b, "..=") || P(b, "::")) return "";
    if (P(a, ".") || P(a, "?.") || P(a, "..") || P(a, "..=") || P(a, "::") || P(a, "...") || P(a, "@")) return "";
    if (ra == Role::Prefix) return "";
    if (P(a, "(") || P(a, "[")) return "";
    if (rb == Role::Binary || rb == Role::Ternary || rb == Role::TernaryColon) return " ";
    if (ra == Role::Binary || ra == Role::Ternary || ra == Role::TernaryColon) return " ";
    if (P(b, ":")) return "";
    if (P(a, ":") || P(a, ",") || P(a, ";")) return " ";
    if (P(b, "(")) {
        if (a.k == K::Word) return controlWords().count(a.text) && ra != Role::Name ? " " : "";
        if (P(a, ")") || P(a, "]") || ra == Role::Postfix) return "";
        if (P(a, "{") || P(a, "}")) return " ";
        return orig.empty() ? "" : " ";
    }
    if (P(b, "[")) {
        if (a.k == K::Word) return controlWords().count(a.text) && ra != Role::Name ? " " : "";
        if (a.k == K::Str || P(a, ")") || P(a, "]") || ra == Role::Postfix) return "";
        return orig.empty() ? "" : " ";
    }
    if (P(b, "{")) return P(a, "(") || P(a, "[") ? "" : " ";
    if (P(a, "{")) return P(b, "}") ? (orig.empty() ? "" : " ") : " ";
    if (P(b, "}")) return " ";
    if (P(a, "}") || P(a, ")") || P(a, "]")) return " ";
    if (rb == Role::Prefix) return " ";
    if (a.k != K::Punct && b.k != K::Punct) return " ";
    return orig.empty() ? "" : " ";
}

// Indexes of each bracket's partner (or npos).
std::vector<size_t> partners(const std::vector<Tok> &t) {
    std::vector<size_t> m(t.size(), std::string::npos), st;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].k != K::Punct) continue;
        const std::string &x = t[i].text;
        if (x == "(" || x == "[" || x == "{") st.push_back(i);
        else if ((x == ")" || x == "]" || x == "}") && !st.empty()) {
            m[i] = st.back();
            m[st.back()] = i;
            st.pop_back();
        }
    }
    return m;
}

bool spansLines(const Tok &t) { return t.text.find('\n') != std::string::npos; }

} // namespace

std::string layoutGrav(const std::string &src) {
    std::vector<Tok> t = lex(src);
    std::vector<Role> r = roles(t);
    std::vector<size_t> m = partners(t);
    for (size_t i = 0; i < t.size(); ++i)
        if (r[i] == Role::Literal && m[i] != std::string::npos) r[m[i]] = Role::Literal;
    // Where to break lines: before token i (breakBefore), and `;`s to drop.
    std::vector<bool> breakBefore(t.size() + 1, false), drop(t.size(), false);
    auto lineHasBreak = [&](size_t from, size_t to) { // a line break among (from, to)
        for (size_t k = from + 1; k < to; ++k)
            if (t[k].k == K::Newline || spansLines(t[k])) return true;
        return false;
    };
    // A `{ ... }` on one line spreads over lines when it holds a `;` between
    // statements, or a block that does (worked out innermost first); `{ a; }` stays.
    std::vector<bool> spread(t.size(), false);
    {
        std::vector<size_t> open;
        for (size_t i = 0; i < t.size(); ++i) {
            if (is(&t[i], "{")) open.push_back(i);
            else if (is(&t[i], "}") && !open.empty()) {
                size_t o = open.back();
                open.pop_back();
                if (m[o] != i || lineHasBreak(o, i)) continue;
                int depth = 0;
                for (size_t k = o + 1; k < i && !spread[o]; ++k) {
                    if (t[k].k != K::Punct) continue;
                    const std::string &x = t[k].text;
                    if (x == "{" && spread[k]) spread[o] = true;
                    if (x == "(" || x == "[" || x == "{") ++depth;
                    else if (x == ")" || x == "]" || x == "}") --depth;
                    else if (x == ";" && depth == 0 && k + 1 < i) spread[o] = true; // (not one ending the block)
                }
            }
        }
    }
    for (size_t i = 0; i < t.size(); ++i) {
        if (!is(&t[i], "{") || m[i] == std::string::npos) continue;
        size_t close = m[i];
        if (!lineHasBreak(i, close)) {
            if (!spread[i]) continue;
            // `{ a; b }` on one line: a statement per line.
            if (i + 1 < close) breakBefore[i + 1] = true;
            int depth = 0;
            for (size_t s = i + 1; s < close; ++s) {
                if (t[s].k != K::Punct) continue;
                const std::string &x = t[s].text;
                if (x == "(" || x == "[" || x == "{") ++depth;
                else if (x == ")" || x == "]" || x == "}") --depth;
                if (x == "}" && depth == 0 && r[s] != Role::Literal && s + 1 < close && t[s + 1].k == K::Word) {
                    static const std::set<std::string> joined = {"else", "catch", "finally", "while", "is", "as"};
                    if (!joined.count(t[s + 1].text)) breakBefore[s + 1] = true; // `{ ... } next()`
                }
                if (x != ";" || depth != 0) continue;
                if (s + 1 < close) breakBefore[s + 1] = true;
                const Tok *before = s > 0 ? &t[s - 1] : nullptr, *after = &t[s + 1];
                bool bare = before && before->k == K::Word &&
                            (before->text == "return" || before->text == "break" || before->text == "continue" ||
                             before->text == "yield");
                bool startsClean = s + 1 == close || after->k == K::Word || is(after, "(") || is(after, "[");
                drop[s] = (!bare || s + 1 == close) && startsClean;
            }
            breakBefore[close] = true;
            continue;
        }
        // `{` with code after it on its line, running past the line.
        size_t k = i + 1;
        if (k < t.size() && t[k].k != K::Newline && t[k].k != K::Comment) breakBefore[k] = true;
        // `}` after other code on its line.
        size_t c = close;
        bool codeBefore = false;
        for (size_t j = c; j-- > 0 && t[j].k != K::Newline;)
            if (!(t[j].k == K::Punct && (t[j].text == "}" || t[j].text == ")" || t[j].text == "]"))) {
                codeBefore = true;
                break;
            }
        if (codeBefore) breakBefore[c] = true;
    }
    std::string out;
    const Tok *prev = nullptr;
    Role prevRole = Role::Plain;
    for (size_t i = 0; i < t.size(); ++i) {
        const Tok &x = t[i];
        if (drop[i]) continue;
        if (x.k == K::Newline) {
            out += "\n";
            prev = nullptr;
            continue;
        }
        if (breakBefore[i] && prev) {
            out += "\n";
            prev = nullptr;
        }
        if (!prev) out += out.empty() || out.back() == '\n' ? x.ws : ""; // (the indent pass redoes it)
        else {
            std::string g = gap(*prev, prevRole, x, r[i]);
            out += g == " " && x.ws.size() > 1 ? x.ws : g; // (wider gaps line things up, or part statements)
        }
        out += x.text;
        prev = &x;
        prevRole = r[i];
    }
    return out;
}

} // namespace grav
