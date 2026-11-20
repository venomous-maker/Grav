#include "parser/parser.h"

#include "common/diagnostics.h"

#include <utility>

namespace grav {

Block Parser::parseBlock() {
    expect(TokenType::LBrace, "to open the block");
    Block block;
    localGenerics_.emplace_back();
    localConsts_.emplace_back();
    localVars_.emplace_back();
    for (const auto &v : std::exchange(nextBlockVars_, {})) declareLocal(v);
    while (!check(TokenType::RBrace) && !atEnd()) {
        if (matchToken(TokenType::Semicolon)) continue; // stray / empty statement
        size_t first = block.statements.size();
        block.statements.push_back(parseStatement());
        for (auto &extra : pendingStmts_) block.statements.push_back(std::move(extra)); // destructuring
        pendingStmts_.clear();
        for (size_t i = first; i < block.statements.size(); ++i) // the variables it declares
            if (auto *let = as<LetStmt>(block.statements[i].get())) declareLocal(let->name);
        // Statements may be terminated by ';' (recommended). Consume any here;
        // whitespace/newlines also separate statements when ';' is omitted.
        while (matchToken(TokenType::Semicolon)) {}
    }
    expect(TokenType::RBrace, "to close the block");
    localGenerics_.pop_back();
    localConsts_.pop_back();
    localVars_.pop_back();
    return block;
}

// `yield v` (contextual): in a function body, an identifier `yield` starting a
// statement that is not an assignment to, or a member or element of, a `yield`.
bool Parser::atYield() const {
    if (!check(TokenType::Identifier) || peek().lexeme != "yield" || yieldScopes_.empty()) return false;
    switch (peekAt(1).type) {
        case TokenType::Assign: case TokenType::PlusEq: case TokenType::MinusEq: case TokenType::StarEq:
        case TokenType::SlashEq: case TokenType::PercentEq: case TokenType::Dot: case TokenType::LBracket:
        case TokenType::Semicolon: case TokenType::RBrace: case TokenType::PlusPlus: case TokenType::MinusMinus:
            return false;
        default:
            return true;
    }
}

StmtPtr Parser::parseYield() {
    const Token &kw = advance(); // 'yield'
    if (yieldScopes_.back() == 0)
        fail(kw, "'yield' belongs to a function or method body, not a lambda");
    yieldScopes_.back() = 2;
    auto put = std::make_unique<MemberExpr>();
    put->line = kw.line;
    put->col = kw.col;
    auto self = std::make_unique<NameExpr>();
    self->line = kw.line;
    self->col = kw.col;
    self->name = "__yield";
    put->object = std::move(self);
    put->member = "put";
    auto call = std::make_unique<CallExpr>();
    call->line = kw.line;
    call->col = kw.col;
    call->callee = std::move(put);
    call->args.push_back(parseExpression());
    auto st = std::make_unique<ExprStmt>();
    st->line = kw.line;
    st->col = kw.col;
    st->expr = std::move(call);
    return st;
}

StmtPtr Parser::parseStatement() {
    if (atYield()) return parseYield();
    switch (peek().type) {
        case TokenType::Let:
        case TokenType::Const: return parseLet();
        case TokenType::Return: return parseReturn();
        case TokenType::If: return parseIf();
        case TokenType::While: return parseWhile();
        case TokenType::Do: return parseDoWhile();
        case TokenType::For: return parseFor();
        case TokenType::Switch:
        case TokenType::Match: return atName() ? parseExprOrAssign() : parseSwitch();
        case TokenType::Fn: // `fn name(...) { }` inside a function: a local function
            if (peekAt(1).type == TokenType::Identifier) return parseLocalFunction();
            return parseExprOrAssign();
        case TokenType::Try: return parseTry();
        case TokenType::Throw: return parseThrow();
        case TokenType::Break: {
            const Token &t = advance();
            auto s = std::make_unique<BreakStmt>();
            s->line = t.line; s->col = t.col;
            if (check(TokenType::Identifier) && !onNewLine()) s->label = advance().lexeme; // `break outer`
            return s;
        }
        case TokenType::Continue: {
            const Token &t = advance();
            auto s = std::make_unique<ContinueStmt>();
            s->line = t.line; s->col = t.col;
            if (check(TokenType::Identifier) && !onNewLine()) s->label = advance().lexeme;
            return s;
        }
        case TokenType::Identifier:
            // `outer: for (...)` — a labeled loop.
            if (peekAt(1).type == TokenType::Colon &&
                (peekAt(2).type == TokenType::For || peekAt(2).type == TokenType::While ||
                 peekAt(2).type == TokenType::Do)) {
                std::string label = advance().lexeme;
                advance(); // ':'
                StmtPtr loop = parseStatement();
                if (auto *w = as<WhileStmt>(loop.get())) w->label = label;
                else if (auto *d = as<DoWhileStmt>(loop.get())) d->label = label;
                else if (auto *f = as<ForStmt>(loop.get())) f->label = label;
                else if (auto *fi = as<ForInStmt>(loop.get())) fi->label = label;
                return loop;
            }
            // `defer` is contextual (still usable as a name): it starts a defer
            // statement only when followed by a block or another statement.
            if (peek().lexeme == "defer" &&
                (peekAt(1).type == TokenType::LBrace || peekAt(1).type == TokenType::Identifier ||
                 peekAt(1).type == TokenType::Self || peekAt(1).type == TokenType::Return)) {
                const Token &t = advance();
                auto s = std::make_unique<DeferStmt>();
                s->line = t.line; s->col = t.col;
                if (check(TokenType::LBrace)) {
                    s->body = parseBlock();
                } else {
                    s->body.statements.push_back(parseStatement());
                    matchToken(TokenType::Semicolon);
                }
                return s;
            }
            return parseExprOrAssign();
        case TokenType::LBrace: {
            const Token &t = peek();
            auto s = std::make_unique<BlockStmt>();
            s->line = t.line; s->col = t.col;
            s->block = parseBlock();
            return s;
        }
        default: return parseExprOrAssign();
    }
}

StmtPtr Parser::parseThrow() {
    const Token &kw = advance(); // 'throw'
    auto s = std::make_unique<ThrowStmt>();
    s->line = kw.line; s->col = kw.col;
    s->value = parseExpression();
    matchToken(TokenType::Semicolon);
    return s;
}

StmtPtr Parser::parseTry() {
    const Token &kw = advance(); // 'try'
    auto s = std::make_unique<TryStmt>();
    s->line = kw.line; s->col = kw.col;
    s->tryBlock = parseBlock();
    while (check(TokenType::Catch)) {
        const Token &ck = advance();
        CatchClause c;
        c.line = ck.line; c.col = ck.col;
        expect(TokenType::LParen, "after 'catch'");
        c.var = expectName("as the caught variable").lexeme;
        expect(TokenType::Colon, "after the caught variable");
        c.type = parseType("for the caught exception");
        expect(TokenType::RParen, "after the catch type");
        nextBlockVars_ = {c.var};
        c.block = parseBlock();
        s->catches.push_back(std::move(c));
    }
    if (matchToken(TokenType::Finally)) {
        s->hasFinally = true;
        s->finallyBlock = parseBlock();
    }
    if (s->catches.empty() && !s->hasFinally)
        fail(kw, "'try' requires a 'catch' or 'finally' clause");
    return s;
}

StmtPtr Parser::parseIf() {
    const Token &kw = advance(); // 'if'
    auto stmt = std::make_unique<IfStmt>();
    stmt->line = kw.line; stmt->col = kw.col;
    expect(TokenType::LParen, "after 'if'");
    stmt->cond = parseExpression();
    expect(TokenType::RParen, "after the condition");
    stmt->thenBlock = parseBlock();
    if (matchToken(TokenType::Else)) {
        // 'else if' chains; otherwise an else block.
        if (check(TokenType::If)) {
            stmt->elseStmt = parseIf();
        } else {
            const Token &t = peek();
            auto b = std::make_unique<BlockStmt>();
            b->line = t.line; b->col = t.col;
            b->block = parseBlock();
            stmt->elseStmt = std::move(b);
        }
    }
    return stmt;
}

StmtPtr Parser::parseWhile() {
    const Token &kw = advance(); // 'while'
    auto stmt = std::make_unique<WhileStmt>();
    stmt->line = kw.line; stmt->col = kw.col;
    expect(TokenType::LParen, "after 'while'");
    stmt->cond = parseExpression();
    expect(TokenType::RParen, "after the condition");
    stmt->body = parseBlock();
    return stmt;
}

StmtPtr Parser::parseDoWhile() {
    const Token &kw = advance(); // 'do'
    auto stmt = std::make_unique<DoWhileStmt>();
    stmt->line = kw.line; stmt->col = kw.col;
    stmt->body = parseBlock();
    expect(TokenType::While, "after the 'do' block");
    expect(TokenType::LParen, "after 'while'");
    stmt->cond = parseExpression();
    expect(TokenType::RParen, "after the condition");
    return stmt;
}

StmtPtr Parser::parseSimpleStmt() {
    if (check(TokenType::Let) || check(TokenType::Const)) return parseLet();
    return parseExprOrAssign();
}

// At `(`, `[` or `{` in a for-header: a destructuring pattern followed by `in`?
bool Parser::destructureAhead() const {
    size_t i = pos_;
    int depth = 0;
    do {
        if (i >= tokens_.size()) return false;
        TokenType tt = tokens_[i++].type;
        if (tt == TokenType::LParen || tt == TokenType::LBracket || tt == TokenType::LBrace) ++depth;
        else if (tt == TokenType::RParen || tt == TokenType::RBracket || tt == TokenType::RBrace) --depth;
        else if (tt != TokenType::Identifier && tt != TokenType::Comma) return false;
    } while (depth > 0);
    return i < tokens_.size() && tokens_[i].type == TokenType::In;
}

StmtPtr Parser::parseFor() {
    const Token &kw = advance(); // 'for'
    expect(TokenType::LParen, "after 'for'");

    // `for ((k, v) in pairs)` / `for ([a, b] in rows)` / `for ({x, y} in points)`:
    // each element goes into a hidden variable that the body destructures.
    if ((check(TokenType::LParen) || check(TokenType::LBracket) || check(TokenType::LBrace)) &&
        destructureAhead()) {
        auto stmt = std::make_unique<ForInStmt>();
        stmt->line = kw.line; stmt->col = kw.col;
        // Reuse `let <pattern> = <hidden>` by parsing the pattern against a placeholder.
        size_t patternAt = pos_;
        int depth = 0;
        do { // skip the pattern
            TokenType tt = advance().type;
            if (tt == TokenType::LParen || tt == TokenType::LBracket || tt == TokenType::LBrace) ++depth;
            if (tt == TokenType::RParen || tt == TokenType::RBracket || tt == TokenType::RBrace) --depth;
        } while (depth > 0 && !atEnd());
        expect(TokenType::In, "after the names");
        stmt->lo = parseExpression();
        expect(TokenType::RParen, "to close the for-header");
        size_t afterHeader = pos_;
        stmt->var = "destructured__" + std::to_string(destructureCounter_++);
        // Parse `<pattern> = <var>` from synthetic tokens: the pattern, '=', the name.
        // (The real tokens are moved aside and back, so references into them stay valid.)
        std::vector<Token> synth;
        size_t p = patternAt;
        depth = 0;
        do {
            const Token &tk = tokens_[p++];
            synth.push_back(tk);
            if (tk.type == TokenType::LParen || tk.type == TokenType::LBracket || tk.type == TokenType::LBrace) ++depth;
            if (tk.type == TokenType::RParen || tk.type == TokenType::RBracket || tk.type == TokenType::RBrace) --depth;
        } while (depth > 0);
        Token eq = synth.back(); eq.type = TokenType::Assign; eq.lexeme = "=";
        Token nm = synth.back(); nm.type = TokenType::Identifier; nm.lexeme = stmt->var;
        Token end = synth.back(); end.type = TokenType::Semicolon; end.lexeme = ";";
        synth.push_back(eq); synth.push_back(nm); synth.push_back(end);
        std::vector<Token> saved = std::move(tokens_);
        tokens_ = std::move(synth);
        pos_ = 0;
        StmtPtr hidden = parseDestructure(kw); // `let destructured__M = <var>` plus pendingStmts_
        tokens_ = std::move(saved);
        pos_ = afterHeader;
        std::vector<StmtPtr> binds = std::move(pendingStmts_);
        pendingStmts_.clear();
        nextBlockVars_ = {stmt->var};
        for (const auto &b : binds)
            if (auto *let = as<LetStmt>(b.get())) nextBlockVars_.push_back(let->name);
        stmt->body = parseBlock();
        auto &body = stmt->body.statements;
        body.insert(body.begin(), std::make_move_iterator(binds.begin()), std::make_move_iterator(binds.end()));
        body.insert(body.begin(), std::move(hidden));
        return stmt;
    }

    // `for (i in lo..hi [step s])`, `for (x in xs)`, `for (i, x in xs)`.
    bool pairForm = atName() && peekAt(1).type == TokenType::Comma &&
                    atName(2) && peekAt(3).type == TokenType::In;
    if (pairForm || (atName() && peekAt(1).type == TokenType::In)) {
        auto stmt = std::make_unique<ForInStmt>();
        stmt->line = kw.line; stmt->col = kw.col;
        if (pairForm) {
            stmt->indexVar = advance().lexeme;
            advance(); // ','
        }
        stmt->var = advance().lexeme; // loop variable
        advance();                    // 'in'
        stmt->lo = parseExpression();
        if (check(TokenType::DotDot) || check(TokenType::DotDotEq)) {
            if (pairForm) fail(peek(), "the 'i, x in' form iterates a collection, not a range");
            stmt->inclusive = advance().type == TokenType::DotDotEq;
            stmt->hi = parseExpression();
            // Optional contextual `step` (may be negative to count down).
            if (check(TokenType::Identifier) && peek().lexeme == "step") {
                advance();
                stmt->step = parseExpression();
            }
        }
        expect(TokenType::RParen, "to close the for-header");
        nextBlockVars_ = {stmt->var};
        if (!stmt->indexVar.empty()) nextBlockVars_.push_back(stmt->indexVar);
        stmt->body = parseBlock();
        return stmt;
    }

    auto stmt = std::make_unique<ForStmt>();
    stmt->line = kw.line; stmt->col = kw.col;
    localVars_.emplace_back(); // the initializer's variable
    if (!check(TokenType::Semicolon)) stmt->init = parseSimpleStmt();
    if (auto *let = as<LetStmt>(stmt->init.get())) declareLocal(let->name);
    expect(TokenType::Semicolon, "after the for-initializer");
    if (!check(TokenType::Semicolon)) stmt->cond = parseExpression();
    expect(TokenType::Semicolon, "after the for-condition");
    if (!check(TokenType::RParen)) stmt->update = parseSimpleStmt();
    expect(TokenType::RParen, "to close the for-header");
    stmt->body = parseBlock();
    localVars_.pop_back();
    return stmt;
}

// A case body: a `{ block }`, or the statements up to the next `case`, `default`
// or the closing brace (`case 1: log("one"); n += 1`).
Block Parser::parseCaseBody() {
    if (check(TokenType::LBrace)) {
        const Token &open = peek();
        int line = open.line, col = open.col;
        Block b = parseBlock();
        matchToken(TokenType::Semicolon);
        if (check(TokenType::Case) || check(TokenType::Default) || check(TokenType::RBrace)) return b;
        // `case 1: { ... } more()`: the block was the first statement of several.
        auto first = std::make_unique<BlockStmt>();
        first->line = line; first->col = col;
        first->block = std::move(b);
        Block all;
        all.statements.push_back(std::move(first));
        parseCaseStatements(all);
        return all;
    }
    if (check(TokenType::Case) || check(TokenType::Default) || check(TokenType::RBrace))
        fail(peek(), "a case needs a body; for several values in one case write `case a, b:`");
    Block b;
    parseCaseStatements(b);
    return b;
}

void Parser::parseCaseStatements(Block &b) {
    while (!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RBrace) && !atEnd()) {
        b.statements.push_back(parseStatement());
        for (auto &extra : pendingStmts_) b.statements.push_back(std::move(extra));
        pendingStmts_.clear();
        while (matchToken(TokenType::Semicolon)) {}
    }
}

