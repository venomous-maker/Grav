// Automatic reference counting.
//
// Reference types: class instances (and data enums), interface values, closures
// (their environments are counted objects), strings built at run time (counted
// blocks from the runtime's string allocator; literals and C strings are permanent
// and ignored by retain/release) and `any` values holding one of those.
//
// The rules the generated code follows:
//   - Owning places hold one reference each: local variables, object fields,
//     globals and static fields, closure environments, container slots (the
//     library retains/releases those explicitly with __retain/__release).
//   - A call or `new` hands its result to the caller (+1). Used inside an
//     expression it is put on the per-thread release list and released when the
//     statement ends, so a result is valid for exactly the statement that made it.
//   - Parameters and `self` are borrowed: the caller keeps the argument alive.
//   - Storing into an owning place retains the new value (or takes over a fresh
//     result) before releasing the old one.
//   - Places the compiler cannot follow (struct copies, fixed arrays, exceptions,
//     async task arguments) take a reference they never give back: a leak, never
//     a dangling pointer.
#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

CodeGen::RefKind CodeGen::refKind(const TypeRef &t) const {
    if (t.isNamed()) {
        if (reg_->isInterface(t.name)) return RefKind::Iface;
        if (reg_->isClass(t.name)) return RefKind::Obj;
        if (reg_->isStruct(t.name) && valueHasRefs(t)) return RefKind::Value;
        return RefKind::None;
    }
    if (t.isArray()) return valueHasRefs(t) ? RefKind::Value : RefKind::None;
    if (t.kind == TypeRef::Kind::String) return RefKind::Str;
    if (t.isNullable() && t.elem->kind == TypeRef::Kind::String) return RefKind::Str; // may be NULL
    if (t.isFunction()) return RefKind::Fn;
    if (t.isAny()) return RefKind::Any;
    return RefKind::None;
}

bool CodeGen::valueHasRefs(const TypeRef &t) const {
    std::string key = valueTypeName(t);
    if (key.empty()) return false;
    auto it = valueRefsMemo_.find(key);
    if (it != valueRefsMemo_.end()) return it->second == 1;
    valueRefsMemo_[key] = 0; // value containment is acyclic; this only guards the walk
    bool has = false;
    if (t.isArray()) {
        has = t.elem && refKind(*t.elem) != RefKind::None;
    } else if (const StructInfo *si = reg_->strct(t.name)) {
        for (const auto &f : si->fields)
            if (refKind(f.type) != RefKind::None) { has = true; break; }
    }
    valueRefsMemo_[key] = has ? 1 : 0;
    return has;
}

std::string CodeGen::valueTypeName(const TypeRef &t) const {
    if (t.isArray()) return arrayStructName(t);
    if (t.isNamed() && reg_->isStruct(t.name)) return structName(t.name);
    return "";
}

// The ARC helpers of a value struct or fixed array `key` that holds references.
std::string CodeGen::emitValueArc(const std::string &key, const TypeRef &t) const {
    std::string ret, rel;
    auto member = [&](const TypeRef &ft, const std::string &m) {
        if (refKind(ft) == RefKind::None) return;
        ret += "    " + m + " = " + retainOf(ft, m) + ";\n";
        rel += "    " + releaseOf(ft, m) + "\n";
    };
    std::string pre;
    if (t.isArray()) {
        pre = "    for (int i = 0; i < " + std::to_string(t.arrayLen) + "; i++) {\n";
        member(*t.elem, "v.data[i]");
        ret = pre + ret + "    }\n";
        rel = pre + rel + "    }\n";
    } else if (const StructInfo *si = reg_->strct(t.name)) {
        for (const auto &f : si->fields) member(f.type, "v." + memberCName(f.name));
    }
    std::string out;
    out += "static GRAV_UNUSED " + key + " " + key + "__retain(" + key + " v) {\n" + ret + "    return v;\n}\n";
    out += "static GRAV_UNUSED void " + key + "__release(" + key + " v) {\n" + rel + "}\n";
    out += "static GRAV_UNUSED void " + key + "__release_box(void* p) { " + key + "__release(*(" + key + "*)p); }\n";
    std::string vis;
    if (t.isArray()) {
        std::string v = visitOf(*t.elem, "v.data[i]");
        if (!v.empty()) vis = "    for (int i = 0; i < " + std::to_string(t.arrayLen) + "; i++) { " + v + " }\n";
    } else if (const StructInfo *si = reg_->strct(t.name)) {
        for (const auto &f : si->fields) {
            std::string v = visitOf(f.type, "v." + memberCName(f.name));
            if (!v.empty()) vis += "    " + v + "\n";
        }
    }
    out += "static GRAV_UNUSED void " + key + "__visit(" + key + " v) {\n    (void)v;\n" + vis + "}\n";
    out += "static GRAV_UNUSED " + key + " " + key + "__autorelease(" + key + " v) { grav_autorelease_box(" + key +
           "__release_box, &v, sizeof v); return v; }\n\n";
    return out;
}

