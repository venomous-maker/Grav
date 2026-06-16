#include "codegen/codegen.h"

#include "codegen/mangle.h"

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
        if (auto *fn = dynamic_cast<const FunctionDecl *>(declPtr.get())) {
            protos_ += cTy(fn->returnType) + " " + funcCName(fn->fqName) +
                       paramList(fn->params, "") + ";\n";
        } else if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            std::string S = structName(c->fqName);
            const auto &ctorParams = c->constructor.present
                                         ? c->constructor.params
                                         : std::vector<Param>{};
            protos_ += S + "* " + ctorCName(c->fqName) +
                       paramList(ctorParams, "") + ";\n";
            for (const auto &m : c->methods) {
                if (!m.hasBody) continue;
                std::string self = m.isStatic ? "" : S;
                protos_ += cTy(m.returnType) + " " +
                           methodCName(c->fqName, m.name) +
                           paramList(m.params, self) + ";\n";
            }
            if (const ClassInfo *ci = reg_->cls(c->fqName)) {
                for (const auto &m : ci->methods) {
                    if (m.accessor == AccessorKind::None) continue;
                    protos_ += accessorSig(c->fqName, m) + ";\n";
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
    return sig + ")";
}

void CodeGen::emitAccessorBody(const std::string &classFq, const MethodInfo &m) {
    defs_ += accessorSig(classFq, m) + " {\n";
    if (m.accessor == AccessorKind::Getter)
        defs_ += "    return self->" + m.accessorField + ";\n";
    else
        defs_ += "    self->" + m.accessorField + " = value;\n";
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

void CodeGen::emitFunction(const FunctionDecl &fn) {
    if (fn.fqName == "main") hasMain_ = true;
    defs_ += cTy(fn.returnType) + " " + funcCName(fn.fqName) +
             paramList(fn.params, "") + " {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = fn.returnType;
    emitBlock(fn.body);
    indent_ = 0;
    defs_ += "}\n\n";
}

void CodeGen::emitClassBodies(const ClassDecl &cls) {
    emitConstructor(cls);
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
    std::string S = structName(cls.fqName);
    const auto &params = cls.constructor.present ? cls.constructor.params
                                                : std::vector<Param>{};
    defs_ += S + "* " + ctorCName(cls.fqName) + paramList(params, "") + " {\n";
    defs_ += "    " + S + "* self = (" + S + "*)calloc(1, sizeof(" + S + "));\n";

    const ClassInfo *ci = reg_->cls(cls.fqName);
    if (ci && !ci->slots.empty()) {
        defs_ += "    self->__vt = &" + vtableInstance(cls.fqName) + ";\n";
    } else {
        defs_ += "    self->__vt = 0;\n";
    }
    defs_ += "    self->__type = &" + mangle(cls.fqName) + "_typeinfo;\n";

    if (cls.constructor.present) {
        cur_ = &defs_;
        indent_ = 1;
        currentReturnType_ = TypeRef::prim(TypeRef::Kind::Void);
        emitBlock(cls.constructor.body);
        indent_ = 0;
    }
    defs_ += "    return self;\n}\n\n";
}

void CodeGen::emitMethod(const ClassDecl &cls, const MethodDecl &m) {
    std::string S = structName(cls.fqName);
    std::string self = m.isStatic ? "" : S;
    defs_ += cTy(m.returnType) + " " + methodCName(cls.fqName, m.name) +
             paramList(m.params, self) + " {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = m.returnType;
    emitBlock(m.body);
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
