// Closures and function values. A lambda is lifted to a top-level C function
// `grav_lambda_N(void* env, params...)`; its captured variables are copied into a
// heap `GravEnv_N` when the lambda is created and re-bound as locals (same names)
// at the top of the lifted body, so the body compiles exactly like any function.
#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

std::string CodeGen::fnSignature(const TypeRef &ft) const {
    std::string sig = cTy(ft.fnReturn()) + " (*)(void*";
    for (const auto &p : ft.fnParams) sig += ", " + cTy(p);
    return sig + ")";
}

void CodeGen::emitLambdas() {
    for (size_t i = 0; i < lambdas_.size(); ++i) {
        lambdas_[i]->liftId = static_cast<int>(i);
        emitLambda(*lambdas_[i], static_cast<int>(i));
    }
    if (!lambdaDecls_.empty()) lambdaDecls_ += "\n";
}

void CodeGen::emitLambda(const LambdaExpr &lam, int id) {
    std::string n = std::to_string(id);
    std::string fn = "grav_lambda_" + n, env = "GravEnv_" + n;
    const TypeRef &ret = lam.returnType;
    std::string params = "void* grav_envp";
    for (const auto &p : lam.params) params += ", " + cTy(p.type) + " " + p.name;
    std::string head = "static " + cTy(ret) + " " + fn + "(" + params + ")";

    // Declarations: the environment, the lifted function, and its constructor.
    if (!lam.captures.empty()) {
        // The environment is a counted object: same header as class instances, and
        // its type info releases the captured references when the count hits zero.
        lambdaDecls_ += "typedef struct " + env + " { const void* __vt; const GravTypeInfo* __type; long long __rc;";
        for (const auto &c : lam.captures) lambdaDecls_ += " " + cTy(c.type) + " " + c.name + ";";
        lambdaDecls_ += " } " + env + ";\n";
        std::string rel;
        for (const auto &c : lam.captures)
            if (refKind(c.type) != RefKind::None) rel += " " + releaseOf(c.type, "e->" + c.name);
        lambdaDecls_ += "static void " + env + "__arc_fields(void* o) { " + env + "* e = (" + env +
                        "*)o; (void)e;" + rel + " }\n";
        lambdaDecls_ += "static const GravTypeInfo " + env + "_typeinfo = { \"<closure>\", 0, 0, 0, 0, 0, 0, " +
                        env + "__arc_fields };\n";
    }
    lambdaDecls_ += head + ";\n";
    if (!lam.captures.empty()) {
        std::string ps, sets;
        for (const auto &c : lam.captures) {
            ps += (ps.empty() ? "" : ", ") + cTy(c.type) + " " + c.name;
            sets += " grav_e->" + c.name + " = " + retainOf(c.type, c.name) + ";";
        }
        lambdaDecls_ += "static GravFn grav_mk_lambda_" + n + "(" + ps + ") {\n    " + env +
                        "* grav_e = (" + env + "*)grav_alloc(sizeof *grav_e);" +
                        " grav_e->__vt = 0; grav_e->__type = &" + env + "_typeinfo; grav_e->__rc = 1;" + sets +
                        "\n    GravFn grav_f; grav_f.fn = (void (*)(void))" + fn +
                        "; grav_f.env = grav_e; return grav_f;\n}\n";
    }

    // The body.
    defs_ += lineDirective(lam.line);
    defs_ += head + " {\n";
    cur_ = &defs_;
    indent_ = 1;
    currentReturnType_ = ret;
    if (lam.captures.empty()) {
        line("(void)grav_envp;");
    } else {
        line(env + "* grav_env = (" + env + "*)grav_envp;");
        for (const auto &c : lam.captures)
            line(cTy(c.type) + " " + c.name + " = grav_env->" + c.name + ";");
    }
    if (lam.isArrow) {
        volatileNames_.clear();
        cleanups_.clear();
        ownedStrings_.clear();
        emitEnter("<lambda>", lam.line);
        if (trace_) emitLineMark(lam.line);
        if (ret.isVoid()) {
            line(emitExpr(*lam.exprBody) + ";");
            if (trace_) line("grav_leave();");
        } else if (trace_) {
            line(cTy(ret) + " grav_r = " + emitOwned(*lam.exprBody, ret) + ";");
            line("grav_leave();");
            line("return grav_r;");
        } else {
            line("return " + emitOwned(*lam.exprBody, ret) + ";");
        }
    } else {
        std::vector<std::string> outer;
        for (const auto &p : lam.params) outer.push_back(p.name);
        for (const auto &c : lam.captures) outer.push_back(c.name);
        beginBody(lam.body, outer);
        emitEnter("<lambda>", lam.line);
        emitBlock(lam.body);
        if (trace_) line("grav_leave();");
    }
    indent_ = 0;
    endFunction();
}

