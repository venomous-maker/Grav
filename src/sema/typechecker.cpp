#include "sema/typechecker.h"

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

bool TypeChecker::isAssignable(const TypeRef &from, const TypeRef &to) const {
    if (from.isError() || to.isError()) return true; // suppress cascades
    if (from == to) return true;
    // `null` flows into any class, interface, or pointer (reference) type.
    if (from.kind == TypeRef::Kind::Null &&
        (to.isPointer() ||
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
    for (auto &declPtr : program.decls) {
        Decl *d = declPtr.get();
        if (auto *fn = dynamic_cast<FunctionDecl *>(d)) {
            checkFunction(*fn);
        } else if (auto *cls = dynamic_cast<ClassDecl *>(d)) {
            checkClass(*cls);
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

void TypeChecker::checkFunction(FunctionDecl &fn) {
    currentClass_.clear();
    currentNs_ = Registry::namespaceOf(fn.fqName);
    currentReturn_ = fn.returnType;
    inConstructor_ = false;
    inStatic_ = false;
    inAsync_ = fn.isAsync;
    pushScope();
    for (auto &p : fn.params) declareLocal(p.name, p.type, true, false, fn.line, fn.col);
    checkBlock(fn.body);
    popScope();
    inAsync_ = false;
}

void TypeChecker::checkClass(ClassDecl &cls) {
    currentClass_ = cls.fqName;
    currentNs_ = Registry::namespaceOf(cls.fqName);
    inAsync_ = false; // methods are not async in core v0.2

    if (cls.constructor.present) {
        inConstructor_ = true;
        inStatic_ = false;
        currentReturn_ = TypeRef::prim(TypeRef::Kind::Void);
        pushScope();
        for (auto &p : cls.constructor.params)
            declareLocal(p.name, p.type, true, false, cls.constructor.line,
                         cls.constructor.col);
        checkBlock(cls.constructor.body);
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
    }
}

void TypeChecker::checkBlock(Block &block) {
    pushScope();
    for (auto &s : block.statements) checkStmt(*s);
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

void TypeChecker::checkForIn(ForInStmt &s) {
    TypeRef lo = checkExpr(*s.lo);
    TypeRef hi = checkExpr(*s.hi);
    if (!lo.isError() && lo.kind != TypeRef::Kind::Int)
        error(s.lo->line, s.lo->col,
              "a 'for ... in' range bound must be int, got " + typeRefName(lo));
    if (!hi.isError() && hi.kind != TypeRef::Kind::Int)
        error(s.hi->line, s.hi->col,
              "a 'for ... in' range bound must be int, got " + typeRefName(hi));
    pushScope(); // the loop variable's scope spans the whole loop
    declareLocal(s.var, TypeRef::prim(TypeRef::Kind::Int), true, false, s.line, s.col);
    loopDepth_++;
    checkBlock(s.body);
    loopDepth_--;
    popScope();
}

void TypeChecker::checkSwitch(SwitchStmt &s) {
    TypeRef subj = checkExpr(*s.subject);
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
    TypeRef initType = checkExpr(*s.init);
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
    if (s.hasDeclaredType) {
        if (!isAssignable(initType, s.declaredType)) {
            error(s.init->line, s.init->col,
                  "cannot initialize '" + s.name + "' of type " +
                      typeRefName(s.declaredType) + " with a value of type " +
                      typeRefName(initType));
        }
        s.resolvedType = s.declaredType;
    } else {
        if (initType.isVoid()) {
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
    TypeRef valueType = checkExpr(*s.value);

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
            error(name->line, name->col,
                  "assignment to undeclared variable '" + name->name + "'");
            return;
        }
        name->type = local->type;
        if (local->isConst) {
            error(name->line, name->col,
                  "cannot assign to constant '" + name->name + "'");
        }
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
    if (targetType != valueType) {
        error(s.line, s.col, std::string("operator '") + sym +
                                 "=' requires matching types, but got " +
                                 typeRefName(targetType) + " and " + typeRefName(valueType));
        return;
    }
    if (s.compoundOp == BinaryOp::Mod && targetType.kind != TypeRef::Kind::Int)
        error(s.line, s.col, "operator '%=' requires int operands");
}

void TypeChecker::checkReturn(ReturnStmt &s) {
    if (s.value) {
        TypeRef vt = checkExpr(*s.value);
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
