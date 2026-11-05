// Nullable values (`T?`) in C: a `string?` is a string pointer that may be NULL;
// any other `T?` is a { bool has; T v; } struct (GravOpt_<T>, see emitValueStructs).
#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

// `a == b` / `a != b` where a side is a `T?`: against null, a T, or another T?.
std::string CodeGen::nullableEq(const BinaryExpr &e) const {
    std::string neg = e.op == BinaryOp::Eq ? "" : "!";
    const Expr &opt = e.left->type.isNullable() ? *e.left : *e.right;
    const Expr &other = &opt == e.left.get() ? *e.right : *e.left;
    const TypeRef &t = opt.type;
    bool str = t.elem->kind == TypeRef::Kind::String;
    if (other.type.kind == TypeRef::Kind::Null)
        return str ? "(" + emitExpr(opt) + (neg.empty() ? " == 0)" : " != 0)")
                   : "(" + std::string(neg.empty() ? "!" : "") + "(" + emitExpr(opt) + ").has)";
    std::string l = emitExpr(*e.left), r = emitExpr(*e.right);
    if (str) return neg + "grav_streq_opt(" + l + ", " + r + ")";
    std::string key = optStructName(t);
    if (other.type.isNullable()) return neg + key + "__eq(" + l + ", " + r + ")";
    return neg + key + "__eqv(" + emitExpr(opt) + ", " + emitAs(other, *t.elem) + ")";
}

// `x!`: x's value, or a fault (StateError) when it is null.
std::string CodeGen::unwrapC(const Expr &x) const {
    const TypeRef &t = x.type;
    std::string test = "grav_u_", val = "grav_u_";
    if (t.isNullable() && t.elem->kind != TypeRef::Kind::String) { test = "grav_u_.has"; val = "grav_u_.v"; }
    else if (t.isNamed() && reg_->isInterface(t.name)) test = "grav_u_.obj";
    else if (t.isFunction()) test = "grav_u_.fn";
    std::string shown = t.isNamed() ? displayTypeName(t.name) : typeRefName(t);
    std::string msg = "\"'!' found a null " + escapeC(shown) + "\"";
    return "({ " + cTy(t) + " grav_u_ = " + emitExpr(x) + "; if (GRAV_UNLIKELY(!" + test +
           ")) grav_fault_null(" + msg + "); " + val + "; })";
}

} // namespace grav
