# 12. Operators and overloading

When you write a class for something mathematical (a vector, a money amount, a
date, a matrix), it is natural to want `a + b` instead of `a.add(b)`, and
`grid[r, c]` instead of `grid.get(r, c)`. Grav lets a class define what operators
mean for its objects. Along the way, this chapter covers method overloading,
which operators build on.

In this chapter you will learn:

- method overloading: several methods with one name,
- operator methods for arithmetic, equality and ordering,
- indexing operators `obj[k]` and `obj[k] = v`, with one or more keys,
- what you get for free (`!=`, `>`, `+=`) and what the compiler checks.

## Method overloading

Like functions ([chapter 5](05-functions.md#overloading)), methods can share a
name when their parameters differ. The call picks the one that fits:

```grav
class Printer {
    fn show(x: int) -> string { return "int ${x}" }
    fn show(x: float) -> string { return "float ${x}" }
    fn show(s: string) -> string { return "string ${s}" }
    static fn line(s: string) -> string { return "| " + s }
    static fn line(s: string, width: int) -> string { return "| " + s + " (${width})" }
}

fn main() {
    let p = new Printer()
    print(p.show(1))
    print(p.show(2.5))
    print(p.show("x"))
    print(Printer.line("a"))
    print(Printer.line("b", 10))
}
```

Output:

```text
int 1
float 2.5
string x
| a
| b (10)
```

An array literal or a lambda with untyped parameters has no type of its own
until it meets a parameter. Among overloads, it takes the one parameter type it
can fit at its position: an `Array` (or another collection with `push` or
`add`) for `[...]`, a function type for a lambda. So `q.whereIn("id", [1, 2])`
picks `whereIn(name: string, values: Array<any>)` even beside
`whereIn(name: string, sub: Query)`, and `xs.apply(fn(x) => x * 2)` picks the
overload taking a function. When two overloads would fit it, give the literal a
type (`let ids: Array<any> = [1, 2]`).

A subclass can override one overload (same parameter types) and add new ones;
calls through the parent type still pick the right method:

```grav
class Base {
    fn handle(x: int) -> string { return "base int" }
    fn handle(s: string) -> string { return "base string" }
}
class Derived extends Base {
    fn handle(x: int) -> string { return "derived int" }       // overrides
    fn handle(b: bool) -> string { return "derived bool" }     // a new overload
}

fn main() {
    let d: Base = new Derived()
    print(d.handle(1))
    print(d.handle("s"))
    print(new Derived().handle(true))
}
```

Output:

```text
derived int
base string
derived bool
```

## Operator methods

An operator method is an ordinary method with a special name. When you write
`a + b` and `a` is an object, Grav calls `a`'s `operator+` with `b` as the argument:

```grav
class Vec {
    public x: float
    public y: float
    constructor(x: float, y: float) { self.x = x; self.y = y }
    fn operator+(o: Vec) -> Vec { return new Vec(self.x + o.x, self.y + o.y) }
    fn operator-(o: Vec) -> Vec { return new Vec(self.x - o.x, self.y - o.y) }
    fn operator*(k: float) -> Vec { return new Vec(self.x * k, self.y * k) }
    fn operator*(o: Vec) -> float { return self.x * o.x + self.y * o.y }  // dot product
    fn toString() -> string { return "(${self.x}, ${self.y})" }
}

fn main() {
    let a = new Vec(1.0, 2.0)
    let b = new Vec(3.0, 4.0)
    print(a + b)
    print(a - b)
    print(a * 2.0)
    print(a * b)
    let pos = new Vec(0.0, 0.0)
    pos += a
    pos *= 3.0
    print(pos)
}
```

Output:

```text
(4, 6)
(-2, -2)
(2, 4)
11
(3, 6)
```

What to notice:

- **The left operand runs the method.** `self` is `a`, and the parameter is `b`.
  So `a * 2.0` works, but `2.0 * a` does not: a `float` has no operator methods.
- **Operators can be overloaded** by the type of the right operand, like `*`
  above (scaling by a number, or a dot product with another vector).
- **`+=`, `-=`, `*=` and friends come free.** `pos += a` means `pos = pos + a`.
  It stores a new object in `pos`; it does not change the object `pos` used to
  refer to.
- Arithmetic operators usually return a new object and leave both operands alone,
  like numbers do. That keeps `a + b` free of surprises.

The arithmetic operators you can define are `+ - * / %`. They can take and return
any types.

## Equality and ordering

```grav
class Version {
    public major: int
    public minor: int
    constructor(major: int, minor: int) { self.major = major; self.minor = minor }
    fn operator==(o: Version) -> bool {
        return self.major == o.major && self.minor == o.minor
    }
    fn operator<(o: Version) -> bool {
        return self.major < o.major || (self.major == o.major && self.minor < o.minor)
    }
    fn toString() -> string { return "${self.major}.${self.minor}" }
}

fn main() {
    let v1 = new Version(1, 4)
    let v2 = new Version(2, 0)
    print("${v1 == new Version(1, 4)} ${v1 != v2}")
    print("${v1 < v2} ${v1 > v2} ${v1 <= v2} ${v2 >= v1}")
    let none: Version = null
    print(none == null)
}
```

Output:

```text
true true
true false true true
true
```

- Without `operator==`, `==` on objects asks whether they are the **same
  object**. With it, `==` compares however you decide.
- `operator==` gives you `!=` as well, and `operator<` gives you `>`, `<=` and
  `>=`. Define those yourself only if they must behave differently.
- Comparison operators must return `bool`.
- Comparing with `null` is always a reference test and never calls your method,
  so your `operator==` never receives `null`.

`Array.indexOf` and `contains` use `operator==` to compare objects.
`HashMap` and `HashSet` keys are still compared by
identity.

## Indexing

`operator[]` makes `obj[k]` read through your method, and `operator[]=` makes
`obj[k] = v` write through it. The key can be any type, and there can be several
keys:

```grav
import "std"

class Grid {
    private width: int
    private cells: Array<int>
    constructor(width: int, height: int) {
        self.width = width
        self.cells = new Array<int>()
        for (i in 0..width * height) { self.cells.push(0) }
    }
    fn operator[](row: int, col: int) -> int { return self.cells.get(row * self.width + col) }
    fn operator[]=(row: int, col: int, v: int) { self.cells.set(row * self.width + col, v) }
}

class Env {
    private vars: HashMap<string, string>
    constructor() { self.vars = new HashMap<string, string>() }
    fn operator[](name: string) -> string { return self.vars.getOr(name, "") }
    fn operator[]=(name: string, value: string) { self.vars.put(name, value) }
}

fn main() {
    let g = new Grid(3, 2)
    g[1, 2] = 7
    g[1, 2] += 5             // reads g[1, 2], adds 5, writes it back
    print(g[1, 2])

    let env = new Env()
    env["HOME"] = "/home/ada"
    env["PATH"] += "/usr/bin"
    print("${env["HOME"]} [${env["PATH"]}] [${env["MISSING"]}]")
}
```

Output:

```text
12
/home/ada [/usr/bin] []
```

- `operator[](row: int, col: int)` is used as `g[1, 2]`. `operator[]=` takes the
  same keys, then the value.
- `obj[k] += v` and `obj[k]++` need both methods: the old value is read with
  `operator[]` and the new one written with `operator[]=`.
- A class with only `operator[]` is read-only by index. That is a neat way to
  give out a view of a collection that cannot be changed through it.
- Inside `operator[]`, use your own storage (`self.cells.get(i)`); writing
  `self[i]` there would call `operator[]` again, forever.

The standard collections use these too: `Array` indexes by position, and
`HashMap` by key.

## The full list

| Write | For | Rules |
|-------|-----|-------|
| `fn operator+(o: T) -> R` | `a + b`, `a += b` | also `-`, `*`, `/`, `%`; any types |
| `fn operator==(o: T) -> bool` | `a == b`, and `a != b` | must return `bool` |
| `fn operator!=(o: T) -> bool` | `a != b` | optional |
| `fn operator<(o: T) -> bool` | `a < b`, and `>`, `<=`, `>=` | must return `bool` |
| `fn operator<=`, `operator>`, `operator>=` | that comparison | optional; must return `bool` |
| `fn operator[](k: K, ...) -> V` | reading `obj[k]` | one or more keys |
| `fn operator[]=(k: K, ..., v: V)` | `obj[k] = v`, `obj[k] += v` | the keys, then the value |

Operator methods are instance methods (not `static`), binary ones take exactly
one parameter, and they are inherited and overridden like any method. `&&`, `||`,
`!`, the bitwise and shift operators, unary `-`, assignment and `.` cannot be
overloaded.

The compiler explains what is missing when an operator does not fit:

| Mistake | Message |
|---------|---------|
| `a + b` on a class without `operator+` | `operator '+' requires numeric operands` |
| a right operand of the wrong type | `operator '+' on Vec expects a right operand of type …` |
| a comparison method not returning `bool` | `comparison operator method '…' must return bool` |
| assigning through a class with only `operator[]` | `class 'X' has 'operator[]' but no 'operator[]=': its elements are read-only` |
| `obj[k] += v` with only `operator[]=` | `'+=' on an element of 'X' needs 'operator[]' to read it` |

## Making objects printable

`fn toString() -> string` is not an operator, but it plays the same role for
printing: `print`, `str` and `${}` call it. Every example above that prints an
object uses it.

## When to use operators

Operators are worth defining when they mean what a reader expects: `+` adds,
`==` compares values, `[]` looks up an element. A `+` that sends an email is
legal, but nobody will guess what it does. When in doubt, use a named method.

## Summary

- Methods (static and instance) can be overloaded by parameter types.
- `fn operator+(o: T) -> R` and friends define operators; the left operand runs the method.
- `operator==` gives `!=`; `operator<` gives `>`, `<=`, `>=`; arithmetic gives `+=` and friends.
- `operator[]` and `operator[]=` define indexing, with any number of keys.
- `null` comparisons never call `operator==`.

## Exercises

1. Write a `Money` class (cents in a `long`) with `+`, `-`, `==`, `<` and a
   `toString` like `$3.07`.
2. Write a `Fraction` class with `+`, `*` and `==` that keeps fractions reduced
   (use `Math.gcd`).
3. Write a `Matrix` class with `operator[](r: int, c: int)` and `operator[]=`, and
   an `operator*` for matrix multiplication.
4. Add `operator<` to `Version` and sort an `Array<Version>` with
   `sortBy(fn(a, b) => a < b ? -1 : (b < a ? 1 : 0))`.
5. Make a read-only `View` class over an `Array<int>` that only defines `operator[]`.
   What does the compiler say when you assign through it?

Next: [Generics →](13-generics.md)
