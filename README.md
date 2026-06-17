# Grav

**Grav** is a statically typed, compiled, TypeScript-inspired language for backend and
systems work. It compiles to portable **C** (and, with one flag, straight to a native
binary). The compiler — `gravc` — is written in C++20.

> Grav gives you a TypeScript-like OOP surface (classes, interfaces, access modifiers,
> generics-to-come) but lowers everything to a predictable, C-level execution model:
> structs, vtables, and compile-time type checking with **no hidden runtime**.

---

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
- [Editor support (Neovim)](#editor-support-neovim)
- [Project layout](#project-layout)
- [Building the compiler](#building-the-compiler)
- [Roadmap / not yet implemented](#roadmap--not-yet-implemented)

---

## Quick start

```bash
# build the compiler
cmake -S . -B build && cmake --build build

# transpile a program to C
./build/gravc examples/10_inheritance.grav            # writes examples/shapes.c

# …or compile straight to a native binary and run it
./build/gravc examples/bank.grav --emit bin -o /tmp/animals
/tmp/animals
```

```grav
// examples/10_inheritance.grav
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
| `-O0`…`-O3`, `-Os`           | Optimization level (passed to the C compiler).                 |
| `-g`, `-g3`                  | Include debug info.                                            |
| `--cc <compiler>`            | C compiler to use (default `$CC`, else `cc`).                  |
| `-Werror`                    | Treat warnings (e.g. unused variables) as fatal.              |
| `-v`, `--verbose`            | Print the C compiler command.                                  |
| `-h`, `--help`               | Show usage.                                                    |
| `-`                          | Read from stdin and print C to stdout.                         |

For `asm`/`bin`/`both`, `gravc` invokes the system C compiler (`$CC`, default `cc`)
as `cc -std=c11 [opts] <file>.c -o <out>`.

---

## Language tour

> **See it all at once:** [`examples/comprehensive.grav`](examples/comprehensive.grav)
> is a single runnable program that exercises (almost) every feature below —
> macros, type aliases, enums, structs, arrays, pointers, classes/interfaces,
> the full operator set, control flow, `null`/`??`/`?.`, `as`/`is`, async/await,
> and `sizeof`. Build and run it with:
>
> ```bash
> ./build/gravc examples/comprehensive.grav --emit bin -o /tmp/comp && /tmp/comp
> ```

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

Modulo, bitwise and shift operators require `int` operands. `string += string`
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

`async fn` marks a function whose callers receive a `Future<T>`; `await` (only
valid inside an `async fn`) unwraps a `Future<T>` back to `T`.

Grav uses an **eager-future** model: an async call runs to completion immediately
and returns an already-resolved future, so `await` is a zero-cost, statically
checked unwrap. This keeps the surface syntax of structured concurrency while the
generated C stays a plain, dependency-free function call — a cooperative scheduler
where every task finishes at its call site.

```grav
async fn square(x: int) -> int { return x * x }

async fn main() {
    let f = square(9)                // f : Future<int>
    print(await f)                   // 81
}
```

The type checker still enforces the discipline: a `Future<int>` is not an `int`,
so you must `await` it before use, and you may not `await` outside an `async fn`.

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
[generics.grav](examples/11_generics.grav) and
[generic_classes.grav](examples/11_generics.grav).

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
literal at each call — no heap allocation. See [variadics.grav](examples/12_variadics.grav).

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
the type or any subclass); `finally { }` runs on the normal and handled paths. It
lowers to a `setjmp`/`longjmp` runtime; an uncaught exception prints a message and
exits. See [exceptions.grav](examples/13_exceptions.grav).

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
[globals.grav](examples/09_classes.grav).

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

A runnable, multi-file example lives in
[`examples/import_demo/`](examples/import_demo): `app.grav` imports `geometry.grav`,
which in turn imports `mathutil.grav` (a transitive import), and `app.grav` also
re-imports `mathutil.grav` to show that duplicate imports are a safe no-op.

```bash
./build/gravc examples/import_demo/app.grav --emit bin -o /tmp/app && /tmp/app
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
| `print(x)`                | Prints an `int`/`float`/`bool`/`string` and a newline.       |
| `str(x)`                  | Converts an `int`/`float`/`bool`/`string`/enum to a `string`.|
| `typename(obj)`           | Returns the runtime class name as a `string`.                |
| `isInstance(obj, Type)`   | `true` if `obj` is a `Type` or a subclass of it.             |
| `input()`                 | Reads one line from stdin (without the newline) as a `string`.|
| `argc()`                  | Number of command-line arguments (incl. the program name).   |
| `argv(i)`                 | The `i`-th command-line argument as a `string` (`""` if OOB). |

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
├── examples/                # sample .grav programs
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

## Editor support (Neovim)

A LazyVim-ready Neovim integration lives in [`editors/nvim/`](editors/nvim/):
syntax highlighting (keywords, `${}` interpolation, `%{ %}` C blocks, decorators),
filetype detection, and `<leader>mr` / `<leader>mc` build-and-run keymaps. It also
registers the [`gravc` MCP server](mcp_server/) with `mcphub.nvim`. The MCP server
installs the latest build as `grav` in `~/.local/bin` (override the dir with
`GRAV_DIR`). See [`editors/nvim/README.md`](editors/nvim/README.md).

---

## Roadmap / not yet implemented

`v0.8` added the three multiple-inheritance forms (see
[17_multiple_inheritance.grav](examples/17_multiple_inheritance.grav)): **traits**
(`trait` interfaces with default method bodies + `Self`), **composition** (`uses
field: T`, auto-forwarding), and **true multiple inheritance** (`extends A, B`,
flattening fields/methods with multi-base RTTI). `v0.7` completed **generics** —
generic classes/interfaces, generic inheritance (`extends Base<T>`), generic type
aliases, and `T: Bound` constraints (see
[generic_classes.grav](examples/11_generics.grav)) — and added **variadics**
(`fn f(...xs: T)`, see [variadics.grav](examples/12_variadics.grav)) and **exceptions**
(`try`/`catch`/`throw`/`finally`, see [exceptions.grav](examples/13_exceptions.grav)).
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

- **Dynamic collections** — a `list<T>` / `map<K,V>` standard library and set/map
  literals like `#{1, 2, 3}`. The generics needed for these now exist (generic
  classes); what remains is the library types plus a small C runtime (growable
  buffers, hashing) and the literal syntax. Fixed-length `T[N]` arrays *are*
  implemented — see [Arrays](#arrays).

A note on [multiple inheritance](examples/17_multiple_inheritance.grav): the primary
base in `extends A, B` keeps prefix layout (full polymorphism through `A`), while
secondary bases are flattened (fields + methods copied in) with multi-base RTTI. For
polymorphism through a *secondary* base, use an interface or `trait`.

Contributions and design notes welcome — start from the per-folder `README.md` files.
