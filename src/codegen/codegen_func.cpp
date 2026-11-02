#include "codegen/codegen.h"

#include "codegen/mangle.h"
#include "sema/sanitize.h"

namespace grav {

std::string CodeGen::paramList(const std::vector<Param> &params,
                               const std::string &selfStruct) const {
    std::vector<std::string> parts;
    if (!selfStruct.empty()) parts.push_back(selfStruct + "* self");
    for (const auto &p : params) {
        // A variadic parameter lowers to a length + element pointer pair.
        if (p.variadic) {
            parts.push_back("int " + p.name + "__n");
            parts.push_back(cTy(p.type) + "* " + p.name);
        } else {
            parts.push_back(cTy(p.type) + " " + p.name);
        }
    }
    if (parts.empty()) return "(void)";
    std::string out = "(";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ", ";
        out += parts[i];
    }
    return out + ")";
}

void CodeGen::emitPrototypes(const Program &program) {
    for (const auto &declPtr : program.decls) {
        // Everything lives in one translation unit, so every generated function is
        // `static`: the C compiler may then inline, specialize, or drop it freely.
        if (auto *fn = dynamic_cast<const FunctionDecl *>(declPtr.get())) {
            protos_ += "static " + cTy(fn->returnType) + " " +
                       funcCName(fn->fqName, fn->overloadIndex) + paramList(fn->params, "") + ";\n";
        } else if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            std::string S = structName(c->fqName);
            if (!c->constructor.present)
                protos_ += "static " + S + "* " + ctorCName(c->fqName) + "(void);\n";
            int ci = 0;
            auto ctorProto = [&](const ConstructorDecl &ctor) {
                protos_ += "static " + S + "* " + ctorCName(c->fqName, ci) +
                           paramList(ctor.params, "") + ";\n";
                protos_ += "static void " + ctorInitCName(c->fqName, ci) +
                           paramList(ctor.params, S) + ";\n";
                ci++;
            };
            if (c->constructor.present) ctorProto(c->constructor);
            for (const auto &x : c->extraCtors) ctorProto(x);
            if (c->destructor.present)
                protos_ += "static void " + mangle(c->fqName) + "__drop(" + S + "* self);\n";
            for (const auto &m : c->methods) {
                if (!m.hasBody) continue;
                std::string self = m.isStatic ? "" : S;
                protos_ += "static " + cTy(m.returnType) + " " +
                           methodCName(c->fqName, m.name) +
                           paramList(m.params, self) + ";\n";
            }
            if (const ClassInfo *ci = reg_->cls(c->fqName)) {
                for (const auto &m : ci->methods) {
                    if (m.accessor == AccessorKind::None) continue;
                    protos_ += "static " + accessorSig(c->fqName, m) + ";\n";
                }
            }
        }
    }
    protos_ += "\n";
}

std::string CodeGen::accessorSig(const std::string &classFq,
                                 const MethodInfo &m) const {
    std::string S = structName(classFq);
    std::string sig = cTy(m.returnType) + " " + methodCName(classFq, m.name) + "(" + S + "* self";
    if (m.accessor == AccessorKind::Setter)
        sig += ", " + cTy(m.paramTypes[0]) + " value";
    else if (m.accessor == AccessorKind::Delegate)
        for (size_t i = 0; i < m.paramTypes.size(); ++i)
            sig += ", " + cTy(m.paramTypes[i]) + " " + safeLocalName(m.paramNames[i]);
    return sig + ")";
}

void CodeGen::emitAccessorBody(const std::string &classFq, const MethodInfo &m) {
    defs_ += "static " + accessorSig(classFq, m) + " {\n";
    if (m.accessor == AccessorKind::Getter) {
        defs_ += "    return self->" + memberCName(m.accessorField) + ";\n";
    } else if (m.accessor == AccessorKind::Setter) {
        defs_ += "    self->" + memberCName(m.accessorField) + " = value;\n";
    } else { // Delegate: forward to self->field's method via its vtable
        std::string slotOwner = m.delegateClass;
        if (const ClassInfo *ac = reg_->cls(m.delegateClass))
            for (const auto &s : ac->slots)
                if (s.name == m.name) { slotOwner = s.slotOwner; break; }
        std::string obj = "self->" + memberCName(m.accessorField);
        std::string disp = "((" + vtableType(slotOwner) + "*)((struct GravObject*)(" +
                           obj + "))->__vt)->" + memberCName(m.name);
        std::string args = "(" + structName(slotOwner) + "*)(" + obj + ")";
        for (const auto &p : m.paramNames) args += ", " + safeLocalName(p);
        defs_ += (m.returnType.isVoid() ? "    " : "    return ") + disp + "(" + args + ");\n";
    }
    defs_ += "}\n\n";
}

void CodeGen::emitDefinitions(const Program &program) {
    for (const auto &declPtr : program.decls) {
        if (auto *fn = dynamic_cast<const FunctionDecl *>(declPtr.get())) {
            emitFunction(*fn);
        } else if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            emitClassBodies(*c);
        }
    }
}

void CodeGen::emitEnter(const std::string &displayName, int declLine) {
    if (!trace_) return;
    line("grav_enter(\"" + displayName + "\", \"" + escapeC(origin(declLine).file) + "\");");
}

