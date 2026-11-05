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
            if (fn->isAsync) emitAsyncSpawner(*fn);
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
    const FieldInfo *af = m.accessor == AccessorKind::Delegate ? nullptr : reg_->findField(classFq, m.accessorField);
    bool weak = af && af->isWeak;
    if (m.accessor == AccessorKind::Getter) {
        std::string f = "self->" + memberCName(m.accessorField);
        if (weak) f = "((" + cTy(m.returnType) + ")grav_weak_load((void*)" + f + "))";
        defs_ += "    return " + retainOf(m.returnType, f) + ";\n"; // results are the caller's
    } else if (m.accessor == AccessorKind::Setter) {
        std::string f = "self->" + memberCName(m.accessorField);
        const TypeRef &vt = m.paramTypes.empty() ? m.returnType : m.paramTypes[0];
        if (weak)
            defs_ += "    { void* __old = (void*)" + f + "; grav_weak_retain((void*)value); " + f +
                     " = value; grav_weak_release(__old); }\n";
        else if (refKind(vt) != RefKind::None)
            defs_ += "    { " + cTy(vt) + " __old = " + f + "; " + f + " = " + retainOf(vt, "value") + "; " +
                     releaseOf(vt, "__old") + " }\n";
        else
            defs_ += "    " + f + " = value;\n";
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
        } else if (auto *t = dynamic_cast<const TestDecl *>(declPtr.get()); t && testMode_) {
            std::string fn = "grav_test_" + std::to_string(tests_.size());
            tests_.push_back({t->name, fn});
            defs_ += lineDirective(t->line);
            defs_ += "static void " + fn + "(void) {\n";
            cur_ = &defs_;
            indent_ = 1;
            currentReturnType_ = TypeRef::prim(TypeRef::Kind::Void);
            beginBody(t->body);
            emitEnter("test \\\"" + escapeC(t->name) + "\\\"", t->line);
            emitBlock(t->body);
            if (trace_) line("grav_leave();");
            indent_ = 0;
            endFunction();
        }
    }
}

void CodeGen::emitEnter(const std::string &displayName, int declLine) {
    if (!trace_) return;
    line("grav_enter(\"" + displayName + "\", \"" + escapeC(origin(declLine).file) + "\");");
}

void CodeGen::emitFunction(const FunctionDecl &fn) {
    if (fn.fqName == "main") hasMain_ = true;
    defs_ += lineDirective(fn.line);
    defs_ += "static " + cTy(fn.returnType) + " " + funcCName(fn.fqName, fn.overloadIndex) +
             paramList(fn.params, "") + " {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = fn.returnType;
    beginBody(fn.body);
    emitEnter(fn.fqName, fn.line);
    ownAssignedParams(fn.body, fn.params);
    emitBlock(fn.body);
    emitBodyEndCleanups();
    if (trace_) line("grav_leave();");
    indent_ = 0;
    endFunction();
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
        defs_ += lineDirective(ctor->line);
        defs_ += "static void " + ctorInitCName(cls.fqName, idx) + paramList(params, S) + " {\n";
        cur_ = &defs_;
        indent_ = 1;
        currentReturnType_ = TypeRef::prim(TypeRef::Kind::Void);
        beginBody(ctor->body);
        emitEnter(cls.fqName + ".constructor", ctor->line);
        ownAssignedParams(ctor->body, params);
        emitBlock(ctor->body);
        emitBodyEndCleanups();
        if (trace_) line("grav_leave();");
        indent_ = 0;
        endFunction();
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
    defs_ += "    self->__rc = 1;\n"; // the creator's reference
    for (const auto &f : collectFields(cls.fqName))
        defs_ += stringDefaults("self->" + memberCName(f.name), f.type);

    if (ctor) {
        // A constructor that throws frees the half-built object (see grav_uw_unbuilt).
        defs_ += "    grav_uw_push((void*)&self, grav_uw_unbuilt);\n";
        defs_ += "    " + ctorInitCName(cls.fqName, idx) + "(self";
        for (const auto &p : params) defs_ += ", " + p.name;
        defs_ += ");\n";
        defs_ += "    grav_uw_top--;\n";
    }
    defs_ += "    return self;\n}\n\n";
}

// Zeroed memory holds null pointers, but a string is never null: string fields
// (also those inside struct and fixed-array fields) start out empty.
std::string CodeGen::stringDefaults(const std::string &lv, const TypeRef &t, int depth) const {
    if (t.kind == TypeRef::Kind::String) return "    " + lv + " = \"\";\n";
    if (t.isArray() && !t.isSlice() && t.elem) {
        std::string i = "__si" + std::to_string(depth);
        std::string inner = stringDefaults(lv + ".data[" + i + "]", *t.elem, depth + 1);
        if (inner.empty()) return "";
        return "    for (int " + i + " = 0; " + i + " < " + std::to_string(t.arrayLen) + "; ++" + i +
               ") {\n    " + inner + "    }\n";
    }
    std::string out;
    if (t.isNamed())
        if (const StructInfo *si = reg_->strct(t.name))
            for (const auto &f : si->fields)
                out += stringDefaults(lv + "." + memberCName(f.name), f.type, depth);
    return out;
}

