#ifndef GRAV_COMMON_TYPES_H
#define GRAV_COMMON_TYPES_H

#include <memory>
#include <string>
#include <vector>

namespace grav {

// A resolved reference to a Grav type. Primitives are self-describing;
// `Named` carries the fully-qualified name of a class, interface, or enum
// (e.g. "geometry.Circle"). The type checker canonicalizes every Named
// reference to its fully-qualified form before code generation. `Future`
// wraps the eventual result type of an async call (see `elem`).
struct TypeRef {
    enum class Kind { Int, Float, Bool, String, Void, Named, Future, Pointer, Array, Null, Binary, Error };

    Kind kind = Kind::Error;
    std::string name;              // populated only when kind == Named
    std::shared_ptr<TypeRef> elem; // populated when kind == Future, Pointer, or Array
    int arrayLen = 0;              // populated when kind == Array (fixed length)
    // Numeric width/signedness hints. For Kind::Int: bits ∈ {8,16,32,64}. For
    // Kind::Float: bits ∈ {32,64}. bits == 0 means the canonical default (int = 32,
    // float = 64). isUnsigned only applies to Kind::Int.
    int bits = 0;
    bool isUnsigned = false;
    // Generic type arguments for a Named type before monomorphization, e.g. the
    // `int` in `Box<int>`. Cleared once the monomorphizer rewrites the reference
    // to its concrete, mangled name.
    std::vector<TypeRef> args;

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
    static TypeRef integer(int bits, bool isUnsigned) {
        TypeRef t;
        t.kind = Kind::Int;
        t.bits = bits;
        t.isUnsigned = isUnsigned;
        return t;
    }
    static TypeRef floating(int bits) {
        TypeRef t;
        t.kind = Kind::Float;
        t.bits = bits;
        return t;
    }

    // Effective numeric width: 0 resolves to the canonical default per kind.
    int numBits() const { return bits ? bits : (kind == Kind::Float ? 64 : 32); }

    bool isNumeric() const { return kind == Kind::Int || kind == Kind::Float; }
    bool isBinary() const { return kind == Kind::Binary; }
    bool isError() const { return kind == Kind::Error; }
    bool isVoid() const { return kind == Kind::Void; }
    bool isNamed() const { return kind == Kind::Named; }
    bool isFuture() const { return kind == Kind::Future; }
    bool isPointer() const { return kind == Kind::Pointer; }
    bool isArray() const { return kind == Kind::Array; }
    // A "slice": a runtime-length sequence (a variadic parameter), spelled in C as
    // a bare pointer plus a separate length. Marked by a negative array length.
    bool isSlice() const { return kind == Kind::Array && arrayLen < 0; }

    bool hasArgs() const { return !args.empty(); }

    bool operator==(const TypeRef &o) const {
        if (kind != o.kind) return false;
        if (kind == Kind::Int) return numBits() == o.numBits() && isUnsigned == o.isUnsigned;
        if (kind == Kind::Float) return numBits() == o.numBits();
        if (kind == Kind::Named) return name == o.name && args == o.args;
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
