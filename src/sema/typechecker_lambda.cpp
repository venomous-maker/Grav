// Lambdas and first-class functions: checking lambda bodies, capture analysis
// (captured variables are copied into the closure, so they are read-only inside
// it), references to named functions, and calls through function values.
#include "sema/typechecker.h"

namespace grav {

TypeChecker::LocalVar *TypeChecker::lookupLocalAt(const std::string &name, size_t &scopeIdx) {
    for (size_t i = scopes_.size(); i-- > 0;) {
        auto found = scopes_[i].find(name);
        if (found != scopes_[i].end()) { scopeIdx = i; return &found->second; }
    }
    return nullptr;
}

bool TypeChecker::noteCapture(const std::string &name, const TypeRef &type, size_t scopeIdx) {
    bool captured = false;
    for (auto &ctx : lambdaStack_) {
        if (ctx.scopeBase <= scopeIdx) continue; // declared inside this lambda
        captured = true;
        bool have = false;
        for (const auto &c : ctx.lam->captures) if (c.name == name) { have = true; break; }
        if (!have) ctx.lam->captures.push_back({name, type});
    }
    return captured;
}

bool TypeChecker::isCapturedHere(const std::string &name) {
    if (lambdaStack_.empty()) return false;
    size_t idx = 0;
    if (!lookupLocalAt(name, idx)) return false;
    return idx < lambdaStack_.back().scopeBase;
}

TypeRef TypeChecker::checkExprExpect(Expr &e, const TypeRef &expected) {
    const TypeRef *saved = expected_;
    expected_ = dynamic_cast<LambdaExpr *>(&e) || dynamic_cast<NameExpr *>(&e) ||
                        dynamic_cast<NewExpr *>(&e) || dynamic_cast<CallExpr *>(&e) ||
                        dynamic_cast<TernaryExpr *>(&e) || dynamic_cast<ArrayLiteralExpr *>(&e)
                    ? &expected
                    : nullptr;
    TypeRef t = checkExpr(e);
    expected_ = saved;
    // An integer literal must fit the integer type it initializes.
    if (auto *lit = dynamic_cast<IntLiteralExpr *>(&e); lit && expected.kind == TypeRef::Kind::Int) {
        int bits = expected.numBits();
        // (A 64-bit unsigned target takes any literal: 0xFFFFFFFFFFFFFFFF wraps to -1.)
        bool fits = expected.isUnsigned
                        ? bits >= 64 || (lit->value >= 0 && lit->value < (1LL << bits))
                        : bits >= 64 || (lit->value >= -(1LL << (bits - 1)) && lit->value < (1LL << (bits - 1)));
        if (!fits)
            error(e.line, e.col, "integer literal " + lit->raw + " does not fit in " + typeRefName(expected));
    }
    return t;
}

std::optional<TypeRef> TypeChecker::tryFunctionRef(NameExpr &e) {
    std::string fq = reg_->resolveFunc(e.name, currentNs_);
    if (fq.empty()) return std::nullopt;
    const auto &set = reg_->funcOverloads(fq);
    const FunctionInfo *fi = nullptr;
    if (set.size() == 1) {
        fi = &set[0];
    } else if (expected_ && expected_->isFunction()) {
        for (const auto &c : set)
            if (c.paramTypes == expected_->fnParams && c.returnType == expected_->fnReturn())
                fi = &c;
    }
    if (!fi) {
        error(e.line, e.col, "'" + e.name + "' is overloaded; give the variable a function type "
                             "(e.g. `let f: fn(int) -> int = " + e.name + "`) to pick one");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (fi->isVariadic || fi->isAsync) {
        error(e.line, e.col, "variadic and async functions cannot be used as values");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    e.fnRef = fq;
    e.fnRefOverload = fi->overloadIndex;
    return TypeRef::function(fi->paramTypes, fi->returnType);
}

TypeRef TypeChecker::checkLambda(LambdaExpr &e) {
    TypeRef expect = expected_ ? *expected_ : TypeRef::prim(TypeRef::Kind::Error);
    expected_ = nullptr;
    bool haveExp = expect.isFunction() && expect.fnParams.size() == e.params.size();

    std::vector<TypeRef> ptypes;
    for (size_t i = 0; i < e.params.size(); ++i) {
        TypeRef pt;
        if (i < e.paramTyped.size() && e.paramTyped[i]) {
            bool ok = true;
            pt = reg_->resolveCanonical(e.params[i].type, currentNs_, ok);
            if (!ok) {
                error(e.line, e.col, "unknown type '" + typeRefName(e.params[i].type) + "'");
                pt = TypeRef::prim(TypeRef::Kind::Error);
            }
        } else if (haveExp) {
            pt = expect.fnParams[i];
        } else {
            error(e.line, e.col, "cannot infer the type of lambda parameter '" + e.params[i].name +
                                     "'; write `" + e.params[i].name + ": T`");
            pt = TypeRef::prim(TypeRef::Kind::Error);
        }
        e.params[i].type = pt;
        ptypes.push_back(pt);
    }
    TypeRef ret;
    bool retKnown = true;
    if (e.hasReturnType) {
        bool ok = true;
        ret = reg_->resolveCanonical(e.returnType, currentNs_, ok);
        if (!ok) ret = TypeRef::prim(TypeRef::Kind::Error);
    } else if (haveExp && expect.fnReturn().isError()) {
        // The caller is inferring a type parameter from this lambda's result.
        if (e.isArrow) retKnown = false;
        else ret = TypeRef::prim(TypeRef::Kind::Void);
    } else if (haveExp) {
        ret = expect.fnReturn();
    } else if (e.isArrow) {
        retKnown = false; // inferred from the body expression
    } else {
        ret = TypeRef::prim(TypeRef::Kind::Void);
    }

    // The body is checked like a fresh function nested in the current scopes.
    TypeRef savedReturn = currentReturn_;
    int savedLoop = loopDepth_;
    bool savedDefer = inDefer_, savedAsync = inAsync_, savedCtor = inConstructor_;
    lambdaStack_.push_back({&e, scopes_.size()});
    pushScope();
    for (size_t i = 0; i < e.params.size(); ++i)
        declareLocal(e.params[i].name, ptypes[i], true, false, e.line, e.col);
    loopDepth_ = 0;
    inDefer_ = false;
    inAsync_ = false;
    inConstructor_ = false;
    if (e.isArrow) {
        TypeRef bt = retKnown ? checkExprExpect(*e.exprBody, ret) : checkExpr(*e.exprBody);
        if (!retKnown) {
            ret = bt.kind == TypeRef::Kind::Null ? TypeRef::prim(TypeRef::Kind::Error) : bt;
        } else if (!ret.isVoid() && !isAssignable(bt, ret)) {
            error(e.exprBody->line, e.exprBody->col,
                  "lambda returns " + typeRefName(bt) + " but " + typeRefName(ret) + " is expected");
        }
    } else {
        currentReturn_ = ret;
        checkBlock(e.body);
        checkFallthrough(e.body, ret, e.line, e.col, "lambda");
    }
    popScope();
    lambdaStack_.pop_back();
    currentReturn_ = savedReturn;
    loopDepth_ = savedLoop;
    inDefer_ = savedDefer;
    inAsync_ = savedAsync;
    inConstructor_ = savedCtor;

    e.returnType = ret;
    e.hasReturnType = true;
    e.fnType = TypeRef::function(ptypes, ret);
    return e.fnType;
}

TypeRef TypeChecker::checkFnValueCall(CallExpr &e, const TypeRef &fnType) {
    e.kind = CallKind::FnValue;
    checkArgs(e.args, fnType.fnParams, e.line, e.col, "function value of type " + typeRefName(fnType));
    return fnType.fnReturn();
}

} // namespace grav
