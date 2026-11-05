// Evaluation order for statements: which subexpressions are computed into
// temporaries first so Grav's left-to-right order holds where C leaves it
// unspecified (call arguments, the pieces of a string chain).
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

void CodeGen::orderChain(const Expr &chain, std::vector<const Expr *> &out) const {
    std::vector<const Expr *> leaves;
    chainLeaves(chain, leaves);
    // The value each piece formats: `str(x)` of a non-string formats x itself.
    auto valueOf = [](const Expr *l) -> const Expr * {
        if (auto *c = dynamic_cast<const CallExpr *>(l);
            c && c->kind == CallKind::Builtin && c->targetName == "str" && c->args.size() == 1 &&
            c->args[0]->type.kind != TypeRef::Kind::String)
            return c->args[0].get();
        return l;
    };
    int calls = 0, lastCall = -1;
    for (size_t i = 0; i < leaves.size(); ++i)
        if (exprMayCall(valueOf(leaves[i]))) { calls++; lastCall = static_cast<int>(i); }
    // A read of mutable state before a later call must see the value from before it.
    // Any such read next to a call needs the call pinned in order as well.
    bool earlyRead = false, anyRead = false;
    for (size_t i = 0; i < leaves.size(); ++i) {
        const Expr *v = valueOf(leaves[i]);
        if (exprMayCall(v) || !readsMutable(v)) continue;
        anyRead = true;
        if (static_cast<int>(i) < lastCall) earlyRead = true;
    }
    bool ordered = calls >= 2 || (calls >= 1 && anyRead);
    for (size_t i = 0; i < leaves.size(); ++i) {
        const Expr *l = leaves[i];
        const Expr *v = valueOf(l);
        if (earlyRead && static_cast<int>(i) < lastCall && !exprMayCall(v) && readsMutable(v)) {
            out.push_back(v);              // snapshot the read before the call runs
            orderOnly_.insert(v);
        } else if (ordered && exprMayCall(v)) {
            // C leaves argument order unspecified; Grav evaluates left to right.
            collectHoists(v, out);
            out.push_back(v);
            orderOnly_.insert(v);
        } else {
            collectHoists(l, out);
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
        if (b->stringConcat && b->overloadMethod.empty()) {
            orderChain(*e, out);
            return;
        }
        collectHoists(b->left.get(), out);
        if (b->op != BinaryOp::And && b->op != BinaryOp::Or) collectHoists(b->right.get(), out);
        return;
    }
    if (auto *c = dynamic_cast<const CallExpr *>(e)) {
        auto *mem = dynamic_cast<const MemberExpr *>(c->callee.get());
        if (mem && mem->optional) { collectHoists(mem->object.get(), out); return; } // args are conditional
        if (mem) collectHoists(mem->object.get(), out);
        if (c->kind == CallKind::Builtin && c->targetName == "print" && c->args.size() == 1) {
            const Expr *a = c->args[0].get();
            if (auto *b = dynamic_cast<const BinaryExpr *>(a); b && b->stringConcat)
                orderChain(*a, out); // printed piecewise
            else
                collectHoists(a, out);
            return;
        }
        // Left-to-right evaluation: with 2+ operands that can run code (receiver
        // included), each such operand is computed in order before the call.
        int calls = (mem && exprMayCall(mem->object.get())) ? 1 : 0;
        int lastCall = -1;
        for (size_t i = 0; i < c->args.size(); ++i)
            if (exprMayCall(c->args[i].get())) { calls++; lastCall = static_cast<int>(i); }
        // A read of mutable state in an earlier argument must happen before a later call.
        bool earlyRead = false, anyRead = false;
        for (size_t i = 0; i < c->args.size(); ++i) {
            if (exprMayCall(c->args[i].get()) || !readsMutable(c->args[i].get())) continue;
            anyRead = true;
            if (static_cast<int>(i) < lastCall) earlyRead = true;
        }
        if ((calls >= 2 || (calls >= 1 && anyRead)) && c->kind != CallKind::Builtin) {
            size_t argIndex = 0;
            auto order = [&](const Expr *x) {
                size_t idx = argIndex++;
                if (!exprMayCall(x)) {
                    if (earlyRead && static_cast<int>(idx) < lastCall && readsMutable(x)) {
                        out.push_back(x);
                        orderOnly_.insert(x);
                        return;
                    }
                    collectHoists(x, out);
                    return;
                }
                collectHoists(x, out);
                out.push_back(x);
                orderOnly_.insert(x);
            };
            if (mem) {
                // The receiver was walked above; hoist it itself if it can run code.
                if (exprMayCall(mem->object.get())) { out.push_back(mem->object.get()); orderOnly_.insert(mem->object.get()); }
            }
            for (const auto &a : c->args) order(a.get());
            return;
        }
        for (const auto &a : c->args) collectHoists(a.get(), out);
        return;
    }
    if (auto *x = dynamic_cast<const TernaryExpr *>(e)) { collectHoists(x->cond.get(), out); return; }
    if (auto *x = dynamic_cast<const CoalesceExpr *>(e)) { collectHoists(x->left.get(), out); return; }
    if (auto *x = dynamic_cast<const UnaryExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const AsExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const CastExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const IndexExpr *>(e)) {
        collectHoists(x->base.get(), out); collectHoists(x->index.get(), out);
        for (const auto &m : x->more) collectHoists(m.get(), out);
        return;
    }
    if (auto *x = dynamic_cast<const MemberExpr *>(e)) {
        if (!x->optional) collectHoists(x->object.get(), out);
        return;
    }
    if (auto *x = dynamic_cast<const NewExpr *>(e)) {
        for (const auto &a : x->args) collectHoists(a.get(), out);
        return;
    }
    // Literals, names, lambdas (their bodies are separate functions), etc.
}

std::vector<std::string> CodeGen::hoistTemps(const Expr *root) {
    // Hoisted subexpressions fix evaluation order. Their values are borrowed
    // (fresh results sit on the release list), so the temps need no freeing.
    std::vector<const Expr *> hs;
    collectHoists(root, hs);
    std::vector<std::string> temps;
    for (const Expr *h : hs) {
        if (exprOverride_.count(h)) continue;
        orderOnly_.erase(h);
        std::string t = "grav_tmp" + std::to_string(throwCounter_++);
        std::string val = h->type.kind == TypeRef::Kind::String ? emitToString(*h) : emitExpr(*h);
        line(cTy(h->type) + " " + t + " = " + val + ";");
        exprOverride_[h] = t;
        temps.push_back(t);
    }
    return temps;
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

} // namespace grav