// In a `match` expression each arm is an expression: its value is the result.
Block Parser::parseArmValue() {
    const Token &t = peek();
    auto ret = std::make_unique<ReturnStmt>();
    ret->line = t.line; ret->col = t.col;
    ret->value = parseExpression();
    matchToken(TokenType::Comma); // arms may be separated by commas
    Block b;
    b.statements.push_back(std::move(ret));
    return b;
}

StmtPtr Parser::parseSwitch(bool asExpr) {
    const Token &kw = advance(); // 'switch' or 'match'
    auto stmt = std::make_unique<SwitchStmt>();
    stmt->isExpr = asExpr;
    stmt->line = kw.line; stmt->col = kw.col;
    expect(TokenType::LParen, "after 'switch'");
    stmt->subject = parseExpression();
    if (check(TokenType::Comma)) { // `match (a, b)`: a tuple of them
        auto tuple = std::make_unique<NewExpr>();
        tuple->line = stmt->subject->line; tuple->col = stmt->subject->col;
        tuple->args.push_back(std::move(stmt->subject));
        while (matchToken(TokenType::Comma)) tuple->args.push_back(parseExpression());
        if (tuple->args.size() > 4) fail(kw, "a tuple has 2 to 4 elements");
        size_t n = tuple->args.size();
        tuple->className = n == 2 ? "Pair" : n == 3 ? "Triple" : "Quad";
        stmt->subject = std::move(tuple);
    }
    expect(TokenType::RParen, "after the switch subject");
    expect(TokenType::LBrace, "to open the switch body");
    if (check(TokenType::Case) && peekAt(1).type == TokenType::LParen) {
        size_t save = pos_;
        advance();
        bool tuple = tuplePatternAhead();
        pos_ = save;
        if (tuple) return parseTupleMatch(kw, std::move(stmt->subject), asExpr);
    }
    while (!check(TokenType::RBrace) && !atEnd()) {
        if (matchToken(TokenType::Default)) {
            if (stmt->hasDefault) fail(previous(), "duplicate 'default' case");
            stmt->hasDefault = true;
            expect(TokenType::Colon, "after 'default'");
            stmt->defaultBody = asExpr ? parseArmValue() : parseCaseBody();
        } else {
            expect(TokenType::Case, "to begin a switch case");
            SwitchCase c;
            do {
                c.values.push_back(parseExpression());
                // `case 1..5:` / `case 1..=5:` — a range of values.
                bool incl = check(TokenType::DotDotEq);
                if (matchToken(TokenType::DotDot) || matchToken(TokenType::DotDotEq)) {
                    c.rangeHi.push_back(parseExpression());
                    c.rangeInclusive.push_back(incl);
                } else {
                    c.rangeHi.push_back(nullptr);
                    c.rangeInclusive.push_back(false);
                }
            } while (matchToken(TokenType::Comma));
            if (matchToken(TokenType::If)) c.guard = parseExpression(); // `case v if cond:`
            expect(TokenType::Colon, "after the case value");
            c.body = asExpr ? parseArmValue() : parseCaseBody();
            stmt->cases.push_back(std::move(c));
        }
    }
    expect(TokenType::RBrace, "to close the switch body");
    return stmt;
}

