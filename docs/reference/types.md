# Types

Every expression in Grav has a type known at compile time. This page lists every
kind of type, how they convert into each other, and what their zero values are.

## Built-in types

| Type | Size | Range / meaning | C type |
|------|------|-----------------|--------|
| `int` (`int32`) | 4 bytes | −2,147,483,648 … 2,147,483,647 | `int` |
| `long` (`int64`) | 8 bytes | about ±9.22 × 10¹⁸ | `long long` |
| `int8`, `int16` | 1, 2 bytes | signed | `signed char`, `short` |
| `uint` (`unsigned`, `uint32`) | 4 bytes | 0 … 4,294,967,295 | `unsigned int` |
| `ulong` (`uint64`) | 8 bytes | 0 … about 1.8 × 10¹⁹ | `unsigned long long` |
| `byte` (`uint8`), `uint16` | 1, 2 bytes | unsigned | `unsigned char`, `unsigned short` |
| `float` (`double`, `float64`) | 8 bytes | IEEE 754 double | `double` |
| `float32` | 4 bytes | IEEE 754 single | `float` |
| `bool` | 1 byte | `true`, `false` | `bool` |
| `string` | pointer | immutable UTF-8 text, reference counted | `const char*` |
| `binary` | pointer + length | raw bytes | `GravBytes` |
| `void` | | no value; only as a function result | `void` |
| `any` | 16 bytes | a value of any type with a run-time tag | `GravAny` |

## Composite types

| Written | Meaning |
|---------|---------|
| `T[N]` | a fixed array of `N` elements, stored by value |
| `T[N][M]` | `M` arrays of `N` |
| `T*` | a pointer to `T` |
| `T?` | a nullable value: a `T` or `null` |
| `fn(A, B) -> R` | a function taking `A` and `B` and returning `R`; `fn(A)` returns nothing |
| `(A, B)`, `(A, B, C)` | tuples: the standard library's `Pair<A, B>` and `Triple<A, B, C>` |
| `Future<T>` | the result of an `async` call; `await` it for the `T` |
| `Name` | a class, struct, enum, interface, trait or alias |
| `Name<A, B>` | a generic type with type arguments |
| `Self` | inside a trait: the implementing class; a class method's result type: the receiver's own class; in a static method: the class the call names |

Variadic parameters are written `...name: T` and behave like an array of `T`
inside the function.

## Value types and reference types

| Kind | Types | Assigning / passing |
|------|-------|---------------------|
| value | numbers, `bool`, enums, structs, fixed arrays, `T?` of those, pointers | copies the value |
| immutable shared | `string`, `binary` | shares it (it cannot change) |
| reference | classes, interfaces, data enums, function values, `any` holding an object, `Future` | shares the same object |

## Zero values

Fields a constructor does not set, and `HashMap` lookups of missing keys, give
the type's zero value:

| Type | Zero value |
|------|------------|
| numbers | `0` |
| `bool` | `false` |
| `string` | `""` |
| enums | the first member |
| structs | every field at its zero value |
| classes, interfaces, functions, pointers, `T?`, `any` | `null` |

## Assignability

A value of type `From` can be used where `To` is expected when:

| From | To | Notes |
|------|----|-------|
| any type | the same type | |
| an integer type | any integer type | values that do not fit are truncated, as in C |
| an integer type | any float type | |
| a float type | a float type | |
| a float type | an integer type | **not allowed**: write `int(x)`, `x as int` or `(int)x` |
| `null` | a class, interface, function, pointer or `T?` | |
| a subclass | a parent class | |
| a class | an interface it implements | |
| `T` | `T?` | |
| `T?` | `T` | **not allowed**: use `x!`, `x ?? fallback`, or a null check |
| a numeric, bool, string, enum, class, interface, function or `null` value (or a `T?` of one) | `any` | |
| `any` | a concrete type | **not allowed**: use `x as T` (checked at run time) |

Everything else needs an explicit conversion, or is an error.

## Conversions

`T(x)`, `x as T` and `(T)x` all convert. `T(x)` works for every built-in type name
(`int(x)`, `long(x)`, `byte(x)`, `double(x)`, ...), unless a function or variable
with that name is in scope:

| Conversion | Behaviour |
|------------|-----------|
| number → number | as in C; float to integer drops the fraction |
| number → `bool` | `0` is `false`, anything else `true` |
| integer → enum (`n as Color`) | checked: a number that names no member throws `ValueError` |
| enum → integer (`int(c)`) | the member's value |
| `string` ↔ `binary` | the same bytes |
| class → subclass, class or interface → interface | checked at run time: throws `TypeError` |
| `any` → `T` | checked at run time: stops with a message naming both types |
| `T` → `any` | boxes the value |
| anything printable → `string` | `str(x)` (not `as`) |

Text to number conversions are library functions: [`Format`](../std/text/convert.md).

## Nullable types

`T?` is allowed for numbers, `bool`, `string`, enums, and structs that contain no
strings, objects, closures or `any`. For a class, interface, function or pointer
type, `T?` is the same as `T`. A `T?` cannot be used as a `T` until it is
unwrapped with `x!` or `x ?? fallback`, or narrowed by a null check. See
[Null safety](../learn/09-null-safety.md).

## Type inference

- `let x = value` gives `x` the type of `value`. `let x: auto = value` is the same.
- Integer literals are `int` unless they do not fit, then `long`.
- Generic type arguments are inferred from call arguments, constructor
  arguments, lambda results and the expected type; `auto` in a type argument list
  asks for inference explicitly.
- Lambda parameter types are inferred from the expected function type.
- A block lambda without `-> R` takes its result type from its `return` statements.
- `[...]` takes its type from the expected type: a fixed array, or a collection class.

## Generic types

Generic classes, structs, interfaces, traits, aliases, functions and methods take
type parameters in `<...>`. A parameter may have a bound, `T: Bound`, naming a
class or interface the argument must be or implement. Each distinct list of type
arguments compiles to its own code. See [Generics](../learn/13-generics.md).
