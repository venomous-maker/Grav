#include "parser/parser.h"

#include <charconv>

#include "common/diagnostics.h"

namespace grav {

namespace {
template <typename T>
std::unique_ptr<T> at(const Token &tok) {
    auto e = std::make_unique<T>();
    e->line = tok.line;
    e->col = tok.col;
    return e;
}
} // namespace

ExprPtr Parser::parseExpression() { return parseOr(); }

ExprPtr Parser::parseOr() {
    ExprPtr left = parseAnd();
    while (check(TokenType::PipePipe)) {
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = BinaryOp::Or;
        bin->left = std::move(left);
        bin->right = parseAnd();
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parseAnd() {
    ExprPtr left = parseComparison();
    while (check(TokenType::AmpAmp)) {
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = BinaryOp::And;
        bin->left = std::move(left);
        bin->right = parseComparison();
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parseComparison() {
    ExprPtr left = parseAdditive();
    for (;;) {
        BinaryOp op;
        switch (peek().type) {
            case TokenType::EqEq: op = BinaryOp::Eq; break;
            case TokenType::NotEq: op = BinaryOp::NotEq; break;
            case TokenType::Greater: op = BinaryOp::Greater; break;
            case TokenType::Less: op = BinaryOp::Less; break;
            case TokenType::GreaterEq: op = BinaryOp::GreaterEq; break;
            case TokenType::LessEq: op = BinaryOp::LessEq; break;
            default: return left;
        }
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = op;
        bin->left = std::move(left);
        bin->right = parseAdditive();
        left = std::move(bin);
    }
}

ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    for (;;) {
        BinaryOp op;
        if (check(TokenType::Plus)) op = BinaryOp::Add;
        else if (check(TokenType::Minus)) op = BinaryOp::Sub;
        else return left;
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = op;
        bin->left = std::move(left);
        bin->right = parseMultiplicative();
        left = std::move(bin);
    }
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parseUnary();
    for (;;) {
        BinaryOp op;
        if (check(TokenType::Star)) op = BinaryOp::Mul;
        else if (check(TokenType::Slash)) op = BinaryOp::Div;
        else return left;
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = op;
        bin->left = std::move(left);
        bin->right = parseUnary();
        left = std::move(bin);
    }
}

ExprPtr Parser::parseUnary() {
    if (check(TokenType::Minus)) {
        const Token &opTok = advance();
        auto zero = at<IntLiteralExpr>(opTok);
        zero->value = 0;
        zero->raw = "0";
        auto bin = at<BinaryExpr>(opTok);
        bin->op = BinaryOp::Sub;
        bin->left = std::move(zero);
        bin->right = parseUnary();
        return bin;
    }
    if (check(TokenType::Bang)) {
        const Token &opTok = advance();
        auto u = at<UnaryExpr>(opTok);
        u->op = UnaryOp::Not;
        u->operand = parseUnary();
        return u;
    }
    if (check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) {
        const Token &opTok = advance();
        auto e = at<IncDecExpr>(opTok);
        e->isIncrement = (opTok.type == TokenType::PlusPlus);
        e->isPrefix = true;
        e->target = parseUnary();
        return e;
    }
    return parsePostfix();
}

std::vector<ExprPtr> Parser::parseArguments() {
    std::vector<ExprPtr> args;
    expect(TokenType::LParen, "to begin the argument list");
    if (!check(TokenType::RParen)) {
        do {
            args.push_back(parseExpression());
        } while (matchToken(TokenType::Comma));
    }
    expect(TokenType::RParen, "to end the argument list");
    return args;
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();
    for (;;) {
        if (check(TokenType::Dot)) {
            const Token &dot = advance();
            const Token &name = expect(TokenType::Identifier, "after '.'");
            auto member = at<MemberExpr>(dot);
            member->object = std::move(expr);
            member->member = name.lexeme;
            expr = std::move(member);
        } else if (check(TokenType::LParen)) {
            const Token &lp = peek();
            auto call = at<CallExpr>(lp);
            call->args = parseArguments();
            call->callee = std::move(expr);
            expr = std::move(call);
        } else if (check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) {
            const Token &opTok = advance();
            auto e = at<IncDecExpr>(opTok);
            e->isIncrement = (opTok.type == TokenType::PlusPlus);
            e->isPrefix = false;
            e->target = std::move(expr);
            expr = std::move(e);
        } else {
            return expr;
        }
    }
}

// A (possibly dotted) identifier followed by `{` that opens either an empty body
// or a `field:` initializer is a struct literal; anything else (e.g. a name that
// happens to precede a block) is not.
bool Parser::looksLikeStructLiteral() const {
    size_t i = pos_;
    if (i >= tokens_.size() || tokens_[i].type != TokenType::Identifier) return false;
    ++i;
    while (i + 1 < tokens_.size() && tokens_[i].type == TokenType::Dot &&
           tokens_[i + 1].type == TokenType::Identifier)
        i += 2;
    if (i >= tokens_.size() || tokens_[i].type != TokenType::LBrace) return false;
    size_t b = i + 1;
    if (b >= tokens_.size()) return false;
    if (tokens_[b].type == TokenType::RBrace) return true; // `Type {}`
    return b + 1 < tokens_.size() && tokens_[b].type == TokenType::Identifier &&
           tokens_[b + 1].type == TokenType::Colon; // `Type { field: ... }`
}

ExprPtr Parser::parseStructLiteral() {
    const Token &t = peek();
    auto e = at<StructLiteralExpr>(t);
    e->typeName = parseQualifiedName("for a struct literal");
    expect(TokenType::LBrace, "to open the struct literal");
    if (!check(TokenType::RBrace)) {
        do {
            StructFieldInit fi;
            const Token &name = expect(TokenType::Identifier, "as a struct field name");
            fi.name = name.lexeme;
            fi.line = name.line;
            fi.col = name.col;
            expect(TokenType::Colon, "after the struct field name");
            fi.value = parseExpression();
            e->fields.push_back(std::move(fi));
        } while (matchToken(TokenType::Comma));
    }
    expect(TokenType::RBrace, "to close the struct literal");
    return e;
}

ExprPtr Parser::parsePrimary() {
    const Token &t = peek();
    switch (t.type) {
        case TokenType::IntLiteral: {
            advance();
            auto e = at<IntLiteralExpr>(t);
            e->raw = t.lexeme;
            std::from_chars(t.lexeme.data(), t.lexeme.data() + t.lexeme.size(),
                            e->value);
            return e;
        }
        case TokenType::FloatLiteral: {
            advance();
            auto e = at<FloatLiteralExpr>(t);
            e->raw = t.lexeme;
            return e;
        }
        case TokenType::StringLiteral: {
            advance();
            auto e = at<StringLiteralExpr>(t);
            e->value = t.lexeme;
            return e;
        }
        case TokenType::True:
        case TokenType::False: {
            advance();
            auto e = at<BoolLiteralExpr>(t);
            e->value = (t.type == TokenType::True);
            return e;
        }
        case TokenType::Self: {
            advance();
            return at<SelfExpr>(t);
        }
        case TokenType::Identifier: {
            // `Type { field: value, ... }` — a struct literal. Disambiguated from
            // a plain name (and from a following block) by a short lookahead.
            if (looksLikeStructLiteral()) return parseStructLiteral();
            advance();
            auto e = at<NameExpr>(t);
            e->name = t.lexeme;
            return e;
        }
        case TokenType::New: {
            advance();
            auto e = at<NewExpr>(t);
            e->className = parseQualifiedName("after 'new'");
            e->args = parseArguments();
            return e;
        }
        case TokenType::LParen: {
            advance();
            ExprPtr inner = parseExpression();
            expect(TokenType::RParen, "to close the parenthesized expression");
            return inner;
        }
        // Primitive casts: int(...), float(...), bool(...), string(...).
        case TokenType::KwInt:
        case TokenType::KwFloat:
        case TokenType::KwBool:
        case TokenType::KwString: {
            TypeRef target = parseType("for the cast");
            expect(TokenType::LParen, "after the cast type");
            auto e = at<CastExpr>(t);
            e->target = target;
            e->operand = parseExpression();
            expect(TokenType::RParen, "to close the cast");
            return e;
        }
        default:
            fail(t, std::string("expected an expression, but found ") +
                        tokenTypeName(t.type));
    }
}

} // namespace grav
