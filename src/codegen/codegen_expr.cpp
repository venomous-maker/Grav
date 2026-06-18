#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

// Lowers a cast to a primitive target (`as T` and `(T)x`). Integer/float targets
// spell out the full C type so widths are preserved; binary<->string convert
// through the runtime helpers.
std::string CodeGen::emitPrimCast(const TypeRef &target, const Expr &operand,
                                  const std::string &inner) const {
    switch (target.kind) {
        case TypeRef::Kind::Int:
        case TypeRef::Kind::Float:
            return "((" + cTy(target) + ")(" + inner + "))";
        case TypeRef::Kind::Bool:
            return "((" + inner + ") != 0)";
        case TypeRef::Kind::Binary:
            return operand.type.kind == TypeRef::Kind::String
                       ? "grav_bytes_from_str(" + inner + ")"
                       : inner;
        case TypeRef::Kind::String:
            return operand.type.isBinary() ? "grav_bytes_to_str(" + inner + ")" : inner;
        default:
            return inner;
    }
}

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
    // A null reference flowing into an interface value is an empty fat pointer.
    if (target.isNamed() && reg_->isInterface(target.name) &&
        expr.type.kind == TypeRef::Kind::Null) {
        return "(struct GravIface){ 0, 0 }";
    }
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

void CodeGen::emitVariadicArgs(std::string &out, const CallExpr &call,
                               const std::vector<TypeRef> &params) const {
    size_t fixed = params.empty() ? 0 : params.size() - 1;
    TypeRef elem = params.empty() ? TypeRef::prim(TypeRef::Kind::Int) : params.back();
    std::string elemTy = cTy(elem);
    for (size_t i = 0; i < fixed; ++i) {
        if (i) out += ", ";
        out += emitAs(*call.args[i], params[i]);
    }
    if (fixed) out += ", ";
    size_t count = call.args.size() - fixed;
    out += std::to_string(count) + ", ";
    if (count == 0) {
        out += "(" + elemTy + "*)0"; // no variadic args
    } else {
        out += "(" + elemTy + "[]){ ";
        for (size_t i = fixed; i < call.args.size(); ++i) {
            if (i > fixed) out += ", ";
            out += emitAs(*call.args[i], elem);
        }
        out += " }";
    }
}

