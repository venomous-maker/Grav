#include "sema/typechecker.h"

namespace grav {

namespace {

// Method name implementing an overloadable binary operator ("" if none).
const char *opMethodName(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "op_add";
        case BinaryOp::Sub: return "op_sub";
        case BinaryOp::Mul: return "op_mul";
        case BinaryOp::Div: return "op_div";
        case BinaryOp::Mod: return "op_mod";
        case BinaryOp::Eq: return "op_eq";
        case BinaryOp::NotEq: return "op_ne";
        case BinaryOp::Less: return "op_lt";
        case BinaryOp::LessEq: return "op_le";
        case BinaryOp::Greater: return "op_gt";
        case BinaryOp::GreaterEq: return "op_ge";
        default: return "";
    }
}

// Does this statement unconditionally leave the enclosing function (return,
// throw, an infinite loop, exit/panic)? Conservative: "false" means "maybe not".
bool terminates(const Stmt &s);

bool blockTerminates(const Block &b) {
    for (const auto &st : b.statements)
        if (terminates(*st)) return true;
    return false;
}

// Does a `break` inside this block exit *this* loop (not a nested one)?
bool hasOwnBreak(const Block &b);
bool stmtHasOwnBreak(const Stmt &s) {
    if (dynamic_cast<const BreakStmt *>(&s)) return true;
    if (auto *x = dynamic_cast<const BlockStmt *>(&s)) return hasOwnBreak(x->block);
    if (auto *x = dynamic_cast<const IfStmt *>(&s))
        return hasOwnBreak(x->thenBlock) || (x->elseStmt && stmtHasOwnBreak(*x->elseStmt));
    if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) {
        for (const auto &c : x->cases) if (hasOwnBreak(c.body)) return true;
        return x->hasDefault && hasOwnBreak(x->defaultBody);
    }
    if (auto *x = dynamic_cast<const TryStmt *>(&s)) {
        for (const auto &c : x->catches) if (hasOwnBreak(c.block)) return true;
        return hasOwnBreak(x->tryBlock) || hasOwnBreak(x->finallyBlock);
    }
    return false; // nested loops own their breaks
}
bool hasOwnBreak(const Block &b) {
    for (const auto &st : b.statements)
        if (stmtHasOwnBreak(*st)) return true;
    return false;
}

bool isTrueLiteral(const Expr *e) {
    auto *b = dynamic_cast<const BoolLiteralExpr *>(e);
    return b && b->value;
}

bool terminates(const Stmt &s) {
    if (dynamic_cast<const ReturnStmt *>(&s) || dynamic_cast<const ThrowStmt *>(&s))
        return true;
    if (auto *x = dynamic_cast<const ExprStmt *>(&s)) {
        // Inline C may return on its own; exit()/panic() never come back.
        if (dynamic_cast<const CBlockExpr *>(x->expr.get())) return true;
        if (auto *c = dynamic_cast<const CallExpr *>(x->expr.get()))
            return c->kind == CallKind::Builtin &&
                   (c->targetName == "exit" || c->targetName == "panic");
        return false;
    }
    if (auto *x = dynamic_cast<const BlockStmt *>(&s)) return blockTerminates(x->block);
    if (auto *x = dynamic_cast<const IfStmt *>(&s))
        return x->elseStmt && blockTerminates(x->thenBlock) && terminates(*x->elseStmt);
    if (auto *x = dynamic_cast<const WhileStmt *>(&s))
        return isTrueLiteral(x->cond.get()) && !hasOwnBreak(x->body);
    if (auto *x = dynamic_cast<const DoWhileStmt *>(&s))
        return (blockTerminates(x->body) || isTrueLiteral(x->cond.get())) && !hasOwnBreak(x->body);
    if (auto *x = dynamic_cast<const ForStmt *>(&s))
        return !x->cond && !hasOwnBreak(x->body);
    if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) {
        if (!x->hasDefault || !blockTerminates(x->defaultBody)) return false;
        for (const auto &c : x->cases) if (!blockTerminates(c.body)) return false;
        return true;
    }
    if (auto *x = dynamic_cast<const TryStmt *>(&s)) {
        if (x->hasFinally && blockTerminates(x->finallyBlock)) return true;
        if (!blockTerminates(x->tryBlock)) return false;
        for (const auto &c : x->catches) if (!blockTerminates(c.block)) return false;
        return true;
    }
    return false;
}

} // namespace

