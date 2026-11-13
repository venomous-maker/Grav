<p align="center">
  <img src="docs/assets/grav-logo.svg" width="128" height="128" alt="Grav logo">
</p>

<h1 align="center">Grav</h1>

**Grav** is a statically typed, compiled, TypeScript-inspired language for backend and
systems work. It compiles to portable **C** (and, with one flag, straight to a native
binary). The compiler — `gravc` — is written in C++20.

> Grav gives you a TypeScript-like OOP surface (classes, interfaces, access modifiers,
> generics-to-come) but lowers everything to a predictable, C-level execution model:
> structs, vtables, and compile-time type checking with **no hidden runtime**.

---

## Documentation

The documentation lives in [`docs/`](docs/README.md):

- [**Learn Grav**](docs/learn/README.md): twenty chapters, from
  [getting started](docs/learn/01-getting-started.md) to
  [how Grav compiles](docs/learn/20-how-it-compiles.md), each with examples and exercises;
- the [**language reference**](docs/reference/README.md): syntax, keywords, types,
  operators, statements, built-in functions, the `grav` command;
- the [**standard library reference**](docs/std/README.md): a page for every module.

Every runnable snippet in the docs is compiled and checked by `tests/docs.sh`.

## Table of contents

- [Quick start](#quick-start)
- [The compiler (`gravc`)](#the-compiler-gravc)
- [Language tour](#language-tour)
  - [Primitive types & variables](#primitive-types--variables)
  - [Functions & `main`](#functions--main)
  - [Control flow](#control-flow)
  - [Enums](#enums)
  - [Operators](#operators)
  - [Ranges (`for … in`)](#ranges-for--in)
  - [For-each loops & `step`](#for-each-loops--step)
  - [Operator overloading](#operator-overloading)
  - [Memory: destructors & `defer`](#memory-destructors--defer)
  - [Lambdas & function types](#lambdas--function-types)
  - [Method overloading](#method-overloading)
  - [`final`](#final)
  - [Null, `??` & optional chaining](#null----optional-chaining)
  - [Casts & type tests (`as` / `is`)](#casts--type-tests-as--is)
  - [Pointers](#pointers)
  - [Async / await](#async--await)
  - [Structs](#structs)
  - [Arrays](#arrays)
  - [Type aliases](#type-aliases)
  - [Macros](#macros)
  - [sizeof](#sizeof)
  - [Generics](#generics)
  - [Variadics](#variadics)
  - [Exceptions](#exceptions)
  - [Globals & static fields](#globals--static-fields)
  - [Classes](#classes)
  - [Access modifiers & `readonly`](#access-modifiers--readonly)
  - [Auto getters/setters](#auto-getterssetters)
  - [Properties: `get` and `set`](#properties-get-and-set)
  - [Inheritance & polymorphism](#inheritance--polymorphism)
  - [Abstract classes](#abstract-classes)
  - [Interfaces](#interfaces)
  - [Static methods](#static-methods)
  - [Namespaces](#namespaces)
  - [Imports](#imports)
  - [Runtime type info (RTTI)](#runtime-type-info-rtti)
  - [Built-ins](#built-ins)
  - [String interpolation](#string-interpolation)
  - [Command-line arguments & stdin](#command-line-arguments--stdin)
  - [Inline C (`%{ … %}`)](#inline-c---)
  - [Decorators & `export`](#decorators--export)
  - [Comments](#comments)
  - [Diagnostics & warnings](#diagnostics--warnings)
- [How Grav lowers to C](#how-grav-lowers-to-c)
- [Editor support](#editor-support)
- [Project layout](#project-layout)
- [Building the compiler](#building-the-compiler)
- [Roadmap / not yet implemented](#roadmap--not-yet-implemented)

---

## Quick start

```bash
# build the compiler
cmake -S . -B build && cmake --build build

# transpile a program to C
./build/gravc examples/06_inheritance.grav -o /tmp/inheritance.c

# …or compile straight to a native binary and run it
./build/gravc examples/apps/bank.grav --emit bin -o /tmp/bank
/tmp/bank

# …or build and run in one step
./build/gravc examples/apps/life.grav --run
```

```grav
// examples/06_inheritance.grav
interface Shape {
    fn area() -> int
}

class Circle implements Shape {
    public radius: int
    constructor(r: int) { self.radius = r }
    public fn area() -> int { return 3 * self.radius * self.radius }
}

fn main() {
    let c = new Circle(10)   // type inferred as Circle
    print(c.area())          // 300
}
```

---

## The compiler (`gravc`)

```
gravc <input.grav> [options] [-- args...]
gravc -                       # read source from stdin, write C to stdout
```

| Flag                         | Meaning                                                        |
|------------------------------|----------------------------------------------------------------|
| `-o <out>`                   | Output path (`.c`, `.s`, or the executable).                   |
| `--emit c\|asm\|bin\|both`   | What to produce (default `c`).                                 |
| `-c` / `-S` / `-b`,`--bin`   | Aliases for `--emit c` / `asm` / `bin`.                        |
| `-r`, `--run`                | Build a binary and run it (args after `--` go to the program). |
| `--keep-c`                   | Keep the generated `.c` when building a binary.                |
| `-O0`…`-O3`, `-Os`, `-Ofast` | Optimization level (default `-O2`; `-O0` when only `-g` is given). |
| `--release`                  | `-O3`, `-DNDEBUG`, and no stack-trace bookkeeping.             |
| `--no-trace`                 | Drop call-stack tracing (uncaught exceptions print no trace).  |
| `--native`                   | Tune for the build machine (`-march=native`).                  |
| `--race`                     | Build with ThreadSanitizer to detect data races between tasks. |
| `--sanitize`                 | Build with AddressSanitizer + UBSan (memory errors, UB).       |
| `-g`, `-g3`                  | Include debug info.                                            |
| `--cc <compiler>`            | C compiler to use (default `$CC`, else `cc`).                  |
| `-Werror`                    | Treat warnings (e.g. unused variables) as fatal.              |
| `-v`, `--verbose`            | Print the C compiler command.                                  |
| `-h`, `--help`               | Show usage.                                                    |
| `-`                          | Read from stdin and print C to stdout.                         |

For `asm`/`bin`/`both`, `gravc` invokes the system C compiler (`$CC`, default `cc`)
as `cc -std=c11 -fwrapv -O2 [opts] <file>.c -o <out>` (`-fwrapv` makes integer
overflow wrap around); the intermediate `.c` goes to a unique file in the temp
directory (use `--keep-c` to keep it next to the source). `--run` exits with the
program's own exit status.

**What the generated C looks like.** Every generated function is `static` (one
translation unit, so the C compiler can inline and drop freely); vtables and
interface tables are `const`; a method call that no subclass overrides compiles to a
direct call instead of a vtable lookup; virtual and interface calls go through tiny
`static inline` thunks so the receiver is evaluated exactly once; string `+` chains
and `"${…}"` interpolation are fused into a single allocation, and `print` of such a
string is one `printf` with no allocation at all. Stack-trace bookkeeping is a single
store per call-capable statement and disappears entirely under `--release`.

---

## Language tour

> **Runnable examples for every section:** [`examples/`](examples/) has one program
> per area of the language (`01_basics` … `15_inline_c_macros`), a multi-file
> [`project/`](examples/project/) (imports, nested namespaces, shared globals), and
> four complete programs in [`apps/`](examples/apps/) — a bank, a word-frequency
> analyzer, Conway's Game of Life, and a command-driven task manager. See
> [`examples/README.md`](examples/README.md) for the index;
> [`tests/run.sh`](tests/run.sh) builds and runs them all against golden outputs.

### Primitive types & variables

Four primitives: `int`, `float`, `bool`, `string`.

```grav
let x: int = 10
let pi: float = 3.14
let ok: bool = true
let name: string = "Grav"

let inferred = 42          // type inferred from the initializer
const MAX: int = 100       // const: reassignment is a compile error
```

Grav is **strongly typed** — there are no implicit conversions:

```grav
let a: int = 1
let b: float = 2.0
let c: int = a + b         // error: '+' requires matching types (int vs float)
let d: int = a + int(b)    // ok: explicit cast
```

Casts are `int(x)`, `float(x)`, `bool(x)`. String `+` string concatenates.

### Functions & `main`

```grav
fn add(a: int, b: int) -> int {
    return a + b
}

fn greet(name: string) -> void {   // '-> void' optional
    print("hi " + name)
}

fn main() {                        // program entry point
    print(add(2, 3))
}
```

### Control flow

`if` / `else if` / `else`, `while`, `do`/`while`, C-style `for`, and `switch`/`match`
(no fall-through; cases need braces, multiple values via commas). `break`/`continue`
work inside loops. Logical operators `&&`, `||`, `!` short-circuit; `++`/`--` work in
prefix and postfix form.

```grav
fn classify(x: int) -> string {
    match (x) {                     // 'switch' is a synonym
        case 0:        { return "zero" }
        case 1, 2, 3:  { return "small" }
        default:       { return "big" }
    }
    return "?"
}

fn main() {
    for (let i: int = 0; i < 5; i++) {
        if (i == 2 || i == 4) { continue }
        print(i)                    // 0 1 3
    }
    let n: int = 3
    do { print(n)  n-- } while (n > 0)
    print(!(1 > 2) && true)         // true
}
```

Conditions must be `bool` (no truthy ints); `switch`/`match` subjects are `int`,
`string`, or an [enum](#enums). Statements may be terminated with `;` (recommended,
and required to disambiguate a statement that begins with `*`/`&`/`-` from the
previous line); when omitted, whitespace separates statements.

### Enums

An `enum` is a named set of integer constants. Members are referenced as
`EnumName.Member`, compared with `==`/`!=`, used as `switch`/`match` subjects, and
cast to their ordinal with `int(...)`. Enums lower to a real C `enum`.

```grav
enum Direction { North, East, South, West }

fn turn(d: Direction) -> Direction {
    switch (d) {
        case Direction.North: { return Direction.East }
        default:              { return Direction.North }
    }
}

fn main() {
    let d = Direction.South
    print(d == Direction.South)   // true
    print(int(Direction.West))    // 3
}
```

### Operators

Beyond `+ - * /` and the comparison/logical operators, Grav has:

| Group | Operators |
| --- | --- |
| Arithmetic | `%` (modulo, ints only) |
| Bitwise | `&` `|` `^` `~` |
| Shifts | `<<` `>>` |
| Compound assignment | `+= -= *= /= %=` and `&= |= ^= <<= >>=` |
| Ternary | `cond ? a : b` |

Operands and arguments are evaluated **left to right**. Modulo, bitwise and shift
operators require `int` operands. `string += string`
concatenates in place. The ternary's branches must share a common type.

```grav
fn main() {
    let flags = 0
    flags |= 4
    flags |= 1                       // flags == 5
    print(flags & 6)                 // 4
    print(1 << 10)                   // 1024
    print(~0)                        // -1
    print(flags % 2 == 0 ? "even" : "odd")   // odd
}
```

### Ranges (`for … in`)

A counting loop over a half-open (`..`) or inclusive (`..=`) integer range. The
loop variable is an `int` scoped to the loop; the upper bound is evaluated once.

```grav
fn main() {
    let sum = 0
    for (i in 1..=100) { sum += i }  // 1 + 2 + … + 100
    print(sum)                       // 5050
}
```

### For-each loops & `step`

`for … in` also walks collections: fixed arrays, variadic slices, `binary` bytes,
and any object with `length()`/`size()` plus `get(i)`/`at(i)` (so `Array<T>`,
`List<T>`, `StringBuilder`-style classes, your own types). The two-name form binds the
index too. Ranges take an optional `step`, which may be negative to count down.

```grav
fn main() {
    let xs = [3, 1, 4]
    for (x in xs) { print(x) }                 // 3 1 4
    for (i, x in xs) { print("${i}:${x}") }    // 0:3 1:1 2:4
    for (i in 10..0 step -5) { print(i) }      // 10 5
    for (i in 0..=10 step 5) { print(i) }      // 0 5 10
}
```

### Operator overloading

A class can implement `+ - * / %`, `== != < <= > >=`, and indexing with
`fn operator<op>(other: T)`. `!=` falls back to `!(a == b)`; `operator[]` reads and
`operator[]=` writes (`obj[k] = v`). Comparisons against `null` stay reference tests.

```grav
class Vec {
    public x: int  public y: int
    constructor(x: int, y: int) { self.x = x; self.y = y }
    fn operator+(o: Vec) -> Vec { return new Vec(self.x + o.x, self.y + o.y) }
    fn operator==(o: Vec) -> bool { return self.x == o.x && self.y == o.y }
    fn toString() -> string { return "(${self.x}, ${self.y})" }
}

fn main() {
    let v = new Vec(1, 2) + new Vec(3, 4)
    print(v)                          // (4, 6)   — toString() is used by print/str/${}
    print(v == new Vec(4, 6))         // true
}
```

`**` is exponentiation (`2 ** 10` is `1024`; float operands use `pow`). It binds
tighter than `*` and is right-associative.

### Memory: destructors & `defer`

There is no garbage collector: `new` allocates and `free(obj)` releases. A class may
declare `destructor() { … }`; `free` runs the *dynamic* type's destructor, then each
base class's, most-derived first. The standard collections free their buffers this
way, so `free(list)` releases everything it owns.

`defer stmt` (or `defer { … }`) schedules code to run when the enclosing block exits —
normally or through `return`, `break`, or `continue` — in last-in, first-out order. A
`return` value is computed before deferred code runs. (Deferred code does not run when
an exception propagates; use `finally` for that.)

```grav
class Conn {
    constructor() { print("open") }
    destructor() { print("close") }
}

fn query(id: int) -> string {
    let c = new Conn()
    defer free(c)                     // always closes, whichever return is taken
    if (id < 0) { return "bad id" }
    return "row ${id}"
}
```

**Strings are freed automatically** where the compiler can prove it is safe. A local
`string` variable is *owned* when every use of it only reads or copies its characters
(it is part of a `+`/`${}` chain, printed, compared, used as a `switch` subject, or
passed to a known non-retaining call such as `IO.print`, `Str.length`, or
`StringBuilder.append`). An owned variable frees its old value on reassignment
(including `s += x`) and its final value when its block exits by any path, but only
values the function itself allocated (literals and borrowed strings are never
freed). Temporaries passed to those calls or compared with `==` are freed right
after use. A variable that is returned, stored in a field/collection, captured by a
lambda, or passed to other code is left alone (it may be shared), as is everything in
a function that contains inline C.

```grav
fn main() {
    let report = ""
    for (i in 0..1000) {
        let line = "item ${i}"           // freed at the end of every iteration
        report += line + "; "            // the previous `report` is freed each time
    }
    IO.print("done: " + report)          // the temporary is freed after the call
}                                        // `report` is freed here
```

Ownership also crosses function boundaries: a function whose every `return` yields a
new string (a `+`/`${}` result, a call to another such function, or one of its own
owned locals) hands that string to the caller, so `let s = banner("x")` is freed like
any owned local, a discarded `banner("x");` result is freed immediately, and fresh
strings used as pieces of a `+` chain or `print` are freed right after the statement.
Inline C only disqualifies the variables it actually names.

Closures follow the same rules: a local function value that is only ever called has
its captured environment freed at scope exit, and a capturing lambda passed directly
to `map`/`filter`/`forEach`/`reduce`/`sortBy`/… is freed after the call.

Strings or closures that escape (stored in objects or collections, returned from a
function that sometimes returns a borrowed string) are not reclaimed; for heavy text
building, `StringBuilder` (lib/strbuf.grav) avoids the repeated copies.

### Lambdas & function types

Functions are values. A function type is written `fn(A, B) -> R` (`-> R` omitted for
`void`). A lambda is `fn(x: T) => expr` or `fn(x: T) -> R { ... }`; parameter types
may be left out when the expected type is known (an argument, a typed `let`, a field
assignment, a `return`). Named functions can be used as values too.

```grav
fn square(x: int) -> int { return x * x }
fn twice(f: fn(int) -> int) -> fn(int) -> int { return fn(x) => f(f(x)) }

fn main() {
    let k = 10
    let addK = fn(x: int) => x + k        // captures k
    print(addK(5))                        // 15
    print(twice(square)(3))               // 81
    let g: fn(int) -> int = square        // a named function as a value
    let nums = new Array<int>()
    for (n in [5, 3, 8]) { nums.push(n) }
    print(nums.filter(fn(n) => n > 4).map(fn(n) => n * 10).reduce(0, fn(a, b) => a + b))  // 130
}
```

Lambdas **capture by value**: outer variables are copied into the closure when it is
created, so assigning to them inside the lambda is a compile error. Share mutable
state through an object (`cell.value = cell.value + 1`). `self` can be captured inside
methods. Function values can be stored in fields and collections (`Array<fn(string)>`),
compared with `null`, and called like functions (`self.onClick(label)`); calling a null
function value stops the program with an error. A closure's captured environment is
heap-allocated; `free(f)` releases it.

`Array<T>` has `forEach`, `forEachIndexed`, `map`, `filter`, `reduce`, `any`, `all`,
`countWhere`, `findIndex`, `copy`, a stable `sortBy(cmp)`, and the generic methods
`mapTo::<U>(f)` and `fold::<A>(init, f)` that change the element type.

**Generic methods.** A method can declare its own type parameters
(`fn map<U>(f: fn(T) -> U) -> Box<U>`) — on generic or plain classes, instance or
static. Call it with explicit type arguments: `box.map::<string>(fn(v) => str(v))`,
`Conv.pair::<int, string>(1, "a")`. Each distinct argument list is compiled once.

### Method overloading

Methods (instance and static) and operator methods can be overloaded by parameter
types. The best match is chosen at compile time: an exact match wins, otherwise the
single candidate whose parameters accept the arguments; otherwise the call is
reported as ambiguous or unmatched. A subclass can override one overload and add new
ones; overriding is by exact signature. Constructors overload the same way
(`new Color()`, `new Color(128)`, `new Color(255, 64, 0)`), and `super(...)` picks
the matching parent constructor.

```grav
class Fmt {
    static fn show(x: int) -> string { return "int ${x}" }
    static fn show(s: string) -> string { return "string ${s}" }
    static fn show(a: int, b: int) -> string { return "pair ${a},${b}" }
}
class Vec2 {
    public x: float  public y: float
    constructor(x: float, y: float) { self.x = x; self.y = y }
    fn operator*(k: float) -> Vec2 { return new Vec2(self.x * k, self.y * k) }
    fn operator*(o: Vec2) -> float { return self.x * o.x + self.y * o.y }
}
```

### `final`

`final class C` cannot be extended and `final fn m()` cannot be overridden. Besides
documenting intent, it guarantees the compiler can call such methods directly.

### Null, `??` & optional chaining

`null` is the null reference, assignable to any class, interface, or pointer type
and compared with `==`/`!=`. (There is no `null` for value types — `int`, `bool`,
etc.) Two operators build on it:

- **`a ?? b`** — null-coalescing: `a` unless it is null, otherwise `b`. It lowers
  at compile time to a plain "if null then `b` else `a`" conditional.
- **`a?.m()` / `a?.field`** — optional chaining: guards a null receiver, yielding a
  zero value (`0` / `""`) when `a` is null. The result must be a scalar, enum, or
  reference (not a value-type struct or interface).

```grav
fn main() {
    let s: Shape = null
    print(s == null)                 // true
    let shape = s ?? new Circle(3)   // falls back when s is null
    print(shape.area())

    let maybe: Circle = null
    print(maybe?.area())             // 0  (guarded; no null deref)
}
```

### Casts & type tests (`as` / `is`)

Grav has three cast spellings — use whichever reads best:

| Spelling | Example |
| --- | --- |
| Call | `int(x)`, `float(x)`, `bool(x)`, `string(x)` |
| `as` | `x as int`, `c as Shape`, `p as Point*` |
| C-style | `(int)x`, `(Shape)c`, `(Point*)p` |

All cast between numeric types, enum→`int`, within a class hierarchy, class→interface,
and between pointer types. `expr is ClassName` is a runtime type test (RTTI) → `bool`.

```grav
fn main() {
    let c = new Circle(2)
    let s = c as Shape               // class -> interface
    print(s.area())
    print(c is Circle)               // true
    print((int)3.9)                  // 3
}
```

### Pointers

Pointer types are written `T*` (`int*`, `Point**`). `&x` takes the address of a
variable, field, or dereference; `*p` dereferences (and is assignable); `p->field`
is sugar for `(*p).field`. Pointers compare against `null` and lower directly to C
pointers, so they give real memory access.

```grav
fn bump(p: int*) { *p = *p + 1; }   // write through the pointer

fn main() {
    let n = 41;
    let p: int* = &n;
    bump(p);
    *p = *p + 1;
    print(n);                        // 43

    let pt = Point { x: 3, y: 4 };
    let pp: Point* = &pt;
    pp->x = 100;                     // (*pp).x = 100
    print(pt.x)                      // 100
}
```

### Async / await

`async fn` marks a function whose calls run **concurrently** on a pool of worker
threads; a call returns a `Future<T>` at once, and `await` waits for the result
(re-throwing any exception the task raised). The pool has `GRAV_MAXPROCS` workers —
by default the number of CPU cores; set the `GRAV_MAXPROCS` environment variable or
call `Async.setMaxProcs(n)` to change it. A task awaited before a worker picks it up
runs on the awaiting thread, so tasks can await tasks without deadlocking even with
one worker.

```grav
import "lib/std.grav"

async fn countPrimes(lo: int, hi: int) -> int {
    let n = 0
    for (x in lo..hi) {
        let p = x > 1
        for (d in 2..x) { if (d * d > x) { break } if (x % d == 0) { p = false; break } }
        if (p) { n++ }
    }
    return n
}

fn main() {
    let parts = new Array<Future<int>>()
    for (k in 0..4) { parts.push(countPrimes(k * 50000, (k + 1) * 50000)) }   // run in parallel
    print(awaitAll::<int>(parts).reduce(0, fn(a, b) => a + b))              // 17984
}
```

Share data between tasks through `Mutex`, `AtomicInt`, and `Channel<T>`
(`lib/async.grav`); the runtime itself (exceptions, stack traces, library buffers) is
thread-safe, and `--race` builds the program with ThreadSanitizer to catch unsafe
sharing in your code. See [Concurrency](docs/learn/18-concurrency.md) and
[16_async.grav](examples/16_async.grav).

### Structs

A `struct` is a plain **value type**: named fields, no methods, no inheritance,
no vtable, and no runtime type info. It lowers to a bare C `struct` and is
passed, returned, and assigned **by value** (copied).

```grav
struct Point {
    x: int
    y: int
}

struct Rect {
    origin: Point      // structs may nest other structs (stored by value)
    width: int
    height: int
}

fn main() {
    let p: Point = Point { x: 3, y: 4 }   // struct literal
    print(p.x)                            // 3
    p.y = 10                              // fields are mutable

    let q = p                             // copy
    q.x = 99
    print(p.x)                            // 3  (p is unaffected)

    let r = Rect { origin: p, width: 5, height: 6 }
    print(r.origin.y)                     // 10
}
```

A struct literal must list **every** field exactly once. A struct may hold class
references (pointers), but a struct cannot contain itself by value (that would be
infinitely large — use a class for recursive shapes).

### Arrays

A `T[N]` is a **fixed-length array** of `N` elements of type `T`. Like structs,
arrays are **value types**: they are copied on assignment and passed / returned by
value (Grav boxes them in a small C struct so this stays sound — no hidden runtime,
no heap). The element type may be any type (primitive, struct, class reference…).

```grav
fn sum(xs: int[3]) -> int {           // arrays pass by value
    let total = 0
    for (i in 0..xs.length) {         // `.length` is the compile-time length
        total += xs[i]                // index to read…
    }
    return total
}

fn main() {
    let primes: int[4] = [2, 3, 5, 7] // an array literal
    print(primes.length)              // 4
    print(primes[2])                  // 5
    primes[0] = 11                    // …and to write
    print(primes[0])                  // 11

    let row = [10, 20, 30]            // element type & length inferred -> int[3]
    print(sum(row))                   // 60
}
```

The length is part of the type, so `int[3]` and `int[4]` are distinct types. A
literal's length must match the declared length, and `[]` (empty) needs an explicit
`T[N]` annotation. Indexing also works on pointers (`p[i]`), which are **not**
bounds-checked.

### Type aliases

`type Name = T` introduces a **transparent alias** for an existing type. Aliases are
fully expanded during type checking, so `Celsius` below *is* `int` everywhere — they
cost nothing and leave no trace in the generated C.

```grav
type Celsius = int
type Row     = int[3]
type Coord   = Point          // alias any named type, array, or pointer

fn freezing(c: Celsius) -> bool { return c <= 0 }

fn main() {
    let r: Row = [1, 2, 3]
    print(freezing(0 as Celsius)) // true
}
```

Aliases may be used wherever a type is expected (declarations, parameters, fields,
return types, `as` casts, `sizeof`). They are transparent, so an alias and its
target are interchangeable.

### Macros

A C-style preprocessor runs over the token stream before parsing. `#define` supports
both **object-like** and **function-like** macros; each directive occupies one line
and disappears from the output.

```grav
#define SIZE 8
#define SQUARE(x) ((x) * (x))

fn main() {
    let buf: int[SIZE] = [0, 0, 0, 0, 0, 0, 0, 0]  // SIZE is usable as a type length
    print(buf.length)        // 8
    print(SQUARE(SIZE + 1))  // 81  (parenthesize args defensively, as in C)
}
```

Function-like invocations must supply the exact arity; arguments are expanded in the
caller's context, and a macro will not recursively expand into itself.

### sizeof

`sizeof(T)` (a type) and `sizeof(expr)` (a value) yield the size in bytes as an
`int`, lowering to C's `sizeof`. For a class or struct it reports the underlying
object/value size.

```grav
fn main() {
    print(sizeof(int) > 0)                       // true
    let primes: int[4] = [2, 3, 5, 7]
    print(sizeof(primes) == sizeof(int) * 4)     // true
}
```

### Generics

`struct`, `class`, `interface`, `fn`, and `type` may take type parameters `<T, …>`.
Each distinct instantiation is **monomorphized** into its own concrete declaration
in the generated C (no runtime type erasure). Type arguments are explicit: `Box<int>`
in type positions and the turbofish `id::<int>(x)` on a call. Generic inheritance
(`extends Base<T>`, `implements Iface<T>`), generic aliases (`type Vec<T> = T[3]`),
and `T: Bound` constraints are all supported. See
[08_generics.grav](examples/08_generics.grav) and
[08_generics.grav](examples/08_generics.grav).

```grav
class Box<T> {
    private value: T
    constructor(v: T) { self.value = v; }
    public fn get() -> T { return self.value; }
}

interface Named { fn name() -> string }
fn greet<T: Named>(x: T) -> string { return x.name(); }  // constrained

fn identity<T>(x: T) -> T { return x; }
type Vec<T> = T[3]

fn main() {
    let b = new Box<int>(42);
    print(b.get());                       // 42
    print(identity::<string>("hi"));      // hi
    let v: Vec<int> = [1, 2, 3];
    print(v.length);                      // 3
}
```

### Variadics

A function's last parameter may be variadic (`...name: T`), collecting the trailing
arguments into a runtime-length slice: `name.length` is the count and `name[i]`
indexes elements. It lowers to a `(int count, T* ptr)` pair with a C compound
literal at each call — no heap allocation. See [03_functions.grav](examples/03_functions.grav).

```grav
fn sum(...nums: int) -> int {
    let total = 0;
    for (i in 0..nums.length) { total += nums[i]; }
    return total;
}

fn main() {
    print(sum());            // 0
    print(sum(1, 2, 3, 4));  // 10
}
```

### Exceptions

`throw` raises a class instance; `try { } catch (e: Type) { }` catches it (matching
the type or any subclass). A `try` may have several `catch` clauses, tried in order
(a clause made unreachable by an earlier, broader one is a compile error);
`finally { }` always runs — on normal completion, after a
handled exception, while an unhandled one propagates, when a handler itself throws,
and when `return`, `break`, or `continue` leaves the `try`/`catch`. It lowers to a `setjmp`/`longjmp` runtime (locals
written inside `try` are made `volatile` so they keep their values in `catch` at any
optimization level); an uncaught exception prints a message and the stack trace of
the original `throw`, then exits. See [10_exceptions.grav](examples/10_exceptions.grav).

```grav
class AppError { public msg: string  constructor(m: string) { self.msg = m; } }

fn main() {
    try {
        throw new AppError("boom");
    } catch (e: AppError) {
        print("caught: ${e.msg}");        // caught: boom
    } finally {
        print("cleanup");                 // cleanup
    }
}
```

### Globals & static fields

A module-level `const`/`let` is a global; a class `static x: T = value` is a
class-level field accessed as `Class.x`. Both need an explicit type and a constant
initializer, and lower to C globals. `const`/`readonly` ones are immutable. See
[05_classes.grav](examples/05_classes.grav).

```grav
const PI: float = 3.14159
let hits: int = 0

class Config { static version: int = 1 }

fn main() {
    hits += 1;
    print(PI * 2.0);
    Config.version = 2;
    print(Config.version);                // 2
}
```

### Classes

```grav
class User {
    public id: int
    public name: string

    constructor(id: int, name: string) {
        self.id = id
        self.name = name
    }

    public fn greet() -> string {
        return "Hello " + self.name
    }
}

let u = new User(1, "John")        // objects are heap-allocated
print(u.greet())
```

`self` (or `this`) refers to the current instance inside methods/constructors.

### Access modifiers & `readonly`

`public` (default), `private`, `protected`, and `readonly` are enforced **at compile
time**:

```grav
class BankAccount {
    private balance: int
    public readonly owner: string

    constructor(owner: string) { self.owner = owner  self.balance = 0 }
    public fn deposit(n: int) -> void { self.balance = self.balance + n }
}
// outside the class:
//   acct.balance      -> error: 'balance' is private
//   acct.owner = "x"  -> error: cannot assign to readonly field outside its constructor
```

### Auto getters/setters

For every **private** field, Grav synthesizes `get_<field>()` and `set_<field>(v)`
methods (the setter is omitted for `readonly` private fields). You can still write your
own method with that name to override the default.

```grav
class Person { private name: string  constructor(n: string){ self.name = n } }

let p = new Person("Al")
print(p.get_name())       // Al
p.set_name("Bo")
```

### Properties: `get` and `set`

A property reads like a field and runs code: `get name() -> T` when it is read,
`set name(v: T)` when it is assigned (`+=` and `++` use both). Without a setter
it is read-only. Accessors are methods underneath, so they can be `static`,
private, overridden (`super.name`), abstract, and declared in interfaces and
traits; reflection lists them with `ClassInfo.properties()`.

```grav
class Temperature {
    private c: float = 0.0
    get celsius() -> float { return self.c }
    set celsius(v: float) { self.c = v }
    get fahrenheit() -> float { return self.c * 9.0 / 5.0 + 32.0 }
}

let t = new Temperature()
t.celsius = 100.0
print(t.fahrenheit)       // 212
t.celsius += 1.0
```

### Inheritance & polymorphism

Single base class via `extends`. Methods dispatch through a **vtable**, so overrides are
called through a base-typed reference (true runtime polymorphism):

```grav
class Animal { public fn speak() -> void { print("...") } }
class Dog extends Animal { public fn speak() -> void { print("woof") } }

let a: Animal = new Dog()
a.speak()                 // woof
```

### Abstract classes

`abstract class` cannot be instantiated; `abstract fn` has no body and must be
implemented by every concrete subclass.

```grav
abstract class Shape {
    abstract fn area() -> int
    public fn kind() -> string { return "shape" }
}
class Square extends Shape {
    public side: int
    constructor(s: int) { self.side = s }
    public fn area() -> int { return self.side * self.side }
}
```

### Interfaces

Interfaces are method contracts. A class may implement **several**. Interface-typed
values dispatch through a per-class itable (fat pointer), so this works:

```grav
interface Shape { fn area() -> int }
interface Named { fn label() -> string }

class Box implements Shape, Named {
    public w: int
    constructor(w: int) { self.w = w }
    public fn area() -> int { return self.w * self.w }
    public fn label() -> string { return "box" }
}

let s: Shape = new Box(4)
print(s.area())           // 16  (dispatched through the interface)
```

### Static methods

```grav
class Math {
    static fn add(a: int, b: int) -> int { return a + b }
}
print(Math.add(2, 3))     // 5
```

### Namespaces

Namespaces group declarations and may be nested or dotted. Reference members with a
qualified name; within a namespace, sibling names resolve unqualified.

```grav
namespace zoo {
    class Animal { public fn speak() -> void { print("...") } }
    class Dog extends Animal { public fn speak() -> void { print("woof") } }
}

fn main() {
    let a: zoo.Animal = new zoo.Dog()
    a.speak()
}
```

### Imports

`import "path"` textually pulls another file's declarations in **before** the current
file. Paths are resolved relative to the importing file; each file is included at most
once (cycle-safe).

```grav
// app.grav
import "shapes.grav"
fn main() { let s: Shape = new Square(6)  print(s.area()) }
```

A runnable, multi-file example lives in [`examples/project/`](examples/project/):
`main.grav` imports `util/text.grav` and `model/shapes.grav` (which itself imports
`util/text.grav` — a transitive import), uses their nested namespaces, and re-imports
a module to show that duplicate imports are a safe no-op.

```bash
./build/gravc examples/project/main.grav --run
```

### Runtime type info (RTTI)

Every object carries a type descriptor. Two built-ins query it:

```grav
let a: zoo.Animal = new zoo.Dog()
print(typename(a))            // "zoo.Dog"
print(isInstance(a, zoo.Dog)) // true
print(isInstance(a, zoo.Animal)) // true  (walks the base chain)
```

### Built-ins

| Built-in                  | Description                                                  |
|---------------------------|--------------------------------------------------------------|
| `print(x)`                | Prints a value and a newline: primitives, enums (member name), and any class/interface with `toString() -> string`. |
| `str(x)`                  | Converts the same set of values to a `string`.               |
| `typename(obj)`           | Returns the runtime class name as a `string`.                |
| `isInstance(obj, Type)`   | `true` if `obj` is a `Type` or a subclass of it.             |
| `input()`                 | Reads one line from stdin (without the newline) as a `string`.|
| `argc()`                  | Number of command-line arguments (incl. the program name).   |
| `argv(i)`                 | The `i`-th command-line argument as a `string` (`""` if OOB). |
| `free(obj)`               | Runs the object's destructor chain, then releases it (no GC; see [Memory](#memory-destructors--defer)).|

### String interpolation

Inside a string literal, `${ expr }` splices a value, converted with `str()`, so
any printable type works. Write `\$` for a literal dollar sign.

```grav
fn main() {
    let name = "Ada"; let n = 42;
    print("hello ${name}, answer = ${n}, doubled = ${n * 2}");
    // hello Ada, answer = 42, doubled = 84
}
```

### Command-line arguments & stdin

```grav
fn main() {
    print("got ${argc()} args; first is ${argv(0)}");
    let line = input();              // read a line from stdin
    print("you said: ${line}");
}
```

### Inline C (`%{ … %}`)

An escape hatch for dropping to C. It works in three positions, and Grav values
and C values flow both ways. (Use sparingly — it shrinks as the language grows.)

- **Top level** — verbatim C for `#include`s, globals, and helper functions.
- **Statement** — raw C statements that can read/write Grav locals by name.
- **Expression** — `%{ c_expr %} as T` (or in a typed `let`) yields a value of
  type `T`, reading C globals/locals or calling C functions.

```grav
%{
static int c_total = 0;
int c_add(int a, int b) { c_total++; return a + b; }
%}

fn main() {
    let n = 5;
    %{ printf("C sees n=%d\n", n); %};       // Grav local -> C
    let sum: int = %{ c_add(2, 3) %} as int; // C result -> Grav
    print("sum=${sum} calls=${%{ c_total %} as int}");
}
```

### Decorators & `export`

`@Name` decorators and an `export` modifier may precede any top-level declaration.
They are accepted as metadata (every top-level name is already visible across
modules), so they document intent without changing behaviour.

```grav
@Entity
@table("users")
export class User { public name: string }
```

### Comments

```grav
// line comment
/* block
   comment */
```

### Diagnostics & warnings

Errors carry a `[line:col]` location and are grouped by stage (`lex` / `parse` /
`sema` / `type`). Unused local variables produce a `warning`; pass `-Werror` to make
warnings fatal.

```
gravc: type error [3:16]: operator '+' requires matching types, but got int and float
gravc: warning [2:5]: unused variable 'y'
```

---

## How Grav lowers to C

| Grav feature        | C output model                                                       |
|---------------------|---------------------------------------------------------------------|
| struct              | a plain `struct` (value type); passed/returned/copied by value      |
| struct literal      | a C compound literal `(Point){ .x = 1, .y = 2 }`                    |
| class               | `struct` with a leading `__vt` (vtable) and `__type` (RTTI) header   |
| field               | struct member; inherited fields are flattened base-first            |
| instance method     | `Class_m_name(Class* self, …)`, dispatched via the vtable            |
| static method       | `Class_m_name(…)` (no `self`)                                        |
| constructor         | `Class_new(…)` — `calloc`s, sets `__vt`/`__type`, runs the body     |
| inheritance         | struct prefix compatibility + a layout-compatible per-class vtable   |
| interface           | `GravIface` fat pointer `{ obj, itable }`; one itable per class×iface|
| abstract method     | a vtable slot with no implementation (class can't be instantiated)   |
| RTTI                | a per-class `GravTypeInfo { name, base }` reachable from every object|
| string `+`          | `grav_str_concat` runtime helper                                    |
| namespaces          | name mangling (`a.b.C` → `a__b__C`)                                 |

See [`src/codegen/README.md`](src/codegen/README.md) for the gory details.

---

## Project layout

```
.
├── main.cpp                 # gravc driver: args, imports, pipeline, --emit
├── CMakeLists.txt
├── examples/                # per-section programs, project/ (multi-file), apps/
├── lib/                     # standard library (collections, strings, math, fs, datetime, …)
├── docs/                    # the guide (learn/), reference/, std/ (one page per module), tools/
├── tests/run.sh             # build + run every example against tests/expected/*.out
├── tests/docs.sh            # compile + run every snippet in docs/ and check its output
├── editors/                 # Neovim, VS Code, and JetBrains/CLion integrations
├── mcp_server/              # dependency-free MCP server wrapping gravc
└── src/
    ├── common/   # shared types (TypeRef) and diagnostics
    ├── lexer/    # source text -> tokens
    ├── ast/      # AST node definitions
    ├── parser/   # tokens -> AST (recursive descent)
    ├── sema/     # symbol tables, name resolution, type checking
    └── codegen/  # AST -> C (structs, vtables, itables, RTTI)
```

Every subfolder has its own `README.md` describing that stage. No source file exceeds
~700 lines.

The compilation pipeline:

```
source ──load(imports)──▶ Lexer ──▶ Parser ──▶ Registry (symbols) ──▶ TypeChecker ──▶ CodeGen ──▶ C ──(cc)──▶ binary
```

---

## Building the compiler

Requirements: CMake ≥ 3.20 and a C++20 compiler. For `--emit bin`, a C compiler on
`PATH` (or `$CC`).

```bash
cmake -S . -B build
cmake --build build
./build/gravc --help        # prints usage
```

---

## Editor support

Integrations for three editors live under [`editors/`](editors/), plus a shared,
dependency-free **language server** ([`editors/lsp/`](editors/lsp/)) that gives
every editor the same go-to-declaration, find-references, rename, document-symbol,
hover, and compiler-diagnostics features.

- **Neovim** ([`editors/nvim/`](editors/nvim/)) — a LazyVim-ready setup: syntax
  highlighting (keywords, `${}` interpolation, `%{ %}` C blocks, decorators),
  filetype detection, `<leader>mr` / `<leader>mc` build-and-run keymaps, and
  built-in **LSP** (auto-starts the Grav server for navigation/rename; a pure-Lua
  go-to-definition fallback works without node). It also registers the
  [`gravc` MCP server](mcp_server/) with `mcphub.nvim`, which installs the latest
  build as `grav` in `~/.local/bin` (override with `GRAV_DIR`). See
  [`editors/nvim/README.md`](editors/nvim/README.md).
- **VS Code** ([`editors/vscode/`](editors/vscode/)) — a full extension: TextMate
  grammar, language configuration, snippets, *compile / compile&run* commands, and
  **navigation/refactoring** (definition, references, rename, outline, hover) via
  the shared indexer. See [`editors/vscode/README.md`](editors/vscode/README.md).
- **JetBrains / CLion** ([`editors/jetbrains/`](editors/jetbrains/)) — a TextMate
  bundle for instant highlighting (Settings → Editor → TextMate Bundles → add the
  `textmate/` folder), plus a complete Gradle IntelliJ-Platform plugin project
  (JDK 17+) that adds highlighting **and** LSP-backed navigation/refactoring. See
  [`editors/jetbrains/README.md`](editors/jetbrains/README.md).

All editors reuse the same TextMate grammar and symbol index, so highlighting and
navigation stay consistent.

### Install & test locally

- **VS Code** — package and install the extension (modern VS Code ignores
  unregistered folders/symlinks in `~/.vscode/extensions/`):
  ```bash
  cd editors/vscode
  npx --yes @vscode/vsce package --no-dependencies
  code --install-extension grav-lang-0.9.0.vsix
  ```
  Reload the window (`Ctrl+Shift+P → Developer: Reload Window`), open a `.grav`
  file, and try `F12` (definition), `Shift+F12` (references), `F2` (rename), and
  `Ctrl+Alt+R` (compile & run).
- **Neovim** — copy the runtime files (`syntax`, `ftdetect`, `ftplugin`,
  `lua/grav`) onto your `runtimepath`; LSP starts automatically when editing
  inside the repo. `gd` / `gr` / `grn` / `K` / `<leader>mr`.
- **CLion** — for highlighting now, add the TextMate bundle
  (Settings → Editor → TextMate Bundles → **+** → `editors/jetbrains/textmate`).
  For navigation/refactoring, build and install the plugin (JDK 17+, needs Node on
  `PATH`).

---

## Roadmap / not yet implemented

`v1.5` reworked the **standard library**: one set of conventions across every
collection (`size`, `isEmpty`, `clear`, `contains`, `forEach`, `toArray`, copies from
`keys()`), new data structures (`Deque`, `HashSet`, `PriorityQueue`, `LinkedList`,
`SortedMap`, `BitSet`, `Pair`/`Triple`, `Optional`), O(1) `Queue` and a one-allocation
`Str.join` (100–1000× faster), a portable per-thread xoshiro `Random` with inclusive
ranges, `Dir`, `Process.run`, number formatting (`Format.fixed`, `grouped`, `bytes`),
`DateTime.of`, many new `Str`/`Math` functions, and fixes (`Process.exec` exit codes,
`Str.charAt` bounds, 64-bit file sizes, binary-safe `File.copyTo`, `Map.keys()` copies).
See [18_data_structures.grav](examples/18_data_structures.grav).

`v1.4` added **`auto`** — generic type arguments are inferred from call arguments,
lambda results, constructor arguments, and expected types (`maxOf(3, 9)`,
`new Box(5)`, `xs.mapTo(fn(n) => "#${n}")`, `self.items = new Array<auto>()`) — and
**`any`**, a dynamic value that holds any primitive, string, enum, object, or
function, tested with `x is T` and unpacked with a checked `x as T`. See
[17_auto_any.grav](examples/17_auto_any.grav).

`v1.3` made **async/await concurrent**: async calls run on a pool of `GRAV_MAXPROCS`
worker threads (default: CPU cores, overridable from the environment), `await`
works anywhere and re-throws task exceptions, `Future<T>` is a first-class type, the
runtime and library are thread-safe, and `lib/async.grav` adds `Mutex`, `AtomicInt`,
`Channel<T>`, `awaitAll`, and pool controls. New `--race` (ThreadSanitizer) and
`--sanitize` (AddressSanitizer + UBSan) build flags check programs for data races and
memory errors.

`v1.2` also defined **left-to-right evaluation** (call arguments, receivers, and
the pieces of `+`/`${}` strings — C leaves argument order unspecified), multiple
`catch` clauses, `==` on structs (member-wise), interfaces and functions (identity),
and `binary` (bytes), integer-to-enum casts (`2 as Suit`), `sizeof` of any built-in
type name, mixed-width compound assignment (`int += byte`), C-safe renaming of locals
named like C keywords, and fixed generic instantiation inside namespaces. The
examples were rewritten — see [examples/README.md](examples/README.md).

`v1.2` added **constructor overloading**, **generic methods** (`fn map<U>(…)`, called
as `obj.map::<U>(…)`), caller-owned string results (functions that always return a
fresh string), statement-level freeing of temporary strings, automatic freeing of
local closures and of capturing lambdas passed to collection methods, and narrower
inline-C handling in the ownership analysis.

`v1.1` added **lambdas and first-class functions** (`fn(A) -> R` types, closures that
capture by value, named functions as values, function-typed fields), **higher-order
collection methods** (`map` / `filter` / `reduce` / `forEach` / `any` / `all` /
`sortBy` / … plus generic `mapTo` / `fold`), **method overloading** (instance,
static, and operator methods), and **automatic freeing of owned strings** (see
[Memory](#memory-destructors--defer)). See
[09_lambdas.grav](examples/09_lambdas.grav).

`v1.0` added **for-each loops** (`for (x in xs)`, `for (i, x in xs)`), range **`step`**,
**`defer`**, **destructors** run by `free`, **operator overloading** (arithmetic,
comparison, `[]`/`[]=`), **`toString()`** integration with `print`/`str`/interpolation,
enum values printing as their member names, the **`**`** power operator, **`final`**
classes and methods, string escapes `\r \0 \' \e \xHH \u{…}`, and a missing-return
check. Codegen now devirtualizes non-overridden calls, fuses string building into one
allocation, makes everything `static`/`const`, and builds at `-O2` by default
(`--release` for `-O3` without tracing). New libraries: `StringBuilder`, `HashMap<K,V>`,
`OS`, `Time`/`Stopwatch`, and `Term`. Fixes: `finally` now runs on `return`/`break`/
`continue`, exception handling is correct at `-O2`, `break`/`continue` out of a `try` no
longer corrupt the handler stack, a re-thrown exception keeps its original stack trace,
method calls on temporaries evaluate the receiver once, errors inside `${…}` report the
right line, `DateTime` is 2038-safe, and `Array`/`Map` accesses are bounds-checked. See
[02_control_flow.grav](examples/02_control_flow.grav).

`v0.9` widened the **numeric types** (`long`/`int64`, `uint`/`unsigned`/`ulong`,
`byte`, `double`/`float64`, `float32`, with C-like width promotion), added a
**`binary`** blob type, `typedef` (an alias for `type`), an object-oriented
standard library (an **instance `File`** class, an **`IO`** class, and `Format`
type conversions), plus **`super`** (`super(...)` to run the parent constructor and
`super.method(...)` to call a parent's implementation) and **free-function
overloading** (same name, different parameter signatures) — see
[01_basics.grav](examples/01_basics.grav) and
[07_overloading.grav](examples/07_overloading.grav). (Method overloading
followed in v1.1.)

`v0.8` added the three multiple-inheritance forms (see
[06_inheritance.grav](examples/06_inheritance.grav)): **traits**
(`trait` interfaces with default method bodies + `Self`), **composition** (`uses
field: T`, auto-forwarding), and **true multiple inheritance** (`extends A, B`,
flattening fields/methods with multi-base RTTI). `v0.7` completed **generics** —
generic classes/interfaces, generic inheritance (`extends Base<T>`), generic type
aliases, and `T: Bound` constraints (see
[08_generics.grav](examples/08_generics.grav)) — and added **variadics**
(`fn f(...xs: T)`, see [03_functions.grav](examples/03_functions.grav)) and **exceptions**
(`try`/`catch`/`throw`/`finally`, see [10_exceptions.grav](examples/10_exceptions.grav)).
`v0.6` added generic structs/functions and **globals + static fields**. `v0.5`
added [string interpolation](#string-interpolation), the
[`str`/`input`/`argc`/`argv`](#built-ins) built-ins, an
[inline-C escape hatch](#inline-c---) (`%{ … %}`), and
[decorators + `export`](#decorators--export). `v0.4` added fixed-length
[arrays](#arrays), transparent [type aliases](#type-aliases), a `#define`
[macro](#macros) preprocessor, and [`sizeof`](#sizeof). `v0.3` added enums, the
expanded operator set, ranges, `null` + `??` + `?.`, `as`/`is` + C-style casts,
pointers, optional `;` terminators, and async/await. The following are **not**
implemented yet — using them is a parse/type error today:

- **Collection literals** — set/map literals like `#{1, 2, 3}` are not parsed yet.
  The collection *types* now ship in [`lib/`](lib/): growable `Array<T>`, `List<T>`,
  `Stack<T>` / `Queue<T>`, `Set<T>` (union/intersect), and `Map<K,V>` — built on
  generic classes plus a small inline-C runtime (`realloc` buffers). Construct them
  with `new Array<int>()` etc.; only the literal syntax is outstanding. Fixed-length
  `T[N]` arrays *are* implemented — see [Arrays](#arrays).
- **Lazy iteration** — `Iterator` / `Generator` / `Stream` are not implemented
  (lambdas and eager `map` / `filter` / `reduce` are). Generic type arguments are
  inferred from arguments and expected types, but not from later uses of a value
  (`let xs = new Array<auto>()` with no initializer type cannot be inferred).
- **Garbage collection** — there is no collector: objects are released with `free` /
  destructors, and strings/closures are freed automatically only where the compiler
  proves ownership (see [Memory](#memory-destructors--defer)); values that escape into
  objects or collections are not reclaimed.
- **Reflection** — and the libraries that depend on it (JSON, a general-purpose
  serializer) are not implemented yet (`any` now covers dynamic values).
  `Option<T>` / `Result<T,E>` exist only as much as plain generics allow — there is
  no pattern-match binding (`match`).
- **Networking** — HTTP and sockets are not in the standard library yet (use inline C).

What *is* in the standard library now (see [lib/README.md](lib/README.md)): the
collections above plus `HashMap<K,V>`, `StringBuilder`, an expanded `Str`
(trim/replace/split/join/pad/…), floating-point `Math` (`<math.h>`), `Random`,
`DateTime` (`<time.h>`), `Time`/`Stopwatch`, `File` (`fs`), `Path`, `Process`
(`popen`/`system`), `OS`, and `Term` (ANSI styling).

A note on [multiple inheritance](examples/06_inheritance.grav): the primary
base in `extends A, B` keeps prefix layout (full polymorphism through `A`), while
secondary bases are flattened (fields + methods copied in) with multi-base RTTI. For
polymorphism through a *secondary* base, use an interface or `trait`.

Contributions and design notes welcome — start from the per-folder `README.md` files.
