#include "parser/parser.h"

#include <charconv>

#include "common/diagnostics.h"

namespace grav {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ---------------------------------------------------------------------------
// Token cursor
// ---------------------------------------------------------------------------

const Token &Parser::peek() const { return tokens_[pos_]; }

const Token &Parser::peekAt(size_t offset) const {
    size_t i = pos_ + offset;
    if (i >= tokens_.size()) return tokens_.back(); // EOF
    return tokens_[i];
}

const Token &Parser::previous() const { return tokens_[pos_ - 1]; }
bool Parser::atEnd() const { return peek().type == TokenType::EndOfFile; }
bool Parser::check(TokenType type) const { return peek().type == type; }

const Token &Parser::advance() {
    if (!atEnd()) pos_++;
    return previous();
}

bool Parser::matchToken(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool Parser::onNewLine() const {
    if (pos_ == 0) return false;
    return peek().line != previous().line;
}

void Parser::fail(const Token &at, const std::string &message) {
    throw GravError("parse", at.line, at.col, message);
}

const Token &Parser::expect(TokenType type, const char *context) {
    if (check(type)) return advance();
    const Token &t = peek();
    fail(t, std::string("expected ") + tokenTypeName(type) + " " + context +
                ", but found " + tokenTypeName(t.type));
}

std::string Parser::qualify(const std::string &simpleName) const {
    std::string out;
    for (const auto &part : nsStack_) {
        out += part;
        out += '.';
    }
    out += simpleName;
    return out;
}

std::string Parser::parseQualifiedName(const char *context) {
    const Token &first = expect(TokenType::Identifier, context);
    std::string name = first.lexeme;
    while (check(TokenType::Dot) && peekAt(1).type == TokenType::Identifier) {
        advance(); // '.'
        name += '.';
        name += advance().lexeme;
    }
    return name;
}

// ---------------------------------------------------------------------------
// Program & top-level declarations
// ---------------------------------------------------------------------------

Program Parser::parseProgram() {
    Program program;
    while (!atEnd()) {
        parseTopLevel(program);
    }
    return program;
}

void Parser::parseTopLevel(Program &program) {
    switch (peek().type) {
        case TokenType::Namespace: parseNamespace(program); return;
        case TokenType::Class: program.decls.push_back(parseClass(false)); return;
        case TokenType::Abstract:
            advance(); // 'abstract'
            program.decls.push_back(parseClass(true));
            return;
        case TokenType::Interface: program.decls.push_back(parseInterface()); return;
        case TokenType::Struct: program.decls.push_back(parseStruct()); return;
        case TokenType::Enum: program.decls.push_back(parseEnum()); return;
        case TokenType::Type: program.decls.push_back(parseTypeAlias()); return;
        case TokenType::Fn: program.decls.push_back(parseFunction(false)); return;
        case TokenType::Async:
            advance(); // 'async'
            if (!check(TokenType::Fn))
                fail(peek(), "expected 'fn' after 'async'");
            program.decls.push_back(parseFunction(true));
            return;
        default:
            fail(peek(), std::string("expected a top-level declaration "
                                     "(namespace, class, struct, enum, type, interface, fn, "
                                     "async fn, or abstract class), but found ") +
                             tokenTypeName(peek().type));
    }
}

void Parser::parseNamespace(Program &program) {
    advance(); // 'namespace'
    std::string name = parseQualifiedName("after 'namespace'");
    // A dotted namespace name pushes several path segments.
    std::vector<std::string> pushed;
    size_t start = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '.') {
            pushed.push_back(name.substr(start, i - start));
            start = i + 1;
        }
    }
    for (auto &p : pushed) nsStack_.push_back(p);

    expect(TokenType::LBrace, "to open the namespace body");
    while (!check(TokenType::RBrace) && !atEnd()) {
        parseTopLevel(program);
    }
    expect(TokenType::RBrace, "to close the namespace body");

    for (size_t i = 0; i < pushed.size(); ++i) nsStack_.pop_back();
}