bool CodeGen::gcRelevant(const TypeRef &t) const {
    switch (refKind(t)) {
        case RefKind::Obj: case RefKind::Iface: case RefKind::Fn: case RefKind::Any: return true;
        case RefKind::Value:
            if (t.isArray()) return t.elem && gcRelevant(*t.elem);
            if (const StructInfo *si = reg_->strct(t.name))
                for (const auto &f : si->fields)
                    if (gcRelevant(f.type)) return true;
            return false;
        default: return false;
    }
}

std::string CodeGen::visitOf(const TypeRef &t, const std::string &c) const {
    if (!gcRelevant(t)) return "";
    switch (refKind(t)) {
        case RefKind::Obj: return "grav_gc_visit((void*)(" + c + "));";
        case RefKind::Iface: return "grav_gc_visit((" + c + ").obj);";
        case RefKind::Fn: return "grav_gc_visit((" + c + ").env);";
        case RefKind::Any: return "grav_gc_visit_any(" + c + ");";
        case RefKind::Value: return valueTypeName(t) + "__visit(" + c + ");";
        default: return "";
    }
}

namespace {
// Whether a `__visitRefs` body reports anything the collector cares about (an
// Array<int> instance's loop of __visit calls on ints does not).
using RelevantFn = std::function<bool(const TypeRef &)>;
bool visitsRelevant(const RelevantFn &g, const Block &b);
bool visitsRelevantStmt(const RelevantFn &g, const Stmt *s) {
    if (!s) return false;
    if (auto *x = dynamic_cast<const ExprStmt *>(s)) {
        auto *c = dynamic_cast<const CallExpr *>(x->expr.get());
        return c && c->kind == CallKind::Builtin && c->targetName == "__visit" && c->args.size() == 1 &&
               g(c->args[0]->type);
    }
    if (auto *x = dynamic_cast<const BlockStmt *>(s)) return visitsRelevant(g, x->block);
    if (auto *x = dynamic_cast<const IfStmt *>(s)) return visitsRelevant(g, x->thenBlock) || visitsRelevantStmt(g, x->elseStmt.get());
    if (auto *x = dynamic_cast<const WhileStmt *>(s)) return visitsRelevant(g, x->body);
    if (auto *x = dynamic_cast<const ForStmt *>(s)) return visitsRelevant(g, x->body);
    if (auto *x = dynamic_cast<const ForInStmt *>(s)) return visitsRelevant(g, x->body);
    return false;
}
bool visitsRelevant(const RelevantFn &g, const Block &b) {
    for (const auto &s : b.statements)
        if (visitsRelevantStmt(g, s.get())) return true;
    return false;
}
} // namespace

std::string CodeGen::emitVisitFields(const std::string &classFq) {
    std::string body;
    for (const auto &f : collectFields(classFq)) {
        if (f.isWeak) continue; // weak references are not part of a cycle
        std::string v = visitOf(f.type, "self->" + memberCName(f.name));
        if (!v.empty()) body += "    " + v + "\n";
    }
    // Containers report the references in their buffers with a __visitRefs method.
    std::string impl = reg_->findMethodImpl(classFq, "__visitRefs");
    if (!impl.empty())
        if (const ClassInfo *ci = reg_->cls(impl); ci && ci->decl)
            for (const auto &m : ci->decl->methods)
                if (m.name == "__visitRefs" && m.hasBody && visitsRelevant([this](const TypeRef &t) { return gcRelevant(t); }, m.body))
                    body += "    " + methodCName(impl, "__visitRefs") + "((" + structName(impl) + "*)self);\n";
    if (body.empty()) return "0";
    std::string S = structName(classFq);
    std::string fn = mangle(classFq) + "__gc_visit";
    vtables_ += "static void " + fn + "(void* o) {\n    " + S + "* self = (" + S + "*)o;\n" + body + "}\n";
    return fn;
}

