#ifndef GRAV_CODEGEN_H
#define GRAV_CODEGEN_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ast/ast.h"
#include "sema/symbols.h"

namespace grav {

// Maps a line in the combined (import-inlined) source back to its original
// file and line, so runtime stack traces point at the user's files.
struct SourceMapEntry {
    std::string file;
    int line = 0;
};

// Translates a type-checked Grav program into a self-contained C translation
// unit. Classes become structs with a leading vtable pointer; single
// inheritance flattens base fields; virtual methods dispatch through per-class
// vtables. Implemented across codegen_types.cpp (structs/vtables), codegen_func.cpp
// (functions/methods/main), codegen_stmt.cpp and codegen_expr.cpp.
class CodeGen {
public:
    std::string generate(const Program &program, const Registry &reg,
                         const std::vector<SourceMapEntry> &srcMap = {});
    // Call-stack tracing (exception stack traces). Off in release builds, which
    // removes every per-call/per-line bookkeeping store from the output.
    void setTrace(bool on) { trace_ = on; }
    // `#line` directives mapping the generated C back to .grav source lines (for
    // debuggers, crash backtraces, and sanitizer reports).
    void setLineDirectives(bool on) { lines_ = on; }
    // `grav test`: emit the test blocks and a runner main instead of the program's main.
    void setTestMode(bool on) { testMode_ = on; }
    // After each function the mapping returns to the C file; the driver replaces
    // this marker with `#line <n> "<file.c>"` once the output path is known.
    static constexpr const char *kLineReset = "#line __GRAV_RESET__";

    // Original file/line for a combined-source line (1-based), for stack traces.
    SourceMapEntry origin(int combinedLine) const;
    // The C expression that formats a thrown value (its describe()/message), or
    // "" if the value's type has no such method.
    std::string emitDescribe(const std::string &objExpr, const TypeRef &type) const;

private:
    // codegen_runtime.cpp
    void emitPrelude();
    // Run-time fault checks (codegen_faults.cpp).
    void emitFaultSupport();
    // Display of any value (codegen_display.cpp).
    void emitDisplaySupport();
    std::string displayC(const std::string &c, const TypeRef &t, bool nested) const;
    std::string displayFn(const TypeRef &t) const;
    std::string formatC(const std::string &c, const TypeRef &t, const std::string &spec) const;
    std::string displayTypeName(const std::string &fq) const;
    mutable std::set<std::string> displayDone_;
    mutable std::string displayProtos_, displayDefs_;
    std::vector<std::string> classInterfaces(const std::string &classFq) const;
    std::string ifaceCast(const std::string &obj, const std::string &iface) const;
    void emitFaultRaisers();
    std::string checkedIndex(const std::string &idx, int length) const;
    std::string emitSlice(const IndexExpr &e) const;
    std::string checkedDivision(BinaryOp op, const TypeRef &type, const std::string &l,
                                const std::string &r, const Expr &divisor) const;
    std::string checkedDivisionAssign(BinaryOp op, const TypeRef &type, const std::string &target,
                                      const std::string &r, const Expr &divisor) const;
    bool trace_ = true;
    bool lines_ = true;
    bool testMode_ = false;
    std::vector<std::pair<std::string, std::string>> tests_; // display name, C function
    int curSrcLine_ = 0; // source line of the statement being emitted (for #line)
    const Block *curBlock_ = nullptr; // the block whose statement curStmt_ is being emitted
    size_t curStmt_ = 0;
    std::string lineDirective(int combinedLine) const; // "#line N \"file\"\n" or ""
    void endFunction();                                // close a body + reset marker

