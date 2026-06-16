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

ExprPtr Parser::parseExpression() { return parseTernary(); }

ExprPtr Parser::parseTernary() {
    ExprPtr cond = parseCoalesce();
    if (!check(TokenType::Question)) return cond;
    const Token &q = advance(); // '?'
    auto e = at<TernaryExpr>(q);
    e->cond = std::move(cond);
    e->thenExpr = parseExpression();
    expect(TokenType::Colon, "in a ternary expression");
    e->elseExpr = parseTernary(); // right-associative
    return e;
}

ExprPtr Parser::parseCoalesce() {
    ExprPtr left = parseOr();
    while (check(TokenType::QuestionQuestion)) {
        const Token &opTok = advance();
        auto e = at<CoalesceExpr>(opTok);
        e->left = std::move(left);
        e->right = parseOr();
        left = std::move(e);
    }
    return left;
}

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
    ExprPtr left = parseBitOr();
    while (check(TokenType::AmpAmp)) {
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = BinaryOp::And;
        bin->left = std::move(left);
        bin->right = parseBitOr();
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parseBitOr() {
    ExprPtr left = parseBitXor();
    while (check(TokenType::Pipe)) {
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = BinaryOp::BitOr;
        bin->left = std::move(left);
        bin->right = parseBitXor();
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parseBitXor() {
    ExprPtr left = parseBitAnd();
    while (check(TokenType::Caret)) {
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = BinaryOp::BitXor;
        bin->left = std::move(left);
        bin->right = parseBitAnd();
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parseBitAnd() {
    ExprPtr left = parseComparison();
    // A line-leading `&` is address-of starting a new statement, not binary AND.
    while (check(TokenType::Amp) && !onNewLine()) {
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = BinaryOp::BitAnd;
        bin->left = std::move(left);
        bin->right = parseComparison();
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parseComparison() {
    ExprPtr left = parseShift();
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
        bin->right = parseShift();
        left = std::move(bin);
    }
}

ExprPtr Parser::parseShift() {
    ExprPtr left = parseAdditive();
    for (;;) {
        BinaryOp op;
        if (check(TokenType::ShiftLeft)) op = BinaryOp::Shl;
        else if (check(TokenType::ShiftRight)) op = BinaryOp::Shr;
        else return left;
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
        // A line-leading `+`/`-` begins a new statement (unary), not a binary op.
        if ((check(TokenType::Plus) || check(TokenType::Minus)) && onNewLine())
            return left;
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
    ExprPtr left = parseAsIs();
    for (;;) {
        // A line-leading `*` is a dereference starting a new statement.
        if (check(TokenType::Star) && onNewLine()) return left;
        BinaryOp op;
        if (check(TokenType::Star)) op = BinaryOp::Mul;
        else if (check(TokenType::Slash)) op = BinaryOp::Div;
        else if (check(TokenType::Percent)) op = BinaryOp::Mod;
        else return left;
        const Token &opTok = advance();
        auto bin = at<BinaryExpr>(opTok);
        bin->op = op;
        bin->left = std::move(left);
        bin->right = parseAsIs();
        left = std::move(bin);
    }
}

// `expr as Type` (cast) and `expr is ClassName` (RTTI test). Left-associative.
ExprPtr Parser::parseAsIs() {
    ExprPtr left = parseUnary();
    for (;;) {
        if (check(TokenType::As)) {
            const Token &kw = advance();
            auto e = at<AsExpr>(kw);
            e->operand = std::move(left);
            e->target = parseType("after 'as'");
            left = std::move(e);
        } else if (check(TokenType::Is)) {
            const Token &kw = advance();
            auto e = at<IsExpr>(kw);
            e->operand = std::move(left);
            e->typeName = parseQualifiedName("after 'is'");
            left = std::move(e);
        } else {
            return left;
        }
    }
}

// Lookahead for a C-style cast `( Type )value`. `pos_` is at the '('.
bool Parser::looksLikeCast() const {
    size_t i = pos_ + 1;
    if (i >= tokens_.size()) return false;
    TokenType t0 = tokens_[i].type;
    bool primitive = t0 == TokenType::KwInt || t0 == TokenType::KwFloat ||
                     t0 == TokenType::KwBool || t0 == TokenType::KwString ||
                     t0 == TokenType::KwVoid;
    bool named = t0 == TokenType::Identifier;
    if (!primitive && !named) return false;
    ++i; // consume first type token
    if (named)
        while (i + 1 < tokens_.size() && tokens_[i].type == TokenType::Dot &&
               tokens_[i + 1].type == TokenType::Identifier)
            i += 2;
    while (i < tokens_.size() && tokens_[i].type == TokenType::Star) ++i; // pointers
    if (i >= tokens_.size() || tokens_[i].type != TokenType::RParen) return false;
    ++i; // past ')'
    if (i >= tokens_.size()) return false;
    switch (tokens_[i].type) {
        // Unambiguous value starters -> a cast.
        case TokenType::Identifier: case TokenType::IntLiteral:
        case TokenType::FloatLiteral: case TokenType::StringLiteral:
        case TokenType::True: case TokenType::False: case TokenType::Null:
        case TokenType::Self: case TokenType::New: case TokenType::LParen:
        case TokenType::Bang: case TokenType::Tilde:
            return true;
        // `-`, `*`, `&` are also binary ops; only a primitive type disambiguates
        // them (a value can't be named `int`), so `(Foo) - x` stays a subtraction.
        case TokenType::Minus: case TokenType::Star: case TokenType::Amp:
            return primitive;
        default:
            return false;
    }
}

ExprPtr Parser::parseUnary() {
    if (check(TokenType::LParen) && looksLikeCast()) {
        const Token &lp = advance(); // '('
        TypeRef ty = parseType("for the cast");
        expect(TokenType::RParen, "after the cast type");
        auto e = at<AsExpr>(lp);
        e->target = ty;
        e->operand = parseUnary();
        return e;
    }
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
    if (check(TokenType::Tilde)) {
        const Token &opTok = advance();
        auto u = at<UnaryExpr>(opTok);
        u->op = UnaryOp::BitNot;
        u->operand = parseUnary();
        return u;
    }
    if (check(TokenType::Await)) {
        const Token &opTok = advance();
        auto a = at<AwaitExpr>(opTok);
        a->operand = parseUnary();
        return a;
    }
    if (check(TokenType::Amp)) { // &lvalue -> address-of
        const Token &opTok = advance();
        auto a = at<AddrOfExpr>(opTok);
        a->operand = parseUnary();
        return a;
    }
    if (check(TokenType::Star)) { // *ptr -> dereference
        const Token &opTok = advance();
        auto d = at<DerefExpr>(opTok);
        d->operand = parseUnary();
        return d;
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
        } else if (check(TokenType::QuestionDot)) {
            // `a?.b` — optional chaining: guard against a null `a`.
            const Token &dot = advance();
            const Token &name = expect(TokenType::Identifier, "after '?.'");
            auto member = at<MemberExpr>(dot);
            member->object = std::move(expr);
            member->member = name.lexeme;
            member->optional = true;
            expr = std::move(member);
        } else if (check(TokenType::Arrow)) {
            // `p->m` — pointer member access, i.e. `(*p).m`.
            const Token &arrow = advance();
            const Token &name = expect(TokenType::Identifier, "after '->'");
            auto deref = at<DerefExpr>(arrow);
            deref->operand = std::move(expr);
            auto member = at<MemberExpr>(arrow);
            member->object = std::move(deref);
            member->member = name.lexeme;
            expr = std::move(member);
        } else if (check(TokenType::ColonColon)) {
            // Turbofish: `id::<int>(x)` — explicit generic arguments on a call.
            advance(); // '::'
            std::vector<TypeRef> targs = parseTypeArgs();
            const Token &lp = peek();
            auto call = at<CallExpr>(lp);
            call->typeArgs = std::move(targs);
            call->args = parseArguments();
            call->callee = std::move(expr);
            expr = std::move(call);
        } else if (check(TokenType::LParen)) {
            const Token &lp = peek();
            auto call = at<CallExpr>(lp);
            call->args = parseArguments();
            call->callee = std::move(expr);
            expr = std::move(call);
        } else if (check(TokenType::LBracket) && !onNewLine()) {
            // `base[index]` — element access. A line-leading `[` begins a new
            // statement (an array literal), not an index of the previous line.
            const Token &lb = advance();
            auto idx = at<IndexExpr>(lb);
            idx->base = std::move(expr);
            idx->index = parseExpression();
            expect(TokenType::RBracket, "to close the index");
            expr = std::move(idx);
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
    // Skip a balanced generic argument list `<...>` (a `>>` closes two levels).
    if (i < tokens_.size() && tokens_[i].type == TokenType::Less) {
        int depth = 0;
        while (i < tokens_.size()) {
            TokenType tt = tokens_[i].type;
            if (tt == TokenType::Less) depth += 1;
            else if (tt == TokenType::Greater) depth -= 1;
            else if (tt == TokenType::ShiftRight) depth -= 2;
            ++i;
            if (depth <= 0) break;
        }
        if (depth > 0) return false;
    }
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
    if (check(TokenType::Less)) e->typeArgs = parseTypeArgs(); // `Box<int> { ... }`
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

// Lookahead from the token after `sizeof(`: does the parenthesized content spell
// a *definite* type (a primitive, or a named type with a `*`/`[N]`/dotted suffix)
// rather than an expression? A lone identifier stays an expression and is sorted
// out by the type checker, which can tell a type name from a variable.
bool Parser::looksLikeTypeArg() const {
    size_t i = pos_;
    if (i >= tokens_.size()) return false;
    TokenType t0 = tokens_[i].type;
    bool primitive = t0 == TokenType::KwInt || t0 == TokenType::KwFloat ||
                     t0 == TokenType::KwBool || t0 == TokenType::KwString ||
                     t0 == TokenType::KwVoid;
    bool named = t0 == TokenType::Identifier;
    if (!primitive && !named) return false;
    ++i;
    bool sawExtra = false;
    if (named)
        while (i + 1 < tokens_.size() && tokens_[i].type == TokenType::Dot &&
               tokens_[i + 1].type == TokenType::Identifier) { i += 2; sawExtra = true; }
    for (;;) {
        if (i < tokens_.size() && tokens_[i].type == TokenType::Star) { ++i; sawExtra = true; }
        else if (i + 2 < tokens_.size() && tokens_[i].type == TokenType::LBracket &&
                 tokens_[i + 1].type == TokenType::IntLiteral &&
                 tokens_[i + 2].type == TokenType::RBracket) { i += 3; sawExtra = true; }
        else break;
    }
    if (i >= tokens_.size() || tokens_[i].type != TokenType::RParen) return false;
    return primitive || sawExtra;
}

ExprPtr Parser::parsePrimary() {
    const Token &t = peek();
    switch (t.type) {
        case TokenType::CBlock: { // `%{ ... %}` inline C as an expression
            advance();
            auto e = at<CBlockExpr>(t);
            e->code = t.lexeme;
            return e;
        }
        case TokenType::Sizeof: {
            advance(); // 'sizeof'
            expect(TokenType::LParen, "after 'sizeof'");
            auto e = at<SizeofExpr>(t);
            if (looksLikeTypeArg()) {
                e->isType = true;
                e->target = parseType("in 'sizeof'");
            } else {
                e->operand = parseExpression();
            }
            expect(TokenType::RParen, "to close 'sizeof'");
            return e;
        }
        case TokenType::LBracket: { // `[a, b, c]` — an array literal
            advance(); // '['
            auto e = at<ArrayLiteralExpr>(t);
            if (!check(TokenType::RBracket)) {
                do {
                    e->elements.push_back(parseExpression());
                } while (matchToken(TokenType::Comma));
            }
            expect(TokenType::RBracket, "to close the array literal");
            return e;
        }
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
        case TokenType::Null: {
            advance();
            return at<NullLiteralExpr>(t);
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
            if (check(TokenType::Less)) e->typeArgs = parseTypeArgs(); // `new Box<int>(...)`
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
