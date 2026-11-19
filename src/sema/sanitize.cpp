#include "sema/sanitize.h"

#include <unordered_set>

namespace grav {

std::string safeLocalName(const std::string &name) {
    static const std::unordered_set<std::string> reserved = {
        // C keywords (C11) and stdbool macros
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
        "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register",
        "restrict", "return", "short", "signed", "sizeof", "static", "struct", "switch",
        "typedef", "union", "unsigned", "void", "volatile", "while", "_Bool", "_Complex",
        "_Imaginary", "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
        "_Static_assert", "_Thread_local", "bool", "true", "false", "NULL",
        // C library functions/macros the generated code and runtime call by name
        "printf", "puts", "fputs", "fprintf", "snprintf", "vsnprintf", "fflush", "stdout",
        "stderr", "malloc", "calloc", "realloc", "free", "memcpy", "memcmp", "memset", "strlen",
        "strcmp", "strcpy", "pow", "exit", "abort", "setjmp", "longjmp", "jmp_buf", "va_list",
        "va_start", "va_end", "va_arg", "va_copy", "getchar", "getenv", "getcwd", "main",
        "errno", "assert", "offsetof", "size_t",
    };
    return reserved.count(name) ? name + "_" : name;
}

namespace {

void fix(std::string &n) { n = safeLocalName(n); }
void walkBlock(Block &b);
void walkExpr(Expr *e);

void walkStmt(Stmt *s) {
    if (!s) return;
    if (auto *x = as<LetStmt>(s)) { fix(x->name); walkExpr(x->init.get()); }
    else if (auto *x = as<AssignStmt>(s)) { walkExpr(x->target.get()); walkExpr(x->value.get()); }
    else if (auto *x = as<ReturnStmt>(s)) walkExpr(x->value.get());
    else if (auto *x = as<ExprStmt>(s)) walkExpr(x->expr.get());
    else if (auto *x = as<BlockStmt>(s)) walkBlock(x->block);
    else if (auto *x = as<IfStmt>(s)) {
        walkExpr(x->cond.get()); walkBlock(x->thenBlock); walkStmt(x->elseStmt.get());
    } else if (auto *x = as<WhileStmt>(s)) { walkExpr(x->cond.get()); walkBlock(x->body); }
    else if (auto *x = as<DoWhileStmt>(s)) { walkBlock(x->body); walkExpr(x->cond.get()); }
    else if (auto *x = as<ForStmt>(s)) {
        walkStmt(x->init.get()); walkExpr(x->cond.get()); walkStmt(x->update.get()); walkBlock(x->body);
    } else if (auto *x = as<ForInStmt>(s)) {
        fix(x->var);
        if (!x->indexVar.empty()) fix(x->indexVar);
        walkExpr(x->lo.get()); walkExpr(x->hi.get()); walkExpr(x->step.get()); walkBlock(x->body);
    } else if (auto *x = as<SwitchStmt>(s)) {
        walkExpr(x->subject.get());
        for (auto &c : x->cases) {
            for (auto &v : c.values) walkExpr(v.get());
            for (auto &h : c.rangeHi) walkExpr(h.get());
            walkExpr(c.guard.get());
            walkBlock(c.body);
        }
        walkBlock(x->defaultBody);
    } else if (auto *x = as<ThrowStmt>(s)) walkExpr(x->value.get());
    else if (auto *x = as<TryStmt>(s)) {
        walkBlock(x->tryBlock);
        for (auto &c : x->catches) { fix(c.var); walkBlock(c.block); }
        walkBlock(x->finallyBlock);
    } else if (auto *x = as<DeferStmt>(s)) walkBlock(x->body);
}

void walkBlock(Block &b) { for (auto &s : b.statements) walkStmt(s.get()); }

void walkExpr(Expr *e) {
    if (!e) return;
    if (auto *x = as<NameExpr>(e)) {
        // Globals and function references are emitted under their own C names.
        if (x->resolvedGlobal.empty() && x->fnRef.empty()) fix(x->name);
    } else if (auto *x = as<BinaryExpr>(e)) { walkExpr(x->left.get()); walkExpr(x->right.get()); }
    else if (auto *x = as<UnaryExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<TernaryExpr>(e)) {
        walkExpr(x->cond.get()); walkExpr(x->thenExpr.get()); walkExpr(x->elseExpr.get());
    } else if (auto *x = as<AsExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<IsExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<AwaitExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<AddrOfExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<DerefExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<CoalesceExpr>(e)) { walkExpr(x->left.get()); walkExpr(x->right.get()); }
    else if (auto *x = as<IncDecExpr>(e)) walkExpr(x->target.get());
    else if (auto *x = as<CastExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<SizeofExpr>(e)) walkExpr(x->operand.get());
    else if (auto *x = as<ArrayLiteralExpr>(e)) { for (auto &el : x->elements) walkExpr(el.get()); walkExpr(x->built.get()); }
    else if (auto *x = as<IndexExpr>(e)) {
        walkExpr(x->base.get()); walkExpr(x->index.get());
        for (auto &m : x->more) walkExpr(m.get());
    }
    else if (auto *x = as<NewExpr>(e)) { for (auto &a : x->args) walkExpr(a.get()); }
    else if (auto *x = as<StructLiteralExpr>(e)) { for (auto &f : x->fields) walkExpr(f.value.get()); }
    else if (auto *x = as<CallExpr>(e)) {
        // A bare-name callee of a free function/builtin is not a local.
        if (x->kind == CallKind::FnValue || !as<NameExpr>(x->callee.get()))
            walkExpr(x->callee.get());
        for (auto &a : x->args) walkExpr(a.get());
    } else if (auto *x = as<MemberExpr>(e)) { walkExpr(x->object.get()); walkExpr(x->bound.get()); }
    else if (auto *x = as<LambdaExpr>(e)) {
        for (auto &p : x->params) fix(p.name);
        for (auto &c : x->captures) fix(c.name);
        walkExpr(x->exprBody.get());
        walkBlock(x->body);
    }
}

void fixParams(std::vector<Param> &ps) { for (auto &p : ps) fix(p.name); }

} // namespace

void sanitizeIdentifiers(Program &program) {
    for (auto &d : program.decls) {
        if (auto *fn = as<FunctionDecl>(d.get())) {
            fixParams(fn->params);
            walkBlock(fn->body);
        } else if (auto *c = as<ClassDecl>(d.get())) {
            fixParams(c->constructor.params);
            walkBlock(c->constructor.body);
            for (auto &x : c->extraCtors) { fixParams(x.params); walkBlock(x.body); }
            walkBlock(c->destructor.body);
            for (auto &m : c->methods) { fixParams(m.params); walkBlock(m.body); }
            for (auto &sf : c->staticFields) walkExpr(sf.init.get());
        } else if (auto *g = as<GlobalVarDecl>(d.get())) {
            walkExpr(g->init.get());
        } else if (auto *t = as<TestDecl>(d.get())) {
            walkBlock(t->body);
        }
    }
}

} // namespace grav
