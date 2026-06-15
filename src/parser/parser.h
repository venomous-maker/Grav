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

    // --- declarations (parser_core.cpp) ---
    void parseTopLevel(Program &program);
    void parseNamespace(Program &program);
    DeclPtr parseClass(bool isAbstract);
    DeclPtr parseInterface();
    DeclPtr parseStruct();
    DeclPtr parseFunction();
    MethodDecl parseMethod(bool inInterface);
    ConstructorDecl parseConstructor();
    FieldDecl parseField(Access access, bool readonly);
    std::vector<Param> parseParams();
    TypeRef parseType(const char *context);
    std::string parseQualifiedName(const char *context);
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
    ExprPtr parseExpression();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseComparison();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    ExprPtr parseStructLiteral();
    bool looksLikeStructLiteral() const;
    std::vector<ExprPtr> parseArguments();

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    // Namespace path currently being parsed, e.g. {"geometry","shapes"}.
    std::vector<std::string> nsStack_;
};

} // namespace grav

#endif // GRAV_PARSER_H
