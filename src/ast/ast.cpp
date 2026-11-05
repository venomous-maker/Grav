#include "ast/ast.h"

namespace grav {

const char *binaryOpSymbol(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
        case BinaryOp::Mod: return "%";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr: return "|";
        case BinaryOp::BitXor: return "^";
        case BinaryOp::Shl: return "<<";
        case BinaryOp::Shr: return ">>";
        case BinaryOp::Eq: return "==";
        case BinaryOp::NotEq: return "!=";
        case BinaryOp::Greater: return ">";
        case BinaryOp::Less: return "<";
        case BinaryOp::GreaterEq: return ">=";
        case BinaryOp::LessEq: return "<=";
        case BinaryOp::And: return "&&";
        case BinaryOp::Or: return "||";
        case BinaryOp::Pow: return "**";
    }
    return "?";
}

bool isComparison(BinaryOp op) {
    switch (op) {
        case BinaryOp::Eq:
        case BinaryOp::NotEq:
        case BinaryOp::Greater:
        case BinaryOp::Less:
        case BinaryOp::GreaterEq:
        case BinaryOp::LessEq:
            return true;
        default:
            return false;
    }
}

bool isLogical(BinaryOp op) {
    return op == BinaryOp::And || op == BinaryOp::Or;
}

bool isIntOnly(BinaryOp op) {
    switch (op) {
        case BinaryOp::Mod:
        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::Shl:
        case BinaryOp::Shr:
            return true;
        default:
            return false;
    }
}

const char *accessName(Access a) {
    switch (a) {
        case Access::Public: return "public";
        case Access::Private: return "private";
        case Access::Protected: return "protected";
    }
    return "public";
}