std::string CodeGen::emitLambdaValue(const LambdaExpr &lam) const {
    std::string n = std::to_string(lam.liftId);
    if (lam.captures.empty())
        return "((GravFn){ (void (*)(void))grav_lambda_" + n + ", 0 })";
    std::string args;
    for (const auto &c : lam.captures) args += (args.empty() ? "" : ", ") + c.name;
    return "grav_mk_lambda_" + n + "(" + args + ")";
}

std::string CodeGen::emitFnRef(const NameExpr &e) const {
    const FunctionInfo *fi = nullptr;
    for (const auto &c : reg_->funcOverloads(e.fnRef))
        if (c.overloadIndex == e.fnRefOverload) fi = &c;
    if (!fi) return "((GravFn){ 0, 0 })";
    std::string target = funcCName(e.fnRef, e.fnRefOverload);
    std::string name = "grav_fnref_" + target;
    if (!thunks_.count(name)) {
        std::string params = "void* env", fwd;
        for (size_t i = 0; i < fi->paramTypes.size(); ++i) {
            std::string a = "a" + std::to_string(i);
            params += ", " + cTy(fi->paramTypes[i]) + " " + a;
            fwd += (i ? ", " : "") + a;
        }
        thunks_[name] = "static " + cTy(fi->returnType) + " " + name + "(" + params +
                        ") {\n    (void)env;\n    " + (fi->returnType.isVoid() ? "" : "return ") +
                        target + "(" + fwd + ");\n}\n";
    }
    return "((GravFn){ (void (*)(void))" + name + ", 0 })";
}

std::string CodeGen::emitFnCall(const CallExpr &call) const {
    const TypeRef &ft = call.callee->type;
    std::string sig = fnSignature(ft);
    auto it = fnCallers_.find(sig);
    std::string name;
    if (it != fnCallers_.end()) {
        name = it->second;
    } else {
        name = "grav_call_" + std::to_string(fnCallers_.size());
        fnCallers_[sig] = name;
        std::string params = "GravFn grav_f", fwd = "grav_f.env";
        for (size_t i = 0; i < ft.fnParams.size(); ++i) {
            std::string a = "a" + std::to_string(i);
            params += ", " + cTy(ft.fnParams[i]) + " " + a;
            fwd += ", " + a;
        }
        std::string castFn = "((" + sig + ")grav_f.fn)";
        thunks_[name] = "static inline " + cTy(ft.fnReturn()) + " " + name + "(" + params +
                        ") {\n    if (GRAV_UNLIKELY(!grav_f.fn)) grav_panic(\"call through a null function value\");\n    " +
                        (ft.fnReturn().isVoid() ? "" : "return ") + castFn + "(" + fwd + ");\n}\n";
    }
    std::string out = name + "(" + emitExpr(*call.callee);
    for (size_t i = 0; i < call.args.size(); ++i)
        out += ", " + (i < ft.fnParams.size() ? emitStable(*call.args[i], ft.fnParams[i])
                                              : emitExpr(*call.args[i]));
    return out + ")";
}

} // namespace grav
