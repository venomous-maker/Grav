#ifndef GRAV_SEMA_TYPECHECKER_H
#define GRAV_SEMA_TYPECHECKER_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "sema/symbols.h"

namespace grav {

// Type-checks statement/expression bodies against a built Registry, enforcing
// strong typing, access control and readonly rules, and annotating the AST with
// resolved types and call/member resolutions for the code generator.
// Implemented across typechecker.cpp (driver + statements) and
// typechecker_expr.cpp (expressions).
class TypeChecker {
public:
    const std::vector<GravError> &check(Program &program, const Registry &reg);
    const std::vector<GravError> &errors() const { return errors_; }

private:
    // driver (typechecker.cpp)
    void checkFunction(FunctionDecl &fn);
    void checkClass(ClassDecl &cls);
    void checkBlock(Block &block);
    void checkStmt(Stmt &stmt);
    void checkGlobal(GlobalVarDecl &g);
    void checkStaticFields(ClassDecl &cls);
    // Resolves `Class.field` to a static field if it is one; returns its type, or
    // nullopt to fall through to ordinary member handling.
    std::optional<TypeRef> tryStaticField(MemberExpr &e);
    void checkLet(LetStmt &s);
    void checkAssign(AssignStmt &s);
    // Validates the operator of a compound assignment (`x += y`) against the
    // resolved target/value types.
    void checkCompound(const AssignStmt &s, const TypeRef &targetType,
                       const TypeRef &valueType);
    void checkReturn(ReturnStmt &s);
    void checkIf(IfStmt &s);
    void checkWhile(WhileStmt &s);
    void checkDoWhile(DoWhileStmt &s);
    void checkFor(ForStmt &s);
    void checkSwitch(SwitchStmt &s);
    void checkForIn(ForInStmt &s);
    void checkThrow(ThrowStmt &s);
    void checkTry(TryStmt &s);
    void checkDefer(DeferStmt &s);
    void requireBool(Expr &cond, const char *ctx);
    // Errors when a non-void body can fall off its end without returning.
    void checkFallthrough(const Block &body, const TypeRef &ret, int line, int col,
                          const std::string &what);

    // typechecker_lambda.cpp — lambdas, captures, function values.
    TypeRef checkLambda(LambdaExpr &e);
    // checkExpr with an expected type, used to infer untyped lambda parameters.
    TypeRef checkExprExpect(Expr &e, const TypeRef &expected);
    // Records that local `name` (declared in scope `scopeIdx`) is used inside
    // the lambdas nested deeper than it; returns true if it was captured.
    bool noteCapture(const std::string &name, const TypeRef &type, size_t scopeIdx);
    // Is local `name` captured by the innermost lambda (so it may not be assigned)?
    bool isCapturedHere(const std::string &name);
    // A free function named in value position: its type as a function value.
    std::optional<TypeRef> tryFunctionRef(NameExpr &e);
    // `f(args)` where f evaluates to a function value.
    TypeRef checkFnValueCall(CallExpr &e, const TypeRef &fnType);

    // typechecker_ops.cpp — operator overloading, element loops, printing.
    // Picks among same-named method overloads for a call (exact match first, then
    // a unique assignable one) and checks the arguments. Reports and returns null
    // when nothing matches. `cands` must already be filtered to static/instance.
    const MethodInfo *resolveMethodOverload(const std::vector<const MethodInfo *> &cands,
                                            std::vector<ExprPtr> &args, int line, int col,
                                            const std::string &what);
    // Chooses a constructor overload of `classFq` for `args` (checks them); returns
    // its index, or -1 after reporting an error.
    int resolveCtor(const std::string &classFq, std::vector<ExprPtr> &args, int line, int col,
                    const std::string &what);
    // Overload choice for a single right-hand operand / index (no expressions).
    const MethodInfo *pickByOperand(const std::vector<const MethodInfo *> &cands,
                                    const TypeRef &operand) const;
    // A class operator method (`fn operator+`) implementing `e`, if any.
    std::optional<TypeRef> tryOperatorOverload(BinaryExpr &e, const TypeRef &lt,
                                               const TypeRef &rt);
    // `obj[i]` on a class with `operator[]`; nullopt when `base` is not such a class.
    std::optional<TypeRef> tryIndexOverload(IndexExpr &e, const TypeRef &base);
    // Values print/str/interpolation can render: primitives, enums (member name),
    // and classes/interfaces with `toString() -> string`.
    bool isDisplayable(const TypeRef &t) const;

