# 5. Data types

## Enums

An enum is a named set of constants. Values print as their names; `int(e)` gives the
ordinal and `n as E` converts back.

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

## Structs

A struct is a plain value type: named fields, no methods, copied on assignment.
Create one with a literal that names every field.

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

Use a [class](06-classes.md) when you need methods, inheritance, or shared mutable state.

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

For growable lists use `Array<T>` from the [standard library](14-standard-library.md#collections).

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
`any` directly.

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
of an `any` comes out as its class (or a base class); to use it through an
interface, unbox it as the class first. Prefer real types where you can — `any` is
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
