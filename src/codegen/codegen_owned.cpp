// Automatic freeing of strings, by compile-time ownership analysis.
//
// Grav strings are plain `const char*` with no header or count, and library
// collections store them in raw C buffers, so a run-time collector could not see
// every reference. Instead each function body is analysed: a local `string`
// variable is *owned* when every use of it provably copies or only reads the
// characters (part of a `+`/interpolation chain, `print`, `==`, a `switch`
// subject, or an argument to a known non-retaining library call). An owned
// variable frees its previous value when reassigned and its value at scope exit,
// but only when that value was freshly allocated by the function itself (a run-
// time flag tracks that, so literals and borrowed strings are never freed).
// Anything else — returning it, storing it, passing it to other code, capturing
// it in a lambda, naming it inside inline C — leaves the variable unmanaged.
#include "codegen/codegen.h"

#include <cctype>
#include <map>

#include "codegen/mangle.h"

namespace grav {

namespace {

// lib/string.grav functions that return a newly allocated string (never one of
// their arguments), so the result is owned by the caller.
const std::set<std::string> &strFreshFns() {
    static const std::set<std::string> fns = {
        "concat", "upper", "lower", "reverse", "repeat", "substring", "remove", "trim",
        "trimStart", "trimEnd", "replace", "padLeft", "padRight", "capitalize", "slice",
        "removePrefix", "removeSuffix", "center", "fromChar"};
    return fns;
}

// Higher-order library calls that only *call* their function arguments (never
// store them), so a closure passed to them can be freed afterwards.
bool isFnNonRetainingCall(const CallExpr &c) {
    static const std::set<std::string> hof = {"forEach", "forEachIndexed", "map", "filter",
                                              "reduce", "any", "all", "countWhere",
                                              "findIndex", "sortBy"};
    auto startsWith = [](const std::string &s, const char *p) { return s.rfind(p, 0) == 0; };
    if (c.kind == CallKind::InstanceMethod)
        return (startsWith(c.ownerClass, "Array$") || startsWith(c.ownerClass, "List$")) &&
               hof.count(c.methodName);
    if (c.kind == CallKind::FreeFunction)
        return startsWith(c.targetName, "mapTo$") || startsWith(c.targetName, "fold$");
    return false;
}

// Calls known not to keep a reference to their string arguments.
bool isNonRetainingCall(const CallExpr &c) {
    static const std::set<std::string> strFns = {"length", "isEmpty", "equals", "contains",
                                                 "count", "indexOf", "indexFrom", "startsWith",
                                                 "endsWith", "charAt", "compare", "isBlank",
                                                 "equalsIgnoreCase", "compareIgnoreCase",
                                                 "lastIndexOf", "isDigits", "isAlpha",
                                                 "isAlphanumeric", "isUpper", "isLower", "split",
                                                 "lines", "words"};
    if (c.kind == CallKind::StaticMethod && c.targetName == "Str" && strFreshFns().count(c.methodName))
        return true;
    static const std::set<std::string> ioFns = {"print", "write", "eprint"};
    static const std::set<std::string> fmtFns = {"toInt", "toLong", "toUInt", "toByte",
                                                 "toFloat", "toDouble", "toBool", "toIntOr",
                                                 "isInt", "isNumber"};
    static const std::set<std::string> freeFns = {"printRaw", "eprint"};
    static const std::set<std::string> sbFns = {"append", "appendLine"};
    switch (c.kind) {
        case CallKind::StaticMethod:
            return (c.targetName == "Str" && strFns.count(c.methodName)) ||
                   (c.targetName == "IO" && ioFns.count(c.methodName)) ||
                   (c.targetName == "Format" && fmtFns.count(c.methodName));
        case CallKind::FreeFunction: return freeFns.count(c.targetName) != 0;
        case CallKind::InstanceMethod:
            return c.ownerClass == "StringBuilder" && sbFns.count(c.methodName);
        case CallKind::Builtin:
            return c.targetName == "print" || c.targetName == "panic" || c.targetName == "env";
        default: return false;
    }
}

struct OwnScan {
    bool returnsOwned = false;             // `return name` is a hand-off, not an escape
    std::map<std::string, int> decls;      // name -> number of declarations
    std::set<std::string> candidates;      // string lets
    std::set<std::string> escaped;         // used in a way that may keep the pointer
    bool giveUp = false;                   // inline C: anything could happen

