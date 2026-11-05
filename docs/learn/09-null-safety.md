# 9. Null safety

Some values are sometimes missing: a user without a nickname, a search that
found nothing, a setting that was never set. Most languages represent "missing"
with a special value called `null`, and most crashes in those languages come from
using a `null` as if it were a real value. Grav gives you tools to say clearly
which values can be missing and to handle them safely.

In this chapter you will learn:

- which types can be `null`, and how to check for it,
- `??` (use a fallback) and `?.` (skip a member access),
- nullable types `T?` for numbers, strings and structs,
- `x!` and *narrowing*: getting the value out once you know it is there,
- `Option<T>`, a data enum for values that may be absent.

## Which values can be null

Two kinds of type can be `null`:

- **References**: objects (instances of classes), interfaces, function values and pointers.
  These have always been able to be `null`, because a reference can point
  nowhere.
- **Nullable types** written with a question mark, like `int?` or `string?`.
  These are covered below.

Everything else, `int`, `float`, `bool`, `string`, enums and structs, **can
never be null**. If a function takes an `int`, it always gets a number; there is
nothing to check.

```grav
class Node {
    public label: string
    public next: Node
    constructor(label: string) { self.label = label; self.next = null }
}

fn main() {
    let head = new Node("first")
    print(head.next == null)
    head.next = new Node("second")
    print(head.next.label)
}
```

Output:

```text
true
second
```

Reading a field or calling a method on a `null` reference stops the program
with a "null reference" panic and the call stack. The tools below help you avoid
that.

## `??`: a fallback

`a ?? b` is `a`, unless `a` is null, in which case it is `b`:

```grav
class User {
    public name: string
    constructor(name: string) { self.name = name }
}

fn find(name: string) -> User {
    if (name == "ada") { return new User("Ada Lovelace") }
    return null
}

fn main() {
    let guest = new User("guest")
    print((find("ada") ?? guest).name)
    print((find("bob") ?? guest).name)
}
```

Output:

```text
Ada Lovelace
guest
```

`a` is evaluated once, and `b` only when it is needed.

## `?.`: skip when null

`a?.member` reads `a.member`, or gives a *zero value* (`0`, `false`, `""` or
`null`) when `a` is null, instead of crashing:

```grav
class Node {
    public label: string
    public next: Node
    constructor(label: string) { self.label = label; self.next = null }
}

fn main() {
    let head = new Node("first")
    print(head.next?.label == "")
    head.next = new Node("second")
    print(head.next?.label)
}
```

Output:

```text
true
second
```

`?.` also works for method calls: `a?.describe()` calls `describe` only when
`a` is not null. Its arguments are only evaluated when the call happens.

## Nullable values: `T?`

What about a number that might be missing, like "the position of `x` in this
list, if it is there"? Returning `-1` for "not found" is a common trick, but
nothing stops a caller from using that `-1` as a real position.

Add `?` to a type to allow `null`: an `int?` holds either an `int` or `null`.

```grav
import "std"

fn find(xs: Array<string>, want: string) -> int? {
    for (i in 0..xs.size()) {
        if (xs[i] == want) { return i }
    }
    return null
}

fn main() {
    let names: Array<string> = ["ann", "bob"]
    let i = find(names, "bob")
    let j = find(names, "cy")
    print("${i} ${j}")
}
```

Output:

```text
1 null
```

A plain value goes into a `T?` without any ceremony (`return i` above returns an
`int` from a function declared `int?`). The other direction is guarded: you
cannot use an `int?` where an `int` is needed until you have dealt with the
possibility of `null`.

```text
let k: int = j       // error: cannot initialize 'k' of type int with a value of type int?
                     //   (it may be null: use 'x!', 'x ?? fallback', or check 'x != null' first)
print(j + 1)         // error: operator '+' cannot use int?, which may be null
```

That error is the whole point: the compiler makes sure every place that uses
the value has thought about the missing case.

Which types can be nullable:

- `int?`, `float?`, `bool?` and the other number types,
- `string?`,
- `Color?` for an enum,
- `Point?` for a struct whose fields are plain values (no strings or objects inside).

Classes, interfaces and functions can already be null, so `User?` just means `User`.

### Getting the value out

There are three ways, from bluntest to most careful.

**`x!`** says "I know this is not null". It gives the value, or throws a
`StateError` if you were wrong:

```grav
import "std"

fn main() {
    let a: int? = 5
    print(a! + 1)
    let b: int? = null
    try {
        print(b! + 1)
    } catch (e: StateError) {
        print(e.message())
    }
}
```

Output:

```text
6
'!' found a null int?
```

