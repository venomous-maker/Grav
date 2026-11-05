#include "lexer/lexer.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "common/diagnostics.h"

namespace grav {

namespace {
const std::unordered_map<std::string, TokenType> &keywords() {
    static const std::unordered_map<std::string, TokenType> table = {
        {"let", TokenType::Let},
        {"const", TokenType::Const},
        {"int", TokenType::KwInt},
        {"float", TokenType::KwFloat},
        {"bool", TokenType::KwBool},
        {"string", TokenType::KwString},
        {"void", TokenType::KwVoid},
        {"true", TokenType::True},
        {"false", TokenType::False},
        {"null", TokenType::Null},
        {"fn", TokenType::Fn},
        {"class", TokenType::Class},
        {"struct", TokenType::Struct},
        {"enum", TokenType::Enum},
        {"type", TokenType::Type},
        {"typedef", TokenType::Type}, // `typedef` is an alias for `type`
        {"sizeof", TokenType::Sizeof},
        {"abstract", TokenType::Abstract},
        {"interface", TokenType::Interface},
        {"trait", TokenType::Trait},
        {"async", TokenType::Async},
        {"await", TokenType::Await},
        {"namespace", TokenType::Namespace},
        {"constructor", TokenType::Constructor},
        {"extends", TokenType::Extends},
        {"implements", TokenType::Implements},
        {"uses", TokenType::Uses},
        {"new", TokenType::New},
        {"return", TokenType::Return},
        {"static", TokenType::Static},
        {"readonly", TokenType::Readonly},
        {"public", TokenType::Public},
        {"private", TokenType::Private},
        {"protected", TokenType::Protected},
        {"self", TokenType::Self},
        {"this", TokenType::Self},
        {"super", TokenType::Super},
        {"if", TokenType::If},
        {"else", TokenType::Else},
        {"while", TokenType::While},
        {"for", TokenType::For},
        {"do", TokenType::Do},
        {"switch", TokenType::Switch},
        {"case", TokenType::Case},
        {"default", TokenType::Default},
        {"match", TokenType::Match},
        {"break", TokenType::Break},
        {"continue", TokenType::Continue},
        {"try", TokenType::Try},
        {"catch", TokenType::Catch},
        {"finally", TokenType::Finally},
        {"throw", TokenType::Throw},
        {"in", TokenType::In},
        {"as", TokenType::As},
        {"is", TokenType::Is},
        {"export", TokenType::Export},
    };
    return table;
}
} // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

bool Lexer::atEnd() const { return pos_ >= source_.size(); }

char Lexer::peek() const { return atEnd() ? '\0' : source_[pos_]; }

char Lexer::peekNext() const {
    return pos_ + 1 >= source_.size() ? '\0' : source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() != expected) return false;
    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peekNext() == '/') {
            while (!atEnd() && peek() != '\n') advance();
        } else if (c == '/' && peekNext() == '*') {
            int startLine = line_, startCol = col_;
            advance(); // '/'
            advance(); // '*'
            bool closed = false;
            while (!atEnd()) {
                if (peek() == '*' && peekNext() == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                throw GravError("lex", startLine, startCol,
                                "unterminated block comment (missing '*/')");
            }
        } else {
            return;
        }
    }
}

Token Lexer::makeToken(TokenType type, std::string lexeme) const {
    return Token{type, std::move(lexeme), tokLine_, tokCol_};
}

