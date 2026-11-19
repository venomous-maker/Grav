#include "sema/cells.h"

#include <deque>
#include <unordered_map>

#include "lexer/lexer.h"
#include "lexer/preprocess.h"
#include "parser/parser.h"

namespace grav {

namespace {

// The shared cell a variable assigned by a lambda lives in.
const char *kCellClass = "__GravCell";
const char *kCellSource =
    "class __GravCell<T> {\n"
    "    public value: T\n"
    "    constructor() {}\n"
    "    constructor(value: T) { self.value = value }\n"
    "}\n";

// One local variable, from its declaration to the end of its scope.
struct Var {
    int depth = 0;                 // lambda nesting where it is declared
    bool assignedInside = false;   // a lambda nested deeper assigns it
    bool called = false;           // used as `name(...)`: left alone
    std::vector<ExprPtr *> uses;   // every NameExpr naming it
    LetStmt *let = nullptr;        // declared by `let`, or else:
    std::string *nameRef = nullptr; // a parameter, loop or catch variable's name,
    Block *body = nullptr;         //   and the block where the cell is made
    const TypeRef *type = nullptr; //   with its written type, if any
    int line = 0, col = 0;
};

class Boxer {
public:
    std::deque<Var> vars;

    void function(std::vector<Param> &params, Block &body) {
        depth_ = 0;
        scopes_.clear();
        scopes_.emplace_back();
        for (auto &p : params) param(p, &body);
        block(body);
        scopes_.clear();
    }
    void expression(ExprPtr &e) {
        depth_ = 0;
        scopes_.assign(1, {});
        expr(e);
        scopes_.clear();
    }

private:
    std::vector<std::unordered_map<std::string, Var *>> scopes_;
    int depth_ = 0;

    Var *declare(const std::string &name) {
        vars.emplace_back();
        Var *v = &vars.back();
        v->depth = depth_;
        scopes_.back()[name] = v;
        return v;
    }
    Var *lookup(const std::string &name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        return nullptr;
    }
    void param(Param &p, Block *body, int line = 0, int col = 0) {
        Var *v = declare(p.name);
        v->line = line;
        v->col = col;
        v->nameRef = &p.name;
        v->body = body;
        if (p.type.kind != TypeRef::Kind::Error) v->type = &p.type;
        if (p.variadic) v->called = true; // (a slice: not a value a cell can hold)
    }
    // `target` is assigned: from inside a lambda, its variable needs a cell.
    void assigned(Expr *target) {
        if (auto *n = dynamic_cast<NameExpr *>(target))
            if (Var *v = lookup(n->name); v && v->depth < depth_) v->assignedInside = true;
    }

