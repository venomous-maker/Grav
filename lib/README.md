# lib — Grav's standard library

Modules written in Grav (with inline C where they talk to the system). Import the
umbrella `std.grav`, or individual modules (paths are relative to the importing
file). Full documentation with examples: [docs/14-standard-library.md](../docs/14-standard-library.md).

## Collections

All collections share `size()` (`length()` too), `isEmpty()`, `clear()`,
`contains(x)`, `forEach(f)`, and `toArray()`; indexed ones support `c[i]` and
`for (x in c)`. Returned collections (`keys()`, `filter`, …) are copies.

| Module | Provides |
|--------|----------|
| `array.grav` | **`Array<T>`** — growable list (bounds-checked): `add`/`push`, `insert`, `removeAt`, `remove`, `removeWhere`, `swap`, `fill`, `slice`, `addAll`, `indexOf`/`lastIndexOf`, `reverse`, `copy`, `sortBy` (stable), `maxBy`/`minBy`, `findFirst`, and `forEach`/`map`/`mapTo`/`filter`/`reduce`/`fold`/`any`/`all`/`countWhere`/`findIndex` |
| `list.grav` | **`List<T>`** — the same with collection-style names |
| `deque.grav` | **`Deque<T>`** — ring buffer, O(1) at both ends |
| `stack.grav` | **`Stack<T>`** (LIFO) and **`Queue<T>`** (FIFO, O(1)), with `peekOr` |
| `hashmap.grav` | **`HashMap<K,V>`** — O(1) average; `getOr`, `getOrPut`, `forEach(fn(k, v))`; keys: integers, bool, float, string, enums, objects (identity) via `hashOf` overloads |
| `hashset.grav` | **`HashSet<T>`** — O(1) membership, `union`/`intersect`/`difference`/`isSubsetOf` |
| `map.grav` | **`Map<K,V>`** — insertion-ordered, linear; any key with `==` |
| `set.grav` | **`Set<T>`** — insertion-ordered, linear; any element with `==` |
| `sortedmap.grav` | **`SortedMap<K,V>`** — keys ordered by a comparator; `firstKey`/`lastKey`/`keysBetween` |
| `heap.grav` | **`PriorityQueue<T>`** — binary heap ordered by a comparator |
| `linkedlist.grav` | **`LinkedList<T>`** — doubly linked; `addFirst`/`addLast`/`removeFirst`/`removeLast`/`reverse` |
| `bitset.grav` | **`BitSet`** — sets of small non-negative ints; `count`, `unionWith`, `intersectWith` |
| `tuple.grav` | **`Pair<A,B>`**, **`Triple<A,B,C>`** |
| `optional.grav` | **`Optional<T>`** — `isPresent`, `get`, `getOr`, `map`, `filter`, `ifPresent` |
| `strbuf.grav` | **`StringBuilder`** — `append` (string or any value), `appendInt`/`Float`/`Bool`/`Char`, `appendLine`, `insert`, `truncate`, `reserve`, `toString` |

## Text, numbers, and data

| Module | Provides |
|--------|----------|
| `string.grav` | **`Str`** — inspect (`isBlank`, `isDigits`, …), compare (also ignoring case), search (`indexOf`, `lastIndexOf`, …), transform (`upper`, `capitalize`, `replace`, …), cut (`substring`, `slice`, `removePrefix`, …), layout (`padLeft`, `center`), `split`/`lines`/`words`/`join` |
| `convert.grav` | **`Format`** — parsing (`toInt`, `isInt`, `toIntOr`, `parseInt(s, base, fallback)`, …) and number formatting (`fixed`, `grouped`, `percent`, `bytes`, `hex`/`binary`/`octal`) |
| `math.grav` | **`Math`** — constants `PI`/`E`/`TAU`, powers/logs, trigonometry, rounding (`roundTo`), `min`/`max`/`abs`/`clamp`/`sign` with integer overloads, `gcd`/`lcm`/`isPrime`/`factorial` |
| `random.grav` | **`Random`** (xoshiro256**, per thread, reproducible) — inclusive `nextInt`/`nextLong`, `nextFloat`, `nextGaussian`, `chance`, `shuffle`, `choice`; **`Rng`** for independent streams |
| `exception.grav` | **`Exception`** with `ValueError`, `TypeError`, `IndexError`, `NotFound`, `IOError`, `StateError`, `TimeoutError`, `RuntimeError` |

## System

| Module | Provides |
|--------|----------|
| `io.grav` | **`IO`** — `print`/`write`/`eprint` (strings or any value), `prompt`, `readInt`/…, `readAll`, `readLines`; free-function mirrors (`printRaw`, `readLine`, …) |
| `fs.grav` | **`File`** — text/lines/binary read and write (writes return success), `size` (64-bit), `modified`, `copyTo`, `moveTo`, `delete`; **`Dir`** — `exists`, `create` (with parents), `remove`, `list`, `temp` |
| `path.grav` | **`Path`** — `basename`, `dirname`, `extension`, `stem`, `withExtension`, `isAbsolute`, `join`, `combine`, `normalize` |
| `process.grav` | **`Process`** — `exec` (exit code), `execOutput`, `run` → `ProcessResult`, `args`, `pid`, `exit` |
| `os.grav` | **`OS`** — platform, host, user, environment, CPU count, `isTerminal` |
| `time.grav` | **`Time`**, **`Stopwatch`** |
| `datetime.grav` | **`DateTime`** — `of(y, m, d, …)`, fields, `weekday`, `format`, `toString`, `add…`, `daysUntil`, comparisons, `isLeapYear`, `daysInMonth` |
| `term.grav` | **`Term`** — ANSI styles and colours (256 and RGB), cursor control, `width`/`height` |
| `async.grav` | **`Async`**, **`Mutex`**, **`AtomicInt`**, **`Channel<T>`**, `awaitAll` — see [docs/16-concurrency.md](../docs/16-concurrency.md) |
| `std.grav` | imports everything above |

Every module is exercised by [`tests/lib/stdlib.grav`](../tests/lib/stdlib.grav),
whose output `tests/run.sh` checks against a golden file.