std::string CodeGen::retainOf(const TypeRef &t, const std::string &c) const {
    switch (refKind(t)) {
        case RefKind::Obj: return "((" + cTy(t) + ")grav_retain((void*)(" + c + ")))";
        case RefKind::Iface: return "grav_iface_retain(" + c + ")";
        case RefKind::Fn: return "grav_fn_retain(" + c + ")";
        case RefKind::Any: return "grav_any_retain(" + c + ")";
        case RefKind::Str: return "grav_str_retain(" + c + ")";
        case RefKind::Value: return valueTypeName(t) + "__retain(" + c + ")";
        default: return c;
    }
}

std::string CodeGen::releaseOf(const TypeRef &t, const std::string &c) const {
    switch (refKind(t)) {
        case RefKind::Obj: return "grav_release((void*)(" + c + "));";
        case RefKind::Iface: return "grav_iface_release(" + c + ");";
        case RefKind::Fn: return "grav_fn_release(" + c + ");";
        case RefKind::Any: return "grav_any_release(" + c + ");";
        case RefKind::Str: return "grav_str_release(" + c + ");";
        case RefKind::Value: return valueTypeName(t) + "__release(" + c + ");";
        default: return "";
    }
}

std::string CodeGen::unwindPush(const TypeRef &t, const std::string &var) const {
    std::string rel;
    switch (refKind(t)) {
        case RefKind::Obj: rel = "grav_uw_obj"; break;
        case RefKind::Iface: rel = "grav_uw_iface"; break;
        case RefKind::Fn: rel = "grav_uw_fn"; break;
        case RefKind::Any: rel = "grav_uw_any"; break;
        case RefKind::Str: rel = "grav_uw_str"; break;
        case RefKind::Value: rel = valueTypeName(t) + "__release_box"; break;
        default: return "";
    }
    return "grav_uw_push((void*)&" + var + ", " + rel + ");";
}

std::string CodeGen::ownedRelease(const TypeRef &t, const std::string &var, bool registered) const {
    std::string rel = releaseOf(t, var);
    return rel.empty() || !registered ? rel : "grav_uw_top--; " + rel;
}

// Whether a throw or fault can happen after `s` while its scope lasts: the statements
// after it in its block (a statement nested elsewhere, like a `for` header's
// `let`, is assumed to).
bool CodeGen::restOfBlockMayThrow(const Stmt &s) const {
    if (!curBlock_ || curStmt_ >= curBlock_->statements.size() ||
        curBlock_->statements[curStmt_].get() != &s)
        return true;
    return blockMayThrow(*curBlock_, curStmt_ + 1);
}

std::string CodeGen::autoreleaseOf(const TypeRef &t, const std::string &c) const {
    switch (refKind(t)) {
        case RefKind::Obj: return "((" + cTy(t) + ")grav_autorelease((void*)(" + c + ")))";
        case RefKind::Iface: return "grav_iface_autorelease(" + c + ")";
        case RefKind::Fn: return "grav_fn_autorelease(" + c + ")";
        case RefKind::Any: return "grav_any_autorelease(" + c + ")";
        case RefKind::Str: return "grav_str_autorelease(" + c + ")";
        case RefKind::Value: return valueTypeName(t) + "__autorelease(" + c + ")";
        default: return c;
    }
}