**`x ?? fallback`** gives the value, or the fallback when `x` is null. The
result is a plain `T`:

```grav
fn main() {
    let nickname: string? = null
    let retries: int? = 3
    print(nickname ?? "anonymous")
    print((retries ?? 1) * 2)
}
```

Output:

```text
anonymous
6
```

**A null check** is the most natural: inside `if (x != null) { ... }`, the
compiler knows `x` has a value, so there `x` *is* a plain `T`:

```grav
import "std"

fn find(xs: Array<string>, want: string) -> int? {
    for (i in 0..xs.size()) {
        if (xs[i] == want) { return i }
    }
    return null
}

fn position(xs: Array<string>, want: string) -> string {
    let i = find(xs, want)
    if (i == null) {
        return "${want} is missing"
    }
    return "${want} is number ${i + 1}"    // i is an int here
}

fn main() {
    let names: Array<string> = ["ann", "bob"]
    print(position(names, "bob"))
    print(position(names, "cy"))
}
```

Output:

```text
bob is number 2
cy is missing
```

This is called *narrowing*. The compiler narrows a local variable `x` of type
`T?` to `T`:

- inside `if (x != null) { ... }`, and in the `else` of `if (x == null)`,
- after `if (x == null) { return ... }` (or `throw`, `break`, `continue`), for the
  rest of the block,
- on the right of `x != null && ...`, and of `x == null || ...`,
- in the branches of `x != null ? a : b`.

It only narrows when nothing in that region assigns to `x`, because an
assignment could put `null` back.

### Comparing and printing

A `T?` can be compared with `==` and `!=` against `null`, a `T`, or another
`T?`, and it prints as its value or `null`. It does not support arithmetic or
`<`/`>` directly; get the value out first.

```grav
fn main() {
    let a: int? = 5
    let b: int? = null
    print(a == 5)
    print(a == b)
    print(b == null)
    print("a=${a}, b=${b}")
}
```

Output:

```text
true
false
true
a=5, b=null
```

Nullable fields and parameters work the same way, including default values:
`fn greet(name: string?, times: int? = null)`.

## Option: absence as a value

`Option<T>` from the standard library is a data enum with two variants:
`Some(value)` and `None`. It does the same job as `T?`, but as an ordinary value
with methods, which makes it pleasant for chaining:

```grav
import "std"

fn parsePort(s: string) -> Option<int> {
    if (!Format.isInt(s)) { return Option.None }
    let n = Format.toInt(s)
    if (n < 1 || n > 65535) { return Option.None }
    return Option.Some(n)
}

fn main() {
    print(parsePort("8080"))
    print(parsePort("abc"))
    print(parsePort("80").map(fn(p: int) => p + 1).unwrapOr(0))
    match (parsePort("443")) {
        case Some(p): print("port ${p}")
        case None: print("no port")
    }
}
```

Output:

```text
Some(8080)
None
81
port 443
```

`Option` has `isSome`, `isNone`, `unwrap`, `unwrapOr`, `map`, `andThen`,
`filter` and more; see [`Option`](../std/core/result.md). There is also an
`Optional<T>` class with the same idea in a Java-like style
([`Optional`](../std/core/optional.md)).

Which one should you use? `T?` is the simplest, and the compiler checks it.
`Option<T>` suits code that transforms the value through several steps, or
generic code where `T` may itself be a class. When a value is missing *because
something went wrong*, use `Result` or an exception instead, so the reason is
not lost ([Errors](14-errors.md)).

## Summary

- Objects, interfaces, functions and pointers can be `null`; `int`, `string`,
  enums and structs cannot, unless you write `T?`.
- `a ?? b` falls back to `b` when `a` is null; `a?.m` skips the access.
- A `T?` is used as a `T` after `x!` (which throws on null), with `x ?? fallback`,
  or inside a null check, where the compiler narrows it.
- `Option<T>` is a data enum (`Some`/`None`) with methods for chaining.

## Exercises

1. Write `fn firstWord(s: string) -> string?` that returns `null` for a blank
   string. Print the first word of three strings, using `??` for the blank one.
2. Write `fn average(xs: Array<int>) -> float?` returning `null` for an empty
   array, and use narrowing to print the average or "no data".
3. Give a class a field `manager: Employee` that can be null. Print each
   employee's manager's name, or "none", using `?.` and `??`.
4. Rewrite exercise 2 with `Option<float>` and `unwrapOr`.
5. What does the compiler say about `let x: int? = 5; x = null; print(x + 1)`? Fix it
   three different ways.

Next: [Classes →](10-classes.md)
