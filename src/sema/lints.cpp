// Lints over the type-checked program (see lints.h). Each looks at one node and
// its children only, so a lint never needs to know what a call does: an
// expression with a call in it is never "the same" as another.
#include "sema/lints.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <typeinfo>

namespace grav {

namespace {

bool primitive(const TypeRef &t) {
    return t.kind == TypeRef::Kind::String || t.kind == TypeRef::Kind::Int || t.kind == TypeRef::Kind::Float ||
           t.kind == TypeRef::Kind::Bool;
}
bool sameType(const TypeRef &a, const TypeRef &b) {
    return primitive(a) && a.kind == b.kind && a.bits == b.bits && a.isUnsigned == b.isUnsigned;
}
const char *typeWord(const TypeRef &t) {
    switch (t.kind) {
    case TypeRef::Kind::String: return "a string";
    case TypeRef::Kind::Bool: return "a bool";
    case TypeRef::Kind::Float: return "a number";
    default: return "a number";
    }
}

// Whether `e` uses one of the compiler's own locals (`tuple__3`): it was
// written by a lowering (a `match`, a destructuring), not by the program.
bool hidden(const Expr *e) {
    if (!e) return false;
    bool found = false;
    visitExpr(const_cast<Expr &>(*e), [&](Expr &x) {
        if (auto *n = as<NameExpr>(&x); n && n->name.find("__") != std::string::npos) found = true;
    });
    return found;
}

bool boolLit(const Expr *e, bool &v) {
    if (auto *b = as<const BoolLiteralExpr>(e)) { v = b->value; return true; }
    return false;
}

// Whether a and b are the same expression with no effects (names, fields,
// indexes, literals and operators over them; never a call).
bool same(const Expr *a, const Expr *b) {
    if (!a || !b || typeid(*a) != typeid(*b)) return false;
    if (auto *x = as<const NameExpr>(a)) {
        auto *y = as<const NameExpr>(b);
        return x->name == y->name && x->resolvedGlobal == y->resolvedGlobal && x->fnRef.empty();
    }
    if (as<const SelfExpr>(a)) return true;
    if (auto *x = as<const IntLiteralExpr>(a)) return x->value == as<const IntLiteralExpr>(b)->value;
    if (auto *x = as<const BoolLiteralExpr>(a)) return x->value == as<const BoolLiteralExpr>(b)->value;
    if (auto *x = as<const StringLiteralExpr>(a)) return x->value == as<const StringLiteralExpr>(b)->value;
    if (auto *x = as<const FloatLiteralExpr>(a)) return x->raw == as<const FloatLiteralExpr>(b)->raw;
    if (auto *x = as<const MemberExpr>(a)) {
        auto *y = as<const MemberExpr>(b);
        return x->member == y->member && !x->bound && !y->bound && same(x->object.get(), y->object.get());
    }
    if (auto *x = as<const IndexExpr>(a)) {
        auto *y = as<const IndexExpr>(b);
        return !x->isSlice && !y->isSlice && x->more.empty() && y->more.empty() && x->overloadMethod.empty() &&
               same(x->base.get(), y->base.get()) && same(x->index.get(), y->index.get());
    }
    if (auto *x = as<const UnaryExpr>(a)) {
        auto *y = as<const UnaryExpr>(b);
        return x->op == y->op && same(x->operand.get(), y->operand.get());
    }
    if (auto *x = as<const BinaryExpr>(a)) {
        auto *y = as<const BinaryExpr>(b);
        return x->op == y->op && x->overloadMethod.empty() && same(x->left.get(), y->left.get()) &&
               same(x->right.get(), y->right.get());
    }
    return false;
}

// Source text for a simple expression (for a suggestion), or "" when it is not simple.
std::string show(const Expr *e, int depth = 0) {
    if (!e || depth > 6) return "";
    std::string s;
    if (auto *x = as<const NameExpr>(e)) s = x->name;
    else if (as<const SelfExpr>(e)) s = "self";
    else if (auto *x = as<const IntLiteralExpr>(e)) s = x->raw.empty() ? std::to_string(x->value) : x->raw;
    else if (auto *x = as<const FloatLiteralExpr>(e)) s = x->raw;
    else if (auto *x = as<const BoolLiteralExpr>(e)) s = x->value ? "true" : "false";
    else if (as<const NullLiteralExpr>(e)) s = "null";
    else if (auto *x = as<const StringLiteralExpr>(e)) {
        if (x->value.find_first_of("\"\\\n$") != std::string::npos) return "";
        s = "\"" + x->value + "\"";
    } else if (auto *x = as<const MemberExpr>(e)) {
        if (x->bound) return "";
        std::string o = show(x->object.get(), depth + 1);
        if (o.empty()) return "";
        s = o + (x->optional ? "?." : ".") + x->member;
    } else if (auto *x = as<const CallExpr>(e)) {
        std::string c = show(x->callee.get(), depth + 1);
        if (c.empty() || x->args.size() > 3 || x->spreadLast) return "";
        s = c + "(";
        for (size_t i = 0; i < x->args.size(); ++i) {
            std::string a = show(x->args[i].get(), depth + 1);
            if (a.empty()) return "";
            s += (i ? ", " : "") + a;
        }
        s += ")";
    } else if (auto *x = as<const IndexExpr>(e)) {
        if (x->isSlice || !x->more.empty()) return "";
        std::string b = show(x->base.get(), depth + 1), i = show(x->index.get(), depth + 1);
        if (b.empty() || i.empty()) return "";
        s = b + "[" + i + "]";
    } else if (auto *x = as<const UnaryExpr>(e)) {
        std::string o = show(x->operand.get(), depth + 1);
        if (o.empty()) return "";
        bool wrap = as<const BinaryExpr>(x->operand.get()) || as<const TernaryExpr>(x->operand.get());
        s = std::string(x->op == UnaryOp::Not ? "!" : "~") + (wrap ? "(" + o + ")" : o);
    } else if (auto *x = as<const BinaryExpr>(e)) {
        if (x->stringConcat) return "";
        std::string l = show(x->left.get(), depth + 1), r = show(x->right.get(), depth + 1);
        if (l.empty() || r.empty()) return "";
        if (as<const BinaryExpr>(x->left.get())) l = "(" + l + ")";
        if (as<const BinaryExpr>(x->right.get())) r = "(" + r + ")";
        s = l + " " + binaryOpSymbol(x->op) + " " + r;
    } else return "";
    return s.size() > 72 ? "" : s;
}
std::string tick(const std::string &code) { return "`" + code + "`"; }
// `!x`, `!(a && b)`.
std::string negate(const Expr *e) {
    std::string s = show(e);
    if (s.empty()) return "";
    bool wrap = as<const BinaryExpr>(e) || as<const TernaryExpr>(e);
    return "!" + (wrap ? "(" + s + ")" : s);
}
std::string opposite(BinaryOp op) {
    switch (op) {
    case BinaryOp::Eq: return "!=";
    case BinaryOp::NotEq: return "==";
    default: return "";
    }
}

// The collection classes that have isEmpty().
bool hasIsEmpty(const std::string &ownerClass) {
    static const std::set<std::string> s = {"Array", "HashMap", "HashSet", "Deque", "LinkedList", "List", "Set",
                                            "Map", "Heap", "Stack", "SortedMap", "BitSet"};
    std::string base = ownerClass.substr(0, ownerClass.find('$'));
    if (auto dot = base.rfind('.'); dot != std::string::npos) base = base.substr(dot + 1);
    return s.count(base) > 0;
}
// `xs.size()` / `xs.length()` on a collection with isEmpty(): its receiver's text.
std::string sizeCallReceiver(const Expr *e) {
    auto *c = as<const CallExpr>(e);
    if (!c || !c->args.empty() || !hasIsEmpty(c->ownerClass)) return "";
    if (c->methodName.rfind("size", 0) != 0 && c->methodName.rfind("length", 0) != 0) return "";
    auto *m = as<const MemberExpr>(c->callee.get());
    return m ? show(m->object.get()) : "";
}

struct Linter {
    std::vector<GravError> out;
    std::set<std::tuple<int, int, std::string>> seen;

