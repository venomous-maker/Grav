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
    // Optional leading `@Decorator`s and an `export` modifier. These are metadata
    // only (every top-level name is already visible); they attach to the decl.
    std::vector<std::string> decorators;
    bool exported = false;
    while (check(TokenType::At) || check(TokenType::Export)) {
        if (matchToken(TokenType::Export)) { exported = true; continue; }
        advance(); // '@'
        decorators.push_back(parseQualifiedName("after '@'"));
        // Optional decorator argument list, e.g. `@route("/x")` — parsed and
        // discarded so the syntax is accepted.
        if (check(TokenType::LParen)) parseArguments();
    }
    bool decorated = !decorators.empty() || exported;

    size_t before = program.decls.size();
    switch (peek().type) {
        case TokenType::Namespace:
            if (decorated) fail(peek(), "decorators/'export' cannot be applied to a namespace");
            parseNamespace(program);
            return;
        case TokenType::Class: program.decls.push_back(parseClass(false)); break;
        case TokenType::Abstract:
            advance(); // 'abstract'
            program.decls.push_back(parseClass(true));
            break;
        case TokenType::Interface:
        case TokenType::Trait: program.decls.push_back(parseInterface()); break;
        case TokenType::Struct: program.decls.push_back(parseStruct()); break;
        case TokenType::Enum: program.decls.push_back(parseEnum()); break;
        case TokenType::Type: program.decls.push_back(parseTypeAlias()); break;
        case TokenType::Const:
        case TokenType::Let: program.decls.push_back(parseGlobalVar()); break;
        case TokenType::Fn: program.decls.push_back(parseFunction(false)); break;
        case TokenType::CBlock: {
            const Token &t = advance();
            auto cb = std::make_unique<CBlockDecl>();
            cb->line = t.line; cb->col = t.col;
            cb->code = t.lexeme;
            program.decls.push_back(std::move(cb));
            break;
        }
        case TokenType::Async:
            advance(); // 'async'
            if (!check(TokenType::Fn))
                fail(peek(), "expected 'fn' after 'async'");
            program.decls.push_back(parseFunction(true));
            break;
        default:
            fail(peek(), std::string("expected a top-level declaration "
                                     "(namespace, class, struct, enum, type, interface, fn, "
                                     "async fn, or abstract class), but found ") +
                             tokenTypeName(peek().type));
    }
    if (program.decls.size() > before) {
        Decl *d = program.decls.back().get();
        d->decorators = std::move(decorators);
        d->exported = exported;
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
        case TokenType::Self: // `Self` — the implementing type, inside a trait
            advance();
            base = TypeRef::named("Self");
            break;
        case TokenType::Identifier: {
            base = TypeRef::named(parseQualifiedName(context));
            // Generic arguments: `Box<int>`, `Pair<int, string>`.
            if (check(TokenType::Less)) base.args = parseTypeArgs();
            break;
        }
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

// Closes a `<...>` list. A `>` closes directly; a `>>`/`>=` token is split so the
// first `>` closes this list and the remainder stays for an enclosing list.
void Parser::expectGenericClose() {
    Token &t = tokens_[pos_];
    if (t.type == TokenType::Greater) { advance(); return; }
    if (t.type == TokenType::ShiftRight) { t.type = TokenType::Greater; t.lexeme = ">"; return; }
    if (t.type == TokenType::GreaterEq) { t.type = TokenType::Assign; t.lexeme = "="; return; }
    fail(t, std::string("expected '>' to close type arguments, but found ") +
                tokenTypeName(t.type));
}

std::vector<std::string> Parser::parseTypeParams(std::vector<std::string> *bounds) {
    std::vector<std::string> params;
    if (!matchToken(TokenType::Less)) return params;
    do {
        params.push_back(expect(TokenType::Identifier, "as a type parameter").lexeme);
        // Optional constraint: `T: Bound`.
        std::string b;
        if (matchToken(TokenType::Colon)) b = parseQualifiedName("as a type-parameter bound");
        if (bounds) bounds->push_back(b);
    } while (matchToken(TokenType::Comma));
    expectGenericClose();
    return params;
}

std::vector<TypeRef> Parser::parseTypeArgs() {
    std::vector<TypeRef> args;
    expect(TokenType::Less, "to begin type arguments");
    do {
        args.push_back(parseType("as a type argument"));
    } while (matchToken(TokenType::Comma));
    expectGenericClose();
    return args;
}

std::vector<Param> Parser::parseParams(bool allowVariadic) {
    std::vector<Param> params;
    expect(TokenType::LParen, "to begin the parameter list");
    if (!check(TokenType::RParen)) {
        do {
            Param p;
            if (check(TokenType::Ellipsis)) {
                if (!allowVariadic)
                    fail(peek(), "variadic parameters are only allowed on free functions");
                advance(); // '...'
                p.variadic = true;
            }
            p.name = expect(TokenType::Identifier, "as a parameter name").lexeme;
            expect(TokenType::Colon, "after the parameter name");
            p.type = parseType("for the parameter");
            bool isVar = p.variadic;
            params.push_back(std::move(p));
            if (isVar && check(TokenType::Comma))
                fail(peek(), "a variadic parameter must be the last parameter");
        } while (matchToken(TokenType::Comma));
    }
    expect(TokenType::RParen, "to end the parameter list");
    return params;
}

DeclPtr Parser::parseGlobalVar() {
    const Token &kw = advance(); // 'const' or 'let'
    auto g = std::make_unique<GlobalVarDecl>();
    g->line = kw.line;
    g->col = kw.col;
    g->isConst = (kw.type == TokenType::Const);
    g->name = expect(TokenType::Identifier, "after a global 'const'/'let'").lexeme;
    g->fqName = qualify(g->name);
    if (matchToken(TokenType::Colon)) {
        g->hasDeclaredType = true;
        g->declaredType = parseType("in the global declaration");
    }
    expect(TokenType::Assign, "after the global name");
    g->init = parseExpression();
    matchToken(TokenType::Semicolon); // optional terminator
    return g;
}

DeclPtr Parser::parseFunction(bool isAsync) {
    const Token &kw = advance(); // 'fn'
    auto fn = std::make_unique<FunctionDecl>();
    fn->line = kw.line;
    fn->col = kw.col;
    fn->isAsync = isAsync;
    fn->name = expect(TokenType::Identifier, "after 'fn'").lexeme;
    fn->fqName = qualify(fn->name);
    fn->typeParams = parseTypeParams(&fn->typeParamBounds); // optional <T, ...>
    fn->params = parseParams(/*allowVariadic=*/true);
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
    iface->typeParams = parseTypeParams(&iface->typeParamBounds); // optional <T, ...>
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
    st->typeParams = parseTypeParams(&st->typeParamBounds); // optional <T, ...>
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
    alias->typeParams = parseTypeParams(); // optional <T, ...>
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
    cls->typeParams = parseTypeParams(&cls->typeParamBounds); // optional <T, ...>

    if (matchToken(TokenType::Extends)) {
        cls->baseName = parseQualifiedName("after 'extends'");
        if (check(TokenType::Less)) cls->baseArgs = parseTypeArgs(); // extends Base<T>
        // `extends A, B, C` — additional bases are flattened (multiple inheritance).
        while (matchToken(TokenType::Comma)) {
            cls->extraBases.push_back(parseQualifiedName("after ','"));
            cls->extraBaseArgs.push_back(check(TokenType::Less) ? parseTypeArgs()
                                                                : std::vector<TypeRef>{});
        }
    }
    if (matchToken(TokenType::Implements)) {
        do {
            cls->interfaceNames.push_back(parseQualifiedName("after 'implements'"));
            cls->interfaceArgs.push_back(check(TokenType::Less) ? parseTypeArgs()
                                                                : std::vector<TypeRef>{});
        } while (matchToken(TokenType::Comma));
    }
    // Composition: `uses field: Type, ...` — each becomes a private delegate field.
    if (matchToken(TokenType::Uses)) {
        do {
            Param d;
            d.name = expect(TokenType::Identifier, "as a delegate field name").lexeme;
            expect(TokenType::Colon, "after the delegate field name");
            d.type = parseType("for the delegate");
            FieldDecl f;
            f.access = Access::Private;
            f.name = d.name;
            f.type = d.type;
            f.line = cls->line; f.col = cls->col;
            cls->fields.push_back(std::move(f));
            cls->delegates.push_back(std::move(d));
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
            if (isStatic) {
                // `static name: T = value` — a class-level field (a C global).
                StaticFieldDecl sf;
                sf.access = access;
                sf.isConst = isReadonly;
                const Token &nm = expect(TokenType::Identifier, "as a static field name");
                sf.name = nm.lexeme;
                sf.line = nm.line;
                sf.col = nm.col;
                expect(TokenType::Colon, "after the static field name");
                sf.type = parseType("for the static field");
                expect(TokenType::Assign, "a static field requires an initializer");
                sf.init = parseExpression();
                matchToken(TokenType::Semicolon);
                cls->staticFields.push_back(std::move(sf));
            } else {
                cls->fields.push_back(parseField(access, isReadonly));
            }
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
        // An interface/trait method with a `{ ... }` body is a *default* method
        // that implementers inherit unless they override it.
        if (check(TokenType::LBrace)) { m.hasBody = true; m.body = parseBlock(); }
        else m.hasBody = false;
    } else {
        m.hasBody = true;
        m.body = parseBlock();
    }
    return m;
}

} // namespace grav
