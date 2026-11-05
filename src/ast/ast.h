#ifndef GRAV_AST_H
#define GRAV_AST_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/types.h"

namespace grav {

// ===========================================================================
// Expressions
// ===========================================================================

struct Expr {
    int line = 0;
    int col = 0;
    TypeRef type; // filled in by the type checker
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteralExpr : Expr {
    long long value = 0;
    std::string raw;
};

struct FloatLiteralExpr : Expr {
    std::string raw;
};

struct BoolLiteralExpr : Expr {
    bool value = false;
};

struct StringLiteralExpr : Expr {
    std::string value;
};

// `null` — the null reference, assignable to any class/interface type.
struct NullLiteralExpr : Expr {};

// `%{ ... %}` — an inline C escape hatch. As an expression it emits the raw C
// verbatim; its type comes from context (a typed `let` or an `as Type`). As a
// statement (an ExprStmt wrapping it) it emits raw C statements that can read and
// write Grav locals by name. At top level it becomes a CBlockDecl.
struct CBlockExpr : Expr {
    std::string code;
};

// A bare name: a local variable, parameter, or the leading segment of a
// qualified reference (class / namespace) that the checker resolves later.
struct NameExpr : Expr {
    std::string name;
    // Set by the checker when the name resolves to a global / static value: the
    // C identifier to emit (otherwise the local `name` is used verbatim).
    std::string resolvedGlobal;
    // Set when the name is a free function used as a value (`let f = double`):
    // its fully-qualified name and chosen overload.
    std::string fnRef;
    int fnRefOverload = 0;
    // `Stack<int>.of(3)`: the type arguments of a generic class named as a static
    // target (monomorphization replaces `name` with the instance and clears them).
    std::vector<TypeRef> typeArgs;
};

// `self` (or `this`) inside a method body.
struct SelfExpr : Expr {};
// `super` — the current instance viewed as its parent class. Only valid as the
// callee of `super(...)` (parent constructor) or `super.method(...)`.
struct SuperExpr : Expr {};

// `name: value` in an argument list. The checker moves it to its parameter's
// position; none are left by the time code is generated.
struct NamedArgExpr : Expr {
    std::string name;
    ExprPtr value;
};

// A parameter's default value standing in for an argument the call left out.
// It is checked where the callee was declared (its namespace, no caller locals),
// then replaced by `value` once the call is resolved.
struct DefaultArgExpr : Expr {
    ExprPtr value;
    std::string ns;
};

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    BitAnd, BitOr, BitXor, Shl, Shr,
    Eq, NotEq, Greater, Less, GreaterEq, LessEq,
    And, Or,
    Pow, // `a ** b`
};
const char *binaryOpSymbol(BinaryOp op);
bool isComparison(BinaryOp op);
bool isLogical(BinaryOp op);
// Operators defined only on ints (modulo, bitwise, shifts).
bool isIntOnly(BinaryOp op);

struct BinaryExpr : Expr {
    BinaryOp op;
    ExprPtr left;
    ExprPtr right;
    bool stringConcat = false; // set by checker: '+' on two strings
    // Set by the checker when a class operator method implements this operator
    // (`fn operator+(o: T)` is the method `op_add`). `negate` flips the result
    // (`!=` falling back to `op_eq`).
    std::string overloadMethod;
    std::string overloadClass;
    bool overloadNegate = false;
    bool overloadSwap = false; // `a > b` as `b < a` (from operator<)
};

// Prefix unary: '!' (logical not) and '~' (bitwise not).
enum class UnaryOp { Not, BitNot };
struct UnaryExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
};

// `cond ? thenExpr : elseExpr`.
struct TernaryExpr : Expr {
    ExprPtr cond;
    ExprPtr thenExpr;
    ExprPtr elseExpr;
};

// `expr as Type` — an explicit value cast (numeric/enum/class).
struct AsExpr : Expr {
    ExprPtr operand;
    TypeRef target;
};

// `expr is ClassName` — a runtime type test (RTTI), yields bool.
struct IsExpr : Expr {
    ExprPtr operand;
    TypeRef target;        // the tested type (`x is int` on an `any`, or a class)
    std::string typeName;  // as written; checker rewrites to FQ class name
    std::string className; // resolved FQ class
};

