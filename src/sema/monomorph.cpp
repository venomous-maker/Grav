#include "sema/monomorph.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>

namespace grav {

namespace {

// ---------------------------------------------------------------------------
// A stable C-identifier-ish key for a concrete type, used to name instances.
// ---------------------------------------------------------------------------
std::string typeMangle(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: return "int";
        case TypeRef::Kind::Float: return "float";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "string";
        case TypeRef::Kind::Void: return "void";
        case TypeRef::Kind::Null: return "null";
        case TypeRef::Kind::Pointer:
            return (t.elem ? typeMangle(*t.elem) : "v") + "_p";
        case TypeRef::Kind::Array:
            return (t.elem ? typeMangle(*t.elem) : "v") + "_a" + std::to_string(t.arrayLen);
        case TypeRef::Kind::Named: {
            std::string s;
            for (char c : t.name) s += (c == '.') ? '_' : c;
            for (const auto &a : t.args) s += "$" + typeMangle(a);
            return s;
        }
        default: return "x";
    }
}

// ---------------------------------------------------------------------------
// Deep AST clone (templates are cloned per instantiation).
// ---------------------------------------------------------------------------
ExprPtr cloneExpr(const Expr *e);
StmtPtr cloneStmt(const Stmt *s);

Block cloneBlock(const Block &b) {
    Block out;
    for (const auto &s : b.statements) out.statements.push_back(cloneStmt(s.get()));
    return out;
}

template <class T> std::unique_ptr<T> mkE(const Expr *src) {
    auto n = std::make_unique<T>();
    n->line = src->line;
    n->col = src->col;
    n->type = src->type;
    return n;
}
template <class T> std::unique_ptr<T> mkS(const Stmt *src) {
    auto n = std::make_unique<T>();
    n->line = src->line;
    n->col = src->col;
    return n;
}

ExprPtr cloneExpr(const Expr *e) {
    if (!e) return nullptr;
    if (auto *x = dynamic_cast<const IntLiteralExpr *>(e)) {
        auto n = mkE<IntLiteralExpr>(e); n->value = x->value; n->raw = x->raw; return n;
    }
    if (auto *x = dynamic_cast<const FloatLiteralExpr *>(e)) {
        auto n = mkE<FloatLiteralExpr>(e); n->raw = x->raw; return n;
    }
    if (auto *x = dynamic_cast<const BoolLiteralExpr *>(e)) {
        auto n = mkE<BoolLiteralExpr>(e); n->value = x->value; return n;
    }
    if (auto *x = dynamic_cast<const StringLiteralExpr *>(e)) {
        auto n = mkE<StringLiteralExpr>(e); n->value = x->value; return n;
    }
    if (dynamic_cast<const NullLiteralExpr *>(e)) return mkE<NullLiteralExpr>(e);
    if (auto *x = dynamic_cast<const CBlockExpr *>(e)) {
        auto n = mkE<CBlockExpr>(e); n->code = x->code; return n;
    }
    if (auto *x = dynamic_cast<const NameExpr *>(e)) {
        auto n = mkE<NameExpr>(e); n->name = x->name; return n;
    }
    if (dynamic_cast<const SelfExpr *>(e)) return mkE<SelfExpr>(e);
    if (auto *x = dynamic_cast<const BinaryExpr *>(e)) {
        auto n = mkE<BinaryExpr>(e); n->op = x->op; n->stringConcat = x->stringConcat;
        n->left = cloneExpr(x->left.get()); n->right = cloneExpr(x->right.get()); return n;
    }
    if (auto *x = dynamic_cast<const UnaryExpr *>(e)) {
        auto n = mkE<UnaryExpr>(e); n->op = x->op; n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const TernaryExpr *>(e)) {
        auto n = mkE<TernaryExpr>(e); n->cond = cloneExpr(x->cond.get());
        n->thenExpr = cloneExpr(x->thenExpr.get()); n->elseExpr = cloneExpr(x->elseExpr.get()); return n;
    }
    if (auto *x = dynamic_cast<const AsExpr *>(e)) {
        auto n = mkE<AsExpr>(e); n->target = x->target; n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const IsExpr *>(e)) {
        auto n = mkE<IsExpr>(e); n->typeName = x->typeName; n->className = x->className;
        n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const AwaitExpr *>(e)) {
        auto n = mkE<AwaitExpr>(e); n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const AddrOfExpr *>(e)) {
        auto n = mkE<AddrOfExpr>(e); n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const DerefExpr *>(e)) {
        auto n = mkE<DerefExpr>(e); n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const CoalesceExpr *>(e)) {
        auto n = mkE<CoalesceExpr>(e); n->left = cloneExpr(x->left.get());
        n->right = cloneExpr(x->right.get()); return n;
    }
    if (auto *x = dynamic_cast<const IncDecExpr *>(e)) {
        auto n = mkE<IncDecExpr>(e); n->isIncrement = x->isIncrement; n->isPrefix = x->isPrefix;
        n->target = cloneExpr(x->target.get()); return n;
    }
    if (auto *x = dynamic_cast<const CastExpr *>(e)) {
        auto n = mkE<CastExpr>(e); n->target = x->target; n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const SizeofExpr *>(e)) {
        auto n = mkE<SizeofExpr>(e); n->isType = x->isType; n->target = x->target;
        n->operand = cloneExpr(x->operand.get()); return n;
    }
    if (auto *x = dynamic_cast<const ArrayLiteralExpr *>(e)) {
        auto n = mkE<ArrayLiteralExpr>(e);
        for (const auto &el : x->elements) n->elements.push_back(cloneExpr(el.get()));
        return n;
    }
    if (auto *x = dynamic_cast<const IndexExpr *>(e)) {
        auto n = mkE<IndexExpr>(e); n->base = cloneExpr(x->base.get());
        n->index = cloneExpr(x->index.get()); return n;
    }
    if (auto *x = dynamic_cast<const MemberExpr *>(e)) {
        auto n = mkE<MemberExpr>(e); n->member = x->member; n->optional = x->optional;
        n->kind = x->kind; n->ownerClass = x->ownerClass; n->qualified = x->qualified;
        n->object = cloneExpr(x->object.get()); return n;
    }
    if (auto *x = dynamic_cast<const NewExpr *>(e)) {
        auto n = mkE<NewExpr>(e); n->className = x->className; n->typeArgs = x->typeArgs;
        for (const auto &a : x->args) n->args.push_back(cloneExpr(a.get()));
        return n;
    }
    if (auto *x = dynamic_cast<const StructLiteralExpr *>(e)) {
        auto n = mkE<StructLiteralExpr>(e); n->typeName = x->typeName; n->typeArgs = x->typeArgs;
        for (const auto &f : x->fields) {
            StructFieldInit fi; fi.name = f.name; fi.line = f.line; fi.col = f.col;
            fi.value = cloneExpr(f.value.get()); n->fields.push_back(std::move(fi));
        }
        return n;
    }
    if (auto *x = dynamic_cast<const CallExpr *>(e)) {
        auto n = mkE<CallExpr>(e); n->typeArgs = x->typeArgs; n->kind = x->kind;
        n->targetName = x->targetName; n->methodName = x->methodName;
        n->ownerClass = x->ownerClass; n->slotOwner = x->slotOwner; n->ifaceDispatch = x->ifaceDispatch;
        n->callee = cloneExpr(x->callee.get());
        for (const auto &a : x->args) n->args.push_back(cloneExpr(a.get()));
        return n;
    }
    return nullptr; // unreachable for known nodes
}

StmtPtr cloneStmt(const Stmt *s) {
    if (!s) return nullptr;
    if (auto *x = dynamic_cast<const LetStmt *>(s)) {
        auto n = mkS<LetStmt>(s); n->name = x->name; n->isConst = x->isConst;
        n->hasDeclaredType = x->hasDeclaredType; n->declaredType = x->declaredType;
        n->resolvedType = x->resolvedType; n->init = cloneExpr(x->init.get()); return n;
    }
    if (auto *x = dynamic_cast<const AssignStmt *>(s)) {
        auto n = mkS<AssignStmt>(s); n->isCompound = x->isCompound; n->compoundOp = x->compoundOp;
        n->target = cloneExpr(x->target.get()); n->value = cloneExpr(x->value.get()); return n;
    }
    if (auto *x = dynamic_cast<const ReturnStmt *>(s)) {
        auto n = mkS<ReturnStmt>(s); n->value = cloneExpr(x->value.get()); return n;
    }
    if (auto *x = dynamic_cast<const ExprStmt *>(s)) {
        auto n = mkS<ExprStmt>(s); n->expr = cloneExpr(x->expr.get()); return n;
    }
    if (auto *x = dynamic_cast<const BlockStmt *>(s)) {
        auto n = mkS<BlockStmt>(s); n->block = cloneBlock(x->block); return n;
    }
    if (auto *x = dynamic_cast<const IfStmt *>(s)) {
        auto n = mkS<IfStmt>(s); n->cond = cloneExpr(x->cond.get());
        n->thenBlock = cloneBlock(x->thenBlock); n->elseStmt = cloneStmt(x->elseStmt.get()); return n;
    }
    if (auto *x = dynamic_cast<const WhileStmt *>(s)) {
        auto n = mkS<WhileStmt>(s); n->cond = cloneExpr(x->cond.get()); n->body = cloneBlock(x->body); return n;
    }
    if (auto *x = dynamic_cast<const DoWhileStmt *>(s)) {
        auto n = mkS<DoWhileStmt>(s); n->body = cloneBlock(x->body); n->cond = cloneExpr(x->cond.get()); return n;
    }
    if (auto *x = dynamic_cast<const SwitchStmt *>(s)) {
        auto n = mkS<SwitchStmt>(s); n->subject = cloneExpr(x->subject.get());
        n->hasDefault = x->hasDefault; n->defaultBody = cloneBlock(x->defaultBody);
        for (const auto &c : x->cases) {
            SwitchCase nc;
            for (const auto &v : c.values) nc.values.push_back(cloneExpr(v.get()));
            nc.body = cloneBlock(c.body); n->cases.push_back(std::move(nc));
        }
        return n;
    }
    if (auto *x = dynamic_cast<const ForStmt *>(s)) {
        auto n = mkS<ForStmt>(s); n->init = cloneStmt(x->init.get()); n->cond = cloneExpr(x->cond.get());
        n->update = cloneStmt(x->update.get()); n->body = cloneBlock(x->body); return n;
    }
    if (auto *x = dynamic_cast<const ForInStmt *>(s)) {
        auto n = mkS<ForInStmt>(s); n->var = x->var; n->inclusive = x->inclusive;
        n->lo = cloneExpr(x->lo.get()); n->hi = cloneExpr(x->hi.get()); n->body = cloneBlock(x->body); return n;
    }
    if (auto *x = dynamic_cast<const ThrowStmt *>(s)) {
        auto n = mkS<ThrowStmt>(s); n->value = cloneExpr(x->value.get()); return n;
    }
    if (auto *x = dynamic_cast<const TryStmt *>(s)) {
        auto n = mkS<TryStmt>(s);
        n->tryBlock = cloneBlock(x->tryBlock);
        n->hasCatch = x->hasCatch; n->catchVar = x->catchVar; n->catchType = x->catchType;
        n->catchBlock = cloneBlock(x->catchBlock);
        n->hasFinally = x->hasFinally; n->finallyBlock = cloneBlock(x->finallyBlock);
        return n;
    }
    if (dynamic_cast<const BreakStmt *>(s)) return mkS<BreakStmt>(s);
    if (dynamic_cast<const ContinueStmt *>(s)) return mkS<ContinueStmt>(s);
    return nullptr;
}

MethodDecl cloneMethod(const MethodDecl &m) {
    MethodDecl n;
    n.access = m.access; n.isStatic = m.isStatic; n.isAbstract = m.isAbstract;
    n.name = m.name; n.params = m.params; n.returnType = m.returnType;
    n.hasBody = m.hasBody; n.line = m.line; n.col = m.col;
    n.body = cloneBlock(m.body);
    return n;
}

// ---------------------------------------------------------------------------
// The monomorphizer.
// ---------------------------------------------------------------------------
using Subst = std::unordered_map<std::string, TypeRef>;

class Mono {
public:
    explicit Mono(Program &program) : program_(program) {}
    void run();
    std::vector<GravError> errors;

private:
    Program &program_;
    std::vector<DeclPtr> templates_;                 // owned generic templates
    std::unordered_map<std::string, StructDecl *> structTpl_;
    std::unordered_map<std::string, FunctionDecl *> fnTpl_;
    std::unordered_map<std::string, ClassDecl *> classTpl_;
    std::unordered_map<std::string, InterfaceDecl *> ifaceTpl_;
    std::unordered_map<std::string, TypeAliasDecl *> aliasTpl_;
    // class simple-name -> (base name, implemented interface names) for constraints.
    std::unordered_map<std::string, std::pair<std::string, std::vector<std::string>>> classImpl_;
    std::unordered_map<std::string, bool> done_;
    std::vector<DeclPtr> instances_;