Token Lexer::scanNumber() {
    auto digitOf = [](char c, int base) -> int {
        int v = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 99;
        return v < base ? v : -1;
    };
    // 0x1F (hex) and 0b1010 (binary); `_` may separate digits: 1_000_000.
    if (peek() == '0' && (peekNext() == 'x' || peekNext() == 'X' || peekNext() == 'b' || peekNext() == 'B')) {
        advance();
        char prefix = advance();
        int base = (prefix == 'x' || prefix == 'X') ? 16 : 2;
        unsigned long long v = 0;
        int n = 0;
        while (peek() == '_' || digitOf(peek(), base) >= 0 ||
               (base == 2 && std::isalnum(static_cast<unsigned char>(peek())))) {
            char c = advance();
            if (c == '_') continue;
            int d = digitOf(c, base);
            if (d < 0) throw GravError("lex", line_, col_, std::string("invalid digit '") + c + "' in a " +
                                                             (base == 16 ? "hex" : "binary") + " literal");
            v = v * base + static_cast<unsigned>(d);
            ++n;
        }
        if (n == 0) throw GravError("lex", line_, col_, "a number literal needs digits after its prefix");
        // Normalized to decimal text; values above the signed range wrap like C.
        return makeToken(TokenType::IntLiteral, std::to_string(static_cast<long long>(v)));
    }
    std::string text;
    auto digits = [&] {
        while (std::isdigit(static_cast<unsigned char>(peek())) ||
               (peek() == '_' && std::isdigit(static_cast<unsigned char>(peekNext())))) {
            char c = advance();
            if (c != '_') text += c;
        }
    };
    digits();
    bool isFloat = false;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        isFloat = true;
        text += advance(); // '.'
        digits();
    }
    // Exponent: 1e9, 2.5e-3.
    if ((peek() == 'e' || peek() == 'E') &&
        (std::isdigit(static_cast<unsigned char>(peekNext())) ||
         ((peekNext() == '-' || peekNext() == '+') && (pos_ + 2 < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_ + 2])))))) {
        isFloat = true;
        text += advance();
        if (peek() == '-' || peek() == '+') text += advance();
        digits();
    }

    return makeToken(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral,
                     std::move(text));
}

Token Lexer::scanString() {
    advance(); // consume opening quote
    std::string value;
    while (!atEnd() && peek() != '"') {
        char c = advance();
        if (c == '\\') {
            if (atEnd()) break;
            char esc = advance();
            switch (esc) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                default:
                    throw GravError("lex", line_, col_,
                                    std::string("invalid escape sequence '\\") +
                                        esc + "'");
            }
        } else if (c == '\n') {
            throw GravError("lex", tokLine_, tokCol_,
                            "unterminated string literal");
        } else {
            value += c;
        }
    }
    if (atEnd()) {
        throw GravError("lex", tokLine_, tokCol_, "unterminated string literal");
    }
    advance(); // closing quote
    return makeToken(TokenType::StringLiteral, std::move(value));
}

