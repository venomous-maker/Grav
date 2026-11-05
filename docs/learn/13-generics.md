# 13. Generics

You have been using generics since chapter 7: `Array<string>` and
`HashMap<string, int>` are one class, `Array`, used with different element types.
This chapter shows how to write your own. A generic class or function is written
once with a *type parameter*, a placeholder like `T`, and works for any type you
fill in.

In this chapter you will learn:

- generic classes and structs, and generic functions,
- type parameters with *bounds*: "any type that implements `Named`",
- generic interfaces, inheritance, static members and methods,
- how type arguments are inferred, and `auto`,
- how generics compile (and why they cost nothing at run time).

## Why generics

Imagine a box that holds one value. Without generics you would need `IntBox`,
`StringBox`, `UserBox`, each the same code with a different type. Or you could
use `any` and lose type checking. A generic class is written once:

```grav
class Box<T> {
    private value: T
    constructor(v: T) { self.value = v }
    fn get() -> T { return self.value }
    fn set(v: T) { self.value = v }
}

fn main() {
    let n = new Box<int>(5)
    n.set(n.get() * 2)
    let s = new Box<string>("hi")
    print("${n.get()} ${s.get()}")
}
```

Output:

```text
10 hi
```

`T` stands for "some type, chosen by whoever uses `Box`". `Box<int>` fills it in
with `int`: its `get` returns an `int`, and `n.set("x")` is a compile error. You
get the reuse of `any` with the safety of real types.

By convention, type parameters are single capital letters: `T` for a type, `K`
and `V` for keys and values, `A`, `B` for several unrelated types.

## Generic functions

Functions can have type parameters too, written after the name:

```grav
fn larger<T>(a: T, b: T) -> T {
    return a > b ? a : b
}

fn describe<K, V>(k: K, v: V) -> string {
    return "${k} -> ${v}"
}

fn main() {
    print(larger(3, 7))                 // T is inferred as int
    print(larger("pear", "apple"))      // T is string
    print(larger::<float>(2.5, 1))      // or given explicitly
    print(describe("pi", 3.14))
}
```

Output:

```text
7
pear
2.5
pi -> 3.14
```

You rarely have to write the type arguments: the compiler works them out from
the arguments. When you want to, put them after `::`, as in `larger::<float>(...)`.

How does `larger` know that `a > b` works? It is checked for each type it is used
with: `larger(3, 7)` is fine, but calling it with two objects that have no
`operator<` is an error at that call. Bounds, below, let you state such
requirements up front.

## Generic structs, interfaces and inheritance

Structs, interfaces and subclasses can all be generic:

```grav
struct Pair<A, B> { first: A  second: B }

interface Source<T> {
    fn next() -> T
}

class Counter implements Source<int> {
    private n: int
    constructor() { self.n = 0 }
    fn next() -> int { self.n += 1; return self.n }
}

class Stack<T> {
    protected items: T[8]
    protected size: int
    constructor() { self.size = 0 }
    fn push(x: T) { self.items[self.size] = x; self.size += 1 }
    fn pop() -> T { self.size -= 1; return self.items[self.size] }
}

class LoggedStack<T> extends Stack<T> {
    fn push(x: T) {
        print("push ${x}")
        super.push(x)
    }
}

fn main() {
    let p = Pair<string, int> { first: "age", second: 36 }
    print("${p.first}=${p.second}")
    let src: Source<int> = new Counter()
    src.next()
    print(src.next())
    let st = new LoggedStack<string>()
    st.push("a")
    st.push("b")
    print(st.pop())
}
```

Output:

```text
age=36
2
push a
push b
b
```

- `Counter implements Source<int>`: a class can implement a generic interface
  for one particular type.
- `LoggedStack<T> extends Stack<T>`: a generic class can extend another, passing
  its own type parameter along.

## Bounds: requiring abilities

Inside a generic function, you can only do with a `T` what works for every type
it is used with. A *bound* narrows that down: `T: Named` means "any type that is
or implements `Named`", and in exchange you may call `Named`'s methods on it:

```grav
interface Named {
    fn name() -> string
}

class City implements Named {
    private n: string
    constructor(n: string) { self.n = n }
    fn name() -> string { return self.n }
}

fn shout<T: Named>(x: T) -> string {
    return x.name() + "!"
}

fn main() {
    print(shout(new City("Nairobi")))
}
```