void CodeGen::emitFunction(const FunctionDecl &fn) {
    if (fn.fqName == "main") hasMain_ = true;
    defs_ += "static " + cTy(fn.returnType) + " " + funcCName(fn.fqName, fn.overloadIndex) +
             paramList(fn.params, "") + " {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = fn.returnType;
    std::vector<std::string> pnames;
    for (const auto &p : fn.params) pnames.push_back(p.name);
    beginBody(fn.body, pnames);
    emitEnter(fn.fqName, fn.line);
    emitBlock(fn.body);
    if (trace_) line("grav_leave();");
    indent_ = 0;
    defs_ += "}\n\n";
}

void CodeGen::emitClassBodies(const ClassDecl &cls) {
    emitConstructor(cls);
    emitDestructor(cls);
    for (const auto &m : cls.methods) {
        if (m.hasBody) emitMethod(cls, m);
    }
    if (const ClassInfo *ci = reg_->cls(cls.fqName)) {
        for (const auto &m : ci->methods) {
            if (m.accessor != AccessorKind::None) emitAccessorBody(cls.fqName, m);
        }
    }
}

void CodeGen::emitConstructor(const ClassDecl &cls) {
    if (!cls.constructor.present) {
        emitConstructorAt(cls, nullptr, 0);
        return;
    }
    emitConstructorAt(cls, &cls.constructor, 0);
    for (size_t i = 0; i < cls.extraCtors.size(); ++i)
        emitConstructorAt(cls, &cls.extraCtors[i], static_cast<int>(i + 1));
}

void CodeGen::emitConstructorAt(const ClassDecl &cls, const ConstructorDecl *ctor, int idx) {
    std::string S = structName(cls.fqName);
    const std::vector<Param> noParams;
    const auto &params = ctor ? ctor->params : noParams;

    // The constructor body runs as a separate `_init(self, ...)` so a subclass can
    // delegate to it through `super(...)` without re-allocating the object.
    if (ctor) {
        defs_ += "static void " + ctorInitCName(cls.fqName, idx) + paramList(params, S) + " {\n";
        cur_ = &defs_;
        indent_ = 1;
        currentReturnType_ = TypeRef::prim(TypeRef::Kind::Void);
        std::vector<std::string> pnames;
        for (const auto &p : params) pnames.push_back(p.name);
        beginBody(ctor->body, pnames);
        emitEnter(cls.fqName + ".constructor", ctor->line);
        emitBlock(ctor->body);
        if (trace_) line("grav_leave();");
        indent_ = 0;
        defs_ += "}\n\n";
    }

    // `_new(...)` allocates, installs the vtable / type info, then runs `_init`.
    defs_ += "static " + S + "* " + ctorCName(cls.fqName, idx) + paramList(params, "") + " {\n";
    defs_ += "    " + S + "* self = (" + S + "*)grav_zalloc(sizeof(" + S + "));\n";

    const ClassInfo *ci = reg_->cls(cls.fqName);
    if (ci && !ci->slots.empty()) {
        defs_ += "    self->__vt = &" + vtableInstance(cls.fqName) + ";\n";
    } else {
        defs_ += "    self->__vt = 0;\n";
    }
    defs_ += "    self->__type = &" + mangle(cls.fqName) + "_typeinfo;\n";

    if (ctor) {
        defs_ += "    " + ctorInitCName(cls.fqName, idx) + "(self";
        for (const auto &p : params) defs_ += ", " + p.name;
        defs_ += ");\n";
    }
    defs_ += "    return self;\n}\n\n";
}

void CodeGen::emitMethod(const ClassDecl &cls, const MethodDecl &m) {
    std::string S = structName(cls.fqName);
    std::string self = m.isStatic ? "" : S;
    defs_ += "static " + cTy(m.returnType) + " " + methodCName(cls.fqName, m.name) +
             paramList(m.params, self) + " {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = m.returnType;
    std::vector<std::string> pnames;
    for (const auto &p : m.params) pnames.push_back(p.name);
    beginBody(m.body, pnames);
    emitEnter(cls.fqName + "." + m.name, m.line);
    emitBlock(m.body);
    if (trace_) line("grav_leave();");
    indent_ = 0;
    defs_ += "}\n\n";
}

// `destructor() { ... }` -> `Class__drop(self)`: the body, then the nearest base
// destructor, so `free(obj)` tears down most-derived first.
void CodeGen::emitDestructor(const ClassDecl &cls) {
    if (!cls.destructor.present) return;
    std::string S = structName(cls.fqName);
    defs_ += "static void " + mangle(cls.fqName) + "__drop(" + S + "* self) {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = TypeRef::prim(TypeRef::Kind::Void);
    beginBody(cls.destructor.body);
    emitEnter(cls.fqName + ".destructor", cls.destructor.line);
    emitBlock(cls.destructor.body);
    if (trace_) line("grav_leave();");
    if (const ClassInfo *ci = reg_->cls(cls.fqName); ci && !ci->baseClass.empty()) {
        std::string baseDtor = reg_->findDestructor(ci->baseClass);
        if (!baseDtor.empty())
            line(mangle(baseDtor) + "__drop((" + structName(baseDtor) + "*)self);");
    }
    indent_ = 0;
    defs_ += "}\n\n";
}

void CodeGen::emitMainWrapper() {
    defs_ += "int main(int argc, char** argv) {\n";
    defs_ += "    grav_argc = argc; grav_argv = argv;\n";
    if (hasMain_) defs_ += "    " + funcCName("main") + "();\n";
    defs_ += "    return 0;\n}\n";
}

} // namespace grav
