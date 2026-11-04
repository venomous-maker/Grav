#include "sema/symbols.h"

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
} // namespace

// Constructors and inheritance (runs once base names are canonical):
//  - a class without a constructor of its own inherits its parent's: each one
//    becomes `constructor(<same params>) { super(<params>) }`, so `new D(x)`
//    runs the parent's constructor with x;
//  - a constructor that never calls `super(...)` first runs the parent's
//    constructor without parameters. A parent without one is an error.
void Registry::inheritConstructors() {
    std::unordered_set<std::string> done;
    std::function<void(ClassInfo &)> visit = [&](ClassInfo &ci) {
        if (!done.insert(ci.fqName).second) return;
        auto bit = ci.baseClass.empty() ? classes_.end() : classes_.find(ci.baseClass);
        if (bit == classes_.end() || !ci.decl) return;
        ClassInfo &base = bit->second;
        visit(base); // the parent's inherited constructors exist before ours copy them
        if (base.ctors.empty()) return;
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
}

} // namespace grav