void Lexer::scanStringInto(std::vector<Token> &out) {
    int sLine = tokLine_, sCol = tokCol_;
    advance(); // opening quote
    // A part is either literal text (isExpr == false) or embedded expression
    // source (isExpr == true), captured between `${` and the matching `}`.
    struct Part { bool isExpr; std::string text; };
    std::vector<Part> parts;
    std::string lit;
    bool interpolated = false;

    while (!atEnd() && peek() != '"') {
        char c = peek();
        if (c == '\\') {
            advance();
            if (atEnd()) break;
            char esc = advance();
            switch (esc) {
                case 'n': lit += '\n'; break;
                case 't': lit += '\t'; break;
                case '\\': lit += '\\'; break;
                case '"': lit += '"'; break;
                case '$': lit += '$'; break; // escaped interpolation
                case 'r': lit += '\r'; break;
                case '0': lit += '\0'; break;
                case '\'': lit += '\''; break;
                case 'a': lit += '\a'; break;
                case 'b': lit += '\b'; break;
                case 'f': lit += '\f'; break;
                case 'v': lit += '\v'; break;
                case 'e': lit += '\x1b'; break; // ESC, for ANSI terminal codes
                case 'x': { // \xHH — one byte
                    auto hex = [](char h) {
                        return std::isxdigit(static_cast<unsigned char>(h))
                                   ? (std::isdigit(static_cast<unsigned char>(h)) ? h - '0'
                                                                                  : (std::tolower(h) - 'a' + 10))
                                   : -1;
                    };
                    int hi = atEnd() ? -1 : hex(peek());
                    int lo = hi < 0 ? -1 : hex(peekNext());
                    if (hi < 0 || lo < 0)
                        throw GravError("lex", line_, col_, "'\\x' needs two hex digits");
                    advance(); advance();
                    lit += static_cast<char>(hi * 16 + lo);
                    break;
                }
                case 'u': { // \u{XXXX} — a Unicode code point, encoded as UTF-8
                    if (atEnd() || peek() != '{')
                        throw GravError("lex", line_, col_, "'\\u' must be written \\u{XXXX}");
                    advance();
                    unsigned long cp = 0;
                    int digits = 0;
                    while (!atEnd() && std::isxdigit(static_cast<unsigned char>(peek())) && digits < 6) {
                        char h = advance();
                        cp = cp * 16 + (std::isdigit(static_cast<unsigned char>(h)) ? h - '0'
                                                                                    : std::tolower(h) - 'a' + 10);
                        digits++;
                    }
                    if (atEnd() || peek() != '}' || digits == 0 || cp > 0x10FFFF)
                        throw GravError("lex", line_, col_, "malformed \\u{...} escape");
                    advance();
                    if (cp < 0x80) lit += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        lit += static_cast<char>(0xC0 | (cp >> 6));
                        lit += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        lit += static_cast<char>(0xE0 | (cp >> 12));
                        lit += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        lit += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        lit += static_cast<char>(0xF0 | (cp >> 18));
                        lit += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        lit += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        lit += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    throw GravError("lex", line_, col_,
                                    std::string("invalid escape sequence '\\") + esc + "'");
            }
        } else if (c == '$' && peekNext() == '{') {
            interpolated = true;
            parts.push_back({false, lit});
            lit.clear();
            advance(); advance(); // ${
            std::string expr;
            int depth = 1;
            while (!atEnd() && depth > 0) {
                char d = peek();
                if (d == '{') depth++;
                else if (d == '}') { depth--; if (depth == 0) { advance(); break; } }
                else if (d == '\n') throw GravError("lex", sLine, sCol,
                                                    "unterminated interpolation in string");
                expr += d;
                advance();
            }
            if (depth > 0) throw GravError("lex", sLine, sCol,
                                           "unterminated '${' in string literal");
            parts.push_back({true, expr});
        } else if (c == '\n') {
            throw GravError("lex", sLine, sCol, "unterminated string literal");
        } else {
            lit += c;
            advance();
        }
    }
    if (atEnd()) throw GravError("lex", sLine, sCol, "unterminated string literal");
    advance(); // closing quote
    parts.push_back({false, lit});

    if (!interpolated) {
        out.push_back(Token{TokenType::StringLiteral, parts.empty() ? "" : parts[0].text,
                            sLine, sCol});
        return;
    }

    // Desugar to `( part + str( expr ) + part + ... )`, converting each embedded
    // expression to a string via the `str` builtin so any printable type works.
    auto emit = [&](TokenType t, std::string lx) {
        out.push_back(Token{t, std::move(lx), sLine, sCol});
    };
    emit(TokenType::LParen, "(");
    bool first = true;
    for (const auto &p : parts) {
        if (!first) emit(TokenType::Plus, "+");
        first = false;
        if (!p.isExpr) {
            emit(TokenType::StringLiteral, p.text);
        } else {
            // `${x:.2f}` — a format spec after the last top-level ':' (see splitFormatSpec).
            std::string exprText = p.text, spec;
            splitFormatSpec(exprText, spec, sLine, sCol);
            emit(TokenType::Identifier, spec.empty() ? "str" : "__fmt");
            emit(TokenType::LParen, "(");
            Lexer sub(exprText);
            // Re-anchor the embedded tokens at the string's position so errors
            // inside `${...}` point at the right line (not line 1).
            for (auto &tk : sub.tokenize()) {
                if (tk.type == TokenType::EndOfFile) continue;
                tk.line = sLine;
                tk.col = sCol + tk.col;
                out.push_back(tk);
            }
            if (!spec.empty()) {
                emit(TokenType::Comma, ",");
                emit(TokenType::StringLiteral, spec);
            }
            emit(TokenType::RParen, ")");
        }
    }
    emit(TokenType::RParen, ")");
}

// `x:.2f` -> ("x", ".2f"). The spec follows the last ':' outside brackets and
// quotes, and is `[<>^][+][0][width][.precision][d f e x X o b s %]` with no
// spaces (so `${a ? b : c}` stays a conditional).
void Lexer::splitFormatSpec(std::string &expr, std::string &spec, int line, int col) {
    int depth = 0;
    bool inStr = false;
    size_t colon = std::string::npos;
    for (size_t i = 0; i < expr.size(); ++i) {
        char c = expr[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == ':' && depth == 0 && (i + 1 >= expr.size() || expr[i + 1] != ':') &&
                 (i == 0 || expr[i - 1] != ':'))
            colon = i;
    }
    if (colon == std::string::npos) return;
    std::string tail = expr.substr(colon + 1);
    size_t k = 0;
    auto digits = [&] { size_t a = k; while (k < tail.size() && std::isdigit(static_cast<unsigned char>(tail[k]))) ++k; return k > a; };
    if (k < tail.size() && (tail[k] == '<' || tail[k] == '>' || tail[k] == '^')) ++k;
    if (k < tail.size() && tail[k] == '+') ++k;
    digits();
    if (k < tail.size() && tail[k] == '.') { ++k; if (!digits()) return; }
    if (k < tail.size() && std::string("dfexXobs%").find(tail[k]) != std::string::npos) ++k;
    if (tail.empty() || k != tail.size()) {
        // Not a spec: something like `${cond ? a : b}`.
        bool looksLikeSpec = !tail.empty() && tail.find(' ') == std::string::npos &&
                             (tail[0] == '.' || std::isdigit(static_cast<unsigned char>(tail[0])) || tail[0] == '<' ||
                              tail[0] == '>' || tail[0] == '^');
        if (looksLikeSpec) throw GravError("lex", line, col, "invalid format spec '" + tail + "' in '${...}'");
        return;
    }
    spec = tail;
    expr = expr.substr(0, colon);
}

// `"""..."""` (multi-line: the first newline and the common indentation are
// removed; a closing `"""` on its own line sets that indentation) and raw
// `r"..."` / `r"""..."""` (no escapes, no `${}`). The text is turned into an
// ordinary string literal and lexed as one, so escapes and interpolation behave
// exactly as in `"..."`.
void Lexer::scanSpecialString(std::vector<Token> &out, bool raw) {
    int sLine = tokLine_, sCol = tokCol_;
    if (raw) advance(); // 'r'
    auto tripleAt = [&](size_t p) {
        return p + 2 < source_.size() && source_[p] == '"' && source_[p + 1] == '"' && source_[p + 2] == '"';
    };
    bool triple = tripleAt(pos_);
    for (int k = 0; k < (triple ? 3 : 1); ++k) advance();
    std::string body;
    int depth = 0; // inside ${ ... }
    for (;;) {
        if (atEnd()) throw GravError("lex", sLine, sCol, "unterminated string literal");
        char c = peek();
        if (depth == 0) {
            if (triple ? tripleAt(pos_) : c == '"') break;
            if (!triple && c == '\n')
                throw GravError("lex", sLine, sCol, "unterminated string literal (use \"\"\" for several lines)");
            if (!raw && c == '\\') { body += advance(); if (!atEnd()) body += advance(); continue; }
            if (!raw && c == '$' && peekNext() == '{') { body += advance(); body += advance(); depth = 1; continue; }
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
        }
        body += advance();
    }
    for (int k = 0; k < (triple ? 3 : 1); ++k) advance();

    if (triple) body = dedent(body);

    // As an ordinary literal: quotes and line breaks escaped (and, when raw,
    // backslashes and `$` too); the text inside ${ ... } copied as it is.
    std::string lit = "\"";
    depth = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (depth > 0) {
            if (c == '{') ++depth;
            else if (c == '}') --depth;
            lit += c;
            continue;
        }
        if (!raw && c == '\\' && i + 1 < body.size()) { lit += c; lit += body[++i]; continue; }
        if (!raw && c == '$' && i + 1 < body.size() && body[i + 1] == '{') { lit += "${"; ++i; depth = 1; continue; }
        if (c == '"') lit += "\\\"";
        else if (c == '\n') lit += "\\n";
        else if (c == '\r') lit += "\\r";
        else if (raw && c == '\\') lit += "\\\\";
        else if (raw && c == '$') lit += "\\$";
        else lit += c;
    }
    lit += "\"";
    Lexer sub(lit);
    for (auto &tk : sub.tokenize()) {
        if (tk.type == TokenType::EndOfFile) continue;
        tk.line = sLine;
        tk.col = sCol;
        out.push_back(tk);
    }
}