    // expressions (typechecker_expr.cpp)
    TypeRef checkExpr(Expr &expr);
    TypeRef checkBinary(BinaryExpr &e);
    TypeRef checkUnary(UnaryExpr &e);
    TypeRef checkTernary(TernaryExpr &e);
    TypeRef checkAs(AsExpr &e);
    TypeRef checkIs(IsExpr &e);
    TypeRef checkAwait(AwaitExpr &e);
    TypeRef checkAddrOf(AddrOfExpr &e);
    TypeRef checkDeref(DerefExpr &e);
    TypeRef checkCoalesce(CoalesceExpr &e);
    // Resolves a `Type.Member` reference to an enum constant, if it is one.
    // Returns the enum type on success, or nullopt to fall through to normal
    // member handling.
    std::optional<TypeRef> tryEnumValue(MemberExpr &e);
    TypeRef checkIncDec(IncDecExpr &e);
    TypeRef checkCast(CastExpr &e);
    TypeRef checkSizeof(SizeofExpr &e);
    TypeRef checkArrayLiteral(ArrayLiteralExpr &e);
    TypeRef checkIndex(IndexExpr &e);
    TypeRef checkNew(NewExpr &e);
    TypeRef checkStructLiteral(StructLiteralExpr &e);
    TypeRef checkCall(CallExpr &e);
    std::optional<TypeRef> checkSuperCall(CallExpr &e);
    TypeRef resolveFreeCall(CallExpr &e, const std::string &fnFq);
    const FunctionInfo *pickOverload(const std::vector<FunctionInfo> &cands,
                                     const std::vector<TypeRef> &argTypes) const;
    void validateArgTypes(const std::vector<ExprPtr> &args,
                          const std::vector<TypeRef> &argTypes,
                          const std::vector<TypeRef> &params, bool variadic,
                          int line, int col, const std::string &what);
    TypeRef checkMember(MemberExpr &e); // value position (field read)
    bool isLvalue(const Expr &e) const;
    // `?.` lowers to a null guard returning a zero sentinel, so its result must be
    // a scalar/reference/enum (not a value-type struct or interface fat pointer).
    void checkOptionalResult(int line, int col, const TypeRef &t, bool allowVoid);

    // helpers
    void error(int line, int col, const std::string &msg);
    void warn(int line, int col, const std::string &msg);
    bool isAssignable(const TypeRef &from, const TypeRef &to) const;
    static TypeRef numericPromote(const TypeRef &a, const TypeRef &b);
    bool checkAccess(Access a, const std::string &definingClass) const;
    void pushScope();
    void popScope(); // emits "unused variable" warnings for the scope
    void declareLocal(const std::string &name, const TypeRef &type, bool isParam,
                      bool isConst, int line, int col);
    std::optional<std::vector<std::string>> flattenNames(const Expr *e) const;
    void checkArgs(const std::vector<ExprPtr> &args,
                   const std::vector<TypeRef> &params, int line, int col,
                   const std::string &what);
    // Like checkArgs but the last `params` entry is a variadic element type that
    // accepts zero or more trailing arguments.
    void checkVariadicArgs(const std::vector<ExprPtr> &args,
                           const std::vector<TypeRef> &params, int line, int col,
                           const std::string &what);

    // A local binding tracked for type, const-ness, and use (for warnings).
public:
    struct LocalVar {
        TypeRef type;
        bool used = false;
        bool isParam = false;
        bool isConst = false;
        int line = 0, col = 0;
        std::string name;
    };
    LocalVar *lookupLocal(const std::string &name);
    LocalVar *lookupLocalAt(const std::string &name, size_t &scopeIdx);
private:

    const Registry *reg_ = nullptr;
    std::vector<GravError> errors_;
    std::vector<GravError> warnings_;
    std::vector<std::unordered_map<std::string, LocalVar>> scopes_;

    std::string currentClass_; // FQ, empty in a free function
    std::string currentNs_;    // namespace context
    TypeRef currentReturn_;
    bool inConstructor_ = false;
    bool inStatic_ = false;
    bool inAsync_ = false; // true while checking an `async fn` body
    int loopDepth_ = 0; // for validating break/continue
    bool inDefer_ = false; // checking a `defer` body (no `return` allowed)
    struct LambdaCtx { LambdaExpr *lam; size_t scopeBase; };
    std::vector<LambdaCtx> lambdaStack_; // enclosing lambdas, innermost last
    const TypeRef *expected_ = nullptr;  // expected type for the next lambda

public:
    const std::vector<GravError> &warnings() const { return warnings_; }
};

} // namespace grav

#endif // GRAV_SEMA_TYPECHECKER_H
