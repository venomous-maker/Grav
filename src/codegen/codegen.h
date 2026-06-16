#ifndef GRAV_CODEGEN_H
#define GRAV_CODEGEN_H

#include <map>
#include <string>
#include <vector>

#include "ast/ast.h"
#include "sema/symbols.h"

namespace grav {

// Translates a type-checked Grav program into a self-contained C translation
// unit. Classes become structs with a leading vtable pointer; single
// inheritance flattens base fields; virtual methods dispatch through per-class
// vtables. Implemented across codegen_types.cpp (structs/vtables), codegen_func.cpp
// (functions/methods/main), codegen_stmt.cpp and codegen_expr.cpp.
class CodeGen {
public:
    std::string generate(const Program &program, const Registry &reg);

private:
    // codegen_types.cpp
    void emitPrelude();
    void emitEnums();
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
    void emitVTableTypes();
    void emitVTableType(const ClassInfo &ci);
    void emitVTableInstances();
    void emitVTableInstance(const ClassInfo &ci);
    void emitTypeInfos();
    void emitInterfaceTables();   // itable types + per-class instances
    // C type spelling, interface-aware (interfaces become a fat pointer struct).
    std::string cTy(const TypeRef &t) const;
    std::vector<FieldInfo> collectFields(const std::string &classFq) const;
    std::string slotFnType(const VTableSlot &slot) const;

    // codegen_func.cpp
    void emitPrototypes(const Program &program);
    void emitDefinitions(const Program &program);
    void emitFunction(const FunctionDecl &fn);
    void emitClassBodies(const ClassDecl &cls);
    void emitMethod(const ClassDecl &cls, const MethodDecl &m);
    void emitConstructor(const ClassDecl &cls);
    std::string accessorSig(const std::string &classFq, const MethodInfo &m) const;
    void emitAccessorBody(const std::string &classFq, const MethodInfo &m);
    void emitMainWrapper();
    std::string paramList(const std::vector<Param> &params,
                          const std::string &selfStruct) const;

    // codegen_stmt.cpp
    void emitBlock(const Block &block);
    void emitStmt(const Stmt &stmt);
    void emitIf(const IfStmt &s);        // if / else-if / else chain
    std::string emitAssign(const AssignStmt &s) const; // plain or compound assignment
    void emitBraced(const Block &block); // "{\n …\n}" at the current indent
    std::string inlineSimple(const Stmt *s) const; // render a for-header clause
    int switchCounter_ = 0;

    // codegen_expr.cpp
    std::string emitExpr(const Expr &expr) const;
    std::string emitCall(const CallExpr &call) const;
    // Emits an expression, adding a C pointer cast when the value flows into a
    // named (class/interface) slot of a different static type (upcasts).
    std::string emitAs(const Expr &expr, const TypeRef &target) const;
    void emitArgs(std::string &out, const CallExpr &call,
                  const std::vector<TypeRef> &params, bool leadingSelf,
                  const std::string &self) const;
    static std::string escapeC(const std::string &s);

    const Registry *reg_ = nullptr;
    const Program *program_ = nullptr;

    // Fixed-length array types in use, keyed by their C struct name (deduped).
    std::map<std::string, TypeRef> arrayTypes_;

    // Output sections, concatenated in order by generate().
    std::string typedefs_;
    std::string structs_;
    std::string vtableTypes_;
    std::string protos_;
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
