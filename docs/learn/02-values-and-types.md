# 2. Values, variables and types

Every value in Grav has a type, and the compiler knows it before the program
runs. That is what *statically typed* means: if you try to add a number to a
list, you find out when you compile, not when a user hits that line.

In this chapter you will learn:

- how to declare variables and constants, and when to write their types,
- the built-in types: integers, floats, booleans, strings and a few more,
- how numbers mix, and how to convert between types,
- the operators,
- how values are printed.

## Variables

`let` declares a variable: a name for a value that can change later.

```grav
fn main() {
    let count = 3
    count = count + 1
    print(count)
}
```

Output:

```text
4
```

You did not have to say that `count` is an integer: the compiler worked it out
from `3`. This is *type inference*. You can always write the type yourself,
after a colon:

```grav
fn main() {
    let count: int = 3
    let price: float = 9.99
    let name: string = "Ada"
    let ready: bool = true
    print("${name} has ${count} items at ${price}; ready: ${ready}")
}
```

Output:

```text
Ada has 3 items at 9.99; ready: true
```

Writing the type is useful when the value alone would give a different type
than you want (`let total: long = 0`), or simply to make code easier to read.
`let x: auto = 5` asks for inference explicitly; it is the same as `let x = 5`.

Once a variable has a type, it keeps it. Assigning a value of another type is an
error:

```text
let count = 3
count = "three"     // type error: cannot assign a value of type string to 'count' of type int
```

### Constants

`const` declares a name whose value never changes:

```grav
fn main() {
    const LIMIT = 10
    let used = 4
    print("${LIMIT - used} left")
}
```

Output:

```text
6 left
```

Assigning to a constant is a compile error. Use `const` whenever a value is
not meant to change: it tells readers (and the compiler) one less thing can
move.

### Scope

A variable exists from its declaration to the end of the block (`{ ... }`) it
was declared in. An inner block may declare a variable with the same name as an
outer one; inside the inner block the new one *shadows* the old. Within one
block, a name can be declared only once.

```grav
fn main() {
    let x = 1
    if (x > 0) {
        let x = 100        // a different x, only inside these braces
        print(x)
    }
    print(x)
}
```

Output:

```text
100
1
```

Using a variable before its declaration is an error. Declaring one and never
using it is a warning (`warning: unused variable 'x'`), which usually means a typo
or leftover code.

### Global variables

Variables and constants can also be declared outside any function. Global
declarations must state their type:

```grav
const APP: string = "inventory"
let requests: int = 0

fn handle() {
    requests += 1
}

fn main() {
    handle()
    handle()
    print("${APP}: ${requests} requests")
}
```

Output:

```text
inventory: 2 requests
```