void TypeChecker::checkFallthrough(const Block &body, const TypeRef &ret, int line, int col,
                                   const std::string &what) {
    if (ret.isVoid() || ret.isError()) return;
    if (!blockTerminates(body))
        error(line, col, what + " may reach the end without returning a value of type " +
                             typeRefName(ret));
}

// "(key: long) -> bool": how a callable is declared, for error messages.
std::string signatureText(const std::vector<std::string> &names, const std::vector<TypeRef> &types,
                          const TypeRef &ret, bool variadic) {
    std::string s = "(";
    for (size_t i = 0; i < types.size(); ++i) {
        if (i) s += ", ";
        if (i < names.size() && !names[i].empty()) s += names[i] + ": ";
        if (variadic && i + 1 == types.size()) s += "...";
        s += typeRefName(types[i]);
    }
    s += ")";
    if (!ret.isVoid() && !ret.isError()) s += " -> " + typeRefName(ret);
    return s;
}

// "method 'A.m'" + "(x: int)" -> "method 'A.m(x: int)'".
std::string withSignature(const std::string &what, const std::string &sig) {
    if (!what.empty() && what.back() == '\'') return what.substr(0, what.size() - 1) + sig + "'";
    return what + " " + sig;
}

const MethodInfo *TypeChecker::resolveMethodOverload(const std::vector<const MethodInfo *> &cands,
                                                     std::vector<ExprPtr> &args, int line, int col,
                                                     const std::string &what) {
    if (cands.empty()) return nullptr;
    auto sigOf = [](const MethodInfo *m) {
        return signatureText(m->paramNames, m->paramTypes, m->returnType, false);
    };
    if (cands.size() == 1) {
        checkArgs(args, cands[0]->paramTypes, line, col, withSignature(what, sigOf(cands[0])));
        return cands[0];
    }
    std::vector<TypeRef> argTypes;
    for (auto &a : args) argTypes.push_back(checkExpr(*a));
    for (const auto &t : argTypes) if (t.isError()) return nullptr;
    const MethodInfo *exact = nullptr, *assignable = nullptr;
    int nAssignable = 0;
    for (const MethodInfo *m : cands) {
        if (m->paramTypes.size() != argTypes.size()) continue;
        bool ok = true, same = true;
        for (size_t i = 0; ok && i < argTypes.size(); ++i) {
            ok = isAssignable(argTypes[i], m->paramTypes[i]);
            same = same && argTypes[i] == m->paramTypes[i];
        }
        if (!ok) continue;
        if (same) exact = m;
        assignable = m;
        nAssignable++;
    }
    const MethodInfo *mi = exact ? exact : (nAssignable == 1 ? assignable : nullptr);
    if (!mi) {
        std::string got;
        for (size_t i = 0; i < argTypes.size(); ++i)
            got += (i ? ", " : "") + typeRefName(argTypes[i]);
        std::string options;
        for (const MethodInfo *m : cands) options += "\n    " + sigOf(m);
        error(line, col, std::string(nAssignable > 1 ? "ambiguous call to " : "no overload of ") +
                             what + " matches (" + got + "); candidates are:" + options);
        return nullptr;
    }
    validateArgTypes(args, argTypes, mi->paramTypes, false, line, col, withSignature(what, sigOf(mi)));
    return mi;
}

int TypeChecker::resolveCtor(const std::string &classFq, std::vector<ExprPtr> &args, int line,
                             int col, const std::string &what) {
    const ClassInfo *ci = reg_->cls(classFq);
    if (!ci || ci->ctors.empty()) { // implicit no-argument constructor
        checkArgs(args, {}, line, col, what);
        return 0;
    }
    std::vector<MethodInfo> sigs(ci->ctors.size());
    std::vector<const MethodInfo *> cands;
    for (size_t i = 0; i < ci->ctors.size(); ++i) {
        for (const auto &p : ci->ctors[i]->params) {
            sigs[i].paramTypes.push_back(p.type);
            sigs[i].paramNames.push_back(p.name);
        }
        sigs[i].returnType = TypeRef::prim(TypeRef::Kind::Void);
        cands.push_back(&sigs[i]);
    }
    const MethodInfo *mi = resolveMethodOverload(cands, args, line, col, what);
    return mi ? static_cast<int>(mi - sigs.data()) : -1;
}

