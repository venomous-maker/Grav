#include "sema/typechecker.h"

#include <algorithm>

namespace grav {

namespace {
std::string joinDots(const std::vector<std::string> &parts, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        if (i) out += '.';
        out += parts[i];
    }
    return out;
}
} // namespace

std::optional<std::vector<std::string>>
TypeChecker::flattenNames(const Expr *e) const {
    std::vector<std::string> out;
    const Expr *cur = e;
    while (true) {
        if (auto *m = dynamic_cast<const MemberExpr *>(cur)) {
            out.push_back(m->member);
            cur = m->object.get();
        } else if (auto *n = dynamic_cast<const NameExpr *>(cur)) {
            out.push_back(n->name);
            break;
        } else {
            return std::nullopt;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void TypeChecker::checkArgs(const std::vector<ExprPtr> &args,
                            const std::vector<TypeRef> &params, int line, int col,
                            const std::string &what) {
    std::vector<TypeRef> argTypes;
    for (size_t i = 0; i < args.size(); ++i)
        argTypes.push_back(i < params.size() ? checkExprExpect(*args[i], params[i])
                                             : checkExpr(*args[i]));
    if (argTypes.size() != params.size()) {
        error(line, col, what + " expects " + std::to_string(params.size()) +
                             " argument(s), but got " + std::to_string(argTypes.size()));
    }
    size_t n = std::min(argTypes.size(), params.size());
    for (size_t i = 0; i < n; ++i) {
        if (!isAssignable(argTypes[i], params[i])) {
            error(args[i]->line, args[i]->col,
                  "argument " + std::to_string(i + 1) + " to " + what +
                      " expects " + typeRefName(params[i]) + ", but got " +
                      typeRefName(argTypes[i]));
        }
    }
}

void TypeChecker::checkVariadicArgs(const std::vector<ExprPtr> &args,
                                    const std::vector<TypeRef> &params, int line, int col,
                                    const std::string &what) {
    size_t fixed = params.empty() ? 0 : params.size() - 1;
    TypeRef elem = params.empty() ? TypeRef::prim(TypeRef::Kind::Error) : params.back();
    if (args.size() < fixed) {
        error(line, col, what + " expects at least " + std::to_string(fixed) +
                             " argument(s), but got " + std::to_string(args.size()));
    }
    for (size_t i = 0; i < args.size(); ++i) {
        const TypeRef &want = i < fixed ? params[i] : elem;
        TypeRef at = checkExprExpect(*args[i], want);
        if (!isAssignable(at, want))
            error(args[i]->line, args[i]->col,
                  "argument " + std::to_string(i + 1) + " to " + what + " expects " +
                      typeRefName(want) + ", but got " + typeRefName(at));
    }
}

TypeRef TypeChecker::checkExpr(Expr &expr) {
    TypeRef t = TypeRef::prim(TypeRef::Kind::Error);
    // An integer literal is an int, or a long when it needs 64 bits.
    if (auto *lit = dynamic_cast<IntLiteralExpr *>(&expr))
        t = lit->value > 2147483647LL || lit->value < -2147483648LL ? TypeRef::integer(64, false)
                                                                   : TypeRef::prim(TypeRef::Kind::Int);
    else if (dynamic_cast<FloatLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::Float);
    else if (dynamic_cast<BoolLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::Bool);
    else if (dynamic_cast<StringLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::String);
    else if (dynamic_cast<NullLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::Null);
    else if (dynamic_cast<CBlockExpr *>(&expr)) {
        // Inline C may reference any in-scope local by name; mark them all used so
        // we don't warn about "unused" variables consumed only by the C escape.
        for (auto &scope : scopes_)
            for (auto &[n, v] : scope) v.used = true;
        t = TypeRef::prim(TypeRef::Kind::Void); // typed by context
    }
    else if (auto *e = dynamic_cast<SelfExpr *>(&expr)) {
        if (currentClass_.empty() || inStatic_) {
            error(e->line, e->col, "'self' is only valid inside an instance method or constructor");
            t = TypeRef::prim(TypeRef::Kind::Error);
        } else {
            t = TypeRef::named(currentClass_);
            // `self` inside a lambda is captured like any other outer variable.
            for (auto &ctx : lambdaStack_) {
                bool have = false;
                for (const auto &c : ctx.lam->captures) if (c.name == "self") have = true;
                if (!have) ctx.lam->captures.push_back({"self", t});
            }
        }
    } else if (auto *e = dynamic_cast<SuperExpr *>(&expr)) {
        const ClassInfo *ci = currentClass_.empty() ? nullptr : reg_->cls(currentClass_);
        if (currentClass_.empty() || inStatic_ || !ci || ci->baseClass.empty()) {
            error(e->line, e->col,
                  "'super' is only valid inside an instance method or constructor of a "
                  "class that extends a parent");
            t = TypeRef::prim(TypeRef::Kind::Error);
        } else {
            t = TypeRef::named(ci->baseClass);
        }
    } else if (auto *e = dynamic_cast<NameExpr *>(&expr)) {
        size_t scopeIdx = 0;
        LocalVar *local = lookupLocalAt(e->name, scopeIdx);
        if (local) {
            local->used = true;
            t = local->type;
            if (!lambdaStack_.empty()) noteCapture(e->name, t, scopeIdx);
        } else if (std::string gfq = reg_->resolveGlobal(e->name, currentNs_); !gfq.empty()) {
            const GlobalInfo *gi = reg_->global(gfq);
            e->resolvedGlobal = gi->cName;
            t = gi->type;
        } else if (auto fr = tryFunctionRef(*e)) {
            t = *fr; // a named function used as a value
        } else {
            error(e->line, e->col, "use of undeclared variable '" + e->name + "'");
            t = TypeRef::prim(TypeRef::Kind::Error);
        }
    } else if (auto *e = dynamic_cast<BinaryExpr *>(&expr)) {
        t = checkBinary(*e);
    } else if (auto *e = dynamic_cast<UnaryExpr *>(&expr)) {
        t = checkUnary(*e);
    } else if (auto *e = dynamic_cast<TernaryExpr *>(&expr)) {
        t = checkTernary(*e);
    } else if (auto *e = dynamic_cast<AsExpr *>(&expr)) {
        t = checkAs(*e);
    } else if (auto *e = dynamic_cast<IsExpr *>(&expr)) {
        t = checkIs(*e);
    } else if (auto *e = dynamic_cast<AwaitExpr *>(&expr)) {
        t = checkAwait(*e);
    } else if (auto *e = dynamic_cast<AddrOfExpr *>(&expr)) {
        t = checkAddrOf(*e);
    } else if (auto *e = dynamic_cast<DerefExpr *>(&expr)) {
        t = checkDeref(*e);
    } else if (auto *e = dynamic_cast<CoalesceExpr *>(&expr)) {
        t = checkCoalesce(*e);
    } else if (auto *e = dynamic_cast<IncDecExpr *>(&expr)) {
        t = checkIncDec(*e);
    } else if (auto *e = dynamic_cast<CastExpr *>(&expr)) {
        t = checkCast(*e);
    } else if (auto *e = dynamic_cast<SizeofExpr *>(&expr)) {
        t = checkSizeof(*e);
    } else if (auto *e = dynamic_cast<ArrayLiteralExpr *>(&expr)) {
        t = checkArrayLiteral(*e);
    } else if (auto *e = dynamic_cast<IndexExpr *>(&expr)) {
        t = checkIndex(*e);
    } else if (auto *e = dynamic_cast<NewExpr *>(&expr)) {
        t = checkNew(*e);
    } else if (auto *e = dynamic_cast<StructLiteralExpr *>(&expr)) {
        t = checkStructLiteral(*e);
    } else if (auto *e = dynamic_cast<CallExpr *>(&expr)) {
        t = checkCall(*e);
    } else if (auto *e = dynamic_cast<MemberExpr *>(&expr)) {
        t = checkMember(*e);
    } else if (auto *e = dynamic_cast<LambdaExpr *>(&expr)) {
        t = checkLambda(*e);
    }
    expr.type = t;
    return t;
}

TypeRef TypeChecker::checkBinary(BinaryExpr &e) {
    TypeRef lt = checkExpr(*e.left);
    TypeRef rt = checkExpr(*e.right);
    if (lt.isError() || rt.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // A class operator method (`fn operator+(o: T)`) takes over the operator.
    if (auto ov = tryOperatorOverload(e, lt, rt)) return *ov;

    const char *sym = binaryOpSymbol(e.op);
    if (isLogical(e.op)) {
        if (lt.kind != TypeRef::Kind::Bool || rt.kind != TypeRef::Kind::Bool)
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires bool operands, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt));
        return TypeRef::prim(TypeRef::Kind::Bool);
    }
    // Reference equality against `null`: `obj == null`, `null != obj`, `null == null`.
    if ((e.op == BinaryOp::Eq || e.op == BinaryOp::NotEq) &&
        (lt.kind == TypeRef::Kind::Null || rt.kind == TypeRef::Kind::Null)) {
        auto refOrNull = [&](const TypeRef &x) {
            return x.kind == TypeRef::Kind::Null || x.isPointer() || x.isFunction() || x.isAny() ||
                   (x.isNamed() && (reg_->isClass(x.name) || reg_->isInterface(x.name)));
        };
        if (refOrNull(lt) && refOrNull(rt))
            return TypeRef::prim(TypeRef::Kind::Bool);
        error(e.line, e.col,
              "cannot compare " + typeRefName(lt) + " and " + typeRefName(rt) +
                  " against null");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!isComparison(e.op)) {
        // string + string -> concatenation
        if (e.op == BinaryOp::Add && lt.kind == TypeRef::Kind::String &&
            rt.kind == TypeRef::Kind::String) {
            e.stringConcat = true;
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (!lt.isNumeric() || !rt.isNumeric()) {
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires numeric operands, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        // '%', bitwise and shift operators are integer-only in C.
        if (isIntOnly(e.op) &&
            (lt.kind != TypeRef::Kind::Int || rt.kind != TypeRef::Kind::Int)) {
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires int operands, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        // Mixed numeric operands promote to the wider/float type, like C.
        return numericPromote(lt, rt);
    }

    bool ordered = e.op != BinaryOp::Eq && e.op != BinaryOp::NotEq;
    // `any == value` compares the boxed value (the other side is boxed).
    if (!ordered && (lt.isAny() || rt.isAny())) {
        if (isBoxable(lt) && isBoxable(rt)) return TypeRef::prim(TypeRef::Kind::Bool);
        error(e.line, e.col, "cannot compare " + typeRefName(lt) + " with " + typeRefName(rt));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    // Numeric comparisons allow mixed widths/signedness; otherwise types must match.
    if (lt.isNumeric() && rt.isNumeric()) {
        return TypeRef::prim(TypeRef::Kind::Bool);
    }
    if (lt != rt) {
        error(e.line, e.col, std::string("operator '") + sym +
                                 "' requires matching types, but got " +
                                 typeRefName(lt) + " and " + typeRefName(rt));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (ordered && !lt.isNumeric()) {
        error(e.line, e.col, std::string("operator '") + sym +
                                 "' requires numeric operands");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return TypeRef::prim(TypeRef::Kind::Bool);
}

bool TypeChecker::isLvalue(const Expr &e) const {
    return dynamic_cast<const NameExpr *>(&e) != nullptr ||
           dynamic_cast<const MemberExpr *>(&e) != nullptr ||
           dynamic_cast<const DerefExpr *>(&e) != nullptr ||
           dynamic_cast<const IndexExpr *>(&e) != nullptr;
}

TypeRef TypeChecker::checkAddrOf(AddrOfExpr &e) {
    TypeRef t = checkExpr(*e.operand);
    if (t.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (!isLvalue(*e.operand)) {
        error(e.line, e.col, "'&' requires a variable, field, or dereference");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return TypeRef::pointer(t);
}

TypeRef TypeChecker::checkDeref(DerefExpr &e) {
    TypeRef t = checkExpr(*e.operand);
    if (t.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (!t.isPointer()) {
        error(e.line, e.col,
              "'*' requires a pointer operand, but got " + typeRefName(t));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return *t.elem; // an lvalue of the pointee type
}

TypeRef TypeChecker::checkCoalesce(CoalesceExpr &e) {
    TypeRef l = checkExpr(*e.left);
    TypeRef r = checkExpr(*e.right);
    if (l.isError() || r.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // `null ?? b` -> b's type. Otherwise the left must be a nullable reference.
    if (l.kind == TypeRef::Kind::Null) return r;
    bool leftNullable = l.isPointer() ||
                        (l.isNamed() && (reg_->isClass(l.name) || reg_->isInterface(l.name)));
    if (!leftNullable) {
        error(e.line, e.col, "the left of '??' must be a nullable reference "
                             "(class, interface, or pointer), but got " + typeRefName(l));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (isAssignable(r, l)) return l;
    if (isAssignable(l, r)) return r;
    error(e.line, e.col, "'??' branches have incompatible types " +
                             typeRefName(l) + " and " + typeRefName(r));
    return TypeRef::prim(TypeRef::Kind::Error);
}

TypeRef TypeChecker::checkUnary(UnaryExpr &e) {
    TypeRef t = checkExpr(*e.operand);
    if (t.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (e.op == UnaryOp::BitNot) {
        if (t.kind != TypeRef::Kind::Int) {
            error(e.line, e.col,
                  "operator '~' requires an int operand, but got " + typeRefName(t));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        return TypeRef::prim(TypeRef::Kind::Int);
    }
    // UnaryOp::Not
    if (t.kind != TypeRef::Kind::Bool) {
        error(e.line, e.col,
              "operator '!' requires a bool operand, but got " + typeRefName(t));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return TypeRef::prim(TypeRef::Kind::Bool);
}

TypeRef TypeChecker::checkTernary(TernaryExpr &e) {
    // The expected type (a declared variable, parameter or return type) flows
    // into both branches: `return ok ? Result.Ok(x) : Result.Err(e)` infers both.
    const TypeRef *outer = expected_;
    TypeRef want = outer ? *outer : TypeRef::prim(TypeRef::Kind::Error);
    expected_ = nullptr;
    requireBool(*e.cond, "a ternary condition");
    TypeRef a = outer ? checkExprExpect(*e.thenExpr, want) : checkExpr(*e.thenExpr);
    TypeRef b = outer ? checkExprExpect(*e.elseExpr, want) : checkExpr(*e.elseExpr);
    if (a.isError() || b.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // Pick the common type of the two branches (handles `cond ? obj : null`).
    if (a == b) return a;
    if (isAssignable(b, a)) return a;
    if (isAssignable(a, b)) return b;
    error(e.line, e.col, "ternary branches have incompatible types " +
                             typeRefName(a) + " and " + typeRefName(b));
    return TypeRef::prim(TypeRef::Kind::Error);
}

TypeRef TypeChecker::checkAs(AsExpr &e) {
    // `%{ c_expr %} as Type` — an inline C value adopts the annotated type.
    if (dynamic_cast<CBlockExpr *>(e.operand.get())) {
        checkExpr(*e.operand); // marks in-scope locals used (inline C may read them)
        TypeRef *inner = &e.target;
        while (inner->isPointer() && inner->elem) inner = inner->elem.get();
        if (inner->isNamed()) {
            std::string fq = reg_->resolveType(inner->name, currentNs_);
            if (!fq.empty()) *inner = TypeRef::named(fq);
        }
        e.operand->type = e.target;
        return e.target;
    }
    // Canonicalize the target (resolving named parts and expanding aliases, looking
    // through any pointer wrappers like `Point*`).
    {
        bool ok = true;
        TypeRef c = reg_->resolveCanonical(e.target, currentNs_, ok);
        if (!ok) {
            checkExpr(*e.operand);
            error(e.line, e.col, "unknown type '" + typeRefName(e.target) + "' in cast");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        e.target = c;
    }
    // The cast target guides inference: `new Array<auto>() as Array<int>`.
    TypeRef src = checkExprExpect(*e.operand, e.target);
    if (src.isError()) return e.target;

    // `any`: boxing is implicit (and allowed here too); unboxing is checked at run time.
    if (e.target.isAny()) {
        if (isBoxable(src)) return e.target;
        error(e.line, e.col, "a value of type " + typeRefName(src) + " cannot be stored in 'any'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (src.isAny()) {
        if (e.target.isNamed() && reg_->isInterface(e.target.name)) {
            error(e.line, e.col, "take the object out of 'any' as its class, then use it as '" +
                                     e.target.name + "'");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        if (isBoxable(e.target) && e.target.kind != TypeRef::Kind::Null) return e.target;
        error(e.line, e.col, "cannot take a " + typeRefName(e.target) + " out of 'any'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }

    // Pointer cast: between any pointer types, or from null (like a C cast).
    if (e.target.isPointer()) {
        if (src.isPointer() || src.kind == TypeRef::Kind::Null) return e.target;
        error(e.line, e.col, "cannot cast " + typeRefName(src) + " to a pointer type");
        return TypeRef::prim(TypeRef::Kind::Error);
    }

    bool srcEnum = src.isNamed() && reg_->isEnum(src.name);
    switch (e.target.kind) {
        case TypeRef::Kind::Int:
            if (src.isNumeric() || src.kind == TypeRef::Kind::Bool || srcEnum) return e.target;
            break;
        case TypeRef::Kind::Float:
        case TypeRef::Kind::Bool:
            if (src.isNumeric() || src.kind == TypeRef::Kind::Bool) return e.target;
            break;
        case TypeRef::Kind::String:
            if (src.kind == TypeRef::Kind::String || src.isBinary()) return e.target;
            break;
        case TypeRef::Kind::Binary:
            if (src.isBinary() || src.kind == TypeRef::Kind::String) return e.target;
            break;
        case TypeRef::Kind::Named: {
            // An integer ordinal -> enum value (`2 as Suit`).
            if (reg_->isEnum(e.target.name) && (src.kind == TypeRef::Kind::Int || srcEnum))
                return e.target;
            // Class<->class within one hierarchy, or class -> interface it implements.
            if (reg_->isClass(e.target.name) && src.isNamed() && reg_->isClass(src.name) &&
                (reg_->isSubclass(src.name, e.target.name) ||
                 reg_->isSubclass(e.target.name, src.name)))
                return e.target;
            if (reg_->isInterface(e.target.name) && src.isNamed() &&
                reg_->isClass(src.name) && reg_->classImplements(src.name, e.target.name))
                return e.target;
            if (src.kind == TypeRef::Kind::Null &&
                (reg_->isClass(e.target.name) || reg_->isInterface(e.target.name)))
                return e.target;
            break;
        }
        default:
            break;
    }
    error(e.line, e.col, "cannot cast a value of type " + typeRefName(src) +
                             " to " + typeRefName(e.target));
    return TypeRef::prim(TypeRef::Kind::Error);
}

TypeRef TypeChecker::checkIs(IsExpr &e) {
    TypeRef src = checkExpr(*e.operand);
    // `x is int` / `x is string` / `x is MyClass` on an `any`: a run-time tag test.
    if (src.isAny()) {
        bool ok = true;
        TypeRef t = reg_->resolveCanonical(e.target, currentNs_, ok);
        if (!ok || !isBoxable(t) || t.kind == TypeRef::Kind::Null) {
            error(e.line, e.col, "'is' on an 'any' needs a type an 'any' can hold, got '" +
                                     typeRefName(e.target) + "'");
            return TypeRef::prim(TypeRef::Kind::Bool);
        }
        e.target = t;
        if (t.isNamed()) e.className = t.name;
        return TypeRef::prim(TypeRef::Kind::Bool);
    }
    if (!e.target.isNamed()) {
        error(e.line, e.col, "'is' expects a class name (or an 'any' on the left)");
        return TypeRef::prim(TypeRef::Kind::Bool);
    }
    std::string fq = reg_->resolveType(e.typeName, currentNs_);
    if (fq.empty() || !reg_->isClass(fq)) {
        error(e.line, e.col, "'is' expects a class name, got '" + e.typeName + "'");
        return TypeRef::prim(TypeRef::Kind::Bool);
    }
    e.className = fq;
    if (!src.isError() && !(src.isNamed() && reg_->isClass(src.name)))
        error(e.operand->line, e.operand->col,
              "'is' expects a class instance on the left, got " + typeRefName(src));
    return TypeRef::prim(TypeRef::Kind::Bool);
}

TypeRef TypeChecker::checkAwait(AwaitExpr &e) {
    TypeRef t = checkExpr(*e.operand);
    if (t.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (!t.isFuture()) {
        error(e.line, e.col,
              "'await' expects a Future (from an async call), but got " + typeRefName(t));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return *t.elem; // unwrap Future<T> -> T
}

// `EnumType.Member` — recognized when the whole expression is a name chain whose
// head is not a local and whose prefix resolves to an enum type.
std::optional<TypeRef> TypeChecker::tryEnumValue(MemberExpr &e) {
    auto chain = flattenNames(&e);
    if (!chain || chain->size() < 2) return std::nullopt;
    if (lookupLocal((*chain)[0]) != nullptr) return std::nullopt;
    std::string member = chain->back();
    std::string prefix = joinDots(*chain, chain->size() - 1);
    std::string enumFq = reg_->resolveType(prefix, currentNs_);
    if (enumFq.empty() || !reg_->isEnum(enumFq)) return std::nullopt;
    if (!reg_->hasEnumMember(enumFq, member)) {
        error(e.line, e.col,
              "enum '" + enumFq + "' has no member '" + member + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    e.kind = MemberKind::EnumValue;
    e.qualified = enumFq;
    e.member = member;
    return TypeRef::named(enumFq);
}

TypeRef TypeChecker::checkIncDec(IncDecExpr &e) {
    TypeRef t = checkExpr(*e.target);
    if (t.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (auto *n = dynamic_cast<NameExpr *>(e.target.get()); n && isCapturedHere(n->name))
        error(e.line, e.col, "cannot modify captured variable '" + n->name +
                                 "' inside a lambda (captures are copies)");
    if (!isLvalue(*e.target)) {
        error(e.line, e.col, "'++'/'--' requires a variable or field");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!t.isNumeric()) {
        error(e.line, e.col,
              "'++'/'--' requires a numeric operand, but got " + typeRefName(t));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return t;
}

TypeRef TypeChecker::checkCast(CastExpr &e) {
    TypeRef src = checkExpr(*e.operand);
    if (src.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    bool ok = false;
    bool srcEnum = src.isNamed() && reg_->isEnum(src.name);
    switch (e.target.kind) {
        case TypeRef::Kind::Int:
            // enums lower to ints, so int(color) is allowed (e.g. for printing).
            ok = src.isNumeric() || src.kind == TypeRef::Kind::Bool || srcEnum;
            break;
        case TypeRef::Kind::Float:
        case TypeRef::Kind::Bool:
            ok = src.isNumeric() || src.kind == TypeRef::Kind::Bool;
            break;
        case TypeRef::Kind::String:
            ok = src.kind == TypeRef::Kind::String || src.isBinary();
            break;
        case TypeRef::Kind::Binary:
            ok = src.isBinary() || src.kind == TypeRef::Kind::String;
            break;
        default:
            ok = false;
    }
    if (!ok) {
        error(e.line, e.col, "cannot cast a value of type " + typeRefName(src) +
                                 " to " + typeRefName(e.target));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return e.target;
}

TypeRef TypeChecker::checkSizeof(SizeofExpr &e) {
    // A bare type name (e.g. `sizeof(Point)`) parses as a NameExpr; promote it to
    // the type form when the name is a type/alias rather than a local variable.
    if (!e.isType && e.operand) {
        if (auto chain = flattenNames(e.operand.get())) {
            if (!lookupLocal((*chain)[0])) {
                std::string fq = reg_->resolveTypeOrAlias(joinDots(*chain, chain->size()),
                                                          currentNs_);
                if (!fq.empty()) {
                    e.isType = true;
                    e.target = TypeRef::named(joinDots(*chain, chain->size()));
                    e.operand.reset();
                }
            }
        }
    }
    if (e.isType) {
        bool ok = true;
        TypeRef c = reg_->resolveCanonical(e.target, currentNs_, ok);
        if (!ok) {
            error(e.line, e.col, "unknown type '" + typeRefName(e.target) + "' in sizeof");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        e.target = c;
    } else {
        TypeRef t = checkExpr(*e.operand);
        if (t.isVoid())
            error(e.line, e.col, "cannot take sizeof a void value");
    }
    return TypeRef::prim(TypeRef::Kind::Int);
}

TypeRef TypeChecker::checkArrayLiteral(ArrayLiteralExpr &e) {
    if (e.elements.empty()) {
        error(e.line, e.col,
              "an empty array literal '[]' has no element type; "
              "give the variable an explicit `T[N]` type");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    TypeRef elem = checkExpr(*e.elements[0]);
    for (size_t i = 1; i < e.elements.size(); ++i) {
        TypeRef et = checkExpr(*e.elements[i]);
        // Unify toward a common element type, allowing upcasts in either direction.
        if (et.isError() || elem.isError()) continue;
        if (isAssignable(et, elem)) continue;
        if (isAssignable(elem, et)) { elem = et; continue; }
        error(e.elements[i]->line, e.elements[i]->col,
              "array element " + std::to_string(i + 1) + " has type " +
                  typeRefName(et) + ", incompatible with " + typeRefName(elem));
    }
    if (elem.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    return TypeRef::array(elem, static_cast<int>(e.elements.size()));
}

TypeRef TypeChecker::checkIndex(IndexExpr &e) {
    TypeRef base = checkExpr(*e.base);
    if (auto ov = tryIndexOverload(e, base)) return *ov;
    if (!e.more.empty()) {
        error(e.more[0]->line, e.more[0]->col,
              "several indices need a class with an 'operator[]' taking them; arrays and pointers take one");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    TypeRef idx = checkExpr(*e.index);
    if (!idx.isError() && idx.kind != TypeRef::Kind::Int)
        error(e.index->line, e.index->col,
              "an array index must be int, but got " + typeRefName(idx));
    if (base.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    if (base.isArray() || base.isPointer()) return *base.elem; // an lvalue of the element type
    error(e.base->line, e.base->col,
          "cannot index a value of type " + typeRefName(base) +
              " (only arrays and pointers are indexable)");
    return TypeRef::prim(TypeRef::Kind::Error);
}

TypeRef TypeChecker::checkNew(NewExpr &e) {
    const TypeRef *expNew = expected_;
    expected_ = nullptr;
    std::string fq = reg_->resolveType(e.className, currentNs_);
    // A generic class constructed without (or with `auto`) type arguments.
    if (fq.empty() || !reg_->isClass(fq))
        if (auto r = tryInferNew(e, expNew)) return *r;
    if (fq.empty() || !reg_->isClass(fq)) {
        error(e.line, e.col, "cannot construct unknown class '" + e.className + "'");
        for (auto &a : e.args) checkExpr(*a);
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    e.className = fq;
    const ClassInfo *ci = reg_->cls(fq);
    if (ci->isAbstract) {
        error(e.line, e.col, "cannot instantiate abstract class '" + fq + "'");
    }
    int idx = resolveCtor(fq, e.args, e.line, e.col, "constructor of '" + fq + "'");
    e.ctorIndex = idx < 0 ? 0 : idx;
    // A data enum variant's constructor yields the enum (`Shape.Circle(1.0)` is a
    // Shape), so variants mix freely in arrays, returns and reassignments.
    if (ci->decl && ci->decl->variantTag >= 0 && !ci->baseClass.empty())
        return TypeRef::named(ci->baseClass);
    return TypeRef::named(fq);
}

TypeRef TypeChecker::checkStructLiteral(StructLiteralExpr &e) {
    std::string fq = reg_->resolveType(e.typeName, currentNs_);
    if (fq.empty() || !reg_->isStruct(fq)) {
        error(e.line, e.col, "'" + e.typeName + "' is not a struct type");
        for (auto &fi : e.fields) checkExpr(*fi.value);
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    e.typeName = fq;
    const StructInfo *si = reg_->strct(fq);

    std::vector<bool> seen(si->fields.size(), false);
    for (auto &fi : e.fields) {
        TypeRef vt = checkExpr(*fi.value);
        const FieldInfo *f = reg_->findStructField(fq, fi.name);
        if (!f) {
            error(fi.line, fi.col,
                  "struct '" + fq + "' has no field '" + fi.name + "'");
            continue;
        }
        size_t idx = static_cast<size_t>(f - si->fields.data());
        if (idx < seen.size()) {
            if (seen[idx])
                error(fi.line, fi.col, "field '" + fi.name + "' is set more than once");
            seen[idx] = true;
        }
        if (!isAssignable(vt, f->type))
            error(fi.value->line, fi.value->col,
                  "field '" + fi.name + "' expects " + typeRefName(f->type) +
                      ", but got " + typeRefName(vt));
    }
    for (size_t i = 0; i < si->fields.size(); ++i)
        if (!seen[i])
            error(e.line, e.col, "struct literal for '" + fq +
                                     "' is missing field '" + si->fields[i].name + "'");
    return TypeRef::named(fq);
}

// Validates already-evaluated argument types against a parameter list (no second
// pass over the argument expressions, so overload resolution doesn't double-report).
void TypeChecker::validateArgTypes(const std::vector<ExprPtr> &args,
                                   const std::vector<TypeRef> &argTypes,
                                   const std::vector<TypeRef> &params, bool variadic,
                                   int line, int col, const std::string &what) {
    if (variadic) {
        size_t fixed = params.empty() ? 0 : params.size() - 1;
        TypeRef elem = params.empty() ? TypeRef::prim(TypeRef::Kind::Error) : params.back();
        if (argTypes.size() < fixed)
            error(line, col, what + " expects at least " + std::to_string(fixed) +
                                 " argument(s), but got " + std::to_string(argTypes.size()));
        for (size_t i = 0; i < argTypes.size(); ++i) {
            const TypeRef &want = i < fixed ? params[i] : elem;
            if (!isAssignable(argTypes[i], want))
                error(args[i]->line, args[i]->col, "argument " + std::to_string(i + 1) +
                          " to " + what + " expects " + typeRefName(want) + ", but got " +
                          typeRefName(argTypes[i]));
        }
        return;
    }
    if (argTypes.size() != params.size())
        error(line, col, what + " expects " + std::to_string(params.size()) +
                             " argument(s), but got " + std::to_string(argTypes.size()));
    size_t n = std::min(argTypes.size(), params.size());
    for (size_t i = 0; i < n; ++i)
        if (!isAssignable(argTypes[i], params[i]))
            error(args[i]->line, args[i]->col, "argument " + std::to_string(i + 1) + " to " +
                      what + " expects " + typeRefName(params[i]) + ", but got " +
                      typeRefName(argTypes[i]));
}

// Chooses the best overload for the given argument types: an exact-match wins;
// otherwise a unique assignable candidate. Returns null if none or ambiguous.
const FunctionInfo *TypeChecker::pickOverload(const std::vector<FunctionInfo> &cands,
                                              const std::vector<TypeRef> &argTypes) const {
    const FunctionInfo *exact = nullptr;
    const FunctionInfo *assignable = nullptr;
    int assignableCount = 0;
    for (const auto &c : cands) {
        size_t fixed = c.isVariadic && !c.paramTypes.empty() ? c.paramTypes.size() - 1
                                                             : c.paramTypes.size();
        bool arityOk = c.isVariadic ? argTypes.size() >= fixed
                                    : argTypes.size() == c.paramTypes.size();
        if (!arityOk) continue;
        bool allAssignable = true, allExact = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            const TypeRef &want =
                (c.isVariadic && i >= fixed) ? c.paramTypes.back() : c.paramTypes[i];
            if (!isAssignable(argTypes[i], want)) { allAssignable = false; break; }
            if (!(argTypes[i] == want)) allExact = false;
        }
        if (!allAssignable) continue;
        if (allExact) exact = &c;
        assignable = &c;
        assignableCount++;
    }
    if (exact) return exact;
    return assignableCount == 1 ? assignable : nullptr;
}

// Resolves a call to a free function, choosing among overloads of the name.
TypeRef TypeChecker::resolveFreeCall(CallExpr &e, const std::string &fnFq) {
    const std::vector<FunctionInfo> &cands = reg_->funcOverloads(fnFq);
    std::vector<TypeRef> argTypes;
    argTypes.reserve(e.args.size());
    for (size_t i = 0; i < e.args.size(); ++i) {
        // With a single candidate its parameter types guide lambda inference.
        const TypeRef *want = nullptr;
        if (cands.size() == 1) {
            const auto &ps = cands[0].paramTypes;
            if (i < ps.size()) want = &ps[i];
            else if (cands[0].isVariadic && !ps.empty()) want = &ps.back();
        }
        argTypes.push_back(want ? checkExprExpect(*e.args[i], *want) : checkExpr(*e.args[i]));
    }

    const FunctionInfo *fi = pickOverload(cands, argTypes);
    if (!fi) {
        if (cands.size() == 1) {
            fi = &cands[0]; // single overload: fall through to a precise mismatch error
        } else {
            std::string got, options;
            for (size_t i = 0; i < argTypes.size(); ++i) got += (i ? ", " : "") + typeRefName(argTypes[i]);
            for (const auto &c : cands)
                options += "\n    " + fnFq + signatureText(c.paramNames, c.paramTypes, c.returnType, c.isVariadic);
            error(e.line, e.col, "no overload of '" + fnFq + "' matches (" + got +
                                     "); candidates are:" + options);
            return TypeRef::prim(TypeRef::Kind::Error);
        }
    }
    e.kind = CallKind::FreeFunction;
    e.targetName = fnFq;
    e.resolvedOverload = fi->overloadIndex;
    validateArgTypes(e.args, argTypes, fi->paramTypes, fi->isVariadic, e.line, e.col,
                     "function '" + fnFq +
                         signatureText(fi->paramNames, fi->paramTypes, fi->returnType, fi->isVariadic) + "'");
    return fi->isAsync ? TypeRef::future(fi->returnType) : fi->returnType;
}

// Resolves `super(...)` and `super.method(...)`. Returns nullopt when the call
// is not a super-call (so the normal resolution path runs).
std::optional<TypeRef> TypeChecker::checkSuperCall(CallExpr &e) {
    const ClassInfo *ci = currentClass_.empty() ? nullptr : reg_->cls(currentClass_);
    std::string parent = ci ? ci->baseClass : "";

    // super(...) — parent constructor.
    if (dynamic_cast<SuperExpr *>(e.callee.get())) {
        if (!ci || parent.empty()) {
            error(e.line, e.col, "'super(...)' requires a parent class (extends ...)");
            for (auto &a : e.args) checkExpr(*a);
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        if (!inConstructor_) {
            error(e.line, e.col, "'super(...)' can only be called from a constructor");
            for (auto &a : e.args) checkExpr(*a);
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        int idx = resolveCtor(parent, e.args, e.line, e.col, "parent constructor '" + parent + "'");
        e.resolvedOverload = idx < 0 ? 0 : idx;
        e.kind = CallKind::SuperConstructor;
        e.ownerClass = parent;
        return TypeRef::prim(TypeRef::Kind::Void);
    }

    // super.method(...) — parent method, dispatched statically to the parent impl.
    auto *mem = dynamic_cast<MemberExpr *>(e.callee.get());
    if (!mem || !dynamic_cast<SuperExpr *>(mem->object.get())) return std::nullopt;
    if (!ci || parent.empty()) {
        error(e.line, e.col, "'super' requires a parent class (extends ...)");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    std::vector<const MethodInfo *> cands;
    for (const MethodInfo *c : reg_->findMethodsBySource(parent, mem->member))
        if (!c->isStatic && c->hasBody) cands.push_back(c);
    if (cands.empty()) {
        error(mem->line, mem->col, "parent class '" + parent + "' has no method '" +
                                       mem->member + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    const MethodInfo *mi = resolveMethodOverload(cands, e.args, e.line, e.col,
                                                 "parent method '" + parent + "." + mem->member + "'");
    if (!mi) return TypeRef::prim(TypeRef::Kind::Error);
    e.kind = CallKind::SuperMethod;
    e.methodName = mi->name;
    e.ownerClass = mi->definingClass; // class whose C function we call directly
    return mi->returnType;
}

TypeRef TypeChecker::checkCall(CallExpr &e) {
    // The expected result type (for inferring generic arguments) applies to this
    // call only, not to anything nested inside it.
    const TypeRef *expCall = expected_;
    expected_ = nullptr;
    // `super(...)` — delegate to the parent constructor; `super.m(...)` — call the
    // parent's method implementation directly (non-virtual).
    if (auto sup = checkSuperCall(e)) return *sup;

    // A local/global holding a function value shadows functions and builtins.
    if (auto *name = dynamic_cast<NameExpr *>(e.callee.get())) {
        LocalVar *lv = lookupLocal(name->name);
        const GlobalInfo *gv = nullptr;
        if (!lv)
            if (std::string g = reg_->resolveGlobal(name->name, currentNs_); !g.empty())
                gv = reg_->global(g);
        if ((lv && lv->type.isFunction()) || (gv && gv->type.isFunction())) {
            TypeRef ft = checkExpr(*e.callee);
            return checkFnValueCall(e, ft);
        }
    }
    // Free function or builtin: callee is a bare name.
    if (auto *name = dynamic_cast<NameExpr *>(e.callee.get())) {
        if (name->name == "print" || name->name == "println") {
            e.kind = CallKind::Builtin;
            e.targetName = "print";   // println is an alias (print already newlines)
            std::vector<TypeRef> argTypes;
            for (auto &a : e.args) argTypes.push_back(checkExpr(*a));
            if (argTypes.size() != 1) {
                error(e.line, e.col, name->name + " expects exactly 1 argument");
            } else if (!argTypes[0].isError() && !isDisplayable(argTypes[0]) &&
                       !argTypes[0].isBinary()) {
                error(e.args[0]->line, e.args[0]->col,
                      name->name + " cannot display a value of type " + typeRefName(argTypes[0]));
            }
            return TypeRef::prim(TypeRef::Kind::Void);
        }
        // Library-internal reference counting for values kept in raw buffers:
        // __retain(x) is x with one more reference, __release(x) drops one.
        // Both do nothing for non-reference types (so they work in generic code).
        if ((name->name == "__retain" || name->name == "__release" || name->name == "__visit") && e.args.size() == 1) {
            e.kind = CallKind::Builtin;
            e.targetName = name->name;
            TypeRef t = checkExpr(*e.args[0]);
            return name->name == "__retain" ? t : TypeRef::prim(TypeRef::Kind::Void);
        }
        if (name->name == "__show" && e.args.size() == 1) {
            // Internal (generated data-enum toString): any value as text; values
            // that cannot be boxed show their type name.
            e.kind = CallKind::Builtin;
            e.targetName = "__show";
            checkExpr(*e.args[0]);
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "assertEq") {
            // assertEq(actual, expected[, message]) — reports both values on failure.
            e.kind = CallKind::Builtin;
            e.targetName = "assertEq";
            if (e.args.size() < 2 || e.args.size() > 3) {
                error(e.line, e.col, "assertEq expects (actual, expected[, message])");
                for (auto &a : e.args) checkExpr(*a);
                return TypeRef::prim(TypeRef::Kind::Void);
            }
            TypeRef a = checkExpr(*e.args[0]);
            TypeRef b = checkExprExpect(*e.args[1], a);
            if (!a.isError() && !b.isError() && !(isBoxable(a) && isBoxable(b)) &&
                !(a == b))
                error(e.line, e.col, "assertEq cannot compare " + typeRefName(a) + " with " + typeRefName(b));
            if (e.args.size() == 3) {
                TypeRef m = checkExpr(*e.args[2]);
                if (!m.isError() && m.kind != TypeRef::Kind::String)
                    error(e.args[2]->line, e.args[2]->col, "assertEq message must be a string");
            }
            return TypeRef::prim(TypeRef::Kind::Void);
        }
        if (name->name == "exit" || name->name == "panic" || name->name == "assert") {
            e.kind = CallKind::Builtin;
            e.targetName = name->name;
            // exit(int), panic(string), assert(bool[, string])
            TypeRef::Kind want = name->name == "exit" ? TypeRef::Kind::Int
                                : name->name == "panic" ? TypeRef::Kind::String
                                                        : TypeRef::Kind::Bool;
            size_t maxArgs = name->name == "assert" ? 2 : 1;
            if (e.args.empty() || e.args.size() > maxArgs) {
                error(e.line, e.col, name->name + " expects 1" +
                                         (name->name == "assert" ? " or 2 argument(s)" : " argument"));
            } else {
                TypeRef t = checkExpr(*e.args[0]);
                if (!t.isError() && t.kind != want)
                    error(e.args[0]->line, e.args[0]->col,
                          name->name + " expects " + typeRefName(TypeRef::prim(want)) +
                              ", got " + typeRefName(t));
                if (e.args.size() == 2) checkExpr(*e.args[1]); // assert message
            }
            return TypeRef::prim(TypeRef::Kind::Void);
        }
        if (name->name == "cwd") {
            e.kind = CallKind::Builtin;
            e.targetName = "cwd";
            if (!e.args.empty()) error(e.line, e.col, "cwd expects no arguments");
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "env") {
            e.kind = CallKind::Builtin;
            e.targetName = "env";
            if (e.args.size() != 1) error(e.line, e.col, "env expects 1 argument (a name)");
            else {
                TypeRef t = checkExpr(*e.args[0]);
                if (!t.isError() && t.kind != TypeRef::Kind::String)
                    error(e.args[0]->line, e.args[0]->col, "env expects a string name");
            }
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "typename") {
            e.kind = CallKind::Builtin;
            e.targetName = "typename";
            if (e.args.size() != 1) {
                error(e.line, e.col, "typename expects exactly 1 argument");
            } else {
                TypeRef t = checkExpr(*e.args[0]);
                if (!t.isError() && !t.isAny() && !(t.isNamed() && reg_->isClass(t.name)))
                    error(e.args[0]->line, e.args[0]->col,
                          "typename expects a class instance or an any, got " + typeRefName(t));
            }
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "isInstance") {
            e.kind = CallKind::Builtin;
            e.targetName = "isInstance";
            if (e.args.size() != 2) {
                error(e.line, e.col, "isInstance expects (instance, Type)");
                return TypeRef::prim(TypeRef::Kind::Bool);
            }
            TypeRef obj = checkExpr(*e.args[0]);
            if (!obj.isError() && !(obj.isNamed() && reg_->isClass(obj.name)))
                error(e.args[0]->line, e.args[0]->col,
                      "isInstance expects a class instance as the first argument");
            // Second argument names a type, not a value.
            auto chain = flattenNames(e.args[1].get());
            std::string typeFq = chain ? reg_->resolveType(joinDots(*chain, chain->size()), currentNs_) : "";
            if (typeFq.empty() || !reg_->isClass(typeFq)) {
                error(e.args[1]->line, e.args[1]->col,
                      "isInstance expects a class name as the second argument");
            }
            e.ownerClass = typeFq; // stash resolved class for codegen
            return TypeRef::prim(TypeRef::Kind::Bool);
        }
        if (name->name == "str") {
            e.kind = CallKind::Builtin;
            e.targetName = "str";
            if (e.args.size() != 1) {
                error(e.line, e.col, "str expects exactly 1 argument");
            } else {
                TypeRef t = checkExpr(*e.args[0]);
                bool ok = isDisplayable(t) || t.isBinary();
                if (!t.isError() && !ok)
                    error(e.args[0]->line, e.args[0]->col,
                          "str cannot convert a value of type " + typeRefName(t));
            }
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "input") {
            e.kind = CallKind::Builtin;
            e.targetName = "input";
            if (!e.args.empty())
                error(e.line, e.col, "input expects no arguments");
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "argc") {
            e.kind = CallKind::Builtin;
            e.targetName = "argc";
            if (!e.args.empty())
                error(e.line, e.col, "argc expects no arguments");
            return TypeRef::prim(TypeRef::Kind::Int);
        }
        if (name->name == "free") {
            // Frees a heap object created with `new` (or a pointer). Use with care.
            e.kind = CallKind::Builtin;
            e.targetName = "free";
            if (e.args.size() != 1) {
                error(e.line, e.col, "free expects exactly 1 argument");
            } else {
                TypeRef t = checkExpr(*e.args[0]);
                bool ok = t.isPointer() || t.isFunction() || t.isFuture() ||
                          (t.isNamed() && reg_->isClass(t.name));
                if (!t.isError() && !ok)
                    error(e.args[0]->line, e.args[0]->col,
                          "free expects a class instance or pointer, got " + typeRefName(t));
            }
            return TypeRef::prim(TypeRef::Kind::Void);
        }
        if (name->name == "argv") {
            e.kind = CallKind::Builtin;
            e.targetName = "argv";
            if (e.args.size() != 1) {
                error(e.line, e.col, "argv expects exactly 1 argument (an index)");
            } else {
                TypeRef t = checkExpr(*e.args[0]);
                if (!t.isError() && t.kind != TypeRef::Kind::Int)
                    error(e.args[0]->line, e.args[0]->col,
                          "argv expects an int index, got " + typeRefName(t));
            }
            return TypeRef::prim(TypeRef::Kind::String);
        }
        std::string fnFq = reg_->resolveFunc(name->name, currentNs_);
        // A generic function called without (or with `auto`) type arguments.
        if (fnFq.empty() && program_)
            for (const auto &cand : nameCandidates(name->name, currentNs_, program_->uses))
                if (program_->generics.functions.count(cand))
                    return *tryInferCall(e, name->name, program_->generics.functions.at(cand), expCall);
        if (fnFq.empty()) {
            error(e.line, e.col, "unknown function '" + name->name + "'");
            for (auto &a : e.args) checkExpr(*a);
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        return resolveFreeCall(e, fnFq);
    }

    auto *mem = dynamic_cast<MemberExpr *>(e.callee.get());
    if (!mem) {
        // Any other expression producing a function value: `make()(x)`, `fs[i](x)`.
        TypeRef ft = checkExpr(*e.callee);
        if (ft.isFunction()) return checkFnValueCall(e, ft);
        if (!ft.isError()) error(e.line, e.col, "this expression is not callable");
        for (auto &a : e.args) checkExpr(*a);
        return TypeRef::prim(TypeRef::Kind::Error);
    }

    // Static method or namespace-qualified function: a pure name chain whose
    // head is not a local variable.
    auto chain = flattenNames(e.callee.get());
    if (chain && chain->size() >= 2 && lookupLocal((*chain)[0]) == nullptr) {
        std::string last = chain->back();
        std::string prefix = joinDots(*chain, chain->size() - 1);
        std::string clsFq = reg_->resolveType(prefix, currentNs_);
        if (!clsFq.empty() && reg_->isClass(clsFq)) {
            std::vector<const MethodInfo *> statics;
            const MethodInfo *anyMi = nullptr;
            for (const MethodInfo *c : reg_->findMethodsBySource(clsFq, last)) {
                anyMi = c;
                if (c->isStatic) statics.push_back(c);
            }
            if (!statics.empty()) {
                const MethodInfo *mi = resolveMethodOverload(
                    statics, e.args, e.line, e.col, "static method '" + clsFq + "." + last + "'");
                if (!mi) return TypeRef::prim(TypeRef::Kind::Error);
                mem->kind = MemberKind::StaticTarget;
                mem->qualified = clsFq;
                e.kind = CallKind::StaticMethod;
                e.targetName = clsFq;
                e.methodName = mi->name;
                e.ownerClass = mi->definingClass;
                if (!checkAccess(mi->access, mi->definingClass))
                    error(e.line, e.col, "static method '" + last + "' is " +
                                             accessName(mi->access));
                return mi->returnType;
            }
            const MethodInfo *mi = anyMi;
            if (mi && !mi->isStatic) {
                error(e.line, e.col, "method '" + last + "' is not static; call it on an instance");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
        }
        std::string fnFq = reg_->resolveFunc(joinDots(*chain, chain->size()), currentNs_);
        if (!fnFq.empty()) {
            return resolveFreeCall(e, fnFq);
        }
        // A static generic method called without type arguments.
        if (!clsFq.empty() && program_) {
            auto gm = program_->generics.methods.find(clsFq + "." + last);
            if (gm != program_->generics.methods.end())
                return *tryInferCall(e, last, gm->second, expCall);
        }
        error(e.line, e.col, "unknown function or static method '" +
                                 joinDots(*chain, chain->size()) + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }

    // Instance method: dispatch on the object's class via its vtable.
    TypeRef objType = checkExpr(*mem->object);
    if (objType.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // `obj.handler(x)` where `handler` is a field holding a function value.
    if (objType.isNamed() && (reg_->isClass(objType.name) || reg_->isStruct(objType.name))) {
        const FieldInfo *fld = reg_->isClass(objType.name)
                                   ? reg_->findField(objType.name, mem->member)
                                   : reg_->findStructField(objType.name, mem->member);
        if (fld && fld->type.isFunction() &&
            (!reg_->isClass(objType.name) ||
             reg_->findMethodsBySource(objType.name, mem->member).empty())) {
            TypeRef ft = checkExpr(*e.callee);
            return checkFnValueCall(e, ft);
        }
    }

    // Interface-typed receiver: dispatch through the interface's itable.
    if (objType.isNamed() && reg_->isInterface(objType.name)) {
        const MethodInfo *mi = reg_->findInterfaceMethod(objType.name, mem->member);
        if (!mi) {
            error(mem->line, mem->col, "interface '" + objType.name +
                                           "' has no method '" + mem->member + "'");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        mem->kind = MemberKind::MethodRef;
        e.kind = CallKind::InstanceMethod;
        e.ifaceDispatch = true;
        e.ownerClass = objType.name; // interface fq
        e.methodName = mem->member;
        checkArgs(e.args, mi->paramTypes, e.line, e.col,
                  "method '" + objType.name + "." + mem->member + "'");
        return mi->returnType;
    }

    if (!objType.isNamed() || !reg_->isClass(objType.name)) {
        error(mem->line, mem->col, "cannot call method '" + mem->member +
                                       "' on a value of type " + typeRefName(objType));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    std::vector<const MethodInfo *> cands;
    for (const MethodInfo *c : reg_->findMethodsBySource(objType.name, mem->member))
        if (!c->isStatic) cands.push_back(c);
    // A generic method called without type arguments (on this class or a base).
    if (cands.empty() && program_) {
        std::string cur = objType.name;
        for (int guard = 0; !cur.empty() && guard < 100; ++guard) {
            auto gm = program_->generics.methods.find(cur + "." + mem->member);
            if (gm != program_->generics.methods.end())
                return *tryInferCall(e, mem->member, gm->second, expCall);
            const ClassInfo *ci = reg_->cls(cur);
            cur = ci ? ci->baseClass : "";
        }
    }
    if (cands.empty()) {
        error(mem->line, mem->col, "type '" + objType.name + "' has no method '" +
                                       mem->member + "'");
        for (auto &a : e.args) checkExpr(*a);
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    const MethodInfo *mi = resolveMethodOverload(cands, e.args, e.line, e.col,
                                                 "method '" + objType.name + "." + mem->member + "'");
    if (!mi) return TypeRef::prim(TypeRef::Kind::Error);
    if (!checkAccess(mi->access, mi->definingClass))
        error(mem->line, mem->col, "method '" + mem->member + "' is " +
                                       accessName(mi->access) + " in '" +
                                       mi->definingClass + "'");

    // Find the vtable slot owner for the static receiver type.
    std::string slotOwner = objType.name;
    if (const ClassInfo *ci = reg_->cls(objType.name)) {
        for (const auto &s : ci->slots)
            if (s.name == mi->name) { slotOwner = s.slotOwner; break; }
    }
    mem->kind = MemberKind::MethodRef;
    mem->ownerClass = objType.name;
    e.kind = CallKind::InstanceMethod;
    e.methodName = mi->name;
    e.ownerClass = objType.name;
    e.slotOwner = slotOwner;
    if (mem->optional) checkOptionalResult(e.line, e.col, mi->returnType, /*allowVoid=*/true);
    return mi->returnType;
}

void TypeChecker::checkOptionalResult(int line, int col, const TypeRef &t,
                                     bool allowVoid) {
    if (t.isError()) return;
    if (allowVoid && t.isVoid()) return;
    bool bad = t.isVoid() ||
               (t.isNamed() && (reg_->isStruct(t.name) || reg_->isInterface(t.name)));
    if (bad)
        error(line, col, "optional chaining '?.' cannot yield " + typeRefName(t) +
                             " (a value-type or interface); use '.' instead");
}

// `Class.field` where `field` is a static field — resolved like an enum value
// (a name chain whose head is not a local).
std::optional<TypeRef> TypeChecker::tryStaticField(MemberExpr &e) {
    auto chain = flattenNames(&e);
    if (!chain || chain->size() < 2) return std::nullopt;
    if (lookupLocal((*chain)[0]) != nullptr) return std::nullopt;
    std::string member = chain->back();
    std::string prefix = joinDots(*chain, chain->size() - 1);
    std::string clsFq = reg_->resolveType(prefix, currentNs_);
    if (clsFq.empty() || !reg_->isClass(clsFq)) return std::nullopt;
    const GlobalInfo *gi = reg_->findStaticField(clsFq, member);
    if (!gi) return std::nullopt;
    if (!checkAccess(gi->access, gi->ownerClass))
        error(e.line, e.col, "static field '" + member + "' is " +
                                 accessName(gi->access) + " in '" + gi->ownerClass + "'");
    e.kind = MemberKind::StaticField;
    e.qualified = gi->cName;
    return gi->type;
}

TypeRef TypeChecker::checkMember(MemberExpr &e) {
    // `EnumType.Member` is a constant, not a field access on a value.
    if (auto enumTy = tryEnumValue(e)) return *enumTy;
    if (auto sf = tryStaticField(e)) return *sf;
    TypeRef objType = checkExpr(*e.object);
    if (objType.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // `arr.length` is the fixed length of an array, as an int.
    if (objType.isArray()) {
        if (e.member == "length") return TypeRef::prim(TypeRef::Kind::Int);
        error(e.line, e.col,
              "an array has no member '" + e.member + "' (only 'length')");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    // `bytes.length` is the runtime byte count of a binary value, as an int.
    if (objType.isBinary()) {
        if (e.member == "length") return TypeRef::prim(TypeRef::Kind::Int);
        error(e.line, e.col,
              "a binary value has no member '" + e.member + "' (only 'length')");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (objType.isNamed() && reg_->isStruct(objType.name)) {
        const FieldInfo *f = reg_->findStructField(objType.name, e.member);
        if (!f) {
            error(e.line, e.col, "struct '" + objType.name + "' has no field '" +
                                     e.member + "'");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        e.kind = MemberKind::InstanceField;
        e.ownerClass = objType.name;
        return f->type;
    }
    if (!objType.isNamed() || !reg_->isClass(objType.name)) {
        error(e.line, e.col, "cannot access member '" + e.member +
                                 "' of a value of type " + typeRefName(objType));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    const FieldInfo *f = reg_->findField(objType.name, e.member);
    if (!f) {
        if (reg_->findMethod(objType.name, e.member))
            error(e.line, e.col, "method '" + e.member + "' must be called");
        else
            error(e.line, e.col, "type '" + objType.name + "' has no field '" +
                                     e.member + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!checkAccess(f->access, f->definingClass))
        error(e.line, e.col, "field '" + e.member + "' is " +
                                 accessName(f->access) + " in '" + f->definingClass + "'");
    e.kind = MemberKind::InstanceField;
    e.ownerClass = f->definingClass;
    if (e.optional) checkOptionalResult(e.line, e.col, f->type, /*allowVoid=*/false);
    return f->type;
}

} // namespace grav
