// Properties: `get name() -> T { }` and `set name(v: T) { }` are the methods
// __get_name and __set_name (see the parser). A read of `obj.name` becomes
// `obj.__get_name()`, `obj.name = v` becomes `obj.__set_name(v)`, and
// `obj.name op= v` becomes `obj.__set_name(obj.__get_name() op v)`, with the
// object evaluated once. Static properties work the same through `Class.name`.
#include "sema/monomorph.h"
#include "sema/typechecker.h"

namespace grav {

namespace {

std::string joinDots(const std::vector<std::string> &parts, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) out += (i ? "." : "") + parts[i];
    return out;
}

ExprPtr nameAt(const std::string &n, int line, int col) {
    auto e = std::make_unique<NameExpr>();
    e->name = n;
    e->line = line;
    e->col = col;
    return e;
}

std::unique_ptr<CallExpr> accessorCall(ExprPtr object, const std::string &method, ExprPtr arg, int line, int col,
                                       bool optional = false) {
    auto callee = std::make_unique<MemberExpr>();
    callee->line = line;
    callee->col = col;
    callee->object = std::move(object);
    callee->member = method;
    callee->optional = optional;
    auto call = std::make_unique<CallExpr>();
    call->line = line;
    call->col = col;
    call->callee = std::move(callee);
    if (arg) call->args.push_back(std::move(arg));
    return call;
}

// An object that can be evaluated twice without a difference: a name, self or
// super (fields of those are read again, which is what `x.f.n += 1` means too).
bool stableObject(const Expr *x) {
    if (dynamic_cast<const NameExpr *>(x) || dynamic_cast<const SelfExpr *>(x) || dynamic_cast<const SuperExpr *>(x))
        return true;
    if (auto *m = dynamic_cast<const MemberExpr *>(x)) return stableObject(m->object.get());
    return false;
}

} // namespace

// The class whose static property `e` names (`Config.level`), or "".
std::string TypeChecker::staticPropertyOwner(MemberExpr &e, bool getter) {
    auto chain = flattenNames(&e);
    if (!chain || chain->size() < 2 || lookupLocal((*chain)[0]) != nullptr) return "";
    std::string clsFq = reg_->resolveType(joinDots(*chain, chain->size() - 1), currentNs_);
    if (clsFq.empty() || !reg_->isClass(clsFq)) return "";
    std::string method = getter ? propertyGetter(e.member) : propertySetter(e.member);
    for (const MethodInfo *m : reg_->findMethodsBySource(clsFq, method))
        if (m->isStatic) return clsFq;
    // A static property with only the other accessor still names the class.
    std::string other = getter ? propertySetter(e.member) : propertyGetter(e.member);
    for (const MethodInfo *m : reg_->findMethodsBySource(clsFq, other))
        if (m->isStatic) return clsFq;
    return "";
}

// Does the class or interface `type` have the accessor `method`?
bool TypeChecker::hasAccessor(const std::string &type, const std::string &method) const {
    if (reg_->isClass(type)) return !reg_->findMethodsBySource(type, method).empty();
    if (reg_->isInterface(type)) return reg_->findInterfaceMethod(type, method) != nullptr;
    return false;
}

// `obj.name` (or `Class.name`) read through a getter; nullopt when `name` is no property.
std::optional<TypeRef> TypeChecker::tryPropertyRead(MemberExpr &e, const TypeRef *objType) {
    std::string getter = propertyGetter(e.member), setter = propertySetter(e.member);
    ExprPtr object;
    if (!objType) {
        std::string owner = staticPropertyOwner(e, true);
        if (owner.empty()) return std::nullopt;
        object = nameAt(owner, e.object->line, e.object->col);
        if (reg_->findMethodsBySource(owner, getter).empty()) {
            error(e.line, e.col, "static property '" + e.member + "' of '" + owner + "' has no getter: it can only be set");
            return TypeRef::prim(TypeRef::Kind::Error);
        }
    } else {
        if (!objType->isNamed() || !hasAccessor(objType->name, getter)) {
            if (objType->isNamed() && hasAccessor(objType->name, setter)) {
                error(e.line, e.col, "property '" + e.member + "' of '" + objType->name +
                                         "' has no getter: it can only be set");
                return TypeRef::prim(TypeRef::Kind::Error);
            }
            return std::nullopt;
        }
        object = cloneExpression(e.object.get()); // (e.object stays for re-checks; `bound` is what is emitted)
    }
    e.bound = accessorCall(std::move(object), getter, nullptr, e.line, e.col, e.optional);
    return checkExpr(*e.bound);
}