// The body of a `"""` string without its first line break and without the
// indentation its lines share (or the indentation of the closing `"""`).
std::string Lexer::dedent(std::string body) {
    if (!body.empty() && body[0] == '\r') body.erase(0, 1);
    if (!body.empty() && body[0] == '\n') body.erase(0, 1);
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i <= body.size(); ++i)
        if (i == body.size() || body[i] == '\n') { lines.push_back(body.substr(start, i - start)); start = i + 1; }
    auto blank = [](const std::string &l) { return l.find_first_not_of(" \t\r") == std::string::npos; };
    size_t indent = std::string::npos;
    bool closingLine = lines.size() > 1 && blank(lines.back());
    if (closingLine) {
        indent = lines.back().size();
        lines.pop_back();
    } else {
        for (const auto &l : lines)
            if (!blank(l)) indent = std::min(indent, l.find_first_not_of(" \t"));
    }
    if (indent == std::string::npos) indent = 0;
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string &l = lines[i];
        size_t cut = 0;
        while (cut < indent && cut < l.size() && (l[cut] == ' ' || l[cut] == '\t')) ++cut;
        out += l.substr(cut);
        if (i + 1 < lines.size()) out += '\n';
    }
    return out;
}

Token Lexer::scanCBlock() {
    advance(); advance(); // consume `%{`
    std::string raw;
    while (!atEnd()) {
        if (peek() == '%' && peekNext() == '}') {
            advance(); advance(); // `%}`
            return makeToken(TokenType::CBlock, std::move(raw));
        }
        raw += advance();
    }
    throw GravError("lex", tokLine_, tokCol_, "unterminated inline C block (missing '%}')");
}