    void block(Block &b) {
        scopes_.emplace_back();
        for (auto &s : b.statements) stmt(s.get());
        scopes_.pop_back();
    }
    void stmt(Stmt *s) {
        if (!s) return;
        if (auto *x = dynamic_cast<LetStmt *>(s)) {
            expr(x->init);
            Var *v = declare(x->name);
            v->let = x;
            v->line = x->line;
            v->col = x->col;
        } else if (auto *x = dynamic_cast<AssignStmt *>(s)) {
            assigned(x->target.get());
            expr(x->target);
            expr(x->value);
        } else if (auto *x = dynamic_cast<ReturnStmt *>(s)) expr(x->value);
        else if (auto *x = dynamic_cast<ExprStmt *>(s)) expr(x->expr);
        else if (auto *x = dynamic_cast<BlockStmt *>(s)) block(x->block);
        else if (auto *x = dynamic_cast<IfStmt *>(s)) { expr(x->cond); block(x->thenBlock); stmt(x->elseStmt.get()); }
        else if (auto *x = dynamic_cast<WhileStmt *>(s)) { expr(x->cond); block(x->body); }
        else if (auto *x = dynamic_cast<DoWhileStmt *>(s)) { block(x->body); expr(x->cond); }
        else if (auto *x = dynamic_cast<ForStmt *>(s)) {
            scopes_.emplace_back();
            stmt(x->init.get());
            expr(x->cond);
            stmt(x->update.get());
            block(x->body);
            scopes_.pop_back();
        } else if (auto *x = dynamic_cast<ForInStmt *>(s)) {
            expr(x->lo);
            expr(x->hi);
            expr(x->step);
            scopes_.emplace_back();
            for (std::string *n : {&x->indexVar, &x->var}) {
                if (n->empty()) continue;
                Var *v = declare(*n);
                v->nameRef = n;
                v->body = &x->body;
                v->line = x->line;
                v->col = x->col;
            }
            block(x->body);
            scopes_.pop_back();
        } else if (auto *x = dynamic_cast<SwitchStmt *>(s)) {
            expr(x->subject);
            for (auto &c : x->cases) {
                for (auto &v : c.values) expr(v);
                for (auto &h : c.rangeHi) expr(h);
                expr(c.guard);
                block(c.body);
            }
            block(x->defaultBody);
        } else if (auto *x = dynamic_cast<ThrowStmt *>(s)) expr(x->value);
        else if (auto *x = dynamic_cast<TryStmt *>(s)) {
            block(x->tryBlock);
            for (auto &c : x->catches) {
                scopes_.emplace_back();
                Var *v = declare(c.var);
                v->nameRef = &c.var;
                v->body = &c.block;
                v->type = &c.type;
                v->line = c.line;
                v->col = c.col;
                block(c.block);
                scopes_.pop_back();
            }
            block(x->finallyBlock);
        } else if (auto *x = dynamic_cast<DeferStmt *>(s)) block(x->body);
    }
    void expr(ExprPtr &slot) {
        if (!slot) return;
        Expr *e = slot.get();
        if (auto *x = dynamic_cast<NameExpr *>(e)) {
            if (x->typeArgs.empty())
                if (Var *v = lookup(x->name)) v->uses.push_back(&slot);
        } else if (auto *x = dynamic_cast<BinaryExpr *>(e)) { expr(x->left); expr(x->right); }
        else if (auto *x = dynamic_cast<UnaryExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<TernaryExpr *>(e)) { expr(x->cond); expr(x->thenExpr); expr(x->elseExpr); }
        else if (auto *x = dynamic_cast<AsExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<IsExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<AwaitExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<AddrOfExpr *>(e)) {
            // `&x` takes the variable's own address: it must stay where it is.
            if (auto *n = dynamic_cast<NameExpr *>(x->operand.get()))
                if (Var *v = lookup(n->name)) v->called = true;
            expr(x->operand);
        } else if (auto *x = dynamic_cast<DerefExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<CoalesceExpr *>(e)) { expr(x->left); expr(x->right); }
        else if (auto *x = dynamic_cast<IncDecExpr *>(e)) { assigned(x->target.get()); expr(x->target); }
        else if (auto *x = dynamic_cast<CastExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<SizeofExpr *>(e)) expr(x->operand);
        else if (auto *x = dynamic_cast<ArrayLiteralExpr *>(e)) { for (auto &el : x->elements) expr(el); expr(x->built); }
        else if (auto *x = dynamic_cast<IndexExpr *>(e)) {
            expr(x->base);
            expr(x->index);
            for (auto &m : x->more) expr(m);
            expr(x->sliceHi);
        } else if (auto *x = dynamic_cast<NewExpr *>(e)) { for (auto &a : x->args) expr(a); }
        else if (auto *x = dynamic_cast<StructLiteralExpr *>(e)) { for (auto &fi : x->fields) expr(fi.value); }
        else if (auto *x = dynamic_cast<CallExpr *>(e)) {
            // `f(...)` with a local `f`: calls stay calls of the variable itself.
            if (auto *n = dynamic_cast<NameExpr *>(x->callee.get())) {
                if (Var *v = lookup(n->name)) v->called = true;
            } else {
                expr(x->callee);
            }
            for (auto &a : x->args) expr(a);
        } else if (auto *x = dynamic_cast<MemberExpr *>(e)) { expr(x->object); expr(x->bound); }
        else if (auto *x = dynamic_cast<LambdaExpr *>(e)) {
            ++depth_;
            scopes_.emplace_back();
            for (size_t i = 0; i < x->params.size(); ++i) {
                Param &p = x->params[i];
                // An arrow body has no block to make a cell in. (Each cell is made at
                // its own position: inference tells cells apart by where they are.)
                param(p, x->exprBody ? nullptr : &x->body, x->line, x->col + static_cast<int>(i));
                if (x->exprBody) scopes_.back()[p.name]->called = true;
            }
            if (!x->selfName.empty()) declare(x->selfName)->called = true; // the lambda itself
            expr(x->exprBody);
            block(x->body);
            scopes_.pop_back();
            --depth_;
        } else if (auto *x = dynamic_cast<NamedArgExpr *>(e)) expr(x->value);
        else if (auto *x = dynamic_cast<DefaultArgExpr *>(e)) expr(x->value);
    }
};

ExprPtr nameExpr(const std::string &name, int line, int col) {
    auto n = std::make_unique<NameExpr>();
    n->name = name;
    n->line = line;
    n->col = col;
    return n;
}

// `new __GravCell<T>(init)` (`<T>` when the type is written; no argument without an init).
ExprPtr newCell(ExprPtr init, const TypeRef *type, int line, int col) {
    auto n = std::make_unique<NewExpr>();
    n->className = kCellClass;
    n->line = line;
    n->col = col;
    if (type) n->typeArgs.push_back(*type);
    if (init) n->args.push_back(std::move(init));
    return n;
}

} // namespace

void boxAssignedCaptures(Program &program) {
    Boxer b;
    for (auto &d : program.decls) {
        if (auto *fn = dynamic_cast<FunctionDecl *>(d.get())) b.function(fn->params, fn->body);
        else if (auto *c = dynamic_cast<ClassDecl *>(d.get())) {
            b.function(c->constructor.params, c->constructor.body);
            for (auto &x : c->extraCtors) b.function(x.params, x.body);
            std::vector<Param> none;
            b.function(none, c->destructor.body);
            for (auto &m : c->methods) b.function(m.params, m.body);
            for (auto &sf : c->staticFields) b.expression(sf.init);
        } else if (auto *g = dynamic_cast<GlobalVarDecl *>(d.get())) b.expression(g->init);
        else if (auto *t = dynamic_cast<TestDecl *>(d.get())) {
            std::vector<Param> none;
            b.function(none, t->body);
        }
    }

    bool any = false;
    for (Var &v : b.vars) {
        if (!v.assignedInside || v.called) continue;
        if (v.let) {
            // `let x: T` with neither a type nor a value cannot say what the cell holds.
            if (v.let->isConst || (!v.let->init && !v.let->hasDeclaredType)) continue;
        } else if (!v.body) {
            continue;
        }
        any = true;
        // Every use reads and writes the cell's value...
        for (ExprPtr *slot : v.uses) {
            auto m = std::make_unique<MemberExpr>();
            m->line = (*slot)->line;
            m->col = (*slot)->col;
            m->member = "value";
            m->object = std::move(*slot);
            *slot = std::move(m);
        }
        // ...and the declaration makes the cell.
        if (v.let) {
            const TypeRef *t = v.let->hasDeclaredType ? &v.let->declaredType : nullptr;
            v.let->init = newCell(std::move(v.let->init), t, v.line, v.col);
            v.let->hasDeclaredType = false;
        } else {
            std::string name = *v.nameRef;
            *v.nameRef = name + "__cell";
            auto let = std::make_unique<LetStmt>();
            let->line = v.line;
            let->col = v.col;
            let->name = name;
            let->quietUnused = true;
            let->init = newCell(nameExpr(*v.nameRef, v.line, v.col), v.type, v.line, v.col);
            v.body->statements.insert(v.body->statements.begin(), std::move(let));
        }
    }
    if (!any) return;
    Lexer lexer(kCellSource);
    Parser parser(preprocess(lexer.tokenize()));
    Program cell = parser.parseProgram();
    for (auto it = cell.decls.rbegin(); it != cell.decls.rend(); ++it)
        program.decls.insert(program.decls.begin(), std::move(*it));
}

} // namespace grav