namespace {
// Whether every return in a body is `return self` (counting them in `returns`).
bool onlyReturnsSelf(const Block &b, int &returns);
bool onlyReturnsSelfStmt(const Stmt *s, int &returns) {
    if (auto *r = dynamic_cast<const ReturnStmt *>(s)) {
        if (!r->value || !dynamic_cast<const SelfExpr *>(r->value.get())) return false;
        returns++;
        return true;
    }
    if (auto *x = dynamic_cast<const BlockStmt *>(s)) return onlyReturnsSelf(x->block, returns);
    if (auto *x = dynamic_cast<const IfStmt *>(s))
        return onlyReturnsSelf(x->thenBlock, returns) && (!x->elseStmt || onlyReturnsSelfStmt(x->elseStmt.get(), returns));
    if (auto *x = dynamic_cast<const WhileStmt *>(s)) return onlyReturnsSelf(x->body, returns);
    if (auto *x = dynamic_cast<const DoWhileStmt *>(s)) return onlyReturnsSelf(x->body, returns);
    if (auto *x = dynamic_cast<const ForStmt *>(s)) return onlyReturnsSelf(x->body, returns);
    if (auto *x = dynamic_cast<const ForInStmt *>(s)) return onlyReturnsSelf(x->body, returns);
    if (auto *x = dynamic_cast<const SwitchStmt *>(s)) {
        for (const auto &c : x->cases) if (!onlyReturnsSelf(c.body, returns)) return false;
        return onlyReturnsSelf(x->defaultBody, returns);
    }
    // Keep the convention simple around unwinding.
    if (dynamic_cast<const TryStmt *>(s) || dynamic_cast<const DeferStmt *>(s)) return false;
    return true;
}
bool onlyReturnsSelf(const Block &b, int &returns) {
    for (const auto &st : b.statements)
        if (!onlyReturnsSelfStmt(st.get(), returns)) return false;
    return true;
}
} // namespace

// A method that only ever returns `self`, is the one implementation of its name
// in its class hierarchy and is no interface's method, returns its receiver
// borrowed: no retain in the method and nothing for the caller to release, so
// fluent chains (`sb.append(a).append(b)`, `json.set(..).set(..)`) do no counting.
// The receiver is alive for the whole expression anyway, held by whoever made it.
bool CodeGen::returnsSelfBorrowed(const std::string &classFq, const std::string &method) const {
    const MethodInfo *mi = reg_->findMethod(classFq, method);
    if (!mi || mi->isStatic || !mi->decl || !mi->hasBody) return false;
    std::string key = mi->definingClass + ":" + method;
    if (auto it = selfReturnCache_.find(key); it != selfReturnCache_.end()) return it->second;
    bool ok = refKind(mi->returnType) == RefKind::Obj && !reg_->isOverridden(mi->definingClass, method) &&
              !reg_->interfaceDeclares(mi->sourceName.empty() ? method : mi->sourceName);
    if (ok) {
        if (const ClassInfo *ci = reg_->cls(mi->definingClass)) {
            std::vector<std::string> bases = ci->extraBases;
            if (!ci->baseClass.empty()) bases.push_back(ci->baseClass);
            for (const auto &b : bases)
                if (reg_->findMethod(b, method)) ok = false;   // it overrides a base method
        }
    }
    if (ok) {
        int returns = 0;
        ok = onlyReturnsSelf(mi->decl->body, returns) && returns > 0;
    }
    selfReturnCache_[key] = ok;
    return ok;
}