    // codegen_types.cpp
    void emitEnums();
    void emitGlobals(); // module-level const/let and class static fields
    void emitStructs();
    void emitStruct(const std::string &classFq);
    void emitValueStructs();              // plain `struct` + array types, dependency-ordered
    // Walks the whole program collecting every fixed-length array type that needs
    // a backing C struct (into arrayTypes_), so they can be emitted up front.
    void collectArrayTypes();
    void collectInBlock(const Block &block);
    void collectInStmt(const Stmt &stmt);
    void collectInExpr(const Expr &expr);
    void collectType(const TypeRef &t);
    // C spelling of a type for `sizeof` (named class/struct/enum -> the value, not
    // a pointer).
    std::string sizeofSpelling(const TypeRef &t) const;
    // C expression comparing two values of type `t` for `==` (structs member-wise,
    // interfaces/functions by identity, strings by content, binary by bytes).
    std::string equalExpr(const TypeRef &t, const std::string &a, const std::string &b) const;
    void emitVTableTypes();
    void emitVTableType(const ClassInfo &ci);
    void emitVTableInstances();
    void emitVTableInstance(const ClassInfo &ci);
    void emitTypeInfos();
    // Globals and static fields whose initializer is not a C constant: set in
    // grav_init_globals() (declaration order) before main or the tests run.
    struct DynamicGlobal { std::string cName; TypeRef type; const Expr *init; int line; };
    std::vector<DynamicGlobal> dynamicGlobals_;
    bool isStaticInit(const Expr &e) const;
    void emitGlobalInits();
    // Reflection metadata (std.core.ClassInfo): the `&Class__rmeta` table, or "0"
    // for a class outside every hierarchy that has decorators or is named by reflect().
    std::string emitReflection(const std::string &fq);
    bool reflected(const std::string &fq) const;
    mutable std::set<std::string> reflectRoots_;
    mutable bool reflectScanned_ = false;
    void emitInterfaceTables();   // itable types + per-class instances
    // C type spelling, interface-aware (interfaces become a fat pointer struct).
    std::string cTy(const TypeRef &t) const;
    std::string emitPrimCast(const TypeRef &target, const Expr &operand,
                             const std::string &inner) const;
    std::vector<FieldInfo> collectFields(const std::string &classFq) const;
    std::string slotFnType(const VTableSlot &slot) const;

    // codegen_func.cpp
    void emitPrototypes(const Program &program);
    void emitDefinitions(const Program &program);
    void emitFunction(const FunctionDecl &fn);
    void emitClassBodies(const ClassDecl &cls);
    void emitMethod(const ClassDecl &cls, const MethodDecl &m);
    void emitConstructor(const ClassDecl &cls);
    void emitConstructorAt(const ClassDecl &cls, const ConstructorDecl *ctor, int idx);
    // C statements setting every string inside the value `lv` of type `t` to "".
    std::string stringDefaults(const std::string &lv, const TypeRef &t, int depth = 0) const;
    std::string accessorSig(const std::string &classFq, const MethodInfo &m) const;
    void emitAccessorBody(const std::string &classFq, const MethodInfo &m);
    void emitMainWrapper();
    void emitDestructor(const ClassDecl &cls);
    // Shared prologue for any function body: resets per-body state (volatile
    // analysis for setjmp safety) before emitting `body`.
    void beginBody(const Block &body);
    std::string paramList(const std::vector<Param> &params,
                          const std::string &selfStruct) const;