    void error(int line, int col, const std::string &m) { errors.emplace_back("generic", line, col, m); }

    void rewriteType(TypeRef &t, const Subst &subst, int line, int col);
    std::string instantiateStruct(const std::string &name, const std::vector<TypeRef> &args,
                                  int line, int col);
    std::string instantiateFn(const std::string &name, const std::vector<TypeRef> &args,
                              int line, int col);
    std::string instantiateClass(const std::string &name, const std::vector<TypeRef> &args,
                                 int line, int col);
    std::string instantiateInterface(const std::string &name, const std::vector<TypeRef> &args,
                                     int line, int col);
    // Resolve a `Base<args>` reference (in extends/implements) to the instance name.
    void rewriteSuper(std::string &name, std::vector<TypeRef> &args, const Subst &subst,
                      bool isClass, int line, int col);
    std::string instName(const std::string &name, const std::vector<TypeRef> &args) const;
    bool satisfies(const TypeRef &arg, const std::string &bound) const;
    void checkBounds(const std::vector<std::string> &params,
                     const std::vector<std::string> &bounds,
                     const std::vector<TypeRef> &args, const std::string &what,
                     int line, int col);
    void walkBlock(Block &b, const Subst &subst);
    void walkStmt(Stmt *s, const Subst &subst);
    void walkExpr(Expr *e, const Subst &subst);
    void walkDeclTypes(Decl *d, const Subst &subst);
    void synthesizeTraits(); // copy interface/trait default methods into implementers
    void flattenBases();     // copy secondary bases' fields+methods (multiple inheritance)
};

std::string Mono::instName(const std::string &name, const std::vector<TypeRef> &args) const {
    std::string inst = name;
    for (const auto &a : args) inst += "$" + typeMangle(a);
    return inst;
}

// Does the concrete `arg` satisfy a `T: Bound` constraint? `arg` must be (a
// subclass of) the bound class, or a class implementing the bound interface.
bool Mono::satisfies(const TypeRef &arg, const std::string &bound) const {
    if (bound.empty()) return true;
    if (!arg.isNamed()) return false;
    std::string cur = arg.name;
    int guard = 0;
    while (!cur.empty() && guard++ < 100) {
        if (cur == bound) return true;
        auto it = classImpl_.find(cur);
        if (it == classImpl_.end()) return false;
        for (const auto &i : it->second.second) if (i == bound) return true;
        cur = it->second.first; // walk up the base chain
    }
    return false;
}

void Mono::checkBounds(const std::vector<std::string> &params,
                       const std::vector<std::string> &bounds,
                       const std::vector<TypeRef> &args, const std::string &what,
                       int line, int col) {
    for (size_t i = 0; i < params.size() && i < bounds.size() && i < args.size(); ++i) {
        if (bounds[i].empty()) continue;
        if (!satisfies(args[i], bounds[i]))
            error(line, col, "type argument '" + typeMangle(args[i]) + "' for '" +
                                 params[i] + "' in " + what +
                                 " does not satisfy the bound '" + bounds[i] + "'");
    }
}

void Mono::rewriteType(TypeRef &t, const Subst &subst, int line, int col) {
    if (t.kind == TypeRef::Kind::Named) {
        // A bare type parameter -> its concrete argument (then re-process it).
        auto it = subst.find(t.name);
        if (it != subst.end() && t.args.empty()) {
            t = it->second;
            rewriteType(t, {}, line, col);
            return;
        }
    }
    if (t.elem) rewriteType(*t.elem, subst, line, col);
    for (auto &a : t.args) rewriteType(a, subst, line, col);

    if (t.kind == TypeRef::Kind::Named && !t.args.empty()) {
        // A generic alias expands transparently: `Vec<int>` -> the target with
        // its type parameters substituted (no instance declaration emitted).
        if (aliasTpl_.count(t.name)) {
            TypeAliasDecl *tpl = aliasTpl_[t.name];
            if (tpl->typeParams.size() != t.args.size()) {
                error(line, col, "generic alias '" + t.name + "' expects " +
                                     std::to_string(tpl->typeParams.size()) +
                                     " type argument(s), got " + std::to_string(t.args.size()));
                t.args.clear();
                return;
            }
            Subst inner;
            for (size_t i = 0; i < t.args.size(); ++i) inner[tpl->typeParams[i]] = t.args[i];
            TypeRef expanded = tpl->target;
            rewriteType(expanded, inner, line, col);
            t = expanded;
            return;
        }
        if (structTpl_.count(t.name)) {
            t.name = instantiateStruct(t.name, t.args, line, col);
            t.args.clear();
        } else if (classTpl_.count(t.name)) {
            t.name = instantiateClass(t.name, t.args, line, col);
            t.args.clear();
        } else if (ifaceTpl_.count(t.name)) {
            t.name = instantiateInterface(t.name, t.args, line, col);
            t.args.clear();
        } else if (fnTpl_.count(t.name)) {
            error(line, col, "'" + t.name + "' is a generic function, not a type");
        } else {
            error(line, col, "unknown generic type '" + t.name + "'");
            t.args.clear();
        }
    }
}

std::string Mono::instantiateStruct(const std::string &name, const std::vector<TypeRef> &args,
                                    int line, int col) {
    std::string inst = name;
    for (const auto &a : args) inst += "$" + typeMangle(a);
    if (done_.count(inst)) return inst;
    done_[inst] = true;

    StructDecl *tpl = structTpl_[name];
    if (tpl->typeParams.size() != args.size()) {
        error(line, col, "generic struct '" + name + "' expects " +
                             std::to_string(tpl->typeParams.size()) + " type argument(s), got " +
                             std::to_string(args.size()));
        return inst;
    }
    Subst subst;
    for (size_t i = 0; i < args.size(); ++i) subst[tpl->typeParams[i]] = args[i];
    checkBounds(tpl->typeParams, tpl->typeParamBounds, args, "struct '" + name + "'", line, col);

    auto out = std::make_unique<StructDecl>();
    out->line = tpl->line; out->col = tpl->col;
    out->name = inst; out->fqName = inst;
    for (const auto &f : tpl->fields) {
        FieldDecl nf = f;          // copies access/readonly/name/type/line/col
        rewriteType(nf.type, subst, f.line, f.col);
        out->fields.push_back(std::move(nf));
    }
    instances_.push_back(std::move(out));
    return inst;
}

std::string Mono::instantiateFn(const std::string &name, const std::vector<TypeRef> &args,
                                int line, int col) {
    std::string inst = name;
    for (const auto &a : args) inst += "$" + typeMangle(a);
    if (done_.count(inst)) return inst;
    done_[inst] = true;

    FunctionDecl *tpl = fnTpl_[name];
    if (tpl->typeParams.size() != args.size()) {
        error(line, col, "generic function '" + name + "' expects " +
                             std::to_string(tpl->typeParams.size()) + " type argument(s), got " +
                             std::to_string(args.size()));
        return inst;
    }
    Subst subst;
    for (size_t i = 0; i < args.size(); ++i) subst[tpl->typeParams[i]] = args[i];
    checkBounds(tpl->typeParams, tpl->typeParamBounds, args, "function '" + name + "'", line, col);

    auto out = std::make_unique<FunctionDecl>();
    out->line = tpl->line; out->col = tpl->col;
    out->name = inst; out->fqName = inst; out->isAsync = tpl->isAsync;
    for (const auto &p : tpl->params) {
        Param np = p;
        rewriteType(np.type, subst, tpl->line, tpl->col);
        out->params.push_back(std::move(np));
    }
    out->returnType = tpl->returnType;
    rewriteType(out->returnType, subst, tpl->line, tpl->col);
    out->body = cloneBlock(tpl->body);
    walkBlock(out->body, subst);
    instances_.push_back(std::move(out));
    return inst;
}

void Mono::rewriteSuper(std::string &name, std::vector<TypeRef> &args, const Subst &subst,
                        bool isClass, int line, int col) {
    if (args.empty()) return;
    for (auto &a : args) rewriteType(a, subst, line, col);
    if (isClass && classTpl_.count(name)) name = instantiateClass(name, args, line, col);
    else if (!isClass && ifaceTpl_.count(name)) name = instantiateInterface(name, args, line, col);
    else error(line, col, "unknown generic supertype '" + name + "'");
    args.clear();
}

std::string Mono::instantiateInterface(const std::string &name, const std::vector<TypeRef> &args,
                                       int line, int col) {
    std::string inst = instName(name, args);
    if (done_.count(inst)) return inst;
    done_[inst] = true;
    InterfaceDecl *tpl = ifaceTpl_[name];
    if (tpl->typeParams.size() != args.size()) {
        error(line, col, "generic interface '" + name + "' expects " +
                             std::to_string(tpl->typeParams.size()) + " type argument(s), got " +
                             std::to_string(args.size()));
        return inst;
    }
    Subst subst;
    for (size_t i = 0; i < args.size(); ++i) subst[tpl->typeParams[i]] = args[i];
    auto out = std::make_unique<InterfaceDecl>();
    out->line = tpl->line; out->col = tpl->col;
    out->name = inst; out->fqName = inst;
    for (const auto &m : tpl->methods) {
        MethodDecl nm = cloneMethod(m);
        for (auto &p : nm.params) rewriteType(p.type, subst, m.line, m.col);
        rewriteType(nm.returnType, subst, m.line, m.col);
        out->methods.push_back(std::move(nm));
    }
    instances_.push_back(std::move(out));
    return inst;
}

std::string Mono::instantiateClass(const std::string &name, const std::vector<TypeRef> &args,
                                   int line, int col) {
    std::string inst = instName(name, args);
    if (done_.count(inst)) return inst;
    done_[inst] = true;

    ClassDecl *tpl = classTpl_[name];
    if (tpl->typeParams.size() != args.size()) {
        error(line, col, "generic class '" + name + "' expects " +
                             std::to_string(tpl->typeParams.size()) + " type argument(s), got " +
                             std::to_string(args.size()));
        return inst;
    }
    Subst subst;
    for (size_t i = 0; i < args.size(); ++i) subst[tpl->typeParams[i]] = args[i];
    checkBounds(tpl->typeParams, tpl->typeParamBounds, args, "class '" + name + "'", line, col);

    auto out = std::make_unique<ClassDecl>();
    out->line = tpl->line; out->col = tpl->col;
    out->name = inst; out->fqName = inst;
    out->isAbstract = tpl->isAbstract;
    out->baseName = tpl->baseName;
    out->baseArgs = tpl->baseArgs;
    rewriteSuper(out->baseName, out->baseArgs, subst, /*isClass=*/true, tpl->line, tpl->col);
    out->extraBases = tpl->extraBases;
    out->extraBaseArgs = tpl->extraBaseArgs;
    for (size_t i = 0; i < out->extraBases.size(); ++i)
        if (i < out->extraBaseArgs.size())
            rewriteSuper(out->extraBases[i], out->extraBaseArgs[i], subst, true, tpl->line, tpl->col);
    out->interfaceNames = tpl->interfaceNames;
    out->interfaceArgs = tpl->interfaceArgs;
    for (size_t i = 0; i < out->interfaceNames.size(); ++i)
        if (i < out->interfaceArgs.size())
            rewriteSuper(out->interfaceNames[i], out->interfaceArgs[i], subst,
                         /*isClass=*/false, tpl->line, tpl->col);
    for (const auto &f : tpl->fields) {
        FieldDecl nf = f;
        rewriteType(nf.type, subst, f.line, f.col);
        out->fields.push_back(std::move(nf));
    }
    for (const auto &sf : tpl->staticFields) {
        StaticFieldDecl ns;
        ns.access = sf.access; ns.isConst = sf.isConst; ns.name = sf.name;
        ns.type = sf.type; ns.line = sf.line; ns.col = sf.col;
        ns.init = cloneExpr(sf.init.get());
        rewriteType(ns.type, subst, sf.line, sf.col);
        walkExpr(ns.init.get(), subst);
        out->staticFields.push_back(std::move(ns));
    }
    out->constructor.present = tpl->constructor.present;
    out->constructor.line = tpl->constructor.line; out->constructor.col = tpl->constructor.col;
    out->constructor.params = tpl->constructor.params;
    for (auto &p : out->constructor.params) rewriteType(p.type, subst, tpl->constructor.line, tpl->constructor.col);
    out->constructor.body = cloneBlock(tpl->constructor.body);
    walkBlock(out->constructor.body, subst);
    for (const auto &m : tpl->methods) {
        MethodDecl nm = cloneMethod(m);
        for (auto &p : nm.params) rewriteType(p.type, subst, m.line, m.col);
        rewriteType(nm.returnType, subst, m.line, m.col);
        if (nm.hasBody) walkBlock(nm.body, subst);
        out->methods.push_back(std::move(nm));
    }
    instances_.push_back(std::move(out));
    return inst;
}

void Mono::walkBlock(Block &b, const Subst &subst) {
    for (auto &s : b.statements) walkStmt(s.get(), subst);
}

void Mono::walkStmt(Stmt *s, const Subst &subst) {
    if (!s) return;
    if (auto *x = dynamic_cast<LetStmt *>(s)) {
        if (x->hasDeclaredType) rewriteType(x->declaredType, subst, x->line, x->col);
        walkExpr(x->init.get(), subst);
    } else if (auto *x = dynamic_cast<AssignStmt *>(s)) {
        walkExpr(x->target.get(), subst); walkExpr(x->value.get(), subst);
    } else if (auto *x = dynamic_cast<ReturnStmt *>(s)) {
        walkExpr(x->value.get(), subst);
    } else if (auto *x = dynamic_cast<ExprStmt *>(s)) {
        walkExpr(x->expr.get(), subst);
    } else if (auto *x = dynamic_cast<BlockStmt *>(s)) {
        walkBlock(x->block, subst);
    } else if (auto *x = dynamic_cast<IfStmt *>(s)) {
        walkExpr(x->cond.get(), subst); walkBlock(x->thenBlock, subst); walkStmt(x->elseStmt.get(), subst);
    } else if (auto *x = dynamic_cast<WhileStmt *>(s)) {
        walkExpr(x->cond.get(), subst); walkBlock(x->body, subst);
    } else if (auto *x = dynamic_cast<DoWhileStmt *>(s)) {
        walkBlock(x->body, subst); walkExpr(x->cond.get(), subst);
    } else if (auto *x = dynamic_cast<SwitchStmt *>(s)) {
        walkExpr(x->subject.get(), subst);
        for (auto &c : x->cases) { for (auto &v : c.values) walkExpr(v.get(), subst); walkBlock(c.body, subst); }
        if (x->hasDefault) walkBlock(x->defaultBody, subst);
    } else if (auto *x = dynamic_cast<ForStmt *>(s)) {
        walkStmt(x->init.get(), subst); walkExpr(x->cond.get(), subst);
        walkStmt(x->update.get(), subst); walkBlock(x->body, subst);
    } else if (auto *x = dynamic_cast<ForInStmt *>(s)) {
        walkExpr(x->lo.get(), subst); walkExpr(x->hi.get(), subst); walkBlock(x->body, subst);
    } else if (auto *x = dynamic_cast<ThrowStmt *>(s)) {
        walkExpr(x->value.get(), subst);
    } else if (auto *x = dynamic_cast<TryStmt *>(s)) {
        walkBlock(x->tryBlock, subst);
        if (x->hasCatch) { rewriteType(x->catchType, subst, x->line, x->col); walkBlock(x->catchBlock, subst); }
        if (x->hasFinally) walkBlock(x->finallyBlock, subst);
    }
}

void Mono::walkExpr(Expr *e, const Subst &subst) {
    if (!e) return;
    if (auto *x = dynamic_cast<CastExpr *>(e)) {
        rewriteType(x->target, subst, e->line, e->col); walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<AsExpr *>(e)) {
        rewriteType(x->target, subst, e->line, e->col); walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<SizeofExpr *>(e)) {
        if (x->isType) rewriteType(x->target, subst, e->line, e->col);
        else walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<StructLiteralExpr *>(e)) {
        for (auto &a : x->typeArgs) rewriteType(a, subst, e->line, e->col);
        if (!x->typeArgs.empty()) {
            x->typeName = instantiateStruct(x->typeName, x->typeArgs, e->line, e->col);
            x->typeArgs.clear();
        }
        for (auto &f : x->fields) walkExpr(f.value.get(), subst);
    } else if (auto *x = dynamic_cast<NewExpr *>(e)) {
        for (auto &a : x->typeArgs) rewriteType(a, subst, e->line, e->col);
        if (!x->typeArgs.empty()) {
            if (classTpl_.count(x->className))
                x->className = instantiateClass(x->className, x->typeArgs, e->line, e->col);
            else
                error(e->line, e->col, "unknown generic class '" + x->className + "'");
            x->typeArgs.clear();
        }
        for (auto &a : x->args) walkExpr(a.get(), subst);
    } else if (auto *x = dynamic_cast<CallExpr *>(e)) {
        for (auto &a : x->typeArgs) rewriteType(a, subst, e->line, e->col);
        if (!x->typeArgs.empty()) {
            if (auto *callee = dynamic_cast<NameExpr *>(x->callee.get())) {
                if (fnTpl_.count(callee->name)) {
                    callee->name = instantiateFn(callee->name, x->typeArgs, e->line, e->col);
                } else {
                    error(e->line, e->col, "'" + callee->name + "' is not a generic function");
                }
            } else {
                error(e->line, e->col, "turbofish '::<...>' is only valid on a function name");
            }
            x->typeArgs.clear();
        }
        walkExpr(x->callee.get(), subst);
        for (auto &a : x->args) walkExpr(a.get(), subst);
    } else if (auto *x = dynamic_cast<BinaryExpr *>(e)) {
        walkExpr(x->left.get(), subst); walkExpr(x->right.get(), subst);
    } else if (auto *x = dynamic_cast<UnaryExpr *>(e)) {
        walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<TernaryExpr *>(e)) {
        walkExpr(x->cond.get(), subst); walkExpr(x->thenExpr.get(), subst); walkExpr(x->elseExpr.get(), subst);
    } else if (auto *x = dynamic_cast<IsExpr *>(e)) {
        walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<AwaitExpr *>(e)) {
        walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<AddrOfExpr *>(e)) {
        walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<DerefExpr *>(e)) {
        walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<CoalesceExpr *>(e)) {
        walkExpr(x->left.get(), subst); walkExpr(x->right.get(), subst);
    } else if (auto *x = dynamic_cast<IncDecExpr *>(e)) {
        walkExpr(x->target.get(), subst);
    } else if (auto *x = dynamic_cast<ArrayLiteralExpr *>(e)) {
        for (auto &el : x->elements) walkExpr(el.get(), subst);
    } else if (auto *x = dynamic_cast<IndexExpr *>(e)) {
        walkExpr(x->base.get(), subst); walkExpr(x->index.get(), subst);
    } else if (auto *x = dynamic_cast<MemberExpr *>(e)) {
        walkExpr(x->object.get(), subst);
    }
}

void Mono::walkDeclTypes(Decl *d, const Subst &subst) {
    if (auto *st = dynamic_cast<StructDecl *>(d)) {
        for (auto &f : st->fields) rewriteType(f.type, subst, f.line, f.col);
    } else if (auto *fn = dynamic_cast<FunctionDecl *>(d)) {
        for (auto &p : fn->params) rewriteType(p.type, subst, fn->line, fn->col);
        rewriteType(fn->returnType, subst, fn->line, fn->col);
        walkBlock(fn->body, subst);
    } else if (auto *cd = dynamic_cast<ClassDecl *>(d)) {
        rewriteSuper(cd->baseName, cd->baseArgs, subst, /*isClass=*/true, cd->line, cd->col);
        for (size_t i = 0; i < cd->extraBases.size(); ++i)
            if (i < cd->extraBaseArgs.size())
                rewriteSuper(cd->extraBases[i], cd->extraBaseArgs[i], subst,
                             /*isClass=*/true, cd->line, cd->col);
        for (size_t i = 0; i < cd->interfaceNames.size(); ++i)
            if (i < cd->interfaceArgs.size())
                rewriteSuper(cd->interfaceNames[i], cd->interfaceArgs[i], subst,
                             /*isClass=*/false, cd->line, cd->col);
        for (auto &f : cd->fields) rewriteType(f.type, subst, f.line, f.col);
        for (auto &d : cd->delegates) rewriteType(d.type, subst, cd->line, cd->col);
        for (auto &sf : cd->staticFields) {
            rewriteType(sf.type, subst, sf.line, sf.col);
            walkExpr(sf.init.get(), subst);
        }
        for (auto &p : cd->constructor.params) rewriteType(p.type, subst, cd->line, cd->col);
        if (cd->constructor.present) walkBlock(cd->constructor.body, subst);
        for (auto &m : cd->methods) {
            for (auto &p : m.params) rewriteType(p.type, subst, m.line, m.col);
            rewriteType(m.returnType, subst, m.line, m.col);
            if (m.hasBody) walkBlock(m.body, subst);
        }
    } else if (auto *g = dynamic_cast<GlobalVarDecl *>(d)) {
        if (g->hasDeclaredType) rewriteType(g->declaredType, subst, g->line, g->col);
        walkExpr(g->init.get(), subst);
    } else if (auto *al = dynamic_cast<TypeAliasDecl *>(d)) {
        rewriteType(al->target, subst, al->line, al->col);
    }
}

void Mono::run() {
    // 0) Record class inheritance/implements (by simple name) for constraint checks.
    for (auto &d : program_.decls)
        if (auto *cd = dynamic_cast<ClassDecl *>(d.get()))
            classImpl_[cd->name] = {cd->baseName, cd->interfaceNames};

    // 1) Split templates from concrete decls (templates kept alive in templates_).
    std::vector<DeclPtr> kept;
    for (auto &d : program_.decls) {
        if (auto *st = dynamic_cast<StructDecl *>(d.get()); st && !st->typeParams.empty()) {
            structTpl_[st->name] = st; templates_.push_back(std::move(d));
        } else if (auto *fn = dynamic_cast<FunctionDecl *>(d.get()); fn && !fn->typeParams.empty()) {
            fnTpl_[fn->name] = fn; templates_.push_back(std::move(d));
        } else if (auto *cd = dynamic_cast<ClassDecl *>(d.get()); cd && !cd->typeParams.empty()) {
            classTpl_[cd->name] = cd; templates_.push_back(std::move(d));
        } else if (auto *id = dynamic_cast<InterfaceDecl *>(d.get()); id && !id->typeParams.empty()) {
            ifaceTpl_[id->name] = id; templates_.push_back(std::move(d));
        } else if (auto *al = dynamic_cast<TypeAliasDecl *>(d.get()); al && !al->typeParams.empty()) {
            aliasTpl_[al->name] = al; templates_.push_back(std::move(d));
        } else {
            kept.push_back(std::move(d));
        }
    }

    // 2) Rewrite + collect instantiations over every concrete decl.
    for (auto &d : kept) walkDeclTypes(d.get(), {});

    // 3) Emit: concrete decls first, then the generated instances.
    program_.decls = std::move(kept);
    for (auto &inst : instances_) program_.decls.push_back(std::move(inst));

    // 4) Multiple inheritance: flatten secondary bases into each class.
    flattenBases();
    // 5) Trait/interface default methods are copied into each implementer.
    synthesizeTraits();
}

void Mono::flattenBases() {
    std::unordered_map<std::string, ClassDecl *> classes;
    for (auto &d : program_.decls)
        if (auto *c = dynamic_cast<ClassDecl *>(d.get())) classes[c->name] = c;

    // Does `c` already provide a field/method `name` (own or via the primary chain)?
    std::function<bool(ClassDecl *, const std::string &, bool)> has =
        [&](ClassDecl *c, const std::string &name, bool field) -> bool {
        std::string cur = c->name;
        for (int g = 0; !cur.empty() && g < 100; ++g) {
            auto it = classes.find(cur);
            if (it == classes.end()) break;
            if (field) { for (auto &f : it->second->fields) if (f.name == name) return true; }
            else { for (auto &m : it->second->methods) if (m.name == name) return true; }
            cur = it->second->baseName;
        }
        return false;
    };
    for (auto &d : program_.decls) {
        auto *c = dynamic_cast<ClassDecl *>(d.get());
        if (!c || c->extraBases.empty()) continue;
        // Collect each secondary base's full inheritance chain (deduped).
        std::vector<ClassDecl *> chain;
        std::vector<std::string> seen;
        std::function<void(const std::string &)> collect = [&](const std::string &nm) {
            if (std::find(seen.begin(), seen.end(), nm) != seen.end()) return;
            seen.push_back(nm);
            auto it = classes.find(nm);
            if (it == classes.end()) return;
            ClassDecl *b = it->second;
            chain.push_back(b);
            if (!b->baseName.empty()) collect(b->baseName);
            for (auto &eb : b->extraBases) collect(eb);
        };
        for (const auto &bn : c->extraBases) collect(bn);
        for (ClassDecl *b : chain) {
            for (const auto &f : b->fields)
                if (!has(c, f.name, /*field=*/true)) c->fields.push_back(f);
            for (const auto &m : b->methods)
                if (!m.isStatic && !has(c, m.name, /*field=*/false))
                    c->methods.push_back(cloneMethod(m));
        }
    }
}

void Mono::synthesizeTraits() {
    std::unordered_map<std::string, InterfaceDecl *> ifaces;
    std::unordered_map<std::string, ClassDecl *> classes;
    for (auto &d : program_.decls) {
        if (auto *i = dynamic_cast<InterfaceDecl *>(d.get())) ifaces[i->name] = i;
        else if (auto *c = dynamic_cast<ClassDecl *>(d.get())) classes[c->name] = c;
    }
    auto classHasMethod = [&](ClassDecl *c, const std::string &name) {
        std::string cur = c->name;
        for (int g = 0; !cur.empty() && g < 100; ++g) {
            auto it = classes.find(cur);
            if (it == classes.end()) break;
            for (auto &m : it->second->methods) if (m.name == name) return true;
            cur = it->second->baseName;
        }
        return false;
    };
    for (auto &d : program_.decls) {
        auto *c = dynamic_cast<ClassDecl *>(d.get());
        if (!c) continue;
        // Implemented interfaces across the whole base chain.
        std::vector<std::string> names;
        std::string cur = c->name;
        for (int g = 0; !cur.empty() && g < 100; ++g) {
            auto it = classes.find(cur);
            if (it == classes.end()) break;
            for (auto &in : it->second->interfaceNames)
                if (std::find(names.begin(), names.end(), in) == names.end()) names.push_back(in);
            cur = it->second->baseName;
        }
        Subst selfSubst;
        selfSubst["Self"] = TypeRef::named(c->fqName);
        for (const auto &in : names) {
            auto it = ifaces.find(in);
            if (it == ifaces.end()) continue;
            for (const auto &m : it->second->methods) {
                if (!m.hasBody) continue;             // abstract: class must provide it
                if (classHasMethod(c, m.name)) continue; // overridden / already present
                MethodDecl nm = cloneMethod(m);
                nm.access = Access::Public;
                for (auto &p : nm.params) rewriteType(p.type, selfSubst, m.line, m.col);
                rewriteType(nm.returnType, selfSubst, m.line, m.col);
                walkBlock(nm.body, selfSubst);
                c->methods.push_back(std::move(nm));
            }
        }
    }
}

} // namespace

std::vector<GravError> monomorphize(Program &program) {
    Mono m(program);
    m.run();
    return m.errors;
}

} // namespace grav
