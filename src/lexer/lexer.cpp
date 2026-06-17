#include "lexer/lexer.h"

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
        {"new", TokenType::New},
        {"return", TokenType::Return},
        {"static", TokenType::Static},
        {"readonly", TokenType::Readonly},
        {"public", TokenType::Public},
        {"private", TokenType::Private},
        {"protected", TokenType::Protected},
        {"self", TokenType::Self},
        {"this", TokenType::Self},
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
    std::string text;
    while (std::isdigit(static_cast<unsigned char>(peek()))) text += advance();

    bool isFloat = false;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        isFloat = true;
        text += advance(); // '.'
        while (std::isdigit(static_cast<unsigned char>(peek()))) text += advance();
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
            emit(TokenType::Identifier, "str");
            emit(TokenType::LParen, "(");
            Lexer sub(p.text);
            for (auto &tk : sub.tokenize())
                if (tk.type != TokenType::EndOfFile) out.push_back(tk);
            emit(TokenType::RParen, ")");
        }
    }
    emit(TokenType::RParen, ")");
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