TypeRef Parser::parseType(const char *context) {
    const Token &t = peek();
    TypeRef base;
    switch (t.type) {
        case TokenType::KwInt: advance(); base = TypeRef::prim(TypeRef::Kind::Int); break;
        case TokenType::KwFloat: advance(); base = TypeRef::prim(TypeRef::Kind::Float); break;
        case TokenType::KwBool: advance(); base = TypeRef::prim(TypeRef::Kind::Bool); break;
        case TokenType::KwString: advance(); base = TypeRef::prim(TypeRef::Kind::String); break;
        case TokenType::KwVoid: advance(); base = TypeRef::prim(TypeRef::Kind::Void); break;
        case TokenType::Identifier:
            base = TypeRef::named(parseQualifiedName(context));
            break;
        default:
            fail(t, std::string("expected a type ") + context + ", but found " +
                        tokenTypeName(t.type));
    }
    // Postfix type operators: `*` makes a pointer (`int*`, `Point**`); `[N]` makes
    // a fixed-length array (`int[8]`, `Point[3]`). They compose left-to-right.
    for (;;) {
        if (matchToken(TokenType::Star)) {
            base = TypeRef::pointer(base);
        } else if (check(TokenType::LBracket)) {
            advance(); // '['
            const Token &n = expect(TokenType::IntLiteral,
                                    "as the array length in a `T[N]` type");
            long long len = 0;
            std::from_chars(n.lexeme.data(), n.lexeme.data() + n.lexeme.size(), len);
            if (len <= 0) fail(n, "array length must be a positive integer");
            expect(TokenType::RBracket, "to close the array length");
            base = TypeRef::array(base, static_cast<int>(len));
        } else {
            break;
        }
    }
    return base;
}

std::vector<Param> Parser::parseParams() {
    std::vector<Param> params;
    expect(TokenType::LParen, "to begin the parameter list");
    if (!check(TokenType::RParen)) {
        do {
            Param p;
            p.name = expect(TokenType::Identifier, "as a parameter name").lexeme;
            expect(TokenType::Colon, "after the parameter name");
            p.type = parseType("for the parameter");
            params.push_back(std::move(p));
        } while (matchToken(TokenType::Comma));
    }
    expect(TokenType::RParen, "to end the parameter list");
    return params;
}

DeclPtr Parser::parseFunction(bool isAsync) {
    const Token &kw = advance(); // 'fn'
    auto fn = std::make_unique<FunctionDecl>();
    fn->line = kw.line;
    fn->col = kw.col;
    fn->isAsync = isAsync;
    fn->name = expect(TokenType::Identifier, "after 'fn'").lexeme;
    fn->fqName = qualify(fn->name);
    fn->params = parseParams();
    if (matchToken(TokenType::Arrow)) {
        fn->returnType = parseType("as the return type");
    } else {
        fn->returnType = TypeRef::prim(TypeRef::Kind::Void);
    }
    fn->body = parseBlock();
    return fn;
}

DeclPtr Parser::parseInterface() {
    const Token &kw = advance(); // 'interface'
    auto iface = std::make_unique<InterfaceDecl>();
    iface->line = kw.line;
    iface->col = kw.col;
    iface->name = expect(TokenType::Identifier, "after 'interface'").lexeme;
    iface->fqName = qualify(iface->name);
    expect(TokenType::LBrace, "to open the interface body");
    while (!check(TokenType::RBrace) && !atEnd()) {
        iface->methods.push_back(parseMethod(/*inInterface=*/true));
    }
    expect(TokenType::RBrace, "to close the interface body");
    return iface;
}

DeclPtr Parser::parseStruct() {
    const Token &kw = expect(TokenType::Struct, "to begin a struct");
    auto st = std::make_unique<StructDecl>();
    st->line = kw.line;
    st->col = kw.col;
    st->name = expect(TokenType::Identifier, "after 'struct'").lexeme;
    st->fqName = qualify(st->name);
    expect(TokenType::LBrace, "to open the struct body");
    while (!check(TokenType::RBrace) && !atEnd()) {
        // Structs hold plain data: `name: type` fields only (always public).
        st->fields.push_back(parseField(Access::Public, /*readonly=*/false));
    }
    expect(TokenType::RBrace, "to close the struct body");
    return st;
}

DeclPtr Parser::parseEnum() {
    const Token &kw = expect(TokenType::Enum, "to begin an enum");
    auto en = std::make_unique<EnumDecl>();
    en->line = kw.line;
    en->col = kw.col;
    en->name = expect(TokenType::Identifier, "after 'enum'").lexeme;
    en->fqName = qualify(en->name);
    expect(TokenType::LBrace, "to open the enum body");
    if (!check(TokenType::RBrace)) {
        do {
            // Allow a trailing comma before the closing brace.
            if (check(TokenType::RBrace)) break;
            const Token &m = expect(TokenType::Identifier, "as an enum member");
            en->members.push_back(EnumMember{m.lexeme, m.line, m.col});
        } while (matchToken(TokenType::Comma));
    }
    expect(TokenType::RBrace, "to close the enum body");
    return en;
}

