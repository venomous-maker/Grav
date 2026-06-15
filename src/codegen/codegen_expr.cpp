#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

std::string CodeGen::escapeC(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string CodeGen::emitAs(const Expr &expr, const TypeRef &target) const {
    std::string e = emitExpr(expr);
    // Boxing a class instance into an interface value builds a fat pointer
    // (object + the class's table for that interface).
    if (target.isNamed() && reg_->isInterface(target.name) &&
        expr.type.isNamed() && reg_->isClass(expr.type.name)) {
        return "(struct GravIface){ (void*)(" + e + "), &" +
               mangle(expr.type.name) + "__itab__" + mangle(target.name) + " }";
    }
    // Upcasts between named class pointer types need an explicit C cast.
    if (target.isNamed() && reg_->isClass(target.name) && expr.type.isNamed() &&
        reg_->isClass(expr.type.name) && target.name != expr.type.name) {
        return "(" + cTy(target) + ")(" + e + ")";
    }
    return e;
}

void CodeGen::emitArgs(std::string &out, const CallExpr &call,
                       const std::vector<TypeRef> &params, bool leadingSelf,
                       const std::string &self) const {
    bool first = !leadingSelf;
    if (leadingSelf) out += self;
    for (size_t i = 0; i < call.args.size(); ++i) {
        if (!first) out += ", ";
        first = false;
        if (i < params.size()) out += emitAs(*call.args[i], params[i]);
        else out += emitExpr(*call.args[i]);
    }
}

std::string CodeGen::emitExpr(const Expr &expr) const {
    if (auto *e = dynamic_cast<const IntLiteralExpr *>(&expr)) return e->raw;
    if (auto *e = dynamic_cast<const FloatLiteralExpr *>(&expr)) return e->raw;
    if (auto *e = dynamic_cast<const BoolLiteralExpr *>(&expr))
        return e->value ? "true" : "false";
    if (auto *e = dynamic_cast<const StringLiteralExpr *>(&expr))
        return "\"" + escapeC(e->value) + "\"";
    if (auto *e = dynamic_cast<const NameExpr *>(&expr)) return e->name;
    if (dynamic_cast<const SelfExpr *>(&expr)) return "self";

    if (auto *e = dynamic_cast<const BinaryExpr *>(&expr)) {
        std::string l = emitExpr(*e->left);
        std::string r = emitExpr(*e->right);
        if (e->stringConcat) return "grav_str_concat(" + l + ", " + r + ")";
        return "(" + l + " " + binaryOpSymbol(e->op) + " " + r + ")";
    }
    if (auto *e = dynamic_cast<const UnaryExpr *>(&expr)) {
        return "(!" + emitExpr(*e->operand) + ")";
    }
    if (auto *e = dynamic_cast<const IncDecExpr *>(&expr)) {
        std::string op = e->isIncrement ? "++" : "--";
        std::string t = emitExpr(*e->target);
        return e->isPrefix ? ("(" + op + t + ")") : ("(" + t + op + ")");
    }
    if (auto *e = dynamic_cast<const CastExpr *>(&expr)) {
        std::string inner = emitExpr(*e->operand);
        switch (e->target.kind) {
            case TypeRef::Kind::Int: return "((int)(" + inner + "))";
            case TypeRef::Kind::Float: return "((double)(" + inner + "))";
            case TypeRef::Kind::Bool: return "((" + inner + ") != 0)";
            default: return inner;
        }
    }
    if (auto *e = dynamic_cast<const NewExpr *>(&expr)) {
        std::vector<TypeRef> params;
        if (const ClassInfo *ci = reg_->cls(e->className))
            if (ci->constructor)
                for (const auto &p : ci->constructor->params) params.push_back(p.type);
        std::string out = ctorCName(e->className) + "(";
        for (size_t i = 0; i < e->args.size(); ++i) {
            if (i) out += ", ";
            out += (i < params.size()) ? emitAs(*e->args[i], params[i])
                                       : emitExpr(*e->args[i]);
        }
        return out + ")";
    }
    if (auto *e = dynamic_cast<const StructLiteralExpr *>(&expr)) {
        std::string out = "(" + structName(e->typeName) + "){ ";
        if (e->fields.empty()) return out + "0 }"; // initializes the __empty member
        for (size_t i = 0; i < e->fields.size(); ++i) {
            if (i) out += ", ";
            const FieldInfo *f = reg_->findStructField(e->typeName, e->fields[i].name);
            std::string v = f ? emitAs(*e->fields[i].value, f->type)
                              : emitExpr(*e->fields[i].value);
            out += "." + e->fields[i].name + " = " + v;
        }
        return out + " }";
    }
    if (auto *e = dynamic_cast<const CallExpr *>(&expr)) return emitCall(*e);
    if (auto *e = dynamic_cast<const MemberExpr *>(&expr)) {
        // Field read: structs are values (`.`), class instances are pointers (`->`).
        bool valueObj = e->object->type.isNamed() && reg_->isStruct(e->object->type.name);
        return "(" + emitExpr(*e->object) + (valueObj ? ")." : ")->") + e->member;
    }
    return "/*?*/ 0";
}

std::string CodeGen::emitCall(const CallExpr &call) const {
    switch (call.kind) {
        case CallKind::Builtin: {
            if (call.targetName == "typename") {
                return "grav_typename(" + emitExpr(*call.args[0]) + ")";
            }
            if (call.targetName == "isInstance") {
                return "grav_is_instance(" + emitExpr(*call.args[0]) + ", &" +
                       mangle(call.ownerClass) + "_typeinfo)";
            }
            // print
            std::string a = call.args.empty() ? "0" : emitExpr(*call.args[0]);
            TypeRef t = call.args.empty() ? TypeRef::prim(TypeRef::Kind::Int)
                                          : call.args[0]->type;
            switch (t.kind) {
                case TypeRef::Kind::Int: return "printf(\"%d\\n\", " + a + ")";
                case TypeRef::Kind::Float: return "printf(\"%f\\n\", " + a + ")";
                case TypeRef::Kind::Bool:
                    return "printf(\"%s\\n\", (" + a + ") ? \"true\" : \"false\")";
                case TypeRef::Kind::String: return "printf(\"%s\\n\", " + a + ")";
                default: return "printf(\"\\n\")";
            }
        }
        case CallKind::FreeFunction: {
            std::vector<TypeRef> params;
            if (const FunctionInfo *fi = reg_->func(call.targetName))
                params = fi->paramTypes;
            std::string out = funcCName(call.targetName) + "(";
            emitArgs(out, call, params, false, "");
            return out + ")";
        }
        case CallKind::StaticMethod: {
            std::vector<TypeRef> params;
            if (const MethodInfo *mi = reg_->findMethod(call.targetName, call.methodName))
                params = mi->paramTypes;
            std::string out = methodCName(call.targetName, call.methodName) + "(";
            emitArgs(out, call, params, false, "");
            return out + ")";
        }
        case CallKind::InstanceMethod: {
            auto *mem = dynamic_cast<const MemberExpr *>(call.callee.get());
            std::string obj = emitExpr(*mem->object);
            if (call.ifaceDispatch) {
                std::string itab = mangle(call.ownerClass) + "_ITAB";
                std::vector<TypeRef> params;
                if (const MethodInfo *mi =
                        reg_->findInterfaceMethod(call.ownerClass, call.methodName))
                    params = mi->paramTypes;
                std::string disp = "((" + itab + "*)(" + obj + ").itab)->" +
                                   call.methodName;
                std::string self = "(" + obj + ").obj";
                std::string out = disp + "(";
                emitArgs(out, call, params, true, self);
                return out + ")";
            }
            std::string vt = vtableType(call.slotOwner);
            std::string self = "(" + structName(call.slotOwner) + "*)(" + obj + ")";
            std::string disp = "((" + vt + "*)((struct GravObject*)(" + obj +
                               "))->__vt)->" + call.methodName;
            std::vector<TypeRef> params;
            if (const MethodInfo *mi = reg_->findMethod(call.ownerClass, call.methodName))
                params = mi->paramTypes;
            std::string out = disp + "(";
            emitArgs(out, call, params, true, self);
            return out + ")";
        }
        default:
            return "/*unresolved call*/ 0";
    }
}

} // namespace grav