bool CodeGen::isRefProducer(const Expr &e) const {
    if (refKind(e.type) == RefKind::None) return false;
    if (dynamic_cast<const NewExpr *>(&e)) return true;
    // `a + b` through a class's operator method is a call too.
    if (auto *b = dynamic_cast<const BinaryExpr *>(&e); b && !b->overloadMethod.empty()) return true;
    // `obj[k]` through operator[] is a method call: its result is the caller's.
    if (auto *ix = dynamic_cast<const IndexExpr *>(&e); ix && !ix->overloadClass.empty()) return true;
    if (auto *ix = dynamic_cast<const IndexExpr *>(&e); ix && ix->isSlice && e.type.kind == TypeRef::Kind::String)
        return true; // a fresh string
    // A struct or fixed-array literal holds its own references (+1 each).
    if (refKind(e.type) == RefKind::Value &&
        (dynamic_cast<const StructLiteralExpr *>(&e) || dynamic_cast<const ArrayLiteralExpr *>(&e)))
        return true;
    if (auto *l = dynamic_cast<const LambdaExpr *>(&e)) return !l->captures.empty();
    if (auto *m = dynamic_cast<const MemberExpr *>(&e); m && m->bound) return isRefProducer(*m->bound);
    if (auto *a = dynamic_cast<const ArrayLiteralExpr *>(&e); a && a->built) return isRefProducer(*a->built);
    if (auto *c = dynamic_cast<const CallExpr *>(&e)) {
        if (c->kind == CallKind::Builtin) {
            if (c->targetName == "__retain") return true;
            if (c->targetName == "__unwrap") return false; // the operand, borrowed
            // Builtins returning strings hand back fresh or permanent strings,
            // except str(), which emitToString already makes borrowed.
            return refKind(e.type) == RefKind::Str && c->targetName != "str";
        }
        // A method returning its receiver borrowed hands back nothing new.
        if (c->kind == CallKind::InstanceMethod && !c->ifaceDispatch && returnsSelfBorrowed(c->ownerClass, c->methodName))
            return false;
        return true; // functions, methods, closures: the result is the caller's
    }
    if (refKind(e.type) == RefKind::Str) {
        // Inline C yields a fresh string or a permanent one (literal, static, C library).
        if (dynamic_cast<const CBlockExpr *>(&e)) return true;
        if (auto *b = dynamic_cast<const BinaryExpr *>(&e); b && b->stringConcat && b->overloadMethod.empty())
            return concatAllocates(e);
    }
    return false;
}

// Whether a `+` chain builds a new string (two or more pieces, or a formatted
// one); a single piece is just that piece.
bool CodeGen::concatAllocates(const Expr &e) const {
    std::vector<FmtPart> raw;
    flattenString(e, raw);
    int parts = 0;
    bool lastLit = false, formatted = false;
    for (const auto &p : raw) {
        if (p.literal) { if (!lastLit) parts++; lastLit = true; continue; }
        lastLit = false;
        parts++;
        if (p.conv != "%s") formatted = true;
    }
    return parts >= 2 || formatted;
}

bool CodeGen::exprMayAutorelease(const Expr *e) const {
    if (!e) return false;
    if (isRefProducer(*e)) return true;
    bool found = false;
    struct Walk {
        const CodeGen *g;
        bool &found;
        bool unstable = false; // a field/element/global reference read: held when ordered before a call
        void expr(const Expr *x) {
            if (!x || found) return;
            if (g->isRefProducer(*x)) { found = true; return; }
            if (g->isUnstableRef(*x)) unstable = true;
            if (auto *b = dynamic_cast<const BinaryExpr *>(x)) { expr(b->left.get()); expr(b->right.get()); }
            else if (auto *u = dynamic_cast<const UnaryExpr *>(x)) expr(u->operand.get());
            else if (auto *t = dynamic_cast<const TernaryExpr *>(x)) { expr(t->cond.get()); expr(t->thenExpr.get()); expr(t->elseExpr.get()); }
            else if (auto *a = dynamic_cast<const AsExpr *>(x)) expr(a->operand.get());
            else if (auto *i = dynamic_cast<const IsExpr *>(x)) expr(i->operand.get());
            else if (auto *c = dynamic_cast<const CastExpr *>(x)) expr(c->operand.get());
            else if (auto *co = dynamic_cast<const CoalesceExpr *>(x)) { expr(co->left.get()); expr(co->right.get()); }
            else if (auto *ix = dynamic_cast<const IndexExpr *>(x)) {
                expr(ix->base.get()); expr(ix->index.get());
                for (const auto &m : ix->more) expr(m.get());
            }
            else if (auto *m = dynamic_cast<const MemberExpr *>(x)) {
                if (g->isWeakField(*m)) { found = true; return; } // a weak read is held on the list
                expr(m->object.get());
            }
            else if (auto *ar = dynamic_cast<const ArrayLiteralExpr *>(x)) { for (const auto &el : ar->elements) expr(el.get()); }
            else if (auto *sl = dynamic_cast<const StructLiteralExpr *>(x)) { for (const auto &f : sl->fields) expr(f.value.get()); }
            else if (auto *n = dynamic_cast<const NewExpr *>(x)) {
                for (const auto &a : n->args) { if (g->isUnstableRef(*a)) { found = true; return; } expr(a.get()); }
            } else if (auto *cl = dynamic_cast<const CallExpr *>(x)) {
                // emitStable holds field/element/global arguments and receivers on the list
                if (auto *m = dynamic_cast<const MemberExpr *>(cl->callee.get()); m && g->isUnstableRef(*m->object)) { found = true; return; }
                for (const auto &a : cl->args) if (g->isUnstableRef(*a)) { found = true; return; }
                expr(cl->callee.get());
                for (const auto &a : cl->args) expr(a.get());
            } else if (auto *aw = dynamic_cast<const AwaitExpr *>(x)) expr(aw->operand.get());
            else if (auto *ad = dynamic_cast<const AddrOfExpr *>(x)) expr(ad->operand.get());
            else if (auto *d = dynamic_cast<const DerefExpr *>(x)) expr(d->operand.get());
            else if (auto *id = dynamic_cast<const IncDecExpr *>(x)) expr(id->target.get());
            // lambda bodies are separate functions; literals and names produce nothing
        }
    } w{this, found};
    w.expr(e);
    return found || (w.unstable && exprMayCall(e));
}

