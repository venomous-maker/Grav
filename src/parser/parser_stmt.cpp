#include "parser/parser.h"

#include "common/diagnostics.h"

namespace grav {

Block Parser::parseBlock() {
    expect(TokenType::LBrace, "to open the block");
    Block block;
    while (!check(TokenType::RBrace) && !atEnd()) {
        block.statements.push_back(parseStatement());
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
    auto stmt = std::make_unique<ForStmt>();
    stmt->line = kw.line; stmt->col = kw.col;
    expect(TokenType::LParen, "after 'for'");
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
    // A return is bare when the next token closes the block.
    if (!check(TokenType::RBrace)) {
        stmt->value = parseExpression();
    }
    return stmt;
}

StmtPtr Parser::parseExprOrAssign() {
    ExprPtr expr = parseExpression();
    if (check(TokenType::Assign)) {
        const Token &eq = advance();
        // Only names and member accesses are valid assignment targets.
        if (dynamic_cast<NameExpr *>(expr.get()) == nullptr &&
            dynamic_cast<MemberExpr *>(expr.get()) == nullptr) {
            fail(eq, "the left-hand side of '=' is not assignable");
        }
        auto stmt = std::make_unique<AssignStmt>();
        stmt->line = eq.line;
        stmt->col = eq.col;
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
