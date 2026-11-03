#include "sema/enums.h"

#include <set>
#include <unordered_map>

namespace grav {

namespace {

// "a.b.C" for a chain of names and member accesses; "" for anything else.
std::string dotted(const Expr *e) {
    if (auto *n = dynamic_cast<const NameExpr *>(e)) return n->name;
    if (auto *m = dynamic_cast<const MemberExpr *>(e)) {
        std::string left = dotted(m->object.get());
        return left.empty() || m->optional ? "" : left + "." + m->member;
    }
    return "";
}

std::string lastSegment(const std::string &n) {
    auto d = n.rfind('.');
    return d == std::string::npos ? n : n.substr(d + 1);
}

} // namespace

void lowerEnumConstructors(Program &program) {
    // Enum name (as written: simple or fully qualified) -> its variant names.
    std::unordered_map<std::string, std::set<std::string>> enums;
    for (const auto &d : program.decls) {
        auto *c = dynamic_cast<const ClassDecl *>(d.get());
        if (!c || c->enumVariants.empty()) continue;
        std::set<std::string> vs(c->enumVariants.begin(), c->enumVariants.end());
        enums[c->enumTemplate] = vs;
        enums[lastSegment(c->enumTemplate)] = vs;
    }
    if (enums.empty()) return;

    // `E.V` -> the variant's class name, when E is a data enum with variant V.
    auto variantClass = [&](const Expr *e) -> std::string {
        auto *m = dynamic_cast<const MemberExpr *>(e);
        if (!m || m->optional) return "";
        std::string owner = dotted(m->object.get());
        auto it = enums.find(owner);
        if (owner.empty() || it == enums.end() || !it->second.count(m->member)) return "";
        return owner + "$" + m->member;
    };
    rewriteExprs(program, [&](ExprPtr &slot) {
        std::string cls;
        std::vector<ExprPtr> args;
        std::vector<TypeRef> typeArgs;
        if (auto *call = dynamic_cast<CallExpr *>(slot.get())) {
            cls = variantClass(call->callee.get());
            if (cls.empty()) return;
            args = std::move(call->args);
            typeArgs = std::move(call->typeArgs);
        } else {
            cls = variantClass(slot.get());
            if (cls.empty()) return;
        }
        auto n = std::make_unique<NewExpr>();
        n->line = slot->line;
        n->col = slot->col;
        n->className = cls;
        n->args = std::move(args);
        n->typeArgs = std::move(typeArgs);
        slot = std::move(n);
    });
}

} // namespace grav
