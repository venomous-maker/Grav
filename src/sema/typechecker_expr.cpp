#include "sema/typechecker.h"
#include "sema/monomorph.h"

#include <algorithm>
#include <functional>

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
                      typeRefName(argTypes[i]) + nullHint(argTypes[i], params[i]));
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
                      typeRefName(want) + ", but got " + typeRefName(at) + nullHint(at, want));
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
            captureSelf();
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
            captureSelf();                                 // (`super` is self, seen as its parent)
        }
    } else if (auto *e = dynamic_cast<NameExpr *>(&expr)) {
        size_t scopeIdx = 0;
        LocalVar *local = lookupLocalAt(e->name, scopeIdx);
        if (local) {
            local->used = true;
            t = local->type;
            // A narrowed `T?` reads its value; a closure copies the whole `T?`.
            e->unwrapNullable = local->narrowed && local->narrowedFrom.elem->kind != TypeRef::Kind::String;
            if (!lambdaStack_.empty()) noteCapture(e->name, local->narrowed ? local->narrowedFrom : t, scopeIdx);
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
        unwrapDefaults(e->args);
    } else if (auto *e = dynamic_cast<StructLiteralExpr *>(&expr)) {
        t = checkStructLiteral(*e);
    } else if (auto *e = dynamic_cast<CallExpr *>(&expr)) {
        t = checkCall(*e);
        unwrapDefaults(e->args);
    } else if (auto *e = dynamic_cast<MemberExpr *>(&expr)) {
        t = checkMember(*e);
    } else if (auto *e = dynamic_cast<LambdaExpr *>(&expr)) {
        t = checkLambda(*e);
    } else if (auto *e = dynamic_cast<DefaultArgExpr *>(&expr)) {
        t = checkDefaultArg(*e);
    } else if (auto *e = dynamic_cast<NamedArgExpr *>(&expr)) {
        error(e->line, e->col, "named arguments ('" + e->name + ": ...') are for calls to functions, "
                               "methods and constructors by name");
        checkExpr(*e->value);
    }
    expr.type = t;
    return t;
}

