# 14. Errors and exceptions

Things go wrong: files are missing, input is malformed, the network drops, a
bug divides by zero. A good program decides, for each kind of failure, who should
handle it and how. Grav gives you three tools, each for a different kind of
problem:

| Kind of problem | Example | Tool |
|-----------------|---------|------|
| an expected outcome that the caller must look at | "user not found", "not a number" | a return value: `Result<T, E>`, `Option<T>` or `T?` |
| an unusual condition, handled somewhere up the call chain | the file vanished, the server refused | an **exception**: `throw` and `try`/`catch` |
| a bug: the program is in a state it should never be in | an impossible case, a broken invariant | a **panic**: `panic(...)` stops the program |

In this chapter you will learn:

- how to throw and catch exceptions, and `finally`,
- the standard exception classes, and writing your own,
- the errors the language itself raises (index out of range, division by zero, ...),
- `Result<T, E>` for errors as values,
- panics, and recovering from them in servers and threads.

## Throwing and catching

`throw` stops the current function and hands an exception object to whoever
called it, then their caller, and so on, until a `try` block with a matching
`catch` handles it:

```grav
import "std"

fn divide(a: int, b: int) -> int {
    if (b == 0) { throw new ValueError("cannot divide by zero") }
    return a / b
}

fn main() {
    try {
        print(divide(10, 2))
        print(divide(1, 0))
        print("not reached")
    } catch (e: ValueError) {
        print("caught: ${e.message()}")
    }
    print("carrying on")
}
```

Output:

```text
5
caught: cannot divide by zero
carrying on
```

- Any object can be thrown, but by convention you throw a subclass of
  `Exception` from the standard library.
- `catch (e: ValueError)` handles `ValueError`s and its subclasses. The
  exception object is available as `e`.
- The rest of the `try` block is skipped. After the `catch`, the program
  continues after the whole `try` statement.

A `try` can have several `catch` clauses. The first one whose class matches
wins, so put the specific ones first:

```grav
import "std"

fn risky(n: int) {
    if (n == 1) { throw new IndexError("too far") }
    if (n == 2) { throw new IOError("disk unplugged") }
    throw new ValueError("bad number")
}

fn main() {
    for (n in 1..=3) {
        try {
            risky(n)
        } catch (e: IndexError) {
            print("index: ${e.message()}")
        } catch (e: Exception) {
            print("other: ${e.describe()}")
        }
    }
}
```

Output:

```text
index: too far
other: IOError: disk unplugged
other: ValueError: bad number
```

A `catch` that can never run, because an earlier one already catches a parent
class, is a compile error.

### What an exception carries

Every standard exception has:

- `message()`: the text it was created with,
- `kind()`: its kind, like `"ValueError"`,
- `describe()`: `"Kind: message"`, which is also what `print(e)` shows.

### finally

A `finally` block runs no matter how the `try` ends: normally, through a caught
exception, through an exception that keeps going, or through `return`, `break`
or `continue`. It is the place for clean-up:

```grav
import "std"

fn load(n: int) -> string {
    try {
        if (n < 0) { throw new ValueError("negative") }
        return "loaded ${n}"
    } catch (e: ValueError) {
        return "error: ${e.message()}"
    } finally {
        print("cleanup for ${n}")
    }
    return ""
}

fn main() {
    print(load(1))
    print(load(-1))
}
```

Output:

```text
cleanup for 1
loaded 1
cleanup for -1
error: negative
```

