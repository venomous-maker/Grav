# Reference

## Keywords

```text
let const fn return if else while do for in break continue switch match case default
class struct enum interface trait abstract extends implements uses constructor new
self this super static readonly public private protected namespace import export
type typedef try catch finally throw async await as is sizeof null true false
int float bool string void
```

Contextual names: `auto` (an inferred type argument, or `let x: auto = …`) and
`any` (the dynamic type) are recognized in type positions and stay usable as
identifiers elsewhere.

Contextual keywords (only special where shown, otherwise usable as names):
`defer` (statement), `step` (in a range), `final` (before `class`/`fn`),
`destructor` (in a class body), `operator` (in `fn operator+`).

Built-in type names that are also valid identifiers: `long int64 int32 int16 int8
signed uint unsigned uint64 ulong uint32 uint16 uint8 byte double float64 float32
binary`.

## Operator precedence

From loosest to tightest binding:

| Level | Operators | Associativity |
|-------|-----------|---------------|
| 1 | `? :` (ternary) | right |
| 2 | `??` (null coalescing) | left |
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
| 13 | `as`, `is` (`x is T` also tests what an `any` holds) | left |
| 14 | unary `- ! ~ ++ -- await`, `&` `*` (address/deref), `(T)x` casts | right |
| 15 | calls `f()`, `::<T>()`, indexing `a[i]`, members `.` `?.` `->`, postfix `++ --` | left |

Assignment (`=`, `+=`, …) is a statement, not an expression. Operands and arguments
are evaluated left to right; `&&`, `||`, `??`, `?:`, and `?.` short-circuit.

## Literals

| Kind | Examples |
|------|----------|
| integers | `42`, `0`, `9000000000` |
| floats | `3.14`, `0.5`, `2.0` |
| booleans | `true`, `false` |
| strings | `"text"`, `"tab\t"`, `"sum=${a + b}"`, `"\u{263A}"` |
| arrays | `[1, 2, 3]`, `[[1, 2], [3, 4]]` |
| structs | `Point { x: 1, y: 2 }`, `Pair<int, string> { first: 1, second: "a" }` |
| lambdas | `fn(x) => x * 2`, `fn(a: int, b: int) -> int { return a + b }` |
| null | `null` |

## Types

```text
int long int8..int64 uint ulong uint8..uint64 byte     integers
float double float32 float64                          floating point
bool string binary void any                           (any = dynamic, run-time checked)
Name                       class, interface, struct, enum, or alias
Name<T, U>                 generic instance
T[N]                       fixed-length array
T*                         pointer
fn(A, B) -> R              function value
Future<T>                  handle to a running async call (await it for the T)
```

## Declarations

```text
fn name<T>(a: A, ...rest: R) -> Ret { ... }          function (generic, variadic optional)
async fn name(...) -> T { ... }                      (runs on the worker pool; no variadics)
[final|abstract] class Name<T> extends Base, Other implements I, J uses f: T { ... }
    [public|private|protected] [static] [readonly] field: T [= init for static]
    constructor(...) { ... }          (may repeat with different parameters)
    destructor() { ... }
    [final|abstract|static] fn method<U>(...) -> T { ... }
    fn operator+(o: T) -> R { ... }   (also - * / % == != < <= > >= [] []=)
interface Name<T> { fn m(...) -> T }
trait Name { fn m(o: Self) -> T  fn withDefault() -> T { ... } }
struct Name<T> { a: T  b: int }
enum Name { A, B, C }
type Alias<T> = T[3]                  (typedef is a synonym)
let name: T = value / const NAME: T = value          (module-level globals)
namespace a { namespace b { ... } }
import "relative/path.grav"
#define NAME value / #define F(x) (expr)
%{ C code %}
@Decorator / @Decorator(args) / export                (metadata on declarations)
```

## Statements

```text
let x: T = e / const x = e
x = e / x += e (and -= *= /= %= &= |= ^= <<= >>=)
if (c) { } else if (c) { } else { }
while (c) { } / do { } while (c)                     (a loop may be labeled: `outer: for (...)`)
for (init; cond; update) { }
for (i in a..b [step s]) { } / for (i in a..=b) { }
for (x in xs) { } / for (i, x in xs) { }
switch (v) { case a, b: { } default: { } }            (match is a synonym)
break [label] / continue [label] / return [e]
defer stmt / defer { }
throw e
try { } catch (e: T) { } [catch (e: U) { }] [finally { }]
```

## Built-in functions

| Function | Description |
|----------|-------------|
| `print(x)`, `println(x)` | print a value and a newline |
| `str(x)` | value to string (numbers, bools, enums, `toString()` objects) |
| `int(x)`, `float(x)`, `bool(x)` | numeric casts |
| `input()` | one line from stdin |
| `argc()`, `argv(i)` | command-line arguments |
| `env(name)`, `cwd()` | environment variable, working directory |
| `typename(o)`, `isInstance(o, T)` | runtime type information (`typename` also works on `any`) |
| `sizeof(T)` / `sizeof(expr)` | size in bytes |
| `assert(c[, msg])`, `panic(msg)`, `exit(code)` | checks and termination (a failed `assert` reports file:line) |
| `assertEq(actual, expected[, msg])` | fails with both values printed when they differ |
| `free(x)` | release an object (running destructors), pointer, closure, or finished future |

[← Back to the index](README.md)