    void declare(const std::string &n) { decls[n]++; }

    // The leaves of a fused string chain (same shape as CodeGen::flattenString).
    static void chainLeaves(const Expr &e, std::vector<const Expr *> &out, int &nonEmpty) {
        if (auto *b = dynamic_cast<const BinaryExpr *>(&e);
            b && b->stringConcat && b->overloadMethod.empty()) {
            chainLeaves(*b->left, out, nonEmpty);
            chainLeaves(*b->right, out, nonEmpty);
            return;
        }
        if (auto *lit = dynamic_cast<const StringLiteralExpr *>(&e)) {
            if (!lit->value.empty()) nonEmpty++;
            return;
        }
        if (auto *c = dynamic_cast<const CallExpr *>(&e);
            c && c->kind == CallKind::Builtin && c->targetName == "str" && c->args.size() == 1 &&
            c->args[0]->type.kind == TypeRef::Kind::String) {
            chainLeaves(*c->args[0], out, nonEmpty);
            return;
        }
        out.push_back(&e);
        nonEmpty++;
    }

    void chain(const Expr &e, int extraParts = 0) {
        std::vector<const Expr *> leaves;
        int parts = extraParts;
        chainLeaves(e, leaves, parts);
        for (const Expr *l : leaves) expr(l, /*safe=*/parts >= 2);
    }