bool CodeGen::exitSkipsDrain(const Block &b, bool under, bool loopExits) const {
    for (const auto &s : b.statements)
        if (exitSkipsDrainStmt(*s, under, loopExits)) return true;
    return false;
}

bool CodeGen::exitSkipsDrainStmt(const Stmt &s, bool under, bool loopExits) const {
    if (auto *x = dynamic_cast<const ReturnStmt *>(&s)) return under || exprMayAutorelease(x->value.get());
    if (dynamic_cast<const BreakStmt *>(&s) || dynamic_cast<const ContinueStmt *>(&s)) return loopExits && under;
    bool here = under || stmtNeedsDrain(s);              // this statement drains only when it falls through
    if (auto *x = dynamic_cast<const BlockStmt *>(&s)) return exitSkipsDrain(x->block, here, loopExits);
    if (auto *x = dynamic_cast<const IfStmt *>(&s))
        return exitSkipsDrain(x->thenBlock, here, loopExits) ||
               (x->elseStmt && exitSkipsDrainStmt(*x->elseStmt, here, loopExits));
    if (auto *x = dynamic_cast<const WhileStmt *>(&s)) return exitSkipsDrain(x->body, here, loopExits);
    if (auto *x = dynamic_cast<const DoWhileStmt *>(&s)) return exitSkipsDrain(x->body, here, loopExits);
    if (auto *x = dynamic_cast<const ForStmt *>(&s)) return exitSkipsDrain(x->body, here, loopExits);
    if (auto *x = dynamic_cast<const ForInStmt *>(&s)) return exitSkipsDrain(x->body, here, loopExits);
    if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) {
        for (const auto &c : x->cases) if (exitSkipsDrain(c.body, here, loopExits)) return true;
        return exitSkipsDrain(x->defaultBody, here, loopExits);
    }
    if (auto *x = dynamic_cast<const TryStmt *>(&s)) {
        if (exitSkipsDrain(x->tryBlock, here, loopExits) || exitSkipsDrain(x->finallyBlock, here, loopExits)) return true;
        for (const auto &c : x->catches) if (exitSkipsDrain(c.block, here, loopExits)) return true;
    }
    return false;
}

// Whether a statement's own expressions (not nested blocks, which drain their
// own statements) can put anything on the release list.
bool CodeGen::stmtNeedsDrain(const Stmt &s) const {
    if (auto *x = dynamic_cast<const LetStmt *>(&s)) return exprMayAutorelease(x->init.get());
    if (auto *x = dynamic_cast<const AssignStmt *>(&s))
        return exprMayAutorelease(x->target.get()) || exprMayAutorelease(x->value.get());
    if (auto *x = dynamic_cast<const ExprStmt *>(&s)) return exprMayAutorelease(x->expr.get());
    if (auto *x = dynamic_cast<const IfStmt *>(&s)) {
        if (exprMayAutorelease(x->cond.get())) return true;
        // `else if` conditions belong to this statement too
        for (const Stmt *e = x->elseStmt.get(); e;) {
            auto *ei = dynamic_cast<const IfStmt *>(e);
            if (!ei) break;
            if (exprMayAutorelease(ei->cond.get())) return true;
            e = ei->elseStmt.get();
        }
        return false;
    }
    if (auto *x = dynamic_cast<const WhileStmt *>(&s)) return exprMayAutorelease(x->cond.get());
    if (auto *x = dynamic_cast<const DoWhileStmt *>(&s)) return exprMayAutorelease(x->cond.get());
    if (auto *x = dynamic_cast<const ForStmt *>(&s))
        return (x->init && stmtNeedsDrain(*x->init)) || exprMayAutorelease(x->cond.get()) ||
               (x->update && stmtNeedsDrain(*x->update));
    if (auto *x = dynamic_cast<const ForInStmt *>(&s))
        return exprMayAutorelease(x->lo.get()) || exprMayAutorelease(x->hi.get()) ||
               exprMayAutorelease(x->step.get());
    if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) return exprMayAutorelease(x->subject.get());
    if (auto *x = dynamic_cast<const ThrowStmt *>(&s)) return exprMayAutorelease(x->value.get());
    return false;
}

