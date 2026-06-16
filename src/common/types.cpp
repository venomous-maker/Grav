#include "common/types.h"

namespace grav {

std::string typeRefName(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: return "int";
        case TypeRef::Kind::Float: return "float";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "string";
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
