// Enums. A plain `enum Color { Red, Green }` is a C enum. An enum whose variants
// carry data, or that has type parameters or methods, is a data enum:
//
//     enum Shape {
//         Circle(radius: float),
//         Rect(w: float, h: float),
//         Empty,
//         fn area() -> float { ... }
//     }
//
// It is lowered here to ordinary classes, so the rest of the compiler needs
// nothing new to handle it:
//   - an abstract base class `Shape` (with the enum's methods, plus abstract
//     variantTag/variantName and, unless the enum defines them, toString and ==);
//   - one final class per variant, `Shape$Circle extends Shape`, with public
//     readonly payload fields, a constructor, and the generated overrides.
// `Shape.Circle(2.0)` becomes `new Shape$Circle(2.0)` (sema/enums.cpp), and
// `match` on a data enum is lowered by the checker (typechecker_match.cpp).
#include "parser/parser.h"

#include "common/diagnostics.h"
#include "lexer/lexer.h"

namespace grav {

namespace {

// Generated sources spell `$` as this marker, since `$` is not an identifier
// character; it is swapped back after lexing.
const std::string kDollar = "__grav_dollar__";

struct Variant {
    std::string name;
    std::vector<std::string> fieldNames;
    std::vector<std::string> fieldTypes; // source text of each payload type
    int line = 0, col = 0;
    bool hasValue = false; // `A = 5` (plain enums only)
    long long value = 0;
};

} // namespace

void Parser::parseGenerated(const std::string &code, const Token &at, Program &out) {
    Lexer lexer(code);
    std::vector<Token> toks = lexer.tokenize();
    for (auto &t : toks) {
        t.line = at.line; // diagnostics point at the enum declaration
        t.col = at.col;
        if (t.type == TokenType::Identifier) {
            for (size_t p; (p = t.lexeme.find(kDollar)) != std::string::npos;)
                t.lexeme.replace(p, kDollar.size(), "$");
        }
    }
    Parser sub(std::move(toks));
    sub.nsStack_ = nsStack_;
    while (!sub.atEnd()) sub.parseTopLevel(out);
}

void Parser::parseEnum(Program &program) {
    const Token &kw = expect(TokenType::Enum, "to begin an enum");
    const Token nameTok = expect(TokenType::Identifier, "after 'enum'");
    std::vector<std::string> bounds;
    std::vector<std::string> typeParams = parseTypeParams(&bounds);
    expect(TokenType::LBrace, "to open the enum body");

    std::vector<Variant> variants;
    std::vector<MethodDecl> methods;
    bool hasPayload = false;
    while (!check(TokenType::RBrace) && !atEnd()) {
        if (matchToken(TokenType::Comma) || matchToken(TokenType::Semicolon)) continue;
        // Methods: `fn name(...)`, optionally `public`/`private`/`static`.
        if (check(TokenType::Fn) || check(TokenType::Public) || check(TokenType::Private) ||
            check(TokenType::Static)) {
            Access access = Access::Public;
            if (matchToken(TokenType::Private)) access = Access::Private;
            else matchToken(TokenType::Public);
            bool isStatic = matchToken(TokenType::Static);
            MethodDecl m = parseMethod(/*inInterface=*/false);
            m.access = access;
            m.isStatic = isStatic;
            methods.push_back(std::move(m));
            continue;
        }
        if (!methods.empty())
            fail(peek(), "enum variants must come before the enum's methods");
        const Token &vt = expect(TokenType::Identifier, "as an enum variant");
        Variant v;
        v.name = vt.lexeme;
        v.line = vt.line;
        v.col = vt.col;
        for (const auto &o : variants)
            if (o.name == v.name) fail(vt, "duplicate enum variant '" + v.name + "'");
        if (matchToken(TokenType::Assign)) { // `A = 5` / `B = -1`
            bool neg = matchToken(TokenType::Minus);
            const Token &num = expect(TokenType::IntLiteral, "as the enum member's value");
            v.hasValue = true;
            v.value = std::stoll(num.lexeme) * (neg ? -1 : 1);
        } else if (matchToken(TokenType::LParen)) {
            hasPayload = true;
            std::vector<bool> named;
            while (!check(TokenType::RParen)) {
                std::string fname;
                if (check(TokenType::Identifier) && peekAt(1).type == TokenType::Colon) {
                    fname = advance().lexeme;
                    advance(); // ':'
                }
                size_t start = pos_;
                parseType("for the variant's payload");
                std::string text;
                for (size_t i = start; i < pos_; ++i) {
                    // `>>` closing two generic lists may have been split in place.
                    text += (i > start ? " " : "") + tokens_[i].lexeme;
                }
                named.push_back(!fname.empty());
                v.fieldNames.push_back(fname);
                v.fieldTypes.push_back(text);
                if (!matchToken(TokenType::Comma)) break;
            }
            expect(TokenType::RParen, "to close the variant's payload");
            // Unnamed payloads: a single one is `value`, several are `_0`, `_1`, ...
            for (size_t i = 0; i < v.fieldNames.size(); ++i) {
                if (!v.fieldNames[i].empty()) continue;
                v.fieldNames[i] = v.fieldNames.size() == 1 ? "value" : "_" + std::to_string(i);
            }
            for (size_t i = 0; i < v.fieldNames.size(); ++i)
                for (size_t j = 0; j < i; ++j)
                    if (v.fieldNames[i] == v.fieldNames[j])
                        fail(vt, "variant '" + v.name + "' has two fields named '" +
                                     v.fieldNames[i] + "'");
        }
        variants.push_back(std::move(v));
    }
    expect(TokenType::RBrace, "to close the enum body");
    if (variants.empty()) fail(nameTok, "enum '" + nameTok.lexeme + "' has no variants");

    // A plain enum stays a C enum.
    if (!hasPayload && typeParams.empty() && methods.empty()) {
        auto en = std::make_unique<EnumDecl>();
        en->line = kw.line;
        en->col = kw.col;
        en->name = nameTok.lexeme;
        en->fqName = qualify(en->name);
        long long next = 0;
        for (const auto &v : variants) {
            long long value = v.hasValue ? v.value : next;
            for (const auto &m : en->members)
                if (m.value == value)
                    fail(kw, "enum members '" + m.name + "' and '" + v.name + "' have the same value " + std::to_string(value));
            en->members.push_back(EnumMember{v.name, v.line, v.col, value});
            en->customValues = en->customValues || value != static_cast<long long>(en->members.size() - 1);
            next = value + 1;
        }
        program.decls.push_back(std::move(en));
        return;
    }
    for (const auto &v : variants)
        if (v.hasValue) fail(kw, "only enums without data or methods can give members values ('" + v.name + " = ...')");

    // ---- data enum: generate the classes as Grav source ----
    const std::string &E = nameTok.lexeme;
    std::string tps, tpsDecl;
    if (!typeParams.empty()) {
        tps = "<";
        tpsDecl = "<";
        for (size_t i = 0; i < typeParams.size(); ++i) {
            tps += (i ? ", " : "") + typeParams[i];
            tpsDecl += (i ? ", " : "") + typeParams[i] +
                       (i < bounds.size() && !bounds[i].empty() ? ": " + bounds[i] : "");
        }
        tps += ">";
        tpsDecl += ">";
    }
    bool userToString = false, userEq = false;
    for (const auto &m : methods) {
        if (m.name == "toString" && m.params.empty()) userToString = true;
        if (m.name == "op_eq") userEq = true;
    }
    std::string base = E + tps;
    std::string code = "abstract class " + E + tpsDecl + " {\n"
                       "  abstract fn variantTag() -> int\n"
                       "  abstract fn variantName() -> string\n";
    if (!userToString) code += "  abstract fn toString() -> string\n";
    if (!userEq) code += "  abstract fn operator==(other: " + base + ") -> bool\n";
    code += "}\n";
    for (size_t k = 0; k < variants.size(); ++k) {
        const Variant &v = variants[k];
        std::string cls = E + kDollar + v.name;
        std::string tag = std::to_string(k);
        code += "final class " + cls + tpsDecl + " extends " + base + " {\n";
        for (size_t i = 0; i < v.fieldNames.size(); ++i)
            code += "  public readonly " + v.fieldNames[i] + ": " + v.fieldTypes[i] + "\n";
        if (!v.fieldNames.empty()) {
            code += "  constructor(";
            for (size_t i = 0; i < v.fieldNames.size(); ++i)
                code += (i ? ", " : "") + v.fieldNames[i] + ": " + v.fieldTypes[i];
            code += ") {";
            for (const auto &f : v.fieldNames) code += " self." + f + " = " + f + ";";
            code += " }\n";
        }
        code += "  fn variantTag() -> int { return " + tag + " }\n";
        code += "  fn variantName() -> string { return \"" + v.name + "\" }\n";
        if (!userToString) {
            code += "  fn toString() -> string { return \"" + v.name;
            if (!v.fieldNames.empty()) {
                code += "(\"";
                for (size_t i = 0; i < v.fieldNames.size(); ++i)
                    code += std::string(i ? " + \", \"" : "") + " + __show(self." + v.fieldNames[i] + ")";
                code += " + \")";
            }
            code += "\" }\n";
        }
        if (!userEq) {
            code += "  fn operator==(other: " + base + ") -> bool {"
                    " if (%{ other == 0 %} as bool) { return false; }"
                    " if (other.variantTag() != " + tag + ") { return false; }";
            if (v.fieldNames.empty()) {
                code += " return true; }\n";
            } else {
                code += " let o = other as " + cls + tps + "; return";
                for (size_t i = 0; i < v.fieldNames.size(); ++i)
                    code += std::string(i ? " &&" : "") + " self." + v.fieldNames[i] + " == o." +
                            v.fieldNames[i];
                code += "; }\n";
            }
        }
        code += "}\n";
    }

    Program gen;
    parseGenerated(code, kw, gen);
    std::vector<std::string> names;
    for (const auto &v : variants) names.push_back(v.name);
    for (size_t i = 0; i < gen.decls.size(); ++i) {
        auto *c = static_cast<ClassDecl *>(gen.decls[i].get());
        c->enumTemplate = qualify(E);
        if (i == 0) {
            c->enumVariants = names;
            for (auto &m : methods) c->methods.push_back(std::move(m));
        } else {
            c->variantTag = static_cast<int>(i - 1);
            c->variantFields = variants[i - 1].fieldNames;
        }
        program.decls.push_back(std::move(gen.decls[i]));
    }
}

} // namespace grav
