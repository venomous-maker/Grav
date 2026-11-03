// `match` on a data enum. Each case names a variant and binds its payload:
//
//     match (shape) {
//         case Circle(r): print(r)
//         case Rect(w, _): print(w)
//         case Empty: print("nothing")
//     }
//
// is checked and then replaced by
//
//     { let __match0 = shape
//       if (__match0.variantTag() == 0) { let r = (__match0 as Shape$Circle).radius; print(r) }
//       else if (__match0.variantTag() == 1) { let w = (__match0 as Shape$Rect).w; print(w) }
//       else { print("nothing") } }
//
// Without a `default`, every variant must be handled; the last case then needs
// no test, so a match whose cases all return counts as returning.
#include "sema/typechecker.h"

#include <set>

namespace grav {

namespace {

// The variant a pattern names, and the names it binds ("_" skips a field).
struct Pattern {
    std::string variant;
    std::vector<std::string> binds;
    bool hasArgs = false;
    bool ok = true;
};

Pattern readPattern(const Expr *e) {
    Pattern p;
    const Expr *head = e;
    if (auto *c = dynamic_cast<const CallExpr *>(e)) {
        head = c->callee.get();
        p.hasArgs = true;
        for (const auto &a : c->args) {
            if (auto *n = dynamic_cast<const NameExpr *>(a.get())) p.binds.push_back(n->name);
            else p.ok = false;
        }
    }
    if (auto *n = dynamic_cast<const NameExpr *>(head)) p.variant = n->name;
    else if (auto *m = dynamic_cast<const MemberExpr *>(head)) p.variant = m->member; // Shape.Circle
    else p.ok = false;
    return p;
}

template <typename T> std::unique_ptr<T> at(int line, int col) {
    auto n = std::make_unique<T>();
    n->line = line;
    n->col = col;
    return n;
}

std::string readable(const std::string &enumName) { return prettyNames(enumName); }

} // namespace

const ClassDecl *TypeChecker::dataEnumOf(const TypeRef &t) const {
    if (!t.isNamed() || !reg_->isClass(t.name)) return nullptr;
    const ClassInfo *ci = reg_->cls(t.name);
    if (!ci || !ci->decl || ci->decl->enumVariants.empty()) return nullptr;
    return ci->decl;
}

void TypeChecker::lowerDataEnumMatch(SwitchStmt &s, const TypeRef &subj, const ClassDecl &base) {
    const int line = s.line, col = s.col;
    // `Option$int` instantiates template `Option`; its variants are `Option$Some$int`, ...
    std::string suffix = subj.name.size() > base.enumTemplate.size() &&
                                 subj.name.compare(0, base.enumTemplate.size(), base.enumTemplate) == 0
                             ? subj.name.substr(base.enumTemplate.size())
                             : "";
    auto variantClass = [&](const std::string &v) { return base.enumTemplate + "$" + v + suffix; };
    auto variantIndex = [&](const std::string &v) -> int {
        for (size_t i = 0; i < base.enumVariants.size(); ++i)
            if (base.enumVariants[i] == v) return static_cast<int>(i);
        return -1;
    };
    std::string enumName = readable(subj.name);

    // The subject is evaluated once, into a temporary.
    std::string tmp = "__match" + std::to_string(matchCounter_++);
    auto let = at<LetStmt>(line, col);
    let->name = tmp;
    let->isConst = true;
    let->resolvedType = subj;
    let->init = std::move(s.subject);

    struct Arm {
        std::vector<int> tags;
        Block body;
    };
    std::vector<Arm> arms;
    std::set<int> covered;
    bool bad = false;
    for (auto &c : s.cases) {
        Arm arm;
        std::vector<StmtPtr> prelude;
        for (auto &v : c.values) {
            Pattern p = readPattern(v.get());
            int tag = p.ok ? variantIndex(p.variant) : -1;
            if (!p.ok) {
                error(v->line, v->col, "a case on " + enumName +
                                           " must name a variant, e.g. `case Some(x):` or `case None:`");
                bad = true;
                continue;
            }
            if (tag < 0) {
                std::string all;
                for (size_t i = 0; i < base.enumVariants.size(); ++i)
                    all += (i ? ", " : "") + base.enumVariants[i];
                error(v->line, v->col, "'" + p.variant + "' is not a variant of " + enumName +
                                           " (variants: " + all + ")");
                bad = true;
                continue;
            }
            if (covered.count(tag))
                error(v->line, v->col, "variant '" + p.variant + "' is already handled by an earlier case");
            covered.insert(tag);
            arm.tags.push_back(tag);
            if (!p.hasArgs) continue;
            const ClassInfo *vi = reg_->cls(variantClass(p.variant));
            if (!vi || !vi->decl) { bad = true; continue; }
            const auto &fields = vi->decl->variantFields;
            if (p.binds.size() != fields.size()) {
                error(v->line, v->col, "variant '" + p.variant + "' has " + std::to_string(fields.size()) +
                                           " field(s), but the pattern binds " +
                                           std::to_string(p.binds.size()));
                bad = true;
                continue;
            }
            if (c.values.size() > 1) {
                bool binds = false;
                for (const auto &b : p.binds) binds = binds || b != "_";
                if (binds) {
                    error(v->line, v->col, "a case with several patterns cannot bind variables");
                    bad = true;
                    continue;
                }
            }
            for (size_t i = 0; i < fields.size(); ++i) {
                if (p.binds[i] == "_") continue;
                auto cast = at<AsExpr>(v->line, v->col);
                auto name = at<NameExpr>(v->line, v->col);
                name->name = tmp;
                cast->operand = std::move(name);
                cast->target = TypeRef::named(vi->fqName);
                auto field = at<MemberExpr>(v->line, v->col);
                field->object = std::move(cast);
                field->member = fields[i];
                auto bind = at<LetStmt>(v->line, v->col);
                bind->name = p.binds[i];
                bind->init = std::move(field);
                prelude.push_back(std::move(bind));
            }
        }
        for (auto &st : c.body.statements) prelude.push_back(std::move(st));
        arm.body.statements = std::move(prelude);
        arms.push_back(std::move(arm));
    }
    if (!s.hasDefault && !bad && covered.size() < base.enumVariants.size()) {
        std::string missing;
        for (size_t i = 0; i < base.enumVariants.size(); ++i)
            if (!covered.count(static_cast<int>(i)))
                missing += (missing.empty() ? "" : ", ") + base.enumVariants[i];
        error(line, col, "match on " + enumName + " does not handle " + missing +
                             "; add the missing cases or a default");
    }

    // Build the if/else chain from the last arm backwards.
    bool exhaustive = !s.hasDefault && covered.size() == base.enumVariants.size();
    StmtPtr chain;
    if (s.hasDefault) {
        auto b = at<BlockStmt>(line, col);
        b->block = std::move(s.defaultBody);
        chain = std::move(b);
    }
    for (size_t k = arms.size(); k-- > 0;) {
        Arm &arm = arms[k];
        if (arm.tags.empty()) continue;
        if (!chain && exhaustive) { // the final arm of an exhaustive match: no test needed
            auto b = at<BlockStmt>(line, col);
            b->block = std::move(arm.body);
            chain = std::move(b);
            continue;
        }
        ExprPtr cond;
        for (int tag : arm.tags) {
            auto callee = at<MemberExpr>(line, col);
            auto obj = at<NameExpr>(line, col);
            obj->name = tmp;
            callee->object = std::move(obj);
            callee->member = "variantTag";
            auto call = at<CallExpr>(line, col);
            call->callee = std::move(callee);
            auto lit = at<IntLiteralExpr>(line, col);
            lit->value = tag;
            lit->raw = std::to_string(tag);
            auto eq = at<BinaryExpr>(line, col);
            eq->op = BinaryOp::Eq;
            eq->left = std::move(call);
            eq->right = std::move(lit);
            if (!cond) { cond = std::move(eq); continue; }
            auto orE = at<BinaryExpr>(line, col);
            orE->op = BinaryOp::Or;
            orE->left = std::move(cond);
            orE->right = std::move(eq);
            cond = std::move(orE);
        }
        auto ifs = at<IfStmt>(line, col);
        ifs->cond = std::move(cond);
        ifs->thenBlock = std::move(arm.body);
        ifs->elseStmt = std::move(chain);
        chain = std::move(ifs);
    }

    pushScope();
    declareLocal(tmp, subj, false, true, line, col);
    lookupLocal(tmp)->used = true;
    if (chain) checkStmt(*chain);
    popScope();

    auto block = at<BlockStmt>(line, col);
    block->block.statements.push_back(std::move(let));
    if (chain) block->block.statements.push_back(std::move(chain));
    matchReplacement_ = std::move(block);
}

} // namespace grav