void CodeGen::emitMethod(const ClassDecl &cls, const MethodDecl &m) {
    std::string S = structName(cls.fqName);
    std::string self = m.isStatic ? "" : S;
    defs_ += lineDirective(m.line);
    defs_ += "static " + cTy(m.returnType) + " " + methodCName(cls.fqName, m.name) +
             paramList(m.params, self) + " {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = m.returnType;
    beginBody(m.body);
    emitEnter(cls.fqName + "." + m.name, m.line);
    ownAssignedParams(m.body, m.params);
    emitBlock(m.body);
    emitBodyEndCleanups();
    if (trace_) line("grav_leave();");
    indent_ = 0;
    endFunction();
}

// `destructor() { ... }` -> `Class__drop(self)`: the body, then the nearest base
// destructor, so `free(obj)` tears down most-derived first.
void CodeGen::emitDestructor(const ClassDecl &cls) {
    if (!cls.destructor.present) return;
    std::string S = structName(cls.fqName);
    defs_ += lineDirective(cls.destructor.line);
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

// `async fn f(a: A) -> R` keeps its plain C body `vf_f(a)`; callers use
// `vf_f__spawn(a)`, which copies the arguments into a task record, queues it on
// the worker pool, and returns the GravTask handle that `await` waits on.
void CodeGen::emitAsyncSpawner(const FunctionDecl &fn) {
    std::string base = funcCName(fn.fqName, fn.overloadIndex);
    std::string rec = "GravAsync_" + base;
    bool isVoid = fn.returnType.isVoid();
    std::string fields = "GravTask task;";
    for (const auto &p : fn.params) fields += " " + cTy(p.type) + " " + p.name + ";";
    if (!isVoid) fields += " " + cTy(fn.returnType) + " result;";
    asyncDecls_ += "typedef struct " + rec + " { " + fields + " } " + rec + ";\n";
    std::string args;
    for (const auto &p : fn.params) args += (args.empty() ? "r->" : ", r->") + p.name;
    asyncDecls_ += "static void " + base + "__run(GravTask* t) {\n    " + rec + "* r = (" + rec +
                   "*)t;\n    (void)r;\n    " + (isVoid ? "" : "r->result = ") + base + "(" + args +
                   ");\n}\n";
    std::string sets;
    // The task keeps its reference arguments alive (it may outlive the caller's).
    for (const auto &p : fn.params) sets += " r->" + p.name + " = " + retainOf(p.type, p.name) + ";";
    asyncDecls_ += "static GravTask* " + base + "__spawn" + paramList(fn.params, "") + " {\n    " + rec +
                   "* r = (" + rec + "*)grav_zalloc(sizeof *r);" + sets + "\n    return grav_spawn(&r->task, " +
                   base + "__run, " + (isVoid ? "0" : "&r->result") + ");\n}\n";
}

void CodeGen::emitMainWrapper() {
    if (testMode_) {
        // The test runner: each test runs in isolation; a failed assert, panic,
        // or uncaught exception fails that test and the runner moves on.
        defs_ += "static const char* const grav_test_names[] = { ";
        for (size_t i = 0; i < tests_.size(); ++i)
            defs_ += (i ? ", \"" : "\"") + escapeC(tests_[i].first) + "\"";
        if (tests_.empty()) defs_ += "0";
        defs_ += " };\nstatic void (*const grav_test_fns[])(void) = { ";
        for (size_t i = 0; i < tests_.size(); ++i) defs_ += (i ? ", " : "") + tests_[i].second;
        if (tests_.empty()) defs_ += "0";
        defs_ += " };\n";
        defs_ += "int main(int argc, char** argv) {\n";
        defs_ += "    grav_argc = argc; grav_argv = argv;\n";
        defs_ += "    grav_install_faults();\n";
        defs_ += "    int rc = grav_run_tests(grav_test_names, grav_test_fns, " +
                 std::to_string(tests_.size()) + ");\n";
        if (!asyncDecls_.empty()) defs_ += "    grav_join_all();\n";
        defs_ += "    return rc;\n}\n";
        return;
    }
    defs_ += "int main(int argc, char** argv) {\n";
    defs_ += "    grav_argc = argc; grav_argv = argv;\n";
    defs_ += "    grav_install_faults();\n";
    if (hasMain_) defs_ += "    " + funcCName("main") + "();\n";
    if (!asyncDecls_.empty()) defs_ += "    grav_join_all(); /* finish tasks that were never awaited */\n";
    defs_ += "    return 0;\n}\n";
}

} // namespace grav
