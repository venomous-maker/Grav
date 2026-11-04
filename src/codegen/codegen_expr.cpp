#include "codegen/codegen.h"

#include <cstdio>

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
            default:
                // Other control bytes (e.g. from `\0`/`\x01`) as fixed 3-digit octal,
                // which cannot run into a following digit the way `\x` escapes do.
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\%03o", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string CodeGen::boxAny(const Expr &e) const {
    const TypeRef &t = e.type;
    std::string v = emitExpr(e);
    switch (t.kind) {
        case TypeRef::Kind::Any: return v;
        case TypeRef::Kind::Null: return "grav_any_null()";
        case TypeRef::Kind::Int:
            return t.isUnsigned ? "grav_any_uint((unsigned long long)(" + v + "))"
                                : "grav_any_int((long long)(" + v + "))";
        case TypeRef::Kind::Float: return "grav_any_float((double)(" + v + "))";
        case TypeRef::Kind::Bool: return "grav_any_bool(" + v + ")";
        case TypeRef::Kind::String: return "grav_any_str(" + v + ")";
        case TypeRef::Kind::Function:
            return "grav_any_fn(" + v + ", \"" + escapeC(typeRefName(t)) + "\")";
        default: break;
    }
    if (t.isNamed() && reg_->isEnum(t.name))
        return "grav_any_enum((long long)(" + v + "), \"" + t.name + "\", " + enumNameFn(t.name) + "_any)";
    if (t.isNamed() && reg_->isInterface(t.name)) return "grav_any_obj((" + v + ").obj)";
    return "grav_any_obj((void*)(" + v + "))";
}

std::string CodeGen::unboxAny(const std::string &c, const TypeRef &t) const {
    switch (t.kind) {
        case TypeRef::Kind::Any: return c;
        case TypeRef::Kind::Int:
            return "((" + cTy(t) + ")" + (t.isUnsigned ? "grav_any_to_uint(" : "grav_any_to_int(") + c + "))";
        case TypeRef::Kind::Float: return "((" + cTy(t) + ")grav_any_to_float(" + c + "))";
        case TypeRef::Kind::Bool: return "grav_any_to_bool(" + c + ")";
        case TypeRef::Kind::String: return "grav_any_to_str(" + c + ")";
        case TypeRef::Kind::Function:
            return "grav_any_to_fn(" + c + ", \"" + escapeC(typeRefName(t)) + "\")";
        default: break;
    }
    if (t.isNamed() && reg_->isEnum(t.name))
        return "((" + cTy(t) + ")grav_any_to_enum(" + c + ", \"" + t.name + "\"))";
    return "((" + cTy(t) + ")grav_any_to_obj(" + c + ", &" + mangle(t.name) + "_typeinfo))";
}

std::string CodeGen::isAnyTest(const std::string &c, const TypeRef &t) const {
    switch (t.kind) {
        case TypeRef::Kind::Any: return "1";
        case TypeRef::Kind::Int: return "grav_any_is_int(" + c + ")";
        case TypeRef::Kind::Float: return "grav_any_is_float(" + c + ")";
        case TypeRef::Kind::Bool: return "grav_any_is_bool(" + c + ")";
        case TypeRef::Kind::String: return "grav_any_is_str(" + c + ")";
        case TypeRef::Kind::Function:
            return "grav_any_is_fn(" + c + ", \"" + escapeC(typeRefName(t)) + "\")";
        default: break;
    }
    if (t.isNamed() && reg_->isEnum(t.name)) return "grav_any_is_enum(" + c + ", \"" + t.name + "\")";
    return "grav_any_is_obj(" + c + ", &" + mangle(t.name) + "_typeinfo)";
}

std::string CodeGen::emitAs(const Expr &expr, const TypeRef &target) const {
    // Values flowing into an `any` slot are boxed.
    if (target.isAny() && !expr.type.isAny()) return boxAny(expr);
    std::string e = emitExpr(expr);
    // A null function value.
    if (target.isFunction() && expr.type.kind == TypeRef::Kind::Null)
        return "((GravFn){ 0, 0 })";
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
        if (i < params.size()) out += emitStable(*call.args[i], params[i]);
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
        out += emitStable(*call.args[i], params[i]);
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
            out += emitStable(*call.args[i], elem);
        }
        out += " }";
    }
}