// Does `mem` name a property (of its object's class or interface, or a static one)?
bool TypeChecker::isPropertyTarget(MemberExpr &mem) {
    if (!staticPropertyOwner(mem, false).empty()) return true;
    size_t nErr = errors_.size(), nWarn = warnings_.size();
    ExprPtr probe = cloneExpression(mem.object.get());
    TypeRef t = checkExpr(*probe);
    errors_.erase(errors_.begin() + static_cast<long>(nErr), errors_.end());
    warnings_.erase(warnings_.begin() + static_cast<long>(nWarn), warnings_.end());
    if (!t.isNamed() || (reg_->isClass(t.name) && reg_->findField(t.name, mem.member))) return false;
    return hasAccessor(t.name, propertyGetter(mem.member)) || hasAccessor(t.name, propertySetter(mem.member));
}

// `obj.name = v` / `obj.name op= v` through a setter: true when `s.target` is a
// property (handled, with any error reported), false to go on as a field.
bool TypeChecker::tryPropertyWrite(AssignStmt &s, MemberExpr &mem, bool blockLevel) {
    std::string getter = propertyGetter(mem.member), setter = propertySetter(mem.member);
    std::string owner = staticPropertyOwner(mem, false);
    bool isStatic = !owner.empty();
    if (!isStatic) {
        // Probe the object's type on a copy, leaving no diagnostics behind.
        size_t nErr = errors_.size(), nWarn = warnings_.size();
        ExprPtr probe = cloneExpression(mem.object.get());
        TypeRef t = checkExpr(*probe);
        errors_.erase(errors_.begin() + static_cast<long>(nErr), errors_.end());
        warnings_.erase(warnings_.begin() + static_cast<long>(nWarn), warnings_.end());
        if (!t.isNamed() || (!hasAccessor(t.name, setter) && !hasAccessor(t.name, getter))) return false;
        if (reg_->isClass(t.name) && reg_->findField(t.name, mem.member)) return false;
        owner = t.name;
    }
    if (!hasAccessor(owner, setter)) {
        error(mem.line, mem.col, std::string(isStatic ? "static property '" : "property '") + mem.member + "' of '" +
                                     owner + "' is read-only: it has a getter and no setter");
        return true;
    }
    int line = s.line, col = s.col;
    auto object = [&]() -> ExprPtr {
        if (isStatic) return nameAt(owner, mem.object->line, mem.object->col);
        return cloneExpression(mem.object.get());
    };
    StmtPtr out;
    if (!s.isCompound) {
        auto st = std::make_unique<ExprStmt>();
        st->line = line;
        st->col = col;
        st->expr = accessorCall(object(), setter, std::move(s.value), line, col);
        out = std::move(st);
    } else {
        if (!hasAccessor(owner, getter)) {
            error(mem.line, mem.col, "'" + std::string(binaryOpSymbol(s.compoundOp)) + "=' on property '" + mem.member +
                                         "' needs its getter to read it, and '" + owner + "' has only a setter");
            return true;
        }
        bool stable = isStatic || stableObject(mem.object.get());
        if (!stable && !blockLevel) {
            error(s.line, s.col, "'" + std::string(binaryOpSymbol(s.compoundOp)) + "=' on property '" + mem.member +
                                     "' of a computed object must be a statement of its own: put the object in a variable");
            return true;
        }
        auto block = std::make_unique<BlockStmt>();
        block->line = line;
        block->col = col;
        std::string tmp = "prop__obj" + std::to_string(matchCounter_++);
        auto recv = [&]() -> ExprPtr { return stable ? object() : nameAt(tmp, line, col); };
        if (!stable) {
            auto l = std::make_unique<LetStmt>();
            l->name = tmp;
            l->init = std::move(mem.object);
            l->line = line;
            l->col = col;
            l->quietUnused = true;
            block->block.statements.push_back(std::move(l));
        }
        auto bin = std::make_unique<BinaryExpr>();
        bin->line = s.value->line;
        bin->col = s.value->col;
        bin->op = s.compoundOp;
        bin->left = accessorCall(recv(), getter, nullptr, line, col);
        bin->right = std::move(s.value);
        auto st = std::make_unique<ExprStmt>();
        st->line = line;
        st->col = col;
        st->expr = accessorCall(recv(), setter, std::move(bin), line, col);
        if (stable) out = std::move(st);
        else {
            block->block.statements.push_back(std::move(st));
            out = std::move(block);
        }
    }
    if (auto *b = dynamic_cast<BlockStmt *>(out.get())) checkBlock(b->block);
    else checkStmt(*out);
    matchReplacement_ = std::move(out);
    return true;
}

} // namespace grav
