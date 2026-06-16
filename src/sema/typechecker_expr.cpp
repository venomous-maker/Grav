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
bool isPrintable(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int:
        case TypeRef::Kind::Float:
        case TypeRef::Kind::Bool:
        case TypeRef::Kind::String:
            return true;
        default:
            return false;
    }
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
    for (auto &a : args) argTypes.push_back(checkExpr(*a));
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
        TypeRef at = checkExpr(*args[i]);
        const TypeRef &want = i < fixed ? params[i] : elem;
        if (!isAssignable(at, want))
            error(args[i]->line, args[i]->col,
                  "argument " + std::to_string(i + 1) + " to " + what + " expects " +
                      typeRefName(want) + ", but got " + typeRefName(at));
    }
}

TypeRef TypeChecker::checkExpr(Expr &expr) {
    TypeRef t = TypeRef::prim(TypeRef::Kind::Error);
    if (dynamic_cast<IntLiteralExpr *>(&expr)) t = TypeRef::prim(TypeRef::Kind::Int);
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
        }
    } else if (auto *e = dynamic_cast<NameExpr *>(&expr)) {
        LocalVar *local = lookupLocal(e->name);
        if (local) {
            local->used = true;
            t = local->type;
        } else if (std::string gfq = reg_->resolveGlobal(e->name, currentNs_); !gfq.empty()) {
            const GlobalInfo *gi = reg_->global(gfq);
            e->resolvedGlobal = gi->cName;
            t = gi->type;
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
    }
    expr.type = t;
    return t;
}