namespace {
struct Visitor {
    const std::function<void(Expr &)> &f;
    void block(Block &b) { for (auto &s : b.statements) stmt(s.get()); }
    void stmt(Stmt *s) {
        if (!s) return;
        if (auto *x = dynamic_cast<LetStmt *>(s)) expr(x->init.get());
        else if (auto *x = dynamic_cast<AssignStmt *>(s)) { expr(x->target.get()); expr(x->value.get()); }
        else if (auto *x = dynamic_cast<ReturnStmt *>(s)) expr(x->value.get());
        else if (auto *x = dynamic_cast<ExprStmt *>(s)) expr(x->expr.get());
        else if (auto *x = dynamic_cast<BlockStmt *>(s)) block(x->block);
        else if (auto *x = dynamic_cast<IfStmt *>(s)) { expr(x->cond.get()); block(x->thenBlock); stmt(x->elseStmt.get()); }
        else if (auto *x = dynamic_cast<WhileStmt *>(s)) { expr(x->cond.get()); block(x->body); }
        else if (auto *x = dynamic_cast<DoWhileStmt *>(s)) { block(x->body); expr(x->cond.get()); }
        else if (auto *x = dynamic_cast<ForStmt *>(s)) {
            stmt(x->init.get()); expr(x->cond.get()); stmt(x->update.get()); block(x->body);
        } else if (auto *x = dynamic_cast<ForInStmt *>(s)) {
            expr(x->lo.get()); expr(x->hi.get()); expr(x->step.get()); block(x->body);
        } else if (auto *x = dynamic_cast<SwitchStmt *>(s)) {
            expr(x->subject.get());
            for (auto &c : x->cases) {
                for (auto &v : c.values) expr(v.get());
                for (auto &h : c.rangeHi) expr(h.get());
                expr(c.guard.get());
                block(c.body);
            }
            block(x->defaultBody);
        } else if (auto *x = dynamic_cast<ThrowStmt *>(s)) expr(x->value.get());
        else if (auto *x = dynamic_cast<TryStmt *>(s)) {
            block(x->tryBlock);
            for (auto &c : x->catches) block(c.block);
            block(x->finallyBlock);
        } else if (auto *x = dynamic_cast<DeferStmt *>(s)) block(x->body);
    }
    void expr(Expr *e) {
        if (!e) return;
        f(*e);
        if (auto *x = dynamic_cast<BinaryExpr *>(e)) { expr(x->left.get()); expr(x->right.get()); }
        else if (auto *x = dynamic_cast<UnaryExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<TernaryExpr *>(e)) { expr(x->cond.get()); expr(x->thenExpr.get()); expr(x->elseExpr.get()); }
        else if (auto *x = dynamic_cast<AsExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<IsExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<AwaitExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<AddrOfExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<DerefExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<CoalesceExpr *>(e)) { expr(x->left.get()); expr(x->right.get()); }
        else if (auto *x = dynamic_cast<IncDecExpr *>(e)) expr(x->target.get());
        else if (auto *x = dynamic_cast<CastExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<SizeofExpr *>(e)) expr(x->operand.get());
        else if (auto *x = dynamic_cast<ArrayLiteralExpr *>(e)) { for (auto &el : x->elements) expr(el.get()); }
        else if (auto *x = dynamic_cast<IndexExpr *>(e)) {
            expr(x->base.get()); expr(x->index.get());
            for (auto &m : x->more) expr(m.get());
            expr(x->sliceHi.get());
        }
        else if (auto *x = dynamic_cast<NewExpr *>(e)) { for (auto &a : x->args) expr(a.get()); }
        else if (auto *x = dynamic_cast<StructLiteralExpr *>(e)) { for (auto &fi : x->fields) expr(fi.value.get()); }
        else if (auto *x = dynamic_cast<CallExpr *>(e)) { expr(x->callee.get()); for (auto &a : x->args) expr(a.get()); }
        else if (auto *x = dynamic_cast<MemberExpr *>(e)) { expr(x->object.get()); expr(x->bound.get()); }
        else if (auto *x = dynamic_cast<LambdaExpr *>(e)) { expr(x->exprBody.get()); block(x->body); }
        else if (auto *x = dynamic_cast<NamedArgExpr *>(e)) expr(x->value.get());
        else if (auto *x = dynamic_cast<DefaultArgExpr *>(e)) expr(x->value.get());
    }
};
// Like Visitor, but hands out the owning ExprPtr so expressions can be replaced.
struct Rewriter {
    const std::function<void(ExprPtr &)> &f;
    void block(Block &b) { for (auto &s : b.statements) stmt(s.get()); }
    void stmt(Stmt *s) {
        if (!s) return;
        if (auto *x = dynamic_cast<LetStmt *>(s)) expr(x->init);
        else if (auto *x = dynamic_cast<AssignStmt *>(s)) { expr(x->target); expr(x->value); }
        else if (auto *x = dynamic_cast<ReturnStmt *>(s)) expr(x->value);
        else if (auto *x = dynamic_cast<ExprStmt *>(s)) expr(x->expr);
        else if (auto *x = dynamic_cast<BlockStmt *>(s)) block(x->block);
        else if (auto *x = dynamic_cast<IfStmt *>(s)) { expr(x->cond); block(x->thenBlock); stmt(x->elseStmt.get()); }
        else if (auto *x = dynamic_cast<WhileStmt *>(s)) { expr(x->cond); block(x->body); }
        else if (auto *x = dynamic_cast<DoWhileStmt *>(s)) { block(x->body); expr(x->cond); }
        else if (auto *x = dynamic_cast<ForStmt *>(s)) {
            stmt(x->init.get()); expr(x->cond); stmt(x->update.get()); block(x->body);
        } else if (auto *x = dynamic_cast<ForInStmt *>(s)) {
            expr(x->lo); expr(x->hi); expr(x->step); block(x->body);
        } else if (auto *x = dynamic_cast<SwitchStmt *>(s)) {
            expr(x->subject);
            for (auto &c : x->cases) { expr(c.guard); block(c.body); }
            block(x->defaultBody);
        } else if (auto *x = dynamic_cast<ThrowStmt *>(s)) expr(x->value);
        else if (auto *x = dynamic_cast<TryStmt *>(s)) {
            block(x->tryBlock);
            for (auto &c : x->catches) block(c.block);
            block(x->finallyBlock);
        } else if (auto *x = dynamic_cast<DeferStmt *>(s)) block(x->body);
    }
    void expr(ExprPtr &slot) {
        if (!slot) return;
        f(slot);
        Expr *e = slot.get();
        if (auto *x = dynamic_cast<BinaryExpr *>(e)) { expr(x->left); expr(x->right); }
        else if (auto *x = dynamic_cast<UnaryExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<TernaryExpr *>(e)) { expr(x->cond); expr(x->thenExpr); expr(x->elseExpr); }
        else if (auto *x = dynamic_cast<AsExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<IsExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<AwaitExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<AddrOfExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<DerefExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<CoalesceExpr *>(e)) { expr(x->left); expr(x->right); }
        else if (auto *x = dynamic_cast<IncDecExpr *>(e)) expr(x->target);
        else if (auto *x = dynamic_cast<CastExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<SizeofExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<ArrayLiteralExpr *>(e)) { for (auto &el : x->elements) expr(el); }
        else if (auto *x = dynamic_cast<IndexExpr *>(e)) {
            expr(x->base); expr(x->index);
            for (auto &m : x->more) expr(m);
            expr(x->sliceHi);
        }
        else if (auto *x = dynamic_cast<NewExpr *>(e)) { for (auto &a : x->args) expr(a); }
        else if (auto *x = dynamic_cast<StructLiteralExpr *>(e)) { for (auto &fi : x->fields) expr(fi.value); }
        else if (auto *x = dynamic_cast<CallExpr *>(e)) { expr(x->callee); for (auto &a : x->args) expr(a); }
        else if (auto *x = dynamic_cast<MemberExpr *>(e)) { expr(x->object); expr(x->bound); }
        else if (auto *x = dynamic_cast<LambdaExpr *>(e)) { expr(x->exprBody); block(x->body); }
        else if (auto *x = dynamic_cast<NamedArgExpr *>(e)) expr(x->value);
        else if (auto *x = dynamic_cast<DefaultArgExpr *>(e)) expr(x->value);
    }
};
} // namespace

