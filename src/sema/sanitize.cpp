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
    if (auto *x = dynamic_cast<LetStmt *>(s)) { fix(x->name); walkExpr(x->init.get()); }
    else if (auto *x = dynamic_cast<AssignStmt *>(s)) { walkExpr(x->target.get()); walkExpr(x->value.get()); }
    else if (auto *x = dynamic_cast<ReturnStmt *>(s)) walkExpr(x->value.get());
    else if (auto *x = dynamic_cast<ExprStmt *>(s)) walkExpr(x->expr.get());
    else if (auto *x = dynamic_cast<BlockStmt *>(s)) walkBlock(x->block);
    else if (auto *x = dynamic_cast<IfStmt *>(s)) {
        walkExpr(x->cond.get()); walkBlock(x->thenBlock); walkStmt(x->elseStmt.get());
    } else if (auto *x = dynamic_cast<WhileStmt *>(s)) { walkExpr(x->cond.get()); walkBlock(x->body); }
    else if (auto *x = dynamic_cast<DoWhileStmt *>(s)) { walkBlock(x->body); walkExpr(x->cond.get()); }
    else if (auto *x = dynamic_cast<ForStmt *>(s)) {
        walkStmt(x->init.get()); walkExpr(x->cond.get()); walkStmt(x->update.get()); walkBlock(x->body);
    } else if (auto *x = dynamic_cast<ForInStmt *>(s)) {
        fix(x->var);
        if (!x->indexVar.empty()) fix(x->indexVar);
        walkExpr(x->lo.get()); walkExpr(x->hi.get()); walkExpr(x->step.get()); walkBlock(x->body);
    } else if (auto *x = dynamic_cast<SwitchStmt *>(s)) {
        walkExpr(x->subject.get());
        for (auto &c : x->cases) { for (auto &v : c.values) walkExpr(v.get()); walkBlock(c.body); }
        walkBlock(x->defaultBody);
    } else if (auto *x = dynamic_cast<ThrowStmt *>(s)) walkExpr(x->value.get());
    else if (auto *x = dynamic_cast<TryStmt *>(s)) {
        walkBlock(x->tryBlock);
        for (auto &c : x->catches) { fix(c.var); walkBlock(c.block); }
        walkBlock(x->finallyBlock);
    } else if (auto *x = dynamic_cast<DeferStmt *>(s)) walkBlock(x->body);
}

void walkBlock(Block &b) { for (auto &s : b.statements) walkStmt(s.get()); }

void walkExpr(Expr *e) {
    if (!e) return;
    if (auto *x = dynamic_cast<NameExpr *>(e)) {
        // Globals and function references are emitted under their own C names.
        if (x->resolvedGlobal.empty() && x->fnRef.empty()) fix(x->name);
    } else if (auto *x = dynamic_cast<BinaryExpr *>(e)) { walkExpr(x->left.get()); walkExpr(x->right.get()); }
    else if (auto *x = dynamic_cast<UnaryExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<TernaryExpr *>(e)) {
        walkExpr(x->cond.get()); walkExpr(x->thenExpr.get()); walkExpr(x->elseExpr.get());
    } else if (auto *x = dynamic_cast<AsExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<IsExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<AwaitExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<AddrOfExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<DerefExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<CoalesceExpr *>(e)) { walkExpr(x->left.get()); walkExpr(x->right.get()); }
    else if (auto *x = dynamic_cast<IncDecExpr *>(e)) walkExpr(x->target.get());
    else if (auto *x = dynamic_cast<CastExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<SizeofExpr *>(e)) walkExpr(x->operand.get());
    else if (auto *x = dynamic_cast<ArrayLiteralExpr *>(e)) { for (auto &el : x->elements) walkExpr(el.get()); }
    else if (auto *x = dynamic_cast<IndexExpr *>(e)) { walkExpr(x->base.get()); walkExpr(x->index.get()); }
    else if (auto *x = dynamic_cast<NewExpr *>(e)) { for (auto &a : x->args) walkExpr(a.get()); }
    else if (auto *x = dynamic_cast<StructLiteralExpr *>(e)) { for (auto &f : x->fields) walkExpr(f.value.get()); }
    else if (auto *x = dynamic_cast<CallExpr *>(e)) {
        // A bare-name callee of a free function/builtin is not a local.
        if (x->kind == CallKind::FnValue || !dynamic_cast<NameExpr *>(x->callee.get()))
            walkExpr(x->callee.get());
        for (auto &a : x->args) walkExpr(a.get());
    } else if (auto *x = dynamic_cast<MemberExpr *>(e)) walkExpr(x->object.get());
    else if (auto *x = dynamic_cast<LambdaExpr *>(e)) {
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
        if (auto *fn = dynamic_cast<FunctionDecl *>(d.get())) {
            fixParams(fn->params);
            walkBlock(fn->body);
        } else if (auto *c = dynamic_cast<ClassDecl *>(d.get())) {
            fixParams(c->constructor.params);
            walkBlock(c->constructor.body);
            for (auto &x : c->extraCtors) { fixParams(x.params); walkBlock(x.body); }
            walkBlock(c->destructor.body);
            for (auto &m : c->methods) { fixParams(m.params); walkBlock(m.body); }
            for (auto &sf : c->staticFields) walkExpr(sf.init.get());
        } else if (auto *g = dynamic_cast<GlobalVarDecl *>(d.get())) {
            walkExpr(g->init.get());
        } else if (auto *t = dynamic_cast<TestDecl *>(d.get())) {
            walkBlock(t->body);
        }
    }
}

} // namespace grav
