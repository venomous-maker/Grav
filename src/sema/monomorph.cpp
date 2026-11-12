#include "sema/monomorph.h"
#include "sema/typechecker.h"

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
        case TypeRef::Kind::Int: {
            int b = t.numBits();
            if (!t.isUnsigned && b == 32) return "int";
            return (t.isUnsigned ? "u" : "i") + std::to_string(b);
        }
        case TypeRef::Kind::Float: return t.numBits() == 32 ? "float32" : "float";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "string";
        case TypeRef::Kind::Binary: return "binary";
        case TypeRef::Kind::Any: return "any";
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
        case TypeRef::Kind::Future: return "fut_" + (t.elem ? typeMangle(*t.elem) : "void");
        case TypeRef::Kind::Nullable: return (t.elem ? typeMangle(*t.elem) : "v") + "_opt";
        case TypeRef::Kind::Function: { // fn(int, string) -> bool  =>  fn_int_string_r_bool
            std::string s = "fn";
            for (const auto &p : t.fnParams) s += "_" + typeMangle(p);
            return s + "_r_" + (t.elem ? typeMangle(*t.elem) : "void");
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
        auto n = mkE<NameExpr>(e); n->name = x->name; n->typeArgs = x->typeArgs; return n;
    }
    if (dynamic_cast<const SelfExpr *>(e)) return mkE<SelfExpr>(e);
    if (dynamic_cast<const SuperExpr *>(e)) return mkE<SuperExpr>(e);
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
        n->target = x->target;
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
    if (auto *x = dynamic_cast<const LambdaExpr *>(e)) {
        auto n = mkE<LambdaExpr>(e);
        n->params = x->params; n->paramTyped = x->paramTyped;
        n->hasReturnType = x->hasReturnType; n->returnType = x->returnType;
        n->isArrow = x->isArrow; n->isMatch = x->isMatch; n->selfName = x->selfName; n->exprBody = cloneExpr(x->exprBody.get());
        n->body = cloneBlock(x->body);
        return n;
    }
    if (auto *x = dynamic_cast<const IndexExpr *>(e)) {
        auto n = mkE<IndexExpr>(e); n->base = cloneExpr(x->base.get());
        n->index = cloneExpr(x->index.get());
        n->isSlice = x->isSlice; n->sliceInclusive = x->sliceInclusive; n->sliceHi = cloneExpr(x->sliceHi.get());
        for (const auto &m : x->more) n->more.push_back(cloneExpr(m.get()));
        return n;
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
        auto n = mkE<CallExpr>(e); n->typeArgs = x->typeArgs; n->kind = x->kind; n->spreadLast = x->spreadLast;
        n->targetName = x->targetName; n->methodName = x->methodName;
        n->ownerClass = x->ownerClass; n->slotOwner = x->slotOwner; n->ifaceDispatch = x->ifaceDispatch;
        n->callee = cloneExpr(x->callee.get());
        for (const auto &a : x->args) n->args.push_back(cloneExpr(a.get()));
        return n;
    }
    if (auto *x = dynamic_cast<const NamedArgExpr *>(e)) {
        auto n = mkE<NamedArgExpr>(e); n->name = x->name; n->value = cloneExpr(x->value.get()); return n;
    }
    if (auto *x = dynamic_cast<const DefaultArgExpr *>(e)) {
        auto n = mkE<DefaultArgExpr>(e); n->ns = x->ns; n->value = cloneExpr(x->value.get()); return n;
    }
    return nullptr; // unreachable for known nodes
}

StmtPtr cloneStmt(const Stmt *s) {
    if (!s) return nullptr;
    if (auto *x = dynamic_cast<const LetStmt *>(s)) {
        auto n = mkS<LetStmt>(s); n->name = x->name; n->isConst = x->isConst;
        n->hasDeclaredType = x->hasDeclaredType; n->declaredType = x->declaredType;
        n->resolvedType = x->resolvedType; n->quietUnused = x->quietUnused; n->init = cloneExpr(x->init.get()); return n;
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
        auto n = mkS<WhileStmt>(s); n->label = x->label; n->cond = cloneExpr(x->cond.get()); n->body = cloneBlock(x->body); return n;
    }
    if (auto *x = dynamic_cast<const DoWhileStmt *>(s)) {
        auto n = mkS<DoWhileStmt>(s); n->label = x->label; n->body = cloneBlock(x->body); n->cond = cloneExpr(x->cond.get()); return n;
    }
    if (auto *x = dynamic_cast<const SwitchStmt *>(s)) {
        auto n = mkS<SwitchStmt>(s); n->subject = cloneExpr(x->subject.get());
        n->hasDefault = x->hasDefault; n->defaultBody = cloneBlock(x->defaultBody); n->isExpr = x->isExpr;
        for (const auto &c : x->cases) {
            SwitchCase nc;
            for (const auto &v : c.values) nc.values.push_back(cloneExpr(v.get()));
            for (const auto &h : c.rangeHi) nc.rangeHi.push_back(cloneExpr(h.get()));
            nc.rangeInclusive = c.rangeInclusive;
            nc.guard = cloneExpr(c.guard.get());
            nc.body = cloneBlock(c.body); n->cases.push_back(std::move(nc));
        }
        return n;
    }
    if (auto *x = dynamic_cast<const ForStmt *>(s)) {
        auto n = mkS<ForStmt>(s); n->label = x->label; n->init = cloneStmt(x->init.get()); n->cond = cloneExpr(x->cond.get());
        n->update = cloneStmt(x->update.get()); n->body = cloneBlock(x->body); return n;
    }
    if (auto *x = dynamic_cast<const ForInStmt *>(s)) {
        auto n = mkS<ForInStmt>(s); n->label = x->label; n->var = x->var; n->indexVar = x->indexVar;
        n->inclusive = x->inclusive;
        n->lo = cloneExpr(x->lo.get()); n->hi = cloneExpr(x->hi.get());
        n->step = cloneExpr(x->step.get()); n->body = cloneBlock(x->body); return n;
    }
    if (auto *x = dynamic_cast<const DeferStmt *>(s)) {
        auto n = mkS<DeferStmt>(s); n->body = cloneBlock(x->body); return n;
    }
    if (auto *x = dynamic_cast<const ThrowStmt *>(s)) {
        auto n = mkS<ThrowStmt>(s); n->value = cloneExpr(x->value.get()); return n;
    }
    if (auto *x = dynamic_cast<const TryStmt *>(s)) {
        auto n = mkS<TryStmt>(s);
        n->tryBlock = cloneBlock(x->tryBlock);
        for (const auto &c : x->catches) {
            CatchClause nc;
            nc.var = c.var; nc.type = c.type; nc.line = c.line; nc.col = c.col;
            nc.block = cloneBlock(c.block);
            n->catches.push_back(std::move(nc));
        }
        n->hasFinally = x->hasFinally; n->finallyBlock = cloneBlock(x->finallyBlock);
        return n;
    }
    if (auto *x = dynamic_cast<const BreakStmt *>(s)) { auto n = mkS<BreakStmt>(s); n->label = x->label; return n; }
    if (auto *x = dynamic_cast<const ContinueStmt *>(s)) { auto n = mkS<ContinueStmt>(s); n->label = x->label; return n; }
    return nullptr;
}