// `await expr` — unwraps a Future<T> produced by an async call, yielding T.
struct AwaitExpr : Expr {
    ExprPtr operand;
};

// `&lvalue` — address-of, producing a pointer to the operand.
struct AddrOfExpr : Expr {
    ExprPtr operand;
};

// `*ptr` — pointer dereference (an lvalue).
struct DerefExpr : Expr {
    ExprPtr operand;
};

// `a ?? b` — null-coalescing: `a` unless it is null, otherwise `b`. Lowered to a
// compile-time "if null then b else a" conditional.
struct CoalesceExpr : Expr {
    ExprPtr left;
    ExprPtr right;
};

// ++x / --x (prefix) and x++ / x-- (postfix) on a numeric lvalue.
struct IncDecExpr : Expr {
    ExprPtr target;     // NameExpr or MemberExpr (lvalue)
    bool isIncrement;   // ++ vs --
    bool isPrefix;      // ++x vs x++
};

// Explicit cast: int(...), float(...), bool(...).
struct CastExpr : Expr {
    TypeRef target;
    ExprPtr operand;
};

// `sizeof(T)` (a type) or `sizeof(expr)` (a value), yielding the byte size as int.
struct SizeofExpr : Expr {
    bool isType = false;  // sizeof(T) vs sizeof(expr)
    TypeRef target;       // when isType
    ExprPtr operand;      // when !isType
};

// `[a, b, c]` — a fixed-length array literal. Its element type and length are
// inferred from the elements (length == elements.size()).
struct ArrayLiteralExpr : Expr {
    std::vector<ExprPtr> elements;
};

// `base[index]` — element access on an array (or pointer). An lvalue.
// On a class with `operator[]` it is a call to `op_index` (`op_index_set` when
// assigned); `overloadClass` is then the receiver's class.
struct IndexExpr : Expr {
    std::string overloadClass;
    std::string overloadMethod; // internal name of the chosen op_index / op_index_set
    ExprPtr base;
    ExprPtr index;
    std::vector<ExprPtr> more;  // `m[r, c]`: the indices after the first (operator[] only)
    // `a[lo..hi]` / `a[lo..=hi]` (either end may be left out): a slice. The checker
    // fills in missing ends and moves `hi` to more[0].
    bool isSlice = false;
    bool sliceInclusive = false;
    ExprPtr sliceHi;
};

// `object.member`. Used both as a value (field read) and as the callee of a
// call (method / static method). The checker tags how it resolved.
enum class MemberKind {
    Unresolved, InstanceField, StaticTarget, NamespaceTarget, MethodRef, EnumValue,
    StaticField // `Class.field` — a class-level static value
};
struct MemberExpr : Expr {
    ExprPtr object;
    std::string member;
    bool optional = false; // written with `?.` — guard against a null object
    MemberKind kind = MemberKind::Unresolved;
    // For InstanceField / MethodRef: fully-qualified owning class.
    std::string ownerClass;
    // For StaticTarget / NamespaceTarget: the resolved qualified prefix.
    std::string qualified;
};

// `new ClassName(args)`.
struct NewExpr : Expr {
    std::string className;   // as written; checker rewrites to FQ name
    int ctorIndex = 0;       // chosen constructor overload (set by the checker)
    std::vector<TypeRef> typeArgs; // generic args, e.g. `new Box<int>(...)`
    std::vector<ExprPtr> args;
};

// One `name: value` initializer inside a struct literal.
struct StructFieldInit {
    std::string name;
    ExprPtr value;
    int line = 0, col = 0;
};

// `Point { x: 1, y: 2 }` — a value of a plain struct type.
struct StructLiteralExpr : Expr {
    std::string typeName;   // as written; checker rewrites to FQ name
    std::vector<TypeRef> typeArgs; // generic args, e.g. `Box<int> { value: 5 }`
    std::vector<StructFieldInit> fields;
};

