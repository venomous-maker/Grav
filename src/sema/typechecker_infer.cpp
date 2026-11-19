// Inference of generic type arguments. Generics are expanded before type
// checking, so a call such as `maxOf(1, 2)` or `new Box(5)` reaches the checker
// without an instance. The checker unifies the template's exported signature
// with the argument types (typing lambdas once the parameters they depend on
// are known), or with the expected type for `new Array<auto>()`, records the
// result, and the driver compiles again with the arguments filled in.
#include "sema/typechecker.h"

namespace grav {

namespace {

bool isParam(const TypeRef &t, const std::vector<std::string> &tps) {
    if (t.kind != TypeRef::Kind::Named || !t.args.empty()) return false;
    for (const auto &p : tps) if (p == t.name) return true;
    return false;
}

bool isAutoType(const TypeRef &t) { return t.kind == TypeRef::Kind::Named && t.name == "auto" && t.args.empty(); }
// `auto` anywhere inside (e.g. Future<auto>): the whole argument is inferred.
bool hasAutoInside(const TypeRef &t) {
    if (isAutoType(t)) return true;
    for (const auto &a : t.args) if (hasAutoInside(a)) return true;
    for (const auto &p : t.fnParams) if (hasAutoInside(p)) return true;
    return t.elem && hasAutoInside(*t.elem);
}

std::string lastSegment(const std::string &n) {
    auto d = n.rfind('.');
    return d == std::string::npos ? n : n.substr(d + 1);
}

} // namespace

// A call site: where it is and, since the copies of a generic class or function
// share their positions, in which declaration (`Box$int`, `first$string`).
std::string siteKey(const std::string &scope, int line, int col, const std::string &name) {
    return scope + "|" + std::to_string(line) + ":" + std::to_string(col) + ":" + name;
}

void TypeChecker::unify(const TypeRef &p, const TypeRef &a, const std::vector<std::string> &tps,
                        Subst &s) const {
    if (a.isError() || a.kind == TypeRef::Kind::Null) return;
    if (isParam(p, tps)) {
        if (!s.count(p.name)) s[p.name] = a;
        return;
    }
    // A list literal on its own is a fixed array (`int[3]`); passed for an
    // `Array<T>` it becomes one, so its elements give T: collect([1, 2]).
    if (p.kind == TypeRef::Kind::Named && p.args.size() == 1 && lastSegment(p.name) == "Array" &&
        a.isArray() && a.elem) {
        unify(p.args[0], *a.elem, tps, s);
        return;
    }
    if (p.kind == TypeRef::Kind::Named && !p.args.empty() && a.isNamed() && program_) {
        auto it = program_->generics.instances.find(a.name);
        if (it != program_->generics.instances.end() &&
            lastSegment(it->second.first) == lastSegment(p.name) &&
            it->second.second.size() == p.args.size()) {
            for (size_t i = 0; i < p.args.size(); ++i) unify(p.args[i], it->second.second[i], tps, s);
            return;
        }
        // A class passed where its interface or base is expected: `One<int>` for
        // `Container<T>` gives T = int through `One<T> implements Container<T>`.
        if (const ClassInfo *ci = reg_->cls(a.name)) {
            for (const auto &i : ci->interfaces) if (!i.empty()) unify(p, TypeRef::named(i), tps, s);
            if (!ci->baseClass.empty()) unify(p, TypeRef::named(ci->baseClass), tps, s);
        }
        return;
    }
    if (p.kind == a.kind && p.elem && a.elem &&
        (p.isFuture() || p.isPointer() || p.isArray()))
        unify(*p.elem, *a.elem, tps, s);
    // `T?` takes a `U?` (T = U) or a plain `U` (T = U).
    if (p.isNullable() && p.elem) {
        if (a.isNullable() && a.elem) unify(*p.elem, *a.elem, tps, s);
        else if (!a.isNullable()) unify(*p.elem, a, tps, s);
    }
    if (p.isFunction() && a.isFunction()) {
        for (size_t i = 0; i < p.fnParams.size() && i < a.fnParams.size(); ++i)
            unify(p.fnParams[i], a.fnParams[i], tps, s);
        if (p.elem && a.elem) unify(*p.elem, *a.elem, tps, s);
    }
}

TypeRef TypeChecker::substitute(const TypeRef &t, const Subst &s) const {
    if (t.kind == TypeRef::Kind::Named && t.args.empty()) {
        auto it = s.find(t.name);
        return it != s.end() ? it->second : t;
    }
    TypeRef r = t;
    for (auto &a : r.args) a = substitute(a, s);
    for (auto &p : r.fnParams) p = substitute(p, s);
    if (r.elem) r.elem = std::make_shared<TypeRef>(substitute(*r.elem, s));
    return r;
}

bool TypeChecker::mentions(const TypeRef &t, const std::vector<std::string> &tps, const Subst &s) const {
    if (isParam(t, tps)) return !s.count(t.name);
    for (const auto &a : t.args) if (mentions(a, tps, s)) return true;
    for (const auto &p : t.fnParams) if (mentions(p, tps, s)) return true;
    return t.elem && mentions(*t.elem, tps, s);
}

TypeRef TypeChecker::toSource(const TypeRef &t) const {
    if (t.isNamed() && t.args.empty() && program_) {
        auto it = program_->generics.instances.find(t.name);
        if (it != program_->generics.instances.end()) {
            TypeRef r = TypeRef::named(it->second.first);
            for (const auto &a : it->second.second) r.args.push_back(toSource(a));
            return r;
        }
    }
    TypeRef r = t;
    for (auto &a : r.args) a = toSource(a);
    for (auto &p : r.fnParams) p = toSource(p);
    if (r.elem) r.elem = std::make_shared<TypeRef>(toSource(*r.elem));
    return r;
}

TypeRef TypeChecker::toCanonical(const TypeRef &t) const {
    TypeRef r = t;
    for (auto &a : r.args) a = toCanonical(a);
    for (auto &p : r.fnParams) p = toCanonical(p);
    if (r.elem) r.elem = std::make_shared<TypeRef>(toCanonical(*r.elem));
    if (r.isNamed() && !r.args.empty() && program_) {
        for (const auto &[inst, origin] : program_->generics.instances)
            if (lastSegment(origin.first) == lastSegment(r.name) && origin.second == r.args)
                return TypeRef::named(inst);
        return TypeRef::prim(TypeRef::Kind::Error); // instantiated in the next pass
    }
    return r;
}

// Whether an expression's type is decided by where it is used: a generic
// `new` without type arguments, a collection literal, or a map literal.
static bool takesTypeFromContext(const Expr &e) {
    if (auto *n = as<const NewExpr>(&e)) return n->typeArgs.empty();
    if (as<const ArrayLiteralExpr>(&e)) return true;
    if (auto *c = as<const CallExpr>(&e))
        if (auto *n = as<const NameExpr>(c->callee.get())) return n->name == "mapOf" && c->typeArgs.empty();
    return false;
}

// `{"a": ..., "b": ...}`: a map literal whose keys are all string literals.
static bool stringKeyedMapLiteral(const Expr &e) {
    auto *c = as<const CallExpr>(&e);
    auto *n = c ? as<const NameExpr>(c->callee.get()) : nullptr;
    if (!n || n->name != "mapOf" || !c->typeArgs.empty() || c->args.empty()) return false;
    for (const auto &a : c->args) {
        auto *p = as<const NewExpr>(a.get());
        if (!p || p->args.size() != 2 || !as<const StringLiteralExpr>(p->args[0].get())) return false;
    }
    return true;
}

bool TypeChecker::inferArgs(const GenericSig &sig, std::vector<ExprPtr> &args,
                            const std::vector<TypeRef> &explicitArgs, const TypeRef *expected,
                            Subst &s, std::string &why) {
    for (size_t i = 0; i < explicitArgs.size() && i < sig.typeParams.size(); ++i)
        if (!hasAutoInside(explicitArgs[i])) s[sig.typeParams[i]] = explicitArgs[i];
    if (expected && !expected->isError()) unify(sig.ret, *expected, sig.typeParams, s);
    bool arityOk = sig.variadic ? args.size() + 1 >= sig.params.size() : args.size() == sig.params.size();
    if (!arityOk) {
        why = "expects " + std::to_string(sig.params.size()) + " argument(s), got " +
              std::to_string(args.size());
        return false;
    }
    auto paramFor = [&](size_t i) -> const TypeRef & {
        return i < sig.params.size() ? sig.params[i] : sig.params.back();
    };
    // Plain arguments first; lambdas whose parameters are untyped wait until the
    // type parameters they depend on are known.
    std::vector<size_t> later;
    for (size_t i = 0; i < args.size(); ++i) {
        auto *lam = as<LambdaExpr>(args[i].get());
        bool untyped = false;
        if (lam) for (bool b : lam->paramTyped) if (!b) untyped = true;
        if (untyped) { later.push_back(i); continue; }
        // An argument whose type comes from context (`new Pair(k, v)`, `[...]`,
        // a nested `{...}`) takes the parameter's type once it is fully known
        // (from the call's expected type): {"a": "x"} as HashMap<string, any>.
        TypeRef at;
        TypeRef known = substitute(paramFor(i), s);
        if (!mentions(paramFor(i), sig.typeParams, s) && takesTypeFromContext(*args[i])) {
            // A string-keyed map literal held as `any` (a nested {...} in JSON-like
            // data) is a HashMap<string, any> all the way down.
            if (known.isAny() && stringKeyedMapLiteral(*args[i])) {
                TypeRef m = TypeRef::named("std.collections.HashMap");
                m.args.push_back(TypeRef::prim(TypeRef::Kind::String));
                m.args.push_back(TypeRef::prim(TypeRef::Kind::Any));
                known = m;
            }
            at = checkExprExpect(*args[i], toCanonical(known));
        }
        else at = checkExpr(*args[i]);
        unify(paramFor(i), at, sig.typeParams, s);
    }
    for (size_t i : later) {
        TypeRef p = substitute(paramFor(i), s);
        if (!p.isFunction()) continue;
        for (const auto &fp : p.fnParams)
            if (mentions(fp, sig.typeParams, s)) {
                why = "cannot infer the lambda's parameter types";
                return false;
            }
        // An unknown result type is inferred from the lambda body.
        if (p.elem && mentions(*p.elem, sig.typeParams, s))
            p.elem = std::make_shared<TypeRef>(TypeRef::prim(TypeRef::Kind::Error));
        TypeRef lt = checkExprExpect(*args[i], p);
        unify(paramFor(i), lt, sig.typeParams, s);
    }
    for (const auto &tp : sig.typeParams)
        if (!s.count(tp)) {
            why = "cannot infer type argument '" + tp + "'";
            return false;
        }
    return true;
}

std::optional<TypeRef> TypeChecker::recordInference(int line, int col, const std::string &name,
                                                    const std::vector<std::string> &tps,
                                                    const Subst &s, const TypeRef &ret) {
    std::vector<TypeRef> args;
    for (const auto &tp : tps) args.push_back(toSource(s.at(tp)));
    std::string key = siteKey(currentClass_.empty() ? currentFunction_ : currentClass_, line, col, name);
    if (!infer_) {
        error(line, col, "cannot infer type arguments here; write them explicitly with ::<...>");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    auto it = infer_->entries.find(key);
    if (it != infer_->entries.end()) {
        // Already applied once and still not resolved: report instead of looping.
        error(line, col, "could not instantiate '" + name + "' with the inferred type arguments");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    infer_->entries[key] = args;
    infer_->added++;
    return toCanonical(substitute(ret, s));
}

std::optional<TypeRef> TypeChecker::tryInferCall(CallExpr &e, const std::string &name,
                                                 const std::vector<GenericSig> &sigs,
                                                 const TypeRef *expected) {
    // A string-keyed {...} held as `any` (an argument of type any, say) is a
    // HashMap<string, any>, so its nested values ([...], {...}) are typed too.
    TypeRef asMap;
    if (expected && expected->isAny() && stringKeyedMapLiteral(e)) {
        TypeRef m = TypeRef::named("std.collections.HashMap");
        m.args.push_back(TypeRef::prim(TypeRef::Kind::String));
        m.args.push_back(TypeRef::prim(TypeRef::Kind::Any));
        asMap = toCanonical(m);
        expected = &asMap;
    }
    std::vector<ParamList> pls;
    for (const auto &sig : sigs) pls.push_back(paramList(sig));
    if (completeArgs(e.args, pls, e.line, e.col, "'" + name + "'") == -2) return TypeRef::prim(TypeRef::Kind::Error);
    std::string lastWhy = "no matching signature";
    for (const auto &sig : sigs) {
        Subst s;
        std::string why;
        if (!inferArgs(sig, e.args, e.typeArgs, expected, s, why)) { lastWhy = why; continue; }
        e.kind = CallKind::Builtin; // placeholder until the next pass instantiates it
        return recordInference(e.line, e.col, name, sig.typeParams, s, sig.ret);
    }
    error(e.line, e.col, "cannot infer the type arguments of '" + name + "': " + lastWhy +
                             "; write them explicitly, e.g. " + name + "::<int>(...)");
    return TypeRef::prim(TypeRef::Kind::Error);
}

std::optional<TypeRef> TypeChecker::tryInferNew(NewExpr &e, const TypeRef *expected) {
    if (!program_) return std::nullopt;
    auto it = program_->generics.ctors.end();
    for (const auto &cand : nameCandidates(e.className, currentNs_, program_->uses))
        if ((it = program_->generics.ctors.find(cand)) != program_->generics.ctors.end()) break;
    if (it == program_->generics.ctors.end()) return std::nullopt;
    std::vector<ParamList> pls;
    for (const auto &sig : it->second) pls.push_back(paramList(sig));
    if (completeArgs(e.args, pls, e.line, e.col, "the constructor of '" + e.className + "'") == -2)
        return TypeRef::prim(TypeRef::Kind::Error);
    std::string lastWhy = "no constructor matches the arguments";
    for (const auto &sig : it->second) {
        Subst s;
        std::string why;
        if (!inferArgs(sig, e.args, e.typeArgs, expected, s, why)) { lastWhy = why; continue; }
        return recordInference(e.line, e.col, e.className, sig.typeParams, s, sig.ret);
    }
    error(e.line, e.col, "cannot infer the type arguments of 'new " + e.className + "': " + lastWhy +
                             "; write them explicitly, e.g. new " + e.className + "<int>(...)");
    return TypeRef::prim(TypeRef::Kind::Error);
}

void applyInferences(Program &program, const InferenceTable &table) {
    if (table.entries.empty()) return;
    for (auto &d : program.decls) {
        std::string scope;
        if (auto *fn = as<FunctionDecl>(d.get())) scope = fn->fqName;
        else if (auto *c = as<ClassDecl>(d.get())) scope = c->fqName;
        visitDeclExprs(*d, [&](Expr &e) {
            if (auto *c = as<CallExpr>(&e)) {
                std::string name;
                if (auto *n = as<NameExpr>(c->callee.get())) name = n->name;
                else if (auto *m = as<MemberExpr>(c->callee.get())) name = m->member;
                auto it = table.entries.find(siteKey(scope, c->line, c->col, name));
                if (it != table.entries.end()) c->typeArgs = it->second;
            } else if (auto *n = as<NewExpr>(&e)) {
                auto it = table.entries.find(siteKey(scope, n->line, n->col, n->className));
                if (it != table.entries.end()) n->typeArgs = it->second;
            }
        });
    }
}

} // namespace grav
