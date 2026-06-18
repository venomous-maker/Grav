# lib — Grav's basic standard library

Small, dependency-free modules you can `import` (paths are relative to the importing
file). Import the umbrella `std.grav`, or individual modules.

| Module           | Provides                                                              |
| ---------------- | -------------------------------------------------------------------- |
| `exception.grav` | `Exception` base class + `ValueError` / `RuntimeError` / `IndexError` / `NotFound`, each with `message()` / `describe()`. |
| `convert.grav`   | **`Format`** class — `toInt` / `toFloat` / `toBool` / `toIntOr` (string → value). The reverse is the built-in `str(x)`. |
| `string.grav`    | **`Str`** class — `length`, `isEmpty`, `equals`, `contains`, `count`, `indexOf`, `startsWith`, `endsWith`, `charAt`, `concat`, `upper`, `lower`, `reverse`, `repeat`, `substring`, `remove`, `trim` / `trimStart` / `trimEnd`, `replace`, `padLeft` / `padRight`, `split` → `Array<string>`, `join`. |
| `math.grav`      | **`Math`** class — floating-point `sqrt`, `cbrt`, `pow`, `abs`, `sin`, `cos`, `tan`, `atan2`, `floor`, `ceil`, `round`, `log`, `log10`, `exp`, `min`, `max`, `pi()`, `e()` (over `<math.h>`). |
| `array.grav`     | **`Array<T>`** — growable list: `length`, `isEmpty`, `push`, `pop`, `get`, `set`, `first`, `last`, `clear`, `insert`, `removeAt`, `indexOf`, `contains`, `reverse`, `dispose`. |
| `list.grav`      | **`List<T>`** — thin convenience wrapper over `Array<T>`. |
| `stack.grav`     | **`Stack<T>`** (`push`/`pop`/`peek`) and **`Queue<T>`** (`enqueue`/`dequeue`/`peek`). |
| `set.grav`       | **`Set<T>`** — `add`, `contains`, `remove`, `size`, `at`, `union`, `intersect`. |
| `map.grav`       | **`Map<K,V>`** — `put`, `get`, `getOr`, `containsKey`, `remove`, `size`, `keys`, `values`. |
| `random.grav`    | **`Random`** — `seed`, `seedTime`, `nextInt`, `nextFloat`, `nextBool`. |
| `datetime.grav`  | **`DateTime`** — `now`, `fromUnix`, `unix`, `year`/`month`/`day`/`hour`/`minute`/`second`, `format`, `addDays`, `addHours` (over `<time.h>`). |
| `fs.grav`        | **`File`** — `exists`, `read`, `write`, `append`, `delete`, `size`. |
| `path.grav`      | **`Path`** — `basename`, `dirname`, `extension`, `join`. |
| `process.grav`   | **`Process`** — `exec`, `execOutput`, `pid`, `sleepMs` (over `popen`/`system`). |
| `io.grav`        | `readLine`, `prompt`, `readInt`, `readFloat`, `readBool`, `promptInt`, `eprint`, `printRaw` (over `input()`/`print`). |
| `std.grav`       | imports all of the above.                                            |

Conversions live in **one** place: `Format` parses strings into values, and the
built-in `str(x)` turns any value into a string — so there's no separate "to string"
class. Memory: `new` heap-allocates; the built-in `free(obj)` releases it (no GC).

```grav
import "../../lib/std.grav"

fn main() {
    print(Math.sqrt(2.0));              // 1.41421
    print(Str.upper("hello"));          // HELLO
    let n = Format.toIntOr(prompt("n? "), 0);
    print("n + 1 = ${n + 1}, as text = ${str(n)}");
}
```