    void expr(const Expr *e, bool safe) {
        if (!e || giveUp) return;
        if (auto *x = dynamic_cast<const NameExpr *>(e)) {
            if (!safe) escaped.insert(x->name);
            return;
        }
        if (auto *cb = dynamic_cast<const CBlockExpr *>(e)) {
            // Inline C can do anything with the variables it names (store them,
            // free them, reassign them), so every identifier it mentions escapes.
            const std::string &code = cb->code;
            for (size_t i = 0; i < code.size();) {
                unsigned char ch = static_cast<unsigned char>(code[i]);
                if (std::isalpha(ch) || ch == '_') {
                    size_t j = i;
                    while (j < code.size() && (std::isalnum(static_cast<unsigned char>(code[j])) ||
                                               code[j] == '_'))
                        ++j;
                    escaped.insert(code.substr(i, j - i));
                    i = j;
                } else {
                    ++i;
                }
            }
            return;
        }
        if (dynamic_cast<const IntLiteralExpr *>(e) || dynamic_cast<const FloatLiteralExpr *>(e) ||
            dynamic_cast<const BoolLiteralExpr *>(e) || dynamic_cast<const StringLiteralExpr *>(e) ||
            dynamic_cast<const NullLiteralExpr *>(e) || dynamic_cast<const SelfExpr *>(e) ||
            dynamic_cast<const SuperExpr *>(e))
            return;
        if (auto *x = dynamic_cast<const BinaryExpr *>(e)) {
            if (x->stringConcat && x->overloadMethod.empty()) { chain(*x); return; }
            bool strCmp = (x->op == BinaryOp::Eq || x->op == BinaryOp::NotEq) &&
                          x->left->type.kind == TypeRef::Kind::String && x->overloadMethod.empty();
            expr(x->left.get(), strCmp);
            expr(x->right.get(), strCmp);
            return;
        }
        if (auto *x = dynamic_cast<const CallExpr *>(e)) {
            if (x->kind == CallKind::Builtin && x->targetName == "print" && x->args.size() == 1) {
                if (auto *b = dynamic_cast<const BinaryExpr *>(x->args[0].get()); b && b->stringConcat)
                    chain(*b, 2); // printed piecewise; never aliased
                else
                    expr(x->args[0].get(), true);
                return;
            }
            bool keep = !isNonRetainingCall(*x);
            bool fnSafe = isFnNonRetainingCall(*x);
            if (x->callee) {
                if (auto *m = dynamic_cast<const MemberExpr *>(x->callee.get())) expr(m->object.get(), false);
                else if (x->kind == CallKind::FnValue)
                    expr(x->callee.get(), /*safe=*/true); // calling a closure keeps nothing
            }
            for (const auto &a : x->args)
                expr(a.get(), !keep || (fnSafe && a->type.isFunction()));
            return;
        }
        if (auto *x = dynamic_cast<const UnaryExpr *>(e)) { expr(x->operand.get(), false); return; }
        if (auto *x = dynamic_cast<const TernaryExpr *>(e)) {
            expr(x->cond.get(), false); expr(x->thenExpr.get(), false); expr(x->elseExpr.get(), false);
            return;
        }
        if (auto *x = dynamic_cast<const AsExpr *>(e)) { expr(x->operand.get(), false); return; }
        if (auto *x = dynamic_cast<const CastExpr *>(e)) { expr(x->operand.get(), false); return; }
        if (auto *x = dynamic_cast<const IsExpr *>(e)) { expr(x->operand.get(), false); return; }
        if (auto *x = dynamic_cast<const AwaitExpr *>(e)) { expr(x->operand.get(), false); return; }
        if (auto *x = dynamic_cast<const AddrOfExpr *>(e)) { expr(x->operand.get(), false); return; }
        if (auto *x = dynamic_cast<const DerefExpr *>(e)) { expr(x->operand.get(), false); return; }
        if (auto *x = dynamic_cast<const CoalesceExpr *>(e)) {
            expr(x->left.get(), false); expr(x->right.get(), false);
            return;
        }
        if (auto *x = dynamic_cast<const IncDecExpr *>(e)) { expr(x->target.get(), false); return; }
        if (auto *x = dynamic_cast<const SizeofExpr *>(e)) { expr(x->operand.get(), true); return; }
        if (auto *x = dynamic_cast<const ArrayLiteralExpr *>(e)) {
            for (const auto &el : x->elements) expr(el.get(), false);
            return;
        }
        if (auto *x = dynamic_cast<const IndexExpr *>(e)) {
            expr(x->base.get(), false); expr(x->index.get(), false);
            return;
        }
        if (auto *x = dynamic_cast<const NewExpr *>(e)) {
            for (const auto &a : x->args) expr(a.get(), false);
            return;
        }
        if (auto *x = dynamic_cast<const StructLiteralExpr *>(e)) {
            for (const auto &f : x->fields) expr(f.value.get(), false);
            return;
        }
        if (auto *x = dynamic_cast<const MemberExpr *>(e)) { expr(x->object.get(), false); return; }
        if (auto *x = dynamic_cast<const LambdaExpr *>(e)) {
            // A lambda copies what it captures: every name used inside escapes.
            for (const auto &p : x->params) declare(p.name);
            for (const auto &c : x->captures) escaped.insert(c.name);
            OwnScan inner;
            inner.expr(x->exprBody.get(), false);
            inner.block(x->body);
            for (const auto &n : inner.escaped) escaped.insert(n);
            for (const auto &[n, k] : inner.decls) decls[n] += k;
            if (inner.giveUp) giveUp = true;
            return;
        }
        giveUp = true; // an expression kind this scan does not understand
    }

    void block(const Block &b) { for (const auto &s : b.statements) stmt(*s, false); }

