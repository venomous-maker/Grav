# 11. Exceptions

## throw, try, catch

Any class instance can be thrown. A `try` may have several `catch` clauses; the first
whose class matches (exactly or as a base class) handles the exception. The standard
library's `lib/exception.grav` provides `Exception` (with `message()`, `kind()`, and
`describe()`) and `ValueError`, `TypeError`, `IndexError`, `NotFound`, `IOError`,
`StateError`, `TimeoutError`, and `RuntimeError`.

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

Next: [Memory →](12-memory.md)
