#include "sema/typechecker.h"

#include <algorithm>

namespace grav {

namespace {
std::string joinDots(const std::vector<std::string> &parts, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        if (i) out += '.';
        out += parts[i];
    }
    return out;
}
bool isPrintable(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int:
        case TypeRef::Kind::Float:
        case TypeRef::Kind::Bool:
        case TypeRef::Kind::String:
            return true;
        default:
            return false;
    }
}
} // namespace

std::optional<std::vector<std::string>>
TypeChecker::flattenNames(const Expr *e) const {
    std::vector<std::string> out;
    const Expr *cur = e;
    while (true) {
        if (auto *m = dynamic_cast<const MemberExpr *>(cur)) {
            out.push_back(m->member);
            cur = m->object.get();
        } else if (auto *n = dynamic_cast<const NameExpr *>(cur)) {
            out.push_back(n->name);
            break;
        } else {
            return std::nullopt;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void TypeChecker::checkArgs(const std::vector<ExprPtr> &args,
                            const std::vector<TypeRef> &params, int line, int col,
                            const std::string &what) {
    std::vector<TypeRef> argTypes;
    for (auto &a : args) argTypes.push_back(checkExpr(*a));
    if (argTypes.size() != params.size()) {
        error(line, col, what + " expects " + std::to_string(params.size()) +
                             " argument(s), but got " + std::to_string(argTypes.size()));
    }
    size_t n = std::min(argTypes.size(), params.size());
    for (size_t i = 0; i < n; ++i) {
        if (!isAssignable(argTypes[i], params[i])) {
            error(args[i]->line, args[i]->col,
                  "argument " + std::to_string(i + 1) + " to " + what +
                      " expects " + typeRefName(params[i]) + ", but got " +
                      typeRefName(argTypes[i]));
        }
    }
}

TypeRef TypeChecker::checkExpr(Expr &expr) {
    TypeRef t = TypeRef::prim(TypeRef::Kind::Error);
    if (dynamic_cast<IntLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::Int);
    else if (dynamic_cast<FloatLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::Float);
    else if (dynamic_cast<BoolLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::Bool);
    else if (dynamic_cast<StringLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::String);
    else if (auto *e = dynamic_cast<SelfExpr *>(&expr)) {
        if (currentClass_.empty() || inStatic_) {
            error(e->line, e->col, "'self' is only valid inside an instance method or constructor");
            t = TypeRef::prim(TypeRef::Kind::Error);
        } else {
            t = TypeRef::named(currentClass_);
        }
    } else if (auto *e = dynamic_cast<NameExpr *>(&expr)) {
        LocalVar *local = lookupLocal(e->name);
        if (local) {
            local->used = true;
            t = local->type;
        } else {
            error(e->line, e->col, "use of undeclared variable '" + e->name + "'");
            t = TypeRef::prim(TypeRef::Kind::Error);
        }
    } else if (auto *e = dynamic_cast<BinaryExpr *>(&expr)) {
        t = checkBinary(*e);
    } else if (auto *e = dynamic_cast<UnaryExpr *>(&expr)) {
        t = checkUnary(*e);
    } else if (auto *e = dynamic_cast<IncDecExpr *>(&expr)) {
        t = checkIncDec(*e);
    } else if (auto *e = dynamic_cast<CastExpr *>(&expr)) {
        t = checkCast(*e);
    } else if (auto *e = dynamic_cast<NewExpr *>(&expr)) {
        t = checkNew(*e);
    } else if (auto *e = dynamic_cast<StructLiteralExpr *>(&expr)) {
        t = checkStructLiteral(*e);
    } else if (auto *e = dynamic_cast<CallExpr *>(&expr)) {
        t = checkCall(*e);
    } else if (auto *e = dynamic_cast<MemberExpr *>(&expr)) {
        t = checkMember(*e);
    }
    expr.type = t;
    return t;
}

TypeRef TypeChecker::checkBinary(BinaryExpr &e) {
    TypeRef lt = checkExpr(*e.left);
    TypeRef rt = checkExpr(*e.right);
    if (lt.isError() || rt.isError()) return TypeRef::prim(TypeRef::Kind::Error);

    const char *sym = binaryOpSymbol(e.op);
    if (isLogical(e.op)) {
        if (lt.kind != TypeRef::Kind::Bool || rt.kind != TypeRef::Kind::Bool)
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires bool operands, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt));
        return TypeRef::prim(TypeRef::Kind::Bool);
    }
    if (!isComparison(e.op)) {
        // string + string -> concatenation
        if (e.op == BinaryOp::Add && lt.kind == TypeRef::Kind::String &&
            rt.kind == TypeRef::Kind::String) {
            e.stringConcat = true;
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (!lt.isNumeric() || !rt.isNumeric()) {
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires numeric operands, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        if (lt != rt) {
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires matching types, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt) +
                                     " (use int()/float() to convert)");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        return lt;
    }

    bool ordered = e.op != BinaryOp::Eq && e.op != BinaryOp::NotEq;
    if (lt != rt) {
        error(e.line, e.col, std::string("operator '") + sym +
                                 "' requires matching types, but got " +
                                 typeRefName(lt) + " and " + typeRefName(rt));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (ordered && !lt.isNumeric()) {
        error(e.line, e.col, std::string("operator '") + sym +
                                 "' requires numeric operands");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return TypeRef::prim(TypeRef::Kind::Bool);
}

bool TypeChecker::isLvalue(const Expr &e) const {
    return dynamic_cast<const NameExpr *>(&e) != nullptr ||
           dynamic_cast<const MemberExpr *>(&e) != nullptr;
}

TypeRef TypeChecker::checkUnary(UnaryExpr &e) {
    TypeRef t = checkExpr(*e.operand);
    if (t.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // Only '!' for now.
    if (t.kind != TypeRef::Kind::Bool) {
        error(e.line, e.col,
              "operator '!' requires a bool operand, but got " + typeRefName(t));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return TypeRef::prim(TypeRef::Kind::Bool);
}

TypeRef TypeChecker::checkIncDec(IncDecExpr &e) {
    TypeRef t = checkExpr(*e.target);
    if (t.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (!isLvalue(*e.target)) {
        error(e.line, e.col, "'++'/'--' requires a variable or field");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!t.isNumeric()) {
        error(e.line, e.col,
              "'++'/'--' requires a numeric operand, but got " + typeRefName(t));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return t;
}

TypeRef TypeChecker::checkCast(CastExpr &e) {
    TypeRef src = checkExpr(*e.operand);
    if (src.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    bool ok = false;
    switch (e.target.kind) {
        case TypeRef::Kind::Int:
        case TypeRef::Kind::Float:
        case TypeRef::Kind::Bool:
            ok = src.isNumeric() || src.kind == TypeRef::Kind::Bool;
            break;
        case TypeRef::Kind::String:
            ok = src.kind == TypeRef::Kind::String;
            break;
        default:
            ok = false;
    }
    if (!ok) {
        error(e.line, e.col, "cannot cast a value of type " + typeRefName(src) +
                                 " to " + typeRefName(e.target));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return e.target;
}

TypeRef TypeChecker::checkNew(NewExpr &e) {
    std::string fq = reg_->resolveType(e.className, currentNs_);
    if (fq.empty() || !reg_->isClass(fq)) {
        error(e.line, e.col, "cannot construct unknown class '" + e.className + "'");
        for (auto &a : e.args) checkExpr(*a);
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    e.className = fq;
    const ClassInfo *ci = reg_->cls(fq);
    if (ci->isAbstract) {
        error(e.line, e.col, "cannot instantiate abstract class '" + fq + "'");
    }
    std::vector<TypeRef> params;
    if (ci->hasConstructor) {
        for (auto &p : ci->constructor->params) params.push_back(p.type);
    }
    checkArgs(e.args, params, e.line, e.col, "constructor of '" + fq + "'");
    return TypeRef::named(fq);
}

TypeRef TypeChecker::checkStructLiteral(StructLiteralExpr &e) {
    std::string fq = reg_->resolveType(e.typeName, currentNs_);
    if (fq.empty() || !reg_->isStruct(fq)) {
        error(e.line, e.col, "'" + e.typeName + "' is not a struct type");
        for (auto &fi : e.fields) checkExpr(*fi.value);
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    e.typeName = fq;
    const StructInfo *si = reg_->strct(fq);

    std::vector<bool> seen(si->fields.size(), false);
    for (auto &fi : e.fields) {
        TypeRef vt = checkExpr(*fi.value);
        const FieldInfo *f = reg_->findStructField(fq, fi.name);
        if (!f) {
            error(fi.line, fi.col,
                  "struct '" + fq + "' has no field '" + fi.name + "'");
            continue;
        }
        size_t idx = static_cast<size_t>(f - si->fields.data());
        if (idx < seen.size()) {
            if (seen[idx])
                error(fi.line, fi.col, "field '" + fi.name + "' is set more than once");
            seen[idx] = true;
        }
        if (!isAssignable(vt, f->type))
            error(fi.value->line, fi.value->col,
                  "field '" + fi.name + "' expects " + typeRefName(f->type) +
                      ", but got " + typeRefName(vt));
    }
    for (size_t i = 0; i < si->fields.size(); ++i)
        if (!seen[i])
            error(e.line, e.col, "struct literal for '" + fq +
                                     "' is missing field '" + si->fields[i].name + "'");
    return TypeRef::named(fq);
}

TypeRef TypeChecker::checkCall(CallExpr &e) {
    // Free function or builtin: callee is a bare name.
    if (auto *name = dynamic_cast<NameExpr *>(e.callee.get())) {
        if (name->name == "print") {
            e.kind = CallKind::Builtin;
            e.targetName = "print";
            std::vector<TypeRef> argTypes;
            for (auto &a : e.args) argTypes.push_back(checkExpr(*a));
            if (argTypes.size() != 1) {
                error(e.line, e.col, "print expects exactly 1 argument");
            } else if (!isPrintable(argTypes[0])) {
                error(e.args[0]->line, e.args[0]->col,
                      "print cannot display a value of type " + typeRefName(argTypes[0]));
            }
            return TypeRef::prim(TypeRef::Kind::Void);
        }
        if (name->name == "typename") {
            e.kind = CallKind::Builtin;
            e.targetName = "typename";
            if (e.args.size() != 1) {
                error(e.line, e.col, "typename expects exactly 1 argument");
            } else {
                TypeRef t = checkExpr(*e.args[0]);
                if (!t.isError() && !(t.isNamed() && reg_->isClass(t.name)))
                    error(e.args[0]->line, e.args[0]->col,
                          "typename expects a class instance, got " + typeRefName(t));
            }
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "isInstance") {
            e.kind = CallKind::Builtin;
            e.targetName = "isInstance";
            if (e.args.size() != 2) {
                error(e.line, e.col, "isInstance expects (instance, Type)");
                return TypeRef::prim(TypeRef::Kind::Bool);
            }
            TypeRef obj = checkExpr(*e.args[0]);
            if (!obj.isError() && !(obj.isNamed() && reg_->isClass(obj.name)))
                error(e.args[0]->line, e.args[0]->col,
                      "isInstance expects a class instance as the first argument");
            // Second argument names a type, not a value.
            auto chain = flattenNames(e.args[1].get());
            std::string typeFq = chain ? reg_->resolveType(joinDots(*chain, chain->size()), currentNs_) : "";
            if (typeFq.empty() || !reg_->isClass(typeFq)) {
                error(e.args[1]->line, e.args[1]->col,
                      "isInstance expects a class name as the second argument");
            }
            e.ownerClass = typeFq; // stash resolved class for codegen
            return TypeRef::prim(TypeRef::Kind::Bool);
        }
        std::string fnFq = reg_->resolveFunc(name->name, currentNs_);
        if (fnFq.empty()) {
            error(e.line, e.col, "unknown function '" + name->name + "'");
            for (auto &a : e.args) checkExpr(*a);
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        const FunctionInfo *fi = reg_->func(fnFq);
        e.kind = CallKind::FreeFunction;
        e.targetName = fnFq;
        checkArgs(e.args, fi->paramTypes, e.line, e.col, "function '" + fnFq + "'");
        return fi->returnType;
    }

    auto *mem = dynamic_cast<MemberExpr *>(e.callee.get());
    if (!mem) {
        error(e.line, e.col, "this expression is not callable");
        return TypeRef::prim(TypeRef::Kind::Error);
    }

    // Static method or namespace-qualified function: a pure name chain whose
    // head is not a local variable.
    auto chain = flattenNames(e.callee.get());
    if (chain && chain->size() >= 2 && lookupLocal((*chain)[0]) == nullptr) {
        std::string last = chain->back();
        std::string prefix = joinDots(*chain, chain->size() - 1);
        std::string clsFq = reg_->resolveType(prefix, currentNs_);
        if (!clsFq.empty() && reg_->isClass(clsFq)) {
            const MethodInfo *mi = reg_->findMethod(clsFq, last);
            if (mi && mi->isStatic) {
                mem->kind = MemberKind::StaticTarget;
                mem->qualified = clsFq;
                e.kind = CallKind::StaticMethod;
                e.targetName = clsFq;
                e.methodName = last;
                e.ownerClass = mi->definingClass;
                if (!checkAccess(mi->access, mi->definingClass))
                    error(e.line, e.col, "static method '" + last + "' is " +
                                             accessName(mi->access));
                checkArgs(e.args, mi->paramTypes, e.line, e.col,
                          "static method '" + clsFq + "." + last + "'");
                return mi->returnType;
            }
            if (mi && !mi->isStatic) {
                error(e.line, e.col, "method '" + last + "' is not static; call it on an instance");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
        }
        std::string fnFq = reg_->resolveFunc(joinDots(*chain, chain->size()), currentNs_);
        if (!fnFq.empty()) {
            const FunctionInfo *fi = reg_->func(fnFq);
            e.kind = CallKind::FreeFunction;
            e.targetName = fnFq;
            checkArgs(e.args, fi->paramTypes, e.line, e.col, "function '" + fnFq + "'");
            return fi->returnType;
        }
        error(e.line, e.col, "unknown function or static method '" +
                                 joinDots(*chain, chain->size()) + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }

    // Instance method: dispatch on the object's class via its vtable.
    TypeRef objType = checkExpr(*mem->object);
    if (objType.isError()) return TypeRef::prim(TypeRef::Kind::Error);

    // Interface-typed receiver: dispatch through the interface's itable.
    if (objType.isNamed() && reg_->isInterface(objType.name)) {
        const MethodInfo *mi = reg_->findInterfaceMethod(objType.name, mem->member);
        if (!mi) {
            error(mem->line, mem->col, "interface '" + objType.name +
                                           "' has no method '" + mem->member + "'");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        mem->kind = MemberKind::MethodRef;
        e.kind = CallKind::InstanceMethod;
        e.ifaceDispatch = true;
        e.ownerClass = objType.name; // interface fq
        e.methodName = mem->member;
        checkArgs(e.args, mi->paramTypes, e.line, e.col,
                  "method '" + objType.name + "." + mem->member + "'");
        return mi->returnType;
    }

    if (!objType.isNamed() || !reg_->isClass(objType.name)) {
        error(mem->line, mem->col, "cannot call method '" + mem->member +
                                       "' on a value of type " + typeRefName(objType));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    const MethodInfo *mi = reg_->findMethod(objType.name, mem->member);
    if (!mi || mi->isStatic) {
        error(mem->line, mem->col, "type '" + objType.name + "' has no method '" +
                                       mem->member + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!checkAccess(mi->access, mi->definingClass))
        error(mem->line, mem->col, "method '" + mem->member + "' is " +
                                       accessName(mi->access) + " in '" +
                                       mi->definingClass + "'");

    // Find the vtable slot owner for the static receiver type.
    std::string slotOwner = objType.name;
    if (const ClassInfo *ci = reg_->cls(objType.name)) {
        for (const auto &s : ci->slots)
            if (s.name == mem->member) { slotOwner = s.slotOwner; break; }
    }
    mem->kind = MemberKind::MethodRef;
    mem->ownerClass = objType.name;
    e.kind = CallKind::InstanceMethod;
    e.methodName = mem->member;
    e.ownerClass = objType.name;
    e.slotOwner = slotOwner;
    checkArgs(e.args, mi->paramTypes, e.line, e.col,
              "method '" + objType.name + "." + mem->member + "'");
    return mi->returnType;
}

TypeRef TypeChecker::checkMember(MemberExpr &e) {
    TypeRef objType = checkExpr(*e.object);
    if (objType.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (objType.isNamed() && reg_->isStruct(objType.name)) {
        const FieldInfo *f = reg_->findStructField(objType.name, e.member);
        if (!f) {
            error(e.line, e.col, "struct '" + objType.name + "' has no field '" +
                                     e.member + "'");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        e.kind = MemberKind::InstanceField;
        e.ownerClass = objType.name;
        return f->type;
    }
    if (!objType.isNamed() || !reg_->isClass(objType.name)) {
        error(e.line, e.col, "cannot access member '" + e.member +
                                 "' of a value of type " + typeRefName(objType));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    const FieldInfo *f = reg_->findField(objType.name, e.member);
    if (!f) {
        if (reg_->findMethod(objType.name, e.member))
            error(e.line, e.col, "method '" + e.member + "' must be called");
        else
            error(e.line, e.col, "type '" + objType.name + "' has no field '" +
                                     e.member + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!checkAccess(f->access, f->definingClass))
        error(e.line, e.col, "field '" + e.member + "' is " +
                                 accessName(f->access) + " in '" + f->definingClass + "'");
    e.kind = MemberKind::InstanceField;
    e.ownerClass = f->definingClass;
    return f->type;
}

} // namespace grav