std::string CodeGen::emitExpr(const Expr &expr) const {
    if (auto *e = dynamic_cast<const IntLiteralExpr *>(&expr)) return e->raw;
    if (auto *e = dynamic_cast<const FloatLiteralExpr *>(&expr)) return e->raw;
    if (auto *e = dynamic_cast<const BoolLiteralExpr *>(&expr))
        return e->value ? "true" : "false";
    if (auto *e = dynamic_cast<const StringLiteralExpr *>(&expr))
        return "\"" + escapeC(e->value) + "\"";
    if (dynamic_cast<const NullLiteralExpr *>(&expr)) return "0";
    if (auto *e = dynamic_cast<const CBlockExpr *>(&expr)) return hoistIncludes(e->code);
    if (auto *e = dynamic_cast<const NameExpr *>(&expr))
        return e->resolvedGlobal.empty() ? e->name : e->resolvedGlobal;
    if (dynamic_cast<const SelfExpr *>(&expr)) return "self";
    if (dynamic_cast<const SuperExpr *>(&expr)) return "self"; // viewed as parent in calls

    if (auto *e = dynamic_cast<const BinaryExpr *>(&expr)) {
        // Comparing an interface value against null tests its object pointer.
        if (e->op == BinaryOp::Eq || e->op == BinaryOp::NotEq) {
            auto isIface = [&](const Expr &x) {
                return x.type.isNamed() && reg_->isInterface(x.type.name);
            };
            const Expr *iface = nullptr;
            if (isIface(*e->left) && e->right->type.kind == TypeRef::Kind::Null)
                iface = e->left.get();
            else if (isIface(*e->right) && e->left->type.kind == TypeRef::Kind::Null)
                iface = e->right.get();
            if (iface)
                return "((" + emitExpr(*iface) + ").obj " + binaryOpSymbol(e->op) + " 0)";
        }
        std::string l = emitExpr(*e->left);
        std::string r = emitExpr(*e->right);
        if (e->stringConcat) return "grav_str_concat(" + l + ", " + r + ")";
        // String ==/!= compares contents (not pointers).
        if ((e->op == BinaryOp::Eq || e->op == BinaryOp::NotEq) &&
            e->left->type.kind == TypeRef::Kind::String &&
            e->right->type.kind == TypeRef::Kind::String)
            return "(strcmp(" + l + ", " + r + ") " + binaryOpSymbol(e->op) + " 0)";
        return "(" + l + " " + binaryOpSymbol(e->op) + " " + r + ")";
    }
    if (auto *e = dynamic_cast<const UnaryExpr *>(&expr)) {
        const char *op = e->op == UnaryOp::BitNot ? "~" : "!";
        return std::string("(") + op + emitExpr(*e->operand) + ")";
    }
    if (auto *e = dynamic_cast<const TernaryExpr *>(&expr)) {
        return "(" + emitExpr(*e->cond) + " ? " +
               emitAs(*e->thenExpr, e->type) + " : " +
               emitAs(*e->elseExpr, e->type) + ")";
    }
    if (auto *e = dynamic_cast<const AsExpr *>(&expr)) {
        // Pointer casts spell out the C pointer type explicitly.
        if (e->target.isPointer())
            return "((" + cTy(e->target) + ")(" + emitExpr(*e->operand) + "))";
        // Named targets reuse the upcast/box logic; primitives do a C cast.
        if (e->target.isNamed()) return emitAs(*e->operand, e->target);
        std::string inner = emitExpr(*e->operand);
        return emitPrimCast(e->target, *e->operand, inner);
    }
    if (auto *e = dynamic_cast<const IsExpr *>(&expr)) {
        return "grav_is_instance(" + emitExpr(*e->operand) + ", &" +
               mangle(e->className) + "_typeinfo)";
    }
    if (auto *e = dynamic_cast<const AwaitExpr *>(&expr)) {
        // Futures resolve eagerly, so the awaited value is already computed:
        // `await e` lowers to `e` itself.
        return emitExpr(*e->operand);
    }
    if (auto *e = dynamic_cast<const AddrOfExpr *>(&expr)) {
        return "(&" + emitExpr(*e->operand) + ")";
    }
    if (auto *e = dynamic_cast<const DerefExpr *>(&expr)) {
        return "(*" + emitExpr(*e->operand) + ")";
    }
    if (auto *e = dynamic_cast<const CoalesceExpr *>(&expr)) {
        // `a ?? b` -> "is a null? then b else a". The left is re-evaluated for the
        // test (keep `??` operands side-effect free).
        std::string l = emitExpr(*e->left);
        std::string guard = (e->left->type.isNamed() && reg_->isInterface(e->left->type.name))
                                ? "(" + l + ").obj != 0"
                                : l + " != 0";
        return "(" + guard + " ? " + emitAs(*e->left, e->type) + " : " +
               emitAs(*e->right, e->type) + ")";
    }
    if (auto *e = dynamic_cast<const IncDecExpr *>(&expr)) {
        std::string op = e->isIncrement ? "++" : "--";
        std::string t = emitExpr(*e->target);
        return e->isPrefix ? ("(" + op + t + ")") : ("(" + t + op + ")");
    }
    if (auto *e = dynamic_cast<const CastExpr *>(&expr)) {
        std::string inner = emitExpr(*e->operand);
        return emitPrimCast(e->target, *e->operand, inner);
    }
    if (auto *e = dynamic_cast<const SizeofExpr *>(&expr)) {
        if (e->isType) return "sizeof(" + sizeofSpelling(e->target) + ")";
        return "sizeof(" + emitExpr(*e->operand) + ")";
    }
    if (auto *e = dynamic_cast<const ArrayLiteralExpr *>(&expr)) {
        // `[a, b, c]` -> a compound literal of the backing array struct.
        std::string out = "(" + cTy(e->type) + "){ { ";
        const TypeRef elem = e->type.elem ? *e->type.elem : TypeRef::prim(TypeRef::Kind::Int);
        for (size_t i = 0; i < e->elements.size(); ++i) {
            if (i) out += ", ";
            out += emitAs(*e->elements[i], elem);
        }
        return out + " } }";
    }
    if (auto *e = dynamic_cast<const IndexExpr *>(&expr)) {
        std::string base = emitExpr(*e->base);
        std::string idx = emitExpr(*e->index);
        // Fixed arrays index through their `.data` member; slices and pointers
        // index directly.
        if (e->base->type.isArray() && !e->base->type.isSlice())
            return "(" + base + ").data[" + idx + "]";
        return "(" + base + ")[" + idx + "]";
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
            out += "." + memberCName(e->fields[i].name) + " = " + v;
        }
        return out + " }";
    }
    if (auto *e = dynamic_cast<const CallExpr *>(&expr)) return emitCall(*e);
    if (auto *e = dynamic_cast<const MemberExpr *>(&expr)) {
        // `EnumType.Member` lowers to the C enum constant.
        if (e->kind == MemberKind::EnumValue)
            return enumConst(e->qualified, e->member);
        // `Class.staticField` lowers to the static global's C name.
        if (e->kind == MemberKind::StaticField)
            return e->qualified;
        // `slice.length` is the runtime count passed alongside the pointer.
        if (e->object->type.isSlice() && e->member == "length")
            return emitExpr(*e->object) + "__n";
        // `arr.length` is the array's fixed length, a compile-time constant.
        if (e->object->type.isArray() && e->member == "length")
            return std::to_string(e->object->type.arrayLen);
        // `bytes.length` is the runtime byte count stored alongside the buffer.
        if (e->object->type.isBinary() && e->member == "length")
            return "(int)((" + emitExpr(*e->object) + ").len)";
        // Field read: structs are values (`.`), class instances are pointers (`->`).
        bool valueObj = e->object->type.isNamed() && reg_->isStruct(e->object->type.name);
        std::string obj = emitExpr(*e->object);
        std::string access = "(" + obj + (valueObj ? ")." : ")->") + memberCName(e->member);
        // `a?.field` guards against a null object, yielding a zero value when null.
        if (e->optional) {
            std::string nul = e->type.kind == TypeRef::Kind::String ? "\"\"" : "0";
            return "(" + obj + " != 0 ? " + access + " : " + nul + ")";
        }
        return access;
    }
    return "/*?*/ 0";
}

