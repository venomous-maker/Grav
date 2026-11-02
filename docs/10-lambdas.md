# 10. Lambdas & closures

## Function types

`fn(A, B) -> R` is the type of a function taking `A` and `B` and returning `R` (omit
`-> R` for `void`). Named functions are values of these types.

```grav
fn square(x: int) -> int { return x * x }
fn apply(f: fn(int) -> int, v: int) -> int { return f(v) }

fn main() {
    let f: fn(int) -> int = square
    print(f(6))
    print(apply(square, 7))
}
```

Output:

```text
36
49
```

## Lambdas

A lambda is an anonymous function. Use `=> expr` for a single expression or a block
with an explicit return type.

```grav
fn main() {
    let double = fn(x: int) => x * 2
    let clamp = fn(x: int) -> int {
        if (x < 0) { return 0 }
        return x > 10 ? 10 : x
    }
    let greet = fn(name: string) { print("hi ${name}") }     // returns void
    print(double(21))
    print(clamp(99))
    greet("Ada")
}
```

Output:

```text
42
10
hi Ada
```

Parameter types can be omitted wherever the expected function type is known — a
function argument, a typed `let`, a field assignment, or a `return`:

```grav
fn applyTwice(f: fn(int) -> int, x: int) -> int { return f(f(x)) }

fn main() {
    print(applyTwice(fn(x) => x + 10, 1))      // x: int is inferred
    let shout: fn(string) -> string = fn(s) => s + "!"
    print(shout("hey"))
}
```

Output:

```text
21
hey!
```

## Closures

A lambda can use variables from the surrounding scope. They are **captured by
value** when the lambda is created, so assigning to them inside the lambda is a
compile error. To share mutable state, capture an object.

```grav
class Cell { public n: int  constructor() { self.n = 0 } }

fn makeAdder(k: int) -> fn(int) -> int { return fn(x) => x + k }

fn makeCounter() -> fn() -> int {
    let cell = new Cell()
    return fn() -> int { cell.n = cell.n + 1; return cell.n }
}

fn main() {
    let add5 = makeAdder(5)
    print(add5(10))
    let next = makeCounter()
    next()
    next()
    print(next())
}
```

Output:

```text
15
3
```

Lambdas inside methods can capture `self`, and lambdas can return lambdas:

```grav
fn main() {
    let multiplier = fn(a: int) => fn(b: int) => a * b
    let triple = multiplier(3)
    print(triple(14))
}
```

Output:

```text
42
```

## Callbacks in fields and collections

```grav
import "../lib/std.grav"

class Button {
    public onClick: fn(string) -> string
    private label: string
    constructor(label: string) { self.label = label; self.onClick = null }
    fn click() -> string {
        if (self.onClick == null) { return "no handler" }
        return self.onClick(self.label)
    }
}

fn main() {
    let b = new Button("OK")
    print(b.click())
    b.onClick = fn(l) => "clicked ${l}"
    print(b.click())

    let handlers = new Array<fn(int)>()
    handlers.push(fn(x) => print("a got ${x}"))
    handlers.push(fn(x) => print("b got ${x * 10}"))
    for (h in handlers) { h(4) }
}
```

Output:

```text
no handler
clicked OK
a got 4
b got 40
```

Calling a `null` function value stops the program with an error message.

## Higher-order collection methods

`Array<T>` (and partly `List<T>`) accept lambdas:

| Method | Does |
|--------|------|
| `forEach(f)`, `forEachIndexed(f)` | call `f` for each element |
| `map(f)` | a new array of `f(x)` (same element type) |
| `mapTo(f)` / `mapTo::<U>(f)` | a new array of `f(x)` with element type `U` (inferred from `f`) |
| `filter(pred)` | the elements where `pred(x)` is true |
| `reduce(init, f)`, `fold(init, f)` | combine all elements into one value (`fold` may change the type) |
| `any(pred)`, `all(pred)`, `countWhere(pred)`, `findIndex(pred)` | queries |
| `sortBy(cmp)` | stable sort; `cmp(a, b) < 0` puts `a` first |

```grav
import "../lib/std.grav"

fn main() {
    let nums = new Array<int>()
    for (n in [5, 3, 8, 1, 9, 2]) { nums.push(n) }

    let big = nums.filter(fn(n) => n > 2).map(fn(n) => n * 10)
    print(big.reduce(0, fn(a, b) => a + b))
    print(nums.any(fn(n) => n > 8))
    print(nums.countWhere(fn(n) => n % 2 == 1))

    nums.sortBy(fn(a, b) => a - b)
    let labels = nums.mapTo::<string>(fn(n) => "<${n}>")
    print(Str.join(labels, ""))
}
```

Output:

```text
250
true
4
<1><2><3><5><8><9>
```

Closures' captured variables live in a small heap environment. The compiler frees it
automatically for local lambdas that are only called and for lambdas passed straight
to the methods above; `free(f)` releases one explicitly. See [Memory](12-memory.md).

Next: [Exceptions →](11-exceptions.md)