    void warn(int line, int col, const std::string &name, const std::string &msg, const std::string &help = "") {
        if (line <= 0 || !seen.insert({line, col, name}).second) return;
        std::string m = msg + " [" + name + "]";
        if (!help.empty()) m += std::string(1, kLintHelp) + "help: " + help;
        out.emplace_back("warning", line, col, m);
    }
    // warn, with the fix `grav lint --fix` makes: `text` in place of the source
    // from `from`'s first byte to `to`'s last (nothing when either span is unknown).
    void warnFix(int line, int col, const std::string &name, const std::string &msg, const std::string &help,
                 const Expr *from, const Expr *to, const std::string &text) {
        size_t before = out.size();
        warn(line, col, name, msg, help);
        if (out.size() == before || text.empty() || !from || !to || from->sline == 0 || to->eline == 0) return;
        out.back().setFix({from->sline, from->scol, to->eline, to->ecol, text});
    }

    // ── expressions ─────────────────────────────────────────────────────────
    void expr(Expr &e) {
        if (auto *b = as<BinaryExpr>(&e)) binary(*b);
        else if (auto *u = as<UnaryExpr>(&e)) unary(*u);
        else if (auto *t = as<TernaryExpr>(&e)) ternary(*t);
        else if (auto *a = as<AsExpr>(&e)) {
            // (inline C gets its type from the cast: that one is needed)
            // (nor on a literal, where it says which type is meant)
            Expr *o = a->operand.get();
            if (auto *neg = as<UnaryExpr>(o)) o = neg->operand.get();
            bool literal = as<IntLiteralExpr>(o) || as<FloatLiteralExpr>(o) || as<BinaryExpr>(o) && as<IntLiteralExpr>(as<BinaryExpr>(o)->right.get()) && as<IntLiteralExpr>(as<BinaryExpr>(o)->left.get());
            // (nor the conversion call `price as float` became, `price.toFloat()`, made at the cast's place)
            auto *conv = as<CallExpr>(a->operand.get());
            if (conv && conv->line == a->line && conv->col == a->col) literal = true;
            if (a->operand && !literal && !as<CBlockExpr>(a->operand.get()) && sameType(a->operand->type, a->target) &&
                sameType(a->target, a->type))
                warnFix(a->line, a->col, "redundant-cast", "this value is already " + std::string(typeWord(a->target)) +
                     " of that type: the cast does nothing", show(a->operand.get()).empty() ? "remove `as ...`" : "write " + tick(show(a->operand.get())),
                     a, a, show(a->operand.get()));
        } else if (auto *c = as<CallExpr>(&e)) {
            auto *callee = as<NameExpr>(c->callee.get());
            // (a string's `${x}` is lowered to `str(x)` at the string's quote: only a written
            // `str(` has its name before its parenthesis)
            if (callee && callee->name == "str" && c->kind == CallKind::Builtin && c->args.size() == 1 &&
                callee->line == c->line && callee->col < c->col &&
                c->args[0]->type.kind == TypeRef::Kind::String && !c->args[0]->type.isNullable())
                warnFix(c->line, c->col, "redundant-str", "this is already a string: `str(...)` does nothing",
                     show(c->args[0].get()).empty() ? "" : "write " + tick(show(c->args[0].get())), c, c, show(c->args[0].get()));
            // Str.charAt(s, i): a call (and reference counting) per byte; s[i] is none
            if (c->kind == CallKind::StaticMethod && c->methodName.rfind("charAt", 0) == 0 && c->args.size() == 2 &&
                (c->ownerClass == "std.text.Str" || c->ownerClass == "Str")) {
                std::string s = show(c->args[0].get()), i = show(c->args[1].get());
                warnFix(c->line, c->col, "slow-char-at", "`Str.charAt` makes a call for each byte",
                     s.empty() || i.empty() ? "index the string: `s[i]`" : "write " + tick(s + "[" + i + "]"), c, c,
                     s.empty() || i.empty() ? "" : s + "[" + i + "]");
            }
        } else if (auto *l = as<LambdaExpr>(&e)) {
            if (!l->isMatch) block(l->body);
        }
    }

