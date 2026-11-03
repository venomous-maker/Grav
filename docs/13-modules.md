# 13. Modules, namespaces & C interop

## Imports

`import "path"` includes another file's declarations. Each file is included at most
once, so repeated and circular imports are harmless. A file without `main` is a
library module. The `.grav` extension may be left off.

```grav
// file: util/math.grav
namespace mathx {
    export fn square(n: int) -> int { return n * n }
    export fn clamp(v: int, lo: int, hi: int) -> int { return v < lo ? lo : (v > hi ? hi : v) }
}
```

```grav
// file: app.grav
import "util/math"
import "std"                      // the whole standard library

fn main() {
    print(mathx.square(7))
    print(mathx.clamp(150, 0, 100))
}
```

`import "std"` pulls in the whole standard library; `import "std/hashmap"` imports one
module of it. An import is looked up in this order:

1. relative to the importing file (or as written, if it is an absolute path);
2. `std` and `std/<module>` in the standard library directory;
3. each directory in the project's `grav.toml` `paths`;
4. each directory in the `GRAV_PATH` environment variable (`:`-separated);
5. the standard library directory, so `import "hashmap"` also works.

If nothing matches, the error lists every location that was tried.

### Projects: grav.toml

A `grav.toml` in the project directory (found by searching upward from the file
being compiled, or from the current directory) sets the default entry point and
extra import roots:

```toml
name = "inventory"
main = "src/main.grav"       # used by `grav run`, `grav build`, `grav test` with no file
paths = ["src", "vendor"]    # extra import roots, relative to grav.toml
```

## Namespaces

`namespace name { ... }` groups declarations; refer to them as `name.member`.
Namespaces nest (`outer.inner.member`) and can span several files. Inside a
namespace, its own names can be used unqualified.

```grav
namespace geometry {
    class Circle {
        public r: float
        constructor(r: float) { self.r = r }
        fn area() -> float { return 3.14159 * self.r * self.r }
    }
    namespace units {
        fn cm(x: float) -> string { return "${x} cm" }
    }
    fn describe(c: Circle) -> string { return "circle of " + units.cm(c.r) }
}

fn main() {
    let c = new geometry.Circle(2.0)
    print(geometry.describe(c))
    print(geometry.units.cm(c.area()))
}
```

Output:

```text
circle of 2 cm
12.5664 cm
```

## export and decorators

`export` marks a declaration as part of a module's public surface, and `@Name` or
`@Name(args)` decorators attach metadata. Both are accepted on any top-level
declaration and have no runtime effect today — every top-level name is visible to
importers — so they document intent and are available to tooling.

```grav
@Deprecated("use total()")
export fn sum(a: int, b: int) -> int { return a + b }

@Entity("users")
export class User {
    public name: string
    constructor(name: string) { self.name = name }
}

fn main() { print(sum(2, 3)) }
```

Output:

```text
5
```

## Macros

`#define` creates textual macros, applied before parsing — constants or
function-like macros.

```grav
#define MAX_USERS 100
#define SQUARE(x) ((x) * (x))

fn main() {
    print(MAX_USERS)
    print(SQUARE(7))
}
```

Output:

```text
100
49
```

## Inline C

`%{ ... %}` embeds C code directly — the escape hatch for system calls and existing C
libraries.

- At **top level**, it can declare C helpers, globals, and `#include`s (which are
  hoisted to the top of the generated file).
- As an **expression**, give it a type with `as T` or a typed `let`.
- As a **statement**, end it with `;`.
- Grav locals and parameters are visible inside it by name.

```grav
%{
#include <ctype.h>
static int c_count_upper(const char* s) {
    int n = 0;
    for (; *s; s++) if (isupper((unsigned char)*s)) n++;
    return n;
}
%}

fn countUpper(s: string) -> int { return %{ c_count_upper(s) %} as int }

fn main() {
    print(countUpper("Hello World From Grav"))
    let total = 0
    %{ for (int i = 1; i <= 4; i++) total += i * i; %};
    print(total)
    let pid: int = %{ 12345 %}
    print(pid)
}
```

Output:

```text
4
30
12345
```

Grav type spellings in C: `int` → `int`, `long` → `long long`, `float` →
`double`, `bool` → `bool`, `string` → `const char*`, a class `C` → `C*` (a struct
pointer whose fields have the same names), `binary` → `GravBytes {data, len}`.
The standard library itself (`lib/*.grav`) is written this way and is the best
reference for wrapping C APIs.

Next: [Standard library →](14-standard-library.md)