// At a `(` that opens a tuple pattern: a parenthesized list with a comma at its top level.
bool Parser::tuplePatternAhead() const {
    if (!check(TokenType::LParen)) return false;
    int depth = 0;
    for (size_t i = pos_; i < tokens_.size(); ++i) {
        TokenType tt = tokens_[i].type;
        if (tt == TokenType::LParen || tt == TokenType::LBracket || tt == TokenType::LBrace) ++depth;
        else if (tt == TokenType::RParen || tt == TokenType::RBracket || tt == TokenType::RBrace) {
            if (--depth == 0) return false;
        } else if (tt == TokenType::Comma && depth == 1) return true;
        else if (tt == TokenType::Colon && depth == 0) return false;
    }
    return false;
}

ExprPtr Parser::patternExpr(const PatternPath &path, const Token &at) {
    auto n = std::make_unique<NameExpr>();
    n->line = at.line; n->col = at.col;
    n->name = path.var;
    ExprPtr e = std::move(n);
    for (const auto &f : path.fields) {
        auto m = std::make_unique<MemberExpr>();
        m->line = at.line; m->col = at.col;
        m->object = std::move(e);
        m->member = f;
        e = std::move(m);
    }
    return e;
}

namespace {
ExprPtr binary(BinaryOp op, ExprPtr l, ExprPtr r) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = l->line; b->col = l->col;
    b->op = op;
    b->left = std::move(l);
    b->right = std::move(r);
    return b;
}
} // namespace

