#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

void CodeGen::line(const std::string &text) {
    cur_->append(static_cast<size_t>(indent_) * 4, ' ');
    cur_->append(text);
    cur_->push_back('\n');
}

void CodeGen::emitBlock(const Block &block) {
    for (const auto &s : block.statements) emitStmt(*s);
}

void CodeGen::emitStmt(const Stmt &stmt) {
    if (auto *s = dynamic_cast<const LetStmt *>(&stmt)) {
        line(cTy(s->resolvedType) + " " + s->name + " = " +
             emitAs(*s->init, s->resolvedType) + ";");
        return;
    }
    if (auto *s = dynamic_cast<const AssignStmt *>(&stmt)) {
        line(emitAssign(*s) + ";");
        return;
    }
    if (auto *s = dynamic_cast<const ReturnStmt *>(&stmt)) {
        // Returning out of an enclosing `try` must restore the jmp_buf stack top.
        if (!tryTops_.empty()) line("grav_jmp_top = " + tryTops_.front() + ";");
        if (s->value) line("return " + emitAs(*s->value, currentReturnType_) + ";");
        else line("return;");
        return;
    }
    if (auto *s = dynamic_cast<const ExprStmt *>(&stmt)) {
        line(emitExpr(*s->expr) + ";");
        return;
    }
    if (auto *s = dynamic_cast<const BlockStmt *>(&stmt)) {
        emitBraced(s->block);
        return;
    }
    if (auto *s = dynamic_cast<const IfStmt *>(&stmt)) {
        emitIf(*s);
        return;
    }
    if (auto *s = dynamic_cast<const WhileStmt *>(&stmt)) {
        line("while (" + emitExpr(*s->cond) + ") {");
        indent_++; emitBlock(s->body); indent_--;
        line("}");
        return;
    }
    if (auto *s = dynamic_cast<const DoWhileStmt *>(&stmt)) {
        line("do {");
        indent_++; emitBlock(s->body); indent_--;
        line("} while (" + emitExpr(*s->cond) + ");");
        return;
    }
    if (auto *s = dynamic_cast<const ForStmt *>(&stmt)) {
        std::string init = inlineSimple(s->init.get());
        std::string cond = s->cond ? emitExpr(*s->cond) : "";
        std::string upd = inlineSimple(s->update.get());
        line("for (" + init + "; " + cond + "; " + upd + ") {");
        indent_++; emitBlock(s->body); indent_--;
        line("}");
        return;
    }
    if (auto *s = dynamic_cast<const SwitchStmt *>(&stmt)) {
        std::string tmp = "__sw" + std::to_string(switchCounter_++);
        bool isStr = s->subject->type.kind == TypeRef::Kind::String;
        line("{");
        indent_++;
        line(cTy(s->subject->type) + " " + tmp + " = " + emitExpr(*s->subject) + ";");
        bool first = true;
        for (const auto &c : s->cases) {
            std::string cond;
            for (size_t i = 0; i < c.values.size(); ++i) {
                if (i) cond += " || ";
                std::string v = emitExpr(*c.values[i]);
                cond += isStr ? ("strcmp(" + tmp + ", " + v + ") == 0")
                              : ("(" + tmp + " == " + v + ")");
            }
            line((first ? "if (" : "} else if (") + cond + ") {");
            first = false;
            indent_++; emitBlock(c.body); indent_--;
        }
        if (s->hasDefault) {
            line(first ? "{" : "} else {");
            indent_++; emitBlock(s->defaultBody); indent_--;
            line("}");
        } else if (!first) {
            line("}");
        }
        indent_--;
        line("}");
        return;
    }
    if (auto *s = dynamic_cast<const ForInStmt *>(&stmt)) {
        // `for (i in lo..hi)` -> a counting C for loop. The upper bound is
        // hoisted into a temp so it is evaluated once.
        std::string lim = "__hi" + std::to_string(switchCounter_++);
        std::string cmp = s->inclusive ? " <= " : " < ";
        line("for (int " + s->var + " = " + emitExpr(*s->lo) + ", " + lim + " = " +
             emitExpr(*s->hi) + "; " + s->var + cmp + lim + "; " + s->var + "++) {");
        indent_++; emitBlock(s->body); indent_--;
        line("}");
        return;
    }
    if (auto *s = dynamic_cast<const ThrowStmt *>(&stmt)) {
        const std::string &ty = s->value->type.name;
        line("grav_throw((void*)(" + emitExpr(*s->value) + "), &" + mangle(ty) + "_typeinfo);");
        return;
    }
    if (auto *s = dynamic_cast<const TryStmt *>(&stmt)) {
        emitTry(*s);
        return;
    }
    if (dynamic_cast<const BreakStmt *>(&stmt)) { line("break;"); return; }
    if (dynamic_cast<const ContinueStmt *>(&stmt)) { line("continue;"); return; }
}

