// Structured control flow: blocks with `defer`, loops, `for … in`, try/catch/
// finally, and the unwinding that `return`/`break`/`continue` perform through
// them. Also the setjmp-safety (volatile) and line-mark analyses.
#include "codegen/codegen.h"

#include "codegen/mangle.h"

namespace grav {

namespace {

bool isTerminator(const Stmt &s) {
    return dynamic_cast<const ReturnStmt *>(&s) || dynamic_cast<const BreakStmt *>(&s) ||
           dynamic_cast<const ContinueStmt *>(&s) || dynamic_cast<const ThrowStmt *>(&s);
}

// Constant value of an integer literal, including the parser's `0 - n` form of
// a negative literal.
bool constInt(const Expr *e, long long &out) {
    if (auto *lit = dynamic_cast<const IntLiteralExpr *>(e)) { out = lit->value; return true; }
    if (auto *b = dynamic_cast<const BinaryExpr *>(e); b && b->op == BinaryOp::Sub) {
        long long l, r;
        if (constInt(b->left.get(), l) && constInt(b->right.get(), r)) { out = l - r; return true; }
    }
    return false;
}

// An expression C can evaluate repeatedly without side effects or cost.
bool isSimpleLvalue(const Expr &e) {
    if (dynamic_cast<const NameExpr *>(&e) || dynamic_cast<const SelfExpr *>(&e)) return true;
    if (auto *m = dynamic_cast<const MemberExpr *>(&e))
        return !m->optional && m->object && isSimpleLvalue(*m->object);
    return false;
}

} // namespace

void CodeGen::emitBlock(const Block &block) {
    size_t base = cleanups_.size();
    cleanups_.push_back({Cleanup::Scope, "", nullptr});
    bool fellOff = true;
    const Block *outerBlock = curBlock_;
    size_t outerStmt = curStmt_;
    curBlock_ = &block;
    curStmt_ = 0;
    for (const auto &s : block.statements) {
        curBlock_ = &block; // nested blocks emitted by the previous statement moved it
        curStmt_ = static_cast<size_t>(&s - block.statements.data());
        if (auto *d = dynamic_cast<const DeferStmt *>(s.get())) {
            cleanups_.push_back({Cleanup::Defer, "", &d->body});
            continue;
        }
        bool drain = stmtNeedsDrain(*s);
        std::string mark;
        if (drain) {
            mark = "__ap" + std::to_string(poolCounter_++);
            line("size_t " + mark + " = grav_pool_top;");
        }
        emitStmt(*s);
        fellOff = !isTerminator(*s);
        // The statement's call results are released here (a `return`/`break`
        // leaves them to the caller's or the loop's next release).
        if (drain && fellOff) line("grav_pool_drain(" + mark + ");");
    }
    // Normal exit: run this block's defers, most recent first.
    if (fellOff) {
        for (size_t i = cleanups_.size(); i-- > base + 1;) {
            if (cleanups_[i].kind == Cleanup::Code) { line(cleanups_[i].code); continue; }
            if (cleanups_[i].kind != Cleanup::Defer) continue;
            std::vector<Cleanup> saved = cleanups_;
            cleanups_.resize(i); // a deferred body sees only the cleanups outside it
            emitBraced(*saved[i].body);
            cleanups_ = std::move(saved);
        }
    }
    cleanups_.resize(base);
    curBlock_ = outerBlock;
    curStmt_ = outerStmt;
}

void CodeGen::emitUnwind(bool toLoop, const std::string &labelId) {
    for (size_t i = cleanups_.size(); i-- > 0;) {
        const Cleanup c = cleanups_[i];
        if (c.kind == Cleanup::Loop) {
            if (toLoop && (labelId.empty() || c.label == labelId)) return;
            continue;
        }
        if (c.kind == Cleanup::Scope) continue;
        if (c.kind == Cleanup::Code) { line(c.code); continue; }
        if (c.kind == Cleanup::Try) line("grav_jmp_top = " + c.tryVar + ";");
        if (c.body) {
            std::vector<Cleanup> saved = cleanups_;
            cleanups_.resize(i);
            emitBraced(*c.body);
            cleanups_ = std::move(saved);
        }
    }
}

// The C name of an enclosing loop's label (set up by emitLoopBody's caller).
std::string CodeGen::loopLabelId(const std::string &label) const {
    for (size_t i = loopLabels_.size(); i-- > 0;)
        if (loopLabels_[i].first == label) return loopLabels_[i].second;
    return "";
}

// A labeled loop's body ends with `<id>_next:`, where `continue <label>` jumps
// (after the iteration's cleanups); `break <label>` jumps to `<id>_done:` right
// after the loop.
void CodeGen::emitLoopBody(const Block &body, const std::string &label) {
    std::string id = label.empty() ? "" : loopLabelId(label);
    cleanups_.push_back({Cleanup::Loop, "", nullptr, "", id});
    std::string varRelease = loopVarRelease_;
    loopVarRelease_.clear();
    if (!varRelease.empty()) cleanups_.push_back({Cleanup::Code, "", nullptr, varRelease});
    indent_++;
    if (!loopMarks_.empty() && !loopMarks_.back().empty())
        line("grav_pool_drain(" + loopMarks_.back() + ");"); // the condition's temporaries
    emitBlock(body);
    if (!varRelease.empty()) {
        line(varRelease); // end of an iteration
        cleanups_.pop_back();
    }
    if (!id.empty()) line(id + "_next: ;");
    indent_--;
    cleanups_.pop_back();
}

// On the exception path: run `finally` with the pending exception set aside (an
// exception thrown and caught inside it must not replace it), then re-raise it.
void CodeGen::emitFinallyThenRethrow(const Block *fin) {
    if (fin) {
        std::string sv = "__pend" + std::to_string(tryCounter_++);
        line("{");
        indent_++;
        line("GravExc* " + sv + " = (GravExc*)grav_alloc(sizeof(GravExc));");
        line("grav_exc_save(" + sv + ");");
        emitBraced(*fin);
        line("grav_exc_restore(" + sv + ");");
        line("free(" + sv + ");");
        indent_--;
        line("}");
    }
    line("grav_rethrow();");
}

void CodeGen::emitTry(const TryStmt &s) {
    std::string t = "__try" + std::to_string(tryCounter_++);
    std::string exc = "grav_current_exc";
    const Block *fin = s.hasFinally ? &s.finallyBlock : nullptr;
    line("{");
    indent_++;
    line("int " + t + " = grav_try_push();");
    line("int " + t + "_nf = grav_nframes;"); // frame depth to restore on unwind
    line("size_t " + t + "_ap = grav_pool_top;"); // release-list height to drain back to
    line("if (setjmp(grav_jmp_stack[" + t + "]) == 0) {");
    indent_++;
    cleanups_.push_back({Cleanup::Try, t, fin});
    emitBlock(s.tryBlock);
    cleanups_.pop_back();
    line("grav_jmp_top = " + t + ";"); // normal completion: pop
    indent_--;
    line("} else {");
    indent_++;
    line("grav_jmp_top = " + t + ";"); // exception in flight: pop, then handle
    line("grav_frames_restore(" + t + "_nf);"); // unwinding skipped grav_leave()s
    line("grav_pool_drain(" + t + "_ap);"); // temporaries of the abandoned statements
    // Clauses are tested in order; the first matching class handles it. With a
    // `finally`, the handlers run in their own protected region so an exception
    // escaping a handler (e.g. a re-throw) still runs `finally` on its way out.
    bool guard = fin && !s.catches.empty();
    std::string tc = t + "_c";
    if (guard) {
        line("int " + tc + " = grav_try_push();");
        line("if (setjmp(grav_jmp_stack[" + tc + "]) == 0) {");
        indent_++;
    }
    bool first = true;
    for (const auto &c : s.catches) {
        std::string ct = structName(c.type.name);
        line(std::string(first ? "if" : "} else if") + " (grav_is_instance(" + exc + ".value, &" +
             mangle(c.type.name) + "_typeinfo)) {");
        first = false;
        indent_++;
        // The handler owns the caught object, released when the handler ends.
        line(ct + "* " + c.var + " = (" + ct + "*)grav_exc_take();");
        line("(void)" + c.var + ";");
        bool reg = blockMayCall(c.block);
        if (reg) line("grav_uw_push((void*)&" + c.var + ", grav_uw_obj);");
        std::string varRelease = std::string(reg ? "grav_uw_top--; " : "") + "grav_release((void*)" + c.var + ");";
        cleanups_.push_back({Cleanup::Code, "", nullptr, varRelease});
        // A `return`/`break` out of the handler must still run `finally`.
        if (guard) cleanups_.push_back({Cleanup::Try, tc, fin});
        emitBlock(c.block);
        if (guard) cleanups_.pop_back();
        line(varRelease);
        cleanups_.pop_back();
        indent_--;
    }
    if (!s.catches.empty()) {
        line("} else {");
        indent_++;
    }
    if (guard) line("grav_jmp_top = " + tc + ";");
    emitFinallyThenRethrow(fin); // unhandled here: propagate (after finally)
    if (!s.catches.empty()) {
        indent_--;
        line("}");
    }
    if (guard) {
        line("grav_jmp_top = " + tc + ";"); // handler completed normally
        indent_--;
        line("} else {");
        indent_++;
        line("grav_jmp_top = " + tc + ";"); // a handler threw: finally, then propagate
        line("grav_frames_restore(" + t + "_nf);");
        emitFinallyThenRethrow(fin);
        indent_--;
        line("}");
    }
    indent_--;
    line("}");
    if (fin) emitBraced(*fin); // normal + handled paths
    indent_--;
    line("}");
}

// A labeled loop's C name for its `_next` / `_done` jump targets ("" if unlabeled).
std::string CodeGen::pushLoopLabel(const std::string &label) {
    if (label.empty()) return "";
    std::string id = "grav_loop" + std::to_string(switchCounter_++) + "_" + label;
    loopLabels_.push_back({label, id});
    return id;
}

void CodeGen::popLoopLabel(const std::string &id) {
    if (id.empty()) return;
    line(id + "_done: ;");
    loopLabels_.pop_back();
}

void CodeGen::emitForIn(const ForInStmt &s) {
    std::string labelId = pushLoopLabel(s.label);
    std::string n = std::to_string(switchCounter_++);
    std::string vty = cTy(s.varType);
    if (s.iterKind == ForInKind::Range) {
        // `for (i in lo..hi step s)` -> a counting C loop; bounds evaluated once.
        std::string hi = "__hi" + n;
        long long stepVal = 1;
        bool constStep = !s.step || constInt(s.step.get(), stepVal);
        std::string lt = s.inclusive ? " <= " : " < ";
        std::string gt = s.inclusive ? " >= " : " > ";
        std::string head = "for (" + vty + " " + s.var + " = " + emitExpr(*s.lo) + ", " + hi +
                           " = " + emitExpr(*s.hi);
        if (constStep) {
            std::string cmp = stepVal >= 0 ? lt : gt;
            std::string upd = stepVal == 1    ? s.var + "++"
                              : stepVal == -1 ? s.var + "--"
                                              : s.var + " += " + std::to_string(stepVal);
            line(head + "; " + s.var + cmp + hi + "; " + upd + ") {");
        } else {
            std::string st = "__st" + n;
            line(head + ", " + st + " = " + emitExpr(*s.step) + "; (" + st + " > 0 ? " + s.var +
                 lt + hi + " : " + s.var + gt + hi + "); " + s.var + " += " + st + ") {");
        }
        emitLoopBody(s.body, s.label);
        line("}");
        popLoopLabel(labelId);
        return;
    }

    bool ownedSrc = false, srcReg = false;
    // Element loops: bind the iterable once, then index it.
    std::string idx = s.indexVar.empty() ? "__i" + n : s.indexVar;
    std::string src = "__it" + n;
    std::string elem, count;
    line("{");
    indent_++;
    switch (s.iterKind) {
        case ForInKind::Array: {
            // Iterate in place through a pointer when the array is addressable,
            // instead of copying the whole value struct.
            std::string at = cTy(s.lo->type);
            if (isSimpleLvalue(*s.lo)) {
                line(at + "* " + src + " = &(" + emitExpr(*s.lo) + ");");
                elem = src + "->data[" + idx + "]";
            } else {
                line(at + " " + src + " = " + emitExpr(*s.lo) + ";");
                elem = src + ".data[" + idx + "]";
            }
            count = std::to_string(s.lo->type.arrayLen);
            break;
        }
        case ForInKind::Slice: {
            std::string base = emitExpr(*s.lo);
            line(cTy(s.varType) + "* " + src + " = " + base + ";");
            line("int " + src + "_n = " + base + "__n;");
            elem = src + "[" + idx + "]";
            count = src + "_n";
            break;
        }
        case ForInKind::Bytes:
            line("GravBytes " + src + " = " + emitExpr(*s.lo) + ";");
            elem = src + ".data[" + idx + "]";
            count = "(int)" + src + ".len";
            break;
        case ForInKind::Chars:
            // The string is held for the loop; each character is a fresh string.
            line("const char* " + src + " = " + emitOwned(*s.lo, s.lo->type) + ";");
            srcReg = blockMayCall(s.body);
            if (srcReg) line(unwindPush(s.lo->type, src));
            cleanups_.push_back({Cleanup::Code, "", nullptr, ownedRelease(s.lo->type, src, srcReg)});
            ownedSrc = true;
            break;
        case ForInKind::Object:
        default:
            // The collection is owned for the whole loop (an iteration's releases
            // must not free a collection the loop expression just made).
            line(cTy(s.lo->type) + " " + src + " = " + emitOwned(*s.lo, s.lo->type) + ";");
            if (refKind(s.lo->type) != RefKind::None) {
                line(unwindPush(s.lo->type, src));
                cleanups_.push_back({Cleanup::Code, "", nullptr, ownedRelease(s.lo->type, src, true)});
                ownedSrc = true;
            }
            // length() is re-read each step so the loop tolerates appends.
            count = methodCall(src, s.lo->type, s.lenMethod, "");
            elem = methodCall(src, s.lo->type, s.getMethod, idx);
            break;
    }
    if (s.iterKind == ForInKind::Chars) {
        std::string pos = src + "_p", len = src + "_l";
        line("for (int " + idx + " = 0, " + pos + " = 0, " + len + " = 0; " + src + "[" + pos + "]; " + idx +
             "++, " + pos + " += " + len + ") {");
        indent_++;
        line(len + " = grav_utf8_len(" + src + " + " + pos + ");");
        elem = "grav_str_slice(" + src + " + " + pos + ", " + len + ")";
    } else {
        line("for (int " + idx + " = 0; " + idx + " < " + count + "; " + idx + "++) {");
        indent_++;
    }
    // An element from get(i) is the loop variable's own reference: released at
    // the end of the iteration (and by break/continue/return on the way out).
    bool ownedVar = (s.iterKind == ForInKind::Object || s.iterKind == ForInKind::Chars) &&
                    refKind(s.varType) != RefKind::None;
    line(vty + " " + s.var + " = " + elem + ";");
    bool varReg = ownedVar && blockMayCall(s.body);
    if (varReg) line(unwindPush(s.varType, s.var));
    indent_--;
    if (ownedVar) loopVarRelease_ = ownedRelease(s.varType, s.var, varReg);
    emitLoopBody(s.body, s.label);
    line("}");
    popLoopLabel(labelId); // `break <label>` still releases the iterated collection
    if (ownedSrc) {
        line(cleanups_.back().code);
        cleanups_.pop_back();
    }
    indent_--;
    line("}");
}

// ---------------------------------------------------------------------------
// setjmp safety: locals written inside `try` must be volatile, or an optimizing
// C compiler may keep them in registers that longjmp restores to stale values.
// ---------------------------------------------------------------------------

void CodeGen::collectTryAssigned(const Block &b, int tryDepth) {
    for (const auto &s : b.statements) collectTryAssignedStmt(*s, tryDepth);
}

void CodeGen::collectTryAssignedStmt(const Stmt &s, int depth) {
    auto noteExpr = [&](const Expr *e) {
        if (!e || depth == 0) return;
        if (auto *id = dynamic_cast<const IncDecExpr *>(e))
            if (auto *n = dynamic_cast<const NameExpr *>(id->target.get()))
                volatileNames_.insert(n->name);
    };
    if (auto *x = dynamic_cast<const AssignStmt *>(&s)) {
        if (depth > 0)
            if (auto *n = dynamic_cast<const NameExpr *>(x->target.get()))
                volatileNames_.insert(n->name);
    } else if (auto *x = dynamic_cast<const ExprStmt *>(&s)) {
        noteExpr(x->expr.get());
    } else if (auto *x = dynamic_cast<const BlockStmt *>(&s)) {
        collectTryAssigned(x->block, depth);
    } else if (auto *x = dynamic_cast<const IfStmt *>(&s)) {
        collectTryAssigned(x->thenBlock, depth);
        if (x->elseStmt) collectTryAssignedStmt(*x->elseStmt, depth);
    } else if (auto *x = dynamic_cast<const WhileStmt *>(&s)) {
        collectTryAssigned(x->body, depth);
    } else if (auto *x = dynamic_cast<const DoWhileStmt *>(&s)) {
        collectTryAssigned(x->body, depth);
    } else if (auto *x = dynamic_cast<const ForStmt *>(&s)) {
        if (x->init) collectTryAssignedStmt(*x->init, depth);
        if (x->update) collectTryAssignedStmt(*x->update, depth);
        collectTryAssigned(x->body, depth);
    } else if (auto *x = dynamic_cast<const ForInStmt *>(&s)) {
        collectTryAssigned(x->body, depth);
    } else if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) {
        for (const auto &c : x->cases) collectTryAssigned(c.body, depth);
        if (x->hasDefault) collectTryAssigned(x->defaultBody, depth);
    } else if (auto *x = dynamic_cast<const DeferStmt *>(&s)) {
        collectTryAssigned(x->body, depth);
    } else if (auto *x = dynamic_cast<const TryStmt *>(&s)) {
        collectTryAssigned(x->tryBlock, depth + 1);
        // Handlers run inside a setjmp region of their own when there is a finally.
        for (const auto &c : x->catches) collectTryAssigned(c.block, depth + (x->hasFinally ? 1 : 0));
        collectTryAssigned(x->finallyBlock, depth);
    }
}

