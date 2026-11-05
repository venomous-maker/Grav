# 5. Data types

## Enums

An enum is a named set of constants. Values print as their names; `int(e)` gives the
ordinal and `n as E` converts back (a number that names no member throws `ValueError`).

```grav
enum Color { Red, Green, Blue }

fn main() {
    let c = Color.Green
    print(c)
    print(int(c))
    print(2 as Color)
    print(c == Color.Green)
}
```

Output:

```text
Green
1
Blue
true
```

Members can be given values, for example to match a protocol's numbers; a member
without one takes the next value after the previous member:

```grav
enum Http { Ok = 200, Created, NotFound = 404 }

fn main() {
    print(int(Http.Created))
    print(404 as Http)
}
```

Output:

```text
201
NotFound
```

## Data enums and pattern matching

When variants carry data, an enum is a *data enum* (a tagged union, like Rust's
enums or Swift's enums with associated values). Variants may name their fields
(`Circle(radius: float)`) or not: a single unnamed field is called `value`, and
several are `_0`, `_1`, …. An enum may also have methods, written after the variants.

```grav
enum Shape {
    Circle(radius: float),
    Rect(w: float, h: float),
    Empty,

    fn area() -> float {
        match (self) {
            case Circle(r): return 3.0 * r * r
            case Rect(w, h): return w * h
            case Empty: return 0.0
        }
    }
}

fn main() {
    let shapes = [Shape.Circle(1.0), Shape.Rect(2.0, 3.0), Shape.Empty]
    for (s in shapes) {
        print("${s} has area ${s.area()}")
    }
    print(Shape.Rect(2.0, 3.0) == Shape.Rect(2.0, 3.0))
    let first = shapes[0]
    if (first is Shape.Circle) {
        print("radius ${(first as Shape.Circle).radius}")
    }
}
```

Output:

```text
Circle(1) has area 3
Rect(2, 3) has area 6
Empty has area 0
true
radius 1
```

- `Shape.Circle(1.0)` builds a value; a variant without fields is just `Shape.Empty`.
  Every variant has the enum's type, so they mix in arrays and return values.
- `match` takes one case per variant and binds its fields by position. Use `_`
  to skip a field, and leave off the parentheses to ignore all of them.
- A guard refines a case: `case Rect(w, h) if w == h: ...` applies only to
  squares, and the following cases see the rest. `match` also works as an
  expression: `let a = match (s) { case Circle(r): 3.0 * r * r  default: 0.0 }`.
- Without a `default`, a match must handle every variant. Leaving one out is a
  compile error that names the missing variants.
- Values print as `Circle(1)`, and `==` compares variant and fields. Defining
  `toString` or `operator==` in the enum replaces the generated ones.
- `variantName()` returns the variant's name. `x is Shape.Circle` and
  `x as Shape.Circle` test for a variant and reach its fields directly.

Data enums can be generic. The standard library's `Result<T, E>` and `Option<T>`
(`import "std/result"`, included in `import "std"`) are written this way:

```grav
import "std/result"

fn parseAge(s: string) -> Result<int, string> {
    if (s == "") { return Result.Err("empty input") }
    if (s == "42") { return Result.Ok(42) }
    return Result.Err("not a number: " + s)
}

fn main() {
    match (parseAge("42")) {
        case Ok(age): print("age ${age}")
        case Err(msg): print("error: ${msg}")
    }
    print(parseAge("").unwrapOr(0))
    print(parseAge("x"))
    let doubled = parseAge("42").map(fn(n: int) => n * 2)
    print(doubled)

    let nick: Option<string> = Option.None
    print(nick.unwrapOr("anonymous"))
}
```

Output:

```text
age 42
0
Err(not a number: x)
Ok(84)
anonymous
```

The type arguments of `Result.Ok(42)` and `Option.None` are inferred from the
arguments or from the expected type (the declared variable, parameter or return
type), including through both branches of `? :`, as in
`return found == null ? Result.Err("missing") : Result.Ok(found)`. `Result` has `isOk`, `isErr`, `unwrap`, `expect`, `unwrapOr`,
`unwrapOrElse`, `unwrapErr`, `map`, `mapErr`, `andThen` and `ok`. `Option` has
`isSome`, `isNone`, `unwrap`, `expect`, `unwrapOr`, `unwrapOrElse`, `map`,
`andThen`, `filter`, `ifSome` and `okOr`.

A data enum compiles to an abstract base class plus one final class per
variant, so matching is a tag check followed by a cast. Data enums are heap
objects like class instances.

## Structs

A struct is a plain value type: named fields, copied on assignment. Create one with
a literal that names every field.

```grav
struct Point { x: int  y: int }
struct Rect { origin: Point  size: Point }      // structs nest by value

fn area(r: Rect) -> int { return r.size.x * r.size.y }

fn main() {
    let a = Point { x: 1, y: 2 }
    let b = a              // a copy
    b.x = 99
    print("${a.x} ${b.x}")
    let r = Rect { origin: Point { x: 0, y: 0 }, size: Point { x: 3, y: 4 } }
    print(area(r))
    print(a == Point { x: 1, y: 2 })            // field-by-field comparison
}
```

Output:

```text
1 99
12
true
```

A struct can have methods. Inside one, `self` is the struct the method was called
on, so assigning its fields changes the caller's variable:

```grav
struct Vec2 {
    x: float
    y: float
    fn length2() -> float { return self.x * self.x + self.y * self.y }
    fn scale(k: float) { self.x *= k; self.y *= k }
}

fn main() {
    let v = Vec2 { x: 3.0, y: 4.0 }
    print(v.length2())
    v.scale(2.0)
    print(v)
}
```

Output:

```text
25
Vec2 { x: 6, y: 8 }
```

Methods are called on a variable, a field, or an array element (not on a temporary
value). Use a [class](06-classes.md) when you need inheritance, interfaces, or
shared mutable state.

## Tuples and destructuring

`(a, b)` and `(a, b, c)` group values without declaring a type; `(int, string)` is
the type of such a pair. Tuples are the standard library's `Pair` and `Triple`
(fields `first`, `second`, `third`), so they need `import "std"`. A `let` can take
a value apart: `let (a, b) = pair` for tuples, `let [x, y] = array` for fixed
arrays and `let {x, y} = point` for struct fields by name (`_` skips a part). The
same patterns work in `for`.

```grav
import "std"

struct Point { x: int  y: int }

fn divmod(a: int, b: int) -> (int, int) { return (a / b, a % b) }

fn main() {
    let (q, r) = divmod(17, 5)
    print("${q} remainder ${r}")
    let {x, y} = Point { x: 3, y: 4 }
    print(x * y)
    let scores = new Array<(string, int)>()
    scores.push(("ada", 9))
    scores.push(("bob", 7))
    for ((name, score) in scores) { print("${name}: ${score}") }
}
```

Output:

```text
3 remainder 2
12
ada: 9
bob: 7
```

## Type aliases

`type` (or its synonym `typedef`) gives an existing type another name. Aliases are
fully interchangeable with what they name, and can be generic.

```grav
type Meters = double
typedef Grid = int[3]
type Pair<T> = T[2]

fn main() {
    let d: Meters = 12.5
    let g: Grid = [1, 2, 3]
    let p: Pair<string> = ["left", "right"]
    print("${d} ${g[2]} ${p[1]}")
}
```

Output:

```text
12.5 3 right
```

## Fixed-length arrays

`T[N]` is an array of exactly `N` elements, stored by value. Array literals infer
their type; `.length` is the element count.

```grav
fn main() {
    let squares = [1, 4, 9, 16]
    squares[0] = 0
    let sum = 0
    for (v in squares) { sum += v }
    print("${sum} ${squares.length}")
    let grid: int[3][2] = [[1, 2, 3], [4, 5, 6]]
    print(grid[1][2])
}
```

Output:

```text
29 4
6
```

Indexing is bounds-checked: an index outside `0..length` throws `IndexError` (a
panic without the standard library). A slice with constant bounds, `squares[1..3]`, is a
shorter fixed array (copied). For growable lists use `Array<T>` from the
[standard library](14-standard-library.md#collections).

## Pointers

`&x` takes an address, `*p` dereferences, and `p->field` reaches a struct field
through a pointer. Pointers are mainly for C interop and in-place updates of value
types.

```grav
struct Counter { n: int }

fn bump(c: Counter*) { c->n = c->n + 1 }
fn double(p: int*) { *p = *p * 2; }

fn main() {
    let c = Counter { n: 41 }
    bump(&c);
    let x = 21
    double(&x);
    print("${c.n} ${x}")
}
```

Output:

```text
42 42
```

Note the `;` after `bump(&c)`: without it, the next line's leading `let` is fine,
but a statement that *starts* with `*`, `&`, or `-` needs the previous one terminated.

## null, ?? and ?.

`null` is a valid value for class, interface, pointer, and function types. `a ?? b`
gives `a` unless it is null; `a?.member` yields a zero value (`0`, `false`, `""`, or
`null`) instead of crashing when `a` is null.

```grav
class Node {
    public label: string
    public next: Node
    constructor(label: string) { self.label = label; self.next = null }
}

fn main() {
    let head = new Node("first")
    let missing: Node = null
    print(missing == null)
    print((missing ?? head).label)
    print(head.next?.label == "")
    head.next = new Node("second")
    print(head.next?.label)
}
```

Output:

```text
true
first
true
second
```

## sizeof

```grav
struct Pixel { r: byte  g: byte  b: byte }

fn main() {
    print("${sizeof(int)} ${sizeof(long)} ${sizeof(double)} ${sizeof(Pixel)}")
}
```

Output:

```text
4 8 8 3
```

## any

`any` holds a value of any primitive type, a string, an enum, an object, a function,
or `null`, together with its runtime type. Values go in implicitly; `x is T` tests
what is inside and `x as T` takes it out (stopping the program with a clear message
if the type is wrong). `print`, interpolation, `str`, `==`, and `typename` work on
`any` directly. `==` compares the values inside: numbers by value, strings by
content, and objects with their class's `operator==` (or by identity without one).

```grav
import "../lib/std.grav"

class Point {
    public x: int  public y: int
    constructor(x: int, y: int) { self.x = x; self.y = y }
    fn toString() -> string { return "(${self.x}, ${self.y})" }
}

fn describe(v: any) -> string {
    if (v is int) { return "an int, doubled ${(v as int) * 2}" }
    if (v is string) { return "a string of length ${Str.length(v as string)}" }
    if (v is Point) { return "a point with x=${(v as Point).x}" }
    if (v == null) { return "nothing" }
    return "a ${typename(v)}"
}

fn main() {
    let items = new Array<any>()
    items.push(21)
    items.push("grav")
    items.push(new Point(3, 4))
    items.push(true)
    items.push(null)
    for (it in items) { print("${it}: ${describe(it)}") }

    let config = new HashMap<string, any>()
    config["port"] = 8080
    config["debug"] = true
    print((config["port"] as int) + 1)
    print(config["port"] == 8080)
}
```

Output:

```text
21: an int, doubled 42
grav: a string of length 4
(3, 4): a point with x=3
true: a bool
null: nothing
8081
true
```

`any` cannot hold structs, fixed arrays, pointers, or `binary`. An object taken out
of an `any` comes out as its class, a base class, or an interface it implements
(`v as Drawable`). Prefer real types where you can — `any` is
for genuinely mixed data such as configuration, heterogeneous lists, or values
crossing a plugin boundary.

## binary

`binary` holds raw bytes with an explicit length, so unlike `string` it may contain
zero bytes (for example data read with `File.readBinary()`). Convert from and to
`string` with `as`, read `.length`, and iterate it byte by byte.

```grav
fn main() {
    let data: binary = "Grav" as binary
    print(data.length)
    let sum = 0
    for (b in data) { sum += b }
    print(sum)
    print(data as string)
}
```

Output:

```text
4
400
Grav
```

A `string` ends at its first zero byte (it is a C string), so `"a\x00b" as binary`
has length 1; build binary data containing zeros from files or inline C.

Next: [Classes →](06-classes.md)