DeclPtr Parser::parseTypeAlias() {
    const Token &kw = expect(TokenType::Type, "to begin a type alias");
    auto alias = std::make_unique<TypeAliasDecl>();
    alias->line = kw.line;
    alias->col = kw.col;
    alias->name = expect(TokenType::Identifier, "after 'type'").lexeme;
    alias->fqName = qualify(alias->name);
    expect(TokenType::Assign, "after the alias name");
    alias->target = parseType("as the aliased type");
    matchToken(TokenType::Semicolon); // optional terminator
    return alias;
}

DeclPtr Parser::parseClass(bool isAbstract) {
    const Token &kw = expect(TokenType::Class, "to begin a class");
    auto cls = std::make_unique<ClassDecl>();
    cls->line = kw.line;
    cls->col = kw.col;
    cls->isAbstract = isAbstract;
    cls->name = expect(TokenType::Identifier, "after 'class'").lexeme;
    cls->fqName = qualify(cls->name);

    if (matchToken(TokenType::Extends)) {
        cls->baseName = parseQualifiedName("after 'extends'");
    }
    if (matchToken(TokenType::Implements)) {
        do {
            cls->interfaceNames.push_back(parseQualifiedName("after 'implements'"));
        } while (matchToken(TokenType::Comma));
    }

    expect(TokenType::LBrace, "to open the class body");
    while (!check(TokenType::RBrace) && !atEnd()) {
        // Optional access modifier.
        Access access = Access::Public;
        bool sawAccess = false;
        if (matchToken(TokenType::Public)) { access = Access::Public; sawAccess = true; }
        else if (matchToken(TokenType::Private)) { access = Access::Private; sawAccess = true; }
        else if (matchToken(TokenType::Protected)) { access = Access::Protected; sawAccess = true; }

        if (check(TokenType::Constructor)) {
            if (sawAccess) fail(peek(), "constructors cannot have an access modifier");
            cls->constructor = parseConstructor();
            continue;
        }

        bool isAbstractMethod = matchToken(TokenType::Abstract);
        bool isStatic = matchToken(TokenType::Static);
        bool isReadonly = matchToken(TokenType::Readonly);

        if (check(TokenType::Fn)) {
            // Abstract methods have no body (parsed like an interface signature).
            MethodDecl m = parseMethod(/*inInterface=*/isAbstractMethod);
            m.access = access;
            m.isStatic = isStatic;
            m.isAbstract = isAbstractMethod;
            if (isAbstractMethod && isStatic)
                fail(peek(), "a method cannot be both 'abstract' and 'static'");
            if (isReadonly) fail(peek(), "'readonly' cannot be applied to a method");
            cls->methods.push_back(std::move(m));
        } else if (check(TokenType::Identifier)) {
            if (isStatic) fail(peek(), "static fields are not supported in core v0.2");
            cls->fields.push_back(parseField(access, isReadonly));
        } else {
            fail(peek(), std::string("expected a field or method in the class "
                                     "body, but found ") +
                             tokenTypeName(peek().type));
        }
    }
    expect(TokenType::RBrace, "to close the class body");
    return cls;
}

FieldDecl Parser::parseField(Access access, bool readonly) {
    FieldDecl f;
    f.access = access;
    f.isReadonly = readonly;
    const Token &name = expect(TokenType::Identifier, "as a field name");
    f.name = name.lexeme;
    f.line = name.line;
    f.col = name.col;
    expect(TokenType::Colon, "after the field name");
    f.type = parseType("for the field");
    return f;
}

ConstructorDecl Parser::parseConstructor() {
    const Token &kw = advance(); // 'constructor'
    ConstructorDecl ctor;
    ctor.present = true;
    ctor.line = kw.line;
    ctor.col = kw.col;
    ctor.params = parseParams();
    ctor.body = parseBlock();
    return ctor;
}

MethodDecl Parser::parseMethod(bool inInterface) {
    const Token &kw = expect(TokenType::Fn, "to begin a method");
    MethodDecl m;
    m.line = kw.line;
    m.col = kw.col;
    m.name = expect(TokenType::Identifier, "after 'fn'").lexeme;
    m.params = parseParams();
    if (matchToken(TokenType::Arrow)) {
        m.returnType = parseType("as the return type");
    } else {
        m.returnType = TypeRef::prim(TypeRef::Kind::Void);
    }
    if (inInterface) {
        m.hasBody = false;
    } else {
        m.hasBody = true;
        m.body = parseBlock();
    }
    return m;
}

} // namespace grav