MethodDecl cloneMethod(const MethodDecl &m) {
    MethodDecl n;
    n.access = m.access; n.isStatic = m.isStatic; n.isAbstract = m.isAbstract;
    n.name = m.name; n.params = m.params; n.returnType = m.returnType;
    n.hasBody = m.hasBody; n.isFinal = m.isFinal; n.line = m.line; n.col = m.col;
    n.typeParams = m.typeParams;
    n.decorators = m.decorators;
    n.body = cloneBlock(m.body);
    return n;
}

// ---------------------------------------------------------------------------
// The monomorphizer.
// ---------------------------------------------------------------------------
using Subst = std::unordered_map<std::string, TypeRef>;

class Mono {
public:
    explicit Mono(Program &program, const InferenceTable *infer = nullptr) : program_(program), infer_(infer) {}
    void run();
    std::vector<GravError> errors;

private:
    // Type arguments the checker inferred inside a generic instance (keyed by the
    // instance, `Box$int`): filled in while that instance's copy is walked, since
    // its copies of the template share their call sites.
    const InferenceTable *infer_ = nullptr;
    std::string scope_;                          // the instance being walked ("" for none)
    struct ScopeGuard {
        std::string &slot, saved;
        ScopeGuard(std::string &s, const std::string &v) : slot(s), saved(s) { slot = v; }
        ~ScopeGuard() { slot = saved; }
    };
    void applyInferred(Expr *e);
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
    void semaError(int line, int col, const std::string &m) { errors.emplace_back("sema", line, col, m); }

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
    void rewriteParams(std::vector<Param> &ps, const Subst &subst, int line, int col);
    void synthesizeTraits(); // copy interface/trait default methods into implementers
    void instantiateSelfStatics(); // static methods naming Self, per class they are called on
    // Generic methods: templates (with the owning class's substitution) and the
    // `obj.m::<Args>(...)` requests seen so far; instantiated to a fixed point.
    struct GenericMethod { ClassDecl *owner; MethodDecl tpl; Subst classSubst; std::string ns; };
    std::vector<GenericMethod> genericMethods_;
    std::vector<std::pair<std::string, std::vector<TypeRef>>> methodRequests_;
    int instanceDepth(const std::string &cls, int guard) const;
    std::unordered_map<std::string, bool> methodReqSeen_, methodDone_;
    void extractGenericMethods(ClassDecl *cd, std::vector<MethodDecl> &from, const Subst &classSubst);
    // Namespace-aware type arguments: `Array<Shape>` written inside namespace
    // `geometry` means `Array<geometry.Shape>`, wherever the instance lands.
    std::unordered_map<std::string, bool> knownTypes_; // fully-qualified type names
    std::string currentNs_;                            // namespace of the code being walked
    void qualifyArg(TypeRef &t) const;
    // The first named type inside `t` that is not declared anywhere (nullptr if
    // all are known), so a bad type argument is reported once, at the use site,
    // instead of as errors throughout the instantiated library code.
    const TypeRef *unknownIn(const TypeRef &t) const;
    // `auto` type arguments are inferred later by the type checker.
    static bool isAuto(const TypeRef &t) {
        if (t.kind == TypeRef::Kind::Named && t.name == "auto" && t.args.empty()) return true;
        for (const auto &a : t.args) if (isAuto(a)) return true;
        for (const auto &p : t.fnParams) if (isAuto(p)) return true;
        return t.elem && isAuto(*t.elem);
    }
    static bool hasAuto(const std::vector<TypeRef> &args) {
        for (const auto &a : args) if (isAuto(a)) return true;
        return false;
    }
    // A template type for the exported signature tables: class parameters
    // substituted, other names namespace-qualified, its own parameters kept.
    TypeRef exportType(TypeRef t, const std::vector<std::string> &own, const Subst &cls) const;
    GenericSig exportSig(const std::vector<std::string> &own, const std::vector<Param> &params,
                         const TypeRef &ret, const Subst &cls) const;
    bool rejectUnknownArgs(const std::string &generic, const std::vector<TypeRef> &args,
                           int line, int col);
    struct NsScope {
        std::string &ref, saved;
        NsScope(std::string &r, const std::string &v) : ref(r), saved(r) { r = v; }
        ~NsScope() { ref = saved; }
    };
    static std::string nsOf(const std::string &fq) {
        auto dot = fq.rfind('.');
        return dot == std::string::npos ? "" : fq.substr(0, dot);
    }
    void instantiateGenericMethods();
    // The template `name` (as written in the current namespace) refers to,
    // following namespaces and `use` declarations; "" when it is not one.
    // The current namespace's own templates come first, global ones last, so library
    // code keeps its own Pair even when the program declares a Pair of its own.
    template <typename M> std::string findTpl(const std::string &name, const M &map) const {
        for (const auto &c : nameCandidates(name, currentNs_, program_.uses))
            if (map.count(c)) return c;
        return "";
    }
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
    // `elem` is a shared_ptr, so a copied TypeRef aliases the template's inner
    // type. Clone it before mutating so instantiations never corrupt the template.
    if (t.elem) t.elem = std::make_shared<TypeRef>(*t.elem);
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
    for (auto &a : t.args) { rewriteType(a, subst, line, col); qualifyArg(a); }
    if (hasAuto(t.args)) {
        error(line, col, "'auto' type arguments can only be inferred in calls and `new` "
                         "(e.g. new Array<auto>() or maxOf(1, 2)), not in a declared type");
        t = TypeRef::prim(TypeRef::Kind::Error);
        return;
    }
    for (auto &p : t.fnParams) rewriteType(p, subst, line, col);

