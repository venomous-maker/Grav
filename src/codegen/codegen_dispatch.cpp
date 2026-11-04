// Method dispatch (devirtualized direct calls and single-evaluation thunks) and
// string building (fusing `+` chains / interpolation into one allocation, and
// printing them without allocating at all).
#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

namespace {

// Zero value returned by a null-safe thunk when the receiver is null.
std::string zeroValue(const TypeRef &t, const std::string &cty) {
    if (t.kind == TypeRef::Kind::String) return "\"\"";
    // Values spelled as C structs (structs, arrays, interfaces, binary) need a
    // compound literal; everything else is a scalar or pointer.
    if (cty.rfind("struct ", 0) == 0 || t.isArray() || t.isBinary() ||
        cty.find("__") != std::string::npos || cty.rfind("Grav", 0) == 0)
        return "(" + cty + "){0}";
    return "0";
}

} // namespace

std::string CodeGen::methodArgs(const std::vector<ExprPtr> &args,
                                const std::vector<TypeRef> &params) const {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out += ", ";
        out += i < params.size() ? emitStable(*args[i], params[i]) : emitExpr(*args[i]);
    }
    return out;
}

std::string CodeGen::methodCall(const std::string &obj, const TypeRef &recv,
                                const std::string &method, const std::string &args,
                                bool nullSafe) const {
    const std::string tail = args.empty() ? "" : ", " + args;
    const bool iface = recv.isNamed() && reg_->isInterface(recv.name);

    // Pieces of the thunk: name, receiver C type, the dispatch expression (over
    // `self` and the parameters a0..aN), and the signature it implements.
    std::string name, selfTy, callee, selfArg, castRecv;
    const MethodInfo *mi = nullptr;
    if (iface) {
        mi = reg_->findInterfaceMethod(recv.name, method);
        name = mangle(recv.name) + "__icall__" + memberCName(method);
        selfTy = "struct GravIface";
        callee = "((const " + mangle(recv.name) + "_ITAB*)self.itab)->" + memberCName(method);
        selfArg = "self.obj";
        castRecv = obj;
    } else {
        const ClassInfo *ci = reg_->cls(recv.name);
        mi = reg_->findMethod(recv.name, method);
        if (!reg_->isOverridden(recv.name, method)) {
            // No subclass overrides it: a direct call the C compiler can inline.
            std::string impl = reg_->findMethodImpl(recv.name, method);
            std::string direct = methodCName(impl, method);
            std::string self = "(" + structName(impl) + "*)(" + obj + ")";
            if (!nullSafe) return direct + "(" + self + tail + ")";
            name = mangle(impl) + "__ocall__" + memberCName(method);
            selfTy = structName(impl) + "*";
            callee = direct;
            selfArg = "self";
            castRecv = self;
        } else {
            std::string owner = recv.name;
            if (ci)
                for (const auto &s : ci->slots)
                    if (s.name == method) { owner = s.slotOwner; break; }
            if (const MethodInfo *om = reg_->findMethod(owner, method)) mi = om;
            name = mangle(owner) + "__vcall__" + memberCName(method);
            selfTy = structName(owner) + "*";
            callee = "((const " + vtableType(owner) + "*)self->__vt)->" + memberCName(method);
            selfArg = "self";
            castRecv = "(" + structName(owner) + "*)(" + obj + ")";
        }
    }
    if (nullSafe) name += "__opt";

    if (!thunks_.count(name) && mi) {
        std::string ret = cTy(mi->returnType);
        std::string params = selfTy + " self", fwd = selfArg;
        for (size_t i = 0; i < mi->paramTypes.size(); ++i) {
            std::string a = "a" + std::to_string(i);
            params += ", " + cTy(mi->paramTypes[i]) + " " + a;
            fwd += ", " + a;
        }
        bool isVoid = mi->returnType.isVoid();
        std::string def = "static inline " + ret + " " + name + "(" + params + ") {\n";
        if (nullSafe) {
            std::string isNull = iface ? "!self.obj" : "!self";
            def += "    if (" + isNull + ") return" +
                   (isVoid ? std::string("") : " " + zeroValue(mi->returnType, ret)) + ";\n";
        }
        def += std::string("    ") + (isVoid ? "" : "return ") + callee + "(" + fwd + ");\n}\n";
        thunks_[name] = def;
    }
    return name + "(" + castRecv + tail + ")";
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

std::string CodeGen::fmtEscape(const std::string &s) {
    std::string e = escapeC(s), out;
    for (char c : e) {
        out += c;
        if (c == '%') out += '%';
    }
    return out;
}

std::string CodeGen::enumNameFn(const std::string &enumFq) const {
    return mangle(enumFq) + "__name";
}

void CodeGen::flattenString(const Expr &e, std::vector<FmtPart> &parts) const {
    if (auto *b = dynamic_cast<const BinaryExpr *>(&e);
        b && b->stringConcat && b->overloadMethod.empty()) {
        flattenString(*b->left, parts);
        flattenString(*b->right, parts);
        return;
    }
    if (auto *lit = dynamic_cast<const StringLiteralExpr *>(&e)) {
        if (!lit->value.empty()) parts.push_back({true, lit->value, ""});
        return;
    }
    // `str(x)` (also what `${x}` desugars to): format the value in place
    // instead of allocating a temporary string for it.
    if (auto *c = dynamic_cast<const CallExpr *>(&e);
        c && c->kind == CallKind::Builtin && c->targetName == "str" && c->args.size() == 1) {
        const Expr &a = *c->args[0];
        const TypeRef &t = a.type;
        std::string v = emitExpr(a);
        switch (t.kind) {
            case TypeRef::Kind::String: flattenString(a, parts); return;
            case TypeRef::Kind::Int:
                if (t.isUnsigned) parts.push_back({false, "(unsigned long long)(" + v + ")", "%llu"});
                else parts.push_back({false, "(long long)(" + v + ")", "%lld"});
                return;
            case TypeRef::Kind::Float:
                parts.push_back({false, (t.numBits() == 32 ? "grav_ftoa(" : "grav_dtoa(") + v + ", (char[32]){0})", "%s"});
                return;
            case TypeRef::Kind::Bool:
                parts.push_back({false, "((" + v + ") ? \"true\" : \"false\")", "%s"});
                return;
            default:
                parts.push_back({false, emitToString(a), "%s"});
                return;
        }
    }
    parts.push_back({false, emitExpr(e), "%s"});
}

std::string CodeGen::emitStringExpr(const Expr &e) const {
    auto *b = dynamic_cast<const BinaryExpr *>(&e);
    if (!b || !b->stringConcat || !b->overloadMethod.empty()) return emitExpr(e);

    std::vector<FmtPart> raw, parts;
    flattenString(e, raw);
    for (auto &p : raw) { // merge adjacent literal text
        if (p.literal && !parts.empty() && parts.back().literal) parts.back().text += p.text;
        else parts.push_back(std::move(p));
    }
    return concatParts(parts);
}

std::string CodeGen::concatParts(const std::vector<FmtPart> &parts) const {
    if (parts.empty()) return "\"\"";
    bool allStrings = true;
    for (const auto &p : parts)
        if (!p.literal && p.conv != "%s") allStrings = false;

    if (allStrings) {
        auto arg = [&](const FmtPart &p) {
            return p.literal ? "\"" + escapeC(p.text) + "\"" : p.text;
        };
        if (parts.size() == 1) return arg(parts[0]);
        if (parts.size() == 2)
            return "grav_str_concat(" + arg(parts[0]) + ", " + arg(parts[1]) + ")";
        std::string out = "grav_concatn(" + std::to_string(parts.size());
        for (const auto &p : parts) out += ", " + arg(p);
        return out + ")";
    }
    std::string fmt, args;
    for (const auto &p : parts) {
        if (p.literal) { fmt += fmtEscape(p.text); continue; }
        fmt += p.conv;
        args += ", " + p.text;
    }
    return "grav_fmt(\"" + fmt + "\"" + args + ")";
}

// The text of a value as a borrowed string: built strings go on the release list.
std::string CodeGen::emitToString(const Expr &e) const {
    const TypeRef &t = e.type;
    auto ar = [](const std::string &s) { return "grav_str_autorelease(" + s + ")"; };
    switch (t.kind) {
        case TypeRef::Kind::String: return isRefProducer(e) && dynamic_cast<const BinaryExpr *>(&e) ? ar(emitStringExpr(e)) : emitStringExpr(e);
        case TypeRef::Kind::Int:
            return ar(t.isUnsigned ? "grav_uint_to_str((unsigned long long)(" + emitExpr(e) + "))"
                                   : "grav_int_to_str((long long)(" + emitExpr(e) + "))");
        case TypeRef::Kind::Float:
            return ar((t.numBits() == 32 ? "grav_float32_to_str(" : "grav_float_to_str(") + emitExpr(e) + ")");
        case TypeRef::Kind::Bool: return "((" + emitExpr(e) + ") ? \"true\" : \"false\")";
        case TypeRef::Kind::Binary: return ar("grav_bytes_to_str(" + emitExpr(e) + ")");
        case TypeRef::Kind::Any: return ar("grav_any_to_string(" + emitExpr(e) + ")");
        default: break;
    }
    if (t.isNamed() && reg_->isEnum(t.name)) return enumNameFn(t.name) + "(" + emitExpr(e) + ")";
    // Objects (null prints as null), structs, arrays, functions, pointers.
    return ar(displayC(emitExpr(e), t, false));
}

std::string CodeGen::emitPrint(const Expr &arg) const {
    const TypeRef &t = arg.type;
    std::string a;
    switch (t.kind) {
        case TypeRef::Kind::Int:
            a = emitExpr(arg);
            return t.isUnsigned ? "printf(\"%llu\\n\", (unsigned long long)(" + a + "))"
                                : "printf(\"%lld\\n\", (long long)(" + a + "))";
        case TypeRef::Kind::Float:
            return std::string("puts(") + (t.numBits() == 32 ? "grav_ftoa(" : "grav_dtoa(") + emitExpr(arg) + ", (char[32]){0}))";
        case TypeRef::Kind::Bool:
            return "puts((" + emitExpr(arg) + ") ? \"true\" : \"false\")";
        case TypeRef::Kind::String: {
            // Print the pieces straight to stdout: no intermediate string at all.
            std::vector<FmtPart> raw, parts;
            flattenString(arg, raw);
            for (auto &p : raw) {
                if (p.literal && !parts.empty() && parts.back().literal) parts.back().text += p.text;
                else parts.push_back(std::move(p));
            }
            if (parts.empty()) return "puts(\"\")";
            if (parts.size() == 1)
                return parts[0].literal ? "puts(\"" + escapeC(parts[0].text) + "\")"
                       : parts[0].conv == "%s" ? "puts(" + parts[0].text + ")"
                                               : "printf(\"" + parts[0].conv + "\\n\", " +
                                                     parts[0].text + ")";
            std::string fmt, args;
            for (const auto &p : parts) {
                if (p.literal) { fmt += fmtEscape(p.text); continue; }
                fmt += p.conv;
                args += ", " + p.text;
            }
            return "printf(\"" + fmt + "\\n\"" + args + ")";
        }
        default:
            return "puts(" + emitToString(arg) + ")";
    }
}

} // namespace grav
