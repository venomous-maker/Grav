# 8. Structs, enums and tuples

Numbers and strings only go so far. A point has an x and a y; an order has a
customer, items and a status; a shape is a circle *or* a rectangle. This chapter
shows the ways Grav lets you define your own data types without writing a class.

In this chapter you will learn:

- **structs**: small groups of named fields, copied like numbers,
- **enums**: a fixed set of named values,
- **data enums**: values that are one of several shapes, each with its own data,
  and pattern matching to take them apart,
- **tuples** and **destructuring**: grouping and ungrouping values on the fly,
- **type aliases**: new names for existing types,
- **`any`** and **`binary`**, for the rare cases that need them.

## Structs

A struct is a named group of fields:

```grav
struct Point {
    x: int
    y: int
}

fn main() {
    let p = Point { x: 1, y: 2 }
    print(p.x)
    p.y = 10
    print(p)
}
```

Output:

```text
1
Point { x: 1, y: 10 }
```

`Point { x: 1, y: 2 }` is a *struct literal*: it names every field. Fields are
read and written with a dot. Short structs often go on one line, with fields
separated by spaces: `struct Point { x: int  y: int }`.

### Structs are values

A struct is copied whenever it is assigned or passed, just like an `int`.
Changing the copy does not change the original:

```grav
struct Point { x: int  y: int }
struct Rect { origin: Point  size: Point }      // structs can contain structs

fn area(r: Rect) -> int { return r.size.x * r.size.y }

fn main() {
    let a = Point { x: 1, y: 2 }
    let b = a              // a copy
    b.x = 99
    print("${a.x} ${b.x}")
    let r = Rect { origin: Point { x: 0, y: 0 }, size: Point { x: 3, y: 4 } }
    print(area(r))
    print(a == Point { x: 1, y: 2 })     // compares every field
}
```

Output:

```text
1 99
12
true
```

Copying makes structs predictable: nobody else can change your copy. It also
makes them cheap, because a struct lives directly in its variable, with no
separate allocation. Use them for small pieces of data: coordinates, colours,
ranges, money amounts.

### Methods on structs

A struct can have functions attached to it, called *methods*. Inside a method,
`self` is the struct it was called on:

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

A method that assigns to `self`'s fields changes the variable it was called on,
which is why methods are called on variables, fields or array elements, not on a
temporary value like `makeVec().scale(2.0)`.

When you need inheritance, interfaces, or one object shared by several parts of
the program, use a [class](10-classes.md) instead.

## Enums

An enum is a type with a fixed list of named values:

```grav
enum Color { Red, Green, Blue }

fn main() {
    let c = Color.Green
    print(c)
    print(c == Color.Green)
    print(int(c))           // its position: 0, 1, 2, ...
    print(2 as Color)       // and back
}
```

Output:

```text
Green
true
1
Blue
```

Enums are much safer than using numbers or strings for the same job: a
`Color` can only ever be one of its three values, the compiler catches a
misspelled `Color.Gren`, and a `switch` over a `Color` warns when you forget a
value. They print as their names.

Values can be given numbers, for example to match a file format or a network
protocol. A value without a number takes the next one after the previous value:

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

Converting a number that names no value (`500 as Http`) throws a `ValueError`.

## Data enums and pattern matching

Sometimes a value is one of several *kinds*, and each kind carries different
data. A shape is a circle (with a radius) or a rectangle (with a width and a
height) or empty. A *data enum* says exactly that:

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
}
```

Output:

```text
Circle(1) has area 3
Rect(2, 3) has area 6
Empty has area 0
true
```

Each variant (`Circle`, `Rect`, `Empty`) is a possible shape of the value.
`Shape.Circle(1.0)` creates one. All variants have the type `Shape`, so they can
share an array, a parameter or a return type.

### Pattern matching

`match` is how you find out which variant you have and get at its data. Each
`case` names a variant and gives names to its fields, in order:

- `case Circle(r):` matches circles and calls the radius `r`.
- `_` skips a field you do not need: `case Rect(w, _):`.
- Leaving off the parentheses ignores all fields: `case Rect:`.
- A guard narrows a case further: `case Rect(w, h) if w == h:` matches only squares.
- Without a `default`, a match must cover every variant. Forgetting one is a
  **compile error** that names the missing variants, so adding a variant later
  shows you every place that must handle it.

`match` also works as an expression:

```grav
enum Shape { Circle(radius: float), Rect(w: float, h: float), Empty }

fn describe(s: Shape) -> string {
    return match (s) {
        case Circle(r): "a circle of radius ${r}"
        case Rect(w, h) if w == h: "a square of side ${w}"
        case Rect(w, h): "a ${w} by ${h} rectangle"
        case Empty: "nothing"
    }
}

fn main() {
    print(describe(Shape.Rect(2.0, 2.0)))
    print(describe(Shape.Rect(2.0, 5.0)))
    print(describe(Shape.Circle(0.5)))
}
```

Output:

```text
a square of side 2
a 2 by 5 rectangle
a circle of radius 0.5
```

### Other ways to inspect a variant

- `s is Shape.Circle` tests for a variant, and `(s as Shape.Circle).radius`
  reaches its fields directly.
- `s.variantName()` gives the variant's name as a string.
- Fields declared without names (`Ok(int)`) are called `value` when there is one,
  and `_0`, `_1`, ... when there are several.
- Values print like `Circle(1)` and compare with `==` field by field. Define
  `toString()` or `operator==` inside the enum to change that.

Data enums can be generic. The standard library's `Option<T>` and
`Result<T, E>` are data enums, covered in [Null safety](09-null-safety.md) and
[Errors](14-errors.md).

## Tuples and destructuring

Sometimes you want to group two or three values without declaring a struct for
them, typically to return several results from a function. `(a, b)` makes a
*tuple*, and `(int, string)` is its type:

```grav
import "std"

