// Nullable values (`T?`): unwrapping with `x!`, and narrowing — inside
// `if (x != null)` (and after `if (x == null) { return }`, on the right of
// `x != null && ...`, in `x != null ? ... : ...`) a `T?` local reads as a T.
#include "sema/typechecker.h"

namespace grav {

// The locals a condition proves non-null when it is `whenTrue`.
void TypeChecker::nullFacts(Expr &cond, bool whenTrue, std::vector<std::string> &out) {
    if (auto *u = dynamic_cast<UnaryExpr *>(&cond); u && u->op == UnaryOp::Not)
        return nullFacts(*u->operand, !whenTrue, out);
    auto *b = dynamic_cast<BinaryExpr *>(&cond);
    if (!b) return;
    if ((b->op == BinaryOp::And && whenTrue) || (b->op == BinaryOp::Or && !whenTrue)) {
        nullFacts(*b->left, whenTrue, out);
        nullFacts(*b->right, whenTrue, out);
        return;
    }
    if (b->op != BinaryOp::Eq && b->op != BinaryOp::NotEq) return;
    // `x != null` holds a value when true; `x == null` when false.
    if ((b->op == BinaryOp::NotEq) != whenTrue) return;
    Expr *side = dynamic_cast<NullLiteralExpr *>(b->right.get()) ? b->left.get()
               : dynamic_cast<NullLiteralExpr *>(b->left.get()) ? b->right.get() : nullptr;
    auto *n = dynamic_cast<NameExpr *>(side);
    if (!n) return;
    LocalVar *v = lookupLocal(n->name);
    if (v && v->type.isNullable()) out.push_back(n->name);
}

// Whether a statement (or expression) may store into the local `name`.
bool TypeChecker::assignsName(Stmt &s, const std::string &name) {
    bool found = false;
    auto isIt = [&](const Expr *e) {
        auto *n = dynamic_cast<const NameExpr *>(e);
        return n && n->name == name;
    };
    visitStmt(s, [&](Stmt &st) {
        if (auto *a = dynamic_cast<AssignStmt *>(&st); a && isIt(a->target.get())) found = true;
    }, [&](Expr &e) {
        if (auto *x = dynamic_cast<IncDecExpr *>(&e); x && isIt(x->target.get())) found = true;
        if (auto *x = dynamic_cast<AddrOfExpr *>(&e); x && isIt(x->operand.get())) found = true;
    });
    return found;
}

bool TypeChecker::exprAssignsName(Expr &e, const std::string &name) {
    bool found = false;
    visitExpr(e, [&](Expr &x) {
        const Expr *t = nullptr;
        if (auto *i = dynamic_cast<IncDecExpr *>(&x)) t = i->target.get();
        if (auto *a = dynamic_cast<AddrOfExpr *>(&x)) t = a->operand.get();
        if (auto *n = dynamic_cast<const NameExpr *>(t); n && n->name == name) found = true;
    });
    return found;
}

// Opens a scope where each of `names` (a `T?` local) is seen as its T.
void TypeChecker::pushNarrowing(const std::vector<std::string> &names) {
    std::unordered_map<std::string, LocalVar> shadows;
    for (const auto &n : names) {
        LocalVar *v = lookupLocal(n);
        if (!v || !v->type.isNullable() || shadows.count(n)) continue;
        LocalVar s = *v;
        s.narrowed = true;
        s.narrowedFrom = v->type;
        s.type = *v->type.elem;
        s.used = true;
        shadows[n] = s;
    }
    scopes_.push_back(std::move(shadows));
    narrowScopes_.push_back(scopes_.size() - 1);
}

void TypeChecker::popNarrowing() {
    narrowScopes_.pop_back();
    popScope();
}

// The names among `facts` that `region` never assigns.
std::vector<std::string> TypeChecker::keepUnassigned(std::vector<std::string> facts,
                                                     const std::vector<Stmt *> &region) {
    std::vector<std::string> out;
    for (auto &n : facts) {
        bool assigned = false;
        for (Stmt *s : region) assigned = assigned || assignsName(*s, n);
        if (!assigned) out.push_back(std::move(n));
    }
    return out;
}

static std::vector<Stmt *> blockStmts(Block &b) {
    std::vector<Stmt *> out;
    for (auto &s : b.statements) out.push_back(s.get());
    return out;
}

void TypeChecker::checkIf(IfStmt &s) {
    requireBool(*s.cond, "an 'if' condition");
    std::vector<std::string> yes, no;
    nullFacts(*s.cond, true, yes);
    nullFacts(*s.cond, false, no);
    pushNarrowing(keepUnassigned(std::move(yes), blockStmts(s.thenBlock)));
    checkBlock(s.thenBlock);
    popNarrowing();
    if (s.elseStmt) {
        pushNarrowing(keepUnassigned(std::move(no), {s.elseStmt.get()}));
        checkStmt(*s.elseStmt);
        popNarrowing();
    }
}

// Whether a block always leaves (its last statement returns, throws, breaks or continues).
static bool alwaysExits(const Block &b) {
    if (b.statements.empty()) return false;
    const Stmt *last = b.statements.back().get();
    return dynamic_cast<const ReturnStmt *>(last) || dynamic_cast<const ThrowStmt *>(last) ||
           dynamic_cast<const BreakStmt *>(last) || dynamic_cast<const ContinueStmt *>(last);
}

// After `if (x == null) { return }` (no else), the rest of the block sees x as a T.
// Returns whether a narrowing scope was opened (closed at the end of the block).
bool TypeChecker::narrowAfterGuard(Stmt &s, Block &block, size_t index) {
    auto *ifs = dynamic_cast<IfStmt *>(&s);
    if (!ifs || ifs->elseStmt || !alwaysExits(ifs->thenBlock)) return false;
    std::vector<std::string> facts;
    nullFacts(*ifs->cond, false, facts);
    if (facts.empty()) return false;
    std::vector<Stmt *> rest;
    for (size_t i = index + 1; i < block.statements.size(); ++i) rest.push_back(block.statements[i].get());
    facts = keepUnassigned(std::move(facts), rest);
    if (facts.empty()) return false;
    pushNarrowing(facts);
    return true;
}

// `T?` for a T that can be one (references are already nullable: themselves).
std::optional<TypeRef> TypeChecker::nullableOf(const TypeRef &t) const {
    if (t.isNullable()) return t;
    bool ok = true;
    TypeRef n = reg_->resolveCanonical(TypeRef::nullable(t), "", ok);
    if (!ok || n.isError()) return std::nullopt;
    return n;
}

// `x!`: the value of a `T?` (or a non-null reference); a fault when it is null.
TypeRef TypeChecker::checkUnwrap(CallExpr &e) {
    e.kind = CallKind::Builtin;
    e.targetName = "__unwrap";
    TypeRef t = checkExpr(*e.args[0]);
    if (t.isError()) return t;
    if (t.isNullable()) return *t.elem;
    bool ref = t.isPointer() || t.isFunction() ||
               (t.isNamed() && (reg_->isClass(t.name) || reg_->isInterface(t.name)));
    if (ref) return t;
    if (t.kind == TypeRef::Kind::Null)
        error(e.line, e.col, "'!' on null: it is always null");
    else
        error(e.line, e.col, "'!' needs a value that may be null, but " + aTypeName(t) +
                                 " never is");
    return TypeRef::prim(TypeRef::Kind::Error);
}

} // namespace grav