// One tuple pattern at `path`: each element is `_`, a name (bound to that part),
// a value or range (`0`, "a", Color.Red, 1..5) that part must equal or lie in, or a
// tuple pattern again.
void Parser::parseTuplePattern(const PatternPath &path, std::vector<ExprPtr> &tests,
                               std::vector<std::pair<std::string, PatternPath>> &binds) {
    static const char *fields[] = {"first", "second", "third", "fourth"};
    const Token &open = expect(TokenType::LParen, "to begin the tuple pattern");
    size_t n = 0;
    do {
        if (n == 4) fail(open, "a tuple has 2 to 4 elements");
        PatternPath part = path;
        part.fields.push_back(fields[n++]);
        bool ends = peekAt(1).type == TokenType::Comma || peekAt(1).type == TokenType::RParen;
        if (check(TokenType::LParen) && tuplePatternAhead()) {
            parseTuplePattern(part, tests, binds);
        } else if (atName() && ends) {
            const Token &name = advance();
            if (name.lexeme == "_") continue;
            for (const auto &b : binds)
                if (b.first == name.lexeme) fail(name, "'" + name.lexeme + "' is bound twice in this pattern");
            binds.push_back({name.lexeme, part});
        } else {
            const Token &at = peek();
            ExprPtr lo = parseExpression();
            if (check(TokenType::DotDot) || check(TokenType::DotDotEq)) {
                bool incl = advance().type == TokenType::DotDotEq;
                ExprPtr hi = parseExpression();
                tests.push_back(binary(BinaryOp::And, binary(BinaryOp::GreaterEq, patternExpr(part, at), std::move(lo)),
                                       binary(incl ? BinaryOp::LessEq : BinaryOp::Less, patternExpr(part, at), std::move(hi))));
            } else {
                tests.push_back(binary(BinaryOp::Eq, patternExpr(part, at), std::move(lo)));
            }
        }
    } while (matchToken(TokenType::Comma));
    if (n < 2) fail(open, "a tuple pattern has 2 to 4 elements");
    expect(TokenType::RParen, "to close the tuple pattern");
}