Unlike `defer` ([chapter 4](04-control-flow.md#clean-up-with-defer)), `finally`
also runs when an exception passes through.

### Passing an exception on

A `catch` can do something and then throw the same exception again, for
someone further up to handle:

```grav
import "std"

fn main() {
    try {
        try {
            throw new RuntimeError("disk full")
        } catch (e: RuntimeError) {
            print("logging: ${e.message()}")
            throw e
        } finally {
            print("inner finally")
        }
    } catch (e: Exception) {
        print("outer: ${e.describe()}")
    }
}
```

Output:

```text
logging: disk full
inner finally
outer: RuntimeError: disk full
```

Variables changed inside a `try` keep their latest values in its `catch`,
which helps to report how far you got:

```grav
import "std"

fn main() {
    let done = 0
    try {
        for (s in ["4", "8", "x", "16"]) {
            if (!Format.isInt(s)) { throw new ValueError("bad input '${s}'") }
            done += 1
        }
    } catch (e: ValueError) {
        print("stopped after ${done}: ${e.message()}")
    }
}
```

Output:

```text
stopped after 2: bad input 'x'
```

### Uncaught exceptions

An exception that nobody catches ends the program. It prints the exception and
where it was thrown, and exits with status 1:

```text
grav: uncaught exception: ValueError: cannot divide by zero
  stack trace (most recent call last):
    at main (app.grav:9)
    at divide (app.grav:4)
```

Builds made with `--release` or `--no-trace` leave the stack trace out, which
makes them a little faster.

## The standard exceptions

| Class | Means |
|-------|-------|
| `Exception` | the base of them all; catch it to catch everything |
| `ValueError` | an argument or input is unsuitable |
| `TypeError` | a value has the wrong type (for example a failed `as`) |
| `IndexError` | an index is out of range |
| `ArithmeticError` | integer division or `%` by zero |
| `StateError` | the operation does not fit the current state (popping an empty stack, `x!` on null) |
| `NotFound` | something looked up does not exist |
| `IOError` | a file, network or process operation failed |
| `TimeoutError` | something took too long |
| `RuntimeError` | anything else |

The standard library throws these when it is misused, and the language throws
some itself when the standard library is loaded:

```grav
import "std"

fn main() {
    let xs = new Array<int>()
    try { print(xs[3]) } catch (e: IndexError) { print(e.describe()) }

    let fixed = [1, 2, 3]
    let i = 7
    try { print(fixed[i]) } catch (e: IndexError) { print(e.describe()) }

    let zero = 0
    try { print(10 / zero) } catch (e: ArithmeticError) { print(e.describe()) }

    let empty = new Stack<int>()
    try { empty.pop() } catch (e: StateError) { print(e.describe()) }
}
```

Output:

```text
IndexError: Array index 3 out of range (length 0)
IndexError: array index 7 out of range (length 3)
ArithmeticError: integer division by zero
StateError: Stack.pop on an empty stack
```

Without `import "std"`, these checks still happen, but they panic instead of
throwing, because the exception classes are not there.

### Your own exceptions

Extend `Exception` to create your own kinds of errors. A hierarchy lets callers
catch a whole family at once:

```grav
import "std"

class AppError extends Exception {
    constructor(m: string) { super(m) }
    fn kind() -> string { return "AppError" }
}

class NotAllowed extends AppError {
    public user: string
    constructor(user: string) {
        super("${user} may not do that")
        self.user = user
    }
    fn kind() -> string { return "NotAllowed" }
}

fn check(user: string) {
    if (user != "admin") { throw new NotAllowed(user) }
}

fn main() {
    try {
        check("guest")
    } catch (e: AppError) {
        print(e.describe())
    }
}
```

Output:

```text
NotAllowed: guest may not do that
```

Fields like `user` let the handler get at the details without parsing the
message.

## Errors as values: Result

Exceptions are easy to ignore: nothing in `fn parse(s: string) -> int` tells the
caller it might throw. When failure is a normal, expected outcome, return it
instead. `Result<T, E>` is a data enum that holds either `Ok(value)` or
`Err(error)`:

```grav
import "std"

fn parseAge(s: string) -> Result<int, string> {
    if (s == "") { return Result.Err("empty input") }
    if (!Format.isInt(s)) { return Result.Err("not a number: " + s) }
    let n = Format.toInt(s)
    if (n < 0 || n > 150) { return Result.Err("out of range: ${n}") }
    return Result.Ok(n)
}

fn main() {
    for (input in ["42", "", "abc", "200"]) {
        match (parseAge(input)) {
            case Ok(age): print("age ${age}")
            case Err(msg): print("error: ${msg}")
        }
    }
    print(parseAge("x").unwrapOr(0))
    print(parseAge("20").map(fn(n: int) => n + 1))
}
```

Output:

```text
age 42
error: empty input
error: not a number: abc
error: out of range: 200
0
Ok(21)
```

The return type now says "this can fail, and here is what you get when it
does". The caller has to look inside the `Result` to get the value, so the error
cannot be ignored by accident.

Useful methods (see [`Result`](../std/core/result.md) for all of them):

| Method | Gives |
|--------|-------|
| `isOk()`, `isErr()` | which one it is |
| `unwrap()` | the value, or throws `StateError` on an `Err` |
| `unwrapOr(fallback)` | the value, or the fallback |
| `map(f)` | `Ok(f(value))`, passing errors through |
| `mapErr(f)` | `Err(f(error))`, passing values through |
| `andThen(f)` | chains another step that can fail |

### Choosing between them

- Use **`Result`** (or `T?`/`Option`) when the caller is expected to handle the
  failure right away: parsing, validation, lookups.
- Use **exceptions** when failure is rare and is best handled far away from where
  it happens: a request handler catching any `IOError` from deep inside.
- Use **panics** for bugs, which should not be "handled" at all, only fixed.

## Panics

A *panic* means the program has hit a bug. The usual response is to stop at
once, with a message and the call stack, before the bug does more damage:

- `panic("message")` panics on purpose, for "this cannot happen" situations.
- `assert(condition, "message")` panics when the condition is false.
- Using a field or method through `null`, and runaway recursion (a stack
  overflow), panic too.

A `catch` never catches a panic. That is deliberate: a panic is not an error
condition to handle, it is a sign that the program's own logic is wrong.

### Recovering from panics

A server should not stop serving everyone because one request hit a bug.
Inside a *recovery point*, a panic is turned into a recoverable failure instead:
it unwinds the stack like an exception that no `catch` can catch, `finally`
blocks run (so locks are released), and the recovery point receives the message.

`Panic.recover(body)` runs a function as a recovery point. It returns `""` if the
function finished, or the panic's message:

```grav
import "std"

fn main() {
    let failure = Panic.recover(fn() {
        try {
            panic("the ledger does not balance")
        } catch (e: Exception) {
            print("not reached: a catch never takes a panic")
        } finally {
            print("finally still runs")
        }
    })
    print("recovered: " + failure)

    let missing: Result<int, string> = Result.Err("no such user")
    print(Panic.recover(fn() { missing.unwrap() }))
    print("still running")
}
```

Output:

```text
finally still runs
recovered: panic: the ledger does not balance
uncaught exception: StateError: called unwrap() on Err(no such user)
still running
```

An exception that escapes a recovery point is stopped there too.

Some recovery points are built in:

- **Every `HttpServer` request.** A panicking handler answers
  `500 internal error`, the panic is logged, and the server keeps serving.
- **Every `Thread` body.** `join()` reports the panic instead of the whole
  program stopping.

Recovery can be switched off, globally with `Panic.setRecovery(false)` or the
environment variable `GRAV_PANIC_RECOVERY=off`, or per thread with
`Panic.setThreadRecovery(false)`. Then a panic stops the program even inside a
recovery point, which is handy while debugging. See [`Panic`](../std/core/panic.md).

## Summary

- `throw new SomeError("...")` raises an exception; `try`/`catch (e: Type)`
  handles it; `finally` always runs.
- Catch specific classes first; `Exception` catches everything.
- The standard library and the language throw `IndexError`, `ArithmeticError`,
  `StateError` and others; extend `Exception` for your own.
- `Result<T, E>` returns failure as a value the caller must look at.
- `panic` and `assert` are for bugs; `Panic.recover`, servers and threads can
  recover from them.

## Exercises

1. Write `fn readPositive(s: string) -> int` that throws a `ValueError` for bad
   input, and a loop that asks for numbers in an array of strings, reporting the
   bad ones and summing the good ones.
2. Rewrite exercise 1 with `Result<int, string>`. Which version do you prefer for
   this job, and why?
3. Make a hierarchy `BankError` → `InsufficientFunds`, `AccountLocked`, and a
   `withdraw` method that throws them. Catch `BankError` in one place.
4. Use `finally` to print "done" after a loop that may throw halfway.
5. Write a function that panics on a "can't happen" case, and call it inside
   `Panic.recover`. Then set `GRAV_PANIC_RECOVERY=off` and run it again.

Next: [Memory →](15-memory.md)
