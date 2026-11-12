#include "sema/symbols.h"
#include "sema/monomorph.h"

#include <functional>
#include <unordered_set>

namespace grav {

namespace {
// Does the constructor body call `super(...)` anywhere?
bool callsSuper(Block &body) {
    bool found = false;
    visitBlockExprs(body, [&](Expr &e) {
        if (auto *c = dynamic_cast<CallExpr *>(&e))
            if (dynamic_cast<SuperExpr *>(c->callee.get())) found = true;
    });
    return found;
}

// `super(<args>)` as a statement, positioned at line:col.
StmtPtr superCall(std::vector<ExprPtr> args, int line, int col) {
    auto call = std::make_unique<CallExpr>();
    call->callee = std::make_unique<SuperExpr>();
    call->callee->line = line;
    call->callee->col = col;
    call->line = line;
    call->col = col;
    call->args = std::move(args);
    auto st = std::make_unique<ExprStmt>();
    st->expr = std::move(call);
    st->line = line;
    st->col = col;
    return st;
}

bool hasFieldInits(const ClassDecl &cd) {
    for (const auto &f : cd.fields) if (f.init) return true;
    return false;
}

// `self.<field> = <a copy of its initializer>`.
StmtPtr fieldInit(const FieldDecl &f) {
    auto target = std::make_unique<MemberExpr>();
    target->object = std::make_unique<SelfExpr>();
    target->object->line = f.line;
    target->object->col = f.col;
    target->member = f.name;
    target->line = f.line;
    target->col = f.col;
    auto st = std::make_unique<AssignStmt>();
    st->target = std::move(target);
    st->value = cloneExpression(f.init.get());
    st->line = f.line;
    st->col = f.col;
    if (f.ns.empty()) return st;
    auto in = std::make_unique<BlockStmt>();          // (a trait's field: its names are the trait's)
    in->ns = f.ns;
    in->line = f.line;
    in->col = f.col;
    in->block.statements.push_back(std::move(st));
    return in;
}

// Whether `s` is a `super(...)` call statement.
bool isSuperCall(const Stmt *s) {
    auto *es = dynamic_cast<const ExprStmt *>(s);
    auto *c = es ? dynamic_cast<const CallExpr *>(es->expr.get()) : nullptr;
    return c && dynamic_cast<const SuperExpr *>(c->callee.get());
}
} // namespace

// Constructors and inheritance (runs once base names are canonical):
//  - a class without a constructor of its own inherits its parent's: each one
//    becomes `constructor(<same params>) { super(<params>) }`, so `new D(x)`
//    runs the parent's constructor with x;
//  - a constructor that never calls `super(...)` first runs the parent's
//    constructor without parameters. A parent without one is an error.
void Registry::inheritConstructors() {
    std::unordered_set<std::string> done;
    // A class with field initializers but no constructor at all (and none to
    // inherit) gets an empty one, for its initializers to run in.
    auto giveConstructor = [&](ClassInfo &ci) {
        if (!ci.decl || !ci.ctors.empty() || !hasFieldInits(*ci.decl)) return;
        ClassDecl *cd = ci.decl;
        cd->constructor = ConstructorDecl();
        cd->constructor.present = true;
        cd->constructor.line = cd->line;
        cd->constructor.col = cd->col;
        ci.constructor = &cd->constructor;
        ci.hasConstructor = true;
        ci.ctors.push_back(&cd->constructor);
    };
    std::function<void(ClassInfo &)> visit = [&](ClassInfo &ci) {
        if (!done.insert(ci.fqName).second) return;
        auto bit = ci.baseClass.empty() ? classes_.end() : classes_.find(ci.baseClass);
        if (bit == classes_.end() || !ci.decl) { giveConstructor(ci); return; }
        ClassInfo &base = bit->second;
        visit(base); // the parent's inherited constructors exist before ours copy them
        if (base.ctors.empty()) { giveConstructor(ci); return; }
        ClassDecl *cd = ci.decl;

        if (ci.ctors.empty()) {
            for (const ConstructorDecl *bc : base.ctors) {
                ConstructorDecl nc;
                nc.present = true;
                nc.line = cd->line;
                nc.col = cd->col;
                nc.params = bc->params;
                std::vector<ExprPtr> args;
                for (const auto &p : nc.params) {
                    auto n = std::make_unique<NameExpr>();
                    n->name = p.name;
                    n->line = cd->line;
                    n->col = cd->col;
                    args.push_back(std::move(n));
                }
                // A variadic parameter arrives as a slice, which cannot be passed
                // on as the parent's trailing arguments.
                if (!nc.params.empty() && nc.params.back().variadic) continue;
                nc.body.statements.push_back(superCall(std::move(args), cd->line, cd->col));
                if (!cd->constructor.present) cd->constructor = std::move(nc);
                else cd->extraCtors.push_back(std::move(nc));
            }
            if (cd->constructor.present) {
                ci.constructor = &cd->constructor;
                ci.hasConstructor = true;
                ci.ctors.push_back(&cd->constructor);
                for (auto &x : cd->extraCtors) ci.ctors.push_back(&x);
            }
            return;
        }

        bool baseHasDefault = false;
        for (const ConstructorDecl *bc : base.ctors)
            if (bc->params.empty()) baseHasDefault = true;
        for (ConstructorDecl *c : ci.ctors) {
            if (callsSuper(c->body)) continue;
            if (!baseHasDefault) {
                error(c->line, c->col, "constructor of '" + ci.fqName + "' must call super(...): '" +
                                           base.fqName + "' has no constructor without parameters");
                continue;
            }
            auto &stmts = c->body.statements;
            stmts.insert(stmts.begin(), superCall({}, c->line, c->col));
        }
    };
    for (auto &[fq, ci] : classes_) visit(ci);
    // Field initializers run in every constructor: after the parent's (a leading
    // `super(...)`), before the body, in declaration order.
    for (auto &[fq, ci] : classes_) {
        if (!ci.decl || !hasFieldInits(*ci.decl)) continue;
        for (ConstructorDecl *c : ci.ctors) {
            auto &stmts = c->body.statements;
            size_t at = !stmts.empty() && isSuperCall(stmts[0].get()) ? 1 : 0;
            std::vector<StmtPtr> inits;
            for (const auto &f : ci.decl->fields) if (f.init) inits.push_back(fieldInit(f));
            stmts.insert(stmts.begin() + static_cast<long>(at), std::make_move_iterator(inits.begin()),
                         std::make_move_iterator(inits.end()));
        }
    }
}

} // namespace grav