void CodeGen::beginBody(const Block &body, const std::vector<std::string> &outerNames) {
    volatileNames_.clear();
    cleanups_.clear();
    curSrcLine_ = 0;
    collectTryAssigned(body, 0);
    analyzeOwnedStrings(body, outerNames);
}

// ---------------------------------------------------------------------------
// Line marks: only statements that can call out (and so appear as a caller
// frame in a trace, or throw) need `GRAV_LINE`; pure arithmetic skips it.
// ---------------------------------------------------------------------------

bool CodeGen::exprMayCall(const Expr *e) const {
    if (!e) return false;
    if (dynamic_cast<const CallExpr *>(e) || dynamic_cast<const NewExpr *>(e) ||
        dynamic_cast<const CBlockExpr *>(e))
        return true;
    if (dynamic_cast<const IntLiteralExpr *>(e) || dynamic_cast<const FloatLiteralExpr *>(e) ||
        dynamic_cast<const BoolLiteralExpr *>(e) || dynamic_cast<const StringLiteralExpr *>(e) ||
        dynamic_cast<const NullLiteralExpr *>(e) || dynamic_cast<const NameExpr *>(e) ||
        dynamic_cast<const SelfExpr *>(e) || dynamic_cast<const SizeofExpr *>(e) ||
        dynamic_cast<const LambdaExpr *>(e))
        return false;
    if (auto *x = dynamic_cast<const BinaryExpr *>(e))
        return !x->overloadMethod.empty() || exprMayCall(x->left.get()) ||
               exprMayCall(x->right.get());
    if (auto *x = dynamic_cast<const UnaryExpr *>(e)) return exprMayCall(x->operand.get());
    if (auto *x = dynamic_cast<const TernaryExpr *>(e))
        return exprMayCall(x->cond.get()) || exprMayCall(x->thenExpr.get()) ||
               exprMayCall(x->elseExpr.get());
    if (auto *x = dynamic_cast<const AsExpr *>(e)) return exprMayCall(x->operand.get());
    if (auto *x = dynamic_cast<const CastExpr *>(e)) return exprMayCall(x->operand.get());
    if (auto *x = dynamic_cast<const IsExpr *>(e)) return exprMayCall(x->operand.get());
    if (auto *x = dynamic_cast<const AddrOfExpr *>(e)) return exprMayCall(x->operand.get());
    if (auto *x = dynamic_cast<const DerefExpr *>(e)) return exprMayCall(x->operand.get());
    if (auto *x = dynamic_cast<const IncDecExpr *>(e)) return exprMayCall(x->target.get());
    if (auto *x = dynamic_cast<const IndexExpr *>(e))
        return !x->overloadClass.empty() || exprMayCall(x->base.get()) ||
               exprMayCall(x->index.get());
    if (auto *x = dynamic_cast<const MemberExpr *>(e)) return exprMayCall(x->object.get());
    if (auto *x = dynamic_cast<const ArrayLiteralExpr *>(e)) {
        for (const auto &el : x->elements) if (exprMayCall(el.get())) return true;
        return false;
    }
    if (auto *x = dynamic_cast<const StructLiteralExpr *>(e)) {
        for (const auto &f : x->fields) if (exprMayCall(f.value.get())) return true;
        return false;
    }
    return true; // unknown node: be conservative
}