    // codegen_arc.cpp — automatic reference counting. Objects, interface values,
    // closures and `any` values are reference types: owning places (locals,
    // fields, globals, captures, returns) hold one reference each. emitExpr()
    // yields a *borrowed* value valid until the statement ends (call and `new`
    // results go on the per-thread release list); emitOwned() yields +1.
    enum class RefKind { None, Obj, Iface, Fn, Any, Str, Value };
    // Value structs and fixed arrays that hold references: copied by value, with
    // generated K__retain/K__release/K__autorelease for their reference members.
    bool valueHasRefs(const TypeRef &t) const;
    std::string valueTypeName(const TypeRef &t) const;
    std::string emitValueArc(const std::string &key, const TypeRef &t) const;
    mutable std::unordered_map<std::string, int> valueRefsMemo_;
    bool concatAllocates(const Expr &e) const;
    bool isUnstableRef(const Expr &e) const;
    bool isWeakField(const MemberExpr &m) const;
    // Cycle collection: whether a type can hold (strong) object references, and
    // the statement that reports a value's referenced objects to the collector.
    bool gcRelevant(const TypeRef &t) const;
    std::string visitOf(const TypeRef &t, const std::string &c) const;
    std::string emitVisitFields(const std::string &classFq); // returns the hook name, or "0"
    RefKind refKind(const TypeRef &t) const;
    std::string retainOf(const TypeRef &t, const std::string &c) const;
    std::string releaseOf(const TypeRef &t, const std::string &c) const;   // a statement
    // An owned local (a variable, not an expression) is registered while in
    // scope so an exception leaving its frame releases it; ownedRelease
    // unregisters and releases it where its scope ends.
    // A local whose scope can neither call nor fault is never unwound past, so it skips the
    // registration (`registered` false).
    std::string unwindPush(const TypeRef &t, const std::string &var) const;
    std::string ownedRelease(const TypeRef &t, const std::string &var, bool registered) const;
    bool restOfBlockMayThrow(const Stmt &s) const;
    std::string autoreleaseOf(const TypeRef &t, const std::string &c) const;
    // Does evaluating `e` yield a new reference (a call, `new`, a capturing lambda)?
    bool isRefProducer(const Expr &e) const;
    bool exprMayAutorelease(const Expr *e) const;
    bool stmtNeedsDrain(const Stmt &s) const;
    // Whether a return (or, with loopExits, a break/continue) can leave while a
    // statement around it still owes a drain of the release list, or returns a
    // value whose temporaries nobody would drain. Such a function or loop
    // records the list's height and drains back to it on the way out.
    bool exitSkipsDrain(const Block &b, bool under, bool loopExits) const;
    bool exitSkipsDrainStmt(const Stmt &s, bool under, bool loopExits) const;
    void forInMark(const ForInStmt &s);
    void forInEnd();
    std::string fnPoolMark_; // the release-list height at function entry ("" when not needed)
    // Methods that return their receiver borrowed (see returnsSelfBorrowed).
    bool returnsSelfBorrowed(const std::string &classFq, const std::string &method) const;
    mutable std::map<std::string, bool> selfReturnCache_;
    bool curMethodBorrowsSelf_ = false;
    std::string returnValue(const Expr &value) const;
    // The value of `e` converted to `target`, as an owned (+1) reference.
    std::string emitOwned(const Expr &e, const TypeRef &target) const;
    // The C statement storing `value` into the owning place `slot` (an lvalue).
    std::string storeOwned(const std::string &slot, const TypeRef &slotType, const Expr &value) const;
    void emitReleaseFields(const std::string &classFq);  // Class__arc_fields(): typeinfo `fields` hook
    // Parameters a body assigns to: owned for the body (retained on entry).
    void ownAssignedParams(const Block &body, const std::vector<Param> &params);
    void emitBodyEndCleanups();
    std::string emitStable(const Expr &e, const TypeRef &t) const; // a call argument / receiver
    // Methods that can drop no reference while they run (see releaseFree): a
    // receiver or argument read from a field needs no holding for them.
    bool releaseFree(const MethodInfo *mi) const;
    bool releaseFreeCall(const CallExpr &call) const;      // the call's target is such a method
    mutable std::map<const MethodInfo *, int> releaseFreeCache_; // 1 yes, 2 no, 3 being worked out
    mutable const Expr *ownedRoot_ = nullptr; // emitOwned: this producer transfers, no autorelease
    int poolCounter_ = 0;
    std::vector<std::string> loopMarks_;      // release-list depth before each enclosing loop
    std::string loopVarRelease_;              // for-in: release of an owned loop variable
    std::string emitExprRaw(const Expr &expr) const;

    // codegen_flow.cpp — structured control flow with cleanups.
    // Every construct that must run code on an early exit pushes an entry: a
    // `return` unwinds all of them, `break`/`continue` unwind to the nearest loop.
    struct Cleanup {
        enum Kind { Scope, Loop, Try, Finally, Defer, Code } kind;
        std::string tryVar;          // Try: the jmp-top slot to restore
        const Block *body = nullptr; // Try/Finally: finally block; Defer: deferred body
        std::string code;            // Code: a C statement (e.g. freeing an owned string)
        std::string label;           // Loop: the C name behind a labeled loop's label
    };
    std::vector<Cleanup> cleanups_;
    // `break outer` / `continue outer`: the Grav label -> its C name, innermost last.
    std::vector<std::pair<std::string, std::string>> loopLabels_;
    std::string loopLabelId(const std::string &label) const;
    std::string pushLoopLabel(const std::string &label);
    void popLoopLabel(const std::string &id);
    void emitUnwind(bool toLoop, const std::string &labelId = "");
    void emitForIn(const ForInStmt &s);
    void emitLoopBody(const Block &body, const std::string &label = "");
    // Names assigned inside a `try` block of the current body; such locals are
    // declared `volatile` so their values survive the longjmp into `catch`.
    std::set<std::string> volatileNames_;
    void collectTryAssigned(const Block &b, int tryDepth);
    void collectTryAssignedStmt(const Stmt &s, int tryDepth);
    // Statements that cannot call into other code need no stack-trace line mark.
    bool stmtMayCall(const Stmt &s) const;
    bool exprMayCall(const Expr *e) const;
    bool exprMayThrow(const Expr *e) const;
    bool stmtMayThrow(const Stmt &s) const;
    bool blockMayThrow(const Block &b, size_t from = 0) const;
    bool readsMutable(const Expr *e) const;