std::vector<std::string> nameCandidates(const std::string &name, const std::string &ns,
                                        const UseTable &uses) {
    std::vector<std::string> out;
    std::string level = ns;
    for (;;) {
        out.push_back(level.empty() ? name : level + "." + name);
        auto it = uses.byNamespace.find(level);
        if (it != uses.byNamespace.end()) {
            for (const auto &u : it->second) {           // explicit names and aliases first
                if (u.glob) continue;
                if (name == u.alias) out.push_back(u.target);
                else if (name.size() > u.alias.size() && name.compare(0, u.alias.size(), u.alias) == 0 &&
                         name[u.alias.size()] == '.')
                    out.push_back(u.target + name.substr(u.alias.size())); // namespace alias: col.X
            }
            for (const auto &u : it->second)
                if (u.glob) out.push_back(u.target + "." + name);
        }
        if (level.empty()) break;
        auto dot = level.rfind('.');
        level = dot == std::string::npos ? "" : level.substr(0, dot);
    }
    return out;
}

void rewriteStmtExprs(Stmt &stmt, const std::function<void(ExprPtr &)> &f) {
    Rewriter v{f};
    v.stmt(&stmt);
}

void rewriteExpr(ExprPtr &slot, const std::function<void(ExprPtr &)> &f) {
    Rewriter v{f};
    v.expr(slot);
}

void rewriteExprs(Program &program, const std::function<void(ExprPtr &)> &f) {
    Rewriter v{f};
    for (auto &d : program.decls) {
        if (auto *fn = dynamic_cast<FunctionDecl *>(d.get())) v.block(fn->body);
        else if (auto *c = dynamic_cast<ClassDecl *>(d.get())) {
            v.block(c->constructor.body);
            for (auto &x : c->extraCtors) v.block(x.body);
            v.block(c->destructor.body);
            for (auto &m : c->methods) v.block(m.body);
            for (auto &sf : c->staticFields) v.expr(sf.init);
        } else if (auto *g = dynamic_cast<GlobalVarDecl *>(d.get())) v.expr(g->init);
        else if (auto *t = dynamic_cast<TestDecl *>(d.get())) v.block(t->body);
    }
}

void visitBlockExprs(Block &block, const std::function<void(Expr &)> &f) {
    Visitor v{f};
    v.block(block);
}

void visitExprs(Program &program, const std::function<void(Expr &)> &f) {
    Visitor v{f};
    for (auto &d : program.decls) {
        if (auto *fn = dynamic_cast<FunctionDecl *>(d.get())) v.block(fn->body);
        else if (auto *c = dynamic_cast<ClassDecl *>(d.get())) {
            v.block(c->constructor.body);
            for (auto &x : c->extraCtors) v.block(x.body);
            v.block(c->destructor.body);
            for (auto &m : c->methods) v.block(m.body);
            for (auto &sf : c->staticFields) v.expr(sf.init.get());
        } else if (auto *g = dynamic_cast<GlobalVarDecl *>(d.get())) v.expr(g->init.get());
        else if (auto *t = dynamic_cast<TestDecl *>(d.get())) v.block(t->body);
    }
}

} // namespace grav