    void stmt(const Stmt &s, bool inForHeader) {
        if (giveUp) return;
        if (auto *x = dynamic_cast<const LetStmt *>(&s)) {
            declare(x->name);
            if ((x->resolvedType.kind == TypeRef::Kind::String || x->resolvedType.isFunction()) &&
                !inForHeader)
                candidates.insert(x->name);
            expr(x->init.get(), false);
        } else if (auto *x = dynamic_cast<const AssignStmt *>(&s)) {
            if (auto *n = dynamic_cast<const NameExpr *>(x->target.get())) {
                if (inForHeader) escaped.insert(n->name);
                if (x->isCompound && x->compoundOp == BinaryOp::Add &&
                    n->type.kind == TypeRef::Kind::String) {
                    // `s += v` builds s + v in one allocation (2+ parts).
                    if (auto *b = dynamic_cast<const BinaryExpr *>(x->value.get()); b && b->stringConcat)
                        chain(*b, 1);
                    else
                        expr(x->value.get(), true);
                } else {
                    expr(x->value.get(), false);
                }
            } else {
                expr(x->target.get(), false);
                expr(x->value.get(), false);
            }
        } else if (auto *x = dynamic_cast<const ReturnStmt *>(&s)) {
            if (!(returnsOwned && dynamic_cast<const NameExpr *>(x->value.get()) &&
                  x->value->type.kind == TypeRef::Kind::String))
                expr(x->value.get(), false);
        } else if (auto *x = dynamic_cast<const ExprStmt *>(&s)) {
            expr(x->expr.get(), false);
        } else if (auto *x = dynamic_cast<const BlockStmt *>(&s)) {
            block(x->block);
        } else if (auto *x = dynamic_cast<const IfStmt *>(&s)) {
            expr(x->cond.get(), false);
            block(x->thenBlock);
            if (x->elseStmt) stmt(*x->elseStmt, false);
        } else if (auto *x = dynamic_cast<const WhileStmt *>(&s)) {
            expr(x->cond.get(), false);
            block(x->body);
        } else if (auto *x = dynamic_cast<const DoWhileStmt *>(&s)) {
            block(x->body);
            expr(x->cond.get(), false);
        } else if (auto *x = dynamic_cast<const ForStmt *>(&s)) {
            if (x->init) stmt(*x->init, true);
            expr(x->cond.get(), false);
            if (x->update) stmt(*x->update, true);
            block(x->body);
        } else if (auto *x = dynamic_cast<const ForInStmt *>(&s)) {
            declare(x->var);
            if (!x->indexVar.empty()) declare(x->indexVar);
            expr(x->lo.get(), false);
            expr(x->hi.get(), false);
            expr(x->step.get(), false);
            block(x->body);
        } else if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) {
            expr(x->subject.get(), x->subject->type.kind == TypeRef::Kind::String);
            for (const auto &c : x->cases) {
                for (const auto &v : c.values) expr(v.get(), false);
                block(c.body);
            }
            if (x->hasDefault) block(x->defaultBody);
        } else if (auto *x = dynamic_cast<const ThrowStmt *>(&s)) {
            expr(x->value.get(), false);
        } else if (auto *x = dynamic_cast<const TryStmt *>(&s)) {
            block(x->tryBlock);
            for (const auto &c : x->catches) { declare(c.var); block(c.block); }
            if (x->hasFinally) block(x->finallyBlock);
        } else if (auto *x = dynamic_cast<const DeferStmt *>(&s)) {
            block(x->body);
        } else if (!dynamic_cast<const BreakStmt *>(&s) && !dynamic_cast<const ContinueStmt *>(&s)) {
            giveUp = true;
        }
    }
};

} // namespace

std::set<std::string> CodeGen::ownedSet(const Block &body, const std::vector<std::string> &outerNames,
                                        bool returnsOwned) const {
    std::set<std::string> out;
    OwnScan scan;
    scan.returnsOwned = returnsOwned;
    for (const auto &n : outerNames) scan.declare(n);
    scan.block(body);
    if (scan.giveUp) return out;
    for (const auto &n : scan.candidates)
        if (!scan.escaped.count(n) && scan.decls[n] == 1 && !volatileNames_.count(n))
            out.insert(n);
    return out;
}

void CodeGen::analyzeOwnedStrings(const Block &body, const std::vector<std::string> &outerNames) {
    auto it = ownedCache_.find(&body);
    ownedStrings_ = it != ownedCache_.end() ? it->second : ownedSet(body, outerNames, false);
}