const MethodInfo *TypeChecker::pickByOperand(const std::vector<const MethodInfo *> &cands,
                                             const TypeRef &operand) const {
    const MethodInfo *assignable = nullptr;
    for (const MethodInfo *m : cands) {
        if (m->isStatic || m->paramTypes.empty()) continue;
        if (m->paramTypes[0] == operand) return m;
        if (!assignable && isAssignable(operand, m->paramTypes[0])) assignable = m;
    }
    if (assignable) return assignable;
    for (const MethodInfo *m : cands) if (!m->isStatic) return m; // report the mismatch
    return nullptr;
}

std::optional<TypeRef> TypeChecker::tryOperatorOverload(BinaryExpr &e, const TypeRef &lt,
                                                        const TypeRef &rt) {
    if (!lt.isNamed() || !reg_->isClass(lt.name)) return std::nullopt;
    if (rt.kind == TypeRef::Kind::Null) return std::nullopt; // `obj == null` stays a ref test
    std::string name = opMethodName(e.op);
    if (name.empty()) return std::nullopt;
    const MethodInfo *mi = pickByOperand(reg_->findMethodsBySource(lt.name, name), rt);
    bool negate = false;
    if (!mi && e.op == BinaryOp::NotEq) { // `!=` falls back to `!(a == b)`
        mi = pickByOperand(reg_->findMethodsBySource(lt.name, "op_eq"), rt);
        negate = mi != nullptr;
    }
    if (!mi) return std::nullopt;
    if (mi->isStatic || mi->paramTypes.size() != 1) {
        error(e.line, e.col, "operator method '" + name + "' must be an instance method "
                             "taking exactly one parameter");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!checkAccess(mi->access, mi->definingClass))
        error(e.line, e.col, "operator method of '" + lt.name + "' is not accessible here");
    if (!isAssignable(rt, mi->paramTypes[0]))
        error(e.right->line, e.right->col,
              std::string("operator '") + binaryOpSymbol(e.op) + "' on " + typeRefName(lt) +
                  " expects a right operand of type " + typeRefName(mi->paramTypes[0]) +
                  ", got " + typeRefName(rt));
    e.overloadMethod = mi->name;
    e.overloadClass = lt.name;
    e.overloadNegate = negate;
    if ((isComparison(e.op)) && mi->returnType.kind != TypeRef::Kind::Bool) {
        error(e.line, e.col, "comparison operator method '" + mi->name + "' must return bool");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return mi->returnType;
}

std::optional<TypeRef> TypeChecker::tryIndexOverload(IndexExpr &e, const TypeRef &base) {
    if (!base.isNamed() || !reg_->isClass(base.name)) return std::nullopt;
    auto cands = reg_->findMethodsBySource(base.name, "op_index");
    if (cands.empty()) {
        error(e.line, e.col, "class '" + base.name + "' has no 'operator[]'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    TypeRef it = checkExpr(*e.index);
    const MethodInfo *mi = pickByOperand(cands, it);
    if (mi->isStatic || mi->paramTypes.size() != 1) {
        error(e.line, e.col, "'operator[]' must be an instance method taking one parameter");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!isAssignable(it, mi->paramTypes[0]))
        error(e.index->line, e.index->col,
              "index of type " + typeRefName(it) + " does not match 'operator[]' parameter " +
                  typeRefName(mi->paramTypes[0]));
    e.overloadClass = base.name;
    e.overloadMethod = mi->name;
    return mi->returnType;
}

bool TypeChecker::isDisplayable(const TypeRef &t) const {
    switch (t.kind) {
        case TypeRef::Kind::Int:
        case TypeRef::Kind::Float:
        case TypeRef::Kind::Bool:
        case TypeRef::Kind::String:
        case TypeRef::Kind::Any:
            return true;
        default: break;
    }
    if (!t.isNamed()) return false;
    if (reg_->isEnum(t.name)) return true;
    const MethodInfo *mi = nullptr;
    if (reg_->isClass(t.name)) mi = reg_->findMethod(t.name, "toString");
    else if (reg_->isInterface(t.name)) mi = reg_->findInterfaceMethod(t.name, "toString");
    return mi && !mi->isStatic && mi->paramTypes.empty() &&
           mi->returnType.kind == TypeRef::Kind::String;
}

void TypeChecker::checkDefer(DeferStmt &s) {
    bool savedDefer = inDefer_;
    int savedLoop = loopDepth_;
    inDefer_ = true;
    loopDepth_ = 0; // break/continue cannot leave a deferred block
    checkBlock(s.body);
    inDefer_ = savedDefer;
    loopDepth_ = savedLoop;
}

void TypeChecker::checkForIn(ForInStmt &s) {
    TypeRef lo = checkExpr(*s.lo);
    pushScope(); // loop variables span the whole loop
    if (s.hi) {
        // Counting loop over a range.
        TypeRef hi = checkExpr(*s.hi);
        if (!lo.isError() && lo.kind != TypeRef::Kind::Int)
            error(s.lo->line, s.lo->col,
                  "a 'for ... in' range bound must be an integer, got " + typeRefName(lo));
        if (!hi.isError() && hi.kind != TypeRef::Kind::Int)
            error(s.hi->line, s.hi->col,
                  "a 'for ... in' range bound must be an integer, got " + typeRefName(hi));
        if (s.step) {
            TypeRef st = checkExpr(*s.step);
            if (!st.isError() && st.kind != TypeRef::Kind::Int)
                error(s.step->line, s.step->col,
                      "a range 'step' must be an integer, got " + typeRefName(st));
            if (auto *lit = dynamic_cast<IntLiteralExpr *>(s.step.get()); lit && lit->value == 0)
                error(s.step->line, s.step->col, "a range 'step' cannot be 0");
        }
        s.iterKind = ForInKind::Range;
        // The counter is as wide as the wider bound (`0..n` with a long n is long).
        s.varType = (lo.kind == TypeRef::Kind::Int && hi.kind == TypeRef::Kind::Int)
                        ? numericPromote(lo, hi)
                        : TypeRef::prim(TypeRef::Kind::Int);
        if (s.varType.numBits() < 32) s.varType = TypeRef::prim(TypeRef::Kind::Int);
    } else if (!lo.isError()) {
        // Element loop over an iterable.
        if (lo.isSlice()) {
            s.iterKind = ForInKind::Slice;
            s.varType = *lo.elem;
        } else if (lo.isArray()) {
            s.iterKind = ForInKind::Array;
            s.varType = *lo.elem;
        } else if (lo.isBinary()) {
            s.iterKind = ForInKind::Bytes;
            s.varType = TypeRef::integer(8, true);
        } else if (lo.isNamed() && reg_->isClass(lo.name)) {
            s.iterKind = ForInKind::Object;
            auto pick = [&](std::initializer_list<const char *> names, size_t nparams)
                -> const MethodInfo * {
                for (const char *n : names) {
                    const MethodInfo *m = reg_->findMethod(lo.name, n);
                    if (m && !m->isStatic && m->paramTypes.size() == nparams) return m;
                }
                return nullptr;
            };
            const MethodInfo *len = pick({"length", "size"}, 0);
            const MethodInfo *get = pick({"get", "at"}, 1);
            if (!len || len->returnType.kind != TypeRef::Kind::Int || !get ||
                get->paramTypes[0].kind != TypeRef::Kind::Int) {
                error(s.lo->line, s.lo->col,
                      "cannot iterate " + typeRefName(lo) +
                          ": a collection needs length()/size() -> int and get(int)/at(int)");
                s.varType = TypeRef::prim(TypeRef::Kind::Error);
            } else {
                s.lenMethod = len->name;
                s.getMethod = get->name;
                s.varType = get->returnType;
            }
        } else {
            error(s.lo->line, s.lo->col,
                  "cannot iterate a value of type " + typeRefName(lo) +
                      " (expected an array, slice, binary, or collection object)");
            s.varType = TypeRef::prim(TypeRef::Kind::Error);
        }
    } else {
        s.varType = TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!s.indexVar.empty())
        declareLocal(s.indexVar, TypeRef::prim(TypeRef::Kind::Int), true, false, s.line, s.col);
    declareLocal(s.var, s.varType, true, false, s.line, s.col);
    loopDepth_++;
    checkBlock(s.body);
    loopDepth_--;
    popScope();
}

} // namespace grav
