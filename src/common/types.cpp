#include "common/types.h"

#include <cctype>
#include <unordered_map>
#include <vector>

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

// One dotted name: the longest registered prefix is replaced; standard-library
// names lose their `std.<category>.` prefix.
static std::string prettyToken(const std::string &tok) {
    const auto &names = displayNames();
    std::vector<size_t> ends{tok.size()};                 // longest prefix first
    for (size_t p = tok.rfind('.'); p != std::string::npos && p > 0; p = tok.rfind('.', p - 1)) ends.push_back(p);
    for (size_t end : ends) {
        auto it = names.find(tok.substr(0, end));
        if (it != names.end()) return it->second + tok.substr(end);
    }
    if (tok.rfind("std.", 0) == 0) {                     // std.collections.Array -> Array
        size_t second = tok.find('.', 4);
        if (second != std::string::npos && second + 1 < tok.size()) return prettyToken(tok.substr(second + 1));
    }
    return tok;
}

std::string prettyNames(const std::string &text) {
    if (displayNames().empty() && text.find("std.") == std::string::npos) return text;
    std::string out;
    size_t i = 0;
    auto isName = [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '$'; };
    while (i < text.size()) {
        unsigned char ch = (unsigned char)text[i];
        if (!isName(ch)) { out += text[i++]; continue; }
        size_t j = i;
        while (j < text.size() && (isName((unsigned char)text[j]) ||
                                   (text[j] == '.' && j + 1 < text.size() && isName((unsigned char)text[j + 1]))))
            ++j;
        out += prettyToken(text.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string aTypeName(const TypeRef &t) {
    std::string n = typeRefName(t);
    bool vowel = !n.empty() && std::string("aeiouAEIOU").find(n[0]) != std::string::npos;
    return (vowel ? "an " : "a ") + n;
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