Token Lexer::scanIdentifierOrKeyword() {
    std::string text;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        text += advance();
    }
    auto it = keywords().find(text);
    TokenType type = it != keywords().end() ? it->second : TokenType::Identifier;
    return makeToken(type, std::move(text));
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    for (;;) {
        skipWhitespaceAndComments();
        tokLine_ = line_;
        tokCol_ = col_;

        if (atEnd()) {
            tokens.push_back(makeToken(TokenType::EndOfFile, ""));
            return tokens;
        }

        char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(scanNumber());
            continue;
        }
        // r"raw", r"""raw lines""", """lines""": rewritten into a plain literal.
        bool raw = c == 'r' && peekNext() == '"';
        bool triple = c == '"' && peekNext() == '"' && pos_ + 2 < source_.size() && source_[pos_ + 2] == '"';
        if (raw || triple) {
            scanSpecialString(tokens, raw);
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(scanIdentifierOrKeyword());
            continue;
        }
        if (c == '"') {
            scanStringInto(tokens);
            continue;
        }
        if (c == '%' && peekNext() == '{') {
            tokens.push_back(scanCBlock());
            continue;
        }

        advance(); // consume the punctuation character
        switch (c) {
            case ':':
                tokens.push_back(match(':') ? makeToken(TokenType::ColonColon, "::")
                                            : makeToken(TokenType::Colon, ":"));
                break;
            case ';': tokens.push_back(makeToken(TokenType::Semicolon, ";")); break;
            case ',': tokens.push_back(makeToken(TokenType::Comma, ",")); break;
            case '.':
                if (match('.')) {
                    if (match('.')) tokens.push_back(makeToken(TokenType::Ellipsis, "..."));
                    else if (match('=')) tokens.push_back(makeToken(TokenType::DotDotEq, "..="));
                    else tokens.push_back(makeToken(TokenType::DotDot, ".."));
                } else {
                    tokens.push_back(makeToken(TokenType::Dot, "."));
                }
                break;
            case '(': tokens.push_back(makeToken(TokenType::LParen, "(")); break;
            case ')': tokens.push_back(makeToken(TokenType::RParen, ")")); break;
            case '{': tokens.push_back(makeToken(TokenType::LBrace, "{")); break;
            case '}': tokens.push_back(makeToken(TokenType::RBrace, "}")); break;
            case '[': tokens.push_back(makeToken(TokenType::LBracket, "[")); break;
            case ']': tokens.push_back(makeToken(TokenType::RBracket, "]")); break;
            case '@': tokens.push_back(makeToken(TokenType::At, "@")); break;
            case '#': tokens.push_back(makeToken(TokenType::Hash, "#")); break;
            case '?':
                if (match('?')) tokens.push_back(makeToken(TokenType::QuestionQuestion, "??"));
                else if (match('.')) tokens.push_back(makeToken(TokenType::QuestionDot, "?."));
                else tokens.push_back(makeToken(TokenType::Question, "?"));
                break;
            case '+':
                if (match('+')) tokens.push_back(makeToken(TokenType::PlusPlus, "++"));
                else if (match('=')) tokens.push_back(makeToken(TokenType::PlusEq, "+="));
                else tokens.push_back(makeToken(TokenType::Plus, "+"));
                break;
            case '-':
                if (match('>')) tokens.push_back(makeToken(TokenType::Arrow, "->"));
                else if (match('-')) tokens.push_back(makeToken(TokenType::MinusMinus, "--"));
                else if (match('=')) tokens.push_back(makeToken(TokenType::MinusEq, "-="));
                else tokens.push_back(makeToken(TokenType::Minus, "-"));
                break;
            case '*':
                tokens.push_back(match('=') ? makeToken(TokenType::StarEq, "*=")
                                            : makeToken(TokenType::Star, "*"));
                break;
            case '/':
                tokens.push_back(match('=') ? makeToken(TokenType::SlashEq, "/=")
                                            : makeToken(TokenType::Slash, "/"));
                break;
            case '%':
                tokens.push_back(match('=') ? makeToken(TokenType::PercentEq, "%=")
                                            : makeToken(TokenType::Percent, "%"));
                break;
            case '=':
                if (match('=')) tokens.push_back(makeToken(TokenType::EqEq, "=="));
                else if (match('>')) tokens.push_back(makeToken(TokenType::FatArrow, "=>"));
                else tokens.push_back(makeToken(TokenType::Assign, "="));
                break;
            case '!':
                tokens.push_back(match('=') ? makeToken(TokenType::NotEq, "!=")
                                            : makeToken(TokenType::Bang, "!"));
                break;
            case '&':
                if (match('&')) tokens.push_back(makeToken(TokenType::AmpAmp, "&&"));
                else if (match('=')) tokens.push_back(makeToken(TokenType::AmpEq, "&="));
                else tokens.push_back(makeToken(TokenType::Amp, "&"));
                break;
            case '|':
                if (match('|')) tokens.push_back(makeToken(TokenType::PipePipe, "||"));
                else if (match('=')) tokens.push_back(makeToken(TokenType::PipeEq, "|="));
                else tokens.push_back(makeToken(TokenType::Pipe, "|"));
                break;
            case '^':
                tokens.push_back(match('=') ? makeToken(TokenType::CaretEq, "^=")
                                            : makeToken(TokenType::Caret, "^"));
                break;
            case '~': tokens.push_back(makeToken(TokenType::Tilde, "~")); break;
            case '>':
                if (match('>')) {
                    tokens.push_back(match('=') ? makeToken(TokenType::ShiftRightEq, ">>=")
                                                : makeToken(TokenType::ShiftRight, ">>"));
                } else {
                    tokens.push_back(match('=') ? makeToken(TokenType::GreaterEq, ">=")
                                                : makeToken(TokenType::Greater, ">"));
                }
                break;
            case '<':
                if (match('<')) {
                    tokens.push_back(match('=') ? makeToken(TokenType::ShiftLeftEq, "<<=")
                                                : makeToken(TokenType::ShiftLeft, "<<"));
                } else {
                    tokens.push_back(match('=') ? makeToken(TokenType::LessEq, "<=")
                                                : makeToken(TokenType::Less, "<"));
                }
                break;
            default:
                throw GravError("lex", tokLine_, tokCol_,
                                std::string("unexpected character '") + c + "'");
        }
    }
}

} // namespace grav