void CodeGen::emitIf(const IfStmt &s) {
    line("if (" + emitExpr(*s.cond) + ") {");
    indent_++; emitBlock(s.thenBlock); indent_--;
    // Walk the else-chain so that `else if` flattens to a single C clause and a
    // block `else` is emitted directly (no redundant inner braces).
    const Stmt *e = s.elseStmt.get();
    while (e) {
        if (auto *elif = dynamic_cast<const IfStmt *>(e)) {
            line("} else if (" + emitExpr(*elif->cond) + ") {");
            indent_++; emitBlock(elif->thenBlock); indent_--;
            e = elif->elseStmt.get();
        } else if (auto *blk = dynamic_cast<const BlockStmt *>(e)) {
            line("} else {");
            indent_++; emitBlock(blk->block); indent_--;
            e = nullptr;
        } else {
            line("} else {");
            indent_++; emitStmt(*e); indent_--;
            e = nullptr;
        }
    }
    line("}");
}

void CodeGen::emitTry(const TryStmt &s) {
    std::string t = "__try" + std::to_string(tryCounter_++);
    std::string exc = "grav_current_exc";
    line("{");
    indent_++;
    line("int " + t + " = grav_jmp_top++;");
    line("if (setjmp(grav_jmp_stack[" + t + "]) == 0) {");
    indent_++;
    tryTops_.push_back(t);
    emitBlock(s.tryBlock);
    tryTops_.pop_back();
    line("grav_jmp_top = " + t + ";"); // normal completion: pop
    indent_--;
    line("} else {");
    indent_++;
    line("grav_jmp_top = " + t + ";"); // exception in flight: pop, then handle
    if (s.hasCatch) {
        std::string ct = structName(s.catchType.name);
        line("if (grav_is_instance(" + exc + ".value, &" + mangle(s.catchType.name) + "_typeinfo)) {");
        indent_++;
        line(ct + "* " + s.catchVar + " = (" + ct + "*)" + exc + ".value;");
        emitBlock(s.catchBlock);
        indent_--;
        line("} else {");
        indent_++;
        if (s.hasFinally) emitBlock(s.finallyBlock);
        line("grav_throw(" + exc + ".value, " + exc + ".type);"); // rethrow
        indent_--;
        line("}");
    } else {
        if (s.hasFinally) emitBlock(s.finallyBlock);
        line("grav_throw(" + exc + ".value, " + exc + ".type);"); // propagate
    }
    indent_--;
    line("}");
    if (s.hasFinally) emitBlock(s.finallyBlock); // normal + handled paths
    indent_--;
    line("}");
}

void CodeGen::emitBraced(const Block &block) {
    line("{");
    indent_++;
    emitBlock(block);
    indent_--;
    line("}");
}

std::string CodeGen::inlineSimple(const Stmt *s) const {
    if (!s) return "";
    if (auto *l = dynamic_cast<const LetStmt *>(s))
        return cTy(l->resolvedType) + " " + l->name + " = " + emitAs(*l->init, l->resolvedType);
    if (auto *a = dynamic_cast<const AssignStmt *>(s))
        return emitAssign(*a);
    if (auto *e = dynamic_cast<const ExprStmt *>(s))
        return emitExpr(*e->expr);
    return "";
}

// Renders an assignment (plain or compound) without a trailing ';'. `string +=`
// has no C equivalent, so it lowers to an explicit concat re-assignment.
std::string CodeGen::emitAssign(const AssignStmt &s) const {
    std::string target = emitExpr(*s.target);
    if (!s.isCompound)
        return target + " = " + emitAs(*s.value, s.target->type);
    if (s.compoundOp == BinaryOp::Add &&
        s.target->type.kind == TypeRef::Kind::String)
        return target + " = grav_str_concat(" + target + ", " + emitExpr(*s.value) + ")";
    return target + " " + binaryOpSymbol(s.compoundOp) + "= " + emitExpr(*s.value);
}

} // namespace grav
