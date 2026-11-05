# 5. Functions

A function gives a name to a piece of work. Instead of repeating the same lines
in three places, you write them once and *call* the function from each place.
Good function names also make code read like a description of what it does:
`if (isValid(order)) { ship(order) }`.

In this chapter you will learn:

- how to declare and call functions, with parameters and a result,
- what happens to arguments: copies and references,
- default values and named arguments,
- overloading: several functions with one name,
- functions that take any number of arguments,
- recursion, and functions declared inside other functions.

## Declaring and calling

```grav
fn add(a: int, b: int) -> int {
    return a + b
}

fn greet(name: string) {
    print("Hello, ${name}!")
}

fn main() {
    greet("Ada")
    let sum = add(2, 3)
    print(sum)
    print(add(add(1, 2), 10))
}
```

Output:

```text
Hello, Ada!
5
13
```

The parts of a declaration:

```text
fn add(a: int, b: int) -> int { ... }
   │   └─ parameters ─┘   │    └─ body
   name             result type
```

- **Parameters** are the inputs, each with a name and a type. Their types are
  always written: they are the function's contract with its callers.
- **The result type** comes after `->`. A function without one returns nothing
  (`void`), like `greet`.
- **`return value`** ends the function and hands the value back. In a `void`
  function, a bare `return` ends it early.

A function that promises a result must return one on every path. The compiler
checks this:

```text
fn sign(n: int) -> int {
    if (n > 0) { return 1 }
    if (n < 0) { return -1 }
}   // error: function 'sign' may reach the end without returning a value of type int
```

The order of declarations in a file does not matter. `main` can call a function
declared below it.

## What happens to arguments

When you call a function, each parameter gets the value of its argument. What
"the value" is depends on the type:

- **Numbers, booleans, structs and fixed arrays** are copied. Changing the
  parameter inside the function does not affect the caller's variable.
- **Strings** cannot be changed at all, so sharing them is safe.
- **Objects** (instances of classes, including collections like `Array`) are
  passed as *references*: the function receives the same object, and changes it
  makes to the object are seen by the caller.

```grav
import "std"

fn bump(n: int) {
    n += 1                       // changes only the local copy
}

fn addItem(list: Array<string>) {
    list.push("new")             // changes the caller's object
}

fn main() {
    let count = 1
    bump(count)
    print(count)

    let items: Array<string> = ["old"]
    addItem(items)
    print(items)
}
```

Output:

```text
1
["old", "new"]
```

This difference matters a lot once you use classes;
[chapter 10](10-classes.md) comes back to it.

## Default values and named arguments

A parameter can have a default value, used when the caller leaves the argument
out. Any argument can also be given by name, which makes calls with several
options readable:

```grav
fn greet(name: string, greeting: string = "Hello", times: int = 1) -> string {
    let out = ""
    for (i in 0..times) { out += "${greeting}, ${name}! " }
    return out
}

fn main() {
    print(greet("Ada"))
    print(greet("Ada", "Hi"))
    print(greet("Ada", times: 2))
    print(greet(times: 1, greeting: "Hey", name: "Bob"))
}
```

Output:

```text
Hello, Ada! 
Hi, Ada! 
Hello, Ada! Hello, Ada! 
Hey, Bob! 
```

The rules:

- Once a parameter has a default, the ones after it need defaults too.
- Named arguments can come in any order, but after all positional ones.
- A default is evaluated each time a call needs it, in the context where the
  function is declared: it can use constants and functions from that file, but
  not the caller's variables or the other parameters.

Defaults and named arguments work the same way for methods and constructors.

## Overloading

Several functions can share a name if their parameters differ. Each call picks
the one whose parameters fit its arguments, preferring an exact match:

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

Overloading suits functions that do the same job for different kinds of input.
If two overloads would fit a call equally well, the compiler reports the call as
ambiguous and lists them.

## Any number of arguments

A last parameter written `...name: T` collects any number of arguments of type
`T`. Inside the function it works like an array: loop over it, index it, ask its
`.length`.

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

