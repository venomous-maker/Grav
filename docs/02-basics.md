# 2. Basics

## Variables

`let` declares a variable, `const` a constant. The type comes after a colon and can
be omitted when it can be inferred from the initializer.

```grav
fn main() {
    let count: int = 3
    let price = 9.99            // inferred: float
    const LIMIT: int = 10
    count = count + 1
    print("${count} items at ${price}, limit ${LIMIT}")
}
```

Output:

```text
4 items at 9.99, limit 10
```

Assigning to a `const` is a compile error, and so is using a variable before it is
declared. Unused local variables produce a warning (an error with `-Werror`).

## Primitive types

| Type | Meaning | C type |
|------|---------|--------|
| `int` | 32-bit signed integer | `int` |
| `long` / `int64` | 64-bit signed integer | `long long` |
| `int8` `int16` `int32` | sized signed integers | |
| `uint` / `unsigned` | 32-bit unsigned | `unsigned` |
| `ulong` / `uint64`, `uint8`…`uint32`, `byte` | sized unsigned integers (`byte` = `uint8`) | |
| `float` / `double` / `float64` | 64-bit floating point | `double` |
| `float32` | 32-bit floating point | `float` |
| `bool` | `true` / `false` | `bool` |
| `string` | immutable text | `const char*` |
| `binary` | a byte buffer with a length | |
| `void` | no value (function results) | |

Integers of different widths mix freely and promote like C (the wider type wins,
unsigned if either side is unsigned). An integer flows into a float, but a float into
an integer needs an explicit cast.

```grav
fn main() {
    let big: long = 9000000000
    let small: int = 42
    let b: byte = 255
    print(big + small)          // long arithmetic
    print(b + 1)                // promotes to int: 256
    let ratio: double = small   // int -> double is implicit
    print(ratio / 8.0)
}
```

Output:

```text
9000000042
256
5.250000
```

`print` shows floats with six decimals; string interpolation and `str()` use the
shortest form (`5.25`).

## Operators

| Kind | Operators |
|------|-----------|
| arithmetic | `+ - * / %` and `**` (power) |
| comparison | `== != < <= > >=` |
| logical | `&& \|\| !` (short-circuit) |
| bitwise | `& \| ^ ~ << >>` |
| assignment | `= += -= *= /= %= &= \|= ^= <<= >>=` |
| increment | `++x x++ --x x--` |
| conditional | `cond ? a : b` |
| null | `a ?? b`, `a?.b` (see [Data types](05-data-types.md)) |
| type | `x as T`, `x is T` |

```grav
fn main() {
    let a = 17
    let b = 5
    print("${a / b} ${a % b} ${2 ** 10} ${1 << 4} ${a & 1} ${a > b ? "bigger" : "smaller"}")
    let flags = 0
    flags |= 4
    flags |= 1
    print(flags)
}
```

Output:

```text
3 2 1024 16 1 bigger
5
```

Integer division truncates. `%`, bitwise, and shift operators need integers.
Operands and arguments are evaluated **left to right**. The full precedence table is
in the [reference](reference.md#operator-precedence).

## Casts

```grav
fn main() {
    let f = 3.99
    print(int(f))               // 3 — function-style cast
    print(f as int)             // 3 — `as` cast
    print((long)7 * 1000000000) // C-style cast
    print(float(7) / 2.0)
    print(bool(0))
}
```

Output:

```text
3
3
7000000000
3.500000
false
```

`as` also converts between related classes, from an integer to an enum
(`2 as Color`), and between `string` and `binary`.

## Strings

Strings are immutable. `+` concatenates, `==` compares contents, and `${expr}`
interpolates any printable value (numbers, booleans, enums, and objects with a
`toString()` method).

```grav
fn main() {
    let name = "Ada"
    let year = 1815
    let s = "Hello, " + name + "!"
    s += " Born ${year}, that is ${2025 - year} years ago."
    print(s)
    print("same? ${"abc" == "abc"}")
    print(str(42) + str(true))
}
```

Output:

```text
Hello, Ada! Born 1815, that is 210 years ago.
same? true
42true
```

Escape sequences:

| Escape | Meaning |
|--------|---------|
| `\n` `\t` `\r` | newline, tab, carriage return |
| `\\` `\"` `\'` | backslash and quotes |
| `\$` | a literal `$` (so `\${x}` is not interpolated) |
| `\0` `\a` `\b` `\f` `\v` | control characters |
| `\e` | ESC, for terminal colour codes |
| `\xHH` | one byte, e.g. `\x41` is `A` |
| `\u{1F680}` | a Unicode code point, encoded as UTF-8 |

The `Str` class in the standard library adds searching, slicing, splitting, and
more — see [Standard library](14-standard-library.md#strings).

Next: [Control flow →](03-control-flow.md)
