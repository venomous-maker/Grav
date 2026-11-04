#include "sema/typechecker.h"
#include "sema/monomorph.h"

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
    bool negate = false, swap = false;
    if (!mi && e.op == BinaryOp::NotEq) { // `!=` falls back to `!(a == b)`
        mi = pickByOperand(reg_->findMethodsBySource(lt.name, "op_eq"), rt);
        negate = mi != nullptr;
    }
    // `>`, `<=`, `>=` follow from operator< when the class does not define them:
    // a > b is b < a, a <= b is !(b < a), a >= b is !(a < b).
    if (!mi && (e.op == BinaryOp::Greater || e.op == BinaryOp::LessEq || e.op == BinaryOp::GreaterEq)) {
        if (e.op == BinaryOp::GreaterEq) {
            mi = pickByOperand(reg_->findMethodsBySource(lt.name, "op_lt"), rt);
            negate = mi != nullptr;
        } else if (rt.isNamed() && reg_->isClass(rt.name)) {
            mi = pickByOperand(reg_->findMethodsBySource(rt.name, "op_lt"), lt);
            swap = mi != nullptr;
            negate = mi != nullptr && e.op == BinaryOp::LessEq;
        }
    }
    if (!mi) return std::nullopt;
    if (mi->isStatic || mi->paramTypes.size() != 1) {
        error(e.line, e.col, "operator method '" + name + "' must be an instance method "
                             "taking exactly one parameter");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!checkAccess(mi->access, mi->definingClass))
        error(e.line, e.col, "operator method of '" + lt.name + "' is not accessible here");
    if (!isAssignable(swap ? lt : rt, mi->paramTypes[0]))
        error(e.right->line, e.right->col,
              std::string("operator '") + binaryOpSymbol(e.op) + "' on " + typeRefName(lt) +
                  " expects a right operand of type " + typeRefName(mi->paramTypes[0]) +
                  ", got " + typeRefName(rt));
    e.overloadMethod = mi->name;
    e.overloadClass = swap ? rt.name : lt.name;
    e.overloadNegate = negate;
    e.overloadSwap = swap;
    if ((isComparison(e.op)) && mi->returnType.kind != TypeRef::Kind::Bool) {
        error(e.line, e.col, "comparison operator method '" + mi->name + "' must return bool");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    return mi->returnType;
}

std::vector<TypeRef> TypeChecker::checkIndexKeys(IndexExpr &e) {
    std::vector<TypeRef> keys{checkExpr(*e.index)};
    for (auto &m : e.more) keys.push_back(checkExpr(*m));
    return keys;
}

const MethodInfo *TypeChecker::pickIndexMethod(const std::vector<const MethodInfo *> &cands,
                                               const std::vector<TypeRef> &keys, size_t extra) const {
    const MethodInfo *best = nullptr;
    int bestExact = -1;
    for (const MethodInfo *m : cands) {
        if (m->isStatic || m->paramTypes.size() != keys.size() + extra) continue;
        int exact = 0;
        bool ok = true;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == m->paramTypes[i]) exact++;
            else if (!isAssignable(keys[i], m->paramTypes[i])) { ok = false; break; }
        }
        if (ok && exact > bestExact) { best = m; bestExact = exact; }
    }
    return best;
}

namespace {
std::string indexList(const std::vector<TypeRef> &keys) {
    std::string s;
    for (size_t i = 0; i < keys.size(); ++i) s += (i ? ", " : "") + typeRefName(keys[i]);
    return s;
}
} // namespace

