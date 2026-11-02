#include "parser/parser.h"

#include "common/diagnostics.h"

namespace grav {

Block Parser::parseBlock() {
    expect(TokenType::LBrace, "to open the block");
    Block block;
    while (!check(TokenType::RBrace) && !atEnd()) {
        if (matchToken(TokenType::Semicolon)) continue; // stray / empty statement
        block.statements.push_back(parseStatement());
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
            return s;
        }
        case TokenType::Continue: {
            const Token &t = advance();
            auto s = std::make_unique<ContinueStmt>();
            s->line = t.line; s->col = t.col;
            return s;
        }
        case TokenType::Identifier:
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

StmtPtr Parser::parseFor() {
    const Token &kw = advance(); // 'for'
    expect(TokenType::LParen, "after 'for'");

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

StmtPtr Parser::parseSwitch() {
    const Token &kw = advance(); // 'switch' or 'match'
    auto stmt = std::make_unique<SwitchStmt>();
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
            stmt->defaultBody = parseBlock();
        } else {
            expect(TokenType::Case, "to begin a switch case");
            SwitchCase c;
            c.values.push_back(parseExpression());
            while (matchToken(TokenType::Comma)) c.values.push_back(parseExpression());
            expect(TokenType::Colon, "after the case value");
            c.body = parseBlock();
            stmt->cases.push_back(std::move(c));
        }
    }
    expect(TokenType::RBrace, "to close the switch body");
    return stmt;
}

StmtPtr Parser::parseLet() {
    const Token &kw = advance(); // 'let' or 'const'
    auto stmt = std::make_unique<LetStmt>();
    stmt->line = kw.line;
    stmt->col = kw.col;
    stmt->isConst = (kw.type == TokenType::Const);
    stmt->name = expect(TokenType::Identifier, "after 'let'").lexeme;
    if (matchToken(TokenType::Colon)) {
        stmt->hasDeclaredType = true;
        stmt->declaredType = parseType("in the declaration");
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