A global's value can be anything: a computed value, an object, a list or map
literal. Literal values are part of the program file; any other value is
computed once at start-up, before `main` (or the tests) runs, in the order the
declarations appear (imported modules' globals first), so a global can use
those declared before it:

```grav
import "std"

let base: int = 40
let limit: int = base + 2
let names: Array<string> = ["ada", "bob"]
let seen: HashMap<string, int> = new HashMap<string, int>()

fn main() {
    seen.put("ada", 1)
    print("${limit} ${names.size()} ${seen.size()}")
}
```

Output:

```text
42 2 1
```

## The built-in types

| Type | What it holds | Examples |
|------|---------------|----------|
| `int` | a 32-bit signed integer (about ±2.1 billion) | `0`, `-7`, `1_000_000` |
| `long` | a 64-bit signed integer (about ±9.2 × 10¹⁸) | `9000000000` |
| `float` | a 64-bit floating-point number (`double` is the same type) | `3.14`, `1.5e3` |
| `bool` | `true` or `false` | `true` |
| `string` | immutable text (UTF-8) | `"hello"` |
| `void` | no value: what a function that returns nothing returns | |

And some you will use less often:

| Type | What it holds |
|------|---------------|
| `int8`, `int16`, `int32`, `int64` | signed integers of exactly that many bits (`int32` is `int`, `int64` is `long`) |
| `uint`, `ulong`, `uint8` … `uint64` | unsigned integers (no negatives); `byte` is `uint8`, `unsigned` is `uint` |
| `float32` | a 32-bit float (less precise, half the size) |
| `binary` | a buffer of raw bytes with a length ([`Bytes`](../std/data/bytes.md)) |
| `any` | a value of any type, checked when you take it out ([Structs, enums and tuples](08-structs-enums-tuples.md#any)) |

Each type is the same as a C type underneath (`int` is C's `int`, `long` is
`long long`, `float` is `double`), so numbers cost what they cost in C.

## Numbers

### Writing numbers

```grav
fn main() {
    print(1_000_000)        // underscores separate digits
    print(0xFF)             // hexadecimal
    print(0b1010)           // binary
    print(1.5e3)            // 1.5 × 10³
    print(-0.25)
}
```

Output:

```text
1000000
255
10
1500
-0.25
```

Avoid leading zeros: as in C, `017` is read as octal (15).

A whole number is an `int` unless it does not fit, in which case it is a
`long`. A number with a `.` or an exponent is a `float`.

### Arithmetic

```grav
fn main() {
    print(7 + 2)
    print(7 - 2)
    print(7 * 2)
    print(7 / 2)        // integer division drops the fraction
    print(7 % 2)        // the remainder
    print(2 ** 10)      // power
    print(7.0 / 2)      // a float on either side gives a float result
    print(-7 / 2)       // rounds toward zero
}
```

Output:

```text
9
5
14
3
1
1024
3.5
-3
```

Dividing an integer by zero is an error at run time (an `ArithmeticError`, see
[Errors](14-errors.md)). Dividing a float by zero gives infinity or
not-a-number, as in C; they print as `Infinity` and `NaN`, and the names
`Infinity` and `NaN` stand for them in code (`x.isNaN()` tests for NaN, which
is never `==` to anything).

Integers have a fixed size. Going past the largest value (*overflow*) is not
checked: the value wraps around, so `2147483647 + 1` is `-2147483648` for an
`int`. Use `long` for values that can grow large: totals, timestamps, file sizes.

### Mixing number types

Different integer types can be mixed in one expression. The result has the wider
type, and it is unsigned if either side is. An integer can go anywhere a float is
expected, but a float never turns into an integer by itself, because that would
silently drop the fraction.

One thing to watch: as in C, an integer value can be stored in any integer type,
including a smaller one (`let small: int = someLong`). If the value does not fit,
the high bits are cut off. A number *literal* that does not fit is caught at
compile time, but a computed value is not.

```grav
fn main() {
    let big: long = 9000000000
    let small = 42
    print(big + small)          // long
    let b: byte = 255
    print(b + 1)                // computed as int: 256
    let ratio: float = small    // int -> float: fine
    print(ratio / 8)
}
```

Output:

```text
9000000042
256
5.25
```

```text
let n: int = 2.5    // type error: cannot initialize 'n' of type int with a value of type float
```

## Booleans

A `bool` is `true` or `false`. Comparisons produce one, and `if`, `while` and
the other conditions need one. Grav does not treat numbers or strings as
true or false: `if (count)` is an error, so write `if (count != 0)`.

```grav
fn main() {
    let age = 20
    let member = false
    let adult = age >= 18
    print(adult && !member)     // and, not
    print(adult || member)      // or
}
```

Output:

```text
true
true
```

`&&` and `||` *short-circuit*: `a && b` does not evaluate `b` when `a` is false,
and `a || b` does not evaluate `b` when `a` is true. That makes guards like
`i < n && items[i] > 0` safe.

## Converting between types

Conversions that could lose information must be written out. There are three
ways to write one; they mean the same thing:

```grav
fn main() {
    let f = 3.99
    print(int(f))           // function style
    print(f as int)         // `as`
    print((int)f)           // C style
    print(float(7) / 2)
    print(long(f * 1000))   // every built-in type name works this way
    print(byte(300))        // too big for a byte: cut to its low 8 bits
    print(bool(0))
    print(str(42) + "!")    // any value to text
}
```

Output:

```text
3
3
3
3.5
3990
44
false
42!
```

Converting a float to an integer drops the fraction (toward zero). To round
instead, use `x.round()` (below) or `Math.round`. To turn text into a number,
cast it: `"42" as int`, `"2.5" as float`, `"yes" as bool` parse the text and
throw a `ValueError` for text that is not one; `42 as string` goes the other
way ([Strings and text](03-strings-and-text.md#strings-and-numbers) has more).

`as` also converts between related classes and to and from `any`; those come
up in later chapters. A class can take part in casts too: `price as float`
calls the object's `toFloat()` (likewise `toInt`, `toLong`, `toString`,
`toBool`), and `19.99 as Decimal` calls the class's static `from(19.99)`.

## Methods on values

Numbers, strings and bools have methods, from the standard library's
[types](../std/README.md#types) category: `Integer`, `Float`, `String`, `Bool`,
`Byte`, and `Null` for `T?` values.

```grav
import "std"

fn main() {
    let n = 255
    print(n.toHex() + " " + Integer.toHex(255))
    print("${(-7).abs()} ${1234567.grouped()} ${22.ordinal()}")
    print("${3.14159.round(2)} ${"  hi  ".trim().upper()} ${"42".toInt() + 1}")
    let port: int? = null
    print(port.orElse(8080))
}
```

Output:

```text
ff ff
7 1,234,567 22nd
3.14 HI 43
8080
```

`n.toHex()` is just another way to write `Integer.toHex(n)`: every static
method of those classes whose first parameter is the value works as a method of
the value, as one direct call, with nothing boxed or allocated for the value.
The classes also give limits (`Integer.MAX`, `Float.EPSILON`), parsing
(`Integer.parse`, `Float.tryParse`) and comparators for sorting
(`xs.sortBy(Integer.descending)`).

## Operators

| Kind | Operators |
|------|-----------|
| arithmetic | `+ - * / % **` |
| comparison | `== != < <= > >=` |
| logical | `&& \|\| !` |
| bitwise | `& \| ^ ~ << >>` (integers only) |
| assignment | `= += -= *= /= %= &= \|= ^= <<= >>=` |
| increment, decrement | `x++ ++x x-- --x` |
| conditional | `condition ? a : b` |
| null handling | `a ?? b`, `a?.b`, `x!` ([Null safety](09-null-safety.md)) |
| types | `x as T`, `x is T` |

```grav
fn main() {
    let a = 17
    let b = 5
    print(a > b ? "a is bigger" : "b is bigger")
    let score = 10
    score += 5          // score = score + 5
    score *= 2
    score++
    print(score)
    let flags = 0
    flags |= 4          // set bit 2
    flags |= 1          // set bit 0
    print(flags)
    print(1 << 4)
}
```

Output:

```text
a is bigger
31
5
16
```

`x++` adds one to `x`. Used inside an expression, `x++` gives the old value
and `++x` the new one; as a statement on its own line they do the same thing.

Operators bind in the usual mathematical order (`*` before `+`, comparisons
before `&&`, `&&` before `||`). Use parentheses whenever it is not obvious. The
full precedence table is in the [operator reference](../reference/operators.md).
Operands and function arguments are always evaluated left to right.

## Printing values

`print` writes any value followed by a line break. `str(value)` gives the same
text as a string, and `${value}` puts it inside a string.

```grav
fn main() {
    print(42)
    print(2.50)
    print(0.1 + 0.2)
    print(true)
    print("text")
    print("${1 + 1} and ${3 > 2}")
}
```

Output:

```text
42
2.5
0.30000000000000004
true
text
2 and true
```

Floats print in the shortest form that reads back as exactly the same number,
which is why `2.50` prints as `2.5`, `1.0` as `1`, and `0.1 + 0.2` shows its
true value. For a fixed number of decimals, use a format spec: `"${price:.2f}"`
([Strings and text](03-strings-and-text.md#formatting)).

`print` takes exactly one value. To print several, put them in one string:
`print("${a} ${b}")`.

## Summary

- `let` declares a variable, `const` a constant. Types are inferred from the
  value, or written after a colon; globals always need one.
- `int` (32-bit), `long` (64-bit), `float`, `bool` and `string` cover most code.
- Integers mix and widen freely; float to integer needs an explicit conversion.
- Conditions must be `bool`; there is no "truthy" or "falsy".
- `int(x)`, `x as int` and `(int)x` convert; `str(x)` turns anything into text;
  `"42" as int` parses.
- Values have methods: `n.toHex()`, `x.round(2)`, `s.trim()`, `port.orElse(80)`.

## Exercises

1. Declare `let total: long = 0`, then add `2147483647` to it twice and print it.
   What happens if you declare it as `int` instead?
2. Print the average of 7, 8 and 10 with its fraction. (Hint: one of the operands
   of the division must be a float.)
3. Write `let celsius = 36.6` and print the temperature in Fahrenheit
   (`celsius * 9 / 5 + 32`).
4. Without running it, work out what `print(17 % 5 * 2 + 1)` prints. Then check.
5. Use the conditional operator to print `"even"` or `"odd"` for a number.

Next: [Strings and text →](03-strings-and-text.md)
