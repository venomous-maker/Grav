# The standard library

The standard library is written in Grav itself (with inline C where it talks to
the operating system) and ships with the compiler. Its source is in
[`lib/`](../../lib/); every public function there has a doc comment, which editors
show on hover and these pages list.

## Importing

| Import | Brings in |
|--------|-----------|
| `import "std"` | every category below except `net` |
| `import "std/<category>"` | one category, e.g. `import "std/collections"` |
| `import "std/<category>/<module>"` | one module, e.g. `import "std/collections/hashmap"` |
| `import { A, B as C } from "std/<category>"` | only the names listed |
| `import "std/<category>" as alias` | the names behind a prefix: `alias.Json` |

Each category is a namespace (`std.core`, `std.collections`, ...). Importing
makes its names usable without the prefix, and your own declarations take
priority over the library's. See [Modules and packages](../learn/16-modules-and-packages.md).

## Modules

### core

| Module | Contents |
|--------|----------|
| [exception](core/exception.md) | `Exception` and the standard exception classes |
| [math](core/math.md) | `Math`: roots, powers, trigonometry, rounding, `gcd`, `isPrime`, constants; `Stats`: mean, median, standard deviation, percentiles |
| [random](core/random.md) | `Random` and `Rng`: random numbers, shuffling, choosing |
| [result](core/result.md) | `Result<T, E>` and `Option<T>` data enums |
| [optional](core/optional.md) | `Optional<T>`: a value or nothing, as a class |
| [sort](core/sort.md) | `Sort`: sorting and binary search |
| [timsort](core/timsort.md) | `TimSorter`: the sorting algorithm underneath |
| [panic](core/panic.md) | `Panic`: recovering from panics |
| [gc](core/gc.md) | `GC`: the cycle collector |
| [reflect](core/reflect.md) | `ClassInfo`, `FieldInfo`, `MethodInfo`, `Decorator`: classes, fields and decorators at run time |

### collections

| Module | Contents |
|--------|----------|
| [array](collections/array.md) | `Array<T>`: the growable list |
| [list](collections/list.md) | `List<T>`: a list with collection-style names |
| [deque](collections/deque.md) | `Deque<T>`: add and remove at both ends |
| [stack](collections/stack.md) | `Stack<T>` (last in, first out) and `Queue<T>` (first in, first out) |
| [hashmap](collections/hashmap.md) | `HashMap<K, V>`: fast key-value lookup; `mapOf` |
| [hashset](collections/hashset.md) | `HashSet<T>`: fast membership |
| [map](collections/map.md) | `Map<K, V>`: a small map in insertion order |
| [set](collections/set.md) | `Set<T>`: a small set in insertion order |
| [sortedmap](collections/sortedmap.md) | `SortedMap<K, V>`: keys kept in order |
| [heap](collections/heap.md) | `PriorityQueue<T>`: always the best element next |
| [linkedlist](collections/linkedlist.md) | `LinkedList<T>`: a doubly linked list |
| [bitset](collections/bitset.md) | `BitSet`: a compact set of small integers |
| [tuple](collections/tuple.md) | `Pair<A, B>` and `Triple<A, B, C>`: what `(a, b)` builds |

### text

| Module | Contents |
|--------|----------|
| [string](text/string.md) | `Str`: searching, splitting, trimming, case, padding |
| [strbuf](text/strbuf.md) | `StringBuilder`: building long strings efficiently |
| [convert](text/convert.md) | `Format`: text to numbers and numbers to formatted text |
| [regex](text/regex.md) | `Regex`: regular expressions: find, match, replace, split |

### data

| Module | Contents |
|--------|----------|
| [json](data/json.md) | `Json` and `JsonValue`: parsing, building, writing JSON |
| [bytes](data/bytes.md) | `Bytes`: a buffer of raw bytes |
| [encoding](data/encoding.md) | `Base64`, `Hex`, `Utf8` |
| [hash](data/hash.md) | `Hash` (SHA-256, SHA-1, MD5, CRC-32, FNV-1a, random tokens) and `Uuid` |
| [csv](data/csv.md) | `Csv`: reading and writing comma-separated values |
| [compress](data/compress.md) | `Compress`: gzip, zlib and raw deflate (system zlib) |

### io

| Module | Contents |
|--------|----------|
| [io](io/io.md) | `IO`: console input and output, `printRaw`, `eprint`, `readLine` |
| [fs](io/fs.md) | `File`, `Dir`, `FileReader`, `FileWriter`: files and folders, walking and globbing |
| [path](io/path.md) | `Path`: building and splitting paths |
| [term](io/term.md) | `Term`: colours and cursor control in the terminal |

