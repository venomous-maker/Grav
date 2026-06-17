# lib — Grav's basic standard library

Small, dependency-free modules you can `import` (paths are relative to the importing
file). Import the umbrella `std.grav`, or individual modules.

| Module           | Provides                                                              |
| ---------------- | -------------------------------------------------------------------- |
| `exception.grav` | `Exception` base class + `ValueError` / `RuntimeError` / `IndexError` / `NotFound`, each with `message()` / `describe()`. |
| `convert.grav`   | **`Format`** class — `toInt` / `toFloat` / `toBool` / `toIntOr` (string → value). The reverse is the built-in `str(x)`. |
| `string.grav`    | **`Str`** class — `length`, `isEmpty`, `equals`, `contains`, `startsWith`, `charAt`, `concat`, `upper`, `lower`, `repeat`, `substring`. |
| `math.grav`      | **`Math`** class — `abs`, `min`, `max`, `sign`, `clamp`, `pow`, `isqrt`, `gcd`, `lcm`, `factorial`. |
| `io.grav`        | `readLine`, `prompt`, `readInt`, `readFloat`, `readBool`, `promptInt`, `eprint`, `printRaw` (over `input()`/`print`). |
| `std.grav`       | imports all of the above.                                            |

Conversions live in **one** place: `Format` parses strings into values, and the
built-in `str(x)` turns any value into a string — so there's no separate "to string"
class. Memory: `new` heap-allocates; the built-in `free(obj)` releases it (no GC).

```grav
import "../../lib/std.grav"

fn main() {
    print(Math.gcd(48, 36));            // 12
    print(Str.upper("hello"));          // HELLO
    let n = Format.toIntOr(prompt("n? "), 0);
    print("n + 1 = ${n + 1}, as text = ${str(n)}");
}
```
