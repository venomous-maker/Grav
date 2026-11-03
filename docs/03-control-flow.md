# 3. Control flow

Conditions must be `bool` — there is no "truthiness" for numbers or strings.

## if / else

```grav
fn grade(score: int) -> string {
    if (score >= 90) { return "A" }
    else if (score >= 75) { return "B" }
    else { return "C" }
}

fn main() {
    print(grade(95))
    print(grade(80))
    print(grade(10))
}
```

Output:

```text
A
B
C
```

## while and do/while

```grav
fn main() {
    let n = 1
    while (n < 100) { n *= 3 }
    print(n)

    let tries = 0
    do { tries++ } while (tries < 3)   // the body runs at least once
    print(tries)
}
```

Output:

```text
243
3
```

## C-style for

```grav
fn main() {
    let total = 0
    for (let i = 1; i <= 10; i++) { total += i }
    print(total)
}
```

Output:

```text
55
```

## Ranges

`for (i in a..b)` counts from `a` up to (not including) `b`; `..=` includes `b`. An
optional `step` (which may be negative) changes the increment. The bounds are
evaluated once.

```grav
import "../lib/std.grav"

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

## For-each

`for (x in xs)` iterates the elements of a fixed array, a variadic parameter, a
`binary` value, or any collection object that has `length()`/`size()` and
`get(i)`/`at(i)` (so `Array`, `List`, and your own classes work). The two-name form
also binds the index.

```grav
import "../lib/std.grav"

fn main() {
    let primes = [2, 3, 5, 7]
    for (p in primes) { printRaw("${p} ") }
    print("")

    let names = new Array<string>()
    names.push("ada")
    names.push("grace")
    for (i, name in names) { print("${i}: ${name}") }
}
```

Output:

```text
2 3 5 7 
0: ada
1: grace
```

## break and continue

`break` leaves the innermost loop; `continue` skips to its next iteration.

```grav
import "../lib/std.grav"

fn main() {
    for (i in 1..100) {
        if (i % 2 == 0) { continue }
        if (i > 9) { break }
        printRaw("${i} ")
    }
    print("")
}
```

Output:

```text
1 3 5 7 9 
```

## switch / match

`switch` and `match` are synonyms. Each case has a block (or a single statement),
there is no fall-through, a case may list several values, and `default` handles
the rest. Subjects can be integers, strings, enums, or data enums; matching a data
enum binds the variant's fields (see [Data enums](05-data-types.md#data-enums-and-pattern-matching)).

```grav
enum Shape { Circle, Square, Triangle }

fn sides(s: Shape) -> int {
    match (s) {
        case Shape.Circle:   { return 0 }
        case Shape.Square:   { return 4 }
        default:             { return 3 }
    }
}

fn kind(day: string) -> string {
    switch (day) {
        case "sat", "sun": { return "weekend" }
        default:           { return "weekday" }
    }
}

fn main() {
    print(sides(Shape.Square))
    print(kind("sun"))
    print(kind("tue"))
}
```

Output:

```text
4
weekend
weekday
```

Inside a loop, `break` in a case body leaves the loop (cases never fall through,
so there is nothing else for it to mean).

## defer

`defer stmt` or `defer { ... }` schedules code to run when the enclosing block exits —
at its end, or through `return`, `break`, or `continue`. Deferred code runs in
reverse order of registration, after a `return` value has been computed.

```grav
fn work(fail: bool) -> string {
    print("open")
    defer print("close")                 // runs last
    defer { print("flush") }             // runs first
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

`defer` does not run when an exception propagates out of the block — use
`finally` for that ([Exceptions](11-exceptions.md)). A deferred block cannot contain
`return`, `break`, or `continue`.

Next: [Functions →](04-functions.md)
