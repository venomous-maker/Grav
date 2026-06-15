#include "codegen/codegen.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

#include "codegen/mangle.h"

namespace grav {

std::string CodeGen::generate(const Program &program, const Registry &reg) {
    reg_ = &reg;
    program_ = &program;

    emitPrelude();
    emitStructs();
    emitVTableTypes();
    emitPrototypes(program);
    emitVTableInstances();
    emitTypeInfos();
    emitInterfaceTables();
    emitDefinitions(program);
    emitMainWrapper();

    return typedefs_ + structs_ + vtableTypes_ + protos_ + vtables_ + defs_;
}

void CodeGen::emitPrelude() {
    typedefs_ +=
        "/* Generated from Grav source by gravc. Do not edit. */\n"
        "#include <stdbool.h>\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n\n"
        "typedef struct GravTypeInfo {\n"
        "    const char* name;\n"
        "    const struct GravTypeInfo* base;\n"
        "} GravTypeInfo;\n\n"
        "/* Common object header: vtable pointer + runtime type descriptor. */\n"
        "struct GravObject { const void* __vt; const GravTypeInfo* __type; };\n\n"
        "/* An interface value: the object plus its per-class method table. */\n"
        "struct GravIface { void* obj; const void* itab; };\n\n"
        "static const char* grav_str_concat(const char* a, const char* b) {\n"
        "    size_t la = strlen(a), lb = strlen(b);\n"
        "    char* r = (char*)malloc(la + lb + 1);\n"
        "    memcpy(r, a, la);\n"
        "    memcpy(r + la, b, lb + 1);\n"
        "    return r;\n"
        "}\n\n"
        "static const char* grav_typename(const void* obj) {\n"
        "    return ((const struct GravObject*)obj)->__type->name;\n"
        "}\n\n"
        "static bool grav_is_instance(const void* obj, const GravTypeInfo* want) {\n"
        "    const GravTypeInfo* t = ((const struct GravObject*)obj)->__type;\n"
        "    while (t) { if (t == want) return true; t = t->base; }\n"
        "    return false;\n"
        "}\n\n";
}

std::string CodeGen::cTy(const TypeRef &t) const {
    if (t.isNamed() && reg_->isInterface(t.name)) return "struct GravIface";
    // Structs are value types: spelled as the bare struct, never a pointer.
    if (t.isNamed() && reg_->isStruct(t.name)) return structName(t.name);
    return cType(t);
}

std::vector<FieldInfo> CodeGen::collectFields(const std::string &classFq) const {
    std::vector<FieldInfo> out;
    const ClassInfo *ci = reg_->cls(classFq);
    if (!ci) return out;
    if (!ci->baseClass.empty()) out = collectFields(ci->baseClass);
    for (const auto &f : ci->fields) out.push_back(f);
    return out;
}

void CodeGen::emitStructs() {
    // Forward typedefs so class/struct names can appear anywhere. Interfaces have
    // no storage of their own, so they alias the common object header.
    for (const auto &declPtr : program_->decls) {
        if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            std::string s = structName(c->fqName);
            typedefs_ += "typedef struct " + s + " " + s + ";\n";
        } else if (auto *st = dynamic_cast<const StructDecl *>(declPtr.get())) {
            std::string s = structName(st->fqName);
            typedefs_ += "typedef struct " + s + " " + s + ";\n";
        }
        // Interfaces compile to the GravIface fat-pointer value (no own struct).
    }
    typedefs_ += "\n";

    // Value structs first: a struct with a struct-typed field needs that field's
    // type fully defined (it is stored by value), so emit them dependency-ordered.
    emitValueStructs();

    for (const auto &declPtr : program_->decls) {
        if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) {
            emitStruct(c->fqName);
        }
    }
}

void CodeGen::emitValueStructs() {
    std::unordered_set<std::string> done;
    std::function<void(const std::string &)> emit = [&](const std::string &fq) {
        if (done.count(fq)) return;
        done.insert(fq);
        const StructInfo *si = reg_->strct(fq);
        if (!si) return;
        // Emit struct-typed (by-value) dependencies first.
        for (const auto &f : si->fields)
            if (f.type.isNamed() && reg_->isStruct(f.type.name)) emit(f.type.name);
        structs_ += "struct " + structName(fq) + " {\n";
        if (si->fields.empty())
            structs_ += "    char __empty; /* C has no zero-size structs */\n";
        for (const auto &f : si->fields)
            structs_ += "    " + cTy(f.type) + " " + f.name + ";\n";
        structs_ += "};\n\n";
    };
    for (const auto &declPtr : program_->decls)
        if (auto *st = dynamic_cast<const StructDecl *>(declPtr.get())) emit(st->fqName);
}

void CodeGen::emitStruct(const std::string &classFq) {
    std::string s = structName(classFq);
    structs_ += "struct " + s + " {\n";
    structs_ += "    const void* __vt;\n";
    structs_ += "    const GravTypeInfo* __type;\n";
    for (const auto &f : collectFields(classFq)) {
        structs_ += "    " + cTy(f.type) + " " + f.name + ";\n";
    }
    structs_ += "};\n\n";
}

std::string CodeGen::slotFnType(const VTableSlot &slot) const {
    const MethodInfo *mi = reg_->findMethod(slot.slotOwner, slot.name);
    std::string ret = mi ? cTy(mi->returnType) : "void";
    std::string params = structName(slot.slotOwner) + "*";
    if (mi) {
        for (const auto &pt : mi->paramTypes) params += ", " + cTy(pt);
    }
    return ret + " (*)(" + params + ")";
}