std::string CodeGen::emitOwned(const Expr &e, const TypeRef &target) const {
    if (refKind(target) == RefKind::None || dynamic_cast<const NullLiteralExpr *>(&e) ||
        dynamic_cast<const StringLiteralExpr *>(&e)) // literals are permanent
        return emitAs(e, target);
    if (auto *as = dynamic_cast<const AsExpr *>(&e);
        as && refKind(target) == RefKind::Str && dynamic_cast<const CBlockExpr *>(as->operand.get()) &&
        as->operand->type.kind == TypeRef::Kind::String)
        return emitOwned(*as->operand, target); // `%{ … %} as string`: take the C result over
    bool hoisted = !exprOverride_.empty() && exprOverride_.count(&e);
    if (!hoisted && isRefProducer(e)) {
        // The result is already +1: hand it over without the release list.
        const Expr *saved = ownedRoot_;
        ownedRoot_ = &e;
        std::string out = emitAs(e, target);
        ownedRoot_ = saved;
        return out;
    }
    return retainOf(target, emitAs(e, target));
}

std::string CodeGen::storeOwned(const std::string &slot, const TypeRef &slotType, const Expr &value) const {
    // Retain (or take over) the new value first, then release the old one: storing
    // a place's own value back must not free it in between.
    std::string ct = cTy(slotType);
    return "{ " + ct + "* __slot = &(" + slot + "); " + ct + " __new = " + emitOwned(value, slotType) +
           "; " + ct + " __old = *__slot; *__slot = __new; " + releaseOf(slotType, "__old") + " }";
}

void CodeGen::emitReleaseFields(const std::string &classFq) {
    std::string S = structName(classFq);
    std::string body;
    for (const auto &f : collectFields(classFq)) {
        if (refKind(f.type) == RefKind::None) continue;
        std::string m = "self->" + memberCName(f.name);
        if (f.isWeak) {
            body += "    { void* __v = (void*)" + m + "; " + m + " = 0; grav_weak_release(__v); }\n";
            continue;
        }
        // Clear the field before releasing: a cycle back to this object sees null.
        std::string zero = refKind(f.type) == RefKind::Str ? "\"\"" : refKind(f.type) == RefKind::Obj ? "0" : "(" + cTy(f.type) + "){0}";
        body += "    { " + cTy(f.type) + " __v = " + m + "; " + m + " = " + zero + "; " + releaseOf(f.type, "__v") + " }\n";
    }
    vtables_ += "static void " + mangle(classFq) + "__arc_fields(void* o) {\n";
    if (body.empty()) vtables_ += "    (void)o;\n";
    else vtables_ += "    " + S + "* self = (" + S + "*)o;\n" + body;
    vtables_ += "}\n";
}

