#ifndef GRAV_PARSER_H
#define GRAV_PARSER_H

#include <string>
#include <vector>

#include "ast/ast.h"
#include "lexer/token.h"

namespace grav {

// Recursive-descent parser for Grav v0.2 (OOP + namespaces). The grammar is
// brace-delimited, so whitespace is insignificant. Implementation is split
// across parser_core.cpp (declarations), parser_stmt.cpp (statements) and
// parser_expr.cpp (expressions). Throws GravError on a syntax error.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Program parseProgram();

private:
    // --- token cursor (parser_core.cpp) ---
    const Token &peek() const;
    const Token &peekAt(size_t offset) const;
    const Token &previous() const;
    bool check(TokenType type) const;
    bool atEnd() const;
    const Token &advance();
    bool matchToken(TokenType type);
    const Token &expect(TokenType type, const char *context);
    [[noreturn]] void fail(const Token &at, const std::string &message);
    // True when the current token begins a new source line (used to stop an
    // expression before a line-leading prefix operator like `*p` / `&x`, which
    // begins a new statement rather than continuing a binary `*` / `&`).
    bool onNewLine() const;

    // --- declarations (parser_core.cpp) ---
    void parseTopLevel(Program &program);
    void parseNamespace(Program &program);
    DeclPtr parseClass(bool isAbstract);
    DeclPtr parseInterface();
    DeclPtr parseStruct();
    DeclPtr parseEnum();
    DeclPtr parseTypeAlias();
    DeclPtr parseFunction(bool isAsync);
    MethodDecl parseMethod(bool inInterface);
    ConstructorDecl parseConstructor();
    FieldDecl parseField(Access access, bool readonly);
    std::vector<Param> parseParams();
    TypeRef parseType(const char *context);
    std::string parseQualifiedName(const char *context);
    // Generics: `<T, U>` param lists on declarations, `<int, string>` argument
    // lists in type/call positions, and the `>`-splitting close that lets
    // `Box<Box<int>>` work despite the `>>` token.
    std::vector<std::string> parseTypeParams();
    std::vector<TypeRef> parseTypeArgs();
    void expectGenericClose();
    std::string qualify(const std::string &simpleName) const;

    // --- statements (parser_stmt.cpp) ---
    Block parseBlock();
    StmtPtr parseStatement();
    StmtPtr parseLet();
    StmtPtr parseReturn();
    StmtPtr parseExprOrAssign();
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseDoWhile();
    StmtPtr parseFor();
    StmtPtr parseSwitch(); // also handles 'match'
    StmtPtr parseSimpleStmt(); // for-loop init/update: let / assign / expr

    // --- expressions (parser_expr.cpp) ---
    // Precedence, lowest to highest: ternary < || < && < | < ^ < & <
    // (== !=  < > <= >=) < (<< >>) < (+ -) < (* / %) < (as/is) < unary < postfix.
    ExprPtr parseExpression();
    ExprPtr parseTernary();
    ExprPtr parseCoalesce();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseBitOr();
    ExprPtr parseBitXor();
    ExprPtr parseBitAnd();
    ExprPtr parseComparison();
    ExprPtr parseShift();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseAsIs();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    ExprPtr parseStructLiteral();
    // True when the parenthesized content at the current '(' (for sizeof) is a
    // type rather than an expression.
    bool looksLikeTypeArg() const;
    bool looksLikeStructLiteral() const;
    bool looksLikeCast() const; // C-style `(Type)value` at the current '('
    std::vector<ExprPtr> parseArguments();

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    // Namespace path currently being parsed, e.g. {"geometry","shapes"}.
    std::vector<std::string> nsStack_;
};

} // namespace grav

#endif // GRAV_PARSER_H