void CodeGen::emitVTableTypes() {
    for (const auto &declPtr : program_->decls) {
        auto *c = dynamic_cast<const ClassDecl *>(declPtr.get());
        if (!c) continue;
        const ClassInfo *ci = reg_->cls(c->fqName);
        if (ci) emitVTableType(*ci);
    }
}

void CodeGen::emitVTableType(const ClassInfo &ci) {
    if (ci.slots.empty()) return;
    std::string vt = vtableType(ci.fqName);
    vtableTypes_ += "typedef struct " + vt + " {\n";
    for (const auto &slot : ci.slots) {
        std::string sig = slotFnType(slot); // "RET (*)(params)"
        std::string member = sig;
        auto pos = member.find("(*)");
        if (pos != std::string::npos) member.replace(pos, 3, "(*" + slot.name + ")");
        vtableTypes_ += "    " + member + ";\n";
    }
    vtableTypes_ += "} " + vt + ";\n\n";
}

void CodeGen::emitVTableInstances() {
    for (const auto &declPtr : program_->decls) {
        auto *c = dynamic_cast<const ClassDecl *>(declPtr.get());
        if (!c) continue;
        const ClassInfo *ci = reg_->cls(c->fqName);
        if (ci) emitVTableInstance(*ci);
    }
}

void CodeGen::emitTypeInfos() {
    std::unordered_set<std::string> done;
    std::function<void(const std::string &)> emit = [&](const std::string &fq) {
        if (done.count(fq)) return;
        done.insert(fq);
        const ClassInfo *ci = reg_->cls(fq);
        if (!ci) return;
        std::string base = "0";
        if (!ci->baseClass.empty()) {
            emit(ci->baseClass); // base descriptor must precede derived
            base = "&" + mangle(ci->baseClass) + "_typeinfo";
        }
        vtables_ += "static const GravTypeInfo " + mangle(fq) +
                    "_typeinfo = { \"" + fq + "\", " + base + " };\n";
    };
    for (const auto &declPtr : program_->decls) {
        if (auto *c = dynamic_cast<const ClassDecl *>(declPtr.get())) emit(c->fqName);
    }
    vtables_ += "\n";
}

void CodeGen::emitInterfaceTables() {
    auto itabType = [](const std::string &i) { return mangle(i) + "_ITAB"; };

    // One method-table struct type per interface.
    for (const auto &declPtr : program_->decls) {
        auto *id = dynamic_cast<const InterfaceDecl *>(declPtr.get());
        if (!id) continue;
        const InterfaceInfo *ii = reg_->iface(id->fqName);
        if (!ii) continue;
        vtableTypes_ += "typedef struct " + itabType(id->fqName) + " {\n";
        for (const auto &m : ii->methods) {
            std::string params = "void*";
            for (const auto &pt : m.paramTypes) params += ", " + cTy(pt);
            vtableTypes_ += "    " + cTy(m.returnType) + " (*" + m.name + ")(" +
                            params + ");\n";
        }
        vtableTypes_ += "} " + itabType(id->fqName) + ";\n\n";
    }

    // One table instance per (class, interface-it-implements) pair.
    for (const auto &declPtr : program_->decls) {
        auto *c = dynamic_cast<const ClassDecl *>(declPtr.get());
        if (!c) continue;
        // Collect interfaces from the whole inheritance chain.
        std::vector<std::string> ifaces;
        std::string cur = c->fqName;
        int guard = 0;
        while (!cur.empty() && guard++ < 100) {
            const ClassInfo *ci = reg_->cls(cur);
            if (!ci) break;
            for (const auto &i : ci->interfaces)
                if (!i.empty() &&
                    std::find(ifaces.begin(), ifaces.end(), i) == ifaces.end())
                    ifaces.push_back(i);
            cur = ci->baseClass;
        }
        for (const auto &ifq : ifaces) {
            const InterfaceInfo *ii = reg_->iface(ifq);
            if (!ii) continue;
            vtables_ += "static " + itabType(ifq) + " " + mangle(c->fqName) +
                        "__itab__" + mangle(ifq) + " = {\n";
            for (const auto &m : ii->methods) {
                std::string impl = reg_->findMethodImpl(c->fqName, m.name);
                std::string sig = cTy(m.returnType) + " (*)(void*";
                for (const auto &pt : m.paramTypes) sig += ", " + cTy(pt);
                sig += ")";
                if (impl.empty())
                    vtables_ += "    0,\n";
                else
                    vtables_ += "    (" + sig + ")" + methodCName(impl, m.name) + ",\n";
            }
            vtables_ += "};\n\n";
        }
    }
}

void CodeGen::emitVTableInstance(const ClassInfo &ci) {
    if (ci.slots.empty()) return;
    vtables_ += "static " + vtableType(ci.fqName) + " " +
                vtableInstance(ci.fqName) + " = {\n";
    for (const auto &slot : ci.slots) {
        std::string impl = reg_->findMethodImpl(ci.fqName, slot.name);
        if (impl.empty()) {
            vtables_ += "    0,\n";
        } else {
            vtables_ += "    (" + slotFnType(slot) + ")" +
                        methodCName(impl, slot.name) + ",\n";
        }
    }
    vtables_ += "};\n\n";
}

} // namespace grav