TypeRef TypeChecker::checkBinary(BinaryExpr &e) {
    TypeRef lt = checkExpr(*e.left);
    // `x != null && x > 0`: the right side sees x as a value (and `x == null || ...`).
    std::vector<std::string> facts;
    if (e.op == BinaryOp::And || e.op == BinaryOp::Or) nullFacts(*e.left, e.op == BinaryOp::And, facts);
    std::erase_if(facts, [&](const std::string &n) { return exprAssignsName(*e.right, n); });
    pushNarrowing(facts);
    TypeRef rt = checkExpr(*e.right);
    popNarrowing();
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
            return x.kind == TypeRef::Kind::Null || x.isPointer() || x.isFunction() || x.isAny() || x.isNullable() ||
                   (x.isNamed() && (reg_->isClass(x.name) || reg_->isInterface(x.name)));
        };
        if (refOrNull(lt) && refOrNull(rt))
            return TypeRef::prim(TypeRef::Kind::Bool);
        const TypeRef &value = refOrNull(lt) ? rt : lt;
        error(e.line, e.col,
              aTypeName(value) + " is never null: only objects, interfaces, functions, "
              "pointers, any values and nullable (T?) values can be compared with null");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    // A `T?` compares (==, !=) with a T or another T?; it has no order or arithmetic.
    if (lt.isNullable() || rt.isNullable()) {
        const TypeRef &a = lt.isNullable() ? *lt.elem : lt;
        const TypeRef &b = rt.isNullable() ? *rt.elem : rt;
        bool eq = e.op == BinaryOp::Eq || e.op == BinaryOp::NotEq;
        if (!eq) {
            error(e.line, e.col, std::string("operator '") + sym + "' cannot use " +
                                     typeRefName(lt.isNullable() ? lt : rt) +
                                     ", which may be null: use 'x!', 'x ?" "? fallback', or check 'x != null' first");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        bool fits = a == b || (a.isNumeric() && b.isNumeric() && !(lt.isNullable() && rt.isNullable()));
        if (!fits) {
            error(e.line, e.col, std::string("operator '") + sym + "' requires matching types, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        return TypeRef::prim(TypeRef::Kind::Bool);
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
        // Bitwise and shift operators are integer-only; `%` on floats is fmod.
        if (isIntOnly(e.op) && !(e.op == BinaryOp::Mod && lt.isNumeric() && rt.isNumeric()) &&
            (lt.kind != TypeRef::Kind::Int || rt.kind != TypeRef::Kind::Int)) {
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires int operands, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        // A shift has the (promoted) type of its left operand, like C; shifting
        // by its width or more is undefined, so a constant count must be smaller.
        if (e.op == BinaryOp::Shl || e.op == BinaryOp::Shr) {
            TypeRef res = lt.numBits() < 32 ? TypeRef::prim(TypeRef::Kind::Int) : lt;
            if (auto *n = dynamic_cast<IntLiteralExpr *>(e.right.get()); n && n->value >= res.numBits())
                error(e.right->line, e.right->col, "cannot shift " + aTypeName(res) + " by " + n->raw +
                                                       " bits (it has " + std::to_string(res.numBits()) +
                                                       "); shift a long instead, e.g. (1 as long) << " + n->raw);
            return res;
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
    // Strings order byte by byte (like Str.compare); enums by declaration order.
    if (ordered && !lt.isNumeric() && lt.kind != TypeRef::Kind::String &&
        !(lt.isNamed() && reg_->isEnum(lt.name))) {
        error(e.line, e.col, std::string("operator '") + sym +
                                 "' requires numbers, strings or enum values, but got " + typeRefName(lt));
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
    // `x ?? fallback` on a `T?`: a T when the fallback is one, else still a T?.
    if (l.isNullable()) {
        if (isAssignable(r, *l.elem)) return *l.elem;
        if (r == l) return l;
        error(e.line, e.col, "the fallback of '?" "?' must be " + aTypeName(*l.elem) + " or " +
                                 typeRefName(l) + ", but got " + typeRefName(r));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    bool leftNullable = l.isPointer() ||
                        (l.isNamed() && (reg_->isClass(l.name) || reg_->isInterface(l.name)));
    if (!leftNullable) {
        error(e.line, e.col, "the left of '?" "?' must be a nullable value "
                             "(T?, class, interface, or pointer), but got " + typeRefName(l));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (isAssignable(r, l)) return l;
    if (isAssignable(l, r)) return r;
    error(e.line, e.col, "'?" "?' branches have incompatible types " +
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
    // `x != null ? x + 1 : 0`: each branch sees what the condition proves.
    auto branch = [&](Expr &x, bool whenTrue) {
        std::vector<std::string> facts;
        nullFacts(*e.cond, whenTrue, facts);
        std::erase_if(facts, [&](const std::string &n) { return exprAssignsName(x, n); });
        pushNarrowing(facts);
        TypeRef t = outer ? checkExprExpect(x, want) : checkExpr(x);
        popNarrowing();
        return t;
    };
    TypeRef a = branch(*e.thenExpr, true);
    TypeRef b = branch(*e.elseExpr, false);
    if (a.isError() || b.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // `ok ? 5 : null` is an int?.
    if (a.kind == TypeRef::Kind::Null && b.kind != TypeRef::Kind::Null)
        if (auto n = nullableOf(b); n && n->isNullable()) return *n;
    if (b.kind == TypeRef::Kind::Null && a.kind != TypeRef::Kind::Null)
        if (auto n = nullableOf(a); n && n->isNullable()) return *n;
    // Pick the common type of the two branches (handles `cond ? obj : null`).
    if (a == b) return a;
    if (isAssignable(b, a)) return a;
    if (isAssignable(a, b)) return b;
    if (outer && !want.isError() && isAssignable(a, want) && isAssignable(b, want)) return want;
    if (auto base = commonBase(a, b)) return *base;
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
            error(e.line, e.col, reg_->typeProblem(e.target, currentNs_) + " in cast");
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
        // An object inside is checked against the interface at run time.
        if (e.target.isNamed() && reg_->isInterface(e.target.name)) return e.target;
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
            // Between classes and interfaces in any direction: whatever the static
            // types cannot prove is checked at run time (a subclass may implement
            // an interface its base does not; an interface may hold any class).
            bool srcRef = src.isNamed() && (reg_->isClass(src.name) || reg_->isInterface(src.name));
            if (srcRef && reg_->isInterface(e.target.name)) return e.target;
            if (src.isNamed() && reg_->isInterface(src.name) && reg_->isClass(e.target.name))
                return e.target;
            if (src.kind == TypeRef::Kind::Null &&
                (reg_->isClass(e.target.name) || reg_->isInterface(e.target.name)))
                return e.target;
            break;
        }
        default:
            break;
    }
    // A conversion, as a call in the operand's place (the cast is then a no-op):
    // `price as float` is `price.toFloat()`, `19.99 as Decimal` is
    // `Decimal.from(19.99)`, `"42" as int` is `"42".toInt()`, `42 as string` is
    // `42.toString()` (std.types' methods for the built-in types).
    if (std::string method = conversionMethod(src, e.target); !method.empty()) {
        auto call = std::make_unique<CallExpr>();
        call->line = e.line;
        call->col = e.col;
        auto callee = std::make_unique<MemberExpr>();
        callee->line = e.line;
        callee->col = e.col;
        callee->member = method;
        if (method == "from") {
            auto owner = std::make_unique<NameExpr>();
            owner->name = e.target.name;
            owner->line = e.line;
            owner->col = e.col;
            callee->object = std::move(owner);
            call->args.push_back(std::move(e.operand));
        } else {
            callee->object = std::move(e.operand);
        }
        call->callee = std::move(callee);
        e.operand = std::move(call);
        TypeRef got = checkExpr(*e.operand);
        if (got.isError()) return TypeRef::prim(TypeRef::Kind::Error);
        return checkAs(e);
    }
    error(e.line, e.col, "cannot cast a value of type " + typeRefName(src) +
                             " to " + typeRefName(e.target));
    return TypeRef::prim(TypeRef::Kind::Error);
}

// The method a cast from `src` to `target` calls when it is a conversion rather
// than a cast Grav makes itself: a class's toInt/toLong/toFloat/toString/toBool
// to a built-in type; a class's static `from(x)` from anything it takes; and
// between strings and numbers or bools through std.types. "" when there is none.
std::string TypeChecker::conversionMethod(const TypeRef &src, const TypeRef &target) const {
    auto builtinName = [](const TypeRef &t) -> std::string {
        switch (t.kind) {
            case TypeRef::Kind::Int: return t.bits == 64 ? "toLong" : "toInt";
            case TypeRef::Kind::Float: return "toFloat";
            case TypeRef::Kind::String: return "toString";
            case TypeRef::Kind::Bool: return "toBool";
            default: return "";
        }
    };
    auto hasInstance = [&](const std::string &cls, const std::string &m) {
        for (const MethodInfo *x : reg_->findMethodsBySource(cls, m))
            if (!x->isStatic && x->paramTypes.empty()) return true;
        return false;
    };
    auto hasStatic = [&](const std::string &cls, const std::string &m) {
        for (const MethodInfo *x : reg_->findMethodsBySource(cls, m))
            if (x->isStatic && x->paramTypes.size() == 1) return true;
        return false;
    };
    std::string want = builtinName(target);
    if (src.isNamed() && reg_->isClass(src.name) && !want.empty()) {
        if (hasInstance(src.name, want)) return want;
        if (want == "toInt" && hasInstance(src.name, "toLong")) return "toLong";   // (then narrowed)
        return "";
    }
    if (target.isNamed() && reg_->isClass(target.name) && hasStatic(target.name, "from")) return "from";
    // Built-in to built-in: through the value's std.types class (`"42".toInt()`, `42.toString()`).
    if (!want.empty() && (src.kind == TypeRef::Kind::String || target.kind == TypeRef::Kind::String)) {
        for (const auto &owner : valueMethodClasses(src))
            if (hasStatic(owner, want) || (want != "toString" && !reg_->findMethodsBySource(owner, want).empty())) return want;
    }
    return "";
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
    if (fq.empty() || !(reg_->isClass(fq) || reg_->isInterface(fq))) {
        error(e.line, e.col, "'is' expects a class or interface name, got '" + e.typeName + "'");
        return TypeRef::prim(TypeRef::Kind::Bool);
    }
    e.className = fq;
    if (!src.isError() && !(src.isNamed() && (reg_->isClass(src.name) || reg_->isInterface(src.name))))
        error(e.operand->line, e.operand->col,
              "'is' expects an object or interface value on the left, got " + typeRefName(src));
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
    if (auto *m = dynamic_cast<MemberExpr *>(e.target.get()); m && dynamic_cast<CallExpr *>(m->bound.get())) {
        error(e.line, e.col, "'++'/'--' on property '" + m->member + "' works only as a statement of its own, "
                             "not inside an expression");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
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

// `[a, b]` where a collection class is expected (`let s: HashSet<int> = [1, 2]`):
// `(fn() -> C { let c = new C()  c.add(a)  c.add(b)  return c })()`, with the
// class's add / push / enqueue / pushBack method.
TypeRef TypeChecker::buildCollection(ArrayLiteralExpr &e, const TypeRef &type) {
    const MethodInfo *adder = nullptr;
    for (const char *name : {"add", "push", "enqueue", "pushBack"}) {
        for (const MethodInfo *m : reg_->findMethodsBySource(type.name, name))
            if (!m->isStatic && m->paramTypes.size() == 1) { adder = m; break; }
        if (adder) break;
    }
    const ClassInfo *ci = reg_->cls(type.name);
    bool plainNew = ci && (ci->ctors.empty() || std::any_of(ci->ctors.begin(), ci->ctors.end(),
                                                             [](const ConstructorDecl *c) { return c->params.empty(); }));
    if (!adder || !plainNew) {
        error(e.line, e.col, "a [...] literal can build " + prettyNames(type.name) +
                                 " only if it has a constructor without parameters and add/push");
        for (auto &el : e.elements) checkExpr(*el);
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    int line = e.line, col = e.col;
    std::string var = "collection__" + std::to_string(matchCounter_++);
    auto name = [&] { auto n = std::make_unique<NameExpr>(); n->name = var; n->line = line; n->col = col; return n; };
    auto lam = std::make_unique<LambdaExpr>();
    lam->line = line; lam->col = col;
    lam->hasReturnType = true;
    lam->returnType = type;
    auto let = std::make_unique<LetStmt>();
    let->line = line; let->col = col;
    let->name = var;
    let->isConst = true;
    auto made = std::make_unique<NewExpr>();
    made->line = line; made->col = col;
    made->className = type.name;
    let->init = std::move(made);
    lam->body.statements.push_back(std::move(let));
    for (auto &el : e.elements) {
        auto callee = std::make_unique<MemberExpr>();
        callee->line = el->line; callee->col = el->col;
        callee->object = name();
        callee->member = adder->sourceName.empty() ? adder->name : adder->sourceName;
        auto call = std::make_unique<CallExpr>();
        call->line = el->line; call->col = el->col;
        call->callee = std::move(callee);
        call->args.push_back(std::move(el));
        auto st = std::make_unique<ExprStmt>();
        st->line = line; st->col = col;
        st->expr = std::move(call);
        lam->body.statements.push_back(std::move(st));
    }
    e.elements.clear();
    auto ret = std::make_unique<ReturnStmt>();
    ret->line = line; ret->col = col;
    ret->value = name();
    lam->body.statements.push_back(std::move(ret));
    auto call = std::make_unique<CallExpr>();
    call->line = line; call->col = col;
    call->callee = std::move(lam);
    e.built = std::move(call);
    return checkExpr(*e.built);
}

// The nearest class both `a` and `b` are (or derive from), if they share one.
std::optional<TypeRef> TypeChecker::commonBase(const TypeRef &a, const TypeRef &b) const {
    if (!a.isNamed() || !b.isNamed() || !reg_->isClass(a.name) || !reg_->isClass(b.name)) return std::nullopt;
    for (std::string cur = a.name; !cur.empty();) {
        if (reg_->isSubclass(b.name, cur)) return TypeRef::named(cur);
        const ClassInfo *ci = reg_->cls(cur);
        cur = ci ? ci->baseClass : "";
    }
    return std::nullopt;
}

TypeRef TypeChecker::checkArrayLiteral(ArrayLiteralExpr &e) {
    // With a known `T[N]` (a typed variable, parameter, field or return), every
    // element converts to T: `let shapes: Shape[2] = [new Square(1), new Circle(2)]`.
    const TypeRef *exp = expected_;
    expected_ = nullptr;
    if (exp && exp->isNamed() && reg_->isClass(exp->name)) return buildCollection(e, *exp);
    if (exp && exp->isArray() && !exp->isSlice() && exp->elem &&
        exp->arrayLen == static_cast<int>(e.elements.size()) && !e.elements.empty()) {
        const TypeRef &want = *exp->elem;
        for (size_t i = 0; i < e.elements.size(); ++i) {
            TypeRef et = checkExprExpect(*e.elements[i], want);
            if (!isAssignable(et, want))
                error(e.elements[i]->line, e.elements[i]->col,
                      "array element " + std::to_string(i + 1) + " has type " + typeRefName(et) +
                          ", but the array holds " + typeRefName(want));
        }
        return *exp;
    }
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
        if (auto base = commonBase(elem, et)) { elem = *base; continue; }
        error(e.elements[i]->line, e.elements[i]->col,
              "array element " + std::to_string(i + 1) + " has type " +
                  typeRefName(et) + ", incompatible with " + typeRefName(elem));
    }
    if (elem.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    return TypeRef::array(elem, static_cast<int>(e.elements.size()));
}

// `a[lo..hi]` (`..=` includes hi; a missing end means the start / the end):
// on a string, the bytes in that range; on a collection, its slice(lo, hi); on a
// fixed array, with constant bounds, a shorter fixed array. Bounds are clamped,
// like the collections' slice methods.
TypeRef TypeChecker::checkSlice(IndexExpr &e, const TypeRef &base) {
    auto lit = [&](long long v) {
        auto n = std::make_unique<IntLiteralExpr>();
        n->value = v;
        n->raw = std::to_string(v);
        n->line = e.line;
        n->col = e.col;
        return n;
    };
    bool openEnd = !e.sliceHi;
    if (!e.index) e.index = lit(0);
    if (!e.sliceHi) e.sliceHi = lit(2147483647);
    else if (e.sliceInclusive) { // hi + 1
        auto plus = std::make_unique<BinaryExpr>();
        plus->op = BinaryOp::Add;
        plus->line = e.sliceHi->line;
        plus->col = e.sliceHi->col;
        plus->left = std::move(e.sliceHi);
        plus->right = lit(1);
        e.sliceHi = std::move(plus);
    }
    e.more.clear();
    e.more.push_back(std::move(e.sliceHi));
    for (Expr *b : {e.index.get(), e.more[0].get()}) {
        TypeRef t = checkExpr(*b);
        if (!t.isError() && t.kind != TypeRef::Kind::Int)
            error(b->line, b->col, "a slice bound must be an int, but got " + typeRefName(t));
    }
    if (base.isError()) return base;
    if (base.kind == TypeRef::Kind::String) return base;
    if (base.isArray() && !base.isSlice()) {
        auto *lo = dynamic_cast<IntLiteralExpr *>(e.index.get());
        long long hiV = 0;
        auto *hiLit = dynamic_cast<IntLiteralExpr *>(e.more[0].get());
        auto *hiSum = dynamic_cast<BinaryExpr *>(e.more[0].get());
        if (hiLit) hiV = openEnd ? base.arrayLen : hiLit->value;
        else if (hiSum && dynamic_cast<IntLiteralExpr *>(hiSum->left.get()))
            hiV = static_cast<IntLiteralExpr *>(hiSum->left.get())->value + 1;
        if (!lo || (!hiLit && !(hiSum && dynamic_cast<IntLiteralExpr *>(hiSum->left.get())))) {
            error(e.line, e.col, "a slice of a fixed-length array needs constant bounds (use Array<T> for others)");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        if (lo->value < 0 || hiV > base.arrayLen || lo->value >= hiV) {
            error(e.line, e.col, "the slice [" + std::to_string(lo->value) + ".." + std::to_string(hiV) +
                                     ") does not fit in " + typeRefName(base));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        const TypeRef &el = *base.elem;
        if (!el.isNumeric() && el.kind != TypeRef::Kind::Bool && !(el.isNamed() && reg_->isEnum(el.name))) {
            error(e.line, e.col, "a fixed-length array slice copies numbers, booleans or enums; for " +
                                     typeRefName(el) + " elements use Array<T>.slice");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        e.more[0] = lit(hiV);
        return TypeRef::array(el, static_cast<int>(hiV - lo->value));
    }
    if (base.isNamed() && reg_->isClass(base.name)) {
        for (const MethodInfo *m : reg_->findMethodsBySource(base.name, "slice"))
            if (!m->isStatic && m->paramTypes.size() == 2 && m->paramTypes[0].kind == TypeRef::Kind::Int &&
                m->paramTypes[1].kind == TypeRef::Kind::Int) {
                e.overloadClass = base.name;
                e.overloadMethod = m->name;
                return m->returnType;
            }
    }
    error(e.line, e.col, "cannot slice " + aTypeName(base) + " (strings, fixed arrays and classes with "
                                                            "slice(start, end) can be sliced)");
    return TypeRef::prim(TypeRef::Kind::Error);
}

TypeRef TypeChecker::checkIndex(IndexExpr &e) {
    TypeRef base = checkExpr(*e.base);
    if (e.isSlice) return checkSlice(e, base);
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
    if (base.kind == TypeRef::Kind::String) return TypeRef::integer(8, true); // the byte at an index
    error(e.base->line, e.base->col,
          "cannot index a value of type " + typeRefName(base) +
              " (only arrays, strings and pointers are indexable)");
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
                          typeRefName(argTypes[i]) + nullHint(argTypes[i], want));
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
                      typeRefName(argTypes[i]) + nullHint(argTypes[i], params[i]));
}

// Chooses the best overload for the given argument types: an exact-match wins;
// otherwise a unique assignable candidate. Returns null if none or ambiguous.
const FunctionInfo *TypeChecker::pickOverload(const std::vector<FunctionInfo> &cands,
                                              const std::vector<TypeRef> &argTypes,
                                              const std::vector<ParamList> *pls, bool *tied) const {
    // Tiers as in resolveMethodOverload: the call's arity (exact, then assignable),
    // then the same for candidates whose left-out trailing parameters have defaults.
    int count[4] = {0, 0, 0, 0};
    const FunctionInfo *pick[4] = {nullptr, nullptr, nullptr, nullptr};
    for (size_t k = 0; k < cands.size(); ++k) {
        const FunctionInfo &c = cands[k];
        size_t fixed = c.isVariadic && !c.paramTypes.empty() ? c.paramTypes.size() - 1
                                                             : c.paramTypes.size();
        bool arityOk = c.isVariadic ? argTypes.size() >= fixed
                                    : argTypes.size() == c.paramTypes.size();
        bool defaulted = !arityOk && pls && !c.isVariadic && c.paramTypes.size() > argTypes.size() &&
                         defaultsFrom((*pls)[k], argTypes.size());
        if (!arityOk && !defaulted) continue;
        bool allAssignable = true, allExact = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            const TypeRef &want =
                (c.isVariadic && i >= fixed) ? c.paramTypes.back() : c.paramTypes[i];
            if (!isAssignable(argTypes[i], want)) { allAssignable = false; break; }
            if (!(argTypes[i] == want)) allExact = false;
        }
        if (!allAssignable) continue;
        int tier = (defaulted ? 2 : 0) + (allExact ? 0 : 1);
        count[tier]++;
        pick[tier] = &c;
    }
    for (int tier = 0; tier < 4; ++tier) {
        if (!count[tier]) continue;
        if (tied) *tied = count[tier] > 1;
        return count[tier] == 1 ? pick[tier] : nullptr;
    }
    return nullptr;
}

// Resolves a call to a free function, choosing among overloads of the name.
// The element type a spread argument `...xs` provides: a fixed array, a variadic
// parameter, or an Array (a class holding `data: T*` and `len: int`) named by a
// variable. Error type when it cannot be spread.
TypeRef TypeChecker::spreadElement(Expr &arg) {
    TypeRef t = checkExpr(arg);
    if (t.isError()) return t;
    if (t.isArray() && t.elem) return *t.elem;
    if (t.isNamed() && reg_->isClass(t.name)) {
        const FieldInfo *data = reg_->findField(t.name, "data");
        const FieldInfo *len = reg_->findField(t.name, "len");
        if (data && len && data->type.isPointer() && data->type.elem && len->type.kind == TypeRef::Kind::Int) {
            if (dynamic_cast<NameExpr *>(&arg)) return *data->type.elem;
            error(arg.line, arg.col, "spread an Array through a variable: `let xs = ...` then `f(...xs)`");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
    }
    error(arg.line, arg.col, "cannot spread " + aTypeName(t) + " (arrays, Array<T> and variadic parameters can be)");
    return TypeRef::prim(TypeRef::Kind::Error);
}

TypeRef TypeChecker::resolveFreeCall(CallExpr &e, const std::string &fnFq) {
    const std::vector<FunctionInfo> &cands = reg_->funcOverloads(fnFq);
    if (e.spreadLast) { // `f(a, ...xs)`
        const FunctionInfo *fi = nullptr;
        for (const auto &c : cands)
            if (c.isVariadic && c.paramTypes.size() == e.args.size()) fi = &c;
        if (!fi) {
            error(e.line, e.col, "'...' spreads into a variadic parameter, and '" + fnFq +
                                     "' has none in that position");
            checkArgValues(e.args);
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        for (size_t i = 0; i + 1 < e.args.size(); ++i) {
            TypeRef at = checkExprExpect(*e.args[i], fi->paramTypes[i]);
            if (!isAssignable(at, fi->paramTypes[i]))
                error(e.args[i]->line, e.args[i]->col, "argument " + std::to_string(i + 1) + " expects " +
                                                           typeRefName(fi->paramTypes[i]) + ", but got " + typeRefName(at));
        }
        TypeRef el = spreadElement(*e.args.back());
        if (!el.isError() && el != fi->paramTypes.back())
            error(e.args.back()->line, e.args.back()->col, "the spread elements are " + typeRefName(el) +
                                                               ", but '" + fnFq + "' takes " + typeRefName(fi->paramTypes.back()));
        e.kind = CallKind::FreeFunction;
        e.targetName = fnFq;
        e.resolvedOverload = fi->overloadIndex;
        return fi->isAsync ? TypeRef::future(fi->returnType) : fi->returnType;
    }
    std::vector<ParamList> pls;
    for (const auto &c : cands)
        pls.push_back(c.decl ? paramList(c.decl->params, Registry::namespaceOf(fnFq)) : ParamList{c.paramNames, std::vector<std::shared_ptr<Expr>>(c.paramNames.size()), c.isVariadic, ""});
    int done = completeArgs(e.args, pls, e.line, e.col, "function '" + fnFq + "'", true);
    if (done == -2) {
        checkArgValues(e.args);
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    std::vector<TypeRef> argTypes;
    argTypes.reserve(e.args.size());
    for (size_t i = 0; i < e.args.size(); ++i) {
        // With a single candidate its parameter types guide lambda inference;
        // with several, an array literal or a lambda takes the one type that the
        // candidates taking this many arguments have for it, if they agree.
        const TypeRef *want = nullptr;
        if (cands.size() == 1) {
            const auto &ps = cands[0].paramTypes;
            if (i < ps.size()) want = &ps[i];
            else if (cands[0].isVariadic && !ps.empty()) want = &ps.back();
        } else {
            const Expr *a = e.args[i].get();
            bool list = dynamic_cast<const ArrayLiteralExpr *>(a) != nullptr, lambda = dynamic_cast<const LambdaExpr *>(a) != nullptr;
            for (size_t k = 0; (list || lambda) && k < cands.size(); ++k) {
                const auto &c = cands[k];
                bool takes = !c.isVariadic && (c.paramTypes.size() == e.args.size() ||
                             (done == -3 && c.paramTypes.size() > e.args.size() && defaultsFrom(pls[k], e.args.size())));
                if (!takes) continue;
                const TypeRef &t = c.paramTypes[i];
                bool fits = lambda ? t.isFunction()
                                   : t.isArray() || (t.isNamed() && reg_->isClass(t.name) &&
                                                     (reg_->findMethod(t.name, "push") || reg_->findMethod(t.name, "add")));
                if (!fits) continue;
                if (want && *want != t) { want = nullptr; break; }   // two different ones: no single context
                want = &t;
            }
        }
        argTypes.push_back(want ? checkExprExpect(*e.args[i], *want) : checkExpr(*e.args[i]));
    }

    bool tied = false;
    const FunctionInfo *fi = pickOverload(cands, argTypes, done == -3 ? &pls : nullptr, &tied);
    if (fi && !fi->isVariadic && fi->paramTypes.size() > e.args.size()) {   // chosen with its defaults
        fillDefaults(e.args, pls[static_cast<size_t>(fi - cands.data())], e.line, e.col);
        for (size_t i = argTypes.size(); i < e.args.size(); ++i) argTypes.push_back(checkExprExpect(*e.args[i], fi->paramTypes[i]));
    }
    if (!fi) {
        if (cands.size() == 1) {
            fi = &cands[0]; // single overload: fall through to a precise mismatch error
        } else {
            std::string got, options;
            for (size_t i = 0; i < argTypes.size(); ++i) got += (i ? ", " : "") + typeRefName(argTypes[i]);
            for (const auto &c : cands)
                options += "\n    " + fnFq + signatureText(c.paramNames, c.paramTypes, c.returnType, c.isVariadic);
            error(e.line, e.col, std::string(tied ? "ambiguous call to '" : "no overload of '") + fnFq + "' matches (" + got +
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
    bool statics = false;
    for (const MethodInfo *c : reg_->findMethodsBySource(parent, mem->member)) {
        if (!c->isStatic && c->hasBody) cands.push_back(c);
        if (c->isStatic) statics = true;
    }
    if (cands.empty() && statics) {
        // `super.make()` for a static method: the parent's, as `Parent.make()`.
        auto name = std::make_unique<NameExpr>();
        name->line = mem->object->line;
        name->col = mem->object->col;
        name->name = parent;
        mem->object = std::move(name);
        return std::nullopt;
    }
    if (!inStatic_) captureSelf();
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
    if (mi->returnsSelf) {                // the receiver is self: this class
        TypeRef own = TypeRef::named(currentClass_);
        return mi->returnType.isNullable() ? TypeRef::nullable(own) : own;
    }
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
        if (name->name == "__fmt" && e.args.size() == 2 && dynamic_cast<StringLiteralExpr *>(e.args[1].get())) {
            // `${x:spec}` (lexer-made): the spec's type letter must suit the value.
            e.kind = CallKind::Builtin;
            e.targetName = "__fmt";
            TypeRef t = checkExpr(*e.args[0]);
            e.args[1]->type = TypeRef::prim(TypeRef::Kind::String);
            const std::string &spec = static_cast<StringLiteralExpr *>(e.args[1].get())->value;
            char kind = spec.empty() ? 0 : spec.back();
            bool intOnly = std::string("xXob").find(kind) != std::string::npos && kind;
            bool numOnly = std::string("dfe%").find(kind) != std::string::npos && kind;
            if (!t.isError() && ((intOnly && t.kind != TypeRef::Kind::Int) || (numOnly && !t.isNumeric())))
                error(e.args[0]->line, e.args[0]->col, "format '" + spec + "' needs " +
                                                           (intOnly ? "an integer" : "a number") + ", but got " + aTypeName(t));
            if (!t.isError() && kind == 'd' && t.kind != TypeRef::Kind::Int)
                error(e.args[0]->line, e.args[0]->col, "format 'd' needs an integer, but got " + aTypeName(t));
            return TypeRef::prim(TypeRef::Kind::String);
        }
        if (name->name == "__unwrap" && e.args.size() == 1) return checkUnwrap(e);
        if (name->name == "__repr" && e.args.size() == 1) {
            // Internal (collection toString): a value as text, strings quoted.
            e.kind = CallKind::Builtin;
            e.targetName = "__repr";
            checkExpr(*e.args[0]);
            return TypeRef::prim(TypeRef::Kind::String);
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
        if (name->name == "reflect") {
            // `reflect(User)`: the class's ClassInfo (std.core), for a class named here.
            e.kind = CallKind::Builtin;
            e.targetName = "reflect";
            const std::string info = "std.core.ClassInfo";
            if (e.args.size() != 1) {
                error(e.line, e.col, "reflect expects one class name, as in reflect(User)");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
            auto chain = flattenNames(e.args[0].get());
            std::string fq = chain ? reg_->resolveType(joinDots(*chain, chain->size()), currentNs_) : "";
            if (fq.empty() || !reg_->isClass(fq)) {
                std::string written = chain ? joinDots(*chain, chain->size()) : "";
                error(e.args[0]->line, e.args[0]->col,
                      chain && fq.empty() ? "reflect: there is no class '" + written + "'"
                                          : "reflect expects a class name, as in reflect(User)");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
            if (!reg_->isClass(info)) {
                error(e.line, e.col, "reflect needs the standard library's ClassInfo: import \"std\"");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
            e.ownerClass = fq;
            return TypeRef::named(info);
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
        if (auto t = checkTypeNameCast(e, *name)) return *t;
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
                    error(e.line, e.col, (propertyOf(last).empty() ? "static method '" : "static property accessor '") +
                                             memberDisplayName(last) + "' is " + accessName(mi->access));
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
        // `Cache.items.size()`, `config.limits.get()`: the prefix is a value (a
        // static field or a global, maybe followed by fields), so this is a
        // method call on it, checked below like any other.
        bool onValue = clsFq.empty() && !reg_->resolveGlobal((*chain)[0], currentNs_).empty();
        std::string noField, noFieldName;               // `Class.missing.m()`: the class and the name it lacks
        for (size_t k = 1; !onValue && clsFq.empty() && k + 1 < chain->size(); ++k) {
            std::string owner = joinDots(*chain, k);
            std::string ofq = reg_->resolveType(owner, currentNs_);
            if (!ofq.empty() && reg_->isClass(ofq)) {
                onValue = reg_->global(ofq + "." + (*chain)[k]) != nullptr;
                if (!onValue) { noField = ofq; noFieldName = (*chain)[k]; }
            } else if (ofq.empty()) {
                onValue = !reg_->resolveGlobal(joinDots(*chain, k + 1), currentNs_).empty();
            }
            if (!ofq.empty()) break;
        }
        if (!onValue) {
            if (!noField.empty())
                error(e.line, e.col, "class '" + noField + "' has no static field '" + noFieldName + "'");
            else
                error(e.line, e.col, "unknown function or static method '" +
                                         joinDots(*chain, chain->size()) + "'");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
    }

    // Instance method: dispatch on the object's class via its vtable.
    TypeRef objType = checkExpr(*mem->object);
    if (objType.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // `n.toHex()` on a built-in value: the static method of its type's class in
    // std.types, with the value first (`Integer.toHex(n)`): one plain call.
    if (std::vector<std::string> owners = valueMethodClasses(objType); !owners.empty()) {
        for (const auto &owner : owners) {
            bool has = program_ && program_->generics.methods.count(owner + "." + mem->member);   // (`Null.orElse<T>`)
            for (const MethodInfo *m : reg_->findMethodsBySource(owner, mem->member))
                if (m->isStatic && !m->paramTypes.empty()) has = true;
            if (!has) continue;
            auto cls = std::make_unique<NameExpr>();
            cls->name = owner;
            cls->line = mem->line;
            cls->col = mem->col;
            e.args.insert(e.args.begin(), std::move(mem->object));
            mem->object = std::move(cls);
            return checkCall(e);
        }
        std::string kind = typeRefName(objType);
        std::string a = std::string("aeiou").find(kind[0]) != std::string::npos ? "an " : "a ";
        std::string where = owners.back().substr(owners.back().rfind('.') + 1);
        error(mem->line, mem->col, reg_->isClass(owners.back())
                                       ? a + kind + " has no method '" + mem->member + "' (see " + where + " in std/types)"
                                       : a + kind + " has no methods without std/types: import \"std\" (or \"std/types\")");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
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

    // `p.m(args)` on a struct: its method is the function `S__m(self: S*, args)`.
    if (objType.isNamed() && reg_->isStruct(objType.name)) {
        const StructInfo *si = reg_->strct(objType.name);
        auto it = si && si->decl ? si->decl->methodFns.find(mem->member) : std::map<std::string, std::string>::iterator();
        if (si && si->decl && it != si->decl->methodFns.end()) {
            auto *ix = dynamic_cast<IndexExpr *>(mem->object.get());
            if (!isLvalue(*mem->object) || (ix && !ix->overloadClass.empty())) {
                error(mem->line, mem->col, "call '" + mem->member + "' on a struct variable or field "
                                           "(the method may change it): `let v = ...` then `v." + mem->member + "()`");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
            auto addr = std::make_unique<AddrOfExpr>();
            addr->line = mem->object->line;
            addr->col = mem->object->col;
            addr->operand = std::move(mem->object);
            auto callee = std::make_unique<NameExpr>();
            callee->name = it->second;
            callee->line = mem->line;
            callee->col = mem->col;
            e.callee = std::move(callee);
            e.args.insert(e.args.begin(), std::move(addr));
            return resolveFreeCall(e, it->second);
        }
        error(mem->line, mem->col, "struct '" + prettyNames(objType.name) + "' has no method '" + mem->member + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
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
        if (!resolveMethodOverload({mi}, e.args, e.line, e.col, "method '" + objType.name + "." + mem->member + "'"))
            return TypeRef::prim(TypeRef::Kind::Error);
        if (mi->returnType.isNamed() && mi->returnType.name == "Self") {
            error(mem->line, mem->col, "method '" + mem->member + "' returns Self, which cannot be called through "
                                       "interface '" + objType.name + "' yet: call it on a class value");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        return mi->returnType;
    }

    if (!objType.isNamed() || !reg_->isClass(objType.name)) {
        error(mem->line, mem->col, "cannot call method '" + mem->member +
                                       "' on a value of type " + typeRefName(objType) + maybeNullHint(objType));
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
    if (cands.empty() && reg_->isClass(objType.name)) {
        // A name the class lacks: Object's (std.types), with the object first (`u.toMap()`).
        bool has = false;
        for (const MethodInfo *m : reg_->findMethodsBySource("std.types.Object", mem->member))
            if (m->isStatic && !m->paramTypes.empty()) has = true;
        if (has) {
            auto cls = std::make_unique<NameExpr>();
            cls->name = "std.types.Object";
            cls->line = mem->line;
            cls->col = mem->col;
            e.args.insert(e.args.begin(), std::move(mem->object));
            mem->object = std::move(cls);
            return checkCall(e);
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
        error(mem->line, mem->col, (propertyOf(mem->member).empty() ? "method '" : "property accessor '") +
                                       memberDisplayName(mem->member) + "' is " +
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
    // `-> Self`: the receiver's own class (its static type here).
    TypeRef result = mi->returnType;
    if (mi->returnsSelf) result = mi->returnType.isNullable() ? TypeRef::nullable(objType) : objType;
    if (mem->optional) checkOptionalResult(e.line, e.col, result, /*allowVoid=*/true);
    return result;
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

// `obj.method` as a value: `fn(a, b) => obj.method(a, b)`. The object is read
// when the function is called, so it must be a variable, `self`, or fields of
// those. An overloaded method needs the expected function type to pick one.
TypeRef TypeChecker::bindMethod(MemberExpr &e, const TypeRef &objType, const TypeRef *want) {
    std::function<bool(const Expr *)> pure = [&](const Expr *x) {
        if (dynamic_cast<const NameExpr *>(x) || dynamic_cast<const SelfExpr *>(x) || dynamic_cast<const SuperExpr *>(x)) return true;
        if (auto *m = dynamic_cast<const MemberExpr *>(x)) return pure(m->object.get());
        return false;
    };
    if (!pure(e.object.get())) {
        error(e.line, e.col, "to use '" + e.member + "' as a value, put the object in a variable first");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    std::vector<const MethodInfo *> cands;
    for (const MethodInfo *m : reg_->findMethodsBySource(objType.name, e.member))
        if (!m->isStatic) cands.push_back(m);
    const MethodInfo *mi = cands.size() == 1 ? cands[0] : nullptr;
    if (!mi && want && want->isFunction())
        for (const MethodInfo *m : cands)
            if (m->paramTypes == want->fnParams) mi = m;
    if (!mi) {
        error(e.line, e.col, "method '" + e.member + "' is overloaded; give the variable a function type to pick one");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    auto lam = std::make_unique<LambdaExpr>();
    lam->line = e.line;
    lam->col = e.col;
    lam->isArrow = true;
    auto call = std::make_unique<CallExpr>();
    call->line = e.line;
    call->col = e.col;
    auto callee = std::make_unique<MemberExpr>();
    callee->line = e.line;
    callee->col = e.col;
    callee->object = cloneExpression(e.object.get());
    callee->member = e.member;
    call->callee = std::move(callee);
    for (size_t i = 0; i < mi->paramTypes.size(); ++i) {
        Param p;
        p.name = "arg__" + std::to_string(i);
        p.type = mi->paramTypes[i];
        lam->params.push_back(p);
        lam->paramTyped.push_back(true);
        auto n = std::make_unique<NameExpr>();
        n->name = p.name;
        n->line = e.line;
        n->col = e.col;
        call->args.push_back(std::move(n));
    }
    lam->exprBody = std::move(call);
    e.bound = std::move(lam);
    return checkExpr(*e.bound);
}

// The classes in std.types whose static methods a built-in value of type `t`
// takes as methods, the most specific first (a byte's are Byte's, then
// Integer's; a `T?`'s are Null's; an `any`'s Object's); none for other types
// (an object takes Object's only for names its class lacks).
std::vector<std::string> TypeChecker::valueMethodClasses(const TypeRef &t) const {
    switch (t.kind) {
        case TypeRef::Kind::Int:
            if (t.bits == 8 && t.isUnsigned) return {"std.types.Byte", "std.types.Integer"};
            return {"std.types.Integer"};
        case TypeRef::Kind::Float: return {"std.types.Float"};
        case TypeRef::Kind::String: return {"std.types.String"};
        case TypeRef::Kind::Bool: return {"std.types.Bool"};
        case TypeRef::Kind::Nullable: return {"std.types.Null"};
        case TypeRef::Kind::Any: return {"std.types.Object"};
        default: return {};
    }
}

// Inside lambdas, `self` (used directly or through `super`) is captured like
// any other outer variable.
void TypeChecker::captureSelf() {
    TypeRef t = TypeRef::named(currentClass_);
    for (auto &ctx : lambdaStack_) {
        bool have = false;
        for (const auto &c : ctx.lam->captures) if (c.name == "self") have = true;
        if (!have) ctx.lam->captures.push_back({"self", t});
    }
}

TypeRef TypeChecker::checkMember(MemberExpr &e) {
    const TypeRef *expMember = expected_; // a function type picks an overloaded method value
    expected_ = nullptr;
    // `EnumType.Member` is a constant, not a field access on a value.
    if (auto enumTy = tryEnumValue(e)) return *enumTy;
    if (auto sf = tryStaticField(e)) return *sf;
    if (auto sp = tryPropertyRead(e, nullptr)) return *sp; // `Class.name` through a static getter
    // `Integer.compare` as a value: `fn(a, b) => Integer.compare(a, b)` (of overloads,
    // the one the expected function type takes).
    if (auto chain = flattenNames(&e); chain && chain->size() >= 2 && lookupLocal((*chain)[0]) == nullptr) {
        std::string clsFq = reg_->resolveType(joinDots(*chain, chain->size() - 1), currentNs_);
        if (!clsFq.empty() && reg_->isClass(clsFq)) {
            std::vector<const MethodInfo *> cands;
            for (const MethodInfo *m : reg_->findMethodsBySource(clsFq, e.member))
                if (m->isStatic) cands.push_back(m);
            if (!cands.empty()) {
                const MethodInfo *mi = cands.size() == 1 ? cands[0] : nullptr;
                if (!mi && expMember && expMember->isFunction())
                    for (const MethodInfo *m : cands)
                        if (m->paramTypes == expMember->fnParams) mi = m;
                if (!mi) {
                    error(e.line, e.col, "static method '" + e.member + "' is overloaded; give the variable a function type to pick one");
                    return TypeRef::prim(TypeRef::Kind::Error);
                }
                auto lam = std::make_unique<LambdaExpr>();
                lam->line = e.line;
                lam->col = e.col;
                lam->isArrow = true;
                auto call = std::make_unique<CallExpr>();
                call->line = e.line;
                call->col = e.col;
                auto callee = std::make_unique<MemberExpr>();
                callee->line = e.line;
                callee->col = e.col;
                auto owner = std::make_unique<NameExpr>();
                owner->name = clsFq;
                owner->line = e.line;
                owner->col = e.col;
                callee->object = std::move(owner);
                callee->member = e.member;
                call->callee = std::move(callee);
                for (size_t i = 0; i < mi->paramTypes.size(); ++i) {
                    Param p;
                    p.name = "arg__" + std::to_string(i);
                    p.type = mi->paramTypes[i];
                    lam->params.push_back(p);
                    lam->paramTyped.push_back(true);
                    auto n = std::make_unique<NameExpr>();
                    n->name = p.name;
                    n->line = e.line;
                    n->col = e.col;
                    call->args.push_back(std::move(n));
                }
                lam->exprBody = std::move(call);
                e.bound = std::move(lam);
                return checkExpr(*e.bound);
            }
        }
    }
    // `super.count` for a static field: the parent's (or an ancestor's).
    if (dynamic_cast<SuperExpr *>(e.object.get()))
        if (const ClassInfo *ci = currentClass_.empty() ? nullptr : reg_->cls(currentClass_); ci && !ci->baseClass.empty())
            if (const GlobalInfo *gi = reg_->findStaticField(ci->baseClass, e.member)) {
                if (!checkAccess(gi->access, gi->ownerClass))
                    error(e.line, e.col, "static field '" + e.member + "' is " + accessName(gi->access) + " in '" + gi->ownerClass + "'");
                e.kind = MemberKind::StaticField;
                e.qualified = gi->cName;
                return gi->type;
            }
    TypeRef objType = checkExpr(*e.object);
    if (objType.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    // `arr.length` is the fixed length of an array, as an int.
    if (objType.isArray()) {
        if (e.member == "length") return TypeRef::prim(TypeRef::Kind::Int);
        error(e.line, e.col,
              "an array has no member '" + e.member + "' (only 'length')");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    // `s.length` is a string's length in bytes (UTF-8), as an int.
    if (objType.kind == TypeRef::Kind::String) {
        if (e.member == "length") return TypeRef::prim(TypeRef::Kind::Int);
        error(e.line, e.col, "a string has no member '" + e.member + "' (only 'length'; see Str for the rest)");
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
    if (objType.isNamed() && reg_->isInterface(objType.name))
        if (auto p = tryPropertyRead(e, &objType)) return *p;
    if (!objType.isNamed() || !reg_->isClass(objType.name)) {
        error(e.line, e.col, "cannot access member '" + e.member +
                                 "' of a value of type " + typeRefName(objType) + maybeNullHint(objType));
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    const FieldInfo *f = reg_->findField(objType.name, e.member);
    if (!f) {
        if (auto p = tryPropertyRead(e, &objType)) return *p;
        if (reg_->findMethod(objType.name, e.member))
            return bindMethod(e, objType, expMember);
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
