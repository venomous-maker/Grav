// Evaluation order: Grav evaluates operands and arguments left to right, but C
// leaves the order of call arguments, of the two sides of `+`, of initialiser
// elements and so on unspecified. Where it matters (two operands can run code,
// or one can and another reads state it could change) the earlier operands are
// computed into temporaries first: before the statement (hoistTemps), or inside
// a statement expression where the expression is a condition or runs only
// sometimes (emitOrdered).
#include "codegen/codegen.h"

namespace grav {

namespace {

// The pieces of a fused string chain (same shape as CodeGen::flattenString).
void chainLeaves(const Expr &e, std::vector<const Expr *> &out) {
    if (auto *b = dynamic_cast<const BinaryExpr *>(&e);
        b && b->stringConcat && b->overloadMethod.empty()) {
        chainLeaves(*b->left, out);
        chainLeaves(*b->right, out);
        return;
    }
    if (dynamic_cast<const StringLiteralExpr *>(&e)) return;
    if (auto *c = dynamic_cast<const CallExpr *>(&e);
        c && c->kind == CallKind::Builtin && c->targetName == "str" && c->args.size() == 1 &&
        c->args[0]->type.kind == TypeRef::Kind::String) {
        chainLeaves(*c->args[0], out);
        return;
    }
    out.push_back(&e);
}

} // namespace

// The value a string-chain piece formats: `str(x)` of a non-string formats x itself.
static const Expr *pieceValue(const Expr *l) {
    if (auto *c = dynamic_cast<const CallExpr *>(l);
        c && c->kind == CallKind::Builtin && c->targetName == "str" && c->args.size() == 1 &&
        c->args[0]->type.kind != TypeRef::Kind::String)
        return c->args[0].get();
    return l;
}

void CodeGen::orderChain(const Expr &chain, std::vector<const Expr *> &out) const {
    std::vector<const Expr *> leaves;
    chainLeaves(chain, leaves);
    for (auto &l : leaves) l = pieceValue(l);
    orderOperands(leaves, out);
}

// Operands in Grav's order. When two or more can run code, or one can and
// another reads state it could change, each operand that can run code is
// computed into a temporary in turn, and a read that comes before a later call
// is copied first; otherwise only each operand's own insides are ordered.
void CodeGen::orderOperands(const std::vector<const Expr *> &ops, std::vector<const Expr *> &out,
                            bool hoistLast) const {
    int calls = 0, lastCall = -1;
    for (size_t i = 0; i < ops.size(); ++i)
        if (exprMayCall(ops[i])) { calls++; lastCall = static_cast<int>(i); }
    bool earlyRead = false, anyRead = false;
    for (size_t i = 0; i < ops.size(); ++i) {
        if (exprMayCall(ops[i]) || !readsMutable(ops[i])) continue;
        anyRead = true;
        if (static_cast<int>(i) < lastCall) earlyRead = true;
    }
    bool ordered = calls >= 2 || (calls >= 1 && anyRead);
    for (size_t i = 0; i < ops.size(); ++i) {
        const Expr *x = ops[i];
        const TypeRef &t = x->type;
        bool last = i + 1 == ops.size();
        if (!ordered || t.isVoid() || t.isSlice() || (last && !hoistLast)) { collectHoists(x, out); continue; }
        if (exprMayCall(x)) {
            collectHoists(x, out);
            out.push_back(x);
        } else if (earlyRead && static_cast<int>(i) < lastCall && readsMutable(x) && !t.isArray() &&
                   !(t.isNamed() && reg_->isStruct(t.name))) {
            out.push_back(x); // a copy of the value from before the call (not of an aggregate, which may be written through)
        } else {
            collectHoists(x, out);
        }
    }
}

// Does reading `e` depend on state a call could change (fields, elements,
// pointers, globals)? Locals cannot change: closures capture copies.
bool CodeGen::readsMutable(const Expr *e) const {
    if (!e) return false;
    if (dynamic_cast<const MemberExpr *>(e) || dynamic_cast<const IndexExpr *>(e) ||
        dynamic_cast<const DerefExpr *>(e))
        return true;
    if (auto *n = dynamic_cast<const NameExpr *>(e)) return !n->resolvedGlobal.empty();
    if (auto *x = dynamic_cast<const BinaryExpr *>(e)) return readsMutable(x->left.get()) || readsMutable(x->right.get());
    if (auto *x = dynamic_cast<const UnaryExpr *>(e)) return readsMutable(x->operand.get());
    if (auto *x = dynamic_cast<const AsExpr *>(e)) return readsMutable(x->operand.get());
    if (auto *x = dynamic_cast<const CastExpr *>(e)) return readsMutable(x->operand.get());
    if (auto *x = dynamic_cast<const TernaryExpr *>(e))
        return readsMutable(x->cond.get()) || readsMutable(x->thenExpr.get()) || readsMutable(x->elseExpr.get());
    return false;
}

void CodeGen::collectHoists(const Expr *e, std::vector<const Expr *> &out) const {
    if (!e) return;
    if (auto *b = dynamic_cast<const BinaryExpr *>(e)) {
        if (b->stringConcat && b->overloadMethod.empty()) { orderChain(*e, out); return; }
        // The right side of && and || runs only sometimes: it is ordered on its own.
        if (b->op == BinaryOp::And || b->op == BinaryOp::Or) { collectHoists(b->left.get(), out); return; }
        orderOperands({b->left.get(), b->right.get()}, out);
        return;
    }
    if (auto *c = dynamic_cast<const CallExpr *>(e)) {
        auto *mem = dynamic_cast<const MemberExpr *>(c->callee.get());
        // `a?.m(...)`: the arguments run only for a non-null receiver (ordered on their own).
        if (mem && mem->optional) { collectHoists(mem->object.get(), out); return; }
        if (c->kind == CallKind::Builtin && c->targetName == "print" && c->args.size() == 1) {
            const Expr *a = c->args[0].get();
            if (auto *b = dynamic_cast<const BinaryExpr *>(a); b && b->stringConcat)
                orderChain(*a, out); // printed piecewise
            else
                collectHoists(a, out);
            return;
        }
        std::vector<const Expr *> ops;
        // The receiver comes first (a namespace or type path before a free or
        // static function is no value).
        if (mem && c->kind == CallKind::InstanceMethod) ops.push_back(mem->object.get());
        for (const auto &a : c->args) ops.push_back(a.get());
        if (c->kind == CallKind::Builtin) { // emitted specially: only order their insides
            for (const Expr *x : ops) collectHoists(x, out);
            return;
        }
        orderOperands(ops, out);
        return;
    }
    if (auto *x = dynamic_cast<const NewExpr *>(e)) {
        std::vector<const Expr *> ops;
        for (const auto &a : x->args) ops.push_back(a.get());
        orderOperands(ops, out);
        return;
    }
    if (auto *x = dynamic_cast<const IndexExpr *>(e)) {
        std::vector<const Expr *> ops{x->base.get(), x->index.get()};
        for (const auto &m : x->more) ops.push_back(m.get());
        orderOperands(ops, out);
        return;
    }
    if (auto *x = dynamic_cast<const ArrayLiteralExpr *>(e)) {
        std::vector<const Expr *> ops;
        for (const auto &el : x->elements) ops.push_back(el.get());
        orderOperands(ops, out);
        return;
    }
    if (auto *x = dynamic_cast<const StructLiteralExpr *>(e)) {
        std::vector<const Expr *> ops;
        for (const auto &f : x->fields) ops.push_back(f.value.get());
        orderOperands(ops, out);
        return;
    }
    // A conditional's branches run only sometimes: they are ordered on their own.
    if (auto *x = dynamic_cast<const TernaryExpr *>(e)) { collectHoists(x->cond.get(), out); return; }
    if (auto *x = dynamic_cast<const CoalesceExpr *>(e)) { collectHoists(x->left.get(), out); return; }
    if (auto *x = dynamic_cast<const UnaryExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const AsExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const CastExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const IsExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const MemberExpr *>(e)) {
        if (!x->optional) collectHoists(x->object.get(), out);
        return;
    }
    // Literals, names, lambdas (their bodies are separate functions), etc.
}

// A hoisted expression's value. A copied reference to a field, element or
// global is held (on the release list) so the calls after it cannot free it.
std::string CodeGen::hoistValue(const Expr &h) const {
    if (isUnstableRef(h)) return emitStable(h, h.type);
    return h.type.kind == TypeRef::Kind::String ? emitToString(h) : emitExpr(h);
}

// Declares a temporary for each hoisted expression, in order, and renders the
// expression as that temporary from now on. Returns the temporaries' names.
std::vector<std::string> CodeGen::emitHoists(const std::vector<const Expr *> &hs) {
    std::vector<std::string> temps;
    for (const Expr *h : hs) {
        if (exprOverride_.count(h)) continue;
        std::string t = "grav_tmp" + std::to_string(throwCounter_++);
        line(cTy(h->type) + " " + t + " = " + hoistValue(*h) + ";");
        exprOverride_[h] = t;
        temps.push_back(t);
    }
    return temps;
}

// Hoisted values are borrowed (fresh results sit on the release list), so the
// temporaries need no freeing.
std::vector<std::string> CodeGen::hoistTemps(const Expr *root) {
    std::vector<const Expr *> hs;
    collectHoists(root, hs);
    return emitHoists(hs);
}

// An assignment's target comes before its value: the target's object and
// indices, then the value. The value itself is never moved into a temporary
// (it may be a +1 reference stored as is), and neither is an array, struct or
// raw buffer the target writes into (the value's code may move the buffer).
std::vector<std::string> CodeGen::hoistAssignTemps(const Expr &target, const Expr *value) {
    std::vector<const Expr *> ops, hs;
    auto refBase = [&](const Expr *b) {
        if (refKind(b->type) == RefKind::Obj || refKind(b->type) == RefKind::Iface)
            ops.push_back(b);
        else
            collectHoists(b, hs);
    };
    if (auto *ix = dynamic_cast<const IndexExpr *>(&target)) {
        refBase(ix->base.get());
        ops.push_back(ix->index.get());
        for (const auto &m : ix->more) ops.push_back(m.get());
    } else if (auto *m = dynamic_cast<const MemberExpr *>(&target); m && !m->optional && m->object) {
        refBase(m->object.get());
    }
    ops.push_back(value);
    orderOperands(ops, hs, /*hoistLast=*/false);
    return emitHoists(hs);
}

// Overrides refer to a statement's temps only for that statement.
void CodeGen::endTemps(const std::vector<std::string> &temps) {
    for (auto it = exprOverride_.begin(); it != exprOverride_.end();) {
        bool mine = false;
        for (const auto &t : temps)
            if (it->second == t) mine = true;
        it = mine ? exprOverride_.erase(it) : std::next(it);
    }
}

// `render()` with the hoists in `hs` computed first, as one C expression: the
// temporaries go in a statement expression around it.
std::string CodeGen::withHoists(const std::vector<const Expr *> &hs,
                                const std::function<std::string()> &render) const {
    std::string decls;
    std::vector<const Expr *> mine;
    for (const Expr *h : hs) {
        if (exprOverride_.count(h)) continue;
        std::string t = "grav_ord" + std::to_string(orderCounter_++);
        decls += cTy(h->type) + " " + t + " = " + hoistValue(*h) + "; ";
        exprOverride_[h] = t;
        mine.push_back(h);
    }
    std::string v = render();
    for (const Expr *h : mine) exprOverride_.erase(h);
    return mine.empty() ? v : "({ " + decls + v + "; })";
}

// `e` (rendered by `render`, by default as itself) in left-to-right order, where
// it cannot be hoisted ahead of its statement: a condition, or a part that runs
// only sometimes.
std::string CodeGen::emitOrdered(const Expr &e, const std::function<std::string()> &render) const {
    std::vector<const Expr *> hs;
    collectHoists(&e, hs);
    return withHoists(hs, render ? render : [&] { return emitExpr(e); });
}

} // namespace grav