enum class CallKind {
    Unresolved,
    FreeFunction,   // fqName()
    Builtin,        // print(...)
    InstanceMethod, // obj.m(...)  -> virtual dispatch
    StaticMethod,   // Class.m(...)
    FnValue,          // f(x) where f is a function value (lambda / function reference)
    SuperConstructor, // super(...)        -> run the parent constructor on self
    SuperMethod,    // super.m(...)        -> parent's method impl, non-virtual
};
struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<TypeRef> typeArgs; // turbofish generic args: `id::<int>(x)`
    std::vector<ExprPtr> args;
    bool spreadLast = false;       // `f(a, ...xs)`: the last argument's elements fill the variadic parameter

    CallKind kind = CallKind::Unresolved;
    std::string targetName;  // FQ free function / static method / builtin name
    std::string methodName;  // for instance/static methods
    std::string ownerClass;  // FQ class (or interface, if ifaceDispatch) of the method
    std::string slotOwner;   // FQ class that introduced the vtable slot
    bool ifaceDispatch = false; // dispatch through an interface itable (fat pointer)
    int resolvedOverload = 0;   // chosen free-function overload index (see FunctionDecl)
};

// ===========================================================================
// Statements
// ===========================================================================

struct Stmt {
    int line = 0;
    int col = 0;
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct LetStmt : Stmt {
    std::string name;
    bool isConst = false;
    bool hasDeclaredType = false;
    TypeRef declaredType; // when hasDeclaredType; otherwise inferred
    TypeRef resolvedType; // final type after checking
    ExprPtr init;
    bool quietUnused = false; // generated binding: no "unused variable" warning
};

struct AssignStmt : Stmt {
    ExprPtr target; // NameExpr or MemberExpr (lvalue)
    ExprPtr value;
    // Compound assignment (`x += y`): when set, the C output becomes
    // `target <op>= value`. `op` is one of Add/Sub/Mul/Div/Mod.
    bool isCompound = false;
    BinaryOp compoundOp = BinaryOp::Add;
    // A compound assignment through operator methods (`obj[k] += v` with
    // operator[]/operator[]=, or `x += y` on a class with operator+): the checker
    // rewrites it to `t = t + v`, reading through `readBack` (a copy of the
    // target); the generated code evaluates the target's object/container and
    // indices once for both.
    const Expr *readBack = nullptr;
};

struct ReturnStmt : Stmt {
    ExprPtr value; // may be null
};

struct ExprStmt : Stmt {
    ExprPtr expr;
};

struct Block {
    std::vector<StmtPtr> statements;
};

// A bare `{ ... }` used as a statement (e.g. an `else` body).
struct BlockStmt : Stmt {
    Block block;
};

struct IfStmt : Stmt {
    ExprPtr cond;
    Block thenBlock;
    StmtPtr elseStmt; // null, a BlockStmt, or another IfStmt (else-if)
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    Block body;
    std::string label; // `outer: while (...)`, for `break outer` / `continue outer`
};

struct DoWhileStmt : Stmt {
    Block body;
    ExprPtr cond;
    std::string label;
};

// switch / match: each case matches one or more constant values; no fall-through.
struct SwitchCase {
    std::vector<ExprPtr> values;
    // `case lo..hi:` / `case lo..=hi:`: values[i] is lo and rangeHi[i] hi (null
    // for a single value).
    std::vector<ExprPtr> rangeHi;
    std::vector<bool> rangeInclusive;
    ExprPtr guard; // `case v if cond:` — the case applies only when cond holds
    Block body;
};
struct SwitchStmt : Stmt {
    ExprPtr subject;
    std::vector<SwitchCase> cases;
    bool hasDefault = false;
    Block defaultBody;
    bool isExpr = false; // the body of a `match` expression: every arm returns its value
};

// C-style: for (init; cond; update) { body }. Any of init/cond/update may be null.
struct ForStmt : Stmt {
    StmtPtr init;   // LetStmt / AssignStmt / ExprStmt or null
    ExprPtr cond;   // or null (treated as true)
    StmtPtr update; // AssignStmt / ExprStmt or null
    Block body;
    std::string label;
};

// `for (i in lo..hi [step s]) { ... }` — i takes lo, lo+s, … up to hi (exclusive
// with `..`, inclusive with `..=`); s defaults to 1 and may be negative.
// `for (x in xs)` / `for (i, x in xs)` iterate the elements of a fixed array, a
// slice (variadic param), a `binary`, or a collection object exposing
// `length()`/`size()` plus `get(i)`/`at(i)` (e.g. Array<T>, List<T>).
enum class ForInKind { Range, Array, Slice, Bytes, Chars, Object };
struct ForInStmt : Stmt {
    std::string label;
    std::string var;
    std::string indexVar;   // optional `i` in `for (i, x in xs)`
    ExprPtr lo;             // range start, or the iterable for element loops
    ExprPtr hi;             // null for element loops
    ExprPtr step;           // optional `step` expression (ranges only)
    bool inclusive = false; // `..=` includes hi
    Block body;
    // Filled by the checker.
    ForInKind iterKind = ForInKind::Range;
    TypeRef varType;        // loop variable type (range counter / element)
    std::string lenMethod;  // Object: `length` or `size`
    std::string getMethod;  // Object: `get` or `at`
};

// `defer stmt` / `defer { ... }` — runs when the enclosing block exits (normal
// fall-through, `return`, `break`, `continue`), last-registered first.
struct DeferStmt : Stmt {
    Block body;
};

// `break` / `continue`, optionally naming an enclosing labeled loop.
struct BreakStmt : Stmt { std::string label; };
struct ContinueStmt : Stmt { std::string label; };

// `throw expr;` — raises a class instance as an exception.
struct ThrowStmt : Stmt {
    ExprPtr value;
};

// `try { ... } catch (e: A) { ... } catch (e: B) { ... } [finally { ... }]`.
// Catch clauses are tried in order; the first whose class matches handles it.
struct CatchClause {
    std::string var;
    TypeRef type;          // a class type
    Block block;
    int line = 0, col = 0;
};
struct TryStmt : Stmt {
    Block tryBlock;
    std::vector<CatchClause> catches;
    bool hasFinally = false;
    Block finallyBlock;
};

// ===========================================================================
// Declarations
// ===========================================================================

enum class Access { Public, Private, Protected };
const char *accessName(Access a);

struct Param {
    std::string name;
    TypeRef type;
    bool variadic = false; // `...name: T` — collects trailing args (last param only)
    // `name: T = value`: used when a call leaves the argument out. Shared between
    // copies of a declaration; every use works on a clone.
    std::shared_ptr<Expr> defaultValue;
};

struct FieldDecl {
    Access access = Access::Public;
    bool isReadonly = false;
    bool isWeak = false; // `weak parent: Node`: does not keep the object alive
    std::string name;
    TypeRef type;
    int line = 0, col = 0;
};

struct MethodDecl {
    Access access = Access::Public;
    bool isStatic = false;
    bool isAbstract = false;
    // `name` is the internal (C-level) name: overloads get a signature suffix
    // during symbol registration. `sourceName` is the name as written ("" until
    // registration; then equal to `name` for non-overloaded methods).
    std::string name;
    std::string sourceName;
    // `fn mapTo<U>(...)`: a generic method, instantiated per `obj.mapTo::<X>(...)`
    // call (as `mapTo$X`) before symbol registration; the template is not emitted.
    std::vector<std::string> typeParams;
    std::vector<Param> params;
    TypeRef returnType; // Void if omitted
    Block body;
    bool hasBody = true;
    bool isFinal = false; // `final fn`: cannot be overridden
    int line = 0, col = 0;
};

struct ConstructorDecl {
    std::vector<Param> params;
    Block body;
    bool present = false;
    int line = 0, col = 0;
};

// `destructor() { ... }` — runs when `free(obj)` releases an instance (virtually:
// the dynamic type's destructor runs, then each base's, most-derived first).
struct DestructorDecl {
    Block body;
    bool present = false;
    int line = 0, col = 0;
};

struct Decl {
    int line = 0, col = 0;
    std::string fqName; // fully-qualified, filled during parse from namespace
    // `@Name` decorators and `export` modifier (metadata; no runtime effect —
    // every top-level name is already visible across modules).
    std::vector<std::string> decorators;
    bool exported = false;
    virtual ~Decl() = default;
};
using DeclPtr = std::unique_ptr<Decl>;

// A class-level `static x: T = value` field — lowered to a C global.
struct StaticFieldDecl {
    Access access = Access::Public;
    bool isConst = false;
    std::string name;
    TypeRef type;
    ExprPtr init;
    int line = 0, col = 0;
};

struct ClassDecl : Decl {
    std::string name;          // simple name
    std::vector<std::string> typeParams; // generic params, e.g. <T, U>
    std::vector<std::string> typeParamBounds; // optional `T: Bound` (parallel; "" if none)
    bool isAbstract = false;
    std::string baseName;      // primary base (prefix layout), empty if none
    std::vector<TypeRef> baseArgs; // generic args on the base, e.g. extends Stack<T>
    // Additional bases in `extends A, B, C`: their fields and methods are flattened
    // into this class (multiple inheritance). The primary base keeps prefix layout.
    std::vector<std::string> extraBases;
    std::vector<std::vector<TypeRef>> extraBaseArgs; // parallel to extraBases
    std::vector<std::string> interfaceNames;
    std::vector<std::vector<TypeRef>> interfaceArgs; // parallel to interfaceNames
    // `uses field: Type` — composition: `field` is a delegate the class forwards
    // the public methods of `Type` to (no `is-a` subtyping).
    std::vector<Param> delegates;
    std::vector<FieldDecl> fields;
    std::vector<StaticFieldDecl> staticFields;
    ConstructorDecl constructor;          // the first constructor (if any)
    std::vector<ConstructorDecl> extraCtors; // further overloads, in source order
    DestructorDecl destructor;
    std::vector<MethodDecl> methods;
    bool isFinal = false; // `final class`: cannot be extended
    // Data enums (`enum Shape { Circle(r: float), Empty }`) become an abstract
    // base class plus one final class per variant, named `Shape$Circle`.
    std::vector<std::string> enumVariants; // on the base: variant names, in order
    std::string enumTemplate;              // base and variants: the enum's base class name
    int variantTag = -1;                   // on a variant: its index in enumVariants
    std::vector<std::string> variantFields; // on a variant: payload field names, in order
};

struct InterfaceDecl : Decl {
    std::string name;
    std::vector<std::string> typeParams; // generic params, e.g. <T>
    std::vector<std::string> typeParamBounds; // optional `T: Bound` (parallel; "" if none)
    std::vector<MethodDecl> methods; // signatures (hasBody == false)
};

// A plain data type: named fields, value semantics, no methods/vtable/RTTI.
struct StructDecl : Decl {
    std::string name;
    std::vector<std::string> typeParams; // generic params, e.g. <T>
    std::vector<std::string> typeParamBounds; // optional `T: Bound` (parallel; "" if none)
    std::vector<FieldDecl> fields;
    // Methods are free functions `S__m(self: S*, ...)` (the parser writes them):
    // method name -> the function's fully-qualified name (per instance, if generic).
    std::map<std::string, std::string> methodFns;
};

struct FunctionDecl : Decl {
    std::string name;
    std::vector<std::string> typeParams; // generic params, e.g. <T>
    std::vector<std::string> typeParamBounds; // optional `T: Bound` (parallel; "" if none)
    bool isAsync = false; // callers receive a Future<returnType>
    std::vector<Param> params;
    TypeRef returnType;
    Block body;
    // 0 for the first (or only) function of this name; 1, 2, ... for later
    // overloads, so each gets a distinct C symbol. Set during symbol registration.
    int overloadIndex = 0;
};

// A top-level `const NAME: T = value;` (or `let`) — a module-level global,
// lowered to a C global variable.
struct GlobalVarDecl : Decl {
    std::string name;
    bool isConst = false;
    bool hasDeclaredType = false;
    TypeRef declaredType;
    TypeRef resolvedType;
    ExprPtr init;
};

// A C-style enumeration: a named set of integer constants.
struct EnumMember {
    std::string name;
    int line = 0, col = 0;
    long long value = 0; // `A = 5`; otherwise one more than the previous member (from 0)
};
struct EnumDecl : Decl {
    std::string name;
    std::vector<EnumMember> members;
    bool customValues = false; // some member has an explicit `= value`
};

// `type Name = T;` — a transparent alias for an existing type. Aliases are fully
// expanded to their canonical target during symbol resolution, so they leave no
// trace in the generated C.
struct TypeAliasDecl : Decl {
    std::string name;
    std::vector<std::string> typeParams; // generic params, e.g. type Vec<T> = T[3]
    TypeRef target; // as written; canonicalized by the registry
};

// `test "name" { ... }` — a unit test, compiled and run only by `grav test`.
struct TestDecl : Decl {
    std::string name;
    Block body;
};

// A top-level `%{ ... %}` block: verbatim C emitted into the translation unit
// (for #includes, globals, and helper functions/macros).
struct CBlockDecl : Decl {
    std::string code;
};

// `fn(x: int, y) => x + y` or `fn(x: int) -> int { ... }` — an anonymous
// function value. Parameter types may be omitted when the expected function type
// is known (an argument, a typed `let`, a `return`). Captured outer variables are
// copied (by value) into a heap environment when the lambda is created.
struct LambdaExpr : Expr {
    std::vector<Param> params;     // untyped params have type Kind::Error
    std::vector<bool> paramTyped;  // parallel: did the source give a type?
    bool hasReturnType = false;
    TypeRef returnType;
    bool isArrow = false;          // `=> expr` form
    bool isMatch = false;          // the lambda a `match` expression lowers to
    ExprPtr exprBody;              // arrow form
    Block body;                    // block form
    // Filled by the checker.
    struct Capture { std::string name; TypeRef type; };
    std::vector<Capture> captures;
    TypeRef fnType;
    // Filled by codegen (lifting order); mutable because emission is const.
    mutable int liftId = -1;
};

// Signatures of generic templates, exported by the monomorphizer so the type
// checker can infer omitted (or `auto`) type arguments from call arguments.
struct GenericSig {
    std::vector<std::string> typeParams; // parameters to infer
    std::vector<TypeRef> params;         // in terms of typeParams
    TypeRef ret;
    bool variadic = false;
    std::vector<std::string> paramNames;               // for named arguments
    std::vector<std::shared_ptr<Expr>> defaults;       // parallel to params (null: required)
    std::string ns;                                    // where the defaults are written
};
struct GenericTables {
    std::map<std::string, std::vector<GenericSig>> functions; // generic fn name -> signature
    std::map<std::string, std::vector<GenericSig>> ctors;     // generic class -> constructors
    std::map<std::string, std::vector<GenericSig>> methods;   // "Class.method" -> generic methods
    // Instance name -> (template, type arguments), e.g. "Array$int" -> ("Array", [int]).
    std::map<std::string, std::pair<std::string, std::vector<TypeRef>>> instances;
    // Generic method instances ("fold$string" -> ("fold", [string])), for diagnostics.
    std::map<std::string, std::pair<std::string, std::vector<TypeRef>>> methodInstances;
};

// `use` declarations, per namespace ("" = the global scope):
//   use std.collections.*                 every name in a namespace
//   use std.collections.HashMap           one name (as HashMap)
//   use std.collections.HashMap as Dict   one name under another name
//   use std.collections as col            a namespace alias: col.HashMap
// `::` may be written for `.` (use std::collections::*).
struct UseEntry {
    std::string target; // fully qualified name or namespace
    std::string alias;  // the name it is visible as ("" for a glob)
    bool glob = false;
};
struct UseTable {
    std::map<std::string, std::vector<UseEntry>> byNamespace;
    bool empty() const { return byNamespace.empty(); }
};

// The fully qualified names `name` (as written in namespace `ns`) may refer to,
// in lookup order: at each enclosing level from `ns` outwards, that level's own
// declarations come first, then the names its `use` declarations bring in. So a
// library's code always finds its own names before anything a program defines
// globally, and a program's own global names win over imported ones.
std::vector<std::string> nameCandidates(const std::string &name, const std::string &ns,
                                        const UseTable &uses);

struct Program {
    std::vector<DeclPtr> decls;
    GenericTables generics;
    UseTable uses;
};

// Calls `f` on every expression in the program (pre-order, including lambda
// bodies, initializers of globals and static fields).
void visitExprs(Program &program, const std::function<void(Expr &)> &f);
// The same walk over one block.
void visitBlockExprs(Block &block, const std::function<void(Expr &)> &f);

// Calls `f` on every expression slot so it can replace the expression. Pre-order:
// after `f` returns, the (possibly new) expression's children are visited.
// `switch`/`match` case values are skipped (they may be patterns).
void rewriteExprs(Program &program, const std::function<void(ExprPtr &)> &f);
// The same, over one statement.
void rewriteStmtExprs(Stmt &stmt, const std::function<void(ExprPtr &)> &f);
// The same, over one expression (its slot first, then its children).
void rewriteExpr(ExprPtr &slot, const std::function<void(ExprPtr &)> &f);

} // namespace grav

#endif // GRAV_AST_H
