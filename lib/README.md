# lib — Grav's standard library

Modules written in Grav (with inline C where they talk to the system), grouped in
categories. Each category is a namespace, `std.<category>`, so a program's own
names never collide with the library's (a program may define its own `Array`; the
library keeps using its own).

| Import | Namespace | Contents |
|--------|-----------|----------|
| `import "std"` | all below except `std.net` | the whole library except networking |
| `import "std/core"` | `std.core` | exceptions, `Math`, `Random`, `Result`/`Option`, `Optional` |
| `import "std/collections"` | `std.collections` | `Array`, `List`, `Stack`/`Queue`, `Set`, `Map`, `HashMap`, `HashSet`, `Deque`, `PriorityQueue`, `LinkedList`, `SortedMap`, `BitSet`, `Pair`/`Triple` |
| `import "std/text"` | `std.text` | `Str`, `StringBuilder`, `Format` |
| `import "std/data"` | `std.data` | `Json`/`JsonValue`, `Bytes` |
| `import "std/io"` | `std.io` | `IO`, `File`/`Dir`, `Path`, `Term` |
| `import "std/sys"` | `std.sys` | `OS`, `Process`, `Command`, `Time`/`Stopwatch`, `DateTime` |
| `import "std/concurrency"` | `std.concurrency` | `Async`, `Mutex`, `AtomicInt`, `Channel`, `Thread`, `WaitGroup`, `Semaphore`, `RWLock`, `Once` |
| `import "std/net"` | `std.net` | TCP, HTTP server/client, WebSockets |

Importing a category (or one module, e.g. `import "std/collections/hashmap"`)
makes its names usable unqualified. Pick names instead with
`import { HashMap, Array as Vec } from "std/collections"`, or keep them behind a
prefix with `import "std/text" as text` (`text.Str.upper(s)`). Fully qualified
names (`std.collections.HashMap`) always work. The old flat paths (`lib/array.grav`,
`lib/hashmap.grav`, …) still work and forward to the new locations.

`prelude.grav` is loaded automatically before the first library module; its `use`
lines let library code in one category refer to the others.

Full documentation with examples: [docs/14-standard-library.md](../docs/14-standard-library.md).

## Collections

All collections share `size()` (`length()` too), `isEmpty()`, `clear()`,
`contains(x)`, `forEach(f)`, and `toArray()`; indexed ones support `c[i]` and
`for (x in c)`. Returned collections (`keys()`, `filter`, …) are copies.

| Module | Provides |
|--------|----------|
| `collections/array` | **`Array<T>`** — growable list (bounds-checked): `add`/`push`, `insert`, `removeAt`, `remove`, `removeWhere`, `swap`, `fill`, `slice`, `addAll`, `indexOf`/`lastIndexOf`, `reverse`, `copy`, `sortBy` (stable), `maxBy`/`minBy`, `findFirst`, and `forEach`/`map`/`mapTo`/`filter`/`reduce`/`fold`/`any`/`all`/`countWhere`/`findIndex` |
| `collections/list` | **`List<T>`** — the same with collection-style names |
| `collections/deque` | **`Deque<T>`** — ring buffer, O(1) at both ends |
| `collections/stack` | **`Stack<T>`** (LIFO) and **`Queue<T>`** (FIFO, O(1)), with `peekOr` |
| `collections/hashmap` | **`HashMap<K,V>`** — O(1) average; `getOr`, `getOrPut`, `forEach(fn(k, v))`; keys: integers, bool, float, string, enums, objects (identity) via `hashOf` overloads |
| `collections/hashset` | **`HashSet<T>`** — O(1) membership, `union`/`intersect`/`difference`/`isSubsetOf` |
| `collections/map` | **`Map<K,V>`** — insertion-ordered, linear; any key with `==` |
| `collections/set` | **`Set<T>`** — insertion-ordered, linear; any element with `==` |
| `collections/sortedmap` | **`SortedMap<K,V>`** — keys ordered by a comparator; `firstKey`/`lastKey`/`keysBetween` |
| `collections/heap` | **`PriorityQueue<T>`** — binary heap ordered by a comparator |
| `collections/linkedlist` | **`LinkedList<T>`** — doubly linked; `addFirst`/`addLast`/`removeFirst`/`removeLast`/`reverse` |
| `collections/bitset` | **`BitSet`** — sets of small non-negative ints; `count`, `unionWith`, `intersectWith` |
| `collections/tuple` | **`Pair<A,B>`**, **`Triple<A,B,C>`** |
| `core/optional` | **`Optional<T>`** — `isPresent`, `get`, `getOr`, `map`, `filter`, `ifPresent` |
| `text/strbuf` | **`StringBuilder`** — `append` (string or any value), `appendInt`/`Float`/`Bool`/`Char`, `appendLine`, `insert`, `truncate`, `reserve`, `toString` |