    void binary(BinaryExpr &b) {
        if (!b.overloadMethod.empty() || !b.left || !b.right || hidden(&b)) return;
        Expr *l = b.left.get(), *r = b.right.get();
        bool eq = b.op == BinaryOp::Eq || b.op == BinaryOp::NotEq;
        // x == true, x != false
        bool lit = false;
        if (eq && (boolLit(r, lit) || boolLit(l, lit))) {
            bool litRight = as<BoolLiteralExpr>(r) != nullptr;
            Expr *other = litRight ? l : r;
            if (!as<BoolLiteralExpr>(other) && other->type.kind == TypeRef::Kind::Bool && !other->type.isNullable()) {
                bool positive = (b.op == BinaryOp::Eq) == lit;
                std::string fix = positive ? show(other) : negate(other);
                warnFix(b.line, b.col, "bool-comparison", std::string("comparing with `") + (lit ? "true" : "false") +
                     "` is redundant", fix.empty() ? "" : "write " + tick(fix), &b, &b, fix);
                return;
            }
        }
        // xs.size() == 0
        if ((eq || b.op == BinaryOp::Greater) && as<IntLiteralExpr>(r) && as<IntLiteralExpr>(r)->value == 0) {
            std::string recv = sizeCallReceiver(l);
            if (!recv.empty()) {
                bool empty = b.op == BinaryOp::Eq;
                warnFix(b.line, b.col, "len-zero", "comparing the size with zero: say what you mean",
                     "write " + tick((empty ? "" : "!") + recv + ".isEmpty()"), &b, &b, (empty ? "" : "!") + recv + ".isEmpty()");
                return;
            }
            auto *m = as<MemberExpr>(l);
            if (m && m->member == "length" && m->object && m->object->type.kind == TypeRef::Kind::String && eq) {
                std::string o = show(m->object.get());
                if (!o.empty()) {
                    warnFix(b.line, b.col, "len-zero", "comparing the length with zero: compare with the empty string",
                         "write " + tick(o + (b.op == BinaryOp::Eq ? " == " : " != ") + "\"\""), &b, &b,
                         o + (b.op == BinaryOp::Eq ? " == " : " != ") + "\"\"");
                    return;
                }
            }
        }
        // a == a, a - a, a && a
        static const std::set<BinaryOp> pure = {BinaryOp::Eq, BinaryOp::NotEq, BinaryOp::Less, BinaryOp::Greater,
                                                BinaryOp::LessEq, BinaryOp::GreaterEq, BinaryOp::Sub, BinaryOp::Div,
                                                BinaryOp::Mod, BinaryOp::And, BinaryOp::Or, BinaryOp::BitXor,
                                                BinaryOp::BitAnd, BinaryOp::BitOr};
        // (not literals, which say what they mean; not floats, where `x != x` finds a NaN
        // and `x - x` an infinity)
        bool literal = as<IntLiteralExpr>(l) || as<FloatLiteralExpr>(l) || as<StringLiteralExpr>(l) || as<BoolLiteralExpr>(l);
        if (pure.count(b.op) && !b.stringConcat && !literal && l->type.kind != TypeRef::Kind::Float && same(l, r)) {
            std::string what = show(l);
            warn(b.line, b.col, "identical-operands", std::string("both sides of `") + binaryOpSymbol(b.op) + "` are " +
                 (what.empty() ? "the same" : tick(what)) + ": probably a mistake");
        }
    }

