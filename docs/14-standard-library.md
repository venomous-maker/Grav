# 14. Standard library

The library lives in [`lib/`](../lib/) and is written in Grav (with inline C where
it talks to the system). It is grouped in categories (`std.core`, `std.collections`,
`std.text`, `std.data`, `std.io`, `std.sys`, `std.concurrency`, `std.net`); see
[chapter 13](13-modules.md#the-standard-library). Import everything with
`import "std"`, a category with `import "std/collections"`, one module with
`import "std/collections/hashmap"`, or chosen names with
`import { HashMap } from "std/collections"`. The examples below use
`import "../lib/std.grav"`, which is the same thing by path.

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

## Conventions

Every collection follows the same shape, so switching between them is easy:

| Member | Meaning |
|--------|---------|
| `size()` (`length()` too) | number of elements |
| `isEmpty()`, `clear()` | emptiness, remove everything |
| `contains(x)` | membership |
| `forEach(f)`, `toArray()` | visit every element, copy into an `Array` |
| `get(i)` / `c[i]` | indexed access where it makes sense (bounds-checked) |
| `getOr(k, fallback)`, `peekOr(fallback)` | lookups that never fail |
| `remove(x)` → `bool` | whether something was removed |
| `for (x in c)` | works on every collection with indexed access |
| `c[lo..hi]` | `slice(lo, hi)`: a copy of that part (on `Array` and `Bytes`; bounds are clamped) |
| `toString()` | the contents as text, used by `print` and `${}`: `[1, 2]` for sequences, `{1, 2}` for sets, `{"a": 1}` for maps, `("a", 1)` for `Pair` (strings in quotes) |

Misuse (popping an empty stack, a bad index, `Map.get` on a missing key) stops the
program with a clear message. Methods that return a new collection (`keys()`,
`toArray()`, `filter`, …) return copies, so changing them never affects the source.
`free(c)` releases a collection's memory (not the objects stored in it).

## Collections

| Class | Use it for | Highlights |
|-------|-----------|-----------|
| `Array<T>` | the default ordered list | `add`/`push`, `insert`, `removeAt`, `remove`, `removeWhere`, `swap`, `slice`, `addAll`, `indexOf`/`lastIndexOf`, `reverse`, `sortBy`, `maxBy`/`minBy`, `findFirst`, and `forEach`/`map`/`mapTo`/`filter`/`reduce`/`fold`/`any`/`all`/`countWhere`/`findIndex` |
| `List<T>` | a list with collection-style names | `add`, `insert`, `get`/`set`, `remove`, `removeAt`, `filter`, `sortBy`, … |
| `Deque<T>` | adding/removing at both ends | `pushFront`/`pushBack`, `popFront`/`popBack`, `peekFront`/`peekBack`, `get(i)` — all O(1) |
| `Stack<T>` / `Queue<T>` | LIFO / FIFO | `push`/`pop`/`peek` and `enqueue`/`dequeue`/`peek`, `peekOr` — O(1) |
| `HashMap<K, V>` | fast key → value lookup | `put`/`get`/`m[k]`, `getOr`, `getOrPut`, `containsKey`, `remove`, `keys`, `values`, `forEach(fn(k, v))` |
| `HashSet<T>` | fast membership | `add` (→ added?), `contains`, `remove`, `union`, `intersect`, `difference`, `isSubsetOf` |
| `Map<K, V>` | small maps in insertion order | same API as `HashMap`, linear lookup, any key with `==` |
| `Set<T>` | small sets in insertion order | same API as `HashSet`, any element with `==` |
| `SortedMap<K, V>` | keys kept in order | constructed with a comparator; `firstKey`, `lastKey`, `keysBetween(lo, hi)`, O(log n) lookup |
| `PriorityQueue<T>` | always take the best next | constructed with a comparator; `push`/`pop`/`peek` in O(log n) |
| `LinkedList<T>` | splicing at the ends | `addFirst`/`addLast`, `removeFirst`/`removeLast`, `reverse` |
| `BitSet` | sets of small non-negative ints | `set`/`unset`/`flip`/`get`, `count`, `unionWith`, `intersectWith` |
| `Pair<A, B>`, `Triple<A, B, C>` | returning several values | `first`, `second`, (`third`), `swap()`, `==` |
| `Optional<T>` | a value that may be absent | `isPresent`, `get`, `getOr`, `map`, `filter`, `ifPresent` |
| `StringBuilder` | building text efficiently | `append` (strings or any value), `appendInt`, `appendChar`, `appendLine`, `insert`, `truncate`, `toString` |

`HashMap`/`HashSet` keys can be integers, `bool`, `float`, strings, enums, or objects
(objects hash by identity, matching their `==`). Generic type arguments are usually
inferred: `new HashMap<auto, auto>()` takes them from the variable or field it is
assigned to.

```grav
import "../lib/std.grav"

fn main() {
    let words = new Array<string>()
    for (w in ["pear", "fig", "apple", "fig", "pear", "fig"]) { words.push(w) }

    let counts = new HashMap<string, int>()
    for (w in words) { counts[w] = counts.getOrPut(w, 0) + 1 }

    let byName = new SortedMap<string, int>(fn(a, b) => Str.compare(a, b))
    counts.forEach(fn(k, v) { byName[k] = v })
    byName.forEach(fn(k, v) => print("${k}: ${v}"))

    let unique = new HashSet<string>()
    for (w in words) { unique.add(w) }
    print("unique: ${unique.size()}")

    let urgent = new PriorityQueue<int>(fn(a, b) => b - a)       // largest first
    for (n in [4, 9, 2]) { urgent.push(n) }
    print("most urgent: ${urgent.pop()}")

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
most urgent: 9
next: first
```

### Collection literals

A `[...]` literal where a collection is expected (a declared variable, a
parameter, a field, a return value) builds that collection: any class with a
constructor that takes no arguments and an `add`, `push`, `enqueue` or `pushBack`
method. Without such a type it stays a fixed-length array. `{key: value, ...}`
builds a `HashMap` (through `mapOf`).

```grav
import "../lib/std.grav"

fn total(xs: Array<int>) -> int { return xs.reduce(0, fn(a, b) => a + b) }

fn main() {
    let primes: Array<int> = [2, 3, 5, 7]
    let seen: HashSet<string> = ["a", "b", "a"]
    let ages = {"ann": 31, "bob": 27}
    print(total(primes))
    print(seen.size())
    print(ages["bob"])
    print(total([10, 20]))
}
```

Output:

```text
17
2
27
30
```

## Strings

`Str` holds static string functions (`lib/string.grav`); every transforming
function returns a new string.

| Group | Functions |
|-------|-----------|
| inspect | `length`, `isEmpty`, `isBlank`, `charAt(s, i)` (bounds-checked), `isDigits`, `isAlpha`, `isAlphanumeric`, `isUpper`, `isLower` |
| compare | `equals`, `equalsIgnoreCase`, `compare`, `compareIgnoreCase` (`<0`/`0`/`>0`) |
| search | `contains`, `indexOf`, `indexFrom`, `lastIndexOf`, `count`, `startsWith`, `endsWith` |
| transform | `upper`, `lower`, `capitalize`, `reverse`, `repeat`, `trim`, `trimStart`, `trimEnd`, `replace(s, old, new)` |
| cut | `substring(s, start, len)`, `slice(s, start, end)`, `remove(s, start, len)`, `removePrefix`, `removeSuffix` |
| layout | `padLeft`/`padRight(s, width, ch)`, `center(s, width, ch)` |
| split / join | `split(s, delim)`, `lines(s)` (`\n` and `\r\n`), `words(s)`, `join(parts, delim)` (one allocation) |
| characters | `fromChar(code)` (`71` → `"G"`) |

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

`Format` converts between text and values:

| Group | Functions |
|-------|-----------|
| parse | `toInt`, `toLong`, `toUInt`, `toByte` (`ValueError` when the number does not fit), `toFloat`, `toDouble`, `toBool` (`true`/`1`/`yes`/`y`/`on`) |
| parse safely | `isInt`, `isNumber`, `toIntOr`, `toLongOr`, `toFloatOr`, `parseInt(s, base, fallback)` |
| format numbers | `fixed(x, digits)`, `grouped(n)` (`1,234,567`), `percent(ratio, digits)`, `bytes(n)` (`1.5 KB`), `hex`, `binary`, `octal` |

The general value → string conversion is the built-in `str(x)`.

```grav
import "../lib/std.grav"

fn main() {
    print(Format.toInt("41") + 1)
    print(Format.toIntOr("oops", -1))
    print(Format.isNumber("3.5e2"))
    print(Format.hex(255))
    print(Format.fixed(3.14159, 2))
    print(Format.grouped(1234567))
    print(Format.bytes(1536))
}
```

Output:

```text
42
-1
true
0xff
3.14
1,234,567
1.5 KB
```

## Math and random numbers

`Math`: constants `PI`, `E`, `TAU`; `sqrt`, `cbrt`, `pow`, `hypot`, `exp`, `log`,
`log2`, `log10`; `sin`/`cos`/`tan`, `asin`/`acos`/`atan`/`atan2`, `sinh`/`cosh`/`tanh`,
`toRadians`/`toDegrees`; `floor`, `ceil`, `round`, `trunc`, `roundTo(x, digits)`;
`abs`, `min`, `max`, `clamp`, `sign` (with integer overloads, so `Math.max(3, 7)` is an
`int`); `isNaN`, `isInfinite`, `isFinite`; integer helpers `gcd`, `lcm`, `isPrime`,
`factorial`.

`Random` uses the xoshiro256** generator — fast, high quality, and identical on
every platform. Each thread has its own; the default seed is fixed (so an unseeded
program is reproducible) and `Random.seedTime()` varies it. `nextInt(min, max)` and
`nextLong` are **inclusive** and unbiased; `nextFloat()` is in `[0, 1)`;
`nextFloat(min, max)`, `nextBool()`, `chance(p)`, `nextGaussian(mean, stddev)`,
`shuffle(xs)`, and `choice(xs)` round it out. `new Rng(seed)` gives an independent
generator with the same methods.

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
4
3.14
true
```

## Sorting and searching

`Sort` (in `std.core`, next to `Math`) sorts arrays with TimSort. It is stable
(equal elements keep their order), O(n log n) in the worst case, and close to O(n)
when the input is already sorted, reversed, or made of a few sorted stretches.
`xs.sortBy(cmp)` on an `Array` is the same sort.

| Call | Does |
|------|------|
| `Sort.sort(xs)`, `Sort.sortDescending(xs)` | natural order, for arrays of `int`, `long`, `float` and `string` |
| `Sort.sortBy(xs, cmp)` | any element type: `cmp(a, b) < 0` puts `a` first |
| `Sort.isSorted(xs)`, `Sort.isSortedBy(xs, cmp)` | whether the array is in order |
| `Sort.binarySearch(xs, key)`, `Sort.binarySearchBy(xs, key, cmp)` | in a sorted array: the index of `key`, or `-(insertion point) - 1` |

Strings compare byte by byte, like `Str.compare` (`"B"` sorts before `"a"`). If a
comparison throws, the sort stops and the array keeps all of its elements, in a
partly sorted order.

```grav
import "std"

class Player {
    public name: string
    public score: int
    constructor(name: string, score: int) { self.name = name; self.score = score }
}

fn main() {
    let scores = new Array<int>()
    for (v in [42, 7, 19, 7, 88]) { scores.push(v) }
    Sort.sort(scores)
    print("${scores.get(0)} .. ${scores.get(4)}, 19 at ${Sort.binarySearch(scores, 19)}")

    let players = new Array<Player>()
    players.push(new Player("ana", 30))
    players.push(new Player("bo", 50))
    players.push(new Player("cy", 30))
    Sort.sortBy(players, fn(a, b) => b.score - a.score)    // best first; ties keep their order
    for (p in players) { print("${p.name} ${p.score}") }
}
```

Output:

```text
7 .. 88, 19 at 2
bo 50
ana 30
cy 30
```

## Files and paths

`File` (`lib/fs.grav`) is an object for one path: `exists`, `size` (64-bit),
`modified`, `read`, `readLines`, `readBinary`, and the writers `write`, `append`,
`writeLine`/`appendLine`, `writeLines`, `writeBinary`, `appendBinary` — which return
`true` on success — plus `copyTo` (byte-for-byte), `moveTo`, and `delete`.
`Dir` has `exists`, `create` (with parents), `remove`, `list` (sorted names),
`isFile`, `current`, `change`, and `temp`. `Path` has `basename`, `dirname`,
`extension`, `stem`, `withExtension`, `isAbsolute`, `join(parts)`, `combine(a, b[, c])`,
and `normalize` (`a/./b/../c` → `a/c`).

```grav
import "../lib/std.grav"

fn main() {
    let f = new File("/tmp/grav_docs_demo.txt")
    f.write("alpha\n")
    f.writeLine("beta")
    let lines = f.readLines()
    print("${lines.length()} lines, last is ${lines.get(1)}")
    print(Path.extension("report.final.pdf"))
    print(Path.normalize("logs/./2024/../latest"))
    f.delete()
    print(f.exists())
}
```

Output:

```text
2 lines, last is beta
.pdf
logs/latest
false
```

## Time and dates

`Time`: `now()` (unix seconds), `nowMs()`, `monotonicMs()`, `monotonicNs()`,
`sleepMs(ms)`. `Stopwatch`: `reset`, `elapsedNs`, `elapsedMs`, `elapsedSeconds`.
`DateTime` (local time): `now()`, `fromUnix(s)`, `of(y, m, d[, h, mi, s])`;
`year`/`month`/`day`/`hour`/`minute`/`second`, `weekday` (1 = Monday), `dayOfYear`;
`format(strftimePattern)`, `date()`, `time()`, and `toString()` (`2024-02-29 09:30:00`);
`addSeconds`/`addMinutes`/`addHours`/`addDays`, `secondsUntil`, `daysUntil`; comparison
operators; and the helpers `isLeapYear` and `daysInMonth`.

```grav
import "../lib/std.grav"

fn main() {
    let d = DateTime.fromUnix(1700049600)        // 2023-11-15 12:00 UTC
    print(d.addDays(1).format("%Y-%m-%d"))
    print(DateTime.daysInMonth(2024, 2))
    let sw = new Stopwatch()
    Time.sleepMs(5)
    print(sw.elapsedMs() >= 5)
}
```

Output:

```text
2023-11-16
29
true
```

## Processes, OS, I/O, terminal

| Class | Functions |
|-------|-----------|
| `Process` | `exec(cmd)` (exit code), `execOutput(cmd)` (captured stdout), `run(cmd)` → `ProcessResult` (`code`, `output`, `ok()`), `args()`, `pid()`, `sleepMs(ms)`, `exit(code)` |
| `OS` | `platform`, `arch`, `hostname`, `kernel`, `user`, `home`, `cwd`, `chdir`, `pid`, `ppid`, `cpuCount`, `pageSize`, `getEnv`, `getEnvOr`, `hasEnv`, `setEnv`, `unsetEnv`, `isTerminal` |
| `IO` | `print`, `write` (no newline), `eprint` (stderr) — each takes a string or any value; `flush`, `input`, `prompt` (no newline), `readInt`, `readLong`, `readFloat`, `readDouble`, `readBool`, `readAll`, `readLines` |
| `Term` | `bold`, `dim`, `italic`, `underline`, `inverse`, `strike`, `red`…`white`/`gray`, `color(s, n)`, `background(s, n)`, `rgb`, `bgRgb`, `width`, `height`, `clear`, `clearLine`, `moveTo`, `hideCursor`, `showCursor` |

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

`lib/exception.grav` defines `Exception` (`message()`, `kind()`, `describe()`, and
`toString()`) and the subclasses `ValueError`, `TypeError`, `IndexError`, `NotFound`,
`IOError`, `StateError`, `TimeoutError`, `ArithmeticError`, and `RuntimeError` — see
[Exceptions](11-exceptions.md).

Next: [How Grav compiles →](15-how-it-compiles.md)
