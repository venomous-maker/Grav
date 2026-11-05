# 18. Concurrency

A program that downloads ten files one after another spends most of its time
waiting. A program that does them all at once finishes in roughly the time of
the slowest. Modern machines also have many CPU cores, and a program that only uses
one leaves the rest idle. *Concurrency* means doing several things at the same
time. It is powerful and it is also where the hardest bugs live, so this chapter
covers both the tools and the rules for using them safely.

In this chapter you will learn:

- `async` functions, futures and `await`,
- the worker pool and `GRAV_MAXPROCS`,
- what happens to exceptions in background work,
- sharing data safely: `Mutex`, `AtomicInt`, `Channel`,
- dedicated threads and the other synchronisation tools,
- finding data races with `--race`.

## async and await

Mark a function `async` and every call to it runs in the background. The call
returns at once with a `Future<T>`, a ticket for a result that is not ready yet.
`await` waits for the result:

```grav
import "std"

async fn slowSquare(x: int) -> int {
    Async.sleepMs(100)               // pretend to do slow work
    return x * x
}

fn main() {
    let sw = new Stopwatch()
    let a = slowSquare(3)            // starts now
    let b = slowSquare(4)            // starts now too
    print(await a + await b)         // wait for both
    print("both in parallel: ${sw.elapsedMs() < 190}")
}
```

Output:

```text
25
both in parallel: true
```

Each call took 100 ms, but both ran at the same time, so the total is about
100 ms, not 200.

- `await` works in any function, async or not.
- A `Future<T>` is an ordinary value: keep it in a variable, a field or a
  collection, and await it later. Awaiting it again gives the same result.
- Arguments are copied into the task when it starts.

To run many tasks and collect their results, keep the futures in an array:

```grav
import "std"

async fn cube(x: int) -> int { return x * x * x }

fn main() {
    let jobs = new Array<Future<int>>()
    for (i in 1..=5) { jobs.push(cube(i)) }
    let results = awaitAll::<int>(jobs)
    print(results)
}
```

Output:

```text
[1, 8, 27, 64, 125]
```

When `main` returns, the program waits for any tasks still running before it
exits.

### The worker pool

Tasks run on a pool of worker threads. The pool's size is `GRAV_MAXPROCS`:

| | |
|-|-|
| default | the number of CPU cores |
| environment | `GRAV_MAXPROCS=4 ./app` |
| in code | `Async.setMaxProcs(4)` |
| asking | `Async.maxProcs()`, `Async.pending()`, `Async.cpuCount()` |

For work that mostly waits (network, disk), a pool larger than the number of
cores is fine. For work that computes, more workers than cores only adds
overhead.

A task that is awaited before any worker has started it runs right there, on the
thread that awaits it. So tasks can start and await other tasks without ever
deadlocking, even with a single worker:

```grav
import "std"

async fn fib(n: int) -> int {
    if (n < 15) { return plainFib(n) }
    let a = fib(n - 1)
    let b = fib(n - 2)
    return await a + await b
}

fn plainFib(n: int) -> int { return n < 2 ? n : plainFib(n - 1) + plainFib(n - 2) }

fn main() {
    Async.setMaxProcs(1)
    print(await fib(20))
}
```

Output:

```text
6765
```

### Exceptions in tasks

An exception thrown inside a task is kept, and thrown again by `await`, so you
handle it where you collect the result:

```grav
import "std"

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

A task that fails and is never awaited is reported when the program exits, like
an uncaught exception.

## Sharing data safely

When two tasks change the same data at the same time, updates can be lost.
`count += 1` reads the count, adds one and writes it back; if two threads read
the same old value, one increment disappears. This is a *data race*, and it is
nasty because it happens only sometimes.

The safest design shares nothing: pass data into a task, get a result out.
When tasks must share changing state, protect it with one of the tools below. (Grav's
own runtime, reference counting included, is thread-safe; it is *your* shared
objects that need protecting.)

### Mutex: one at a time

A `Mutex` (mutual exclusion lock) lets only one thread at a time run the code it
guards. `withLock` takes the lock, runs a function and releases the lock, even if
the function throws:

```grav
import "std"

class Account {
    public balance: int
    constructor() { self.balance = 0 }
}