    void unary(UnaryExpr &u) {
        if (u.op != UnaryOp::Not || !u.operand || hidden(&u)) return;
        if (auto *inner = as<UnaryExpr>(u.operand.get()); inner && inner->op == UnaryOp::Not) {
            std::string s = show(inner->operand.get());
            warnFix(u.line, u.col, "double-negation", "two `!`s cancel out", s.empty() ? "remove them both" : "write " + tick(s), &u, &u, s);
            return;
        }
        if (auto *b = as<BinaryExpr>(u.operand.get()); b && b->overloadMethod.empty() && !opposite(b->op).empty()) {
            std::string l = show(b->left.get()), r = show(b->right.get());
            warnFix(u.line, u.col, "negated-comparison", "a negated comparison reads better as its opposite",
                 l.empty() || r.empty() ? "use `" + opposite(b->op) + "`" : "write " + tick(l + " " + opposite(b->op) + " " + r), &u, &u,
                 l.empty() || r.empty() ? "" : l + " " + opposite(b->op) + " " + r);
        }
    }

    void ternary(TernaryExpr &t) {
        if (hidden(&t)) return;
        bool a = false, b = false;
        if (!boolLit(t.thenExpr.get(), a) || !boolLit(t.elseExpr.get(), b)) {
            if (same(t.thenExpr.get(), t.elseExpr.get()))
                warn(t.line, t.col, "identical-branches", "both branches of this `?:` are the same", "use the value without the condition");
            return;
        }
        if (a == b) { warnFix(t.line, t.col, "identical-branches", "both branches of this `?:` are the same", "write " + tick(a ? "true" : "false"), &t, &t, a ? "true" : "false"); return; }
        std::string fix = a ? show(t.cond.get()) : negate(t.cond.get());
        warnFix(t.line, t.col, "needless-bool", "this `?:` gives back its own condition", fix.empty() ? "use the condition itself" : "write " + tick(fix), &t, &t, fix);
    }