### sys

| Module | Contents |
|--------|----------|
| [os](sys/os.md) | `OS`: platform, user, environment variables |
| [command](sys/command.md) | `Command`: running programs without a shell |
| [process](sys/process.md) | `Process`: running shell command lines |
| [time](sys/time.md) | `Time`, `Stopwatch` and `Duration`: clocks, measuring, lengths of time |
| [datetime](sys/datetime.md) | `DateTime`: calendar dates and times, parsing and arithmetic |
| [log](sys/log.md) | `Log` and `Logger`: leveled logging to standard error or a file |
| [args](sys/args.md) | `Args`: command-line options, flags and `--help` |

### concurrency

| Module | Contents |
|--------|----------|
| [async](concurrency/async.md) | `Async`, `Mutex`, `AtomicInt`, `Channel<T>`, `awaitAll` |
| [thread](concurrency/thread.md) | `Thread`, `WaitGroup`, `CountDownLatch`, `Semaphore`, `RWLock`, `Once` |

### net (import "std/net")

| Module | Contents |
|--------|----------|
| [tcp](net/tcp.md) | `TcpStream`, `TcpListener`, `Net`: TCP clients and servers |
| [tls](net/tls.md) | `Tls`, `TlsContext`: encrypted connections (OpenSSL), certificates |
| [udp](net/udp.md) | `UdpSocket`, `UdpPacket`: datagrams |
| [unix](net/unix.md) | `UnixSocket`: local connections through a file path |
| [ip](net/ip.md) | `IpAddress`, `Cidr`: IPv4/IPv6 addresses and ranges |
| [url](net/url.md) | `Url`: parsing, building, resolving links, percent-encoding |
| [http](net/http.md) | `HttpRequest`, `HttpResponse`, `Headers`, `Cookie`, `FormData`, streaming and server-sent events |
| [http_server](net/http_server.md) | `HttpServer`, `Cors`: routing, CORS, HTTPS, conditional and range requests |
| [http_client](net/http_client.md) | `HttpClient`: pooled keep-alive, HTTPS, redirects, auth, cookies |
| [websocket](net/websocket.md) | `WebSocket`: two-way connections over HTTP (`ws://`, `wss://`) |
| [smtp](net/smtp.md) | `SmtpClient`, `EmailMessage`: sending email with STARTTLS and login |

## Conventions

The collections share one shape, so moving from one to another is easy:

| Member | Meaning |
|--------|---------|
| `size()` (also `length()`) | the number of elements |
| `isEmpty()`, `clear()` | emptiness, removing everything |
| `contains(x)` | membership |
| `forEach(f)`, `toArray()` | visiting every element, copying into an `Array` |
| `get(i)`, `c[i]` | indexed access, where it makes sense; always bounds-checked |
| `getOr(k, fallback)`, `peekOr(fallback)` | lookups that never fail |
| `remove(x) -> bool` | whether something was removed |
| `for (x in c)` | works on every collection with indexed access |
| `toString()` | the contents as text: `[1, 2]` for sequences, `{1, 2}` for sets, `{"a": 1}` for maps, `("a", 1)` for pairs; strings in quotes |

Across the library:

- **Misuse throws an exception** from [`exception`](core/exception.md):
  `IndexError` for bad positions, `StateError` for taking from something empty,
  `NotFound` for missing keys in strict lookups, `ValueError` for bad arguments,
  `IOError` for the outside world (file writes instead return `false` when they fail).
- **Methods that build a collection return a new one** (`keys()`, `toArray()`,
  `filter`, `map`), so changing the result never changes the source.
- **Most builder-style methods return the object**, so calls chain:
  `sb.append("a").append("b")`, `new Command("ls").arg("-l").run()`.
- **Static-function classes** (`Str`, `Math`, `Path`, `Format`, `OS`, `Time`)
  group related functions; you never create them.

## Built-in functions

`print`, `str`, `input`, `argv`, `assert`, `panic` and the other functions that
need no import are described in the [built-ins reference](../reference/builtins.md).

## About the API sections

Each page has a hand-written introduction and examples, followed by an **API**
section generated from the library source by
[`gen-api.js`](gen-api.js). Run `node docs/std/gen-api.js` after changing the
library; the test suite checks that the pages are up to date.
