#ifndef GRAV_COMMON_TYPES_H
#define GRAV_COMMON_TYPES_H

#include <memory>
#include <string>

namespace grav {

// A resolved reference to a Grav type. Primitives are self-describing;
// `Named` carries the fully-qualified name of a class, interface, or enum
// (e.g. "geometry.Circle"). The type checker canonicalizes every Named
// reference to its fully-qualified form before code generation. `Future`
// wraps the eventual result type of an async call (see `elem`).
struct TypeRef {
    enum class Kind { Int, Float, Bool, String, Void, Named, Future, Pointer, Array, Null, Error };

    Kind kind = Kind::Error;
    std::string name;              // populated only when kind == Named
    std::shared_ptr<TypeRef> elem; // populated when kind == Future, Pointer, or Array
    int arrayLen = 0;              // populated when kind == Array (fixed length)

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
    static TypeRef future(TypeRef e) {
        TypeRef t;
        t.kind = Kind::Future;
        t.elem = std::make_shared<TypeRef>(std::move(e));
        return t;
    }
    static TypeRef pointer(TypeRef e) {
        TypeRef t;
        t.kind = Kind::Pointer;
        t.elem = std::make_shared<TypeRef>(std::move(e));
        return t;
    }
    static TypeRef array(TypeRef e, int len) {
        TypeRef t;
        t.kind = Kind::Array;
        t.elem = std::make_shared<TypeRef>(std::move(e));
        t.arrayLen = len;
        return t;
    }

    bool isNumeric() const { return kind == Kind::Int || kind == Kind::Float; }
    bool isError() const { return kind == Kind::Error; }
    bool isVoid() const { return kind == Kind::Void; }
    bool isNamed() const { return kind == Kind::Named; }
    bool isFuture() const { return kind == Kind::Future; }
    bool isPointer() const { return kind == Kind::Pointer; }
    bool isArray() const { return kind == Kind::Array; }

    bool operator==(const TypeRef &o) const {
        if (kind != o.kind) return false;
        if (kind == Kind::Named) return name == o.name;
        if (kind == Kind::Array)
            return arrayLen == o.arrayLen && elem && o.elem && *elem == *o.elem;
        if (kind == Kind::Future || kind == Kind::Pointer)
            return elem && o.elem && *elem == *o.elem;
        return true;
    }
    bool operator!=(const TypeRef &o) const { return !(*this == o); }
};

// Human-readable name for diagnostics, e.g. "int", "string", "geometry.Circle".
std::string typeRefName(const TypeRef &t);

} // namespace grav

#endif // GRAV_COMMON_TYPES_H