    // ── statements ──────────────────────────────────────────────────────────
    void block(Block &b) {
        auto &st = b.statements;
        for (size_t i = 0; i < st.size(); ++i) {
            Stmt *s = st[i].get();
            stmt(*s);
            bool leaves = as<ReturnStmt>(s) || as<ThrowStmt>(s) || as<BreakStmt>(s) || as<ContinueStmt>(s);
            if (leaves && i + 1 < st.size() && st[i + 1]->line > s->line) {
                const char *how = as<ReturnStmt>(s) ? "return" : as<ThrowStmt>(s) ? "throw" : as<BreakStmt>(s) ? "break" : "continue";
                warn(st[i + 1]->line, st[i + 1]->col, "unreachable-code", std::string("this never runs: it comes after a `") + how + "`");
            }
        }
        // let x = e; return x
        if (st.size() >= 2) {
            auto *let = as<LetStmt>(st[st.size() - 2].get());
            auto *ret = as<ReturnStmt>(st.back().get());
            auto *name = ret ? as<NameExpr>(ret->value.get()) : nullptr;
            // (not the compiler's own locals, nor a `match` arm's binding, made on the arm's line)
            if (let && name && let->init && !let->hasDeclaredType && name->name == let->name && !name->unwrapNullable &&
                let->name.find("__") == std::string::npos && ret->line > let->line) {
                std::string e = show(let->init.get());
                warn(let->line, let->col, "let-and-return", tick(let->name) + " is returned right after it is made",
                     e.empty() ? "return the expression directly" : "write " + tick("return " + e));
            }
        }
    }

    void stmt(Stmt &s) {
        if (auto *a = as<AssignStmt>(&s)) assign(*a);
        else if (auto *i = as<IfStmt>(&s)) ifStmt(*i);
        else if (auto *f = as<ForInStmt>(&s)) { rangeLoop(*f); block(f->body); }
        else if (auto *w = as<WhileStmt>(&s)) block(w->body);
        else if (auto *d = as<DoWhileStmt>(&s)) block(d->body);
        else if (auto *f = as<ForStmt>(&s)) block(f->body);
        else if (auto *b = as<BlockStmt>(&s)) block(b->block);
        else if (auto *d = as<DeferStmt>(&s)) block(d->body);
        else if (auto *t = as<TryStmt>(&s)) {
            block(t->tryBlock);
            for (auto &c : t->catches) {
                block(c.block);
                // (the driver lets one with a comment beside it be: it says why)
                if (c.block.statements.empty())
                    warn(c.line, c.col, "empty-catch", "an empty `catch` hides the error",
                         "handle it or log it, or say in a comment why it is safe to ignore");
            }
            if (t->hasFinally) block(t->finallyBlock);
        } else if (auto *sw = as<SwitchStmt>(&s)) {
            for (auto &c : sw->cases) block(c.body);
            if (sw->hasDefault) block(sw->defaultBody);
        }
    }