std::optional<TypeRef> TypeChecker::tryIndexOverload(IndexExpr &e, const TypeRef &base) {
    if (!base.isNamed() || !reg_->isClass(base.name)) return std::nullopt;
    auto cands = reg_->findMethodsBySource(base.name, "op_index");
    std::vector<TypeRef> keys = checkIndexKeys(e);
    if (cands.empty()) {
        if (!reg_->findMethodsBySource(base.name, "op_index_set").empty())
            error(e.line, e.col, "class '" + base.name + "' has 'operator[]=' but no 'operator[]': its "
                                 "elements can be assigned but not read");
        else
            error(e.line, e.col, "class '" + base.name + "' has no 'operator[]'");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    for (const auto &k : keys) if (k.isError()) return TypeRef::prim(TypeRef::Kind::Error);
    const MethodInfo *mi = pickIndexMethod(cands, keys, 0);
    if (!mi) {
        std::string have;
        for (const MethodInfo *c : cands) {
            if (c->isStatic) continue;
            have += std::string(have.empty() ? "" : ", ") + "[" + indexList(c->paramTypes) + "]";
        }
        error(e.line, e.col, "no 'operator[]' of '" + base.name + "' takes [" + indexList(keys) +
                                 "] (it has " + (have.empty() ? "none usable" : have) + ")");
        return TypeRef::prim(TypeRef::Kind::Error);
    }
    e.overloadClass = base.name;
    e.overloadMethod = mi->name;
    return mi->returnType;
}

// `obj[k...] = v` and `obj[k...] op= v` on a class: operator[]= (and, for the
// compound form, operator[] to read the old value).
// `xs[i].f = v` (also `xs[i].a.b op= v`) where `xs[i]` is an operator[] call:
// the element is a copy, so the store goes to the copy, which is written back:
//   { let base = xs  let key = i  let elem = base[key]  elem.f = v  base[key] = elem }
bool TypeChecker::rewriteElementStore(AssignStmt &s) {
    auto *top = dynamic_cast<MemberExpr *>(s.target.get());
    if (!top) return false;
    MemberExpr *inner = top;
    while (auto *m = dynamic_cast<MemberExpr *>(inner->object.get())) inner = m;
    auto *ix = dynamic_cast<IndexExpr *>(inner->object.get());
    if (!ix) return false;
    // Only containers with operator[]= qualify: probe the base's type on a copy,
    // leaving no diagnostics behind (the real check happens on the rewrite).
    size_t nErr = errors_.size(), nWarn = warnings_.size();
    ExprPtr probe = cloneExpression(ix->base.get());
    TypeRef bt = checkExpr(*probe);
    errors_.erase(errors_.begin() + static_cast<long>(nErr), errors_.end());
    warnings_.erase(warnings_.begin() + static_cast<long>(nWarn), warnings_.end());
    if (!bt.isNamed() || !reg_->isClass(bt.name) || reg_->findMethodsBySource(bt.name, "op_index_set").empty())
        return false;

    int line = s.line, col = s.col, n = matchCounter_++;
    auto name = [&](const std::string &v) {
        auto e = std::make_unique<NameExpr>();
        e->name = v;
        e->line = line;
        e->col = col;
        return e;
    };
    auto block = std::make_unique<BlockStmt>();
    block->line = line;
    block->col = col;
    auto let = [&](const std::string &v, ExprPtr init) {
        auto l = std::make_unique<LetStmt>();
        l->name = v;
        l->init = std::move(init);
        l->line = line;
        l->col = col;
        block->block.statements.push_back(std::move(l));
    };
    std::string tag = std::to_string(n);
    std::string baseVar = "elem__base" + tag, elemVar = "elem__value" + tag;
    let(baseVar, std::move(ix->base));
    std::vector<std::string> keyVars;
    std::vector<ExprPtr *> keys{&ix->index};
    for (auto &m : ix->more) keys.push_back(&m);
    for (size_t k = 0; k < keys.size(); ++k) {
        keyVars.push_back("elem__key" + tag + "_" + std::to_string(k));
        let(keyVars.back(), std::move(*keys[k]));
    }
    auto access = [&]() {
        auto a = std::make_unique<IndexExpr>();
        a->line = ix->line;
        a->col = ix->col;
        a->base = name(baseVar);
        a->index = name(keyVars[0]);
        for (size_t k = 1; k < keyVars.size(); ++k) a->more.push_back(name(keyVars[k]));
        return a;
    };
    let(elemVar, access());
    inner->object = name(elemVar);
    auto store = std::make_unique<AssignStmt>();
    store->line = line;
    store->col = col;
    store->target = std::move(s.target);
    store->value = std::move(s.value);
    store->isCompound = s.isCompound;
    store->compoundOp = s.compoundOp;
    block->block.statements.push_back(std::move(store));
    auto back = std::make_unique<AssignStmt>();
    back->line = line;
    back->col = col;
    back->target = access();
    back->value = name(elemVar);
    block->block.statements.push_back(std::move(back));
    checkBlock(block->block);
    matchReplacement_ = std::move(block);
    return true;
}

void TypeChecker::checkIndexAssign(AssignStmt &s, IndexExpr &ix, const TypeRef &bt) {
    std::vector<TypeRef> keys = checkIndexKeys(ix);
    for (const auto &k : keys) if (k.isError()) return;
    auto sets = reg_->findMethodsBySource(bt.name, "op_index_set");
    if (sets.empty()) {
        if (!reg_->findMethodsBySource(bt.name, "op_index").empty())
            error(s.line, s.col, "class '" + bt.name + "' has 'operator[]' but no 'operator[]=': its "
                                 "elements are read-only");
        else
            error(s.line, s.col, "class '" + bt.name + "' has no 'operator[]=' for element assignment");
        return;
    }
    const MethodInfo *mi = pickIndexMethod(sets, keys, 1);
    if (!mi) {
        error(s.line, s.col, "no 'operator[]=' of '" + bt.name + "' takes [" + indexList(keys) + "] and a value");
        return;
    }
    const TypeRef &elemT = mi->paramTypes.back();
    TypeRef valueType;
    if (s.isCompound) {
        // obj[k] op= v  ==>  obj[k] = obj[k] op v, with obj and k evaluated once.
        if (reg_->findMethodsBySource(bt.name, "op_index").empty()) {
            error(s.line, s.col, "'" + std::string(binaryOpSymbol(s.compoundOp)) + "=' on an element of '" + bt.name +
                                     "' needs 'operator[]' to read it, and the class has only 'operator[]='");
            return;
        }
        ExprPtr copy = cloneExpression(&ix);
        Expr *read = copy.get();
        auto bin = std::make_unique<BinaryExpr>();
        bin->line = s.value->line;
        bin->col = s.value->col;
        bin->op = s.compoundOp;
        bin->left = std::move(copy);
        bin->right = std::move(s.value);
        s.value = std::move(bin);
        s.isCompound = false;
        s.readBack = read;
        valueType = checkExpr(*s.value);
    } else {
        valueType = checkExprExpect(*s.value, elemT);
    }
    if (!valueType.isError() && !isAssignable(valueType, elemT))
        error(s.value->line, s.value->col, "cannot assign " + typeRefName(valueType) +
                                               " through 'operator[]=' expecting " + typeRefName(elemT));
    ix.overloadClass = bt.name;
    ix.overloadMethod = mi->name;
    ix.type = TypeRef::prim(TypeRef::Kind::Void);
}

// Every value has a text form (see codegen_display.cpp); only `void` has none.
bool TypeChecker::isDisplayable(const TypeRef &t) const {
    return !t.isVoid() && !t.isError();
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
        } else if (lo.kind == TypeRef::Kind::String) {
            s.iterKind = ForInKind::Chars; // each UTF-8 character, as a string
            s.varType = TypeRef::prim(TypeRef::Kind::String);
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
                      " (expected an array, slice, string, binary, or collection object)");
            s.varType = TypeRef::prim(TypeRef::Kind::Error);
        }
    } else {
        s.varType = TypeRef::prim(TypeRef::Kind::Error);
    }
    if (!s.indexVar.empty())
        declareLocal(s.indexVar, TypeRef::prim(TypeRef::Kind::Int), true, false, s.line, s.col);
    declareLocal(s.var, s.varType, true, false, s.line, s.col);
    enterLoop(s.label, s.line, s.col);
    checkBlock(s.body);
    leaveLoop();
    popScope();
}

} // namespace grav