fn divmod(a: int, b: int) -> (int, int) {
    return (a / b, a % b)
}

fn main() {
    let result = divmod(17, 5)
    print(result.first)
    let (q, r) = divmod(17, 5)          // take it apart
    print("${q} remainder ${r}")
}
```

Output:

```text
3
3 remainder 2
```

Tuples are the standard library's `Pair<A, B>` and `Triple<A, B, C>`, with
fields `first`, `second` and `third`, so they need `import "std"`.

`let (q, r) = ...` is *destructuring*: it declares one variable per part. It
works on more than tuples:

| Pattern | Takes apart |
|---------|-------------|
| `let (a, b) = pair` | a tuple, by position |
| `let [x, y, z] = array` | a fixed array, by position |
| `let {x, y} = point` | a struct, by field name |

`_` skips a part: `let (_, r) = divmod(17, 5)`. The same patterns work in `for`
loops:

```grav
import "std"

struct Point { x: int  y: int }

fn main() {
    let {x, y} = Point { x: 3, y: 4 }
    print(x * y)
    let scores = new Array<(string, int)>()
    scores.push(("ada", 9))
    scores.push(("bob", 7))
    for ((name, score) in scores) {
        print("${name}: ${score}")
    }
}
```

Output:

```text
12
ada: 9
bob: 7
```

If a tuple travels far through your program, or its parts deserve names, turn it
into a struct.

## Type aliases

`type` gives an existing type a second name. It does not create a new type: the
alias and the original are interchangeable. Aliases document meaning and shorten
long types. `typedef` means the same as `type`.

```grav
type Meters = float
type Grid = int[3]
type Two<T> = T[2]

fn main() {
    let d: Meters = 12.5
    let g: Grid = [1, 2, 3]
    let p: Two<string> = ["left", "right"]
    print("${d} ${g[2]} ${p[1]}")
}
```

Output:

```text
12.5 3 right
```

## any

`any` holds a value of any type, together with a tag saying what type it is. It
is for data that really is mixed, such as configuration values or a JSON-like
tree. You test what is inside with `is`, and take it out with `as`:

```grav
import "std"

fn describe(v: any) -> string {
    if (v is int) { return "an int, doubled ${(v as int) * 2}" }
    if (v is string) { return "a string of length ${(v as string).length}" }
    if (v == null) { return "nothing" }
    return "a ${typename(v)}"
}

fn main() {
    let items = new Array<any>()
    items.push(21)
    items.push("grav")
    items.push(true)
    items.push(null)
    for (it in items) { print("${it}: ${describe(it)}") }

    let config = new HashMap<string, any>()
    config["port"] = 8080
    config["debug"] = true
    print((config["port"] as int) + 1)
}
```

Output:

```text
21: an int, doubled 42
grav: a string of length 4
true: a bool
null: nothing
8081
```

- Any number, bool, string, enum, object, function or `null` goes into an `any`
  without a cast.
- `v as int` checks the type when the program runs. If `v` holds something else,
  the program stops with a message naming both types.
- `print`, `str`, `${}`, `==` and `typename` work on an `any` directly; `==`
  compares the values inside.
- Structs, fixed arrays, pointers and `binary` cannot go in an `any`.

Prefer real types whenever you can. With `any`, mistakes are found while the
program runs instead of when it compiles.

## binary

`binary` is a buffer of raw bytes with a length. Unlike a `string`, it may
contain zero bytes, so it can hold images, compressed data or anything read from
a file with `File.readBinary()`:

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

`as` converts between `string` and `binary`. The [`Bytes`](../std/data/bytes.md)
class reads and writes numbers in binary data, encodes Base64 and hex, and more.

## Summary

- A **struct** groups named fields and is copied like a number; it can have methods.
- An **enum** is a fixed set of named values; `int(e)` and `n as E` convert.
- A **data enum** is one of several variants, each with its own fields; `match`
  takes it apart and must cover every variant.
- A **tuple** `(a, b)` groups values on the fly; `let (a, b) = ...`,
  `let [x, y] = ...` and `let {x, y} = ...` destructure.
- **`type`** names an existing type.
- **`any`** holds a value of any type, checked when you take it out; **`binary`** holds raw bytes.

## Exercises

1. Define `struct Money { cents: long }` with methods `add(other: Money)` and
   `format() -> string` that prints like `$12.05`.
2. Define `enum Weekday` and write `fn isWeekend(d: Weekday) -> bool` with a
   `switch`. Add a value to the enum and see what the compiler says.
3. Model a traffic event as a data enum: `Accident(lane: int)`,
   `Roadwork(from: int, to: int)`, `Clear`. Write a `match` that describes each.
4. Write `fn minMax(xs: Array<int>) -> (int, int)` and destructure its result.
5. Store an int, a string and a bool in an `Array<any>`, then add up only the ints.

Next: [Null safety →](09-null-safety.md)