// `match (t) { case (0, y) if y > 1: ... case (x, _): ... default: ... }` on a tuple
// (Pair, Triple, Quad) becomes
//
//     { let tuple__N = t
//       if (tuple__N.first == 0 && tuple__N.second > 1) { let y = tuple__N.second; ... }
//       else if (true) { let x = tuple__N.first; ... } else { ... } }
//
// (a guard reads the parts directly). A case of several patterns (`case (0, _), (_, 0):`)
// matches any of them, and binds nothing. A case that matches everything ends the chain.
StmtPtr Parser::parseTupleMatch(const Token &kw, ExprPtr subject, bool asExpr) {
    PatternPath root{"tuple__" + std::to_string(destructureCounter_++), {}};
    auto out = std::make_unique<BlockStmt>();
    out->line = kw.line; out->col = kw.col;
    auto let = std::make_unique<LetStmt>();
    let->line = kw.line; let->col = kw.col;
    let->name = root.var;
    let->quietUnused = true;
    let->init = std::move(subject);
    out->block.statements.push_back(std::move(let));
    StmtPtr *tail = nullptr;  // where the next case goes: the last if's else
    bool total = false;       // a case (or the default) matches whatever is left
    bool hasDefault = false;
    while (!check(TokenType::RBrace) && !atEnd()) {
        if (total && (check(TokenType::Case) || check(TokenType::Default)))
            fail(peek(), hasDefault ? "a case after 'default' can never match"
                                    : "this case can never match: an earlier one matches every tuple");
        Block body;
        ExprPtr cond;
        if (matchToken(TokenType::Default) ||
            (check(TokenType::Case) && peekAt(1).type == TokenType::Identifier && peekAt(1).lexeme == "_" &&
             peekAt(2).type == TokenType::Colon && (advance(), advance(), true))) { // `case _:` is `default:`
            expect(TokenType::Colon, "after 'default'");
            body = asExpr ? parseArmValue() : parseCaseBody();
            total = hasDefault = true;
        } else {
            const Token &ck = expect(TokenType::Case, "to begin a switch case");
            std::vector<ExprPtr> alts;
            std::vector<std::pair<std::string, PatternPath>> binds;
            size_t patterns = 0;
            do {
                if (!tuplePatternAhead()) fail(peek(), "every case of a match on a tuple is a tuple pattern: `case (a, _):`");
                std::vector<ExprPtr> tests;
                parseTuplePattern(root, tests, binds);
                ++patterns;
                ExprPtr all;
                for (auto &t : tests) all = all ? binary(BinaryOp::And, std::move(all), std::move(t)) : std::move(t);
                if (!all) { // matches every tuple
                    auto yes = std::make_unique<BoolLiteralExpr>();
                    yes->line = ck.line; yes->col = ck.col;
                    yes->value = true;
                    all = std::move(yes);
                    total = true;
                }
                alts.push_back(std::move(all));
            } while (matchToken(TokenType::Comma));
            if (patterns > 1 && !binds.empty())
                fail(ck, "a case with several patterns cannot bind names (each pattern would bind its own)");
            for (auto &a : alts) cond = cond ? binary(BinaryOp::Or, std::move(cond), std::move(a)) : std::move(a);
            if (matchToken(TokenType::If)) {
                std::map<std::string, PatternPath> names;
                for (const auto &b : binds) names[b.first] = b.second;
                patternNames_.push_back(std::move(names));
                ExprPtr guard = parseExpression();
                patternNames_.pop_back();
                cond = binary(BinaryOp::And, std::move(cond), std::move(guard));
                total = false;
            }
            expect(TokenType::Colon, "after the case pattern");
            body = asExpr ? parseArmValue() : parseCaseBody();
            std::vector<StmtPtr> lets;
            for (const auto &b : binds) {
                auto l = std::make_unique<LetStmt>();
                l->line = ck.line; l->col = ck.col;
                l->name = b.first;
                l->quietUnused = true; // (a guard may be its only use)
                l->init = patternExpr(b.second, ck);
                lets.push_back(std::move(l));
            }
            body.statements.insert(body.statements.begin(), std::make_move_iterator(lets.begin()),
                                   std::make_move_iterator(lets.end()));
        }
        StmtPtr next;
        if (total && (cond == nullptr || as<BoolLiteralExpr>(cond.get()))) { // the rest: no test
            auto b = std::make_unique<BlockStmt>();
            b->line = kw.line; b->col = kw.col;
            b->block = std::move(body);
            next = std::move(b);
        } else {
            auto iff = std::make_unique<IfStmt>();
            iff->line = kw.line; iff->col = kw.col;
            iff->cond = std::move(cond);
            iff->thenBlock = std::move(body);
            next = std::move(iff);
        }
        StmtPtr *slot = tail ? tail : nullptr;
        if (slot) *slot = std::move(next);
        else out->block.statements.push_back(std::move(next));
        StmtPtr &placed = slot ? *slot : out->block.statements.back();
        if (auto *iff = as<IfStmt>(placed.get())) tail = &iff->elseStmt;
        else tail = &placed; // (nothing may follow: `total`)
    }
    expect(TokenType::RBrace, "to close the match");
    if (asExpr && !total)
        fail(kw, "a match on a tuple used as a value needs a 'default' (or a last case that matches every tuple)");
    return out;
}

