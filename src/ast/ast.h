#ifndef GRAV_AST_H
#define GRAV_AST_H

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
};

// `self` (or `this`) inside a method body.
struct SelfExpr : Expr {};

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    BitAnd, BitOr, BitXor, Shl, Shr,
    Eq, NotEq, Greater, Less, GreaterEq, LessEq,
    And, Or,
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
struct IndexExpr : Expr {
    ExprPtr base;
    ExprPtr index;
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
};
struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<TypeRef> typeArgs; // turbofish generic args: `id::<int>(x)`
    std::vector<ExprPtr> args;

    CallKind kind = CallKind::Unresolved;
    std::string targetName;  // FQ free function / static method / builtin name
    std::string methodName;  // for instance/static methods
    std::string ownerClass;  // FQ class (or interface, if ifaceDispatch) of the method
    std::string slotOwner;   // FQ class that introduced the vtable slot
    bool ifaceDispatch = false; // dispatch through an interface itable (fat pointer)
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
};

struct AssignStmt : Stmt {
    ExprPtr target; // NameExpr or MemberExpr (lvalue)
    ExprPtr value;
    // Compound assignment (`x += y`): when set, the C output becomes
    // `target <op>= value`. `op` is one of Add/Sub/Mul/Div/Mod.
    bool isCompound = false;
    BinaryOp compoundOp = BinaryOp::Add;
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
};

struct DoWhileStmt : Stmt {
    Block body;
    ExprPtr cond;
};

// switch / match: each case matches one or more constant values; no fall-through.
struct SwitchCase {
    std::vector<ExprPtr> values;
    Block body;
};
struct SwitchStmt : Stmt {
    ExprPtr subject;
    std::vector<SwitchCase> cases;
    bool hasDefault = false;
    Block defaultBody;
};

// C-style: for (init; cond; update) { body }. Any of init/cond/update may be null.
struct ForStmt : Stmt {
    StmtPtr init;   // LetStmt / AssignStmt / ExprStmt or null
    ExprPtr cond;   // or null (treated as true)
    StmtPtr update; // AssignStmt / ExprStmt or null
    Block body;
};

// `for (i in lo..hi) { ... }` — i takes lo, lo+1, …, hi-1 (`..`) or hi (`..=`).
struct ForInStmt : Stmt {
    std::string var;
    ExprPtr lo;
    ExprPtr hi;
    bool inclusive = false; // `..=` includes hi
    Block body;
};

struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};

// `throw expr;` — raises a class instance as an exception.
struct ThrowStmt : Stmt {
    ExprPtr value;
};

// `try { ... } catch (e: Type) { ... } [finally { ... }]`.
struct TryStmt : Stmt {
    Block tryBlock;
    bool hasCatch = false;
    std::string catchVar;
    TypeRef catchType;     // a class type
    Block catchBlock;
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
};

struct FieldDecl {
    Access access = Access::Public;
    bool isReadonly = false;
    std::string name;
    TypeRef type;
    int line = 0, col = 0;
};

struct MethodDecl {
    Access access = Access::Public;
    bool isStatic = false;
    bool isAbstract = false;
    std::string name;
    std::vector<Param> params;
    TypeRef returnType; // Void if omitted
    Block body;
    bool hasBody = true;
    int line = 0, col = 0;
};

struct ConstructorDecl {
    std::vector<Param> params;
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
    std::string baseName;      // as written, empty if none
    std::vector<TypeRef> baseArgs; // generic args on the base, e.g. extends Stack<T>
    std::vector<std::string> interfaceNames;
    std::vector<std::vector<TypeRef>> interfaceArgs; // parallel to interfaceNames
    // `uses field: Type` — composition: `field` is a delegate the class forwards
    // the public methods of `Type` to (no `is-a` subtyping).
    std::vector<Param> delegates;
    std::vector<FieldDecl> fields;
    std::vector<StaticFieldDecl> staticFields;
    ConstructorDecl constructor;
    std::vector<MethodDecl> methods;
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
};

struct FunctionDecl : Decl {
    std::string name;
    std::vector<std::string> typeParams; // generic params, e.g. <T>
    std::vector<std::string> typeParamBounds; // optional `T: Bound` (parallel; "" if none)
    bool isAsync = false; // callers receive a Future<returnType>
    std::vector<Param> params;
    TypeRef returnType;
    Block body;
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
};
struct EnumDecl : Decl {
    std::string name;
    std::vector<EnumMember> members;
};

// `type Name = T;` — a transparent alias for an existing type. Aliases are fully
// expanded to their canonical target during symbol resolution, so they leave no
// trace in the generated C.
struct TypeAliasDecl : Decl {
    std::string name;
    std::vector<std::string> typeParams; // generic params, e.g. type Vec<T> = T[3]
    TypeRef target; // as written; canonicalized by the registry
};

// A top-level `%{ ... %}` block: verbatim C emitted into the translation unit
// (for #includes, globals, and helper functions/macros).
struct CBlockDecl : Decl {
    std::string code;
};

struct Program {
    std::vector<DeclPtr> decls;
};

} // namespace grav

#endif // GRAV_AST_H
