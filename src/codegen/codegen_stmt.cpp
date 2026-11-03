#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

void CodeGen::line(const std::string &text) {
    // Every C line of a statement maps to its .grav line, so multi-line
    // expansions (temporaries, trace marks) do not drift.
    if (lines_ && curSrcLine_ > 0) cur_->append(lineDirective(curSrcLine_));
    cur_->append(static_cast<size_t>(indent_) * 4, ' ');
    cur_->append(text);
    cur_->push_back('\n');
}

std::string CodeGen::lineDirective(int combinedLine) const {
    if (!lines_ || combinedLine <= 0) return "";
    SourceMapEntry o = origin(combinedLine);
    if (o.file == "<unknown>") return "";
    return "#line " + std::to_string(o.line) + " \"" + escapeC(o.file) + "\"\n";
}

void CodeGen::endFunction() {
    curSrcLine_ = 0;
    defs_ += "}\n";
    if (lines_) defs_ += std::string(kLineReset) + "\n";
    defs_ += "\n";
}

void CodeGen::emitLineMark(int combinedLine) {
    if (combinedLine <= 0) return;
    line("GRAV_LINE(" + std::to_string(origin(combinedLine).line) + ");");
}

std::string CodeGen::emitDescribe(const std::string &objExpr, const TypeRef &type) const {
    if (!type.isNamed()) return "";
    // Prefer describe() (type-prefixed message), then message().
    const MethodInfo *mi = reg_->findMethod(type.name, "describe");
    if (!mi) mi = reg_->findMethod(type.name, "message");
    if (!mi || mi->isStatic || !mi->paramTypes.empty() ||
        mi->returnType.kind != TypeRef::Kind::String)
        return "";
    // Dispatch virtually so the dynamic type's override is used (e.g. a base-typed
    // `e` still reports its real subclass message).
    std::string slotOwner = type.name;
    if (const ClassInfo *ci = reg_->cls(type.name))
        for (const auto &s : ci->slots)
            if (s.name == mi->name) { slotOwner = s.slotOwner; break; }
    std::string disp = "((" + vtableType(slotOwner) + "*)((struct GravObject*)(" + objExpr +
                       "))->__vt)->" + memberCName(mi->name);
    return disp + "((" + structName(slotOwner) + "*)(" + objExpr + "))";
}