// `let (a, b) = pair` (tuple fields first/second/third/fourth), `let [x, y] = arr`
// (indexes) and `let {x, y} = point` (fields by name): the value goes into a
// hidden local, and each name is a `let` reading its part (`_` skips one).
StmtPtr Parser::parseDestructure(const Token &kw) {
    TokenType open = peek().type;
    TokenType close = open == TokenType::LParen ? TokenType::RParen
                    : open == TokenType::LBracket ? TokenType::RBracket : TokenType::RBrace;
    advance();
    std::vector<const Token *> names;
    do { names.push_back(&expectName("as a name to bind")); } while (matchToken(TokenType::Comma));
    expect(close, "to close the names");
    if (open == TokenType::LParen && (names.size() < 2 || names.size() > 4))
        fail(kw, "a tuple has 2 to 4 elements to bind");
    expect(TokenType::Assign, "after the names");
    auto hidden = std::make_unique<LetStmt>();
    hidden->line = kw.line; hidden->col = kw.col;
    hidden->name = "destructured__" + std::to_string(destructureCounter_++);
    hidden->isConst = true;
    hidden->init = parseExpression();
    static const char *tupleFields[] = {"first", "second", "third", "fourth"};
    for (size_t i = 0; i < names.size(); ++i) {
        const Token &n = *names[i];
        if (n.lexeme == "_") continue;
        auto src = std::make_unique<NameExpr>();
        src->line = n.line; src->col = n.col;
        src->name = hidden->name;
        ExprPtr part;
        if (open == TokenType::LBracket) {
            auto ix = std::make_unique<IndexExpr>();
            auto lit = std::make_unique<IntLiteralExpr>();
            lit->value = static_cast<long long>(i);
            lit->raw = std::to_string(i);
            lit->line = n.line; lit->col = n.col;
            ix->base = std::move(src);
            ix->index = std::move(lit);
            part = std::move(ix);
        } else {
            auto m = std::make_unique<MemberExpr>();
            m->object = std::move(src);
            m->member = open == TokenType::LParen ? tupleFields[i] : n.lexeme;
            part = std::move(m);
        }
        part->line = n.line; part->col = n.col;
        auto let = std::make_unique<LetStmt>();
        let->line = n.line; let->col = n.col;
        let->name = n.lexeme;
        let->isConst = kw.type == TokenType::Const;
        let->init = std::move(part);
        pendingStmts_.push_back(std::move(let));
    }
    return hidden;
}

void Parser::declareLocal(const std::string &name) {
    if (!localVars_.empty()) localVars_.back().push_back({name, ++localVarCounter_});
}

const Parser::LocalVar *Parser::findLocal(const std::string &name, size_t *depth) const {
    for (size_t d = localVars_.size(); d-- > 0;)
        for (auto v = localVars_[d].rbegin(); v != localVars_[d].rend(); ++v)
            if (v->name == name) {
                if (depth) *depth = d;
                return &*v;
            }
    return nullptr;
}