    void assign(AssignStmt &a) {
        if (a.isCompound || a.readBack || !a.target || !a.value || hidden(a.target.get()) || hidden(a.value.get())) return;
        if (same(a.target.get(), a.value.get())) {
            warn(a.line, a.col, "self-assignment", "this assigns " + tick(show(a.target.get()).empty() ? "it" : show(a.target.get())) + " to itself");
            return;
        }
        auto *b = as<BinaryExpr>(a.value.get());
        static const std::set<BinaryOp> ops = {BinaryOp::Add, BinaryOp::Sub, BinaryOp::Mul, BinaryOp::Div, BinaryOp::Mod};
        if (b && ops.count(b->op) && b->overloadMethod.empty() && !b->stringConcat && a.target->type.isNumeric() &&
            same(a.target.get(), b->left.get())) {
            std::string t = show(a.target.get()), r = show(b->right.get());
            warnFix(a.line, a.col, "assign-op", "this can use a compound assignment",
                 t.empty() || r.empty() ? std::string("use `") + binaryOpSymbol(b->op) + "=`"
                                        : "write " + tick(t + " " + binaryOpSymbol(b->op) + "= " + r),
                 a.target.get(), a.value.get(), t.empty() || r.empty() ? "" : t + " " + binaryOpSymbol(b->op) + "= " + r);
        }
    }

    void ifStmt(IfStmt &i) {
        block(i.thenBlock);
        if (i.elseStmt) stmt(*i.elseStmt);
        if (hidden(i.cond.get())) return;
        if (i.thenBlock.statements.empty() && !i.elseStmt) {
            warn(i.line, i.col, "empty-if", "this `if` does nothing", "remove it");
            return;
        }
        auto *elseBlock = i.elseStmt ? as<BlockStmt>(i.elseStmt.get()) : nullptr;
        if (elseBlock && elseBlock->block.statements.size() == 1 && as<IfStmt>(elseBlock->block.statements[0].get()))
            warn(elseBlock->block.statements[0]->line, elseBlock->block.statements[0]->col, "collapsible-else-if",
                 "an `if` alone in an `else` block", "write `else if (...) { ... }`");
        // if (c) { return true } else { return false }
        if (elseBlock && i.thenBlock.statements.size() == 1 && elseBlock->block.statements.size() == 1) {
            auto *r1 = as<ReturnStmt>(i.thenBlock.statements[0].get());
            auto *r2 = as<ReturnStmt>(elseBlock->block.statements[0].get());
            bool a = false, b = false;
            if (r1 && r2 && boolLit(r1->value.get(), a) && boolLit(r2->value.get(), b) && a != b) {
                std::string fix = a ? show(i.cond.get()) : negate(i.cond.get());
                warn(i.line, i.col, "needless-bool", "this `if` returns its own condition",
                     fix.empty() ? "return the condition itself" : "write " + tick("return " + fix));
            }
        }
    }