namespace {
void collectReturns(const Block &b, std::vector<const Expr *> &out);
void collectReturnsStmt(const Stmt &s, std::vector<const Expr *> &out) {
    if (auto *x = dynamic_cast<const ReturnStmt *>(&s)) { out.push_back(x->value.get()); return; }
    if (auto *x = dynamic_cast<const BlockStmt *>(&s)) collectReturns(x->block, out);
    else if (auto *x = dynamic_cast<const IfStmt *>(&s)) {
        collectReturns(x->thenBlock, out);
        if (x->elseStmt) collectReturnsStmt(*x->elseStmt, out);
    } else if (auto *x = dynamic_cast<const WhileStmt *>(&s)) collectReturns(x->body, out);
    else if (auto *x = dynamic_cast<const DoWhileStmt *>(&s)) collectReturns(x->body, out);
    else if (auto *x = dynamic_cast<const ForStmt *>(&s)) collectReturns(x->body, out);
    else if (auto *x = dynamic_cast<const ForInStmt *>(&s)) collectReturns(x->body, out);
    else if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) {
        for (const auto &c : x->cases) collectReturns(c.body, out);
        if (x->hasDefault) collectReturns(x->defaultBody, out);
    } else if (auto *x = dynamic_cast<const TryStmt *>(&s)) {
        collectReturns(x->tryBlock, out);
        for (const auto &c : x->catches) collectReturns(c.block, out);
        collectReturns(x->finallyBlock, out);
    }
}
void collectReturns(const Block &b, std::vector<const Expr *> &out) {
    for (const auto &s : b.statements) collectReturnsStmt(*s, out);
}
} // namespace

void CodeGen::computeFreshFunctions() {
    struct Fn {
        std::string key;
        const Block *body;
        std::vector<std::string> params;
        std::set<std::string> loose, strict; // owned sets with/without return hand-off
    };
    std::vector<Fn> fns;
    auto add = [&](const std::string &key, const TypeRef &ret, const Block &body,
                   const std::vector<Param> &params) {
        Fn f{key, &body, {}, {}, {}};
        for (const auto &p : params) f.params.push_back(p.name);
        volatileNames_.clear();
        collectTryAssigned(body, 0);
        f.strict = ownedSet(body, f.params, false);
        if (ret.kind == TypeRef::Kind::String) f.loose = ownedSet(body, f.params, true);
        fns.push_back(std::move(f));
    };
    for (const auto &d : program_->decls) {
        if (auto *fn = dynamic_cast<const FunctionDecl *>(d.get())) {
            add("F:" + fn->fqName + "#" + std::to_string(fn->overloadIndex), fn->returnType, fn->body,
                fn->params);
        } else if (auto *c = dynamic_cast<const ClassDecl *>(d.get())) {
            for (const auto &m : c->methods)
                if (m.hasBody) add("M:" + c->fqName + "." + m.name, m.returnType, m.body, m.params);
        }
    }
    volatileNames_.clear();
    // Least fixed point: a function is fresh once every `return` is a fresh
    // expression (possibly a call to an already-fresh function) or an owned local.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto &f : fns) {
            if (freshFns_.count(f.key)) continue;
            std::vector<const Expr *> rets;
            collectReturns(*f.body, rets);
            bool ok = !rets.empty();
            for (const Expr *r : rets) {
                if (!ok) break;
                if (!r || r->type.kind != TypeRef::Kind::String) { ok = false; break; }
                auto *n = dynamic_cast<const NameExpr *>(r);
                ok = (n && n->resolvedGlobal.empty() && f.loose.count(n->name)) || isFreshString(*r);
            }
            if (ok) { freshFns_.insert(f.key); changed = true; }
        }
    }
    for (auto &f : fns) ownedCache_[f.body] = freshFns_.count(f.key) ? f.loose : f.strict;
}

