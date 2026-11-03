#include "common/types.h"

#include <cctype>
#include <unordered_map>

namespace grav {

namespace {
std::unordered_map<std::string, std::string> &displayNames() {
    static std::unordered_map<std::string, std::string> names;
    return names;
}
} // namespace

void setDisplayName(const std::string &mangled, const std::string &readable) {
    displayNames()[mangled] = readable;
}
void clearDisplayNames() { displayNames().clear(); }

std::string prettyNames(const std::string &text) {
    const auto &names = displayNames();
    if (names.empty() || text.find('$') == std::string::npos) return text;
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char ch = (unsigned char)text[i];
        if (!(std::isalnum(ch) || ch == '_' || ch == '$')) { out += text[i++]; continue; }
        size_t j = i;
        while (j < text.size() && (std::isalnum((unsigned char)text[j]) || text[j] == '_' || text[j] == '$')) ++j;
        std::string tok = text.substr(i, j - i);
        auto it = names.find(tok);
        out += it != names.end() ? it->second : tok;
        i = j;
    }
    return out;
}

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
        case TypeRef::Kind::Any: return "any";
        case TypeRef::Kind::Void: return "void";
        case TypeRef::Kind::Named: {
            if (t.args.empty()) return prettyNames(t.name);
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
        case TypeRef::Kind::Function: {
            std::string s = "fn(";
            for (size_t i = 0; i < t.fnParams.size(); ++i)
                s += (i ? ", " : "") + typeRefName(t.fnParams[i]);
            s += ")";
            if (t.elem && !t.elem->isVoid()) s += " -> " + typeRefName(*t.elem);
            return s;
        }
        case TypeRef::Kind::Null: return "null";
        case TypeRef::Kind::Error: return "<error>";
    }
    return "<error>";
}

} // namespace grav