void CodeGen::emitStmt(const Stmt &stmt) {
    // Record the current source line in the active frame for stack traces (only
    // where the statement can call out; pure computation never shows in a trace).
    if (stmt.line > 0) curSrcLine_ = stmt.line;
    if (trace_ && stmtMayCall(stmt)) emitLineMark(stmt.line);
    if (auto *s = dynamic_cast<const LetStmt *>(&stmt)) {
        auto temps = hoistTemps(s->init.get());
        bool owned = emitOwnedLet(*s); // an owned string: freed when overwritten / out of scope
        if (owned) { freeTemps(temps); return; }
        // Written inside a `try` and read after a longjmp: must be volatile.
        const TypeRef &t = s->resolvedType;
        bool vol = volatileNames_.count(s->name) && !t.isArray() &&
                   !(t.isNamed() && (reg_->isStruct(t.name) || reg_->isInterface(t.name))) &&
                   !t.isBinary();
        line(cTy(t) + (vol ? " volatile " : " ") + s->name + " = " + emitAs(*s->init, t) + ";");
        freeTemps(temps);
        return;
    }
    if (auto *s = dynamic_cast<const AssignStmt *>(&stmt)) {
        auto temps = hoistTemps(s->value.get());
        if (!emitOwnedAssign(*s)) line(emitAssign(*s) + ";");
        freeTemps(temps);
        return;
    }
    if (auto *s = dynamic_cast<const ReturnStmt *>(&stmt)) {
        bool cleanups = false; // try/finally/defer to run on the way out?
        for (const auto &c : cleanups_)
            if (c.kind == Cleanup::Try || c.kind == Cleanup::Finally || c.kind == Cleanup::Defer ||
                c.kind == Cleanup::Code)
                cleanups = true;
        // Returning an owned local hands it to the caller (copied if it holds a
        // borrowed value), so the function's result is always caller-owned.
        if (auto *n = s->value ? dynamic_cast<const NameExpr *>(s->value.get()) : nullptr;
            n && n->resolvedGlobal.empty() && ownedStrings_.count(n->name)) {
            std::string rt = "__ret" + std::to_string(throwCounter_++);
            line("const char* " + rt + " = " + n->name + "__own ? " + n->name + " : grav_strdup(" +
                 n->name + ");");
            line(n->name + "__own = 0;");
            emitUnwind(false);
            if (trace_) line("grav_leave();");
            line("return " + rt + ";");
            return;
        }
        auto temps = s->value ? hoistTemps(s->value.get()) : std::vector<std::string>{};
        if (!temps.empty()) cleanups = true; // force the value into a temp before freeing
        if (s->value) {
            // Evaluate into a temp *before* unwinding/leaving the frame, so a throw
            // in the return expression still has this frame on the trace and
            // finally/defer code runs after the value is computed. Simple returns
            // stay direct (`return f(x);` keeps tail calls possible).
            if (cleanups || (trace_ && exprMayCall(s->value.get()))) {
                std::string rt = "__ret" + std::to_string(throwCounter_++);
                line(cTy(currentReturnType_) + " " + rt + " = " +
                     emitAs(*s->value, currentReturnType_) + ";");
                freeTemps(temps);
                emitUnwind(false);
                if (trace_) line("grav_leave();");
                line("return " + rt + ";");
            } else {
                if (trace_) line("grav_leave();");
                line("return " + emitAs(*s->value, currentReturnType_) + ";");
            }
        } else {
            emitUnwind(false);
            if (trace_) line("grav_leave();");
            line("return;");
        }
        return;
    }
    if (auto *s = dynamic_cast<const ExprStmt *>(&stmt)) {
        // e.g. IO.print("a" + b): bind the temporary, call, free it after.
        auto temps = hoistTemps(s->expr.get());
        if (isFreshString(*s->expr)) // a fresh result nobody uses
            line("free((void*)" + emitExpr(*s->expr) + ");");
        else
            line(emitExpr(*s->expr) + ";");
        freeTemps(temps);
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
        emitLoopBody(s->body);
        line("}");
        return;
    }
    if (auto *s = dynamic_cast<const DoWhileStmt *>(&stmt)) {
        line("do {");
        emitLoopBody(s->body);
        line("} while (" + emitExpr(*s->cond) + ");");
        return;
    }
    if (auto *s = dynamic_cast<const ForStmt *>(&stmt)) {
        std::string init = inlineSimple(s->init.get());
        std::string cond = s->cond ? emitExpr(*s->cond) : "";
        std::string upd = inlineSimple(s->update.get());
        line("for (" + init + "; " + cond + "; " + upd + ") {");
        emitLoopBody(s->body);
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
                cond += isStr ? ("grav_streq(" + tmp + ", " + v + ")")
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
        emitForIn(*s);
        return;
    }
    if (auto *s = dynamic_cast<const ThrowStmt *>(&stmt)) {
        const TypeRef &vt = s->value->type;
        const std::string &ty = vt.name;
        // Hoist the value into a temp so describe() isn't evaluated twice.
        std::string tmp = "__exc" + std::to_string(throwCounter_++);
        line(cTy(vt) + " " + tmp + " = " + emitExpr(*s->value) + ";");
        std::string msg = emitDescribe(tmp, vt);
        line("grav_throw((void*)(" + tmp + "), &" + mangle(ty) + "_typeinfo, " +
             (msg.empty() ? "0" : msg) + ");");
        return;
    }
    if (auto *s = dynamic_cast<const TryStmt *>(&stmt)) {
        emitTry(*s);
        return;
    }
    // Leaving a loop early first unwinds any try/finally/defer inside it.
    if (dynamic_cast<const BreakStmt *>(&stmt)) { emitUnwind(true); line("break;"); return; }
    if (dynamic_cast<const ContinueStmt *>(&stmt)) { emitUnwind(true); line("continue;"); return; }
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
    // `obj[k] = v` on a class with `operator[]=` calls its op_index_set(k, v).
    if (auto *ix = dynamic_cast<const IndexExpr *>(s.target.get()); ix && !ix->overloadClass.empty()) {
        std::vector<TypeRef> params;
        if (const MethodInfo *mi = reg_->findMethod(ix->overloadClass, ix->overloadMethod))
            params = mi->paramTypes;
        std::string args = params.size() == 2
                               ? emitAs(*ix->index, params[0]) + ", " + emitAs(*s.value, params[1])
                               : emitExpr(*ix->index) + ", " + emitExpr(*s.value);
        return methodCall(emitExpr(*ix->base), ix->base->type, ix->overloadMethod, args);
    }
    std::string target = emitExpr(*s.target);
    if (!s.isCompound)
        return target + " = " + emitAs(*s.value, s.target->type);
    if (s.compoundOp == BinaryOp::Add &&
        s.target->type.kind == TypeRef::Kind::String)
        return target + " = grav_str_concat(" + target + ", " + emitStringExpr(*s.value) + ")";
    return target + " " + binaryOpSymbol(s.compoundOp) + "= " + emitExpr(*s.value);
}

} // namespace grav
