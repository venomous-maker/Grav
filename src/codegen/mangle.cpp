#include "codegen/mangle.h"
#include <unordered_set>

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
    return mangle(classFq) + "_m_" + memberCName(method);
}

std::string memberCName(const std::string &name) {
    // Method names that are C keywords would clash as struct members / function
    // suffixes; append an underscore to keep the generated C valid.
    static const std::unordered_set<std::string> kw = {
        "auto","break","case","char","const","continue","default","do","double",
        "else","enum","extern","float","for","goto","if","inline","int","long",
        "register","restrict","return","short","signed","sizeof","static","struct",
        "switch","typedef","union","unsigned","void","volatile","while","_Bool",
    };
    return kw.count(name) ? name + "_" : name;
}

std::string ctorCName(const std::string &classFq) { return mangle(classFq) + "_new"; }
std::string ctorInitCName(const std::string &classFq) { return mangle(classFq) + "_init"; }

std::string funcCName(const std::string &fnFq) { return "vf_" + mangle(fnFq); }
std::string funcCName(const std::string &fnFq, int overloadIndex) {
    return overloadIndex > 0 ? funcCName(fnFq) + "__ov" + std::to_string(overloadIndex)
                             : funcCName(fnFq);
}

std::string enumConst(const std::string &enumFq, const std::string &member) {
    return mangle(enumFq) + "_" + member;
}

static std::string arrayTag(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: {
            int b = t.numBits();
            if (!t.isUnsigned && b == 32) return "int"; // canonical, keep stable
            return (t.isUnsigned ? "u" : "i") + std::to_string(b);
        }
        case TypeRef::Kind::Float: return t.numBits() == 32 ? "f32" : "flt";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "str";
        case TypeRef::Kind::Binary: return "bytes";
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
        case TypeRef::Kind::Int: {
            int b = t.numBits();
            const char *u = t.isUnsigned ? "unsigned " : "";
            switch (b) {
                case 8:  return t.isUnsigned ? "unsigned char" : "signed char";
                case 16: return std::string(u) + "short";
                case 64: return std::string(u) + "long long";
                default: return std::string(u) + "int";
            }
        }
        case TypeRef::Kind::Float: return t.numBits() == 32 ? "float" : "double";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "const char*";
        case TypeRef::Kind::Binary: return "GravBytes";
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
