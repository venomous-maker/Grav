# lib — Grav's basic standard library

Small, dependency-free modules you can `import` (paths are relative to the importing
file). Import the umbrella `std.grav`, or individual modules.

| Module           | Provides                                                              |
| ---------------- | -------------------------------------------------------------------- |
| `exception.grav` | `Exception` base class + `ValueError` / `RuntimeError` / `IndexError` / `NotFound`, each with `message()` / `describe()`. |
| `convert.grav`   | **`Format`** class — string→value (`toInt` / `toLong` / `toUInt` / `toByte` / `toFloat` / `toDouble` / `toBool` / `toIntOr`, validation `isInt` / `isNumber`), numeric conversions (`intToFloat` / `floatToInt` / `intToLong` / `longToInt` / `intToByte` / `intToDouble`), `hex`, and string↔binary (`toBinary` / `fromBinary`). The general value→string is the built-in `str(x)`. |
| `string.grav`    | **`Str`** class — `length`, `isEmpty`, `equals`, `compare`, `contains`, `count`, `indexOf`, `startsWith`, `endsWith`, `charAt`, `concat`, `upper`, `lower`, `reverse`, `repeat`, `substring`, `remove`, `trim` / `trimStart` / `trimEnd`, `replace`, `padLeft` / `padRight`, `split` → `Array<string>`, `join`. |
| `math.grav`      | **`Math`** class — floating-point `sqrt`, `cbrt`, `pow`, `abs`, `sin`, `cos`, `tan`, `atan2`, `floor`, `ceil`, `round`, `log`, `log10`, `exp`, `min`, `max`, `pi()`, `e()` (over `<math.h>`). |
| `array.grav`     | **`Array<T>`** — growable list: `length`, `isEmpty`, `push`, `pop`, `get`, `set`, `arr[i]` / `arr[i] = v`, `first`, `last`, `clear`, `insert`, `removeAt`, `indexOf`, `contains`, `reverse`, `dispose`, and higher-order `forEach` / `forEachIndexed` / `map` / `filter` / `reduce` / `any` / `all` / `countWhere` / `findIndex` / `copy` / `sortBy(cmp)` (stable merge sort). Generic methods `xs.mapTo::<U>(f)` and `xs.fold::<A>(init, f)` change the element type (free-function forms `mapTo::<T, U>` / `fold::<T, A>` also exist). Bounds-checked (out of range panics); iterable with `for (x in arr)`. |
| `list.grav`      | **`List<T>`** — thin convenience wrapper over `Array<T>` (`add`/`get`/`set`/`remove`/…, `forEach`, `any`, `all`, `sortBy`, `toArray`). |
| `stack.grav`     | **`Stack<T>`** (`push`/`pop`/`peek`) and **`Queue<T>`** (`enqueue`/`dequeue`/`peek`). |
| `set.grav`       | **`Set<T>`** — `add`, `contains`, `remove`, `size`, `at`, `union`, `intersect`. |
| `map.grav`       | **`Map<K,V>`** — insertion-ordered, linear lookup: `put`, `get` (panics if missing), `getOr`, `m[k]` / `m[k] = v`, `containsKey`, `remove`, `size`, `keys`, `values`. |
| `hashmap.grav`   | **`HashMap<K,V>`** — O(1) average hash table for `int`/`long`/`string` keys: `put`, `get` (zero value if missing), `getOr`, `m[k]` / `m[k] = v`, `containsKey`, `remove`, `size`, `isEmpty`, `clear`, `keys`, `values`. Add a `hashOf(x: T) -> long` overload for other key types. |
| `strbuf.grav`    | **`StringBuilder`** — amortized O(1) `append` / `appendLine` / `appendInt` / `appendFloat` / `appendBool` / `appendChar` / `repeat` (chainable), `length`, `isEmpty`, `clear`, `toString` (printable directly). |
| `random.grav`    | **`Random`** — `seed`, `seedTime`, `nextInt`, `nextFloat`, `nextBool`. |
| `datetime.grav`  | **`DateTime`** — `now`, `fromUnix`, `unix`, `year`/`month`/`day`/`hour`/`minute`/`second`, `format`, `addDays`, `addHours` (over `<time.h>`). |
| `fs.grav`        | **`File`** object (`new File(path)`) — `exists`, `size`, `name`, `read`, `write`, `append`, `writeLine`, `readLines` → `Array<string>`, `readBinary`/`writeBinary`/`appendBinary` (`binary`), `copyTo`, `delete`. |
| `path.grav`      | **`Path`** — `basename`, `dirname`, `extension`, `join`. |
| `process.grav`   | **`Process`** — `exec`, `execOutput`, `pid`, `sleepMs` (over `popen`/`system`). |
| `os.grav`        | **`OS`** — `platform`, `arch`, `hostname`, `kernel`, `user`, `home`, `cwd`, `chdir`, `pid`, `ppid`, `cpuCount`, `pageSize`, `getEnv`/`getEnvOr`/`hasEnv`/`setEnv`/`unsetEnv`, `isTerminal`. |
| `time.grav`      | **`Time`** — `now` (unix s), `nowMs`, `monotonicMs`, `monotonicNs`, `sleepMs`; **`Stopwatch`** — `reset`, `elapsedNs`/`elapsedMs`/`elapsedSeconds`. |
| `term.grav`      | **`Term`** — ANSI styles `bold`/`dim`/`italic`/`underline`, colours `red`…`gray`, 256-colour `color(s, n)`, `clear`, `clearLine`, `moveTo`, `hideCursor`/`showCursor`. |
| `io.grav`        | free functions `readLine`/`prompt`/`readInt`/`readFloat`/`readBool`/`promptInt`/`eprint`/`printRaw`, plus an **`IO`** class grouping them as static methods (`IO.print`, `IO.write`, `IO.eprint`, `IO.input`, `IO.prompt`, `IO.readInt`/`readLong`/`readFloat`/`readDouble`/`readBool`, `IO.promptInt`). |
| `std.grav`       | imports all of the above.                                            |

Conversions live in **one** place: `Format` parses strings into values, and the
built-in `str(x)` turns any value into a string — so there's no separate "to string"
class (give your own classes a `toString()` and `str`/`print`/`${}` will use it).
Memory: `new` heap-allocates; the built-in `free(obj)` runs the object's destructor and
releases it (no GC). Every collection here has a destructor, so `free(coll)` also
frees its internal buffers.

```grav
import "../../lib/std.grav"

fn main() {
    print(Math.sqrt(2.0));              // 1.41421
    print(Str.upper("hello"));          // HELLO
    let n = Format.toIntOr(prompt("n? "), 0);
    print("n + 1 = ${n + 1}, as text = ${str(n)}");
}
```
