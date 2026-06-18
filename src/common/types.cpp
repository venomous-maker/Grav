#include "common/types.h"

namespace grav {

std::string typeRefName(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: {
            int b = t.numBits();
            if (t.isUnsigned)
                return b == 8 ? "byte" : "uint" + (b == 32 ? std::string() : std::to_string(b));
            if (b == 64) return "long";
            if (b == 32) return "int";
            return "int" + std::to_string(b);
        }
        case TypeRef::Kind::Float: return t.numBits() == 32 ? "float32" : "float";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "string";
        case TypeRef::Kind::Binary: return "binary";
        case TypeRef::Kind::Void: return "void";
        case TypeRef::Kind::Named: {
            if (t.args.empty()) return t.name;
            std::string s = t.name + "<";
            for (size_t i = 0; i < t.args.size(); ++i) {
                if (i) s += ", ";
                s += typeRefName(t.args[i]);
            }
            return s + ">";
        }
        case TypeRef::Kind::Future:
            return "Future<" + (t.elem ? typeRefName(*t.elem) : "?") + ">";
        case TypeRef::Kind::Pointer:
            return (t.elem ? typeRefName(*t.elem) : "?") + "*";
        case TypeRef::Kind::Array:
            return (t.elem ? typeRefName(*t.elem) : "?") + "[" +
                   std::to_string(t.arrayLen) + "]";
        case TypeRef::Kind::Null: return "null";
        case TypeRef::Kind::Error: return "<error>";
    }
    return "<error>";
}

} // namespace grav