// `name` is read: each generic local function being parsed that it is a block
// variable from outside of captures it.
void Parser::noteLocalUse(const std::string &name) {
    if (captureFrames_.empty()) return;
    size_t depth = 0;
    const LocalVar *v = findLocal(name, &depth);
    if (!v) return;
    for (const auto &f : captureFrames_) {
        Captures &c = localCaptures_[f];
        if (depth >= c.base) continue;
        bool have = false;
        for (const auto &u : c.vars) have = have || u.id == v->id;
        if (!have) c.vars.push_back(*v);
    }
}

// A call of a generic local function passes the block variables it uses first
// (`auto` for their types when the call names its type arguments).
void Parser::completeLocalCall(CallExpr &call) {
    auto *callee = as<NameExpr>(call.callee.get());
    if (!callee) return;
    auto it = localCaptures_.find(callee->name);
    if (it == localCaptures_.end() || (it->second.vars.empty() && it->second.done)) return;
    Captures &c = it->second;
    if (!c.done) { c.pending.push_back(&call); return; } // a call in its own body
    std::vector<ExprPtr> args;
    for (const auto &v : c.vars) {
        const LocalVar *here = findLocal(v.name);
        if (here && here->id != v.id) {
            std::string fn = callee->name.substr(callee->name.find('_', 7) + 1);
            throw GravError("parse", call.line, call.col,
                            "'" + fn + "' uses the variable '" + v.name + "' declared before it, and here '" +
                                v.name + "' is another variable: rename one of them");
        }
        noteLocalUse(v.name);
        auto a = std::make_unique<NameExpr>();
        a->line = call.line;
        a->col = call.col;
        a->name = v.name;
        args.push_back(std::move(a));
    }
    call.args.insert(call.args.begin(), std::make_move_iterator(args.begin()), std::make_move_iterator(args.end()));
    if (!call.typeArgs.empty())
        call.typeArgs.insert(call.typeArgs.begin(), c.vars.size(), TypeRef::named("auto"));
}

// Assigning a block variable from inside a generic local function, which has a copy.
void Parser::checkCapturedWrite(const Expr &target, const Token &at) {
    auto *n = as<const NameExpr>(&target);
    if (!n || captureFrames_.empty()) return;
    size_t depth = 0;
    if (findLocal(n->name, &depth) && depth < localCaptures_[captureFrames_.back()].base)
        fail(at, "a generic local function cannot assign '" + n->name +
                     "', a variable of its block (it has a copy): use a non-generic local function or a lambda");
}

// A function declared inside a block: `const name = fn(params) -> R { body }`,
// which can call itself (and see the block's variables, captured like a lambda's).
// A generic one, `fn pair<T>(x: T)`, becomes a hidden top-level generic function
// that the rest of the block calls by its name. The block's variables it uses
// become hidden leading parameters (each of its own hidden type parameter) that
// every call passes: it sees their values at the call, and cannot assign them.
StmtPtr Parser::parseLocalFunction() {
    const Token &kw = advance(); // 'fn'
    const Token &name = expect(TokenType::Identifier, "after 'fn'");
    if (check(TokenType::Less)) {
        auto fn = std::make_unique<FunctionDecl>();
        fn->line = kw.line; fn->col = kw.col;
        fn->name = "__local" + std::to_string(++localGenericCounter_) + "_" + name.lexeme;
        fn->fqName = qualify(fn->name);
        localGenerics_.back()[name.lexeme] = fn->name; // before the body: it may call itself
        Captures &caps = localCaptures_[fn->name];
        caps.base = localVars_.size();
        captureFrames_.push_back(fn->name);
        fn->typeParams = parseTypeParams(&fn->typeParamBounds);
        fn->params = parseParams(/*allowVariadic=*/true);
        fn->returnType = matchToken(TokenType::Arrow) ? parseType("as the return type")
                                                      : TypeRef::prim(TypeRef::Kind::Void);
        bool saved = extensionSelf_;
        extensionSelf_ = false;
        fn->body = parseFunctionBody(fn->returnType, kw);
        extensionSelf_ = saved;
        captureFrames_.pop_back();
        // What it uses from the block: `fn __localN_f<__C1_n, T>(n: __C1_n, x: T)`.
        Captures &done = localCaptures_[fn->name]; // (the map may have grown)
        done.done = true;
        std::vector<std::string> tps;
        std::vector<Param> ps;
        for (const auto &v : done.vars) {
            tps.push_back("__C" + std::to_string(tps.size() + 1) + "_" + v.name);
            Param p;
            p.name = v.name;
            p.type = TypeRef::named(tps.back());
            ps.push_back(std::move(p));
        }
        fn->typeParamBounds.resize(fn->typeParams.size());
        fn->typeParamBounds.insert(fn->typeParamBounds.begin(), tps.size(), "");
        fn->typeParams.insert(fn->typeParams.begin(), tps.begin(), tps.end());
        fn->params.insert(fn->params.begin(), std::make_move_iterator(ps.begin()), std::make_move_iterator(ps.end()));
        for (CallExpr *call : std::exchange(done.pending, {})) completeLocalCall(*call);
        pendingDecls_.push_back(std::move(fn));
        auto empty = std::make_unique<BlockStmt>();
        empty->line = kw.line; empty->col = kw.col;
        return empty;
    }
    auto lam = std::make_unique<LambdaExpr>();
    lam->line = kw.line; lam->col = kw.col;
    lam->params = parseParams(/*allowVariadic=*/false);
    for (const auto &p : lam->params) {
        if (p.defaultValue) fail(name, "a function inside a block cannot have default values");
        lam->paramTyped.push_back(true);
    }
    lam->hasReturnType = true;
    lam->returnType = matchToken(TokenType::Arrow) ? parseType("as the return type") : TypeRef::prim(TypeRef::Kind::Void);
    yieldScopes_.push_back(0); // (a local function is a lambda: it cannot yield)
    lam->body = parseBlock();
    yieldScopes_.pop_back();
    lam->selfName = name.lexeme;
    auto let = std::make_unique<LetStmt>();
    let->line = kw.line; let->col = kw.col;
    let->name = name.lexeme;
    let->isConst = true;
    let->quietUnused = true; // like a function, it may be declared and not used
    let->init = std::move(lam);
    return let;
}