    // codegen_stmt.cpp
    void emitBlock(const Block &block);
    void emitStmt(const Stmt &stmt);
    void emitIf(const IfStmt &s);        // if / else-if / else chain
    void emitTry(const TryStmt &s);
    void emitFinallyThenRethrow(const Block *fin);      // try / catch / finally via setjmp
    int tryCounter_ = 0;
    std::string emitAssign(const AssignStmt &s) const; // plain or compound assignment
    void emitBraced(const Block &block); // "{\n …\n}" at the current indent
    std::string inlineSimple(const Stmt *s) const; // render a for-header clause
    int switchCounter_ = 0;

    // codegen_dispatch.cpp — method calls, operator methods, string building.
    // C for calling instance method `method` on receiver C expression `obj` of
    // static type `recv` (class or interface). `args` is the already-converted
    // argument list without the receiver ("" or "a, b"). Calls that no subclass
    // overrides become direct calls; the rest go through a small `static inline`
    // thunk so the receiver is evaluated exactly once. `nullSafe` returns a zero
    // value instead of dispatching when the receiver is null (`a?.m()`).
    std::string methodCall(const std::string &obj, const TypeRef &recv,
                           const std::string &method, const std::string &args,
                           bool nullSafe = false) const;
    // Converted argument list (no receiver) for a call to `method` of `recvFq`.
    std::string methodArgs(const std::vector<ExprPtr> &args,
                           const std::vector<TypeRef> &params, bool borrowed = false) const;
    mutable std::map<std::string, std::string> thunks_; // name -> definition
    mutable std::set<std::string> enumNameFns_;          // enums needing a name table
    // One piece of a fused string expression: literal text or a printf conversion.
    struct FmtPart {
        bool literal = false;
        std::string text; // literal text, or the C argument expression
        std::string conv; // printf conversion ("%s", "%lld", ...) for non-literals
    };
    void flattenString(const Expr &e, std::vector<FmtPart> &parts) const;
    // Renders a string-typed expression; `+` chains become one allocation.
    std::string emitStringExpr(const Expr &e) const;
    std::string emitPrint(const Expr &arg) const;
    // C expression rendering a displayable value (`str(x)` semantics).
    std::string emitToString(const Expr &e) const;
    // Name of an enum's member-name lookup function (generated on demand).
    std::string enumNameFn(const std::string &enumFq) const;
    static std::string fmtEscape(const std::string &s); // C-escape + double '%'

    // codegen_order.cpp — evaluation order within a statement.
    mutable std::map<const Expr *, std::string> exprOverride_; // render expr as this C
    // Subexpressions that must run first to keep Grav's left-to-right order are
    // bound to `grav_tmpN` before the statement (hoistTemps returns the names,
    // already declared); endTemps drops their overrides after it.
    std::vector<std::string> hoistTemps(const Expr *root);
    std::vector<std::string> hoistAssignTemps(const Expr &target, const Expr *value);
    std::vector<std::string> emitHoists(const std::vector<const Expr *> &hs);
    void collectHoists(const Expr *e, std::vector<const Expr *> &out) const;
    void endTemps(const std::vector<std::string> &temps);
    void orderOperands(const std::vector<const Expr *> &ops, std::vector<const Expr *> &out,
                       bool hoistLast = true) const;
    std::string hoistValue(const Expr &h) const;
    // Chain pieces in source order; when 2+ can run code they are hoisted in order.
    void orderChain(const Expr &chain, std::vector<const Expr *> &out) const;
    // Inside an expression: the hoists wrapped in a statement expression.
    std::string withHoists(const std::vector<const Expr *> &hs, const std::function<std::string()> &render) const;
    std::string emitOrdered(const Expr &e, const std::function<std::string()> &render = nullptr) const;
    mutable int orderCounter_ = 0;
    std::string concatParts(const std::vector<FmtPart> &parts) const; // merged parts -> C

