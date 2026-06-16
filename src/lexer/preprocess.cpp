#include "lexer/preprocess.h"

#include <set>
#include <string>
#include <unordered_map>

#include "common/diagnostics.h"

namespace grav {

namespace {

struct Macro {
    bool functionLike = false;
    std::vector<std::string> params;
    std::vector<Token> body;
};

using MacroTable = std::unordered_map<std::string, Macro>;

// A `#` begins a directive only when it is the first token on its source line.
bool isLineStart(const std::vector<Token> &toks, size_t i) {
    return i == 0 || toks[i - 1].line != toks[i].line;
}

// Re-stamp expanded tokens with the use site's position so diagnostics (and the
// parser's line-based heuristics) treat the expansion as part of the call.
void stamp(std::vector<Token> &toks, const Token &site) {
    for (auto &t : toks) {
        t.line = site.line;
        t.col = site.col;
    }
}

// Forward declaration: fully expands a token sequence (no directive handling).
std::vector<Token> expandSequence(const std::vector<Token> &seq,
                                  const MacroTable &macros,
                                  std::set<std::string> active);

// Collects function-like macro arguments starting at the '(' that follows the
// macro name (seq[lparen]). Returns the comma-separated argument token lists and
// advances `pos` to just past the matching ')'.
std::vector<std::vector<Token>> collectArgs(const std::vector<Token> &seq,
                                            size_t lparen, size_t &pos,
                                            const Token &site) {
    std::vector<std::vector<Token>> args;
    std::vector<Token> cur;
    int depth = 1;
    size_t k = lparen + 1;
    bool sawAny = false;
    while (k < seq.size() && depth > 0) {
        const Token &tk = seq[k];
        if (tk.type == TokenType::EndOfFile) break;
        if (tk.type == TokenType::LParen) { depth++; cur.push_back(tk); }
        else if (tk.type == TokenType::RParen) { depth--; if (depth > 0) cur.push_back(tk); }
        else if (tk.type == TokenType::Comma && depth == 1) {
            args.push_back(cur); cur.clear(); sawAny = true;
        } else { cur.push_back(tk); sawAny = true; }
        k++;
    }
    if (depth != 0)
        throw GravError("preprocess", site.line, site.col,
                        "unterminated macro argument list");
    if (!(args.empty() && cur.empty() && !sawAny)) args.push_back(cur);
    pos = k; // past the ')'
    return args;
}

std::vector<Token> expandSequence(const std::vector<Token> &seq,
                                  const MacroTable &macros,
                                  std::set<std::string> active) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < seq.size()) {
        const Token &t = seq[i];
        if (t.type == TokenType::Identifier && !active.count(t.lexeme)) {
            auto it = macros.find(t.lexeme);
            if (it != macros.end()) {
                const Macro &m = it->second;
                if (!m.functionLike) {
                    std::set<std::string> act = active;
                    act.insert(t.lexeme);
                    std::vector<Token> r = expandSequence(m.body, macros, act);
                    stamp(r, t);
                    out.insert(out.end(), r.begin(), r.end());
                    i++;
                    continue;
                }
                // Function-like: only expand when an argument list follows.
                if (i + 1 < seq.size() && seq[i + 1].type == TokenType::LParen) {
                    size_t pos = 0;
                    auto args = collectArgs(seq, i + 1, pos, t);
                    if (args.size() != m.params.size())
                        throw GravError("preprocess", t.line, t.col,
                                        "macro '" + t.lexeme + "' expects " +
                                            std::to_string(m.params.size()) +
                                            " argument(s), but got " +
                                            std::to_string(args.size()));
                    // Arguments are expanded in the caller's context.
                    std::vector<std::vector<Token>> expandedArgs;
                    for (auto &a : args)
                        expandedArgs.push_back(expandSequence(a, macros, active));
                    // Substitute parameters into the body.
                    std::vector<Token> sub;
                    for (const Token &bt : m.body) {
                        bool replaced = false;
                        if (bt.type == TokenType::Identifier) {
                            for (size_t p = 0; p < m.params.size(); ++p) {
                                if (m.params[p] == bt.lexeme) {
                                    sub.insert(sub.end(), expandedArgs[p].begin(),
                                               expandedArgs[p].end());
                                    replaced = true;
                                    break;
                                }
                            }
                        }
                        if (!replaced) sub.push_back(bt);
                    }
                    std::set<std::string> act = active;
                    act.insert(t.lexeme);
                    std::vector<Token> r = expandSequence(sub, macros, act);
                    stamp(r, t);
                    out.insert(out.end(), r.begin(), r.end());
                    i = pos;
                    continue;
                }
                // No '(' -> a bare reference to a function-like macro; leave as-is.
            }
        }
        out.push_back(t);
        i++;
    }
    return out;
}

} // namespace

std::vector<Token> preprocess(std::vector<Token> tokens) {
    MacroTable macros;
    std::vector<Token> cleaned;

    size_t i = 0;
    while (i < tokens.size()) {
        const Token &t = tokens[i];
        bool isDefine = t.type == TokenType::Hash && isLineStart(tokens, i) &&
                        i + 1 < tokens.size() &&
                        tokens[i + 1].type == TokenType::Identifier &&
                        tokens[i + 1].lexeme == "define";
        if (!isDefine) {
            cleaned.push_back(t);
            i++;
            continue;
        }

        int dl = t.line;
        size_t j = i + 2;
        if (j >= tokens.size() || tokens[j].type != TokenType::Identifier)
            throw GravError("preprocess", t.line, t.col,
                            "#define requires a macro name");
        const Token &nameTok = tokens[j];
        std::string name = nameTok.lexeme;
        j++;

        Macro m;
        // Function-like only when '(' is directly adjacent to the name (no space).
        if (j < tokens.size() && tokens[j].type == TokenType::LParen &&
            tokens[j].line == dl &&
            tokens[j].col == nameTok.col + static_cast<int>(name.size())) {
            m.functionLike = true;
            j++; // '('
            if (!(j < tokens.size() && tokens[j].type == TokenType::RParen)) {
                for (;;) {
                    if (j >= tokens.size() || tokens[j].type != TokenType::Identifier)
                        throw GravError("preprocess", t.line, t.col,
                                        "expected a macro parameter name");
                    m.params.push_back(tokens[j].lexeme);
                    j++;
                    if (j < tokens.size() && tokens[j].type == TokenType::Comma) { j++; continue; }
                    break;
                }
            }
            if (j >= tokens.size() || tokens[j].type != TokenType::RParen)
                throw GravError("preprocess", t.line, t.col,
                                "expected ')' to close the macro parameter list");
            j++; // ')'
        }

        // The body is the remainder of the directive's source line.
        while (j < tokens.size() && tokens[j].type != TokenType::EndOfFile &&
               tokens[j].line == dl) {
            m.body.push_back(tokens[j]);
            j++;
        }
        macros[name] = std::move(m);
        i = j;
    }

    return expandSequence(cleaned, macros, {});
}

} // namespace grav
