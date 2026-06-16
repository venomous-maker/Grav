#include "sema/monomorph.h"

#include <string>
#include <unordered_map>
#include <vector>

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
    if (dynamic_cast<const BreakStmt *>(s)) return mkS<BreakStmt>(s);
    if (dynamic_cast<const ContinueStmt *>(s)) return mkS<ContinueStmt>(s);
    return nullptr;
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
    std::unordered_map<std::string, bool> done_;
    std::vector<DeclPtr> instances_;

    void error(int line, int col, const std::string &m) { errors.emplace_back("generic", line, col, m); }

    void rewriteType(TypeRef &t, const Subst &subst, int line, int col);
    std::string instantiateStruct(const std::string &name, const std::vector<TypeRef> &args,
                                  int line, int col);
    std::string instantiateFn(const std::string &name, const std::vector<TypeRef> &args,
                              int line, int col);
    void walkBlock(Block &b, const Subst &subst);
    void walkStmt(Stmt *s, const Subst &subst);
    void walkExpr(Expr *e, const Subst &subst);
    void walkDeclTypes(Decl *d, const Subst &subst);
};

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
        if (structTpl_.count(t.name)) {
            std::string inst = instantiateStruct(t.name, t.args, line, col);
            t.name = inst; t.args.clear();
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
        if (!x->typeArgs.empty())
            error(e->line, e->col, "generic classes are not yet supported "
                                   "(use a generic struct or free functions)");
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
        for (auto &f : cd->fields) rewriteType(f.type, subst, f.line, f.col);
        for (auto &p : cd->constructor.params) rewriteType(p.type, subst, cd->line, cd->col);
        if (cd->constructor.present) walkBlock(cd->constructor.body, subst);
        for (auto &m : cd->methods) {
            for (auto &p : m.params) rewriteType(p.type, subst, m.line, m.col);
            rewriteType(m.returnType, subst, m.line, m.col);
            if (m.hasBody) walkBlock(m.body, subst);
        }
    }
}

void Mono::run() {
    // 1) Split templates from concrete decls (templates kept alive in templates_).
    std::vector<DeclPtr> kept;
    for (auto &d : program_.decls) {
        if (auto *st = dynamic_cast<StructDecl *>(d.get()); st && !st->typeParams.empty()) {
            structTpl_[st->name] = st; templates_.push_back(std::move(d));
        } else if (auto *fn = dynamic_cast<FunctionDecl *>(d.get()); fn && !fn->typeParams.empty()) {
            fnTpl_[fn->name] = fn; templates_.push_back(std::move(d));
        } else if (auto *cd = dynamic_cast<ClassDecl *>(d.get()); cd && !cd->typeParams.empty()) {
            error(cd->line, cd->col, "generic classes are not yet supported "
                                     "(use a generic struct or free functions)");
            templates_.push_back(std::move(d)); // drop it from output
        } else {
            kept.push_back(std::move(d));
        }
    }

    // 2) Rewrite + collect instantiations over every concrete decl.
    for (auto &d : kept) walkDeclTypes(d.get(), {});

    // 3) Emit: concrete decls first, then the generated instances.
    program_.decls = std::move(kept);
    for (auto &inst : instances_) program_.decls.push_back(std::move(inst));
}

} // namespace

std::vector<GravError> monomorphize(Program &program) {
    Mono m(program);
    m.run();
    return m.errors;
}

} // namespace grav
