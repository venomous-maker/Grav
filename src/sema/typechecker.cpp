#include "sema/typechecker.h"
#include "sema/monomorph.h"

#include <algorithm>

namespace grav {

void TypeChecker::error(int line, int col, const std::string &msg) {
    errors_.emplace_back("type", line, col, msg);
}

void TypeChecker::warn(int line, int col, const std::string &msg) {
    warnings_.emplace_back("warning", line, col, msg);
}

void TypeChecker::pushScope() { scopes_.emplace_back(); }

void TypeChecker::popScope() {
    for (auto &[name, v] : scopes_.back()) {
        if (!v.used && !v.isParam) {
            warn(v.line, v.col, "unused variable '" + name + "'");
        }
    }
    scopes_.pop_back();
}

void TypeChecker::declareLocal(const std::string &name, const TypeRef &type,
                               bool isParam, bool isConst, int line, int col) {
    LocalVar v;
    v.type = type;
    v.isParam = isParam;
    v.isConst = isConst;
    v.line = line;
    v.col = col;
    v.name = name;
    scopes_.back()[name] = std::move(v);
}

TypeChecker::LocalVar *TypeChecker::lookupLocal(const std::string &name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

// The result type of an arithmetic op on two numeric operands, following C-like
// promotion: any float wins (widest float bits); otherwise the wider integer,
// unsigned if either side is unsigned.
TypeRef TypeChecker::numericPromote(const TypeRef &a, const TypeRef &b) {
    if (a.kind == TypeRef::Kind::Float || b.kind == TypeRef::Kind::Float) {
        int fa = a.kind == TypeRef::Kind::Float ? a.numBits() : 0;
        int fb = b.kind == TypeRef::Kind::Float ? b.numBits() : 0;
        return TypeRef::floating(fa > fb ? fa : fb);
    }
    int bits = a.numBits() > b.numBits() ? a.numBits() : b.numBits();
    return TypeRef::integer(bits, a.isUnsigned || b.isUnsigned);
}

bool TypeChecker::isBoxable(const TypeRef &t) const {
    switch (t.kind) {
        case TypeRef::Kind::Int: case TypeRef::Kind::Float: case TypeRef::Kind::Bool:
        case TypeRef::Kind::String: case TypeRef::Kind::Null: case TypeRef::Kind::Function:
        case TypeRef::Kind::Any:
            return true;
        case TypeRef::Kind::Named:
            return reg_->isClass(t.name) || reg_->isInterface(t.name) || reg_->isEnum(t.name);
        default:
            return false;
    }
}

bool TypeChecker::isAssignable(const TypeRef &from, const TypeRef &to) const {
    if (from.isError() || to.isError()) return true; // suppress cascades
    if (from == to) return true;
    // Anything boxable flows into `any`; getting it back out needs `as`.
    if (to.isAny()) return isBoxable(from);
    // Numeric conversions follow C-like rules: any integer flows into any integer
    // or float (widening); float -> integer is narrowing and needs an explicit cast.
    if (from.isNumeric() && to.isNumeric()) {
        if (from.kind == TypeRef::Kind::Float && to.kind == TypeRef::Kind::Int) return false;
        return true;
    }
    // `null` flows into any class, interface, or pointer (reference) type.
    if (from.kind == TypeRef::Kind::Null &&
        (to.isPointer() || to.isFunction() ||
         (to.isNamed() && (reg_->isClass(to.name) || reg_->isInterface(to.name)))))
        return true;
    if (from.isNamed() && to.isNamed()) {
        if (reg_->isClass(from.name) && reg_->isClass(to.name) &&
            reg_->isSubclass(from.name, to.name))
            return true;
        if (reg_->isClass(from.name) && reg_->isInterface(to.name) &&
            reg_->classImplements(from.name, to.name))
            return true;
    }
    return false;
}

bool TypeChecker::checkAccess(Access a, const std::string &definingClass) const {
    switch (a) {
        case Access::Public: return true;
        case Access::Private: return currentClass_ == definingClass;
        case Access::Protected:
            return !currentClass_.empty() &&
                   reg_->isSubclass(currentClass_, definingClass);
    }
    return true;
}

const std::vector<GravError> &TypeChecker::check(Program &program,
                                                 const Registry &reg) {
    reg_ = &reg;
    program_ = &program;
    for (auto &declPtr : program.decls) {
        Decl *d = declPtr.get();
        if (auto *fn = dynamic_cast<FunctionDecl *>(d)) {
            checkFunction(*fn);
        } else if (auto *cls = dynamic_cast<ClassDecl *>(d)) {
            checkStaticFields(*cls);
            checkClass(*cls);
        } else if (auto *t = dynamic_cast<TestDecl *>(d)) {
            // A test is checked like a parameterless void function.
            currentClass_.clear();
            currentNs_ = Registry::namespaceOf(t->fqName);
            currentReturn_ = TypeRef::prim(TypeRef::Kind::Void);
            inConstructor_ = false; inStatic_ = false; inAsync_ = true;
            pushScope();
            checkBlock(t->body);
            popScope();
            inAsync_ = false;
        } else if (auto *g = dynamic_cast<GlobalVarDecl *>(d)) {
            checkGlobal(*g);
        }
        // interfaces have no bodies to check
    }
    auto byLoc = [](const GravError &a, const GravError &b) {
        if (a.line() != b.line()) return a.line() < b.line();
        return a.col() < b.col();
    };
    std::sort(errors_.begin(), errors_.end(), byLoc);
    std::sort(warnings_.begin(), warnings_.end(), byLoc);
    return errors_;
}

void TypeChecker::checkGlobal(GlobalVarDecl &g) {
    currentClass_.clear();
    currentNs_ = Registry::namespaceOf(g.fqName);
    inConstructor_ = false; inStatic_ = true; inAsync_ = false;
    const GlobalInfo *gi = reg_->global(g.fqName);
    TypeRef declared = gi ? gi->type : g.declaredType;
    g.resolvedType = declared;
    pushScope();
    TypeRef initT = checkExpr(*g.init);
    popScope();
    if (!isAssignable(initT, declared))
        error(g.init->line, g.init->col, "cannot initialize global '" + g.name +
                                             "' of type " + typeRefName(declared) +
                                             " with a value of type " + typeRefName(initT));
}

void TypeChecker::checkStaticFields(ClassDecl &cls) {
    currentClass_ = cls.fqName;
    currentNs_ = Registry::namespaceOf(cls.fqName);
    inConstructor_ = false; inStatic_ = true; inAsync_ = false;
    for (auto &sf : cls.staticFields) {
        const GlobalInfo *gi = reg_->global(cls.fqName + "." + sf.name);
        TypeRef declared = gi ? gi->type : sf.type;
        pushScope();
        TypeRef initT = checkExpr(*sf.init);
        popScope();
        if (!isAssignable(initT, declared))
            error(sf.init->line, sf.init->col, "cannot initialize static field '" +
                                                   sf.name + "' of type " + typeRefName(declared) +
                                                   " with a value of type " + typeRefName(initT));
    }
}

void TypeChecker::checkFunction(FunctionDecl &fn) {
    currentClass_.clear();
    currentNs_ = Registry::namespaceOf(fn.fqName);
    currentReturn_ = fn.returnType;
    inConstructor_ = false;
    inStatic_ = false;
    inAsync_ = fn.isAsync;
    pushScope();
    for (auto &p : fn.params) {
        // A variadic parameter is bound as a slice (runtime-length sequence).
        TypeRef pt = p.variadic ? TypeRef::array(p.type, -1) : p.type;
        declareLocal(p.name, pt, true, false, fn.line, fn.col);
    }
    checkBlock(fn.body);
    popScope();
    inAsync_ = false;
    checkFallthrough(fn.body, fn.returnType, fn.line, fn.col, "function '" + fn.fqName + "'");
}

void TypeChecker::checkClass(ClassDecl &cls) {
    currentClass_ = cls.fqName;
    currentNs_ = Registry::namespaceOf(cls.fqName);
    inAsync_ = false; // methods are not async in core v0.2

    auto checkCtor = [&](ConstructorDecl &ctor) {
        inConstructor_ = true;
        inStatic_ = false;
        currentReturn_ = TypeRef::prim(TypeRef::Kind::Void);
        pushScope();
        for (auto &p : ctor.params) declareLocal(p.name, p.type, true, false, ctor.line, ctor.col);
        checkBlock(ctor.body);
        popScope();
    };
    if (cls.constructor.present) checkCtor(cls.constructor);
    for (auto &x : cls.extraCtors) checkCtor(x);

    if (cls.destructor.present) {
        inConstructor_ = false;
        inStatic_ = false;
        currentReturn_ = TypeRef::prim(TypeRef::Kind::Void);
        pushScope();
        checkBlock(cls.destructor.body);
        popScope();
    }

    for (auto &m : cls.methods) {
        if (!m.hasBody) continue;
        inConstructor_ = false;
        inStatic_ = m.isStatic;
        currentReturn_ = m.returnType;
        pushScope();
        for (auto &p : m.params) declareLocal(p.name, p.type, true, false, m.line, m.col);
        checkBlock(m.body);
        popScope();
        checkFallthrough(m.body, m.returnType, m.line, m.col,
                         "method '" + cls.fqName + "." + m.name + "'");
    }
}

void TypeChecker::checkBlock(Block &block) {
    pushScope();
    for (auto &s : block.statements) {
        checkStmt(*s);
        if (matchReplacement_) s = std::move(matchReplacement_);
    }
    popScope();
}

void TypeChecker::checkStmt(Stmt &stmt) {
    if (auto *s = dynamic_cast<LetStmt *>(&stmt)) return checkLet(*s);
    if (auto *s = dynamic_cast<AssignStmt *>(&stmt)) return checkAssign(*s);
    if (auto *s = dynamic_cast<ReturnStmt *>(&stmt)) return checkReturn(*s);
    if (auto *s = dynamic_cast<IfStmt *>(&stmt)) return checkIf(*s);
    if (auto *s = dynamic_cast<WhileStmt *>(&stmt)) return checkWhile(*s);
    if (auto *s = dynamic_cast<DoWhileStmt *>(&stmt)) return checkDoWhile(*s);
    if (auto *s = dynamic_cast<ForStmt *>(&stmt)) return checkFor(*s);
    if (auto *s = dynamic_cast<ForInStmt *>(&stmt)) return checkForIn(*s);
    if (auto *s = dynamic_cast<SwitchStmt *>(&stmt)) return checkSwitch(*s);
    if (auto *s = dynamic_cast<ThrowStmt *>(&stmt)) return checkThrow(*s);
    if (auto *s = dynamic_cast<TryStmt *>(&stmt)) return checkTry(*s);
    if (auto *s = dynamic_cast<DeferStmt *>(&stmt)) return checkDefer(*s);
    if (auto *s = dynamic_cast<BlockStmt *>(&stmt)) return checkBlock(s->block);
    if (dynamic_cast<BreakStmt *>(&stmt) || dynamic_cast<ContinueStmt *>(&stmt)) {
        if (loopDepth_ == 0)
            error(stmt.line, stmt.col, "'break'/'continue' is only valid inside a loop");
        return;
    }
    if (auto *s = dynamic_cast<ExprStmt *>(&stmt)) {
        checkExpr(*s->expr);
        return;
    }
}

void TypeChecker::requireBool(Expr &cond, const char *ctx) {
    TypeRef t = checkExpr(cond);
    if (!t.isError() && t.kind != TypeRef::Kind::Bool)
        error(cond.line, cond.col,
              std::string(ctx) + " must be a bool, but got " + typeRefName(t));
}

void TypeChecker::checkIf(IfStmt &s) {
    requireBool(*s.cond, "an 'if' condition");
    checkBlock(s.thenBlock);
    if (s.elseStmt) checkStmt(*s.elseStmt);
}

void TypeChecker::checkWhile(WhileStmt &s) {
    requireBool(*s.cond, "a 'while' condition");
    loopDepth_++;
    checkBlock(s.body);
    loopDepth_--;
}

void TypeChecker::checkDoWhile(DoWhileStmt &s) {
    loopDepth_++;
    checkBlock(s.body);
    loopDepth_--;
    requireBool(*s.cond, "a 'do/while' condition");
}

void TypeChecker::checkFor(ForStmt &s) {
    pushScope(); // the initializer's scope spans the whole loop
    if (s.init) checkStmt(*s.init);
    if (s.cond) requireBool(*s.cond, "a 'for' condition");
    if (s.update) checkStmt(*s.update);
    loopDepth_++;
    checkBlock(s.body);
    loopDepth_--;
    popScope();
}

void TypeChecker::checkThrow(ThrowStmt &s) {
    TypeRef t = checkExpr(*s.value);
    if (!t.isError() && !(t.isNamed() && reg_->isClass(t.name)))
        error(s.value->line, s.value->col,
              "can only throw a class instance, but got " + typeRefName(t));
}

void TypeChecker::checkTry(TryStmt &s) {
    checkBlock(s.tryBlock);
    std::vector<std::string> seen; // a later clause for a subclass of an earlier one never runs
    for (auto &c : s.catches) {
        bool ok = true;
        TypeRef ct = reg_->resolveCanonical(c.type, currentNs_, ok);
        if (!ok || !(ct.isNamed() && reg_->isClass(ct.name))) {
            error(c.line, c.col, "'catch' type must be a class, but got " + typeRefName(c.type));
            ct = TypeRef::prim(TypeRef::Kind::Error);
        } else {
            for (const auto &prev : seen)
                if (reg_->isSubclass(ct.name, prev))
                    error(c.line, c.col, "this 'catch (" + ct.name + ")' is unreachable: an earlier "
                                         "clause already catches '" + prev + "'");
            seen.push_back(ct.name);
        }
        c.type = ct;
        pushScope();
        declareLocal(c.var, ct, true, false, c.line, c.col);
        checkBlock(c.block);
        popScope();
    }
    if (s.hasFinally) checkBlock(s.finallyBlock);
}

void TypeChecker::checkSwitch(SwitchStmt &s) {
    TypeRef subj = checkExpr(*s.subject);
    if (const ClassDecl *base = dataEnumOf(subj)) return lowerDataEnumMatch(s, subj, *base);
    bool ok = subj.kind == TypeRef::Kind::Int ||
              subj.kind == TypeRef::Kind::String ||
              (subj.isNamed() && reg_->isEnum(subj.name));
    if (!subj.isError() && !ok)
        error(s.subject->line, s.subject->col,
              "switch/match subject must be int, string, or enum, got " +
                  typeRefName(subj));
    for (auto &c : s.cases) {
        for (auto &v : c.values) {
            TypeRef vt = checkExpr(*v);
            if (!vt.isError() && !subj.isError() && vt != subj)
                error(v->line, v->col, "case value type " + typeRefName(vt) +
                                           " does not match subject type " +
                                           typeRefName(subj));
        }
        checkBlock(c.body);
    }
    if (s.hasDefault) checkBlock(s.defaultBody);
}

void TypeChecker::checkLet(LetStmt &s) {
    if (s.hasDeclaredType) {
        bool ok = true;
        TypeRef c = reg_->resolveCanonical(s.declaredType, currentNs_, ok);
        if (!ok) {
            error(s.line, s.col, "unknown type '" + typeRefName(s.declaredType) + "'");
            s.declaredType = TypeRef::prim(TypeRef::Kind::Error);
        } else {
            s.declaredType = c;
        }
    }
    TypeRef initType = s.hasDeclaredType ? checkExprExpect(*s.init, s.declaredType)
                                         : checkExpr(*s.init);
    // An inline C expression adopts the declared type of the binding it feeds.
    bool isCBlock = dynamic_cast<CBlockExpr *>(s.init.get()) != nullptr;
    if (isCBlock && s.hasDeclaredType && !s.declaredType.isError()) {
        s.init->type = s.declaredType;
        initType = s.declaredType;
    }
    if (s.hasDeclaredType) {
        if (!isAssignable(initType, s.declaredType)) {
            error(s.init->line, s.init->col,
                  "cannot initialize '" + s.name + "' of type " +
                      typeRefName(s.declaredType) + " with a value of type " +
                      typeRefName(initType));
        }
        s.resolvedType = s.declaredType;
    } else {
        if (isCBlock) {
            error(s.init->line, s.init->col,
                  "an inline C expression needs a type: write `let " + s.name +
                      ": T = %{ ... %}` or `%{ ... %} as T`");
            s.resolvedType = TypeRef::prim(TypeRef::Kind::Error);
        } else if (initType.isVoid()) {
            error(s.init->line, s.init->col,
                  "cannot infer the type of '" + s.name + "' from a void value");
            s.resolvedType = TypeRef::prim(TypeRef::Kind::Error);
        } else {
            s.resolvedType = initType;
        }
    }
    declareLocal(s.name, s.resolvedType, false, s.isConst, s.line, s.col);
}

void TypeChecker::checkAssign(AssignStmt &s) {
    // `obj[k] = value` on a class with `operator[]=` calls `op_index_set(k, value)`.
    if (auto *ix = dynamic_cast<IndexExpr *>(s.target.get())) {
        TypeRef bt = checkExpr(*ix->base);
        if (bt.isNamed() && reg_->isClass(bt.name)) { checkIndexAssign(s, *ix, bt); return; }
    }
    // `x op= v` on a class value (operator+ and friends): rewrite to `x = x op v`.
    if (s.isCompound) {
        TypeRef tt = checkExpr(*s.target);
        if (tt.isNamed() && reg_->isClass(tt.name)) {
            ExprPtr copy = cloneExpression(s.target.get());
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
        }
    }
    // A lambda assigned to a typed variable/field takes its parameter types from it.
    TypeRef valueType;
    if (dynamic_cast<LambdaExpr *>(s.value.get()) || dynamic_cast<NewExpr *>(s.value.get())) {
        TypeRef tt = checkExpr(*s.target);
        valueType = checkExprExpect(*s.value, tt);
    } else {
        valueType = checkExpr(*s.value);
    }

    // `*p = value` — write through a pointer.
    if (dynamic_cast<DerefExpr *>(s.target.get())) {
        TypeRef targetType = checkExpr(*s.target); // validates the pointer, sets type
        if (targetType.isError()) return;
        if (s.isCompound)
            checkCompound(s, targetType, valueType);
        else if (!isAssignable(valueType, targetType))
            error(s.value->line, s.value->col,
                  "cannot assign a value of type " + typeRefName(valueType) +
                      " through a pointer to " + typeRefName(targetType));
        return;
    }

    // `a[i] = value` — write through an array/pointer element.
    if (dynamic_cast<IndexExpr *>(s.target.get())) {
        TypeRef targetType = checkExpr(*s.target); // validates indexability, sets type
        if (targetType.isError()) return;
        if (s.isCompound)
            checkCompound(s, targetType, valueType);
        else if (!isAssignable(valueType, targetType))
            error(s.value->line, s.value->col,
                  "cannot assign a value of type " + typeRefName(valueType) +
                      " to an element of type " + typeRefName(targetType));
        return;
    }

    // Resolve the target as an lvalue.
    if (auto *name = dynamic_cast<NameExpr *>(s.target.get())) {
        LocalVar *local = lookupLocal(name->name);
        if (!local) {
            // Maybe a (mutable) module-level global.
            std::string gfq = reg_->resolveGlobal(name->name, currentNs_);
            if (!gfq.empty()) {
                const GlobalInfo *gi = reg_->global(gfq);
                name->resolvedGlobal = gi->cName;
                name->type = gi->type;
                if (gi->isConst)
                    error(name->line, name->col, "cannot assign to constant '" + name->name + "'");
                if (s.isCompound) checkCompound(s, gi->type, valueType);
                else if (!isAssignable(valueType, gi->type))
                    error(s.value->line, s.value->col,
                          "cannot assign a value of type " + typeRefName(valueType) +
                              " to '" + name->name + "' of type " + typeRefName(gi->type));
                return;
            }
            error(name->line, name->col,
                  "assignment to undeclared variable '" + name->name + "'");
            return;
        }
        name->type = local->type;
        if (local->isConst) {
            error(name->line, name->col,
                  "cannot assign to constant '" + name->name + "'");
        }
        if (isCapturedHere(name->name))
            error(name->line, name->col,
                  "cannot assign to captured variable '" + name->name +
                      "' inside a lambda (captures are copies; use an object field to share state)");
        if (s.isCompound) {
            checkCompound(s, local->type, valueType);
        } else if (!isAssignable(valueType, local->type)) {
            error(s.value->line, s.value->col,
                  "cannot assign a value of type " + typeRefName(valueType) +
                      " to '" + name->name + "' of type " + typeRefName(local->type));
        }
        return;
    }

    if (auto *mem = dynamic_cast<MemberExpr *>(s.target.get())) {
        // Static field assignment: `Class.field = value`.
        if (auto chain = flattenNames(mem); chain && chain->size() >= 2 &&
            lookupLocal((*chain)[0]) == nullptr) {
            std::string prefix;
            for (size_t i = 0; i + 1 < chain->size(); ++i)
                prefix += (i ? "." : "") + (*chain)[i];
            std::string clsFq = reg_->resolveType(prefix, currentNs_);
            if (!clsFq.empty() && reg_->isClass(clsFq)) {
                if (const GlobalInfo *gi = reg_->findStaticField(clsFq, chain->back())) {
                    mem->kind = MemberKind::StaticField;
                    mem->qualified = gi->cName;
                    mem->type = gi->type;
                    if (gi->isConst)
                        error(mem->line, mem->col, "cannot assign to constant static field '" +
                                                       chain->back() + "'");
                    if (!checkAccess(gi->access, gi->ownerClass))
                        error(mem->line, mem->col, "static field '" + chain->back() + "' is " +
                                                       accessName(gi->access));
                    if (s.isCompound) checkCompound(s, gi->type, valueType);
                    else if (!isAssignable(valueType, gi->type))
                        error(s.value->line, s.value->col,
                              "cannot assign a value of type " + typeRefName(valueType) +
                                  " to static field '" + chain->back() + "' of type " +
                                  typeRefName(gi->type));
                    return;
                }
            }
        }
        TypeRef objType = checkExpr(*mem->object);
        if (objType.isNamed() && reg_->isStruct(objType.name)) {
            const FieldInfo *f = reg_->findStructField(objType.name, mem->member);
            if (!f) {
                error(mem->line, mem->col, "struct '" + objType.name +
                                               "' has no field '" + mem->member + "'");
                return;
            }
            mem->kind = MemberKind::InstanceField;
            mem->ownerClass = f->definingClass;
            mem->type = f->type;
            if (s.isCompound)
                checkCompound(s, f->type, valueType);
            else if (!isAssignable(valueType, f->type))
                error(s.value->line, s.value->col,
                      "cannot assign a value of type " + typeRefName(valueType) +
                          " to field '" + mem->member + "' of type " +
                          typeRefName(f->type));
            return;
        }
        if (!objType.isNamed() || !reg_->isClass(objType.name)) {
            if (!objType.isError())
                error(mem->line, mem->col, "cannot assign to a member of type " +
                                               typeRefName(objType));
            return;
        }
        const FieldInfo *f = reg_->findField(objType.name, mem->member);
        if (!f) {
            error(mem->line, mem->col, "type '" + objType.name +
                                           "' has no field '" + mem->member + "'");
            return;
        }
        mem->kind = MemberKind::InstanceField;
        mem->ownerClass = f->definingClass;
        mem->type = f->type;
        if (!checkAccess(f->access, f->definingClass)) {
            error(mem->line, mem->col, "field '" + mem->member + "' is " +
                                           accessName(f->access) + " in '" +
                                           f->definingClass + "'");
        }
        if (f->isReadonly && !(inConstructor_ && currentClass_ == f->definingClass)) {
            error(mem->line, mem->col, "cannot assign to readonly field '" +
                                           mem->member + "' outside its constructor");
        }
        if (s.isCompound) {
            checkCompound(s, f->type, valueType);
        } else if (!isAssignable(valueType, f->type)) {
            error(s.value->line, s.value->col,
                  "cannot assign a value of type " + typeRefName(valueType) +
                      " to field '" + mem->member + "' of type " + typeRefName(f->type));
        }
        return;
    }

    error(s.line, s.col, "invalid assignment target");
}

// `x <op>= y` is valid when `x <op> y` is well-typed and assignable back to x.
// That means numeric (matching int/float; `%` int-only) or `string += string`.
void TypeChecker::checkCompound(const AssignStmt &s, const TypeRef &targetType,
                                const TypeRef &valueType) {
    if (targetType.isError() || valueType.isError()) return;
    const char *sym = binaryOpSymbol(s.compoundOp);
    if (s.compoundOp == BinaryOp::Add && targetType.kind == TypeRef::Kind::String &&
        valueType.kind == TypeRef::Kind::String)
        return; // string concatenation in place
    if (!targetType.isNumeric() || !valueType.isNumeric()) {
        error(s.line, s.col, std::string("operator '") + sym +
                                 "=' requires numeric operands, but got " +
                                 typeRefName(targetType) + " and " + typeRefName(valueType));
        return;
    }
    // Mixed widths are fine as long as the result fits the target's kind
    // (`int += byte`, `double += int`); float into an integer needs a cast.
    if (targetType != valueType && !isAssignable(valueType, targetType)) {
        error(s.line, s.col, std::string("operator '") + sym +
                                 "=' needs an explicit cast: " +
                                 typeRefName(targetType) + " and " + typeRefName(valueType));
        return;
    }
    if (s.compoundOp == BinaryOp::Mod && targetType.kind != TypeRef::Kind::Int)
        error(s.line, s.col, "operator '%=' requires int operands");
}

void TypeChecker::checkReturn(ReturnStmt &s) {
    if (inDefer_) error(s.line, s.col, "'return' is not allowed inside a 'defer' block");
    if (s.value) {
        TypeRef vt = checkExprExpect(*s.value, currentReturn_);
        if (currentReturn_.isVoid()) {
            error(s.line, s.col, "cannot return a value from a void function");
        } else if (!isAssignable(vt, currentReturn_)) {
            error(s.value->line, s.value->col, "return type mismatch: expected " +
                                                   typeRefName(currentReturn_) +
                                                   ", got " + typeRefName(vt));
        }
    } else if (!currentReturn_.isVoid()) {
        error(s.line, s.col, "expected a return value of type " +
                                 typeRefName(currentReturn_));
    }
}

} // namespace grav
