#include "parser/parser.h"

#include "common/diagnostics.h"

namespace grav {

Block Parser::parseBlock() {
    expect(TokenType::LBrace, "to open the block");
    Block block;
    while (!check(TokenType::RBrace) && !atEnd()) {
        if (matchToken(TokenType::Semicolon)) continue; // stray / empty statement
        block.statements.push_back(parseStatement());
        for (auto &extra : pendingStmts_) block.statements.push_back(std::move(extra)); // destructuring
        pendingStmts_.clear();
        // Statements may be terminated by ';' (recommended). Consume any here;
        // whitespace/newlines also separate statements when ';' is omitted.
        while (matchToken(TokenType::Semicolon)) {}
    }
    expect(TokenType::RBrace, "to close the block");
    return block;
}

StmtPtr Parser::parseStatement() {
    switch (peek().type) {
        case TokenType::Let:
        case TokenType::Const: return parseLet();
        case TokenType::Return: return parseReturn();
        case TokenType::If: return parseIf();
        case TokenType::While: return parseWhile();
        case TokenType::Do: return parseDoWhile();
        case TokenType::For: return parseFor();
        case TokenType::Switch:
        case TokenType::Match: return parseSwitch();
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
                if (auto *w = dynamic_cast<WhileStmt *>(loop.get())) w->label = label;
                else if (auto *d = dynamic_cast<DoWhileStmt *>(loop.get())) d->label = label;
                else if (auto *f = dynamic_cast<ForStmt *>(loop.get())) f->label = label;
                else if (auto *fi = dynamic_cast<ForInStmt *>(loop.get())) fi->label = label;
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
        c.var = expect(TokenType::Identifier, "as the caught variable").lexeme;
        expect(TokenType::Colon, "after the caught variable");
        c.type = parseType("for the caught exception");
        expect(TokenType::RParen, "after the catch type");
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
        stmt->body = parseBlock();
        auto &body = stmt->body.statements;
        body.insert(body.begin(), std::make_move_iterator(binds.begin()), std::make_move_iterator(binds.end()));
        body.insert(body.begin(), std::move(hidden));
        return stmt;
    }

    // `for (i in lo..hi [step s])`, `for (x in xs)`, `for (i, x in xs)`.
    bool pairForm = check(TokenType::Identifier) && peekAt(1).type == TokenType::Comma &&
                    peekAt(2).type == TokenType::Identifier && peekAt(3).type == TokenType::In;
    if (pairForm || (check(TokenType::Identifier) && peekAt(1).type == TokenType::In)) {
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
        stmt->body = parseBlock();
        return stmt;
    }

    auto stmt = std::make_unique<ForStmt>();
    stmt->line = kw.line; stmt->col = kw.col;
    if (!check(TokenType::Semicolon)) stmt->init = parseSimpleStmt();
    expect(TokenType::Semicolon, "after the for-initializer");
    if (!check(TokenType::Semicolon)) stmt->cond = parseExpression();
    expect(TokenType::Semicolon, "after the for-condition");
    if (!check(TokenType::RParen)) stmt->update = parseSimpleStmt();
    expect(TokenType::RParen, "to close the for-header");
    stmt->body = parseBlock();
    return stmt;
}

// A case body: a `{ block }`, or a single statement (`case Some(x): return x`).
Block Parser::parseCaseBody() {
    if (check(TokenType::LBrace)) return parseBlock();
    Block b;
    b.statements.push_back(parseStatement());
    for (auto &extra : pendingStmts_) b.statements.push_back(std::move(extra));
    pendingStmts_.clear();
    matchToken(TokenType::Semicolon);
    return b;
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
    expect(TokenType::RParen, "after the switch subject");
    expect(TokenType::LBrace, "to open the switch body");
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

// `let (a, b) = pair` (tuple fields first/second/third), `let [x, y] = arr`
// (indexes) and `let {x, y} = point` (fields by name): the value goes into a
// hidden local, and each name is a `let` reading its part (`_` skips one).
StmtPtr Parser::parseDestructure(const Token &kw) {
    TokenType open = peek().type;
    TokenType close = open == TokenType::LParen ? TokenType::RParen
                    : open == TokenType::LBracket ? TokenType::RBracket : TokenType::RBrace;
    advance();
    std::vector<const Token *> names;
    do { names.push_back(&expect(TokenType::Identifier, "as a name to bind")); } while (matchToken(TokenType::Comma));
    expect(close, "to close the names");
    if (open == TokenType::LParen && (names.size() < 2 || names.size() > 3))
        fail(kw, "a tuple has 2 or 3 elements to bind");
    expect(TokenType::Assign, "after the names");
    auto hidden = std::make_unique<LetStmt>();
    hidden->line = kw.line; hidden->col = kw.col;
    hidden->name = "destructured__" + std::to_string(destructureCounter_++);
    hidden->isConst = true;
    hidden->init = parseExpression();
    static const char *tupleFields[] = {"first", "second", "third"};
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

StmtPtr Parser::parseLet() {
    const Token &kw = advance(); // 'let' or 'const'
    if (check(TokenType::LParen) || check(TokenType::LBracket) || check(TokenType::LBrace))
        return parseDestructure(kw);
    auto stmt = std::make_unique<LetStmt>();
    stmt->line = kw.line;
    stmt->col = kw.col;
    stmt->isConst = (kw.type == TokenType::Const);
    stmt->name = expect(TokenType::Identifier, "after 'let'").lexeme;
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
        if (dynamic_cast<NameExpr *>(expr.get()) == nullptr &&
            dynamic_cast<MemberExpr *>(expr.get()) == nullptr &&
            dynamic_cast<DerefExpr *>(expr.get()) == nullptr &&
            dynamic_cast<IndexExpr *>(expr.get()) == nullptr) {
            fail(eq, std::string("the left-hand side of ") + tokenTypeName(eq.type) +
                         " is not assignable");
        }
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
