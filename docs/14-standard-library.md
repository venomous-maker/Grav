# 14. Standard library

The library lives in [`lib/`](../lib/) and is written in Grav (with inline C where
it talks to the system). Import everything with `import "<path>/lib/std.grav"`, or
individual modules such as `lib/hashmap.grav`.

## Built-in functions

These need no import:

| Function | Description |
|----------|-------------|
| `print(x)` / `println(x)` | print a value and a newline |
| `str(x)` | convert a value to a string |
| `input()` | read one line from stdin (`""` at end of input) |
| `argc()`, `argv(i)` | command-line argument count and the `i`-th argument |
| `env(name)`, `cwd()` | environment variable, current directory |
| `typename(obj)`, `isInstance(obj, T)` | runtime type info |
| `assert(cond[, msg])`, `panic(msg)`, `exit(code)` | checks and termination |
| `free(x)` | release an object, pointer, or closure environment |

## Collections

| Class | Highlights |
|-------|-----------|
| `Array<T>` | growable array: `push`, `pop`, `get`/`set`, `a[i]`, `insert`, `removeAt`, `indexOf`, `contains`, `reverse`, `first`/`last`, `length`, `copy`, `sortBy`, and the higher-order `forEach`/`map`/`mapTo`/`filter`/`reduce`/`fold`/`any`/`all`/`countWhere`/`findIndex` |
| `List<T>` | convenience wrapper: `add`, `get`, `set`, `remove`, `removeAt`, `indexOf`, `contains`, `size`, `forEach`, `sortBy`, `toArray` |
| `Stack<T>` / `Queue<T>` | `push`/`pop`/`peek` and `enqueue`/`dequeue`/`peek`, `size`, `isEmpty` |
| `Set<T>` | `add`, `contains`, `remove`, `size`, `at(i)`, `union`, `intersect` |
| `Map<K, V>` | insertion-ordered, linear lookup: `put`, `get`, `getOr`, `m[k]`, `containsKey`, `remove`, `keys`, `values` |
| `HashMap<K, V>` | O(1) hash table for `int`/`long`/`string` keys: `put`, `get`, `getOr`, `m[k]`, `containsKey`, `remove`, `size`, `clear`, `keys`, `values` |
| `StringBuilder` | `append`, `appendLine`, `appendInt`, `appendFloat`, `appendBool`, `appendChar`, `repeat` (chainable), `length`, `clear`, `toString` |

Element access is bounds-checked: an out-of-range index stops the program with a
clear message. Every collection frees its own buffers when freed.

```grav
import "../lib/std.grav"

fn main() {
    let words = new Array<string>()
    for (w in ["pear", "fig", "apple", "fig", "pear", "fig"]) { words.push(w) }

    let counts = new HashMap<string, int>()
    for (w in words) { counts[w] = counts.getOr(w, 0) + 1 }

    let keys = counts.keys()
    keys.sortBy(fn(a, b) => Str.compare(a, b))
    for (k in keys) { print("${k}: ${counts[k]}") }

    let unique = new Set<string>()
    for (w in words) { unique.add(w) }
    print("unique: ${unique.size()}")

    let todo = new Queue<string>()
    todo.enqueue("first")
    todo.enqueue("second")
    print("next: ${todo.dequeue()}")
}
```

Output:

```text
apple: 1
fig: 3
pear: 2
unique: 3
next: first
```

## Strings

`Str` holds static string functions (`lib/string.grav`):

| Function | Description |
|----------|-------------|
| `length`, `isEmpty`, `charAt(s, i)` | size, emptiness, byte at index |
| `equals`, `compare` | equality; `<0`/`0`/`>0` ordering like `strcmp` |
| `contains`, `indexOf`, `indexFrom`, `count`, `startsWith`, `endsWith` | searching |
| `substring(s, start, len)`, `remove(s, start, len)` | slicing |
| `upper`, `lower`, `reverse`, `repeat`, `trim`, `trimStart`, `trimEnd` | transforming |
| `replace(s, old, new)`, `padLeft`/`padRight(s, width, ch)` | editing |
| `split(s, delim)` → `Array<string>`, `join(parts, delim)` | splitting and joining |

```grav
import "../lib/std.grav"

fn main() {
    let s = "  Grav compiles to C  "
    let t = Str.trim(s)
    print(Str.upper(t))
    print(Str.replace(t, "C", "native code"))
    let parts = Str.split("a,b,c", ",")
    print(Str.join(parts, " | "))
    print("${Str.indexOf(t, "to")} ${Str.startsWith(t, "Grav")} ${Str.padLeft("7", 3, "0")}")
}
```

Output:

```text
GRAV COMPILES TO C
Grav compiles to native code
a | b | c
14 true 007
```

## Conversions

`Format` parses text into values: `toInt`, `toLong`, `toUInt`, `toByte`, `toFloat`,
`toDouble`, `toBool` (accepts `true`/`1`/`yes`/`y`/`on`), `toIntOr(s, fallback)`,
`isInt`, `isNumber`, and `hex(n)`. The other direction is the built-in `str(x)`.