bool CodeGen::stmtMayCall(const Stmt &s) const {
    if (auto *x = dynamic_cast<const LetStmt *>(&s)) return exprMayCall(x->init.get());
    if (auto *x = dynamic_cast<const AssignStmt *>(&s))
        return exprMayCall(x->target.get()) || exprMayCall(x->value.get());
    if (auto *x = dynamic_cast<const ReturnStmt *>(&s)) return exprMayCall(x->value.get());
    if (auto *x = dynamic_cast<const ExprStmt *>(&s)) return exprMayCall(x->expr.get());
    if (auto *x = dynamic_cast<const IfStmt *>(&s)) return exprMayCall(x->cond.get());
    if (auto *x = dynamic_cast<const WhileStmt *>(&s)) return exprMayCall(x->cond.get());
    if (auto *x = dynamic_cast<const DoWhileStmt *>(&s)) return exprMayCall(x->cond.get());
    if (auto *x = dynamic_cast<const ForStmt *>(&s))
        return (x->init && stmtMayCall(*x->init)) || exprMayCall(x->cond.get()) ||
               (x->update && stmtMayCall(*x->update));
    if (auto *x = dynamic_cast<const ForInStmt *>(&s))
        return x->iterKind == ForInKind::Object || exprMayCall(x->lo.get()) ||
               exprMayCall(x->hi.get()) || exprMayCall(x->step.get());
    if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) return exprMayCall(x->subject.get());
    if (dynamic_cast<const ThrowStmt *>(&s)) return true;
    return false; // blocks, try, break/continue: their inner statements mark themselves
}

