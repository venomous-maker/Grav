# 6. Lambdas and closures

In the last chapter you saw that a function is a value: you can store it in a
variable and pass it to another function. A *lambda* is a function written right
where it is needed, without a name. Lambdas make it easy to hand a small piece of
behaviour to other code: "sort these by price", "call this when the button is
clicked", "keep only the even ones".

In this chapter you will learn:

- function types, and how to write lambdas,
- when parameter types can be left out,
- closures: lambdas that use variables from around them, and why those are copies,
- lambdas that call themselves,
- storing functions in fields and collections,
- the collection methods that take lambdas: `map`, `filter`, `reduce` and friends.

## Function types

The type of a function is written `fn(ParameterTypes) -> ResultType`:

| Type | Means |
|------|-------|
| `fn(int) -> int` | takes an int, returns an int |
| `fn(string, int) -> bool` | takes a string and an int, returns a bool |
| `fn()` | takes nothing, returns nothing |
| `fn(int)` | takes an int, returns nothing |

Any function with matching parameter and result types fits:

```grav
fn square(x: int) -> int { return x * x }

fn apply(f: fn(int) -> int, v: int) -> int {
    return f(v)
}

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

## Writing lambdas

A lambda looks like a function declaration without a name. For a single
expression, use `=>`; the expression's value is the result. For anything longer,
use a block:

```grav
fn main() {
    let double = fn(x: int) => x * 2
    let clamp = fn(x: int) -> int {
        if (x < 0) { return 0 }
        return x > 10 ? 10 : x
    }
    let greet = fn(name: string) { print("hi ${name}") }
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

A block lambda without `-> Type` takes its result type from its `return`
statements, and returns nothing if it has none.

### Leaving out parameter types

When the compiler already knows what kind of function is expected, the
parameter types can be left out. That is the case when the lambda is an argument,
is assigned to a variable or field with a function type, or is returned from a
function with a function result type:

```grav
fn applyTwice(f: fn(int) -> int, x: int) -> int { return f(f(x)) }

fn main() {
    print(applyTwice(fn(x) => x + 10, 1))      // x is an int
    let shout: fn(string) -> string = fn(s) => s + "!"
    print(shout("hey"))
}
```

Output:

```text
21
hey!
```

This is what makes lambdas pleasant with collections: `names.filter(fn(n) => n != "")`.

## Closures

A lambda can use the variables around it. Such a lambda is called a *closure*,
because it "closes over" those variables:

```grav
fn makeAdder(k: int) -> fn(int) -> int {
    return fn(x) => x + k
}

fn main() {
    let add5 = makeAdder(5)
    let add100 = makeAdder(100)
    print(add5(10))
    print(add100(10))
}
```

Output:

```text
15
110
```

`makeAdder` has returned by the time `add5` is called, yet `add5` still knows
that `k` is 5. Each closure keeps its own copy of the variables it uses.

### Captured variables are copies

Grav copies each captured variable into the closure when the closure is
created. Two consequences:

- The closure sees the value the variable had **when the closure was made**.
- Assigning to a captured variable inside the closure is a compile error, because
  it would only change the copy and that would be confusing.

```text
let count = 0
let inc = fn() { count += 1 }   // error: cannot assign to captured variable 'count' inside a lambda
```

To share changing state between a closure and its surroundings, put the state in
an object. The closure copies the *reference*, so both sides see the same object:

```grav
class Counter {
    public n: int
    constructor() { self.n = 0 }
}

fn makeCounter() -> fn() -> int {
    let c = new Counter()
    return fn() -> int {
        c.n = c.n + 1
        return c.n
    }
}

fn main() {
    let next = makeCounter()
    next()
    next()
    print(next())
}
```

Output:

```text
3
```

Copying makes closures predictable, and safe to run on other threads: nothing
changes a closure's variables behind its back.

### Closures that return closures

Lambdas can return lambdas. This is a neat way to build specialised functions
from general ones:

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

### Lambdas that call themselves

Because captures are copies made when the lambda is created, a lambda cannot
normally see the variable it is about to be stored in. There is one exception:
a lambda stored in a variable that it mentions is given itself under that name.
It must state its result type, because its own calls depend on it:

```grav
fn main() {
    let fact = fn(n: int) -> long {
        return n <= 1 ? 1 as long : n * fact(n - 1)
    }
    print(fact(20))
}
```

Output:

```text
2432902008176640000
```

The same works for an assignment (`walk = fn(...) -> int { ... walk(...) ... }`)
and for functions declared inside a block ([chapter 5](05-functions.md#functions-inside-functions)).

### Methods as values

A method of a particular object is a function value too. `counter.add` is a
function that calls `add` on that object:

```grav
class Account {
    public balance: int
    constructor() { self.balance = 0 }
    fn deposit(amount: int) { self.balance += amount }
}

fn main() {
    let acct = new Account()
    let pay = acct.deposit
    pay(50)
    pay(25)
    print(acct.balance)
}
```

Output:

```text
75
```

If the method is overloaded, say which one with a function type:
`let f: fn(string) -> string = x.label`.

## Functions in fields and collections

Function values can live anywhere other values can: in fields, arrays and maps.
This is how callbacks and event handlers are built:

```grav
import "std"

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

A function-typed field or variable can be `null`. Calling a `null` function
stops the program with an error, so check first, as `click` does.

## Working with collections

`Array<T>` has methods that take lambdas and do the looping for you:

| Method | Gives |
|--------|-------|
| `forEach(f)` | calls `f(x)` for each element |
| `map(f)` | a new array of `f(x)`, of the same element type |
| `mapTo(f)` | a new array of `f(x)`, of whatever type `f` returns |
| `filter(keep)` | the elements for which `keep(x)` is true |
| `reduce(start, f)` | combines all elements into one value |
| `fold(start, f)` | like `reduce`, but the result may have another type |
| `any(p)`, `all(p)` | whether some / every element passes `p` |
| `countWhere(p)`, `findIndex(p)`, `findFirst(p)` | counting and searching |
| `sortBy(compare)` | sorts in place; `compare(a, b) < 0` puts `a` first |

```grav
import "std"

fn main() {
    let nums: Array<int> = [5, 3, 8, 1, 9, 2]

    let big = nums.filter(fn(n) => n > 2).map(fn(n) => n * 10)
    print(big)
    print(big.reduce(0, fn(total, n) => total + n))
    print(nums.any(fn(n) => n > 8))
    print(nums.countWhere(fn(n) => n % 2 == 1))

    nums.sortBy(fn(a, b) => a - b)
    print(nums)
    let labels = nums.mapTo(fn(n) => "<${n}>")
    print(Str.join(labels, ""))
}
```

Output:

```text
[50, 30, 80, 90]
250
true
4
[1, 2, 3, 5, 8, 9]
<1><2><3><5><8><9>
```

Read `nums.filter(fn(n) => n > 2).map(fn(n) => n * 10)` left to right: keep the
numbers above 2, then multiply each by 10. Chaining small steps like this is often
clearer than one loop that does everything.

[`Array`](../std/collections/array.md) lists all of them, and the other
collections have similar methods.

## Summary

- `fn(A, B) -> R` is a function type; named functions, lambdas and bound methods all fit.
- `fn(x: int) => x * 2` for one expression; `fn(x: int) -> int { ... }` for a block.
- Parameter types can be left out where the expected function type is known.
- Closures copy the variables they use, when they are created; share changing
  state through an object.
- A lambda stored in a name it uses can call itself (with a result type written).
- `map`, `filter`, `reduce`, `sortBy` and friends take lambdas.

## Exercises

1. Write `fn compose(f: fn(int) -> int, g: fn(int) -> int) -> fn(int) -> int`
   that returns a function computing `g(f(x))`.
2. Given an `Array<string>` of words, use `filter` and `mapTo` to get the lengths
   of the words longer than 3 letters.
3. Sort an `Array<string>` by length, longest first, with `sortBy`.
4. Explain why this prints 1, not 2, then change it so it prints 2:
   `let x = 1; let f = fn() => x; x = 2; print(f())`.
5. Build a tiny event system: a class with an `Array<fn(string)>` of listeners,
   `on(listener)` to add one, and `emit(message)` to call them all.

Next: [Arrays and collections →](07-arrays-and-collections.md)