StmtPtr Parser::parseLet() {
    const Token &kw = advance(); // 'let' or 'const'
    if (check(TokenType::LParen) || check(TokenType::LBracket) || check(TokenType::LBrace))
        return parseDestructure(kw);
    auto stmt = std::make_unique<LetStmt>();
    stmt->line = kw.line;
    stmt->col = kw.col;
    stmt->isConst = (kw.type == TokenType::Const);
    stmt->name = expectName("after 'let'").lexeme;
    if (!localGenerics_.empty()) localGenerics_.back().erase(stmt->name); // it hides a generic local fn
    if (matchToken(TokenType::Colon)) {
        stmt->hasDeclaredType = true;
        stmt->declaredType = parseType("in the declaration");
        // `let x: auto = e` spells out "infer it" (same as leaving the type off).
        if (stmt->declaredType.isNamed() && stmt->declaredType.name == "auto" &&
            stmt->declaredType.args.empty())
            stmt->hasDeclaredType = false;
    }
    expect(TokenType::Assign, "after the variable name");
    stmt->init = parseExpression();
    if (!localConsts_.empty()) {
        long long v = 0; // (a variable that is not a constant hides one of the same name)
        bool known = stmt->isConst && foldInt(*stmt->init, v);
        localConsts_.back()[stmt->name] = {v, known ? stmt.get() : nullptr};
    }
    return stmt;
}

StmtPtr Parser::parseReturn() {
    const Token &kw = advance(); // 'return'
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->line = kw.line;
    stmt->col = kw.col;
    // A return is bare when the next token closes the block or ends the statement.
    if (!check(TokenType::RBrace) && !check(TokenType::Semicolon)) {
        stmt->value = parseExpression();
    }
    return stmt;
}

StmtPtr Parser::parseExprOrAssign() {
    ExprPtr expr = parseExpression();
    // Plain `=` or a compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`).
    bool isCompound = false;
    BinaryOp compoundOp = BinaryOp::Add;
    switch (peek().type) {
        case TokenType::PlusEq:    isCompound = true; compoundOp = BinaryOp::Add; break;
        case TokenType::MinusEq:   isCompound = true; compoundOp = BinaryOp::Sub; break;
        case TokenType::StarEq:    isCompound = true; compoundOp = BinaryOp::Mul; break;
        case TokenType::SlashEq:   isCompound = true; compoundOp = BinaryOp::Div; break;
        case TokenType::PercentEq: isCompound = true; compoundOp = BinaryOp::Mod; break;
        case TokenType::AmpEq:        isCompound = true; compoundOp = BinaryOp::BitAnd; break;
        case TokenType::PipeEq:       isCompound = true; compoundOp = BinaryOp::BitOr; break;
        case TokenType::CaretEq:      isCompound = true; compoundOp = BinaryOp::BitXor; break;
        case TokenType::ShiftLeftEq:  isCompound = true; compoundOp = BinaryOp::Shl; break;
        case TokenType::ShiftRightEq: isCompound = true; compoundOp = BinaryOp::Shr; break;
        default: break;
    }
    if (check(TokenType::Assign) || isCompound) {
        const Token &eq = advance();
        // Names, member accesses, pointer dereferences, and element indexes are
        // assignable.
        if (as<NameExpr>(expr.get()) == nullptr &&
            as<MemberExpr>(expr.get()) == nullptr &&
            as<DerefExpr>(expr.get()) == nullptr &&
            as<IndexExpr>(expr.get()) == nullptr) {
            fail(eq, std::string("the left-hand side of ") + tokenTypeName(eq.type) +
                         " is not assignable");
        }
        checkCapturedWrite(*expr, eq);
        auto stmt = std::make_unique<AssignStmt>();
        stmt->line = eq.line;
        stmt->col = eq.col;
        stmt->isCompound = isCompound;
        stmt->compoundOp = compoundOp;
        stmt->target = std::move(expr);
        stmt->value = parseExpression();
        return stmt;
    }
    auto stmt = std::make_unique<ExprStmt>();
    stmt->line = expr->line;
    stmt->col = expr->col;
    stmt->expr = std::move(expr);
    return stmt;
}

} // namespace grav