Output:

```text
Nairobi!
```

Calling `shout(42)` is an error at the call: `int` is not `Named`. A bound can
be a class or an interface, and it can itself be generic:
`fn maxOf<T: Comparable<T>>(a: T, b: T) -> T`.

## Static members and generic methods

Static members of a generic class are reached through a filled-in name, like
`Stack<int>.of(3)`. And any method, generic class or not, can have type parameters
of its own:

```grav
class Cell<T> {
    private v: T
    constructor(v: T) { self.v = v }
    fn get() -> T { return self.v }
    fn map<U>(f: fn(T) -> U) -> Cell<U> { return new Cell<U>(f(self.v)) }
    static fn of(v: T) -> Cell<T> { return new Cell<T>(v) }
}

class Util {
    static fn pair<A, B>(a: A, b: B) -> string { return "(${a}, ${b})" }
}

fn main() {
    let c = Cell<int>.of(21)
    let label = c.map(fn(n) => "#${n * 2}")     // U is inferred as string
    print(label.get())
    print(Util.pair(1, true))
}
```

Output:

```text
#42
(1, true)
```

## How type arguments are inferred

The compiler fills in missing type arguments from:

- **the call's arguments**: `larger(3, 9)` means `T = int`;
- **constructor arguments**: `new Box(5)` is a `Box<int>`;
- **lambda results**: `c.map(fn(n) => "#${n}")` gives `U = string`;
- **where the value goes**: a typed `let`, a field, a parameter, a `return`.

`auto` asks for inference explicitly, which is handy when the type is long and
already written elsewhere:

```grav
import "std"

class Registry {
    private names: Array<string>
    constructor() { self.names = new Array<auto>() }    // the field says Array<string>
    fn add(n: string) { self.names.push(n) }
    fn count() -> int { return self.names.size() }
}

fn main() {
    let scores: HashMap<string, int> = new HashMap<auto, auto>()
    scores["ada"] = 99
    print(scores["ada"])
    let r = new Registry()
    r.add("x")
    print(r.count())
}
```

Output:

```text
99
1
```

When nothing tells the compiler what a type argument is, for example
`let xs = new Array<auto>()` on its own, it asks you to write it out. `auto` is
only for type arguments and for `let x: auto = ...`.

## How generics compile

Each different set of type arguments becomes its own copy of the code: `Box<int>`
and `Box<string>` turn into two separate C structs with their own functions. This
is called *monomorphization*. It means:

- generic code runs exactly as fast as code written by hand for each type: no
  boxing, no run-time type checks;
- the program gets a little bigger for each type a generic is used with;
- errors are reported at the place where a type does not fit, with the type
  arguments shown (`Box<int>`).

## Generic aliases

Aliases can be generic too:

```grav
type Triple<T> = T[3]

fn main() {
    let t: Triple<int> = [1, 2, 3]
    print(t[0] + t[1] + t[2])
}
```

Output:

```text
6
```

## Summary

- `class Box<T>`, `struct Pair<A, B>`, `interface Source<T>`, `fn larger<T>(...)`
  and `fn map<U>(...)` methods take type parameters.
- Type arguments are mostly inferred; write them with `Name<...>` or `f::<...>(...)`.
- `T: Bound` requires a class or interface and allows calling its methods.
- `auto` asks for a type argument to be inferred from context.
- Each use with new types compiles to specialised code, with no run-time cost.

## Exercises

1. Write `class Pair<A, B>` with `first`, `second` and a `swap() -> Pair<B, A>` method.
2. Write `fn lastOf<T>(xs: Array<T>) -> T?` ... then notice that `T?` only works
   for some `T`. Write it to return `Option<T>` instead.
3. Write a generic `class Stack<T>` backed by an `Array<T>`, with `push`, `pop`,
   `peek` and `isEmpty`.
4. Write an interface `Shape` with `area()`, and `fn largest<T: Shape>(xs: Array<T>) -> T`.
5. Write `fn countIf<T>(xs: Array<T>, keep: fn(T) -> bool) -> int` and call it with
   an `Array<string>` and an `Array<int>`.

Next: [Errors and exceptions →](14-errors.md)