void CodeGen::ownAssignedParams(const Block &body, const std::vector<Param> &params) {
    // A parameter the body assigns to becomes an owning local for the body.
    std::set<std::string> assigned;
    std::function<void(const Block &)> scanBlock;
    std::function<void(const Stmt *)> scan = [&](const Stmt *s) {
        if (!s) return;
        if (auto *a = dynamic_cast<const AssignStmt *>(s)) {
            if (auto *n = dynamic_cast<const NameExpr *>(a->target.get()); n && n->resolvedGlobal.empty())
                assigned.insert(n->name);
        } else if (auto *b = dynamic_cast<const BlockStmt *>(s)) scanBlock(b->block);
        else if (auto *i = dynamic_cast<const IfStmt *>(s)) { scanBlock(i->thenBlock); scan(i->elseStmt.get()); }
        else if (auto *w = dynamic_cast<const WhileStmt *>(s)) scanBlock(w->body);
        else if (auto *d = dynamic_cast<const DoWhileStmt *>(s)) scanBlock(d->body);
        else if (auto *f = dynamic_cast<const ForStmt *>(s)) { scan(f->init.get()); scan(f->update.get()); scanBlock(f->body); }
        else if (auto *fi = dynamic_cast<const ForInStmt *>(s)) scanBlock(fi->body);
        else if (auto *sw = dynamic_cast<const SwitchStmt *>(s)) {
            for (const auto &c : sw->cases) scanBlock(c.body);
            scanBlock(sw->defaultBody);
        } else if (auto *t = dynamic_cast<const TryStmt *>(s)) {
            scanBlock(t->tryBlock);
            for (const auto &c : t->catches) scanBlock(c.block);
            scanBlock(t->finallyBlock);
        } else if (auto *df = dynamic_cast<const DeferStmt *>(s)) scanBlock(df->body);
    };
    scanBlock = [&](const Block &b) { for (const auto &st : b.statements) scan(st.get()); };
    scanBlock(body);
    for (const auto &p : params) {
        if (!assigned.count(p.name) || refKind(p.type) == RefKind::None || p.variadic) continue;
        const std::string &n = p.name;
        line(n + " = " + retainOf(p.type, n) + ";");
        bool reg = blockMayThrow(body);
        if (reg) line(unwindPush(p.type, n));
        cleanups_.push_back({Cleanup::Code, "", nullptr, ownedRelease(p.type, n, reg)});
    }
}

// A borrowed reference handed to a call must stay alive for the call. Locals,
// parameters and `self` have an owner the call cannot change, and fresh results
// are on the release list; but a value read from a field, an element or a global
// could be released by the callee (`self.unlink(self.tail)` clearing `self.tail`),
// so it is held by an extra reference until the statement ends.
// A `weak` field of a class instance.
bool CodeGen::isWeakField(const MemberExpr &m) const {
    if (m.kind != MemberKind::InstanceField) return false;
    if (!m.object || !m.object->type.isNamed() || !reg_->isClass(m.object->type.name)) return false;
    const FieldInfo *f = reg_->findField(m.object->type.name, m.member);
    return f && f->isWeak;
}

bool CodeGen::isUnstableRef(const Expr &e) const {
    if (refKind(e.type) == RefKind::None) return false;
    if (auto *ix = dynamic_cast<const IndexExpr *>(&e)) return ix->overloadClass.empty() && !ix->isSlice; // operator[] results and slices are fresh
    if (auto *m = dynamic_cast<const MemberExpr *>(&e); m && m->bound) return false;
    if (dynamic_cast<const MemberExpr *>(&e) || dynamic_cast<const DerefExpr *>(&e)) return true;
    if (auto *n = dynamic_cast<const NameExpr *>(&e)) return !n->resolvedGlobal.empty();
    if (auto *c = dynamic_cast<const CallExpr *>(&e); c && c->kind == CallKind::Builtin && c->targetName == "__unwrap")
        return isUnstableRef(*c->args[0]);
    return false;
}

std::string CodeGen::emitStable(const Expr &e, const TypeRef &t) const {
    std::string v = emitAs(e, t);
    if (refKind(t) == RefKind::None || !isUnstableRef(e)) return v;
    if (!exprOverride_.empty() && exprOverride_.count(&e)) return v;
    return autoreleaseOf(t, retainOf(t, v));
}

// At the natural end of a body: the function-level cleanups (owned parameters)
// that a `return` would have run.
void CodeGen::emitBodyEndCleanups() {
    for (size_t i = cleanups_.size(); i-- > 0;)
        if (cleanups_[i].kind == Cleanup::Code) line(cleanups_[i].code);
    if (!fnPoolMark_.empty()) line("grav_pool_drain(" + fnPoolMark_ + ");");
}

} // namespace grav