    // codegen_lambda.cpp — closures and function values.
    std::vector<const LambdaExpr *> lambdas_; // every lambda, inner before outer
    std::string lambdaDecls_;                 // env structs, prototypes, constructors
    std::string asyncDecls_;                  // per-async-fn task record + spawner
    void emitAsyncSpawner(const FunctionDecl &fn);
    void emitLambdas();                       // lift all lambdas (before other bodies)
    void emitLambda(const LambdaExpr &lam, int id);
    std::string emitLambdaValue(const LambdaExpr &lam) const;
    std::string emitFnRef(const NameExpr &e) const;   // `double` used as a value
    std::string emitFnCall(const CallExpr &call) const;
    std::string fnSignature(const TypeRef &fnType) const; // "R (*)(void*, A...)"
    mutable std::map<std::string, std::string> fnCallers_; // C signature -> helper name

    // codegen_expr.cpp
    std::string emitExpr(const Expr &expr) const;
    // `any` boxing / unboxing / type tests.
    std::string boxAny(const Expr &e) const;
    std::string boxAnyC(const std::string &v, const TypeRef &t) const;
    std::string nullableEq(const BinaryExpr &e) const;
    std::string unwrapC(const Expr &x) const;
    std::string unboxAny(const std::string &c, const TypeRef &target) const;
    std::string isAnyTest(const std::string &c, const TypeRef &target) const;
    std::string emitCall(const CallExpr &call) const;
    // Emits an expression, adding a C pointer cast when the value flows into a
    // named (class/interface) slot of a different static type (upcasts).
    std::string emitAs(const Expr &expr, const TypeRef &target) const;
    void emitArgs(std::string &out, const CallExpr &call,
                  const std::vector<TypeRef> &params, bool leadingSelf,
                  const std::string &self, bool borrowed = false) const;
    // Emits args for a variadic call: fixed args, then the trailing count and an
    // element-pointer (a C compound literal, or null when there are none).
    void emitVariadicArgs(std::string &out, const CallExpr &call,
                          const std::vector<TypeRef> &params) const;
    static std::string escapeC(const std::string &s);
    // Moves `#include`/`#pragma` lines from an inline-C block to file scope
    // (`hoisted_`) and returns the remaining code for in-place emission.
    std::string hoistIncludes(const std::string &code) const;

    const Registry *reg_ = nullptr;
    const Program *program_ = nullptr;
    std::vector<SourceMapEntry> srcMap_;
    int throwCounter_ = 0;
    // Emits `GRAV_LINE(n);` for the original line of an AST node (stack traces).
    void emitLineMark(int combinedLine);
    // Emits `grav_enter("name", "file");` (a stack frame) at a body's start.
    void emitEnter(const std::string &displayName, int declLine);

    // Fixed-length array types in use, keyed by their C struct name (deduped).
    std::map<std::string, TypeRef> arrayTypes_;
    std::map<std::string, TypeRef> optTypes_; // nullable value types (`int?`) by C struct name

    // Output sections, concatenated in order by generate().
    std::string typedefs_;
    // `#include`/`#pragma` lines hoisted out of inline-C blocks to file scope.
    mutable std::string hoisted_;
    std::string structs_;
    std::string vtableTypes_;
    std::string protos_;
    std::string globals_;  // module-level const/let and static fields
    std::string cblocks_; // verbatim top-level `%{ ... %}` C
    std::string vtables_;
    std::string defs_;

    std::string *cur_ = nullptr; // current append target for statement emission
    int indent_ = 0;
    void line(const std::string &text);
    bool hasMain_ = false;
    TypeRef currentReturnType_; // return type of the function/method being emitted
};

} // namespace grav

#endif // GRAV_CODEGEN_H