TypeRef TypeChecker::checkBinary(BinaryExpr &e) {
    TypeRef lt = checkExpr(*e.left);
    TypeRef rt = checkExpr(*e.right);
    if (lt.isError() || rt.isError()) return TypeRef::prim(TypeRef::Kind::Error);

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
            return x.kind == TypeRef::Kind::Null || x.isPointer() ||
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
        if (lt != rt) {
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires matching types, but got " +
                                     typeRefName(lt) + " and " + typeRefName(rt) +
                                     " (use int()/float() to convert)");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        // '%', bitwise and shift operators are integer-only in C.
        if (isIntOnly(e.op) && lt.kind != TypeRef::Kind::Int) {
            error(e.line, e.col, std::string("operator '") + sym +
                                     "' requires int operands, but got " +
                                     typeRefName(lt));
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        return lt;
    }

    bool ordered = e.op != BinaryOp::Eq && e.op != BinaryOp::NotEq;
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
    requireBool(*e.cond, "a ternary condition");
    TypeRef a = checkExpr(*e.thenExpr);
    TypeRef b = checkExpr(*e.elseExpr);
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
        TypeRef *inner = &e.target;
        while (inner->isPointer() && inner->elem) inner = inner->elem.get();
        if (inner->isNamed()) {
            std::string fq = reg_->resolveType(inner->name, currentNs_);
            if (!fq.empty()) *inner = TypeRef::named(fq);
        }
        e.operand->type = e.target;
        return e.target;
    }
    TypeRef src = checkExpr(*e.operand);
    // Canonicalize the target (resolving named parts and expanding aliases, looking
    // through any pointer wrappers like `Point*`).
    {
        bool ok = true;
        TypeRef c = reg_->resolveCanonical(e.target, currentNs_, ok);
        if (!ok) {
            error(e.line, e.col, "unknown type '" + typeRefName(e.target) + "' in cast");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        e.target = c;
    }
    if (src.isError()) return e.target;

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
            if (src.kind == TypeRef::Kind::String) return e.target;
            break;
        case TypeRef::Kind::Named: {
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
    if (!inAsync_) {
        error(e.line, e.col, "'await' is only allowed inside an 'async fn'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
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
            ok = src.kind == TypeRef::Kind::String;
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
    std::string fq = reg_->resolveType(e.className, currentNs_);
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
    std::vector<TypeRef> params;
    if (ci->hasConstructor) {
        for (auto &p : ci->constructor->params) params.push_back(p.type);
    }
    checkArgs(e.args, params, e.line, e.col, "constructor of '" + fq + "'");
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

TypeRef TypeChecker::checkCall(CallExpr &e) {
    // Free function or builtin: callee is a bare name.
    if (auto *name = dynamic_cast<NameExpr *>(e.callee.get())) {
        if (name->name == "print") {
            e.kind = CallKind::Builtin;
            e.targetName = "print";
            std::vector<TypeRef> argTypes;
            for (auto &a : e.args) argTypes.push_back(checkExpr(*a));
            if (argTypes.size() != 1) {
                error(e.line, e.col, "print expects exactly 1 argument");
            } else if (!isPrintable(argTypes[0])) {
                error(e.args[0]->line, e.args[0]->col,
                      "print cannot display a value of type " + typeRefName(argTypes[0]));
            }
            return TypeRef::prim(TypeRef::Kind::Void);
        }
        if (name->name == "typename") {
            e.kind = CallKind::Builtin;
            e.targetName = "typename";
            if (e.args.size() != 1) {
                error(e.line, e.col, "typename expects exactly 1 argument");
            } else {
                TypeRef t = checkExpr(*e.args[0]);
                if (!t.isError() && !(t.isNamed() && reg_->isClass(t.name)))
                    error(e.args[0]->line, e.args[0]->col,
                          "typename expects a class instance, got " + typeRefName(t));
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
                bool ok = isPrintable(t) || (t.isNamed() && reg_->isEnum(t.name));
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
        if (fnFq.empty()) {
            error(e.line, e.col, "unknown function '" + name->name + "'");
            for (auto &a : e.args) checkExpr(*a);
            return TypeRef::prim(TypeRef::Kind::Error);
        }
        const FunctionInfo *fi = reg_->func(fnFq);
        e.kind = CallKind::FreeFunction;
        e.targetName = fnFq;
        if (fi->isVariadic)
            checkVariadicArgs(e.args, fi->paramTypes, e.line, e.col, "function '" + fnFq + "'");
        else
            checkArgs(e.args, fi->paramTypes, e.line, e.col, "function '" + fnFq + "'");
        return fi->isAsync ? TypeRef::future(fi->returnType) : fi->returnType;
    }

    auto *mem = dynamic_cast<MemberExpr *>(e.callee.get());
    if (!mem) {
        error(e.line, e.col, "this expression is not callable");
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
            const MethodInfo *mi = reg_->findMethod(clsFq, last);
            if (mi && mi->isStatic) {
                mem->kind = MemberKind::StaticTarget;
                mem->qualified = clsFq;
                e.kind = CallKind::StaticMethod;
                e.targetName = clsFq;
                e.methodName = last;
                e.ownerClass = mi->definingClass;
                if (!checkAccess(mi->access, mi->definingClass))
                    error(e.line, e.col, "static method '" + last + "' is " +
                                             accessName(mi->access));
                checkArgs(e.args, mi->paramTypes, e.line, e.col,
                          "static method '" + clsFq + "." + last + "'");
                return mi->returnType;
            }
            if (mi && !mi->isStatic) {
                error(e.line, e.col, "method '" + last + "' is not static; call it on an instance");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
        }
        std::string fnFq = reg_->resolveFunc(joinDots(*chain, chain->size()), currentNs_);
        if (!fnFq.empty()) {
            const FunctionInfo *fi = reg_->func(fnFq);
            e.kind = CallKind::FreeFunction;
            e.targetName = fnFq;
            if (fi->isVariadic)
                checkVariadicArgs(e.args, fi->paramTypes, e.line, e.col, "function '" + fnFq + "'");
            else
                checkArgs(e.args, fi->paramTypes, e.line, e.col, "function '" + fnFq + "'");
            return fi->isAsync ? TypeRef::future(fi->returnType) : fi->returnType;
        }
        error(e.line, e.col, "unknown function or static method '" +
                                 joinDots(*chain, chain->size()) + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }

    // Instance method: dispatch on the object's class via its vtable.
    TypeRef objType = checkExpr(*mem->object);
    if (objType.isError()) return TypeRef::prim(TypeRef::Kind::Error);

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
    const MethodInfo *mi = reg_->findMethod(objType.name, mem->member);
    if (!mi || mi->isStatic) {
        error(mem->line, mem->col, "type '" + objType.name + "' has no method '" +
                                       mem->member + "'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!checkAccess(mi->access, mi->definingClass))
        error(mem->line, mem->col, "method '" + mem->member + "' is " +
                                       accessName(mi->access) + " in '" +
                                       mi->definingClass + "'");

    // Find the vtable slot owner for the static receiver type.
    std::string slotOwner = objType.name;
    if (const ClassInfo *ci = reg_->cls(objType.name)) {
        for (const auto &s : ci->slots)
            if (s.name == mem->member) { slotOwner = s.slotOwner; break; }
    }
    mem->kind = MemberKind::MethodRef;
    mem->ownerClass = objType.name;
    e.kind = CallKind::InstanceMethod;
    e.methodName = mem->member;
    e.ownerClass = objType.name;
    e.slotOwner = slotOwner;
    checkArgs(e.args, mi->paramTypes, e.line, e.col,
              "method '" + objType.name + "." + mem->member + "'");
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
