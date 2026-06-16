#include "codegen/mangle.h"

namespace grav {

std::string mangle(const std::string &fqName) {
    std::string out;
    for (char c : fqName) {
        if (c == '.') out += "__";
        else out += c;
    }
    return out;
}

std::string structName(const std::string &classFq) { return mangle(classFq); }
std::string vtableType(const std::string &classFq) { return mangle(classFq) + "_VT"; }
std::string vtableInstance(const std::string &classFq) { return mangle(classFq) + "_vtable"; }

std::string methodCName(const std::string &classFq, const std::string &method) {
    return mangle(classFq) + "_m_" + method;
}

std::string ctorCName(const std::string &classFq) { return mangle(classFq) + "_new"; }

std::string funcCName(const std::string &fnFq) { return "vf_" + mangle(fnFq); }

std::string enumConst(const std::string &enumFq, const std::string &member) {
    return mangle(enumFq) + "_" + member;
}

static std::string arrayTag(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: return "int";
        case TypeRef::Kind::Float: return "flt";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "str";
        case TypeRef::Kind::Void: return "void";
        case TypeRef::Kind::Named: return mangle(t.name);
        case TypeRef::Kind::Pointer: return "ptr_" + (t.elem ? arrayTag(*t.elem) : "void");
        case TypeRef::Kind::Array:
            return "arr" + std::to_string(t.arrayLen) + "_" +
                   (t.elem ? arrayTag(*t.elem) : "void");
        default: return "x";
    }
}

std::string arrayStructName(const TypeRef &arrayType) {
    return "GravArr_" + (arrayType.elem ? arrayTag(*arrayType.elem) : "void") + "_" +
           std::to_string(arrayType.arrayLen);
}

std::string cType(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: return "int";
        case TypeRef::Kind::Float: return "double";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "const char*";
        case TypeRef::Kind::Void: return "void";
        case TypeRef::Kind::Named: return structName(t.name) + "*";
        case TypeRef::Kind::Null: return "void*";
        case TypeRef::Kind::Future: return "void* /*future*/";
        case TypeRef::Kind::Pointer:
            return (t.elem ? cType(*t.elem) : "void") + "*";
        case TypeRef::Kind::Array: return arrayStructName(t);
        case TypeRef::Kind::Error: return "int /*error*/";
    }
    return "int";
}

} // namespace grav
