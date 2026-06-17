# lib — Grav's basic standard library

Small, dependency-free modules you can `import` (paths are relative to the
importing file). Import the umbrella `std.grav`, or individual modules.

| Module           | Provides                                                              |
| ---------------- | -------------------------------------------------------------------- |
| `exception.grav` | `Exception` base class + `ValueError` / `RuntimeError` / `IndexError` / `NotFound`, each with `message()` / `describe()`. |
| `convert.grav`   | `parseInt` / `parseFloat` / `parseBool` (string → value) and `intToStr`. |
| `io.grav`        | `readLine()`, `prompt(msg)`, `readInt()`, `readFloat()` over the `input()` builtin. |
| `std.grav`       | imports all of the above.                                            |

Memory: `new` heap-allocates; the built-in `free(obj)` releases it (use with care —
there is no GC).

```grav
import "../../lib/std.grav"

fn main() {
    let name = prompt("Name? ");
    let n = readInt();
    try {
        if (n < 0) { throw new ValueError("must be >= 0"); }
        print("hi ${name}, n+1 = ${n + 1}");
    } catch (e: Exception) {
        print(e.describe());
    }
}
```