If you already have the values in an array or an `Array<T>`, *spread* it into
the call with `...`:

```grav
import "std"

fn sum(...xs: int) -> int {
    let total = 0
    for (x in xs) { total += x }
    return total
}

fn main() {
    let scores: Array<int> = [70, 85, 90]
    print(sum(...scores))
}
```

Output:

```text
245
```

## Recursion

A function can call itself. Each call gets its own parameters and variables, so
a problem can be solved by reducing it to a smaller version of itself:

```grav
fn factorial(n: int) -> long {
    if (n <= 1) { return 1 }          // the base case stops the recursion
    return (n as long) * factorial(n - 1)
}

fn fib(n: int) -> int {
    return n < 2 ? n : fib(n - 1) + fib(n - 2)
}

fn main() {
    print(factorial(20))
    print(fib(20))
}
```

Output:

```text
2432902008176640000
6765
```

Every recursive function needs a *base case* that returns without calling
itself. Without one, the calls never end, and the program stops with a
stack-overflow panic.

## Functions inside functions

A function can be declared inside a block. It can only be used inside that
block, keeps helper code next to its one user, and can read the variables
declared before it:

```grav
fn main() {
    let indent = "  "
    fn show(depth: int, label: string) {
        let pad = ""
        for (i in 0..depth) { pad += indent }
        print(pad + label)
    }
    fn countdown(n: int) -> string {
        return n == 0 ? "go" : str(n) + " " + countdown(n - 1)
    }
    show(0, "root")
    show(2, countdown(3))
}
```

Output:

```text
root
    3 2 1 go
```

The variables it uses are *copied* in when the function is declared, just like
a lambda's ([chapter 6](06-lambdas-and-closures.md)), so it sees their values
at that point and cannot assign to them. A local function can call itself, but
cannot have type parameters or default values.

## Functions are values

A function's name, without parentheses, is a value you can store and pass
around:

```grav
fn double(x: int) -> int { return x * 2 }
fn square(x: int) -> int { return x * x }

fn applyTwice(f: fn(int) -> int, x: int) -> int {
    return f(f(x))
}

fn main() {
    print(applyTwice(double, 3))
    print(applyTwice(square, 3))
    let op = square
    print(op(9))
}
```

Output:

```text
12
81
81
```

`fn(int) -> int` is the type "a function that takes an int and returns an int".
Functions written inline, without a name, are *lambdas*. They are the subject of
the next chapter.

## Looking ahead

Two kinds of function get their own chapters:

- **Generic functions** work with many types: `fn larger<T>(a: T, b: T) -> T`.
  See [Generics](13-generics.md).
- **`async` functions** run in the background and return a `Future`, which you
  `await` for the result. See [Concurrency](18-concurrency.md).

## Summary

- `fn name(param: Type) -> Result { ... }`; no `->` means the function returns nothing.
- A function with a result must `return` on every path.
- Numbers, booleans and structs are copied into parameters; objects are shared.
- Parameters can have defaults; arguments can be named.
- Functions can be overloaded by parameter types.
- `...xs: T` takes any number of arguments; `f(...array)` spreads an array into a call.
- Functions can recurse, be declared inside blocks, and be used as values.

## Exercises

1. Write `fn isPrime(n: int) -> bool` and use it to print the primes below 50.
2. Write `fn max(...xs: int) -> int` that returns the largest argument. What
   should it do with no arguments? (Returning 0, or making the first parameter
   required, are both reasonable; try `fn max(first: int, ...rest: int)`.)
3. Write `fn repeat(text: string, times: int = 2, separator: string = " ")` and
   call it with each combination of named arguments.
4. Write a recursive `fn power(base: int, exp: int) -> long`. How many calls does
   `power(2, 10)` make? Can you make it fewer by using `power(base, exp / 2)`?
5. Write two overloads `area(radius: float)` and `area(width: float, height: float)`.

Next: [Lambdas and closures →](06-lambdas-and-closures.md)
