# 4. Control flow

So far every program ran from top to bottom, once. Real programs make decisions
and repeat things. This chapter covers the statements that do that.

In this chapter you will learn:

- `if` and `else` for decisions,
- loops: `while`, `do`/`while`, `for` over ranges, collections and strings, and the C-style `for`,
- how to leave or skip ahead in a loop, including from nested loops,
- `switch` and `match` for choosing among many cases, and `match` as an expression,
- `defer` for clean-up code that must run when a block ends.

## Decisions: if and else

```grav
fn main() {
    let temperature = 31
    if (temperature > 30) {
        print("hot")
    } else if (temperature > 15) {
        print("mild")
    } else {
        print("cold")
    }
}
```

Output:

```text
hot
```

The condition goes in parentheses and must be a `bool`. The branches are blocks
in braces; braces are required even for one statement, which avoids a classic
class of bugs where a second line looks like it belongs to the `if` but does not.

Grav checks the conditions one after another and runs the first branch whose
condition is true. The `else` branch runs when none was.

For choosing between two *values*, the conditional operator is shorter than an
`if`:

```grav
fn main() {
    let items = 1
    print("${items} ${items == 1 ? "item" : "items"}")
}
```

Output:

```text
1 item
```

## Loops

### while

`while` repeats its block as long as the condition is true. The condition is
checked before each round, so the body may run zero times.

```grav
fn main() {
    let n = 1
    while (n < 100) {
        n *= 3
    }
    print(n)
}
```

Output:

```text
243
```

### do / while

`do { ... } while (condition)` checks after each round, so the body always
runs at least once. It suits "try, then decide whether to try again":

```grav
fn main() {
    let attempts = 0
    do {
        attempts++
        print("attempt ${attempts}")
    } while (attempts < 3)
}
```

Output:

```text
attempt 1
attempt 2
attempt 3
```

### Counting with ranges

The most common loop counts through numbers. `a..b` is the range from `a` up
to, but not including, `b`; `a..=b` includes `b`:

```grav
import "std"

fn main() {
    for (i in 0..5) { printRaw("${i} ") }
    print("")
    for (i in 1..=5) { printRaw("${i} ") }
    print("")
    for (i in 0..=20 step 5) { printRaw("${i} ") }
    print("")
    for (i in 10..0 step -3) { printRaw("${i} ") }
    print("")
}
```

Output:

```text
0 1 2 3 4 
1 2 3 4 5 
0 5 10 15 20 
10 7 4 1 
```

(`printRaw` from the standard library prints without a line break.)

- `step` changes how much is added each time. A negative step counts down.
- The loop variable (`i`) is a new variable for the loop; it does not exist
  after it.
- The start, end and step are worked out once, before the first round. Changing
  a variable used in them inside the loop does not change the loop.

Why does `0..5` exclude 5? Because it matches how indexes work: a list of 5
elements has indexes `0..5`, and `for (i in 0..xs.size())` visits each exactly once.

### Looping over things: for-each

`for (x in collection)` visits each element in turn. It works with fixed arrays,
the standard collections (`Array`, `List`, `Deque` and others), strings (each
character), `binary` values (each byte) and variadic parameters:

```grav
import "std"

fn main() {
    let primes = [2, 3, 5, 7]
    let sum = 0
    for (p in primes) { sum += p }
    print(sum)

    let names: Array<string> = ["Ada", "Grace", "Barbara"]
    for (i, name in names) {
        print("${i + 1}. ${name}")
    }

    for (ch in "hey") { print(ch) }
}
```

Output:

```text
17
1. Ada
2. Grace
3. Barbara
h
e
y
```

The two-name form `for (i, x in xs)` also gives the position of each element.

Your own classes work in a for-each loop too, if they have a `size()` (or
`length()`) method and a `get(i)` (or `at(i)`) method.

