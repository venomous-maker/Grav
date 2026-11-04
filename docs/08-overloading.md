# 8. Overloading & operators

## Method overloading

Instance and static methods can share a name when their parameter types differ. The
compiler picks the exact match, else the single candidate that accepts the
arguments, and reports ambiguous or unmatched calls.

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

A subclass can override one overload (same parameter types) and add new ones:

```grav
class Base {
    fn handle(x: int) -> string { return "base int" }
    fn handle(s: string) -> string { return "base string" }
}
class Derived extends Base {
    fn handle(x: int) -> string { return "derived int" }       // override
    fn handle(b: bool) -> string { return "derived bool" }     // new overload
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

Constructors overload the same way — see [Classes](06-classes.md#constructors).

## Operator methods

A class can define what operators mean for its instances with
`fn operator<op>(other: T)`. Overloadable operators:

| Operators | Method form | Notes |
|-----------|-------------|-------|
| `+ - * / %` | `fn operator+(o: T) -> R` | any return type |
| `== !=` | `fn operator==(o: T) -> bool` | `!=` defaults to `!(a == b)` |
| `< <= > >=` | `fn operator<(o: T) -> bool` | must return `bool`; `>`, `<=`, `>=` follow from `<` |
| `obj[k]`, `obj[r, c]` | `fn operator[](k: K) -> V` | reading; one or more keys |
| `obj[k] = v` | `fn operator[]=(k: K, v: V)` | writing; the keys, then the value |

Compound assignments use them too: `a += b` is `a = a + b`, and `obj[k] += v` (or
`obj[k]++`) reads with `operator[]` and writes with `operator[]=`. When the element
is a struct, `operator[]` returns a copy, so `obj[k].field = v` changes the copy and
writes it back with `operator[]=`. [Writing operators](operators.md)
covers every operator, its rules, and the compiler's checks.

Operator methods can themselves be overloaded (here `*` scales or takes a dot
product depending on the right operand):

```grav
class Vec {
    public x: float
    public y: float
    constructor(x: float, y: float) { self.x = x; self.y = y }
    fn operator+(o: Vec) -> Vec { return new Vec(self.x + o.x, self.y + o.y) }
    fn operator*(k: float) -> Vec { return new Vec(self.x * k, self.y * k) }
    fn operator*(o: Vec) -> float { return self.x * o.x + self.y * o.y }
    fn operator==(o: Vec) -> bool { return self.x == o.x && self.y == o.y }
    fn toString() -> string { return "(${self.x}, ${self.y})" }
}

fn main() {
    let a = new Vec(1.0, 2.0)
    let b = new Vec(3.0, 4.0)
    print(a + b)
    print(a * 2.0)
    print(a * b)
    print(a != b)
}
```

Output:

```text
(4, 6)
(2, 4)
11
true
```

Indexing operators make collection-like classes feel built in:

```grav
class Scores {
    private names: string[3]
    private values: int[3]
    constructor() { self.names = ["ada", "bob", "cy"]; self.values = [0, 0, 0] }
    fn find(name: string) -> int {
        for (i, n in self.names) { if (n == name) { return i } }
        return -1
    }
    fn operator[](name: string) -> int { return self.values[self.find(name)] }
    fn operator[]=(name: string, v: int) { self.values[self.find(name)] = v }
}

fn main() {
    let s = new Scores()
    s["bob"] = 42
    s["cy"] = s["bob"] + 1
    print("${s["bob"]} ${s["cy"]}")
}
```

Output:

```text
42 43
```

A comparison against `null` (`obj == null`) is always a reference test and never
calls `operator==`.

## toString()

Defining `fn toString() -> string` makes a class printable with `print`, `str`, and
`${}` interpolation — as `Vec` above shows.

Next: [Generics →](09-generics.md)
