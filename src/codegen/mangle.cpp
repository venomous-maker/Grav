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

std::string cType(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: return "int";
        case TypeRef::Kind::Float: return "double";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "const char*";
        case TypeRef::Kind::Void: return "void";
        case TypeRef::Kind::Named: return structName(t.name) + "*";
        case TypeRef::Kind::Error: return "int /*error*/";
    }
    return "int";
}

} // namespace grav
