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
    void checkLet(LetStmt &s);
    void checkAssign(AssignStmt &s);
    void checkReturn(ReturnStmt &s);
    void checkIf(IfStmt &s);
    void checkWhile(WhileStmt &s);
    void checkDoWhile(DoWhileStmt &s);
    void checkFor(ForStmt &s);
    void checkSwitch(SwitchStmt &s);
    void requireBool(Expr &cond, const char *ctx);

    // expressions (typechecker_expr.cpp)
    TypeRef checkExpr(Expr &expr);
    TypeRef checkBinary(BinaryExpr &e);
    TypeRef checkUnary(UnaryExpr &e);
    TypeRef checkIncDec(IncDecExpr &e);
    TypeRef checkCast(CastExpr &e);
    TypeRef checkNew(NewExpr &e);
    TypeRef checkStructLiteral(StructLiteralExpr &e);
    TypeRef checkCall(CallExpr &e);
    TypeRef checkMember(MemberExpr &e); // value position (field read)
    bool isLvalue(const Expr &e) const;

    // helpers
    void error(int line, int col, const std::string &msg);
    void warn(int line, int col, const std::string &msg);
    bool isAssignable(const TypeRef &from, const TypeRef &to) const;
    bool checkAccess(Access a, const std::string &definingClass) const;
    void pushScope();
    void popScope(); // emits "unused variable" warnings for the scope
    void declareLocal(const std::string &name, const TypeRef &type, bool isParam,
                      bool isConst, int line, int col);
    std::optional<std::vector<std::string>> flattenNames(const Expr *e) const;
    void checkArgs(const std::vector<ExprPtr> &args,
                   const std::vector<TypeRef> &params, int line, int col,
                   const std::string &what);

    // A local binding tracked for type, const-ness, and use (for warnings).
    struct LocalVar {
        TypeRef type;
        bool used = false;
        bool isParam = false;
        bool isConst = false;
        int line = 0, col = 0;
        std::string name;
    };
    LocalVar *lookupLocal(const std::string &name);

    const Registry *reg_ = nullptr;
    std::vector<GravError> errors_;
    std::vector<GravError> warnings_;
    std::vector<std::unordered_map<std::string, LocalVar>> scopes_;

    std::string currentClass_; // FQ, empty in a free function
    std::string currentNs_;    // namespace context
    TypeRef currentReturn_;
    bool inConstructor_ = false;
    bool inStatic_ = false;
    int loopDepth_ = 0; // for validating break/continue

public:
    const std::vector<GravError> &warnings() const { return warnings_; }
};

} // namespace grav

#endif // GRAV_SEMA_TYPECHECKER_H
