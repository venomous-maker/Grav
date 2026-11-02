# 16. Concurrency

Grav runs `async` functions on a pool of worker threads. Calls start immediately
and run in parallel; `await` collects their results. The runtime is thread-safe,
exceptions travel from a task to whoever awaits it, and the standard library
provides the tools for sharing data safely.

## Tasks and futures

Calling an `async fn` queues a **task** and returns a `Future<T>` right away. `await`
waits for the task and gives you the `T`. `await` works anywhere — inside `async`
functions or ordinary ones.

```grav
import "../lib/std.grav"

async fn slowSquare(x: int) -> int {
    Async.sleepMs(100)
    return x * x
}

fn main() {
    let sw = new Stopwatch()
    let a = slowSquare(3)            // starts now
    let b = slowSquare(4)            // starts now too
    print(await a + await b)
    print("took less than 2 x 100ms: ${sw.elapsedMs() < 190}")
}
```

Output:

```text
25
took less than 2 x 100ms: true
```

`Future<T>` is a regular type: store futures in variables, fields, and collections,
and pass them to functions. `awaitAll::<T>(futures)` awaits a whole `Array` in order.

```grav
import "../lib/std.grav"

async fn cube(x: int) -> int { return x * x * x }

fn main() {
    let jobs = new Array<Future<int>>()
    for (i in 1..=5) { jobs.push(cube(i)) }
    let results = awaitAll::<int>(jobs)
    print(results.reduce(0, fn(a, b) => a + b))
}
```

Output:

```text
225
```

Arguments are copied into the task when it starts. Async functions cannot be
variadic (the extra arguments would live on the caller's stack).

## The worker pool: GRAV_MAXPROCS

Tasks run on at most **GRAV_MAXPROCS** worker threads:

| Setting | How |
|---------|-----|
| default | the number of online CPU cores |
| environment | `GRAV_MAXPROCS=4 ./app` |
| in code | `Async.setMaxProcs(4)` (applies to workers started from then on) |
| query | `Async.maxProcs()`, `Async.pending()`, `Async.cpuCount()` |

Workers start lazily, one per spawned task, up to the limit. The limit can be
oversubscribed on purpose (`GRAV_MAXPROCS=64` on an 8-core machine) for tasks that
mostly wait, or lowered to keep a program from using every core.

A task that is awaited before any worker has picked it up runs on the awaiting
thread. That keeps a small pool from deadlocking: a task can spawn and await other
tasks even with `GRAV_MAXPROCS=1`.

```grav
import "../lib/std.grav"

async fn fib(n: int) -> int {
    if (n < 15) { return n < 2 ? n : slowFib(n) }
    let a = fib(n - 1)               // tasks spawning tasks
    let b = fib(n - 2)
    return await a + await b
}
fn slowFib(n: int) -> int { return n < 2 ? n : slowFib(n - 1) + slowFib(n - 2) }

fn main() {
    Async.setMaxProcs(1)             // even one worker cannot deadlock
    print(await fib(20))
}
```

Output:

```text
6765
```

When `main` returns, the program waits for any tasks still running (including ones
nobody awaited) before exiting.

## Exceptions in tasks

An exception that escapes a task is captured and re-thrown by `await`, with the stack
trace from inside the task:

```grav
import "../lib/std.grav"

async fn parse(s: string) -> int {
    if (!Format.isInt(s)) { throw new ValueError("not a number: '${s}'") }
    return Format.toInt(s)
}

fn main() {
    let good = parse("42")
    let bad = parse("forty-two")
    print(await good)
    try {
        print(await bad)
    } catch (e: ValueError) {
        print("caught: ${e.message()}")
    }
}
```

Output:

```text
42
caught: not a number: 'forty-two'
```

If a task fails and is never awaited, the program reports it like an uncaught
exception when it exits (status 1).

## Sharing data safely

The runtime is thread-safe — each thread has its own exception state and stack
trace, and library internals avoid shared buffers — but **your** objects and globals
are not synchronized. When several tasks write the same data, use one of these
(`lib/async.grav`):

### Mutex

`withLock` runs a function while holding the lock and releases it even if the
function throws.

```grav
import "../lib/std.grav"

class Account { public balance: int  constructor() { self.balance = 0 } }

async fn deposit(m: Mutex, acct: Account, times: int) {
    for (i in 0..times) { m.withLock(fn() { acct.balance = acct.balance + 1 }) }
}

fn main() {
    let m = new Mutex()
    let acct = new Account()
    let tasks = new Array<Future<void>>()
    for (t in 0..8) { tasks.push(deposit(m, acct, 1000)) }
    for (t in tasks) { await t }
    print(acct.balance)
}
```

Output:

```text
8000
```

`lock()`/`unlock()`/`tryLock()` are available for finer control.

### AtomicInt

A counter updated with hardware atomics — no lock needed: `get`, `set`, `add`,
`increment`, `decrement`, `compareAndSet`.

```grav
import "../lib/std.grav"

async fn countEvens(hits: AtomicInt, lo: int, hi: int) {
    for (i in lo..hi) { if (i % 2 == 0) { hits.increment() } }
}

fn main() {
    let hits = new AtomicInt(0)
    let a = countEvens(hits, 0, 5000)
    let b = countEvens(hits, 5000, 10000)
    await a
    await b
    print(hits.get())
}
```

Output:

```text
5000
```

### Channel

`Channel<T>` is a bounded, blocking queue for handing values from producers to
consumers: `send` waits while it is full, `receive` waits while it is empty, and
`close()` tells receivers no more values are coming (`hasNext()` turns false once
it is drained).

```grav
import "../lib/std.grav"

async fn producer(ch: Channel<string>) {
    for (w in ["alpha", "beta", "gamma"]) { ch.send(w) }
    ch.close()
}

async fn consumer(ch: Channel<string>) -> string {
    let sb = new StringBuilder()
    while (ch.hasNext()) { sb.append(Str.upper(ch.receiveOr(""))).append(" ") }
    return sb.toString()
}

fn main() {
    let ch = new Channel<string>(2)
    let c = consumer(ch)
    let p = producer(ch)
    await p
    print(await c)
}
```

Output:

```text
ALPHA BETA GAMMA 
```

## Finding races: --race

Build with `--race` to instrument the program with ThreadSanitizer. Any unsynchronized
access to shared data is reported with the source lines involved:

```bash
grav app.grav --race --run
```

```text
WARNING: ThreadSanitizer: data race (pid=4242)
  Read of size 4 at 0x7208... by thread T1:
    #0 vf_bump /tmp/gravc-app.c:3836
```

`--sanitize` does the same for memory errors (AddressSanitizer + UBSan).

## Memory

Each task allocates a small record that lives until the program exits. A program that
starts very many tasks can release finished ones with `free(future)` (after awaiting
it, and without awaiting it again):

```grav
async fn work(i: int) -> int { return i % 7 }

fn main() {
    let total = 0
    for (i in 0..10000) {
        let f = work(i)
        total += await f
        free(f)
    }
    print(total)
}
```

Output:

```text
29994
```

## Guidelines

- Prefer passing data into a task and returning a result over sharing mutable
  objects; use a `Channel` to stream values between tasks.
- Guard shared mutable state with a `Mutex` (or use `AtomicInt` for counters).
- Treat strings and objects handed to a task as read-only in the caller until the
  task is awaited.
- Run tests with `--race` when you add concurrency.

[← Back to the index](README.md)
