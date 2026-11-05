# Operators

## Precedence

From loosest to tightest binding. Operators on the same row bind equally and
group in the direction shown.

| Level | Operators | Groups |
|-------|-----------|--------|
| 1 | `cond ? a : b` | right |
| 2 | `??` | left |
| 3 | `\|\|` | left |
| 4 | `&&` | left |
| 5 | `\|` | left |
| 6 | `^` | left |
| 7 | `&` | left |
| 8 | `== != < <= > >=` | left |
| 9 | `<< >>` | left |
| 10 | `+ -` | left |
| 11 | `* / %` | left |
| 12 | `**` | right |
| 13 | `as`, `is` | left |
| 14 | unary `- ! ~ ++ -- await`, `&x`, `*p`, casts `(T)x` | right |
| 15 | calls `f()`, `f(...xs)`, `f::<T>()`, indexing `a[i]`, `a[i, j]`, slices `a[lo..hi]`, members `.` `?.` `->`, postfix `++ --`, unwrap `x!` | left |

Assignment (`=`, `+=`, ...) is a statement, not an expression.

Operands and arguments are evaluated **left to right**: a receiver before
its method's arguments, both sides of an operator, array and struct literal
elements, `new` arguments, and conditions alike. In an assignment the target's
object and indices come before the value (`a[i()] = v()` calls `i` first).
`&&`, `||`, `??`, `? :` and `?.` evaluate their right side only when needed,
and then in the same left-to-right order. Each operand runs once, `??`'s left
side included.

## Arithmetic

| Operator | Operands | Result |
|----------|----------|--------|
| `a + b`, `a - b`, `a * b` | numbers | the wider type (float if either is) |
| `a / b` | integers | integer division, rounding toward zero; dividing by zero throws `ArithmeticError` |
| `a / b` | floats | IEEE division (`inf`, `nan` possible) |
| `a % b` | integers | remainder, with the sign of `a`; `% 0` throws `ArithmeticError` |
| `a % b` | floats | floating remainder (`fmod`) |
| `a ** b` | numbers | power |
| `-a` | number | negation |
| `a + b` | strings | concatenation |

Mixed integer types promote to the wider type, and to unsigned if either side is
unsigned; integers narrower than `int` compute as `int`. Integer overflow is not
checked.

## Comparison

| Operator | Works on |
|----------|----------|
| `==`, `!=` | every type: numbers by value, strings by content, structs field by field, fixed arrays element by element, enums by value, data enums by variant and fields, objects by identity (or `operator==`), `any` by the value inside, `T?` against `null`, `T` or `T?` |
| `<`, `<=`, `>`, `>=` | numbers, strings (byte by byte), enums (declaration order), classes with `operator<` |

Comparing with `null` is always a reference test (or, for `T?`, a test for a
value); it never calls `operator==`.

## Logical

| Operator | Meaning |
|----------|---------|
| `a && b` | true when both are; `b` is skipped when `a` is false |
| `a \|\| b` | true when either is; `b` is skipped when `a` is true |
| `!a` | not |

Operands must be `bool`.

## Bitwise and shifts

`&`, `|`, `^` (xor), `~` (complement), `<<`, `>>` work on integers. A shift by a
constant that is not smaller than the operand's width in bits is a compile error;
the result of a shift has the (promoted) type of its left operand.

## Assignment

```text
x = e     x += e   x -= e   x *= e   x /= e   x %= e
x &= e    x |= e   x ^= e   x <<= e  x >>= e
x++  x--  ++x  --x
```

The target can be a variable, a field, an element (`a[i]`, including through
`operator[]`/`operator[]=`) or a dereferenced pointer. `x op= e` is
`x = x op e`, with `x`'s container and index evaluated once. Captured variables
inside a lambda and `const` names cannot be assigned.

## Null handling

| Operator | Meaning |
|----------|---------|
| `a ?? b` | `a` unless it is null, else `b`. On a `T?`, the result is a `T` when `b` is a `T`. |
| `a?.m`, `a?.m(...)` | the member, or a zero value (`0`, `false`, `""`, `null`) when `a` is null |
| `x!` | the value of a `T?` (or a non-null reference); throws `StateError` when null |

## Types

| Operator | Meaning |
|----------|---------|
| `x as T`, `(T)x`, `T(x)` | conversion; see [Types](types.md#conversions) |
| `x is T` | whether `x` is a `T`: a class or subclass, an interface it implements, a data-enum variant, or what an `any` holds |

## Conditional

`cond ? a : b` evaluates `cond`, then exactly one of `a` and `b`. The result
type is the common type of the two branches; `ok ? 5 : null` is an `int?`.

## Indexing and slicing

| Form | Meaning |
|------|---------|
| `a[i]` | element `i` of a fixed array, `Array`, string (a byte), `binary` or pointer; bounds-checked except on pointers |
| `a[i, j]` | a class's `operator[]` with two keys |
| `a[lo..hi]`, `a[lo..=hi]`, `a[lo..]`, `a[..hi]` | a slice: a new string, `Array`, `Bytes`, or (with constant bounds) fixed array |

## Members and calls

| Form | Meaning |
|------|---------|
| `a.m` | a field, method, static member, enum member or namespace member |
| `p->m` | `(*p).m` through a pointer |
| `f(args)` | a call; arguments may be named (`name: value`) and spread (`...xs`) |
| `f::<T>(args)` | a call with explicit type arguments |
| `obj.method` without a call | a function value bound to `obj` |

## Overloadable operators

Classes can define `+ - * / % == != < <= > >= [] []=` as methods. See
[Operators and overloading](../learn/12-operators-and-overloading.md).