// Like stmtMayCall, but also looking inside nested blocks: whether running `s`
// can reach a call (and so an exception that unwinds this frame).
bool CodeGen::stmtMayCallDeep(const Stmt &s) const {
    if (stmtMayCall(s)) return true;
    if (auto *x = dynamic_cast<const BlockStmt *>(&s)) return blockMayCall(x->block);
    if (auto *x = dynamic_cast<const IfStmt *>(&s))
        return blockMayCall(x->thenBlock) || (x->elseStmt && stmtMayCallDeep(*x->elseStmt));
    if (auto *x = dynamic_cast<const WhileStmt *>(&s)) return blockMayCall(x->body);
    if (auto *x = dynamic_cast<const DoWhileStmt *>(&s)) return blockMayCall(x->body);
    if (auto *x = dynamic_cast<const ForStmt *>(&s)) return blockMayCall(x->body);
    if (auto *x = dynamic_cast<const ForInStmt *>(&s)) return blockMayCall(x->body);
    if (auto *x = dynamic_cast<const DeferStmt *>(&s)) return blockMayCall(x->body);
    if (auto *x = dynamic_cast<const SwitchStmt *>(&s)) {
        for (const auto &c : x->cases) {
            if (exprMayCall(c.guard.get()) || blockMayCall(c.body)) return true;
            for (const auto &v : c.values) if (exprMayCall(v.get())) return true;
        }
        return blockMayCall(x->defaultBody);
    }
    if (auto *x = dynamic_cast<const TryStmt *>(&s)) {
        if (blockMayCall(x->tryBlock) || blockMayCall(x->finallyBlock)) return true;
        for (const auto &c : x->catches) if (blockMayCall(c.block)) return true;
        return false;
    }
    if (dynamic_cast<const BreakStmt *>(&s) || dynamic_cast<const ContinueStmt *>(&s)) return false;
    return !dynamic_cast<const LetStmt *>(&s) && !dynamic_cast<const AssignStmt *>(&s) &&
           !dynamic_cast<const ReturnStmt *>(&s) && !dynamic_cast<const ExprStmt *>(&s);
}

bool CodeGen::blockMayCall(const Block &b, size_t from) const {
    for (size_t i = from; i < b.statements.size(); i++)
        if (stmtMayCallDeep(*b.statements[i])) return true;
    return false;
}

} // namespace grav
