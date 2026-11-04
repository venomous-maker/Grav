#ifndef GRAV_CODEGEN_H
#define GRAV_CODEGEN_H

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
    bool trace_ = true;
    bool lines_ = true;
    bool testMode_ = false;
    std::vector<std::pair<std::string, std::string>> tests_; // display name, C function
    int curSrcLine_ = 0; // source line of the statement being emitted (for #line)
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
    void beginBody(const Block &body, const std::vector<std::string> &outerNames = {});
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
    std::string autoreleaseOf(const TypeRef &t, const std::string &c) const;
    // Does evaluating `e` yield a new reference (a call, `new`, a capturing lambda)?
    bool isRefProducer(const Expr &e) const;
    bool exprMayAutorelease(const Expr *e) const;
    bool stmtNeedsDrain(const Stmt &s) const;
    // The value of `e` converted to `target`, as an owned (+1) reference.
    std::string emitOwned(const Expr &e, const TypeRef &target) const;
    // The C statement storing `value` into the owning place `slot` (an lvalue).
    std::string storeOwned(const std::string &slot, const TypeRef &slotType, const Expr &value) const;
    void emitReleaseFields(const std::string &classFq);  // Class__arc_fields(): typeinfo `fields` hook
    // Parameters a body assigns to: owned for the body (retained on entry).
    void ownAssignedParams(const Block &body, const std::vector<Param> &params);
    void emitBodyEndCleanups();
    std::string emitStable(const Expr &e, const TypeRef &t) const; // a call argument / receiver
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
    };
    std::vector<Cleanup> cleanups_;
    void emitUnwind(bool toLoop);
    void emitForIn(const ForInStmt &s);
    void emitLoopBody(const Block &body);
    // Names assigned inside a `try` block of the current body; such locals are
    // declared `volatile` so their values survive the longjmp into `catch`.
    std::set<std::string> volatileNames_;
    void collectTryAssigned(const Block &b, int tryDepth);
    void collectTryAssignedStmt(const Stmt &s, int tryDepth);
    // Statements that cannot call into other code need no stack-trace line mark.
    bool stmtMayCall(const Stmt &s) const;
    bool exprMayCall(const Expr *e) const;
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
                           const std::vector<TypeRef> &params) const;
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

    // codegen_owned.cpp — automatic freeing of owned local strings.
    std::set<std::string> ownedStrings_;
    mutable std::map<const Expr *, std::string> exprOverride_; // render expr as this C
    // `returnsOwned`: a `return name` of an owned local does not count as an escape
    // (used for functions whose every return hands back a fresh string).
    std::set<std::string> ownedSet(const Block &body, const std::vector<std::string> &outerNames,
                                   bool returnsOwned) const;
    void analyzeOwnedStrings(const Block &body, const std::vector<std::string> &outerNames);
    bool isFreshString(const Expr &e) const;
    bool isFreshStringLegacy(const Expr &e) const; // does it allocate a new string?
    bool emitOwnedLet(const LetStmt &s);
    bool emitOwnedAssign(const AssignStmt &s);
    // Interprocedural: functions/methods whose every return yields a fresh string
    // ("F:fq#overload" / "M:Class.method"), and the owned sets chosen per body.
    std::set<std::string> freshFns_;
    std::map<const Block *, std::set<std::string>> ownedCache_;
    void computeFreshFunctions();
    // Statement-level temporaries: fresh strings used where nothing keeps them
    // (pieces of an allocating `+` chain, `print` pieces, arguments to
    // non-retaining calls) are bound to `grav_tmpN` before the statement and
    // freed right after it. Returns the temp names (already declared).
    std::vector<std::string> hoistTemps(const Expr *root);
    void collectHoists(const Expr *e, std::vector<const Expr *> &out) const;
    void freeTemps(const std::vector<std::string> &temps);
    // Hoisted only to fix left-to-right evaluation order (not freed afterwards).
    mutable std::set<const Expr *> orderOnly_;
    // Chain pieces in source order; when 2+ can run code they are hoisted in order.
    void orderChain(const Expr &chain, bool allocates, std::vector<const Expr *> &out) const;
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
    std::string unboxAny(const std::string &c, const TypeRef &target) const;
    std::string isAnyTest(const std::string &c, const TypeRef &target) const;
    std::string emitCall(const CallExpr &call) const;
    // Emits an expression, adding a C pointer cast when the value flows into a
    // named (class/interface) slot of a different static type (upcasts).
    std::string emitAs(const Expr &expr, const TypeRef &target) const;
    void emitArgs(std::string &out, const CallExpr &call,
                  const std::vector<TypeRef> &params, bool leadingSelf,
                  const std::string &self) const;
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
