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

// A bare name: a local variable, parameter, or the leading segment of a
// qualified reference (class / namespace) that the checker resolves later.
struct NameExpr : Expr {
    std::string name;
};

// `self` (or `this`) inside a method body.
struct SelfExpr : Expr {};

enum class BinaryOp {
    Add, Sub, Mul, Div,
    Eq, NotEq, Greater, Less, GreaterEq, LessEq,
    And, Or,
};
const char *binaryOpSymbol(BinaryOp op);
bool isComparison(BinaryOp op);
bool isLogical(BinaryOp op);

struct BinaryExpr : Expr {
    BinaryOp op;
    ExprPtr left;
    ExprPtr right;
    bool stringConcat = false; // set by checker: '+' on two strings
};

// Prefix unary: '!' (logical not).
enum class UnaryOp { Not };
struct UnaryExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
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

// `object.member`. Used both as a value (field read) and as the callee of a
// call (method / static method). The checker tags how it resolved.
enum class MemberKind {
    Unresolved, InstanceField, StaticTarget, NamespaceTarget, MethodRef
};
struct MemberExpr : Expr {
    ExprPtr object;
    std::string member;
    MemberKind kind = MemberKind::Unresolved;
    // For InstanceField / MethodRef: fully-qualified owning class.
    std::string ownerClass;
    // For StaticTarget / NamespaceTarget: the resolved qualified prefix.
    std::string qualified;
};

// `new ClassName(args)`.
struct NewExpr : Expr {
    std::string className;   // as written; checker rewrites to FQ name
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

struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};

// ===========================================================================
// Declarations
// ===========================================================================

enum class Access { Public, Private, Protected };
const char *accessName(Access a);

struct Param {
    std::string name;
    TypeRef type;
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
    virtual ~Decl() = default;
};
using DeclPtr = std::unique_ptr<Decl>;

struct ClassDecl : Decl {
    std::string name;          // simple name
    bool isAbstract = false;
    std::string baseName;      // as written, empty if none
    std::vector<std::string> interfaceNames;
    std::vector<FieldDecl> fields;
    ConstructorDecl constructor;
    std::vector<MethodDecl> methods;
};

struct InterfaceDecl : Decl {
    std::string name;
    std::vector<MethodDecl> methods; // signatures (hasBody == false)
};

// A plain data type: named fields, value semantics, no methods/vtable/RTTI.
struct StructDecl : Decl {
    std::string name;
    std::vector<FieldDecl> fields;
};

struct FunctionDecl : Decl {
    std::string name;
    std::vector<Param> params;
    TypeRef returnType;
    Block body;
};

struct Program {
    std::vector<DeclPtr> decls;
};

} // namespace grav

#endif // GRAV_AST_H
