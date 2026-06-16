#include "common/types.h"

namespace grav {

std::string typeRefName(const TypeRef &t) {
    switch (t.kind) {
        case TypeRef::Kind::Int: return "int";
        case TypeRef::Kind::Float: return "float";
        case TypeRef::Kind::Bool: return "bool";
        case TypeRef::Kind::String: return "string";
        case TypeRef::Kind::Void: return "void";
        case TypeRef::Kind::Named: return t.name;
        case TypeRef::Kind::Future:
            return "Future<" + (t.elem ? typeRefName(*t.elem) : "?") + ">";
        case TypeRef::Kind::Pointer:
            return (t.elem ? typeRefName(*t.elem) : "?") + "*";
        case TypeRef::Kind::Null: return "null";
        case TypeRef::Kind::Error: return "<error>";
    }
    return "<error>";
}

} // namespace grav
