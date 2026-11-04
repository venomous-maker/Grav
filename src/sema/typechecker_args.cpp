// Named arguments and default parameter values. Before a call is resolved, its
// arguments are matched to the parameters of the candidate they fit: named ones
// move to their parameter's position and left-out ones take the parameter's
// default. A default is checked where the callee was declared (its namespace,
// none of the caller's locals) and is evaluated at each call that uses it.
#include "sema/typechecker.h"
#include "sema/monomorph.h"

#include <utility>

namespace grav {

TypeChecker::ParamList TypeChecker::paramList(const std::vector<Param> &ps, const std::string &ns) {
    ParamList pl;
    pl.ns = ns;
    for (const auto &p : ps) {
        pl.names.push_back(p.name);
        pl.defaults.push_back(p.defaultValue);
        if (p.variadic) pl.variadic = true;
    }
    return pl;
}

TypeChecker::ParamList TypeChecker::paramList(const GenericSig &sig) {
    ParamList pl;
    pl.ns = sig.ns;
    pl.names = sig.paramNames;
    pl.defaults = sig.defaults;
    pl.variadic = sig.variadic;
    pl.names.resize(sig.params.size());
    pl.defaults.resize(sig.params.size());
    return pl;
}

namespace {
// Can `args` (positional first, then named) be matched to `pl`? On failure,
// `why` says what is wrong.
bool fits(const std::vector<ExprPtr> &args, const TypeChecker::ParamList &pl, std::string &why) {
    size_t positional = 0;
    bool sawNamed = false;
    std::vector<bool> filled(pl.names.size(), false);
    for (const auto &a : args) {
        auto *na = dynamic_cast<const NamedArgExpr *>(a.get());
        if (!na) {
            if (sawNamed) { why = "a positional argument cannot follow a named one"; return false; }
            if (pl.variadic && positional + 1 >= pl.names.size()) { ++positional; continue; }
            if (positional >= pl.names.size()) { why = "too many arguments"; return false; }
            filled[positional++] = true;
            continue;
        }
        sawNamed = true;
        if (pl.variadic) { why = "a function with a variadic parameter takes no named arguments"; return false; }
        size_t i = 0;
        while (i < pl.names.size() && pl.names[i] != na->name) ++i;
        if (i == pl.names.size()) { why = "there is no parameter named '" + na->name + "'"; return false; }
        if (filled[i]) { why = "'" + na->name + "' is given twice"; return false; }
        filled[i] = true;
    }
    for (size_t i = 0; i < pl.names.size(); ++i) {
        if (filled[i] || pl.defaults[i] || (pl.variadic && i + 1 == pl.names.size())) continue;
        why = "the argument for '" + pl.names[i] + "' is missing";
        return false;
    }
    return true;
}
} // namespace

int TypeChecker::completeArgs(std::vector<ExprPtr> &args, const std::vector<ParamList> &cands, int line,
                              int col, const std::string &what) {
    bool named = false;
    for (const auto &a : args) named = named || dynamic_cast<const NamedArgExpr *>(a.get());
    // Plain positional arguments that some candidate takes as they are.
    if (!named)
        for (const auto &pl : cands) {
            size_t n = pl.names.size();
            if (pl.variadic ? args.size() + 1 >= n : args.size() == n) return -1;
        }
    std::vector<size_t> fitting;
    std::string why;
    for (size_t i = 0; i < cands.size(); ++i) {
        std::string w;
        if (fits(args, cands[i], w)) fitting.push_back(i);
        else if (why.empty()) why = w;
    }
    if (fitting.empty()) {
        if (!named && cands.size() != 1) return -1; // the usual "no overload" report follows
        if (!named && why == "too many arguments") return -1; // and the usual arity report
        error(line, col, cands.size() == 1 ? "in the call to " + what + ": " + why
                                           : "no overload of " + what + " takes these arguments");
        return -2;
    }
    if (fitting.size() > 1) {
        error(line, col, "ambiguous call to " + what + ": several overloads take these arguments");
        return -2;
    }
    const ParamList &pl = cands[fitting[0]];
    std::vector<ExprPtr> out(pl.names.size());
    std::vector<ExprPtr> rest; // a variadic parameter's arguments
    size_t positional = 0;
    for (auto &a : args) {
        if (auto *na = dynamic_cast<NamedArgExpr *>(a.get())) {
            size_t i = 0;
            while (pl.names[i] != na->name) ++i;
            out[i] = std::move(na->value);
        } else if (pl.variadic && positional + 1 >= pl.names.size()) {
            rest.push_back(std::move(a));
            ++positional;
        } else {
            out[positional++] = std::move(a);
        }
    }
    args.clear();
    for (size_t i = 0; i < out.size(); ++i) {
        if (pl.variadic && i + 1 == out.size()) break;
        if (!out[i]) {
            auto d = std::make_unique<DefaultArgExpr>();
            d->line = line;
            d->col = col;
            d->ns = pl.ns;
            d->value = cloneExpression(pl.defaults[i].get());
            out[i] = std::move(d);
        }
        args.push_back(std::move(out[i]));
    }
    for (auto &r : rest) args.push_back(std::move(r));
    return static_cast<int>(fitting[0]);
}

// A default value, checked as written in the callee. Its problems are reported
// once, where the default is declared (checkDefaults), not at every call.
TypeRef TypeChecker::checkDefaultArg(DefaultArgExpr &e) {
    size_t nErr = errors_.size(), nWarn = warnings_.size();
    std::string savedNs = std::exchange(currentNs_, e.ns);
    auto savedScopes = std::exchange(scopes_, {});
    auto savedLambdas = std::exchange(lambdaStack_, {});
    pushScope();
    const TypeRef *want = expected_;
    TypeRef t = want ? checkExprExpect(*e.value, *want) : checkExpr(*e.value);
    if (want && !isAssignable(t, *want)) t = TypeRef::prim(TypeRef::Kind::Error); // reported at the declaration
    scopes_ = std::move(savedScopes);
    lambdaStack_ = std::move(savedLambdas);
    currentNs_ = savedNs;
    errors_.erase(errors_.begin() + static_cast<long>(nErr), errors_.end());
    warnings_.erase(warnings_.begin() + static_cast<long>(nWarn), warnings_.end());
    return t;
}

// The defaults of a declaration's parameters: each must suit its parameter.
void TypeChecker::checkDefaults(const std::vector<Param> &params) {
    for (const auto &p : params) {
        if (!p.defaultValue) continue;
        ExprPtr copy = cloneExpression(p.defaultValue.get());
        auto savedScopes = std::exchange(scopes_, {});
        pushScope();
        TypeRef t = checkExprExpect(*copy, p.type);
        scopes_ = std::move(savedScopes);
        if (!t.isError() && !isAssignable(t, p.type))
            error(copy->line, copy->col, "the default value of '" + p.name + "' is " + aTypeName(t) +
                                             ", but the parameter is " + aTypeName(p.type));
    }
}

// When the arguments fit no candidate: check what they say, named or not.
void TypeChecker::checkArgValues(std::vector<ExprPtr> &args) {
    for (auto &a : args) {
        if (auto *na = dynamic_cast<NamedArgExpr *>(a.get())) checkExpr(*na->value);
        else checkExpr(*a);
    }
}

// After resolution: the defaults stand in the argument list as plain expressions.
void TypeChecker::unwrapDefaults(std::vector<ExprPtr> &args) {
    for (auto &a : args)
        if (auto *d = dynamic_cast<DefaultArgExpr *>(a.get())) {
            TypeRef t = d->type;
            a = std::move(d->value);
            if (a->type.isError()) a->type = t;
        }
}

} // namespace grav
