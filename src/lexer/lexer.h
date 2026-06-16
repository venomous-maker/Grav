#ifndef GRAV_LEXER_H
#define GRAV_LEXER_H

#include <string>
#include <vector>

#include "lexer/token.h"

namespace grav {

// Converts Grav source text into a token stream. Whitespace (including
// newlines) is insignificant beyond separating tokens; statement boundaries
// are recovered by the parser. Throws GravError on an invalid character or
// unterminated string.
class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> tokenize();

private:
    char peek() const;
    char peekNext() const;
    char advance();
    bool match(char expected);
    bool atEnd() const;

    void skipWhitespaceAndComments();
    Token makeToken(TokenType type, std::string lexeme) const;
    Token scanNumber();
    Token scanString();
    // Scans a (possibly interpolated) string. A plain string pushes one
    // StringLiteral; `"a${e}b"` pushes the desugared token stream
    // `( "a" + str( <tokens of e> ) + "b" )`.
    void scanStringInto(std::vector<Token> &out);
    Token scanIdentifierOrKeyword();
    Token scanCBlock(); // `%{ ... %}` verbatim C

    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    // location where the current token started
    int tokLine_ = 1;
    int tokCol_ = 1;
};

} // namespace grav

#endif // GRAV_LEXER_H