    // for (i in 0..xs.size()) { ... xs[i] ... }, i used for nothing else.
    void rangeLoop(ForInStmt &f) {
        if (f.iterKind != ForInKind::Range || f.inclusive || f.step || !f.indexVar.empty()) return;
        auto *lo = as<IntLiteralExpr>(f.lo.get());
        auto *call = as<CallExpr>(f.hi.get());
        if (!lo || lo->value != 0 || !call || !call->args.empty() || !hasIsEmpty(call->ownerClass)) return;
        if (call->methodName.rfind("size", 0) != 0 && call->methodName.rfind("length", 0) != 0) return;
        auto *member = as<MemberExpr>(call->callee.get());
        auto *coll = member ? as<NameExpr>(member->object.get()) : nullptr;
        if (!coll) return;
        int uses = 0, indexing = 0;
        bool written = false;
        std::function<void(Expr &)> count = [&](Expr &e) {
            if (auto *n = as<NameExpr>(&e); n && n->name == f.var) ++uses;
            if (auto *ix = as<IndexExpr>(&e)) {
                auto *base = as<NameExpr>(ix->base.get());
                auto *idx = as<NameExpr>(ix->index.get());
                if (base && idx && base->name == coll->name && idx->name == f.var && !ix->isSlice) ++indexing;
            }
            if (auto *inc = as<IncDecExpr>(&e); inc && as<IndexExpr>(inc->target.get())) written = true;
        };
        std::function<void(Stmt &)> onStmt = [&](Stmt &s) {
            if (auto *a = as<AssignStmt>(&s)) {
                if (auto *ix = as<IndexExpr>(a->target.get())) {
                    auto *base = as<NameExpr>(ix->base.get());
                    if (base && base->name == coll->name) written = true;
                }
                if (auto *n = as<NameExpr>(a->target.get()); n && (n->name == coll->name || n->name == f.var)) written = true;
            }
        };
        for (auto &s : f.body.statements) visitStmt(*s, onStmt, count);
        if (uses > 0 && uses == indexing && !written)
            warn(f.line, f.col, "needless-range-loop", tick(f.var) + " is only used to index " + tick(coll->name),
                 "write " + tick("for (x in " + coll->name + ")") + " and use `x`");
    }
};

// Whether `name` appears in C code as a whole word (inline C reaching a field or
// a parameter: `self->total`, `count`).
bool wordIn(const std::string &code, const std::string &name) {
    if (name.empty()) return false;
    auto ident = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    for (size_t at = code.find(name); at != std::string::npos; at = code.find(name, at + 1)) {
        bool startOk = at == 0 || !ident(code[at - 1]);
        bool endOk = at + name.size() >= code.size() || !ident(code[at + name.size()]);
        if (startOk && endOk) return true;
    }
    return false;
}
std::string baseName(const std::string &n) { return n.substr(0, n.find('$')); }

// The names a body uses: names, members and methods it mentions, and its inline C.
struct Uses {
    std::set<std::string> names;
    std::string c;
    void expr(Expr &e) {
        if (auto *n = as<NameExpr>(&e)) names.insert(n->name);
        else if (auto *m = as<MemberExpr>(&e)) names.insert(baseName(m->member));
        else if (auto *c2 = as<CallExpr>(&e)) { if (!c2->methodName.empty()) names.insert(baseName(c2->methodName)); }
        else if (auto *cb = as<CBlockExpr>(&e)) c += cb->code + "\n";
    }
    // (what is only assigned is not used: `self.x = 0`, as a field's initial
    // value becomes, does not count; reading it does)
    std::set<const Expr *> written;
    void block(Block &b) {
        for (auto &st : b.statements)
            visitStmt(*st, [this](Stmt &x) {
                if (auto *a = as<AssignStmt>(&x); a && !a->isCompound && as<MemberExpr>(a->target.get())) written.insert(a->target.get());
            }, [](Expr &) {});
        for (auto &st : b.statements)
            visitStmt(*st, [](Stmt &) {}, [this](Expr &e) { if (!written.count(&e)) expr(e); });
    }
    bool has(const std::string &name) const { return names.count(name) || wordIn(c, name); }
};

// unused-private (a private field or method nothing in its class uses) and
// unused-parameter (a parameter its function never uses).
void unusedMembers(Program &program, Linter &l) {
    // every class's and interface's method names, and bases, to tell overrides
    std::map<std::string, std::set<std::string>> methodsOf;
    std::map<std::string, std::vector<std::string>> basesOf;
    std::set<std::string> fnValues, boundMethods;
    for (auto &d : program.decls) {
        if (auto *c = as<ClassDecl>(d.get())) {
            for (auto &m : c->methods) methodsOf[c->name].insert(baseName(m.sourceName.empty() ? m.name : m.sourceName));
            auto &b = basesOf[c->name];
            if (!c->baseName.empty()) b.push_back(c->baseName);
            for (auto &x : c->extraBases) b.push_back(x);
            for (auto &x : c->interfaceNames) b.push_back(x);
        } else if (auto *i = as<InterfaceDecl>(d.get())) {
            for (auto &m : i->methods) methodsOf[i->name].insert(baseName(m.sourceName.empty() ? m.name : m.sourceName));
        }
    }
    visitExprs(program, [&](Expr &e) {
        if (auto *n = as<NameExpr>(&e); n && !n->fnRef.empty()) fnValues.insert(n->name);
        if (auto *m = as<MemberExpr>(&e); m && m->bound) boundMethods.insert(baseName(m->member));
    });
    auto shortName = [](std::string n) { auto dot = n.rfind('.'); return dot == std::string::npos ? n : n.substr(dot + 1); };
    std::function<bool(const std::string &, const std::string &, int)> inherited = [&](const std::string &cls, const std::string &m, int depth) {
        if (depth > 32) return false;
        for (auto &b : basesOf[cls]) {
            std::string sb = shortName(b.substr(0, b.find('<')));
            if (methodsOf[sb].count(m) || inherited(sb, m, depth + 1)) return true;
        }
        return false;
    };
    auto params = [&](std::vector<Param> &ps, Block &body, int line, int col, bool skip) {
        if (skip || body.statements.empty()) return;
        Uses u;
        u.block(body);
        for (auto &p : ps)
            if (!p.name.empty() && p.name[0] != '_' && p.name != "self" && p.name.find("__") == std::string::npos && !u.has(p.name))
                l.warn(line, col, "unused-parameter", "parameter " + tick(p.name) + " is never used",
                       "remove it, or name it " + tick("_" + p.name) + " if the signature must keep it");
    };
    for (auto &d : program.decls) {
        if (auto *f = as<FunctionDecl>(d.get())) {
            bool skip = !f->typeParams.empty() || f->name == "main" || fnValues.count(f->name) || f->name.find('$') != std::string::npos;
            params(f->params, f->body, f->line, f->col, skip);
            continue;
        }
        auto *c = as<ClassDecl>(d.get());
        if (!c || c->name.find('$') != std::string::npos || !c->typeParams.empty() || !c->enumTemplate.empty()) continue;
        // what the class's own code uses
        Uses u;
        u.block(c->constructor.body);
        for (auto &x : c->extraCtors) u.block(x.body);
        u.block(c->destructor.body);
        for (auto &m : c->methods) u.block(m.body);
        for (auto &f : c->fields) if (f.init) visitExpr(*f.init, [&](Expr &e) { u.expr(e); });
        bool reflected = !c->decorators.empty();
        for (auto &f : c->fields) {
            if (f.access != Access::Private || reflected || !f.decorators.empty() || f.name.rfind("__", 0) == 0) continue;
            if (!u.has(f.name))
                l.warn(f.line, f.col, "unused-private", "private field " + tick(f.name) + " is never read", "remove it");
        }
        for (auto &m : c->methods) {
            std::string name = baseName(m.sourceName.empty() ? m.name : m.sourceName);
            bool overrides = inherited(c->name, name, 0);
            if (m.access == Access::Private && !reflected && m.decorators.empty() && name.rfind("__", 0) != 0 &&
                name != "toString" && name.rfind("op_", 0) != 0 && !overrides && !boundMethods.count(name) && !u.has(name))
                l.warn(m.line, m.col, "unused-private", "private method " + tick(name) + " is never called", "remove it");
            // (the methods `for`, `x[i]` and the like call by name keep their signatures)
            static const std::set<std::string> protocol = {"get", "set", "size", "length", "iterator", "next", "hasNext",
                                                           "compareTo", "equals", "hashCode", "hash", "toString", "close"};
            bool hook = protocol.count(name) || name.rfind("op_", 0) == 0;
            params(m.params, m.body, m.line, m.col, overrides || hook || m.isAbstract || !m.hasBody || boundMethods.count(name) || !m.typeParams.empty());
        }
        params(c->constructor.params, c->constructor.body, c->constructor.line, c->constructor.col, false);
        for (auto &x : c->extraCtors) params(x.params, x.body, x.line, x.col, false);
    }
}

} // namespace

std::vector<GravError> lintProgram(Program &program) {
    Linter l;
    unusedMembers(program, l);
    for (auto &d : program.decls) {
        if (auto *fn = as<FunctionDecl>(d.get())) l.block(fn->body);
        else if (auto *c = as<ClassDecl>(d.get())) {
            l.block(c->constructor.body);
            for (auto &x : c->extraCtors) l.block(x.body);
            l.block(c->destructor.body);
            for (auto &m : c->methods) l.block(m.body);
        } else if (auto *t = as<TestDecl>(d.get())) l.block(t->body);
    }
    visitExprs(program, [&](Expr &e) { l.expr(e); });
    std::stable_sort(l.out.begin(), l.out.end(), [](const GravError &a, const GravError &b) {
        return a.line() != b.line() ? a.line() < b.line() : a.col() < b.col();
    });
    return l.out;
}

} // namespace grav
