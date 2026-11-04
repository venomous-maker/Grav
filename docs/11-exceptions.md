# 11. Exceptions

## throw, try, catch

Any class instance can be thrown. A `try` may have several `catch` clauses; the first
whose class matches (exactly or as a base class) handles the exception. The standard
library's `lib/exception.grav` provides `Exception` (with `message()`, `kind()`, and
`describe()`) and `ValueError`, `TypeError`, `IndexError`, `NotFound`, `IOError`,
`StateError`, `TimeoutError`, `ArithmeticError`, and `RuntimeError`.

```grav
import "../lib/std.grav"

fn divide(a: int, b: int) -> int {
    if (b == 0) { throw new ValueError("division by zero") }
    return a / b
}

fn main() {
    try {
        print(divide(10, 2))
        print(divide(1, 0))
        print("not reached")
    } catch (e: IndexError) {
        print("index problem")
    } catch (e: ValueError) {
        print("caught: ${e.message()}")
    }
}
```

Output:

```text
5
caught: division by zero
```

A clause that can never run because an earlier one catches a base class is a compile
error.

## finally

`finally` always runs: after normal completion, after a handled exception, while an
unhandled exception propagates, when a handler throws, and when `return`, `break`, or
`continue` leaves the `try`.

```grav
import "../lib/std.grav"

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

## Custom exception hierarchies

Extend `Exception` and override `describe()`; catch by base class to handle a whole
family.

```grav
import "../lib/std.grav"

class AppError extends Exception {
    constructor(m: string) { super(m) }
    fn describe() -> string { return "AppError: ${self.msg}" }
}
class NotAllowed extends AppError {
    public user: string
    constructor(user: string) { super("not allowed"); self.user = user }
    fn describe() -> string { return "NotAllowed: ${self.user}" }
}

fn check(user: string) {
    if (user != "admin") { throw new NotAllowed(user) }
}

fn main() {
    try {
        check("guest")
    } catch (e: AppError) {
        print(e.describe())          // the dynamic type's describe()
    }
}
```

Output:

```text
NotAllowed: guest
```

## Re-throwing and nesting

```grav
import "../lib/std.grav"

fn main() {
    try {
        try {
            throw new RuntimeError("disk full")
        } catch (e: RuntimeError) {
            print("logging: ${e.message()}")
            throw e                       // pass it on
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

## Uncaught exceptions

An exception nobody catches prints its `describe()` (or `message()`) and the stack
trace of the original `throw`, then exits with status 1:

```text
grav: uncaught exception: ValueError: division by zero
  stack trace (most recent call last):
    at main (app.grav:9)
    at divide (app.grav:4)
```

Release builds (`--release` / `--no-trace`) omit the trace for speed.

## Local variables survive into catch

Variables assigned inside `try` keep their latest values in `catch`, at every
optimization level:

```grav
import "../lib/std.grav"

fn main() {
    let done = 0
    try {
        for (s in ["4", "8", "x", "16"]) {
            if (!Format.isInt(s)) { throw new ValueError("bad input '${s}'") }
            done = done + 1
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

## Errors from the standard library

The standard library reports misuse with exceptions, so a `catch` can handle it.
So do the checks the language itself makes when the standard library is loaded
(without it, they are panics):

| Exception | When |
|-----------|------|
| `IndexError` | an index out of range: a fixed-length array (`T[N]`), `Array`, `List`, `Deque`, `LinkedList`, `Bytes`, `Str.charAt`, `insert` past the end, a negative `BitSet` bit |
| `ArithmeticError` | integer division or `%` by zero |
| `StateError` | taking from something empty (`pop`, `peek`, `dequeue`, `removeFirst`, `first`, `Random.choice`, `Optional.get`), receiving on a closed channel, `unwrap()` on an `Err` or `None`, `unwrapErr()` on an `Ok` |
| `NotFound` | `Map.get` / `SortedMap.get` with a missing key |
| `ValueError` | bad input, such as a sort comparison that does not define an order |

```grav
import "std"

fn main() {
    let xs = new Array<int>()
    try {
        print(xs.get(3))
    } catch (e: IndexError) {
        print("caught " + e.describe())
    }
    let missing: Result<int, string> = Result.Err("no such user")
    try {
        missing.unwrap()
    } catch (e: StateError) {
        print(e.message())
    }
}
```

Output:

```text
caught IndexError: Array index 3 out of range (length 0)
called unwrap() on Err(no such user)
```

## Panics and recovery

A *panic* is for bugs that should stop the program: `panic("...")` itself, a
failed internal check, running out of memory, using a field or method through
`null`, or a stack overflow from runaway recursion. Normally it prints its message and
stops the program; no `catch` takes it.

Inside a *recovery point* a panic is recovered instead. It unwinds like an
exception that no `catch` can catch: `finally` blocks still run (so locks taken
with `withLock` are released), and the recovery point gets the message.
`Panic.recover(body)` returns `""` when the body finished, or the message.

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
    print(Panic.recover(fn() { missing.unwrap() }))       // an exception nobody caught
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

An exception that escapes a recovery point is stopped there too, and reported as
`uncaught exception: ...`.

**Built-in recovery points.** Every `HttpServer` request is one. A panicking
handler answers `500 internal error: panic: ...`, the panic is logged to stderr,
and the server keeps serving; set `server.recoverPanics = false` (or
`.withPanicRecovery(false)`) to opt that server out. Every `Thread` body is one
too: `join()` reports the panic instead of the program stopping.

**Switching recovery at run time.** Recovery is on by default. The effective
setting is the current thread's own, if it has one, otherwise the global one:

| Call | Effect |
|------|--------|
| `Panic.setRecovery(false)` / `(true)` | every thread (unless it has its own setting) |
| `Panic.setThreadRecovery(false)` / `(true)` | the calling thread only |
| `Panic.followGlobalRecovery()` | the calling thread follows the global setting again |
| `new Thread(body).withPanicRecovery(false)` | a thread's own setting, before it starts |
| `Panic.recoveryEnabled()` | whether a panic here would be recovered now |
| `GRAV_PANIC_RECOVERY=off` (environment) | the global setting starts off |

With recovery off, a panic stops the program even inside a recovery point. That
is useful while debugging, or to crash on bugs in production and have a
supervisor restart the program.

Next: [Memory →](12-memory.md)
