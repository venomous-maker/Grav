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

### The standard library

The standard library is grouped in categories, each its own namespace:

| Import | Namespace | Contents |
|--------|-----------|----------|
| `import "std"` | every category but `std.net` | the whole library except networking |
| `import "std/core"` | `std.core` | exceptions, `Math`, `Random`, `Result`/`Option`, `Optional` |
| `import "std/collections"` | `std.collections` | `Array`, `HashMap`, `HashSet`, `Deque`, `SortedMap`, `Stack`/`Queue`, … |
| `import "std/text"` | `std.text` | `Str`, `StringBuilder`, `Format` |
| `import "std/data"` | `std.data` | `Json`/`JsonValue`, `Bytes` |
| `import "std/io"` | `std.io` | `IO`, `File`/`Dir`, `Path`, `Term` |
| `import "std/sys"` | `std.sys` | `OS`, `Process`, `Command`, `Time`, `DateTime` |
| `import "std/concurrency"` | `std.concurrency` | `Mutex`, `AtomicInt`, `Channel`, `Thread`, `WaitGroup`, … |
| `import "std/net"` | `std.net` | TCP, HTTP, WebSockets (chapter 18) |

Single modules import too (`import "std/collections/hashmap"`, `import
"std/net/http"`). Importing makes the module's names usable unqualified in your
file. Three forms control what you get:

```grav
// file: forms.grav
import "std/text"                                  // every name: Str, StringBuilder, Format
import { HashMap, Array as Vec } from "std/collections"   // only these, Array renamed
import "std/data" as data                          // behind a prefix: data.Json
```

Because the library lives in namespaces, your own declarations never clash
with it. A program may define its own `Array` or `Str`; unqualified names in your
code then mean yours, the library keeps using its own, and the library's stays
reachable by its full name:

```grav
import "std"

class Str {                       // ours, not the library's
    static fn length(s: string) -> int { return -1 }
}

fn main() {
    print(Str.length("abc"))              // ours
    print(std.text.Str.length("abc"))     // the library's
    let m = new HashMap<string, int>()    // HashMap's code still uses std's Str
    m.put("k", 1)
    print(m.size())
}
```

Output:

```text
-1
3
1
```

A name that exists but was not imported gets a hint in the error, such as
`note: 'Deque' is declared in std.collections; write use std.collections.Deque …`.

### use

`use` brings names from any namespace into scope (yours or the library's).
`::` may be written for `.`.

```grav
namespace shapes {
    class Circle {
        public r: int
        constructor(r: int) { self.r = r }
    }
    fn area(c: Circle) -> int { return 3 * c.r * c.r }
}
namespace legacy {
    class Circle {
        public d: int
        constructor(d: int) { self.d = d }
    }
}

use shapes::*                     // every name in shapes
use legacy.Circle as OldCircle    // one name, renamed
use shapes as sh                  // a namespace alias

fn main() {
    print(area(new Circle(2)))
    print(new OldCircle(9).d)
    print(sh.area(new sh.Circle(1)))
}
```

Output:

```text
12
9
3
```

A `use` applies to the namespace it is written in (at the top of a file: the
global scope). Lookup goes outwards from the current namespace, and at each level
that level's own declarations win over names brought in by `use`.

### Where imports are found

An import is looked up in this order:

1. relative to the importing file (or as written, if it is an absolute path);
2. `std` and `std/<category>[/<module>]` in the standard library directory;
3. a dependency from `grav.toml`: `import "name"` or `import "name/sub/file"`;
4. each directory in the project's `grav.toml` `paths`;
5. each directory in the `GRAV_PATH` environment variable (`:`-separated);
6. the standard library directory, so `import "collections/hashmap"` also works.

If nothing matches, the error lists every location that was tried. The old flat
paths (`lib/array.grav`, `lib/hashmap.grav`, …) still work and forward to the
new locations.

### Projects: grav.toml

A `grav.toml` in the project directory (found by searching upward from the file
being compiled, or from the current directory) sets the default entry point,
extra import roots, and dependencies. `grav new <name>` creates one with a
`src/main.grav` and a test.

```toml
name = "inventory"
main = "src/main.grav"       # used by `grav run`, `grav build`, `grav test` with no file
paths = ["src", "vendor"]    # extra import roots, relative to grav.toml

[dependencies]
colors = "github.com/someone/grav-colors@v1.2.0"
http2 = { git = "https://gitlab.com/team/http2", version = "v0.3.1" }
```

### Packages

Third-party packages are git repositories on GitHub, GitLab, any other git host,
or a local path. The standard library ships with the compiler and is never
downloaded.

```bash
grav add github.com/someone/grav-colors@v1.2.0   # add a dependency and install it
grav add gitlab.com/team/http2                   # no version: the newest tag
grav install                                      # install everything (exact commits)
grav update [name]                                # move to the newest tags
grav remove colors                                # drop a dependency
grav packages                                     # list dependencies and locations
```

- **Cache:** each version is cloned once into `~/.local/grav/packages/<host>/<owner>/<repo>@<version>`
  (override the location with `GRAV_PACKAGES`). Without tags, the default branch is
  used and pinned to its commit.
- **Lock file:** `grav.lock` records the exact commit of every dependency, including
  dependencies of dependencies. Commit it, and `grav install` reproduces the same code.
- **Importing:** `import "colors"` loads the package's entry file: its own
  `grav.toml` `lib = "..."`, else `colors.grav`, `lib.grav` or `src/lib.grav`.
  `import "colors/src/palette"` loads one file. A package's imports resolve
  against its own `grav.toml`.

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
12.56636 cm
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