## Text, numbers, and core types

| Module | Provides |
|--------|----------|
| `text/string` | **`Str`** — inspect (`isBlank`, `isDigits`, …), compare (also ignoring case), search (`indexOf`, `lastIndexOf`, …), transform (`upper`, `capitalize`, `replace`, …), cut (`substring`, `slice`, `removePrefix`, …), layout (`padLeft`, `center`), `split`/`lines`/`words`/`join` |
| `text/convert` | **`Format`** — parsing (`toInt`, `isInt`, `toIntOr`, `parseInt(s, base, fallback)`, …) and number formatting (`fixed`, `grouped`, `percent`, `bytes`, `hex`/`binary`/`octal`) |
| `core/math` | **`Math`** — constants `PI`/`E`/`TAU`, powers/logs, trigonometry, rounding (`roundTo`), `min`/`max`/`abs`/`clamp`/`sign` with integer overloads, `gcd`/`lcm`/`isPrime`/`factorial` |
| `core/random` | **`Random`** (xoshiro256**, per thread, reproducible) — inclusive `nextInt`/`nextLong`, `nextFloat`, `nextGaussian`, `chance`, `shuffle`, `choice`; **`Rng`** for independent streams |
| `core/exception` | **`Exception`** with `ValueError`, `TypeError`, `IndexError`, `NotFound`, `IOError`, `StateError`, `TimeoutError`, `ArithmeticError`, `RuntimeError` |

## System

| Module | Provides |
|--------|----------|
| `io/io` | **`IO`** — `print`/`write`/`eprint` (strings or any value), `prompt`, `readInt`/…, `readAll`, `readLines`; free-function mirrors (`printRaw`, `readLine`, …) |
| `io/fs` | **`File`** — text/lines/binary read and write (writes return success), `size` (64-bit), `modified`, `copyTo`, `moveTo`, `delete`; **`Dir`** — `exists`, `create` (with parents), `remove`, `list`, `temp` |
| `io/path` | **`Path`** — `basename`, `dirname`, `extension`, `stem`, `withExtension`, `isAbsolute`, `join`, `combine`, `normalize` |
| `sys/process` | **`Process`** — `exec` (exit code), `execOutput`, `run` → `ProcessResult`, `args`, `pid`, `exit` |
| `sys/os` | **`OS`** — platform, host, user, environment, CPU count, `isTerminal` |
| `sys/time` | **`Time`**, **`Stopwatch`** |
| `sys/datetime` | **`DateTime`** — `of(y, m, d, …)`, fields, `weekday`, `format`, `toString`, `add…`, `daysUntil`, comparisons, `isLeapYear`, `daysInMonth` |
| `io/term` | **`Term`** — ANSI styles and colours (256 and RGB), cursor control, `width`/`height` |
| `concurrency/async` | **`Async`**, **`Mutex`**, **`AtomicInt`**, **`Channel<T>`**, `awaitAll` — see [docs/16-concurrency.md](../docs/16-concurrency.md) |

Every module is exercised by [`tests/lib/stdlib.grav`](../tests/lib/stdlib.grav),
whose output `tests/run.sh` checks against a golden file.

## Data, threads, processes, networking

| Module | Provides |
|--------|----------|
| `core/result` | **`Result<T, E>`** (`Ok`/`Err`) and **`Option<T>`** (`Some`/`None`) data enums with `unwrap`, `unwrapOr`, `map`, `andThen`, … |
| `data/json` | **`Json`**, **`JsonValue`** — parse (errors with line/column), build, query (`doc["a"][0]`, `path("a.b")`), `toJson`/`pretty` |
| `data/bytes` | **`Bytes`** — growable byte buffer: `appendByte`, `appendUint` (big-endian), `slice`, `toHex`/`fromHex`, `[]` |
| `concurrency/thread` | **`Thread`**, **`WaitGroup`**, **`CountDownLatch`**, **`Semaphore`**, **`RWLock`**, **`Once`** |
| `sys/command` | **`Command`** — run programs without a shell: args, `cwd`, `env`, `stdin`, separate stdout/stderr, `timeoutMs`, `spawn` → **`Child`** |
| `net/tcp` | **`TcpStream`**, **`TcpListener`**, **`Net.resolve`**, **`NetError`** |
| `net/http` | **`HttpServer`** (routes with `:params`, middleware, keep-alive), **`HttpClient`**, **`HttpRequest`**, **`HttpResponse`**, **`Url`**, **`Headers`** |
| `net/websocket` | **`WebSocket`** (server via `WebSocket.route`, client via `connect`), **`WsMessage`** |

See [docs/18-networking.md](../docs/18-networking.md).
