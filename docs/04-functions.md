# 4. Functions

## Declaring functions

```grav
fn add(a: int, b: int) -> int {
    return a + b
}

fn greet(name: string) {            // no `->` means `-> void`
    print("hi ${name}")
}

fn main() {
    greet("Ada")
    print(add(2, 3))
}
```

Output:

```text
hi Ada
5
```

Parameters are passed by value: numbers, booleans, structs, and arrays are copied;
class instances are references, so a function can modify the object it receives.
A function with a non-`void` return type must return on every path — the compiler
reports `may reach the end without returning a value` otherwise.

## Default values and named arguments

A parameter may have a default value, used when a call leaves the argument out;
parameters after one with a default need defaults too. Any argument can also be
passed by name, in any order after the positional ones. Both work for functions,
methods and constructors.

```grav
fn greet(name: string, greeting: string = "Hello", times: int = 1) -> string {
    let out = ""
    for (i in 0..times) { out += "${greeting}, ${name}! " }
    return out
}

class Window {
    public w: int
    public h: int
    constructor(w: int = 640, h: int = 480) { self.w = w; self.h = h }
}

fn main() {
    print(greet("Ada"))
    print(greet("Ada", "Hi"))
    print(greet("Ada", times: 2))
    print(greet(times: 1, greeting: "Hey", name: "Bob"))
    let win = new Window(h: 600)
    print("${win.w}x${win.h}")
}
```

Output:

```text
Hello, Ada! 
Hi, Ada! 
Hello, Ada! Hello, Ada! 
Hey, Bob! 
640x600
```

A default is evaluated each time a call needs it, as written where the function is
declared: it can use that file's constants and functions, not the caller's locals
or the other parameters.

## Recursion

```grav
fn fib(n: int) -> int { return n < 2 ? n : fib(n - 1) + fib(n - 2) }
fn factorial(n: int) -> long { return n <= 1 ? 1 : (n as long) * factorial(n - 1) }

fn main() {
    print(fib(20))
    print(factorial(20))
}
```

Output:

```text
6765
2432902008176640000
```

## Functions inside functions

A `fn` declared inside a block is a local function: it can call itself, and it
sees the block's variables declared before it (copied in, like a closure's
captures — see [Closures](10-lambdas.md#closures)). It cannot have type parameters
or default values.

```grav
fn main() {
    let indent = "  "
    fn show(depth: int, label: string) {
        let pad = ""
        for (i in 0..depth) { pad += indent }
        print(pad + label)
    }
    fn countdown(n: int) -> string { return n == 0 ? "go" : str(n) + " " + countdown(n - 1) }
    show(0, "root")
    show(2, countdown(3))
}
```

Output:

```text
root
    3 2 1 go
```

## Overloading

Several functions may share a name if their parameter types differ. The call picks
the exact match, or else the single candidate whose parameters accept the arguments.

```grav
fn describe(x: int) -> string { return "int ${x}" }
fn describe(x: float) -> string { return "float ${x}" }
fn describe(x: string) -> string { return "text '${x}'" }
fn describe(x: int, y: int) -> string { return "point (${x}, ${y})" }

fn main() {
    print(describe(1))
    print(describe(2.5))
    print(describe("hi"))
    print(describe(3, 4))
}
```

Output:

```text
int 1
float 2.5
text 'hi'
point (3, 4)
```

## Variadic parameters

The last parameter can collect any number of arguments with `...name: T`. Inside the
function it behaves like an array: index it, take `.length`, or loop over it.

```grav
fn sum(...xs: int) -> int {
    let total = 0
    for (x in xs) { total += x }
    return total
}

fn label(prefix: string, ...parts: string) -> string {
    let out = prefix
    for (p in parts) { out += " " + p }
    return out + " (${parts.length})"
}

fn main() {
    print(sum())
    print(sum(1, 2, 3, 4))
    print(label("colors:", "red", "green"))
}
```

Output:

```text
0
10
colors: red green (2)
```

To pass the elements of an array (a fixed array, an `Array<T>` variable, or another
variadic parameter), spread it with `...`: `sum(...scores)`.

## Generic functions

Type parameters go in angle brackets after the name. Call sites can pass the types
with `::<...>` or let the compiler infer them from the arguments. See
[Generics](09-generics.md) for constraints, generic classes, and inference.

```grav
fn larger<T>(a: T, b: T) -> T { return a > b ? a : b }

fn main() {
    print(larger(3, 7))                  // T = int, inferred
    print(larger::<float>(2.5, 1.5))     // or explicit
}
```

Output:

```text
7
2.5
```

## Globals

Top-level `let` and `const` declare module-level values shared by every function
(and every imported file).

```grav
let calls: int = 0
const NAME: string = "counter"

fn tick() -> int {
    calls += 1
    return calls
}

fn main() {
    tick()
    tick()
    print("${NAME}: ${tick()}")
}
```

Output:

```text
counter: 3
```

## Functions as values

Named functions can be stored in variables and passed around; anonymous functions
(lambdas) are written `fn(x) => x * 2`. That has its own chapter:
[Lambdas & closures](10-lambdas.md).

## async / await

`async fn` declares a function whose calls run concurrently on a pool of worker
threads. The call returns a `Future<T>` immediately; `await` waits for the result.

```grav
import "../lib/std.grav"

async fn fetchScore(name: string) -> int {
    Async.sleepMs(100)                    // pretend to do slow work
    return Str.length(name) * 10
}

fn main() {
    let a = fetchScore("ada")             // both start right away...
    let b = fetchScore("grace")
    print(await a + await b)              // ...and run at the same time
}
```

Output:

```text
80
```

Concurrency has its own chapter: [Concurrency](16-concurrency.md).

Next: [Data types →](05-data-types.md)