    if (t.kind == TypeRef::Kind::Named && !t.args.empty()) {
        // A generic alias expands transparently: `Vec<int>` -> the target with
        // its type parameters substituted (no instance declaration emitted).
        if (std::string al = findTpl(t.name, aliasTpl_); !al.empty()) {
            TypeAliasDecl *tpl = aliasTpl_[al];
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
        // Resolve the template through namespaces and `use`; `Opt.Some<int>`
        // names the data enum variant template `Opt$Some`.
        std::string found;
        for (const std::string &cand : {t.name, [&] {
                 auto dot = t.name.rfind('.');
                 return dot == std::string::npos ? std::string()
                                                 : t.name.substr(0, dot) + "$" + t.name.substr(dot + 1);
             }()}) {
            if (cand.empty() || !found.empty()) continue;
            for (const std::string &f : {findTpl(cand, structTpl_), findTpl(cand, classTpl_),
                                         findTpl(cand, ifaceTpl_), findTpl(cand, fnTpl_)})
                if (found.empty() && !f.empty()) found = f;
        }
        if (!found.empty()) t.name = found;
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
    if (rejectUnknownArgs(name, args, line, col)) return name;
    if (done_.count(inst)) return inst;
    done_[inst] = true;
    knownTypes_[inst] = true;
    program_.generics.instances[inst] = {name, args};

    StructDecl *tpl = structTpl_[name];
    NsScope nsScope(currentNs_, nsOf(tpl->fqName)); // names in the template are its own
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
    StructDecl *made = out.get();
    instances_.push_back(std::move(out));
    // Its methods, for the same type arguments.
    for (const auto &[method, fn] : tpl->methodFns)
        if (std::string fq = findTpl(fn, fnTpl_); !fq.empty())
            made->methodFns[method] = instantiateFn(fq, args, line, col);
    return inst;
}

std::string Mono::instantiateFn(const std::string &name, const std::vector<TypeRef> &args,
                                int line, int col) {
    std::string inst = name;
    for (const auto &a : args) inst += "$" + typeMangle(a);
    if (rejectUnknownArgs(name, args, line, col)) return name;
    if (done_.count(inst)) return inst;
    done_[inst] = true;
    knownTypes_[inst] = true;
    program_.generics.instances[inst] = {name, args};

    FunctionDecl *tpl = fnTpl_[name];
    NsScope nsScope(currentNs_, nsOf(tpl->fqName)); // names in the template are its own
    if (tpl->typeParams.size() != args.size()) {
        error(line, col, "generic function '" + name + "' expects " +
                             std::to_string(tpl->typeParams.size()) + " type argument(s), got " +
                             std::to_string(args.size()));
        return inst;
    }
    Subst subst;
    for (size_t i = 0; i < args.size(); ++i) subst[tpl->typeParams[i]] = args[i];
    checkBounds(tpl->typeParams, tpl->typeParamBounds, args, "function '" + name + "'", line, col);

    ScopeGuard scope(scope_, inst);
    auto out = std::make_unique<FunctionDecl>();
    out->line = tpl->line; out->col = tpl->col;
    out->name = inst; out->fqName = inst; out->isAsync = tpl->isAsync;
    out->params = tpl->params;
    rewriteParams(out->params, subst, tpl->line, tpl->col);
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
    std::string fq = isClass ? findTpl(name, classTpl_) : findTpl(name, ifaceTpl_);
    if (isClass && !fq.empty()) name = instantiateClass(fq, args, line, col);
    else if (!isClass && !fq.empty()) name = instantiateInterface(fq, args, line, col);
    else error(line, col, "unknown generic supertype '" + name + "'");
    args.clear();
}

std::string Mono::instantiateInterface(const std::string &name, const std::vector<TypeRef> &args,
                                       int line, int col) {
    std::string inst = instName(name, args);
    if (rejectUnknownArgs(name, args, line, col)) return name;
    if (done_.count(inst)) return inst;
    done_[inst] = true;
    knownTypes_[inst] = true;
    program_.generics.instances[inst] = {name, args};
    InterfaceDecl *tpl = ifaceTpl_[name];
    NsScope nsScope(currentNs_, nsOf(tpl->fqName)); // names in the template are its own
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
        rewriteParams(nm.params, subst, m.line, m.col);
        rewriteType(nm.returnType, subst, m.line, m.col);
        out->methods.push_back(std::move(nm));
    }
    instances_.push_back(std::move(out));
    return inst;
}

std::string Mono::instantiateClass(const std::string &name, const std::vector<TypeRef> &args,
                                   int line, int col) {
    std::string inst = instName(name, args);
    if (rejectUnknownArgs(name, args, line, col)) return name;
    if (done_.count(inst)) return inst;
    done_[inst] = true;
    knownTypes_[inst] = true;
    program_.generics.instances[inst] = {name, args};

    ClassDecl *tpl = classTpl_[name];
    NsScope nsScope(currentNs_, nsOf(tpl->fqName)); // names in the template are its own
    if (tpl->typeParams.size() != args.size()) {
        error(line, col, "generic class '" + name + "' expects " +
                             std::to_string(tpl->typeParams.size()) + " type argument(s), got " +
                             std::to_string(args.size()));
        return inst;
    }
    Subst subst;
    for (size_t i = 0; i < args.size(); ++i) subst[tpl->typeParams[i]] = args[i];
    checkBounds(tpl->typeParams, tpl->typeParamBounds, args, "class '" + name + "'", line, col);

    ScopeGuard scope(scope_, inst);
    auto out = std::make_unique<ClassDecl>();
    out->line = tpl->line; out->col = tpl->col;
    out->name = inst; out->fqName = inst;
    out->isAbstract = tpl->isAbstract;
    out->isFinal = tpl->isFinal;
    out->enumVariants = tpl->enumVariants;
    out->enumTemplate = tpl->enumTemplate.empty() ? "" : name; // instances: the base template
    if (tpl->variantTag >= 0) out->enumTemplate = tpl->baseName;
    out->variantTag = tpl->variantTag;
    out->variantFields = tpl->variantFields;
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
    out->decorators = tpl->decorators;
    out->delegates = tpl->delegates; // `uses field: T` forwarding survives instantiation
    for (auto &d : out->delegates) rewriteType(d.type, subst, tpl->line, tpl->col);
    for (const auto &f : tpl->fields) {
        FieldDecl nf = f;
        rewriteType(nf.type, subst, f.line, f.col);
        if (nf.init) {                                   // (its own copy, with this instance's types)
            nf.init = std::shared_ptr<Expr>(cloneExpr(f.init.get()).release());
            walkExpr(nf.init.get(), subst);
        }
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
    rewriteParams(out->constructor.params, subst, tpl->constructor.line, tpl->constructor.col);
    out->constructor.body = cloneBlock(tpl->constructor.body);
    walkBlock(out->constructor.body, subst);
    for (const auto &x : tpl->extraCtors) {
        ConstructorDecl nc;
        nc.present = true; nc.line = x.line; nc.col = x.col; nc.params = x.params;
        rewriteParams(nc.params, subst, x.line, x.col);
        nc.body = cloneBlock(x.body);
        walkBlock(nc.body, subst);
        out->extraCtors.push_back(std::move(nc));
    }
    out->destructor.present = tpl->destructor.present;
    out->destructor.line = tpl->destructor.line; out->destructor.col = tpl->destructor.col;
    out->destructor.body = cloneBlock(tpl->destructor.body);
    walkBlock(out->destructor.body, subst);
    for (const auto &m : tpl->methods) {
        if (!m.typeParams.empty()) { // kept as a template until a call requests it
            genericMethods_.push_back({out.get(), cloneMethod(m), subst, currentNs_});
            program_.generics.methods[out->fqName + "." + m.name].push_back(
                exportSig(m.typeParams, m.params, m.returnType, subst));
            continue;
        }
        MethodDecl nm = cloneMethod(m);
        rewriteParams(nm.params, subst, m.line, m.col);
        rewriteType(nm.returnType, subst, m.line, m.col);
        if (nm.hasBody) walkBlock(nm.body, subst);
        out->methods.push_back(std::move(nm));
    }
    std::vector<std::string> variants = out->enumVariants;
    instances_.push_back(std::move(out));
    // A data enum's variants exist whenever the enum does: `match` needs them all.
    for (const auto &v : variants) instantiateClass(name + "$" + v, args, line, col);
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
        for (auto &c : x->cases) {
            for (auto &v : c.values) walkExpr(v.get(), subst);
            for (auto &h : c.rangeHi) walkExpr(h.get(), subst);
            walkExpr(c.guard.get(), subst);
            walkBlock(c.body, subst);
        }
        if (x->hasDefault) walkBlock(x->defaultBody, subst);
    } else if (auto *x = dynamic_cast<ForStmt *>(s)) {
        walkStmt(x->init.get(), subst); walkExpr(x->cond.get(), subst);
        walkStmt(x->update.get(), subst); walkBlock(x->body, subst);
    } else if (auto *x = dynamic_cast<ForInStmt *>(s)) {
        walkExpr(x->lo.get(), subst); walkExpr(x->hi.get(), subst);
        walkExpr(x->step.get(), subst); walkBlock(x->body, subst);
    } else if (auto *x = dynamic_cast<DeferStmt *>(s)) {
        walkBlock(x->body, subst);
    } else if (auto *x = dynamic_cast<ThrowStmt *>(s)) {
        walkExpr(x->value.get(), subst);
    } else if (auto *x = dynamic_cast<TryStmt *>(s)) {
        walkBlock(x->tryBlock, subst);
        for (auto &c : x->catches) { rewriteType(c.type, subst, c.line, c.col); walkBlock(c.block, subst); }
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
        // `sizeof(T)` parses T as a name; if T is a type parameter, treat it as a type.
        if (!x->isType) {
            if (auto *nm = dynamic_cast<NameExpr *>(x->operand.get());
                nm && subst.count(nm->name)) {
                x->isType = true;
                x->target = TypeRef::named(nm->name);
                x->operand.reset();
            }
        }
        if (x->isType) rewriteType(x->target, subst, e->line, e->col);
        else walkExpr(x->operand.get(), subst);
    } else if (auto *x = dynamic_cast<StructLiteralExpr *>(e)) {
        for (auto &a : x->typeArgs) { rewriteType(a, subst, e->line, e->col); qualifyArg(a); }
        if (!x->typeArgs.empty()) {
            std::string fq = findTpl(x->typeName, structTpl_);
            x->typeName = instantiateStruct(fq.empty() ? x->typeName : fq, x->typeArgs, e->line, e->col);
            x->typeArgs.clear();
        }
        for (auto &f : x->fields) walkExpr(f.value.get(), subst);
    } else if (auto *x = dynamic_cast<NameExpr *>(e); x && !x->typeArgs.empty()) {
        // `Stack<int>.of(3)` names the instance's static member.
        for (auto &a : x->typeArgs) { rewriteType(a, subst, e->line, e->col); qualifyArg(a); }
        if (std::string fq = findTpl(x->name, classTpl_); !fq.empty())
            x->name = instantiateClass(fq, x->typeArgs, e->line, e->col);
        else
            error(e->line, e->col, "unknown generic class '" + x->name + "'");
        x->typeArgs.clear();
    } else if (auto *x = dynamic_cast<NewExpr *>(e)) {
        applyInferred(e);
        for (auto &a : x->typeArgs) { rewriteType(a, subst, e->line, e->col); qualifyArg(a); }
        if (!x->typeArgs.empty() && !hasAuto(x->typeArgs)) {
            if (std::string fq = findTpl(x->className, classTpl_); !fq.empty())
                x->className = instantiateClass(fq, x->typeArgs, e->line, e->col);
            else
                error(e->line, e->col, "unknown generic class '" + x->className + "'");
            x->typeArgs.clear();
        }
        for (auto &a : x->args) walkExpr(a.get(), subst);
    } else if (auto *x = dynamic_cast<CallExpr *>(e)) {
        applyInferred(e);
        // `reflect(T)` for a type parameter T: the class it stands for here.
        if (auto *callee = dynamic_cast<NameExpr *>(x->callee.get()); callee && callee->name == "reflect" && x->args.size() == 1)
            if (auto *arg = dynamic_cast<NameExpr *>(x->args[0].get()); arg && arg->typeArgs.empty())
                if (auto it = subst.find(arg->name); it != subst.end()) {
                    TypeRef t = it->second;
                    rewriteType(t, subst, e->line, e->col);
                    qualifyArg(t);
                    if (t.isNamed()) arg->name = t.name;
                }
        for (auto &a : x->typeArgs) { rewriteType(a, subst, e->line, e->col); qualifyArg(a); }
        if (!x->typeArgs.empty() && !hasAuto(x->typeArgs)) {
            if (auto *callee = dynamic_cast<NameExpr *>(x->callee.get())) {
                if (std::string fq = findTpl(callee->name, fnTpl_); !fq.empty()) {
                    callee->name = instantiateFn(fq, x->typeArgs, e->line, e->col);
                } else {
                    error(e->line, e->col, "'" + callee->name + "' is not a generic function");
                }
            } else if (auto *mem = dynamic_cast<MemberExpr *>(x->callee.get())) {
                // `obj.m::<Args>(...)` -> the method instance `m$Args`, requested for
                // every class that declares a generic method `m`.
                std::string key = instName(mem->member, x->typeArgs);
                if (!methodReqSeen_.count(key)) {
                    methodReqSeen_[key] = true;
                    methodRequests_.push_back({mem->member, x->typeArgs});
                    program_.generics.methodInstances[key] = {mem->member, x->typeArgs};
                }
                mem->member = key;
            } else {
                error(e->line, e->col, "turbofish '::<...>' needs a function or method name");
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
        rewriteType(x->target, subst, e->line, e->col);
        if (x->target.isNamed()) x->typeName = x->target.name;
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
    } else if (auto *x = dynamic_cast<LambdaExpr *>(e)) {
        for (size_t i = 0; i < x->params.size(); ++i)
            if (i < x->paramTyped.size() && x->paramTyped[i])
                rewriteType(x->params[i].type, subst, e->line, e->col);
        if (x->hasReturnType) rewriteType(x->returnType, subst, e->line, e->col);
        walkExpr(x->exprBody.get(), subst);
        walkBlock(x->body, subst);
    } else if (auto *x = dynamic_cast<IndexExpr *>(e)) {
        walkExpr(x->base.get(), subst); walkExpr(x->index.get(), subst);
        for (auto &m : x->more) walkExpr(m.get(), subst);
        walkExpr(x->sliceHi.get(), subst);
    } else if (auto *x = dynamic_cast<MemberExpr *>(e)) {
        walkExpr(x->object.get(), subst);
    } else if (auto *x = dynamic_cast<NamedArgExpr *>(e)) {
        walkExpr(x->value.get(), subst);
    }
}

void Mono::qualifyArg(TypeRef &t) const {
    // Nested types (fn(A) -> B, Future<T>, T*) are qualified too.
    if (t.elem) { t.elem = std::make_shared<TypeRef>(*t.elem); qualifyArg(*t.elem); }
    for (auto &p : t.fnParams) qualifyArg(p);
    for (auto &a : t.args) qualifyArg(a);
    if (t.kind != TypeRef::Kind::Named || !t.args.empty() || knownTypes_.count(t.name)) return;
    // Through enclosing namespaces and `use` declarations.
    for (const auto &cand : nameCandidates(t.name, currentNs_, program_.uses))
        if (knownTypes_.count(cand)) { t.name = cand; return; }
}

TypeRef Mono::exportType(TypeRef t, const std::vector<std::string> &own, const Subst &cls) const {
    if (t.kind == TypeRef::Kind::Named && t.args.empty()) {
        for (const auto &p : own) if (p == t.name) return t;
        auto it = cls.find(t.name);
        if (it != cls.end()) return it->second;
        qualifyArg(t);
        return t;
    }
    for (auto &a : t.args) a = exportType(a, own, cls);
    for (auto &p : t.fnParams) p = exportType(p, own, cls);
    if (t.elem) t.elem = std::make_shared<TypeRef>(exportType(*t.elem, own, cls));
    return t;
}

GenericSig Mono::exportSig(const std::vector<std::string> &own, const std::vector<Param> &params,
                           const TypeRef &ret, const Subst &cls) const {
    GenericSig g;
    g.typeParams = own;
    for (const auto &p : params) {
        g.params.push_back(exportType(p.type, own, cls));
        if (p.variadic) g.variadic = true;
        g.paramNames.push_back(p.name);
        g.defaults.push_back(p.defaultValue);
    }
    g.ns = currentNs_;
    g.ret = exportType(ret, own, cls);
    return g;
}

const TypeRef *Mono::unknownIn(const TypeRef &t) const {
    if (t.kind == TypeRef::Kind::Named) {
        if (t.args.empty() && t.name != "Self" && !knownTypes_.count(t.name)) return &t;
        for (const auto &a : t.args) if (const TypeRef *u = unknownIn(a)) return u;
    }
    if (t.elem) if (const TypeRef *u = unknownIn(*t.elem)) return u;
    for (const auto &p : t.fnParams) if (const TypeRef *u = unknownIn(p)) return u;
    return nullptr;
}

bool Mono::rejectUnknownArgs(const std::string &generic, const std::vector<TypeRef> &args,
                             int line, int col) {
    for (const auto &a : args) {
        const TypeRef *u = unknownIn(a);
        if (!u || u->name == "auto") continue;
        std::string msg = "unknown type '" + u->name + "' in the type arguments of '" + generic + "'";
        if (u->name == "auto" || u->name == "any" || u->name == "var" || u->name == "dynamic" ||
            u->name == "Any" || u->name == "object")
            msg += "; Grav has no '" + u->name + "' type — write the concrete type (e.g. "
                   "Array<Future<int>>), or let `let x = ...` infer it from the initializer";
        error(line, col, msg);
        return true;
    }
    return false;
}

// Parameter types, and default values: those are shared with the declaration
// they were copied from, so each copy gets its own, walked like any expression.
void Mono::rewriteParams(std::vector<Param> &ps, const Subst &subst, int line, int col) {
    for (auto &p : ps) {
        rewriteType(p.type, subst, line, col);
        if (!p.defaultValue) continue;
        p.defaultValue = std::shared_ptr<Expr>(cloneExpr(p.defaultValue.get()).release());
        walkExpr(p.defaultValue.get(), subst);
    }
}

void Mono::walkDeclTypes(Decl *d, const Subst &subst) {
    NsScope nsScope(currentNs_, nsOf(d->fqName));
    if (auto *t = dynamic_cast<TestDecl *>(d)) { walkBlock(t->body, subst); return; }
    if (auto *st = dynamic_cast<StructDecl *>(d)) {
        for (auto &f : st->fields) rewriteType(f.type, subst, f.line, f.col);
    } else if (auto *fn = dynamic_cast<FunctionDecl *>(d)) {
        rewriteParams(fn->params, subst, fn->line, fn->col);
        rewriteType(fn->returnType, subst, fn->line, fn->col);
        walkBlock(fn->body, subst);
    } else if (auto *cd = dynamic_cast<ClassDecl *>(d)) {
        extractGenericMethods(cd, cd->methods, subst);
        rewriteSuper(cd->baseName, cd->baseArgs, subst, /*isClass=*/true, cd->line, cd->col);
        for (size_t i = 0; i < cd->extraBases.size(); ++i)
            if (i < cd->extraBaseArgs.size())
                rewriteSuper(cd->extraBases[i], cd->extraBaseArgs[i], subst,
                             /*isClass=*/true, cd->line, cd->col);
        for (size_t i = 0; i < cd->interfaceNames.size(); ++i)
            if (i < cd->interfaceArgs.size())
                rewriteSuper(cd->interfaceNames[i], cd->interfaceArgs[i], subst,
                             /*isClass=*/false, cd->line, cd->col);
        for (auto &f : cd->fields) {
            rewriteType(f.type, subst, f.line, f.col);
            if (f.init) {
                f.init = std::shared_ptr<Expr>(cloneExpr(f.init.get()).release());
                walkExpr(f.init.get(), subst);
            }
        }
        for (auto &d : cd->delegates) rewriteType(d.type, subst, cd->line, cd->col);
        for (auto &sf : cd->staticFields) {
            rewriteType(sf.type, subst, sf.line, sf.col);
            walkExpr(sf.init.get(), subst);
        }
        rewriteParams(cd->constructor.params, subst, cd->line, cd->col);
        if (cd->constructor.present) walkBlock(cd->constructor.body, subst);
        for (auto &x : cd->extraCtors) {
            rewriteParams(x.params, subst, x.line, x.col);
            walkBlock(x.body, subst);
        }
        if (cd->destructor.present) walkBlock(cd->destructor.body, subst);
        for (auto &m : cd->methods) {
            rewriteParams(m.params, subst, m.line, m.col);
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
    // Static methods naming Self get their copies first, so that generic types
    // such as Array<Self> are concrete before generics are instantiated.
    instantiateSelfStatics();
    // 0) Every declared type's fully-qualified name (for qualifying type arguments).
    for (auto &d : program_.decls)
        if (dynamic_cast<ClassDecl *>(d.get()) || dynamic_cast<StructDecl *>(d.get()) ||
            dynamic_cast<InterfaceDecl *>(d.get()) || dynamic_cast<EnumDecl *>(d.get()) ||
            dynamic_cast<TypeAliasDecl *>(d.get()))
            knownTypes_[d->fqName] = true;
    // Record class inheritance/implements (by simple name) for constraint checks.
    for (auto &d : program_.decls)
        if (auto *cd = dynamic_cast<ClassDecl *>(d.get()))
            classImpl_[cd->name] = {cd->baseName, cd->interfaceNames};

    // 1) Split templates from concrete decls (templates kept alive in templates_).
    std::vector<DeclPtr> kept;
    for (auto &d : program_.decls) {
        if (auto *st = dynamic_cast<StructDecl *>(d.get()); st && !st->typeParams.empty()) {
            structTpl_[st->fqName] = st; templates_.push_back(std::move(d));
        } else if (auto *fn = dynamic_cast<FunctionDecl *>(d.get()); fn && !fn->typeParams.empty()) {
            fnTpl_[fn->fqName] = fn; templates_.push_back(std::move(d));
        } else if (auto *cd = dynamic_cast<ClassDecl *>(d.get()); cd && !cd->typeParams.empty()) {
            classTpl_[cd->fqName] = cd; templates_.push_back(std::move(d));
        } else if (auto *id = dynamic_cast<InterfaceDecl *>(d.get()); id && !id->typeParams.empty()) {
            ifaceTpl_[id->fqName] = id; templates_.push_back(std::move(d));
        } else if (auto *al = dynamic_cast<TypeAliasDecl *>(d.get()); al && !al->typeParams.empty()) {
            aliasTpl_[al->fqName] = al; templates_.push_back(std::move(d));
        } else {
            kept.push_back(std::move(d));
        }
    }

    // Export template signatures for type-argument inference in the checker.
    for (auto &[name, fn] : fnTpl_) {
        NsScope ns(currentNs_, nsOf(fn->fqName));
        program_.generics.functions[name].push_back(exportSig(fn->typeParams, fn->params, fn->returnType, {}));
    }
    for (auto &[name, cd] : classTpl_) {
        NsScope ns(currentNs_, nsOf(cd->fqName));
        TypeRef self = TypeRef::named(name);
        for (const auto &tp : cd->typeParams) self.args.push_back(TypeRef::named(tp));
        // A data enum variant's constructor produces the enum, so the expected
        // type infers it: `let o: Option<int> = Option.None`.
        if (cd->variantTag >= 0) { self = TypeRef::named(cd->baseName); self.args = cd->baseArgs; }
        auto add = [&](const std::vector<Param> &ps) {
            program_.generics.ctors[name].push_back(exportSig(cd->typeParams, ps, self, {}));
        };
        if (cd->constructor.present) add(cd->constructor.params);
        for (const auto &x : cd->extraCtors) add(x.params);
        if (!cd->constructor.present) add({});
    }

    // 2) Rewrite + collect instantiations over every concrete decl.
    for (auto &d : kept) walkDeclTypes(d.get(), {});
    instantiateGenericMethods();

    // 3) Emit: concrete decls first, then the generated instances.
    program_.decls = std::move(kept);
    for (auto &inst : instances_) program_.decls.push_back(std::move(inst));

    // 4) Multiple inheritance: flatten secondary bases into each class.
    flattenBases();
    // 5) Trait/interface default methods are copied into each implementer.
    synthesizeTraits();
}

void Mono::extractGenericMethods(ClassDecl *cd, std::vector<MethodDecl> &from,
                                 const Subst &classSubst) {
    std::vector<MethodDecl> keep;
    for (auto &m : from) {
        if (m.typeParams.empty()) { keep.push_back(std::move(m)); continue; }
        program_.generics.methods[cd->fqName + "." + m.name].push_back(
            exportSig(m.typeParams, m.params, m.returnType, classSubst));
        genericMethods_.push_back({cd, std::move(m), classSubst, currentNs_});
    }
    from = std::move(keep);
}

// How deeply generic instances nest inside class instance `cls`: Array<int> is 1,
// Array<Pair<string, int>> is 2.
int Mono::instanceDepth(const std::string &cls, int guard) const {
    auto it = program_.generics.instances.find(cls);
    if (it == program_.generics.instances.end() || guard > 32) return 0;
    std::function<int(const TypeRef &)> depth = [&](const TypeRef &t) -> int {
        int d = t.isNamed() ? instanceDepth(t.name, guard + 1) : 0;
        for (const auto &a : t.args) d = std::max(d, depth(a));
        for (const auto &a : t.fnParams) d = std::max(d, depth(a));
        if (t.elem) d = std::max(d, depth(*t.elem));
        return d;
    };
    int d = 0;
    for (const auto &a : it->second.second) d = std::max(d, depth(a));
    return 1 + d;
}

void Mono::instantiateGenericMethods() {
    for (bool progress = true; progress;) {
        progress = false;
        // Index loops: instantiating can add templates (new class instances) and
        // requests (calls inside the new bodies); later passes pick those up.
        for (size_t gi = 0; gi < genericMethods_.size(); ++gi) {
            for (size_t ri = 0; ri < methodRequests_.size(); ++ri) {
                std::string name = methodRequests_[ri].first;
                std::vector<TypeRef> args = methodRequests_[ri].second;
                if (genericMethods_[gi].tpl.name != name ||
                    genericMethods_[gi].tpl.typeParams.size() != args.size())
                    continue;
                ClassDecl *owner = genericMethods_[gi].owner;
                // A request names a method, not its receiver's class, so it reaches every
                // class with a generic method of that name. When the instance creates a
                // deeper class (Array<T>.zip<U> makes Array<Pair<T, U>>), that class gets the
                // request too, without end. Real code never nests generic instances this deep.
                if (instanceDepth(owner->name, 0) > 6) continue;
                NsScope nsScope(currentNs_, genericMethods_[gi].ns);
                std::string inst = instName(name, args);
                std::string key = owner->name + "." + inst;
                if (methodDone_.count(key)) continue;
                methodDone_[key] = true;
                progress = true;
                MethodDecl m = cloneMethod(genericMethods_[gi].tpl);
                Subst s = genericMethods_[gi].classSubst;
                for (size_t i = 0; i < args.size(); ++i) s[m.typeParams[i]] = args[i];
                m.name = inst;
                m.typeParams.clear();
                rewriteParams(m.params, s, m.line, m.col);
                rewriteType(m.returnType, s, m.line, m.col);
                if (m.hasBody) walkBlock(m.body, s);
                owner->methods.push_back(std::move(m));
            }
        }
    }
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
                rewriteParams(nm.params, selfSubst, m.line, m.col);
                rewriteType(nm.returnType, selfSubst, m.line, m.col);
                walkBlock(nm.body, selfSubst);
                c->methods.push_back(std::move(nm));
            }
        }
    }
}

// ── Self in static methods ──────────────────────────────────────────────────
// A static method that names Self (`static fn find(id: int) -> Self? { ...
// new Self(...) ... }`) works for whichever class it is called on. The class that
// declares it keeps a copy with Self as itself; each call through a subclass that
// inherits it (`User.find(1)`) gets its own copy `find$User`, made in the
// declaring class (so that class's private members stay in reach) with Self as
// User, and the call is pointed at that copy. Calls inside a copy (`Self.other()`)
// are followed the same way.
namespace {
bool isSelfType(const TypeRef &t) { return t.isNamed() && t.name == "Self" && t.args.empty(); }
bool typeNamesSelf(const TypeRef &t) {
    if (isSelfType(t)) return true;
    for (const auto &a : t.args) if (typeNamesSelf(a)) return true;
    for (const auto &a : t.fnParams) if (typeNamesSelf(a)) return true;
    return t.elem && typeNamesSelf(*t.elem);
}
void substSelf(TypeRef &t, const std::string &fq) {
    if (isSelfType(t)) { t = TypeRef::named(fq); return; }
    for (auto &a : t.args) substSelf(a, fq);
    for (auto &a : t.fnParams) substSelf(a, fq);
    if (t.elem && typeNamesSelf(*t.elem)) {            // (copied: the element may be shared)
        TypeRef e = *t.elem;
        substSelf(e, fq);
        t.elem = std::make_shared<TypeRef>(std::move(e));
    }
}
// `a.b.C` for a dotted class name, as the parser would build it.
ExprPtr classRef(const std::string &fq, int line, int col) {
    size_t start = 0, dot = fq.find('.');
    auto head = std::make_unique<NameExpr>();
    head->name = fq.substr(0, dot);
    head->line = line; head->col = col;
    ExprPtr cur = std::move(head);
    while (dot != std::string::npos) {
        start = dot + 1;
        dot = fq.find('.', start);
        auto m = std::make_unique<MemberExpr>();
        m->object = std::move(cur);
        m->member = fq.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        m->line = line; m->col = col;
        cur = std::move(m);
    }
    return cur;
}
// The dotted name an expression spells (`User`, `app.User`), or "".
std::string spelledName(const Expr *e) {
    if (auto *n = dynamic_cast<const NameExpr *>(e)) return n->typeArgs.empty() ? n->name : "";
    if (auto *m = dynamic_cast<const MemberExpr *>(e)) {
        std::string o = spelledName(m->object.get());
        return o.empty() ? "" : o + "." + m->member;
    }
    return "";
}
// Calls `onType` on every type written in a method, and `onName` on every class
// name written in an expression (`new X`, `X.m()`, `x is X`); `onSlot` on each
// expression slot.
void eachTypeIn(MethodDecl &m, const std::function<void(TypeRef &)> &onType,
                const std::function<void(ExprPtr &)> &onSlot) {
    for (auto &p : m.params) onType(p.type);
    onType(m.returnType);
    for (auto &st : m.body.statements) {
        visitStmt(*st, [&](Stmt &s) {
            if (auto *l = dynamic_cast<LetStmt *>(&s)) { if (l->hasDeclaredType) onType(l->declaredType); }
            else if (auto *f = dynamic_cast<ForInStmt *>(&s)) onType(f->varType);
            else if (auto *t = dynamic_cast<TryStmt *>(&s)) for (auto &c : t->catches) onType(c.type);
        }, [&](Expr &e) {
            if (auto *x = dynamic_cast<AsExpr *>(&e)) onType(x->target);
            else if (auto *x = dynamic_cast<CastExpr *>(&e)) onType(x->target);
            else if (auto *x = dynamic_cast<IsExpr *>(&e)) onType(x->target);
            else if (auto *x = dynamic_cast<SizeofExpr *>(&e)) { if (x->isType) onType(x->target); }
            else if (auto *x = dynamic_cast<NewExpr *>(&e)) { for (auto &a : x->typeArgs) onType(a); }
            else if (auto *x = dynamic_cast<CallExpr *>(&e)) { for (auto &a : x->typeArgs) onType(a); }
            else if (auto *x = dynamic_cast<NameExpr *>(&e)) { for (auto &a : x->typeArgs) onType(a); }
            else if (auto *x = dynamic_cast<StructLiteralExpr *>(&e)) { for (auto &a : x->typeArgs) onType(a); }
            else if (auto *x = dynamic_cast<LambdaExpr *>(&e)) { for (auto &p : x->params) onType(p.type); onType(x->returnType); }
        });
        rewriteStmtExprs(*st, onSlot);
    }
}
bool methodNamesSelf(MethodDecl &m) {
    bool found = false;
    eachTypeIn(m, [&](TypeRef &t) { if (typeNamesSelf(t)) found = true; }, [&](ExprPtr &e) {
        if (auto *n = dynamic_cast<NameExpr *>(e.get()); n && n->name == "Self") found = true;
        else if (auto *x = dynamic_cast<NewExpr *>(e.get()); x && x->className == "Self") found = true;
        else if (auto *x = dynamic_cast<IsExpr *>(e.get()); x && x->typeName == "Self") found = true;
    });
    return found;
}
void bindSelf(MethodDecl &m, const std::string &fq) {
    eachTypeIn(m, [&](TypeRef &t) { substSelf(t, fq); }, [&](ExprPtr &e) {
        if (auto *n = dynamic_cast<NameExpr *>(e.get()); n && n->name == "Self" && n->typeArgs.empty())
            e = classRef(fq, n->line, n->col);
        else if (auto *x = dynamic_cast<NewExpr *>(e.get()); x && x->className == "Self") x->className = fq;
        else if (auto *x = dynamic_cast<IsExpr *>(e.get()); x && x->typeName == "Self") x->typeName = fq;
    });
}
} // namespace

void Mono::instantiateSelfStatics() {
    std::unordered_map<std::string, ClassDecl *> byName;      // simple and qualified names
    for (auto &d : program_.decls)
        if (auto *c = dynamic_cast<ClassDecl *>(d.get()); c && c->typeParams.empty()) {
            byName[c->fqName] = c;
            byName.emplace(c->name, c);
        }
    // The templates: each class's static methods that name Self, before binding.
    std::unordered_map<std::string, std::vector<MethodDecl>> tpl;   // "Class.method" -> overloads
    for (auto &[nm, c] : byName) {
        if (nm != c->fqName) continue;
        for (auto &m : c->methods) {
            if (!m.isStatic) continue;
            if (!methodNamesSelf(m)) continue;
            tpl[c->fqName + "." + m.name].push_back(cloneMethod(m));
            bindSelf(m, c->fqName);                                // its own class's copy
        }
    }
    // Self names a class only where the class is known: in a static method's copy.
    for (auto &[nm, c] : byName) {
        if (nm != c->fqName) continue;
        for (auto &m : c->methods) {
            if (m.isStatic) continue;
            bool bad = false;
            for (auto &st : m.body.statements)
                rewriteStmtExprs(*st, [&](ExprPtr &e) {
                    if (auto *n = dynamic_cast<NameExpr *>(e.get()); n && n->name == "Self") bad = true;
                    else if (auto *x = dynamic_cast<NewExpr *>(e.get()); x && x->className == "Self") bad = true;
                });
            if (bad)
                semaError(m.line, m.col, "Self names a class only in a static method; in method '" + m.name +
                                         "' use self (its return type may still be Self)");
        }
    }
    if (tpl.empty()) return;
    auto declares = [&](ClassDecl *c, const std::string &m) {
        for (auto &x : c->methods) if (x.name == m) return true;
        return false;
    };
    std::unordered_map<std::string, bool> made;
    std::vector<MethodDecl *> fresh;                               // copies whose bodies still need a look
    struct Pending { ClassDecl *owner; std::vector<MethodDecl> copies; };
    std::vector<Pending> pending;
    auto onCall = [&](ExprPtr &slot) {
        auto *call = dynamic_cast<CallExpr *>(slot.get());
        if (!call) return;
        auto *mem = dynamic_cast<MemberExpr *>(call->callee.get());
        if (!mem) return;
        std::string written = spelledName(mem->object.get());
        auto cit = written.empty() ? byName.end() : byName.find(written);
        if (cit == byName.end()) return;
        ClassDecl *x = cit->second;
        // The class whose `m` this call reaches: the nearest that declares one.
        ClassDecl *owner = nullptr;
        for (ClassDecl *cur = x; cur && !owner;) {
            if (declares(cur, mem->member)) owner = cur;
            else {
                auto b = cur->baseName.empty() ? byName.end() : byName.find(cur->baseName);
                cur = b == byName.end() ? nullptr : b->second;
            }
        }
        if (!owner || owner == x) return;
        auto t = tpl.find(owner->fqName + "." + mem->member);
        if (t == tpl.end()) return;
        std::string inst = instName(mem->member, {TypeRef::named(x->fqName)});
        mem->member = inst;
        std::string key = owner->fqName + "." + inst;
        if (made[key]) return;
        made[key] = true;
        Pending p{owner, {}};
        for (const auto &m : t->second) {
            MethodDecl c = cloneMethod(m);
            c.name = inst;
            bindSelf(c, x->fqName);
            p.copies.push_back(std::move(c));
        }
        pending.push_back(std::move(p));
    };
    rewriteExprs(program_, onCall);
    // Copies call on (`Self.other()` became `User.other()`): follow them too.
    while (!pending.empty()) {
        std::vector<Pending> batch = std::move(pending);
        pending.clear();
        std::vector<std::pair<ClassDecl *, size_t>> added;          // (class, index): pointers only once all are in
        for (auto &p : batch)
            for (auto &c : p.copies) {
                p.owner->methods.push_back(std::move(c));
                added.push_back({p.owner, p.owner->methods.size() - 1});
            }
        fresh.clear();
        for (auto &[c, i] : added) fresh.push_back(&c->methods[i]);
        // (nothing is added to the methods while they are walked: new copies wait in `pending`)
        for (MethodDecl *m : fresh)
            for (auto &st : m->body.statements) rewriteStmtExprs(*st, onCall);
    }
}

} // namespace

// A deep copy of an expression (for rewrites such as `obj[k] += v`).
ExprPtr cloneExpression(const Expr *e) { return cloneExpr(e); }

// Inferred type arguments for a call or `new` in the instance being walked.
void Mono::applyInferred(Expr *e) {
    if (!infer_ || scope_.empty() || infer_->entries.empty()) return;
    if (auto *c = dynamic_cast<CallExpr *>(e)) {
        if (!c->typeArgs.empty()) return;
        std::string name;
        if (auto *n = dynamic_cast<NameExpr *>(c->callee.get())) name = n->name;
        else if (auto *m = dynamic_cast<MemberExpr *>(c->callee.get())) name = m->member;
        auto it = infer_->entries.find(siteKey(scope_, c->line, c->col, name));
        if (it != infer_->entries.end()) c->typeArgs = it->second;
    } else if (auto *n = dynamic_cast<NewExpr *>(e)) {
        if (!n->typeArgs.empty()) return;
        auto it = infer_->entries.find(siteKey(scope_, n->line, n->col, n->className));
        if (it != infer_->entries.end()) n->typeArgs = it->second;
    }
}

std::vector<GravError> monomorphize(Program &program, const InferenceTable *inferred) {
    Mono m(program, inferred);
    m.run();
    return m.errors;
}

} // namespace grav
