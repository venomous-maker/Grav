# Run-time behaviour

What a compiled Grav program does on its own: the checks it makes, what it
prints when something goes wrong, and the settings it reads.

## Checks made while the program runs

| Problem | With `import "std"` | Without the standard library |
|---------|---------------------|------------------------------|
| an index out of range: fixed array, string, `binary`, slice bounds | throws `IndexError` | panics |
| integer `/` or `%` by zero | throws `ArithmeticError` | panics |
| `n as SomeEnum` with a number that names no member | throws `ValueError` | panics |
| `x as Class` or `x as Interface` on an object that is not one | throws `TypeError` | panics |
| `x as T` on an `any` holding something else | panics | panics |
| `x!` on `null` | throws `StateError` | panics |
| using a field or method through `null` | panics | panics |
| a stack overflow (runaway recursion) | panics | panics |
| out of memory | panics | panics |

The standard library throws for its own misuse too: `IndexError` for bad
positions, `StateError` for taking from an empty collection, `NotFound`,
`ValueError` and `IOError` where they apply. Each module's page says what it throws.

## Uncaught exceptions

An exception that no `catch` handles ends the program with status 1:

```text
grav: uncaught exception: IndexError: array index 5 out of range (length 3)
  stack trace (most recent call last):
    at main (app.grav:5)
```

The first line is the exception's `describe()`. The stack trace lists the calls
active when it was thrown, outermost first, with file and line. Builds with
`--release` or `--no-trace` omit the trace.

## Panics

A panic ends the program with status 1:

```text
panic: null reference: a field or method was used through a null value
  stack trace (most recent call last):
    at main (app.grav:5)
```

```text
panic: stack overflow (is a recursion missing its base case?)
  stack trace (most recent call last):
    at main (app.grav:3)
    at f (app.grav:1)
```

`catch` never catches a panic. Inside a *recovery point*, a panic instead unwinds
to it, running `finally` blocks on the way:

- `Panic.recover(fn() { ... })` returns `""`, or the panic message;
- each `HttpServer` request answers 500 and logs the panic;
- each `Thread` body reports the panic through `join()`.

Recovery is controlled by `Panic.setRecovery`, `Panic.setThreadRecovery` and the
`GRAV_PANIC_RECOVERY` environment variable. See [`Panic`](../std/core/panic.md).

## Failed assertions

`assert` and `assertEq` outside a test print the failure with its location and
end the program with status 1:

```text
assertion failed: math broke (app.grav:2)
```

Inside a `test` block they fail that test, and the runner continues.

## Memory

Objects, strings, closures and collections are reference counted and freed when
the last reference is dropped, running destructors at that moment. A cycle
collector frees groups of objects that only refer to each other; it runs
automatically while a single thread uses objects, and on `GC.collect()`. See
[Memory](../learn/15-memory.md).

An exception or a recovered panic releases the local variables of every
function it unwinds through, in the order those functions would have released
them. A constructor that throws frees its object without running its
destructor.

## Threads and tasks

- `async` calls run on a pool of `GRAV_MAXPROCS` worker threads (default: the
  number of CPU cores), started as needed.
- When `main` returns, the program waits for running tasks before exiting. A
  task that failed and was never awaited is reported, and the exit status is 1.
- Reference counting and the runtime are thread-safe; user data shared between
  threads needs synchronisation ([Concurrency](../learn/18-concurrency.md)).

## Environment variables

| Variable | Effect |
|----------|--------|
| `GRAV_MAXPROCS` | the size of the `async` worker pool |
| `GRAV_PANIC_RECOVERY` | `off` turns panic recovery off at start-up |
| `GRAV_TEST_FILTER` | in `grav test` runs, only tests whose name contains it |

## Output

`print` writes to standard output, which is buffered when it is not a terminal.
Standard output is flushed before anything is written to standard error by the
runtime or `IO.eprint`, and when the program exits.
