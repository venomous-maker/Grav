#ifndef GRAV_COMMON_TYPES_H
#define GRAV_COMMON_TYPES_H

#include <string>

namespace grav {

// A resolved reference to a Grav type. Primitives are self-describing;
// `Named` carries the fully-qualified name of a class or interface
// (e.g. "geometry.Circle"). The type checker canonicalizes every Named
// reference to its fully-qualified form before code generation.
struct TypeRef {
    enum class Kind { Int, Float, Bool, String, Void, Named, Error };

    Kind kind = Kind::Error;
    std::string name; // populated only when kind == Named

    static TypeRef prim(Kind k) {
        TypeRef t;
        t.kind = k;
        return t;
    }
    static TypeRef named(std::string n) {
        TypeRef t;
        t.kind = Kind::Named;
        t.name = std::move(n);
        return t;
    }

    bool isNumeric() const { return kind == Kind::Int || kind == Kind::Float; }
    bool isError() const { return kind == Kind::Error; }
    bool isVoid() const { return kind == Kind::Void; }
    bool isNamed() const { return kind == Kind::Named; }

    bool operator==(const TypeRef &o) const {
        return kind == o.kind && (kind != Kind::Named || name == o.name);
    }
    bool operator!=(const TypeRef &o) const { return !(*this == o); }
};

// Human-readable name for diagnostics, e.g. "int", "string", "geometry.Circle".
std::string typeRefName(const TypeRef &t);

} // namespace grav

#endif // GRAV_COMMON_TYPES_H