```grav
import "../lib/std.grav"

fn main() {
    print(Format.toInt("41") + 1)
    print(Format.toIntOr("oops", -1))
    print(Format.isNumber("3.5e2"))
    print(Format.hex(255))
}
```

Output:

```text
42
-1
true
0xff
```

## Math and random numbers

`Math`: `sqrt`, `cbrt`, `pow`, `abs`, `sin`, `cos`, `tan`, `atan2`, `floor`, `ceil`,
`round`, `log`, `log10`, `exp`, `min`, `max`, `pi()`, `e()`.
`Random`: `seed(n)`, `seedTime()`, `nextInt(min, max)` (inclusive), `nextFloat()`
(in `[0, 1)`), `nextBool()`.

```grav
import "../lib/std.grav"

fn main() {
    print(Math.sqrt(16.0))
    print(Math.round(Math.pi() * 100.0) / 100.0)
    Random.seed(1)
    let roll = Random.nextInt(1, 6)
    print(roll >= 1 && roll <= 6)
}
```

Output:

```text
4.000000
3.140000
true
```

## Files and paths

`File` (`lib/fs.grav`) is an object for one path: `exists`, `size`, `read`, `write`,
`append`, `writeLine`, `readLines`, `readBinary`, `writeBinary`, `appendBinary`,
`copyTo`, `delete`. `Path` has `basename`, `dirname`, `extension`, and `join`.

```grav
import "../lib/std.grav"

fn main() {
    let f = new File("/tmp/grav_docs_demo.txt")
    f.write("alpha\n")
    f.writeLine("beta")
    let lines = f.readLines()
    print("${lines.length()} lines, last is ${lines.get(1)}")
    print(Path.extension("report.final.pdf"))
    f.delete()
    print(f.exists())
}
```

Output:

```text
2 lines, last is beta
.pdf
false
```

## Time and dates

`Time`: `now()` (unix seconds), `nowMs()`, `monotonicMs()`, `monotonicNs()`,
`sleepMs(ms)`. `Stopwatch`: `reset`, `elapsedNs`, `elapsedMs`, `elapsedSeconds`.
`DateTime`: `now()`, `fromUnix(s)`, `year`/`month`/`day`/`hour`/`minute`/`second`,
`format(strftimePattern)`, `addDays`, `addHours`, `unix`.

```grav
import "../lib/std.grav"

fn main() {
    let d = DateTime.fromUnix(1700049600)        // 2023-11-15 12:00 UTC
    print(d.addDays(1).format("%Y-%m-%d"))
    let sw = new Stopwatch()
    Time.sleepMs(5)
    print(sw.elapsedMs() >= 5)
}
```

Output:

```text
2023-11-16
true
```

## Processes, OS, I/O, terminal

| Class | Functions |
|-------|-----------|
| `Process` | `exec(cmd)` (exit status), `execOutput(cmd)` (captured stdout), `pid()`, `sleepMs(ms)` |
| `OS` | `platform`, `arch`, `hostname`, `kernel`, `user`, `home`, `cwd`, `chdir`, `pid`, `ppid`, `cpuCount`, `pageSize`, `getEnv`, `getEnvOr`, `hasEnv`, `setEnv`, `unsetEnv`, `isTerminal` |
| `IO` | `print`, `write` (no newline), `eprint` (stderr), `flush`, `input`, `prompt`, `readInt`, `readLong`, `readFloat`, `readDouble`, `readBool` |
| `Term` | `bold`, `dim`, `italic`, `underline`, `red`…`gray`, `color(s, n)`, `clear`, `clearLine`, `moveTo`, `hideCursor`, `showCursor` |

Free functions `printRaw` (print without a newline), `eprint`, `readLine`, `prompt`,
`readInt`, … mirror `IO`.

```grav
import "../lib/std.grav"

fn main() {
    print(Str.trim(Process.execOutput("echo from the shell")))
    OS.setEnv("APP_MODE", "test")
    print(OS.getEnvOr("APP_MODE", "dev"))
    printRaw("no newline, ")
    IO.print("then one")
    print(OS.cpuCount() > 0)
}
```

Output:

```text
from the shell
test
no newline, then one
true
```

## Concurrency

`lib/async.grav`: `Async` (`maxProcs`, `setMaxProcs`, `pending`, `cpuCount`,
`threadId`, `sleepMs`), `Mutex` (`lock`, `unlock`, `tryLock`, `withLock`),
`AtomicInt` (`get`, `set`, `add`, `increment`, `decrement`, `compareAndSet`),
`Channel<T>` (`send`, `receive`, `receiveOr`, `hasNext`, `close`, `size`), and
`awaitAll::<T>(futures)`. See [Concurrency](16-concurrency.md).

## Exceptions

`lib/exception.grav` defines `Exception` (`message()`, `describe()`) and the
subclasses `ValueError`, `RuntimeError`, `IndexError`, and `NotFound` — see
[Exceptions](11-exceptions.md).

Next: [How Grav compiles →](15-how-it-compiles.md)
