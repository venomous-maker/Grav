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

## Generic functions

Type parameters go in angle brackets after the name; call sites pass the types with
`::<...>`. See [Generics](09-generics.md) for constraints and generic classes.

```grav
fn larger<T>(a: T, b: T) -> T { return a > b ? a : b }

fn main() {
    print(larger::<int>(3, 7))
    print(larger::<float>(2.5, 1.5))
}
```

Output:

```text
7
2.500000
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

`async fn` declares a function whose callers receive a future; `await` gets its value.
Futures currently resolve eagerly (calls stay plain C calls), so `async`/`await` is a
way to write code that reads asynchronously today and can gain real concurrency later.

```grav
async fn fetchScore(name: string) -> int { return 10 }

async fn main() {
    let a = await fetchScore("ada")
    let b = await fetchScore("grace")
    print(a + b)
}
```

Output:

```text
20
```

Next: [Data types →](05-data-types.md)
