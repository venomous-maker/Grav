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
    // A member's name: an identifier, or any keyword. After `.` and as a method
    // or field name the position says it is a name (`req.string()`, `fn in()`).
    const Token &memberName(const char *context);
    bool atMemberName() const;
    [[noreturn]] void fail(const Token &at, const std::string &message);
    // True when the current token begins a new source line (used to stop an
    // expression before a line-leading prefix operator like `*p` / `&x`, which
    // begins a new statement rather than continuing a binary `*` / `&`).
    bool onNewLine() const;

    // --- declarations (parser_core.cpp) ---
    void parseTopLevel(Program &program);
    void parseNamespace(Program &program);
    void parseUse(Program &program); // `use a.b.*`, `use a.b.C [as D]`, `use a.b as x`
    DeclPtr parseClass(bool isAbstract);
    DeclPtr parseInterface();
    void parseTraitMember(InterfaceDecl &trait);
    StaticFieldDecl parseStaticField(Access access, bool readonly);
    DeclPtr parseStruct();
    // `enum` — a plain C enum, or a data enum (variants with payloads, type
    // parameters or methods) lowered to an abstract class plus one final class
    // per variant (parser_enum.cpp).
    void parseEnum(Program &program);
    // Parse generated Grav source `code` as top-level declarations placed at `at`.
    void parseGenerated(const std::string &code, const Token &at, Program &out);
    DeclPtr parseTypeAlias();
    DeclPtr parseGlobalVar(); // top-level `const`/`let`
    DeclPtr parseFunction(bool isAsync);
    MethodDecl parseMethod(bool inInterface);
    // `get name() -> T { }` / `set name(v: T) { }`: a property accessor.
    bool atProperty() const;
    MethodDecl parseProperty(bool inInterface);
    ConstructorDecl parseConstructor();
    FieldDecl parseField(Access access, bool readonly);
    std::vector<Param> parseParams(bool allowVariadic = false);
    TypeRef parseType(const char *context);
    static const TypeRef *builtinTypeName(const std::string &name);
    std::string parseQualifiedName(const char *context);
    // `@Name` / `@Name(literal, ...)` decorators before a declaration or member.
    std::vector<Decorator> parseDecorators();
    // Generics: `<T, U>` param lists on declarations, `<int, string>` argument
    // lists in type/call positions, and the `>`-splitting close that lets
    // `Box<Box<int>>` work despite the `>>` token.
    std::vector<std::string> parseTypeParams(std::vector<std::string> *bounds = nullptr);
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
    StmtPtr parseSwitch(bool asExpr = false); // also handles 'match' (asExpr: a match expression)
    Block parseArmValue();
    Block parseValueBranch(const char *what);
    StmtPtr parseIfValue();
    StmtPtr parseDestructure(const Token &kw);
    StmtPtr parseLocalFunction();
    bool destructureAhead() const;
    void parseStructMethod(StructDecl &st);
    std::vector<DeclPtr> pendingDecls_; // declarations made alongside the last one (struct methods)
    std::vector<StmtPtr> pendingStmts_; // more statements from the last one (destructuring)
    int destructureCounter_ = 0;
    bool extensionSelf_ = false;  // parsing an extension method: `self` is a parameter
    bool asyncMethods_ = false;   // an `async fn` method was parsed: add its helpers
    // Function bodies being parsed, innermost last: 0 cannot yield (a lambda), 1 may, 2 did.
    std::vector<int> yieldScopes_;
    bool lastArgsSpread_ = false; // the last parseArguments() ended with `...xs`
    Block parseCaseBody();
    void asyncMethod(MethodDecl &m, const Token &at);
    Block parseFunctionBody(TypeRef &returnType, const Token &at);
    bool atYield() const;
    bool atExtensionHead() const;
    StmtPtr parseYield();
    void parseCaseStatements(Block &b);
    StmtPtr parseTry();
    StmtPtr parseThrow();
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
    ExprPtr parsePower();
    ExprPtr parseAsIs();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    ExprPtr parseStructLiteral();
    // True when the parenthesized content at the current '(' (for sizeof) is a
    // type rather than an expression.
    bool looksLikeTypeArg() const;
    bool looksLikeStructLiteral() const;
    bool looksLikeGenericStatic() const;
    bool nullableMarkAhead() const;
    bool typeAfterOperator_ = false; // parsing the type of `x as T` / `x is T`
    bool looksLikeCast() const; // C-style `(Type)value` at the current '('
    std::vector<ExprPtr> parseArguments();

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    // Namespace path currently being parsed, e.g. {"geometry","shapes"}.
    std::vector<std::string> nsStack_;
};

} // namespace grav

#endif // GRAV_PARSER_H