async fn deposit(m: Mutex, acct: Account, times: int) {
    for (i in 0..times) {
        m.withLock(fn() { acct.balance += 1 })
    }
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

Without the mutex, the result would be some random number below 8000.

### AtomicInt: a counter without a lock

For a single counter, `AtomicInt` uses the processor's atomic instructions,
which is simpler and faster than a lock:

```grav
import "std"

async fn countEvens(hits: AtomicInt, lo: int, hi: int) {
    for (i in lo..hi) {
        if (i % 2 == 0) { hits.increment() }
    }
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

### Channel: passing values between tasks

A `Channel<T>` is a queue that one task puts values into and another takes them
out of. `send` waits while the channel is full and `receive` waits while it is
empty, so the two sides keep pace with each other. `close()` says no more values
are coming:

```grav
import "std"

async fn producer(ch: Channel<string>) {
    for (w in ["alpha", "beta", "gamma"]) { ch.send(w) }
    ch.close()
}

async fn consumer(ch: Channel<string>) -> string {
    let sb = new StringBuilder()
    while (ch.hasNext()) {
        sb.append(Str.upper(ch.receiveOr(""))).append(" ")
    }
    return sb.toString()
}

fn main() {
    let ch = new Channel<string>(2)      // holds at most 2 values
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

Channels turn "shared data" into "messages", which is usually much easier to get
right: each value belongs to one task at a time.

## Threads

`async` tasks share a pool and suit short pieces of work. A `Thread` is a
dedicated operating-system thread, for long-running or blocking work such as a
server's accept loop or a background worker:

```grav
import "std"

fn main() {
    let total = new AtomicInt(0)
    let wg = new WaitGroup()
    for (i in 0..4) {
        wg.add(1)
        Thread.spawn(fn() {
            for (k in 0..1000) { total.increment() }
            wg.done()
        }).detach()
    }
    wg.wait()
    print("total: ${total.get()}")

    let failing = Thread.spawn("worker", fn() { throw new ValueError("disk full") })
    print("worker: " + failing.joinResult())
}
```

Output:

```text
total: 4000
worker: ValueError: disk full
```

- `Thread.spawn(body)` starts a thread; `join()` waits for it to finish, and throws
  a `StateError` if the body failed; `joinResult()` returns the error message
  instead (or `""`); `detach()` lets it run on its own.
- An exception, or a panic, inside a thread stops that thread only.

The other tools in [`std/concurrency`](../std/concurrency/thread.md):

| Type | For |
|------|-----|
| `WaitGroup` | waiting until a group of threads has finished: `add(n)`, `done()`, `wait()` |
| `CountDownLatch` | a gate that opens after `n` calls to `countDown()` |
| `Semaphore` | limiting how many threads do something at once: `withPermit(body)` |
| `RWLock` | many readers or one writer: `withRead(body)`, `withWrite(body)` |
| `Once` | running an initialiser exactly once: `once.run(body)` |

Closures copy the variables they capture ([chapter 6](06-lambdas-and-closures.md#captured-variables-are-copies)),
so threads cannot share state by assigning to captured locals. They share it
through objects, as above, which makes the shared parts easy to spot.

## Finding data races

Build with `--race` to have ThreadSanitizer watch every memory access. When two
threads touch the same data without synchronisation, it reports both places:

```bash
grav app.grav --race --run
```

```text
WARNING: ThreadSanitizer: data race (pid=4242)
  Read of size 4 at 0x7208... by thread T1:
    #0 vf_bump app.grav:12
```

Run your tests with `--race` whenever you add concurrency.

## Guidelines

- Prefer giving each task its own data and returning a result over sharing.
- Use a `Channel` to stream values from one task to another.
- Protect shared state with a `Mutex`, or an `AtomicInt` for counters. Keep the
  locked code short.
- Treat objects you hand to a task as off-limits until you have awaited it.
- Test with `--race`.

## Summary

- Calling an `async fn` starts a task and returns a `Future<T>`; `await` gets the result.
- Tasks run on a pool of `GRAV_MAXPROCS` workers; awaiting never deadlocks the pool.
- Exceptions in a task are rethrown by `await`.
- `Mutex`, `AtomicInt` and `Channel` make sharing safe; `Thread` gives a dedicated
  thread, with `WaitGroup`, `Semaphore` and friends.
- `--race` finds data races.

## Exercises

1. Write an `async fn` that sums the numbers in a range, split 1..10,000,000 into
   four ranges, and add the four results.
2. Remove the `Mutex` from the bank example and run it a few times. Then run it
   with `--race`.
3. Build a pipeline: one task sends the numbers 1 to 20 into a channel, a second
   squares them into another channel, and `main` prints the results.
4. Use a `Semaphore(2)` so that at most two of five threads print "working" at
   the same time.

Next: [Testing →](19-testing.md)
