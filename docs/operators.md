# Writing operators

A class can give meaning to `+`, `==`, `<`, `obj[i]` and the rest by defining
**operator methods**. They are ordinary instance methods with special names: the
compiler turns `a + b` into a call to `a`'s `operator+` with `b` as the argument.
This page covers every operator you can write, the rules each one follows, and the
mistakes the compiler catches.

(Chapter 8, [Overloading & operators](08-overloading.md), introduces them; this
is the full guide.)

## The operators

| You write | Method | Called for | Rules |
|-----------|--------|------------|-------|
| `fn operator+(o: T) -> R` | `op_add` | `a + b`, `a += b` | any parameter and return type |
| `fn operator-(o: T) -> R` | `op_sub` | `a - b`, `a -= b` | |
| `fn operator*(o: T) -> R` | `op_mul` | `a * b`, `a *= b` | |
| `fn operator/(o: T) -> R` | `op_div` | `a / b`, `a /= b` | |
| `fn operator%(o: T) -> R` | `op_mod` | `a % b`, `a %= b` | |
| `fn operator==(o: T) -> bool` | `op_eq` | `a == b`, and `a != b` if there is no `operator!=` | must return `bool` |
| `fn operator!=(o: T) -> bool` | `op_ne` | `a != b` | optional |
| `fn operator<(o: T) -> bool` | `op_lt` | `a < b`, and `>`, `<=`, `>=` when those are not defined | must return `bool` |
| `fn operator<=`, `operator>`, `operator>=` | `op_le`, `op_gt`, `op_ge` | that comparison | optional; must return `bool` |
| `fn operator[](k: K) -> V` | `op_index` | reading `obj[k]` | one or more key parameters |
| `fn operator[]=(k: K, v: V)` | `op_index_set` | `obj[k] = v`, `obj[k] += v` | the keys, then the value |

Every operator method is an instance method (not `static`); the binary operators
take exactly one parameter, the right-hand operand. The left operand is always the
object whose method runs: `a + 2` can call `a.operator+(2)`, but `2 + a` looks at
`int`, which has no operator methods, so it does not compile.

## Arithmetic

Arithmetic operators usually return a new object and leave both operands alone:

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
    pos += a                 // pos = pos + a
    pos *= 3.0               // pos = pos * 3.0
    print(pos)
}
```

Output:

```text
(4, 6)
(-2, -2)
(2, 4)
11.000000
(3, 6)
```

- **Overloading by operand type.** An operator can have several methods that differ
  in the parameter type, like `operator*` above: the compiler picks the one that
  matches the right operand (an exact type first, then one it converts to).
- **Compound assignment comes free.** `x += y` is `x = x + y` with the matching
  `operator+`, for variables, fields (`self.total += amount`) and elements
  (`grid[r, c] += 1.0`). The object or container on the left is evaluated once.
  The result must be assignable back to the left side's type.
- **No hidden mutation.** `+=` stores the operator's result in the variable; it
  does not modify the object the variable pointed to. Other variables that
  referred to the old object still see it unchanged.

## Equality and ordering

```grav
class Version {
    public major: int
    public minor: int
    constructor(major: int, minor: int) { self.major = major; self.minor = minor }
    fn operator==(o: Version) -> bool { return self.major == o.major && self.minor == o.minor }
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

- `operator==` gives you `!=` too (as `!(a == b)`), unless you define
  `operator!=` yourself.
- `operator<` gives you the other three: `a > b` is `b < a`, `a <= b` is
  `!(b < a)` and `a >= b` is `!(a < b)`. Define `operator<=` and the others only
  when they need to differ, for example for values that are not totally ordered.
- Comparing with `null` (`obj == null`, `obj != null`) is always a reference test
  and never calls `operator==`, so a `null` operand never reaches your method.
- Without `operator==`, `==` on objects compares identity: are they the same object?
- Comparison operators must return `bool`.

`Array.indexOf` and `contains` compare objects with `operator==`. `HashMap` and
`HashSet` keys are still matched by identity (the same object), because there is
no hash operator to go with `operator==`.

## Indexing: `operator[]` and `operator[]=`

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

- **Several keys:** `operator[](row: int, col: int)` is used as `g[1, 2]`, and
  `operator[]=` takes the same keys followed by the value. A class can define
  several `operator[]`s that differ in the number or the types of their keys.
- **Compound forms** (`+=`, `-=`, …) on an element need both methods: the old value
  is read with `operator[]`, combined, and written with `operator[]=`. The object
  and the keys are evaluated once.
- **Read-only or write-only.** A class with only `operator[]` is read-only by
  index (assigning is a compile error that says so); one with only `operator[]=`
  can be written but not read. That is how you make a read-only view of a
  collection.
- Indexing with several keys needs an `operator[]`: arrays and pointers take one
  index.
- The standard collections use these: `Array`, `List` and `Deque` index by
  position (bounds-checked), and `HashMap` reads with `get` (the zero value when
  the key is missing) and writes with `put`, so `counts[word] += 1` counts words.

## Inside an operator method

- `self` is the left operand (for `operator[]`, the indexed object). Operator
  methods can read private fields of the other operand when it is the same class.
- Index into your own storage without recursion: inside `operator[]`, `self.data[i]`
  on a raw buffer or `self.cells.get(i)` on a collection does not call your
  `operator[]` again. Calling `self[i]` inside it would.
- Return new objects freely. Memory is reference counted, so the temporaries
  in `a + b + c` are freed at the end of the statement.
- Operator methods are inherited and can be overridden like any method; calls go
  to the object's actual class.
- They can throw. Exceptions from `a + b` propagate like any method call's.

## What the compiler checks

| Mistake | Error |
|---------|-------|
| using `+` on a class without `operator+` | `operator '+' requires numeric operands` |
| a right operand of the wrong type | `operator '+' on Vec expects a right operand of type …` |
| a comparison method not returning `bool` | `comparison operator method '…' must return bool` |
| a `static` operator or a binary operator with two parameters | `operator method '…' must be an instance method taking exactly one parameter` |
| assigning through a class that has only `operator[]` | `class 'X' has 'operator[]' but no 'operator[]=': its elements are read-only` |
| reading through a class that has only `operator[]=` | `class 'X' has 'operator[]=' but no 'operator[]': its elements can be assigned but not read` |
| `obj[k] += v` with only `operator[]=` | `'+=' on an element of 'X' needs 'operator[]' to read it` |
| keys that no `operator[]` takes | `no 'operator[]' of 'X' takes [int, int] (it has [int])` |
| two indices on an array | `several indices need a class with an 'operator[]' taking them` |
| an operator that cannot be overloaded (`&&`, `!`, `=`, …) | `this operator cannot be overloaded` |

The logical operators (`&&`, `||`, `!`), bitwise and shift operators, unary `-`,
assignment itself and member access cannot be overloaded.
