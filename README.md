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
  - [Structs](#structs)
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
  - [Comments](#comments)
  - [Diagnostics & warnings](#diagnostics--warnings)
- [How Grav lowers to C](#how-grav-lowers-to-c)
- [Project layout](#project-layout)
- [Building the compiler](#building-the-compiler)
- [Roadmap / not yet implemented](#roadmap--not-yet-implemented)

---

## Quick start

```bash
# build the compiler
cmake -S . -B build && cmake --build build

# transpile a program to C
./build/gravc examples/shapes.grav            # writes examples/shapes.c

# …or compile straight to a native binary and run it
./build/gravc examples/animals.grav --emit bin -o /tmp/animals
/tmp/animals
```

```grav
// examples/shapes.grav
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
gravc <input.grav> [-o out] [--emit c|bin|both] [-Werror]
gravc -                       # read source from stdin, write C to stdout
```

| Flag            | Meaning                                                              |
|-----------------|---------------------------------------------------------------------|
| `-o <out>`      | Output path (the `.c` file, or the executable for `bin`/`both`).     |
| `--emit c`      | **(default)** Emit C only.                                          |
| `--emit bin`    | Compile the generated C to a native executable (C file is removed). |
| `--emit both`   | Emit the `.c` **and** the executable.                              |
| `-Werror`       | Treat warnings (e.g. unused variables) as fatal errors.            |
| `-`             | Read from stdin and print C to stdout.                             |

For `bin`/`both`, `gravc` invokes the system C compiler (`$CC`, default `cc`) as
`cc -std=c11 <file>.c -o <out>`.

---

## Language tour

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

Conditions must be `bool` (no truthy ints); `switch`/`match` subjects are `int` or
`string`.

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

### Runtime type info (RTTI)

Every object carries a type descriptor. Two built-ins query it:

```grav
let a: zoo.Animal = new zoo.Dog()
print(typename(a))            // "zoo.Dog"
print(isInstance(a, zoo.Dog)) // true
print(isInstance(a, zoo.Animal)) // true  (walks the base chain)
```

### Built-ins

| Built-in                  | Description                                            |
|---------------------------|--------------------------------------------------------|
| `print(x)`                | Prints an `int`/`float`/`bool`/`string` and a newline. |
| `typename(obj)`           | Returns the runtime class name as a `string`.          |
| `isInstance(obj, Type)`   | `true` if `obj` is a `Type` or a subclass of it.       |

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

## Roadmap / not yet implemented

These were discussed but are **not** in the current core (`v0.2`); they are the next
milestones:

- **Built-in data structures** — `array<T>` / `list<T>` / `map<K,V>` (needs generics
  plus a small C runtime).
- **Generics / monomorphization** — `class Box<T> { … }`.
- **`type` aliases** — naming an existing type. (Plain data `struct` types *are*
  implemented — see [Structs](#structs).)
- **Static fields & global `const`** — class-level `static x: T = …` lowered to C
  globals, and module-level constants. (Local `const` *is* implemented.)
- **Multiple class inheritance** — Grav supports a single base class plus multiple
  interfaces. True multiple *class* inheritance doesn't map cleanly onto C's struct
  prefix / single-vtable model; use interfaces (or composition) for multiple supertypes.
Contributions and design notes welcome — start from the per-folder `README.md` files.
