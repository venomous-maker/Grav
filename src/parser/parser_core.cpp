#include "parser/parser.h"

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
        case TokenType::Fn: program.decls.push_back(parseFunction()); return;
        default:
            fail(peek(), std::string("expected a top-level declaration "
                                     "(namespace, class, struct, interface, fn, or "
                                     "abstract class), but found ") +
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
    switch (t.type) {
        case TokenType::KwInt: advance(); return TypeRef::prim(TypeRef::Kind::Int);
        case TokenType::KwFloat: advance(); return TypeRef::prim(TypeRef::Kind::Float);
        case TokenType::KwBool: advance(); return TypeRef::prim(TypeRef::Kind::Bool);
        case TokenType::KwString: advance(); return TypeRef::prim(TypeRef::Kind::String);
        case TokenType::KwVoid: advance(); return TypeRef::prim(TypeRef::Kind::Void);
        case TokenType::Identifier:
            return TypeRef::named(parseQualifiedName(context));
        default:
            fail(t, std::string("expected a type ") + context + ", but found " +
                        tokenTypeName(t.type));
    }
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

DeclPtr Parser::parseFunction() {
    const Token &kw = advance(); // 'fn'
    auto fn = std::make_unique<FunctionDecl>();
    fn->line = kw.line;
    fn->col = kw.col;
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