// Every reference-typed call / `new` result evaluated here is borrowed: it goes
// on the release list (freed when the statement ends) unless emitOwned() is
// taking it over.
std::string CodeGen::emitExpr(const Expr &expr) const {
    if (!exprOverride_.empty())
        if (auto it = exprOverride_.find(&expr); it != exprOverride_.end()) return it->second;
    std::string out = emitExprRaw(expr);
    if (isRefProducer(expr)) {
        if (ownedRoot_ == &expr) { ownedRoot_ = nullptr; return out; }
        return autoreleaseOf(expr.type, out);
    }
    return out;
}

std::string CodeGen::emitExprRaw(const Expr &expr) const {
    if (auto *e = dynamic_cast<const IntLiteralExpr *>(&expr)) return e->raw;
    if (auto *e = dynamic_cast<const FloatLiteralExpr *>(&expr)) return e->raw;
    if (auto *e = dynamic_cast<const BoolLiteralExpr *>(&expr))
        return e->value ? "true" : "false";
    if (auto *e = dynamic_cast<const StringLiteralExpr *>(&expr))
        return "\"" + escapeC(e->value) + "\"";
    if (dynamic_cast<const NullLiteralExpr *>(&expr)) return "0";
    if (auto *e = dynamic_cast<const CBlockExpr *>(&expr)) return hoistIncludes(e->code);
    if (auto *e = dynamic_cast<const NameExpr *>(&expr)) {
        if (!e->fnRef.empty()) return emitFnRef(*e);
        return e->resolvedGlobal.empty() ? e->name : e->resolvedGlobal;
    }
    if (auto *e = dynamic_cast<const LambdaExpr *>(&expr)) return emitLambdaValue(*e);
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
            // `x == null` on an any tests for an empty box.
            const Expr *anyv = nullptr;
            if (e->left->type.isAny() && e->right->type.kind == TypeRef::Kind::Null) anyv = e->left.get();
            else if (e->right->type.isAny() && e->left->type.kind == TypeRef::Kind::Null) anyv = e->right.get();
            if (anyv)
                return "((" + emitExpr(*anyv) + ").tag " + binaryOpSymbol(e->op) + " GRAV_ANY_NULL)";
            // `any == value`: box the other side and compare the boxed values.
            if ((e->left->type.isAny() || e->right->type.isAny()) && e->overloadMethod.empty())
                return std::string(e->op == BinaryOp::Eq ? "" : "!") + "grav_any_eq(" +
                       boxAny(*e->left) + ", " + boxAny(*e->right) + ")";
            // Function values compare their code pointer against null.
            const Expr *fnv = nullptr;
            if (e->left->type.isFunction() && e->right->type.kind == TypeRef::Kind::Null)
                fnv = e->left.get();
            else if (e->right->type.isFunction() && e->left->type.kind == TypeRef::Kind::Null)
                fnv = e->right.get();
            if (fnv)
                return "((" + emitExpr(*fnv) + ").fn " + binaryOpSymbol(e->op) + " 0)";
        }
        // A class operator method: `a + b` -> a.op_add(b) (receiver evaluated once).
        if (!e->overloadMethod.empty()) {
            std::vector<TypeRef> params;
            if (const MethodInfo *mi = reg_->findMethod(e->overloadClass, e->overloadMethod))
                params = mi->paramTypes;
            std::string arg = params.empty() ? emitExpr(*e->right) : emitAs(*e->right, params[0]);
            std::string call = methodCall(emitExpr(*e->left), e->left->type, e->overloadMethod, arg);
            return e->overloadNegate ? "(!" + call + ")" : call;
        }
        // `+` chains on strings fuse into one allocation (see emitStringExpr).
        if (e->stringConcat) return emitStringExpr(*e);
        std::string l = emitExpr(*e->left);
        std::string r = emitExpr(*e->right);
        // String ==/!= compares contents (not pointers).
        if ((e->op == BinaryOp::Eq || e->op == BinaryOp::NotEq) &&
            e->left->type.kind == TypeRef::Kind::String &&
            e->right->type.kind == TypeRef::Kind::String)
        {
            // A freshly built operand (`a + b == c`) is compared, then released.
            bool fl = isFreshString(*e->left), fr = isFreshString(*e->right);
            std::string neg = e->op == BinaryOp::Eq ? "" : "!";
            if (fl || fr)
                return neg + "grav_streq_tmp(" + l + ", " + r + ", " + (fl ? "1" : "0") + ", " +
                       (fr ? "1" : "0") + ")";
            return neg + "grav_streq(" + l + ", " + r + ")";
        }
        // Value equality for kinds C cannot compare with `==`.
        if ((e->op == BinaryOp::Eq || e->op == BinaryOp::NotEq) && e->left->type == e->right->type) {
            const TypeRef &lt = e->left->type;
            bool special = lt.isFunction() || lt.isBinary() || (lt.isArray() && !lt.isSlice()) ||
                           (lt.isNamed() && (reg_->isInterface(lt.name) || reg_->isStruct(lt.name)));
            if (special) {
                if (lt.isArray()) { // needs addressable operands: compare through temps
                    std::string ty = cTy(lt);
                    return std::string(e->op == BinaryOp::Eq ? "" : "!") +
                           "(memcmp((" + ty + "[]){" + l + "}, (" + ty + "[]){" + r + "}, sizeof(" +
                           ty + ")) == 0)";
                }
                return std::string(e->op == BinaryOp::Eq ? "" : "!") + equalExpr(lt, l, r);
            }
        }
        if (e->op == BinaryOp::Pow) {
            if (e->type.kind == TypeRef::Kind::Float)
                return "((" + cTy(e->type) + ")pow((double)(" + l + "), (double)(" + r + ")))";
            return "((" + cTy(e->type) + ")grav_ipow((long long)(" + l + "), (long long)(" + r + ")))";
        }
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
        // Into / out of `any`: box, or a checked unbox (panics on the wrong type).
        if (e->target.isAny()) return boxAny(*e->operand);
        if (e->operand->type.isAny()) return unboxAny(emitExpr(*e->operand), e->target);
        // Pointer casts spell out the C pointer type explicitly.
        if (e->target.isPointer())
            return "((" + cTy(e->target) + ")(" + emitExpr(*e->operand) + "))";
        // Named targets reuse the upcast/box logic; primitives do a C cast.
        if (e->target.isNamed() && reg_->isEnum(e->target.name))
            return "((" + cTy(e->target) + ")(" + emitExpr(*e->operand) + "))";
        if (e->target.isNamed()) return emitAs(*e->operand, e->target);
        std::string inner = emitExpr(*e->operand);
        return emitPrimCast(e->target, *e->operand, inner);
    }
    if (auto *e = dynamic_cast<const IsExpr *>(&expr)) {
        if (e->operand->type.isAny()) return isAnyTest(emitExpr(*e->operand), e->target);
        return "grav_is_instance(" + emitExpr(*e->operand) + ", &" +
               mangle(e->className) + "_typeinfo)";
    }
    if (auto *e = dynamic_cast<const AwaitExpr *>(&expr)) {
        // `await f`: wait for the task (re-throwing its exception) and read the result.
        if (e->type.isVoid()) return "((void)grav_await(" + emitExpr(*e->operand) + "))";
        return "(*(" + cTy(e->type) + "*)grav_await(" + emitExpr(*e->operand) + "))";
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
            out += emitOwned(*e->elements[i], elem); // the literal owns its elements
        }
        return out + " } }";
    }
    if (auto *e = dynamic_cast<const IndexExpr *>(&expr)) {
        // `obj[k]` on a class with `operator[]` -> obj.op_index(k).
        if (!e->overloadClass.empty()) {
            std::vector<TypeRef> params;
            if (const MethodInfo *mi = reg_->findMethod(e->overloadClass, e->overloadMethod))
                params = mi->paramTypes;
            std::string arg = params.empty() ? emitExpr(*e->index) : emitAs(*e->index, params[0]);
            return methodCall(emitExpr(*e->base), e->base->type, e->overloadMethod, arg);
        }
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
            if (e->ctorIndex < static_cast<int>(ci->ctors.size()))
                for (const auto &p : ci->ctors[e->ctorIndex]->params) params.push_back(p.type);
        std::string out = ctorCName(e->className, e->ctorIndex) + "(";
        for (size_t i = 0; i < e->args.size(); ++i) {
            if (i) out += ", ";
            out += (i < params.size()) ? emitStable(*e->args[i], params[i])
                                       : emitExpr(*e->args[i]);
        }
        out += ")";
        // A data enum variant is typed as its enum: `Shape.Circle(1.0)` is a Shape.
        if (e->type.isNamed() && e->type.name != e->className)
            out = "((" + cType(e->type) + ")" + out + ")";
        return out;
    }
    if (auto *e = dynamic_cast<const StructLiteralExpr *>(&expr)) {
        std::string out = "(" + structName(e->typeName) + "){ ";
        if (e->fields.empty()) return out + "0 }"; // initializes the __empty member
        for (size_t i = 0; i < e->fields.size(); ++i) {
            if (i) out += ", ";
            const FieldInfo *f = reg_->findStructField(e->typeName, e->fields[i].name);
            // The literal owns its reference fields (K__release gives them back).
            std::string v = f ? emitOwned(*e->fields[i].value, f->type)
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
        // A weak field reads as the object (held until the statement ends) or null.
        if (!valueObj && isWeakField(*e))
            access = "((" + cTy(e->type) + ")grav_weak_load((void*)" + access + "))";
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
                if (call.args[0]->type.isAny()) return "grav_any_typename(" + emitExpr(*call.args[0]) + ")";
                return "grav_typename(" + emitExpr(*call.args[0]) + ")";
            }
            if (call.targetName == "isInstance") {
                return "grav_is_instance(" + emitExpr(*call.args[0]) + ", &" +
                       mangle(call.ownerClass) + "_typeinfo)";
            }
            if (call.targetName == "free") {
                // Class instances run their destructor chain before release.
                const TypeRef &ft = call.args[0]->type;
                if (ft.isNamed() && reg_->isClass(ft.name))
                    return "grav_free_obj((void*)(" + emitExpr(*call.args[0]) + "))";
                if (ft.isFuture()) // waits for the task, then releases its record
                    return "grav_task_free(" + emitExpr(*call.args[0]) + ")";
                if (ft.isFunction() || ft.kind == TypeRef::Kind::String) // counted: released with their last reference
                    return "((void)(" + emitExpr(*call.args[0]) + "))";
                return "free((void*)(" + emitExpr(*call.args[0]) + "))";
            }
            if (call.targetName == "exit") return "exit(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "panic")
                return "grav_panic(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "assert") {
                std::string cond = emitExpr(*call.args[0]);
                SourceMapEntry o = origin(call.line);
                std::string loc = "\"" + escapeC(o.file) + ":" + std::to_string(o.line) + "\"";
                std::string msg = call.args.size() == 2 ? emitExpr(*call.args[1]) : ("\"" + escapeC(cond) + "\"");
                return "grav_assert_at(" + cond + ", " + msg + ", " + loc + ")";
            }
            if (call.targetName == "__retain") return retainOf(call.args[0]->type, emitExpr(*call.args[0]));
            if (call.targetName == "__visit") { // report a buffer element to the cycle collector
                std::string v = visitOf(call.args[0]->type, emitExpr(*call.args[0]));
                return v.empty() ? "((void)0)" : "(" + v.substr(0, v.size() - 1) + ")";
            }
            if (call.targetName == "__release") {
                std::string r = releaseOf(call.args[0]->type, emitExpr(*call.args[0]));
                if (r.empty()) return "((void)0)";
                return "(" + r.substr(0, r.size() - 1) + ")"; // as an expression
            }
            if (call.targetName == "__show") {
                const TypeRef &t = call.args[0]->type;
                bool boxable = !(t.isNamed() && reg_->isStruct(t.name)) && !t.isArray() &&
                               !t.isBinary() && !t.isPointer();
                if (boxable) return "grav_any_to_string(" + boxAny(*call.args[0]) + ")";
                return "\"" + escapeC(prettyNames(typeRefName(t))) + "\"";
            }
            if (call.targetName == "assertEq") {
                SourceMapEntry o = origin(call.line);
                std::string loc = "\"" + escapeC(o.file) + ":" + std::to_string(o.line) + "\"";
                std::string msg = call.args.size() == 3 ? emitExpr(*call.args[2]) : "\"\"";
                const TypeRef &at = call.args[0]->type;
                const TypeRef &bt = call.args[1]->type;
                bool boxable = !(at.isNamed() && reg_->isStruct(at.name)) && !at.isArray() &&
                               !at.isBinary() && !at.isPointer() &&
                               !(bt.isNamed() && reg_->isStruct(bt.name)) && !bt.isArray() &&
                               !bt.isBinary() && !bt.isPointer();
                if (boxable)
                    return "grav_assert_eq(" + boxAny(*call.args[0]) + ", " + boxAny(*call.args[1]) +
                           ", " + msg + ", " + loc + ")";
                return "grav_assert_at(" + equalExpr(at, emitExpr(*call.args[0]), emitExpr(*call.args[1])) +
                       ", \"assertEq: the values differ\", " + loc + ")";
            }
            if (call.targetName == "cwd") return "grav_cwd()";
            if (call.targetName == "env") return "grav_env(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "input") return "grav_input()";
            if (call.targetName == "argc") return "grav_argc";
            if (call.targetName == "argv")
                return "grav_argv_at(" + emitExpr(*call.args[0]) + ")";
            if (call.targetName == "str") return emitToString(*call.args[0]);
            // print
            if (call.args.empty()) return "puts(\"\")";
            return emitPrint(*call.args[0]);
        }
        case CallKind::FreeFunction: {
            const auto &set = reg_->funcOverloads(call.targetName);
            const FunctionInfo *fi = reg_->func(call.targetName);
            // Use the exact overload chosen during type checking.
            for (const auto &cand : set)
                if (cand.overloadIndex == call.resolvedOverload) { fi = &cand; break; }
            std::vector<TypeRef> params = fi ? fi->paramTypes : std::vector<TypeRef>{};
            // An async function call queues a task and yields its future.
            std::string out = funcCName(call.targetName, call.resolvedOverload) +
                              (fi && fi->isAsync ? "__spawn(" : "(");
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
            TypeRef recv = mem->object->type;
            if (!(recv.isNamed() && (reg_->isClass(recv.name) || reg_->isInterface(recv.name))))
                recv = TypeRef::named(call.ownerClass);
            std::vector<TypeRef> params;
            const MethodInfo *mi = call.ifaceDispatch
                                       ? reg_->findInterfaceMethod(call.ownerClass, call.methodName)
                                       : reg_->findMethod(call.ownerClass, call.methodName);
            if (mi) params = mi->paramTypes;
            std::string args = methodArgs(call.args, params);
            // The receiver is held for the call too (a method may drop its own owner).
            std::string obj = mem->optional ? emitExpr(*mem->object) : emitStable(*mem->object, mem->object->type);
            if (!mem->optional) return methodCall(obj, recv, call.methodName, args);
            // `a?.m(...)`: a side-effect-free receiver keeps the cheap inline guard
            // (and skips evaluating the arguments); anything else goes through a
            // null-checking thunk so the receiver is evaluated once.
            bool simple = dynamic_cast<const NameExpr *>(mem->object.get()) ||
                          dynamic_cast<const SelfExpr *>(mem->object.get());
            if (!simple) return methodCall(obj, recv, call.methodName, args, /*nullSafe=*/true);
            bool iface = reg_->isInterface(recv.name);
            std::string guard = iface ? "(" + obj + ").obj != 0" : obj + " != 0";
            std::string out = methodCall(obj, recv, call.methodName, args);
            if (call.type.isVoid()) return "(" + guard + " ? (" + out + ", 0) : 0)";
            std::string nul = call.type.kind == TypeRef::Kind::String ? "\"\"" : "0";
            return "(" + guard + " ? " + out + " : " + nul + ")";
        }
        case CallKind::FnValue: return emitFnCall(call);
        case CallKind::SuperConstructor: {
            const ClassInfo *pc = reg_->cls(call.ownerClass);
            if (!pc || call.resolvedOverload >= static_cast<int>(pc->ctors.size()))
                return "((void)0)"; // parent has no ctor body
            std::vector<TypeRef> params;
            for (const auto &p : pc->ctors[call.resolvedOverload]->params) params.push_back(p.type);
            std::string self = "(" + structName(call.ownerClass) + "*)self";
            std::string out = ctorInitCName(call.ownerClass, call.resolvedOverload) + "(";
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