std::string CodeGen::hoistIncludes(const std::string &code) const {
    std::string kept;
    size_t i = 0, n = code.size();
    while (i < n) {
        size_t eol = code.find('\n', i);
        if (eol == std::string::npos) eol = n;
        std::string ln = code.substr(i, eol - i);
        size_t s = 0;
        while (s < ln.size() && (ln[s] == ' ' || ln[s] == '\t')) s++;
        bool directive = ln.compare(s, 8, "#include") == 0 || ln.compare(s, 7, "#pragma") == 0;
        if (directive) {
            hoisted_ += ln.substr(s) + "\n";
        } else {
            kept += ln;
            if (eol < n) kept += "\n";
        }
        i = (eol < n) ? eol + 1 : n;
    }
    return kept;
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
            if (call.targetName == "free")
                return "free((void*)(" + emitExpr(*call.args[0]) + "))";
            if (call.targetName == "exit") return "exit(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "panic")
                return "grav_panic(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "assert") {
                std::string cond = emitExpr(*call.args[0]);
                std::string msg = call.args.size() == 2 ? emitExpr(*call.args[1])
                                                        : ("\"" + escapeC(cond) + "\"");
                return "grav_assert(" + cond + ", " + msg + ")";
            }
            if (call.targetName == "cwd") return "grav_cwd()";
            if (call.targetName == "env") return "grav_env(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "input") return "grav_input()";
            if (call.targetName == "argc") return "grav_argc";
            if (call.targetName == "argv")
                return "grav_argv_at(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "str") {
                std::string a = emitExpr(*call.args[0]);
                const TypeRef &t = call.args[0]->type;
                switch (t.kind) {
                    case TypeRef::Kind::String: return a;
                    case TypeRef::Kind::Int:
                        return t.isUnsigned
                                   ? "grav_uint_to_str((unsigned long long)(" + a + "))"
                                   : "grav_int_to_str((long long)(" + a + "))";
                    case TypeRef::Kind::Float: return "grav_float_to_str(" + a + ")";
                    case TypeRef::Kind::Bool:
                        return "((" + a + ") ? \"true\" : \"false\")";
                    case TypeRef::Kind::Binary: return "grav_bytes_to_str(" + a + ")";
                    default:
                        if (t.isNamed() && reg_->isEnum(t.name))
                            return "grav_int_to_str((long long)(" + a + "))";
                        return "\"\"";
                }
            }
            // print
            std::string a = call.args.empty() ? "0" : emitExpr(*call.args[0]);
            TypeRef t = call.args.empty() ? TypeRef::prim(TypeRef::Kind::Int)
                                          : call.args[0]->type;
            switch (t.kind) {
                case TypeRef::Kind::Int:
                    return t.isUnsigned
                               ? "printf(\"%llu\\n\", (unsigned long long)(" + a + "))"
                               : "printf(\"%lld\\n\", (long long)(" + a + "))";
                case TypeRef::Kind::Float: return "printf(\"%f\\n\", " + a + ")";
                case TypeRef::Kind::Bool:
                    return "printf(\"%s\\n\", (" + a + ") ? \"true\" : \"false\")";
                case TypeRef::Kind::String: return "printf(\"%s\\n\", " + a + ")";
                case TypeRef::Kind::Binary:
                    return "printf(\"%s\\n\", grav_bytes_to_str(" + a + "))";
                default: return "printf(\"\\n\")";
            }
        }
        case CallKind::FreeFunction: {
            const auto &set = reg_->funcOverloads(call.targetName);
            const FunctionInfo *fi = reg_->func(call.targetName);
            // Use the exact overload chosen during type checking.
            for (const auto &cand : set)
                if (cand.overloadIndex == call.resolvedOverload) { fi = &cand; break; }
            std::vector<TypeRef> params = fi ? fi->paramTypes : std::vector<TypeRef>{};
            std::string out = funcCName(call.targetName, call.resolvedOverload) + "(";
            if (fi && fi->isVariadic) {
                emitVariadicArgs(out, call, params);
            } else {
                emitArgs(out, call, params, false, "");
            }
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
            std::string out;
            if (call.ifaceDispatch) {
                std::string itab = mangle(call.ownerClass) + "_ITAB";
                std::vector<TypeRef> params;
                if (const MethodInfo *mi =
                        reg_->findInterfaceMethod(call.ownerClass, call.methodName))
                    params = mi->paramTypes;
                std::string disp = "((" + itab + "*)(" + obj + ").itab)->" +
                                   memberCName(call.methodName);
                std::string self = "(" + obj + ").obj";
                out = disp + "(";
                emitArgs(out, call, params, true, self);
                out += ")";
            } else {
                std::string vt = vtableType(call.slotOwner);
                std::string self = "(" + structName(call.slotOwner) + "*)(" + obj + ")";
                std::string disp = "((" + vt + "*)((struct GravObject*)(" + obj +
                                   "))->__vt)->" + memberCName(call.methodName);
                std::vector<TypeRef> params;
                if (const MethodInfo *mi = reg_->findMethod(call.ownerClass, call.methodName))
                    params = mi->paramTypes;
                out = disp + "(";
                emitArgs(out, call, params, true, self);
                out += ")";
            }
            // `a?.m(...)` guards the dispatch against a null receiver.
            if (mem->optional) {
                bool iface = mem->object->type.isNamed() &&
                             reg_->isInterface(mem->object->type.name);
                std::string guard = iface ? "(" + obj + ").obj != 0" : obj + " != 0";
                if (call.type.isVoid())
                    return "(" + guard + " ? (" + out + ", 0) : 0)";
                std::string nul = call.type.kind == TypeRef::Kind::String ? "\"\"" : "0";
                return "(" + guard + " ? " + out + " : " + nul + ")";
            }
            return out;
        }
        case CallKind::SuperConstructor: {
            const ClassInfo *pc = reg_->cls(call.ownerClass);
            if (!pc || !pc->constructor) return "((void)0)"; // parent has no ctor body
            std::vector<TypeRef> params;
            for (const auto &p : pc->constructor->params) params.push_back(p.type);
            std::string self = "(" + structName(call.ownerClass) + "*)self";
            std::string out = ctorInitCName(call.ownerClass) + "(";
            emitArgs(out, call, params, true, self);
            return out + ")";
        }
        case CallKind::SuperMethod: {
            std::vector<TypeRef> params;
            if (const MethodInfo *mi = reg_->findMethod(call.ownerClass, call.methodName))
                params = mi->paramTypes;
            std::string self = "(" + structName(call.ownerClass) + "*)self";
            std::string out = methodCName(call.ownerClass, call.methodName) + "(";
            emitArgs(out, call, params, true, self);
            return out + ")";
        }
        default:
            return "/*unresolved call*/ 0";
    }
}

} // namespace grav
