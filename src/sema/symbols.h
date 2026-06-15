#ifndef GRAV_SEMA_SYMBOLS_H
#define GRAV_SEMA_SYMBOLS_H

#include <string>
#include <unordered_map>
#include <vector>

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "common/types.h"

namespace grav {

struct FieldInfo {
    Access access;
    bool isReadonly;
    std::string name;
    TypeRef type;            // canonical
    std::string definingClass; // FQ
};

enum class AccessorKind { None, Getter, Setter };

struct MethodInfo {
    std::string name;
    Access access = Access::Public;
    bool isStatic = false;
    bool isAbstract = false;         // declared 'abstract' (no body, must override)
    std::vector<TypeRef> paramTypes; // canonical
    std::vector<std::string> paramNames;
    TypeRef returnType;              // canonical
    std::string definingClass;      // FQ
    MethodDecl *decl = nullptr;      // AST node (for body); null for interface/synthetic
    bool hasBody = false;
    // Synthetic accessor info (auto-generated getter/setter for a private field).
    AccessorKind accessor = AccessorKind::None;
    std::string accessorField;       // the field this accessor reads/writes
};

// One virtual-dispatch slot in a class hierarchy. `slotOwner` is the class that
// first introduced the method name; the slot keeps its position across the whole
// hierarchy so a base pointer's vtable view stays layout-compatible.
struct VTableSlot {
    std::string name;
    std::string slotOwner; // FQ class that introduced the slot
};

struct ClassInfo {
    std::string fqName;
    bool isAbstract = false;
    std::string baseClass;                 // FQ, empty if none
    std::vector<std::string> interfaces;   // FQ
    std::vector<FieldInfo> fields;         // own fields only (declared order)
    std::vector<MethodInfo> methods;       // own methods only
    ConstructorDecl *constructor = nullptr;
    bool hasConstructor = false;
    std::vector<VTableSlot> slots;         // full hierarchy, base-first
    ClassDecl *decl = nullptr;
};

struct InterfaceInfo {
    std::string fqName;
    std::vector<MethodInfo> methods; // signatures
    InterfaceDecl *decl = nullptr;
};

// A plain value type: just an ordered list of fields.
struct StructInfo {
    std::string fqName;
    std::vector<FieldInfo> fields; // declared order
    StructDecl *decl = nullptr;
};

struct FunctionInfo {
    std::string fqName;
    std::vector<TypeRef> paramTypes;
    std::vector<std::string> paramNames;
    TypeRef returnType;
    FunctionDecl *decl = nullptr;
};

// Owns the symbol tables and performs name/type resolution. Built in two passes
// (register, then canonicalize) before type checking begins.
class Registry {
public:
    // Returns accumulated errors (also queryable via errors()).
    const std::vector<GravError> &build(Program &program);
    const std::vector<GravError> &errors() const { return errors_; }

    bool isClass(const std::string &fq) const { return classes_.count(fq) != 0; }
    bool isInterface(const std::string &fq) const { return interfaces_.count(fq) != 0; }
    bool isStruct(const std::string &fq) const { return structs_.count(fq) != 0; }
    bool isType(const std::string &fq) const {
        return isClass(fq) || isInterface(fq) || isStruct(fq);
    }

    const ClassInfo *cls(const std::string &fq) const;
    const InterfaceInfo *iface(const std::string &fq) const;
    const StructInfo *strct(const std::string &fq) const;
    const FunctionInfo *func(const std::string &fq) const;

    // Resolve a (possibly dotted, possibly unqualified) class/interface name in
    // a namespace context to its FQ name. Returns "" if not found.
    std::string resolveType(const std::string &name, const std::string &nsContext) const;
    // Resolve a free function name. Returns "" if not found.
    std::string resolveFunc(const std::string &name, const std::string &nsContext) const;
    // Is `name` (in nsContext) a known namespace prefix?
    std::string resolveNamespace(const std::string &name, const std::string &nsContext) const;

    // Field lookup on a struct type (structs have no inheritance).
    const FieldInfo *findStructField(const std::string &structFq,
                                     const std::string &name) const;

    // Field / method lookup that walks the inheritance chain.
    const FieldInfo *findField(const std::string &classFq, const std::string &name) const;
    const MethodInfo *findMethod(const std::string &classFq, const std::string &name) const;
    const MethodInfo *findInterfaceMethod(const std::string &ifaceFq,
                                          const std::string &name) const;
    // Most-derived class (at or above classFq) that defines `method`.
    std::string findMethodImpl(const std::string &classFq, const std::string &name) const;

    // Is `sub` the same as or a subclass of `base`?
    bool isSubclass(const std::string &sub, const std::string &base) const;
    // Does class `classFq` conform to interface `ifaceFq`?
    bool classImplements(const std::string &classFq, const std::string &ifaceFq) const;

    // Namespace prefix (FQ minus final segment); "" when top-level.
    static std::string namespaceOf(const std::string &fqName);

private:
    void registerDecls(Program &program);
    void canonicalize();
    void synthesizeAccessors();
    TypeRef canonType(const TypeRef &t, const std::string &nsContext, int line, int col);
    void computeSlots();
    void checkHierarchies();
    void checkStructCycles();
    void error(int line, int col, const std::string &msg);

    std::unordered_map<std::string, ClassInfo> classes_;
    std::unordered_map<std::string, InterfaceInfo> interfaces_;
    std::unordered_map<std::string, StructInfo> structs_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    std::vector<std::string> namespaces_; // known namespace prefixes
    std::vector<GravError> errors_;
};

} // namespace grav

#endif // GRAV_SEMA_SYMBOLS_H