bool CodeGen::isFreshString(const Expr &e) const {
    if (e.type.kind != TypeRef::Kind::String) return false;
    if (auto *c = dynamic_cast<const CallExpr *>(&e)) {
        switch (c->kind) {
            case CallKind::StaticMethod:
                if (c->targetName == "Str" && strFreshFns().count(c->methodName)) return true;
                return freshFns_.count("M:" + c->ownerClass + "." + c->methodName) != 0;
            case CallKind::FreeFunction:
                return freshFns_.count("F:" + c->targetName + "#" +
                                       std::to_string(c->resolvedOverload)) != 0;
            case CallKind::InstanceMethod: {
                if (c->ifaceDispatch) return false;
                auto *m = dynamic_cast<const MemberExpr *>(c->callee.get());
                if (m && m->optional) return false; // `a?.m()` yields "" when a is null
                std::string recv = m && m->object->type.isNamed() ? m->object->type.name : c->ownerClass;
                if (reg_->isOverridden(recv, c->methodName)) return false; // unknown impl
                return freshFns_.count("M:" + reg_->findMethodImpl(recv, c->methodName) + "." +
                                       c->methodName) != 0;
            }
            default: break;
        }
    }
    if (auto *b = dynamic_cast<const BinaryExpr *>(&e);
        b && b->stringConcat && b->overloadMethod.empty()) {
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
    if (auto *c = dynamic_cast<const CallExpr *>(&e);
        c && c->kind == CallKind::Builtin && c->targetName == "str" && c->args.size() == 1) {
        const TypeRef &t = c->args[0]->type;
        if (t.kind == TypeRef::Kind::Int || t.kind == TypeRef::Kind::Float || t.isBinary()) return true;
        if (t.kind == TypeRef::Kind::String) return isFreshString(*c->args[0]);
    }
    return false;
}

bool CodeGen::emitOwnedLet(const LetStmt &s) {
    if (!ownedStrings_.count(s.name)) return false;
    if (s.resolvedType.isFunction()) {
        // A closure only ever called here: release its environment at scope exit.
        line("GravFn " + s.name + " = " + emitAs(*s.init, s.resolvedType) + ";");
        bool fresh = dynamic_cast<const LambdaExpr *>(s.init.get()) != nullptr;
        line("bool " + s.name + "__own = " + (fresh ? "1" : "0") + ";");
        Cleanup c{Cleanup::Code, "", nullptr};
        c.code = "if (" + s.name + "__own) free(" + s.name + ".env);";
        cleanups_.push_back(c);
        return true;
    }
    line("const char* " + s.name + " = " + emitStringExpr(*s.init) + ";");
    line("bool " + s.name + "__own = " + (isFreshString(*s.init) ? "1" : "0") + ";");
    Cleanup c{Cleanup::Code, "", nullptr};
    c.code = "if (" + s.name + "__own) free((void*)" + s.name + ");";
    cleanups_.push_back(c);
    return true;
}

bool CodeGen::emitOwnedAssign(const AssignStmt &s) {
    auto *n = dynamic_cast<const NameExpr *>(s.target.get());
    if (!n || !n->resolvedGlobal.empty() || !ownedStrings_.count(n->name)) return false;
    if (n->type.isFunction()) {
        bool fresh = dynamic_cast<const LambdaExpr *>(s.value.get()) != nullptr;
        line("{");
        indent_++;
        line("GravFn grav_t = " + emitAs(*s.value, n->type) + ";");
        line("if (" + n->name + "__own) free(" + n->name + ".env);");
        line(n->name + " = grav_t;");
        line(n->name + "__own = " + (fresh ? "1" : "0") + ";");
        indent_--;
        line("}");
        return true;
    }
    std::string rhs;
    bool fresh;
    if (s.isCompound) {
        // `s += v`: one allocation for s + v; the old s is released afterwards.
        std::vector<FmtPart> parts{{false, n->name, "%s"}};
        flattenString(*s.value, parts);
        std::vector<FmtPart> merged;
        for (auto &p : parts) {
            if (p.literal && !merged.empty() && merged.back().literal) merged.back().text += p.text;
            else merged.push_back(std::move(p));
        }
        if (merged.size() < 2) return true; // `s += ""`: nothing to do
        rhs = concatParts(merged);
        fresh = true;
    } else {
        rhs = emitStringExpr(*s.value);
        fresh = isFreshString(*s.value);
    }
    line("{");
    indent_++;
    line("const char* grav_t = " + rhs + ";");
    line("if (" + n->name + "__own) free((void*)" + n->name + ");");
    line(n->name + " = grav_t;");
    line(n->name + "__own = " + (fresh ? "1" : "0") + ";");
    indent_--;
    line("}");
    return true;
}

void CodeGen::orderChain(const Expr &chain, bool allocates, std::vector<const Expr *> &out) const {
    std::vector<const Expr *> leaves;
    int parts = 0;
    OwnScan::chainLeaves(chain, leaves, parts);
    // The value each piece formats: `str(x)` of a non-string formats x itself.
    auto valueOf = [](const Expr *l) -> const Expr * {
        if (auto *c = dynamic_cast<const CallExpr *>(l);
            c && c->kind == CallKind::Builtin && c->targetName == "str" && c->args.size() == 1 &&
            c->args[0]->type.kind != TypeRef::Kind::String)
            return c->args[0].get();
        return l;
    };
    int calls = 0;
    for (const Expr *l : leaves) if (exprMayCall(valueOf(l))) calls++;
    for (const Expr *l : leaves) {
        bool isChain = dynamic_cast<const BinaryExpr *>(l) &&
                       static_cast<const BinaryExpr *>(l)->stringConcat;
        const Expr *v = valueOf(l);
        if (!isChain && v == l && isFreshString(*l) && allocates) {
            collectHoists(l, out);
            out.push_back(l); // freed after the statement
        } else if (calls >= 2 && exprMayCall(v)) {
            // C leaves argument order unspecified; Grav evaluates left to right.
            collectHoists(v, out);
            out.push_back(v);
            orderOnly_.insert(v);
        } else {
            collectHoists(l, out);
        }
    }
}

void CodeGen::collectHoists(const Expr *e, std::vector<const Expr *> &out) const {
    if (!e) return;
    auto take = [&](const Expr *x) { // hoist x (after anything hoistable inside it)
        collectHoists(x, out);
        out.push_back(x);
    };
    if (auto *b = dynamic_cast<const BinaryExpr *>(e)) {
        if (b->stringConcat && b->overloadMethod.empty()) {
            // Pieces of an allocating chain are copied into the result.
            orderChain(*e, isFreshString(*e), out);
            return;
        }
        collectHoists(b->left.get(), out);
        if (b->op != BinaryOp::And && b->op != BinaryOp::Or) collectHoists(b->right.get(), out);
        return;
    }
    if (auto *c = dynamic_cast<const CallExpr *>(e)) {
        auto *mem = dynamic_cast<const MemberExpr *>(c->callee.get());
        if (mem && mem->optional) { collectHoists(mem->object.get(), out); return; } // args are conditional
        if (mem) collectHoists(mem->object.get(), out);
        if (c->kind == CallKind::Builtin && c->targetName == "print" && c->args.size() == 1) {
            const Expr *a = c->args[0].get();
            if (auto *b = dynamic_cast<const BinaryExpr *>(a); b && b->stringConcat) {
                orderChain(*a, /*allocates=*/true, out); // printed piecewise: pieces are copied
            } else if (isFreshString(*a)) {
                take(a);
            } else {
                collectHoists(a, out);
            }
            return;
        }
        bool nonRetaining = isNonRetainingCall(*c);
        bool fnSafe = isFnNonRetainingCall(*c);
        // Left-to-right evaluation: with 2+ operands that can run code (receiver
        // included), each such operand is computed in order before the call.
        int calls = (mem && exprMayCall(mem->object.get())) ? 1 : 0;
        for (const auto &a : c->args) if (exprMayCall(a.get())) calls++;
        if (calls >= 2 && c->kind != CallKind::Builtin) {
            auto order = [&](const Expr *x) {
                if (!exprMayCall(x)) { collectHoists(x, out); return; }
                auto *lam = dynamic_cast<const LambdaExpr *>(x);
                if (nonRetaining && isFreshString(*x)) { collectHoists(x, out); out.push_back(x); return; }
                if (fnSafe && lam && !lam->captures.empty()) { out.push_back(x); return; }
                collectHoists(x, out);
                out.push_back(x);
                orderOnly_.insert(x);
            };
            if (mem) {
                // The receiver was walked above; hoist it itself if it can run code.
                if (exprMayCall(mem->object.get())) { out.push_back(mem->object.get()); orderOnly_.insert(mem->object.get()); }
            }
            for (const auto &a : c->args) order(a.get());
            return;
        }
        for (const auto &a : c->args) {
            auto *lam = dynamic_cast<const LambdaExpr *>(a.get());
            if (nonRetaining && isFreshString(*a)) take(a.get());
            else if (fnSafe && lam && !lam->captures.empty()) out.push_back(a.get()); // closure temp
            else collectHoists(a.get(), out);
        }
        return;
    }
    if (auto *x = dynamic_cast<const TernaryExpr *>(e)) { collectHoists(x->cond.get(), out); return; }
    if (auto *x = dynamic_cast<const CoalesceExpr *>(e)) { collectHoists(x->left.get(), out); return; }
    if (auto *x = dynamic_cast<const UnaryExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const AsExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const CastExpr *>(e)) { collectHoists(x->operand.get(), out); return; }
    if (auto *x = dynamic_cast<const IndexExpr *>(e)) {
        collectHoists(x->base.get(), out); collectHoists(x->index.get(), out);
        return;
    }
    if (auto *x = dynamic_cast<const MemberExpr *>(e)) {
        if (!x->optional) collectHoists(x->object.get(), out);
        return;
    }
    if (auto *x = dynamic_cast<const NewExpr *>(e)) {
        for (const auto &a : x->args) collectHoists(a.get(), out);
        return;
    }
    // Literals, names, lambdas (their bodies are separate functions), etc.
}

std::vector<std::string> CodeGen::hoistTemps(const Expr *root) {
    std::vector<const Expr *> hs;
    collectHoists(root, hs);
    std::vector<std::string> temps;
    for (const Expr *h : hs) {
        if (exprOverride_.count(h)) continue;
        std::string t = "grav_tmp" + std::to_string(throwCounter_++);
        if (orderOnly_.count(h)) {
            orderOnly_.erase(h);
            std::string val = h->type.kind == TypeRef::Kind::String ? emitStringExpr(*h) : emitExpr(*h);
            line(cTy(h->type) + " " + t + " = " + val + ";");
            exprOverride_[h] = t;
            temps.push_back("!" + t); // ordering only: not freed
            continue;
        }
        if (h->type.isFunction()) {
            line("GravFn " + t + " = " + emitExpr(*h) + ";");
            exprOverride_[h] = t;
            temps.push_back(t + ".env"); // freed as an environment pointer
        } else {
            line("const char* " + t + " = " + emitStringExpr(*h) + ";");
            exprOverride_[h] = t;
            temps.push_back(t);
        }
    }
    return temps;
}

void CodeGen::freeTemps(const std::vector<std::string> &temps) {
    for (const auto &t : temps)
        if (t[0] != '!') line("free((void*)" + t + ");");
    // Overrides refer to these temps only for this statement.
    for (auto it = exprOverride_.begin(); it != exprOverride_.end();) {
        bool mine = false;
        for (const auto &t : temps)
            if (it->second == t || it->second + ".env" == t || "!" + it->second == t) mine = true;
        it = mine ? exprOverride_.erase(it) : std::next(it);
    }
}

} // namespace grav