Maps, and pairs in general, can be taken apart in the loop header:
`for ((key, value) in pairs)`. See [Structs, enums and tuples](08-structs-enums-tuples.md#tuples-and-destructuring).

### The C-style for

The three-part loop from C is there too: a start statement, a condition checked
before each round, and an update run after each round.

```grav
fn main() {
    let total = 0
    for (let i = 1; i <= 10; i++) {
        total += i
    }
    print(total)
}
```

Output:

```text
55
```

Prefer a range when you are counting; use this form when the update is unusual
(doubling, following a linked list, two variables at once).

## Leaving a loop early

`break` ends the loop at once. `continue` skips the rest of this round and
goes on with the next one.

```grav
import "std"

fn main() {
    for (i in 1..100) {
        if (i % 2 == 0) { continue }   // skip even numbers
        if (i > 9) { break }           // stop after 9
        printRaw("${i} ")
    }
    print("")
}
```

Output:

```text
1 3 5 7 9 
```

### Labels: leaving nested loops

`break` and `continue` act on the innermost loop. To act on an outer one, give
it a label, then name the label:

```grav
fn main() {
    search: for (i in 0..5) {
        for (j in 0..5) {
            if (i * j == 6) {
                print("found ${i} x ${j}")
                break search          // leaves both loops
            }
            if (j > i) { continue search }
        }
    }
}
```

Output:

```text
found 2 x 3
```

Labels must be unique among the loops they are nested in, and `break`/`continue`
must name a label of an enclosing loop. Clean-up code on the way out (`defer`,
`finally`) still runs.

## Choosing among many cases: switch and match

When one value decides between many branches, a chain of `else if`s gets long.
`switch` compares a value against each `case` in turn and runs the first that
fits. `match` is another name for the same statement; use whichever reads better.

```grav
fn kind(day: string) -> string {
    switch (day) {
        case "sat", "sun": { return "weekend" }
        case "fri":        { return "almost weekend" }
        default:           { return "weekday" }
    }
}

fn main() {
    print(kind("sun"))
    print(kind("fri"))
    print(kind("tue"))
}
```

Output:

```text
weekend
almost weekend
weekday
```

- A case can list several values separated by commas.
- Only the matching case runs. There is no "fall-through" into the next case,
  so there is no `break` to forget.
- `default` runs when no case fits.
- The value can be an integer, a string, an enum, or a data enum.

A case body is a block, or a single statement written after the colon.

### Ranges and guards

A case can be a range of numbers (or strings). A *guard*, `if condition`
before the colon, makes a case apply only when the condition is also true;
otherwise the following cases are tried:

```grav
fn describe(n: int) -> string {
    switch (n) {
        case 0: { return "zero" }
        case 1..10 if n % 2 == 0: { return "small and even" }
        case 1..10: { return "small and odd" }
        case 10..=99: { return "two digits" }
        default: { return "big" }
    }
}

fn main() {
    print(describe(4))
    print(describe(7))
    print(describe(42))
    print(describe(1000))
}
```

Output:

```text
small and even
small and odd
two digits
big
```

### Switching on enums

An *enum* is a type with a fixed set of named values ([chapter 8](08-structs-enums-tuples.md#enums)).
A `switch` over an enum without a `default` should handle every value: leaving
one out is a warning that names it, which is how the compiler reminds you to
update every `switch` when you add a new value.

```grav
enum Light { Red, Yellow, Green }

fn next(l: Light) -> Light {
    switch (l) {
        case Light.Red:    { return Light.Green }
        case Light.Green:  { return Light.Yellow }
        case Light.Yellow: { return Light.Red }
    }
    return l
}

fn main() {
    print(next(Light.Red))
    print(next(Light.Green))
}
```

Output:

```text
Green
Yellow
```

*Data enums*, whose values carry extra data, are matched the same way and
can bind that data to names (`case Circle(r): ...`). That is covered in
[chapter 8](08-structs-enums-tuples.md#data-enums-and-pattern-matching).

### match as an expression

Where a value is expected, `match` picks one. Each case gives an expression
instead of a block:

```grav
enum Size { Small, Medium, Large }

fn main() {
    let n = 3
    let word = match (n) { case 1: "one" case 2, 3: "a few" default: "many" }
    print(word)

    let s = Size.Medium
    let price = match (s) {
        case Size.Small: 2.5
        case Size.Medium: 3.0
        case Size.Large: 3.5
    }
    print(price)
}
```

Output:

```text
a few
3
```

A `match` expression must always produce a value, so it needs a `default`
unless its cases cover every value of an enum.

## Clean-up with defer

Some code must run when a block ends, however it ends: closing a file,
releasing a lock, printing a closing tag. `defer` schedules it right next to the
code that made it necessary:

```grav
fn work(fail: bool) -> string {
    print("open")
    defer print("close")         // runs last
    defer { print("flush") }     // runs first
    if (fail) { return "failed" }
    return "done"
}

fn main() {
    print(work(false))
    print(work(true))
}
```

Output:

```text
open
flush
close
done
open
flush
close
failed
```

- Deferred code runs when the enclosing block is left: at its end, or through
  `return`, `break` or `continue`.
- Several defers run in reverse order: the last one registered runs first, so
  things are undone in the opposite order they were done.
- In a `return`, the value is computed first, then the deferred code runs.
- A deferred block cannot itself `return`, `break` or `continue`.
- `defer` does **not** run when an exception leaves the block. For code that must
  run even then, use `try`/`finally` ([Errors](14-errors.md)).

## Summary

- `if`/`else if`/`else` needs `bool` conditions and braces.
- `while` checks first, `do`/`while` checks last.
- `for (i in a..b)` counts (`..=` includes the end, `step` changes the stride);
  `for (x in xs)` and `for (i, x in xs)` walk collections and strings.
- `break` and `continue` work on the innermost loop, or on a labeled one.
- `switch`/`match` picks one case, never falls through, and supports value lists,
  ranges, guards and enums; `match` also works as an expression.
- `defer` runs clean-up code, last-in first-out, when a block ends.

## Exercises

1. Print the numbers 1 to 30, but print `Fizz` for multiples of 3, `Buzz` for
   multiples of 5 and `FizzBuzz` for multiples of both.
2. Find the first number above 1000 that is divisible by 7, 11 and 13, using a
   `while` loop and `break`.
3. Print a right-angled triangle of `*` with 5 rows using two nested range loops.
4. Write `fn season(month: int) -> string` with a `switch` on ranges
   (`3..=5` is spring, and so on).
5. Write a function that prints `start`, then uses `defer` to print `end`, and
   returns early from the middle. Check that `end` is still printed.

Next: [Functions →](05-functions.md)
