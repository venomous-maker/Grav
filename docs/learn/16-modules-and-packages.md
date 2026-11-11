# 16. Modules and packages

A program of a few hundred lines fits in one file. Beyond that, you split it:
one file for the data model, one for storage, one for the user interface. And
you reuse code other people wrote: the standard library, and packages from git.

In this chapter you will learn:

- how `import` brings in other files and the standard library,
- namespaces, which keep names from different parts of a program apart,
- `use`, for shorter names,
- projects with a `grav.toml`, and where imports are looked for,
- packages: adding, installing and updating dependencies.

## Splitting a program into files

`import "path"` makes another file's declarations available. Here a small
library and a program that uses it:

```grav
// file: util/mathx.grav
namespace mathx {
    fn square(n: int) -> int { return n * n }
    fn clamp(v: int, lo: int, hi: int) -> int { return v < lo ? lo : (v > hi ? hi : v) }
}
```

```grav
// file: app.grav
import "util/mathx"

fn main() {
    print(mathx.square(7))
    print(mathx.clamp(150, 0, 100))
}
```

- Paths are relative to the importing file. The `.grav` extension can be left off.
- A file is loaded once, however many files import it, so repeated and circular
  imports are harmless.
- A file without `main` is a library. Only the file you compile needs `main`.

## Namespaces

If two files both declare a `parse` function, which one does a call mean?
Namespaces avoid the question. `namespace name { ... }` puts declarations under a
name, and outside code refers to them as `name.member`:

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
    fn describe(c: Circle) -> string {
        return "circle of " + units.cm(c.r)   // own names need no prefix inside
    }
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

Namespaces nest (`geometry.units.cm`) and can be spread over several files.

### use: shorter names

`use` makes names from a namespace available without the prefix. It has three
forms:

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
use shapes as sh                  // a shorter name for the namespace

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

`::` and `.` mean the same in `use`. A `use` applies inside the namespace it is
written in (at the top of a file, to the whole file). When a name could mean two
things, the closer declaration wins: a namespace's own names beat names brought
in with `use`.

## The standard library

`import "std"` loads the standard library. It is organised in categories, each
in its own namespace, which you can also import on their own:

| Import | Contents |
|--------|----------|
| `import "std"` | everything except networking |
| `import "std/core"` | exceptions, `Math`, `Random`, `Result`/`Option`, `Optional`, `Panic`, `GC` |
| `import "std/collections"` | `Array`, `HashMap`, `HashSet`, `Deque`, `Stack`, `Queue`, `SortedMap`, ... |
| `import "std/text"` | `Str`, `StringBuilder`, `Format` |
| `import "std/data"` | `Json`, `Bytes` |
| `import "std/io"` | `IO`, `File`, `Dir`, `Path`, `Term` |
| `import "std/sys"` | `OS`, `Process`, `Command`, `Time`, `DateTime` |
| `import "std/concurrency"` | `Mutex`, `AtomicInt`, `Channel`, `Thread`, `WaitGroup`, ... |
| `import "std/net"` | TCP, HTTP and WebSockets |

A single module can be imported too: `import "std/collections/hashmap"`. The
[standard library reference](../std/README.md) has a page for every module.

Importing makes the module's names usable without a prefix. Three import forms
let you choose what you get:

```grav
// file: forms.grav
import "std/text"                                          // every name
import { HashMap, Array as Vec } from "std/collections"    // only these, Array renamed
import "std/data" as data                                  // behind a prefix: data.Json
```

Because the library lives in namespaces, your names never clash with it. If
your program declares its own `Str`, your code means yours, the library keeps
using its own, and the library's is still reachable by its full name:

```grav
import "std"

class Str {
    static fn length(s: string) -> int { return -1 }
}

fn main() {
    print(Str.length("abc"))              // ours
    print(std.text.Str.length("abc"))     // the library's
}
```

Output:

```text
-1
3
```

## Projects

A *project* is a folder with a `grav.toml` file. Create one with:

```bash
grav new inventory
cd inventory
grav run
```

That makes:

```text
inventory/
├── .gitignore
├── grav.toml
└── src/main.grav
```

`grav.toml` describes the project:

```toml
name = "inventory"
main = "src/main.grav"       # used by grav run, grav build and grav test without a file
paths = ["src", "vendor"]    # extra places to look for imports

[dependencies]
colors = "github.com/someone/grav-colors@v1.2.0"
http2 = { git = "https://gitlab.com/team/http2", version = "v0.3.1" }
```

Inside a project, `grav run`, `grav build` and `grav test` work without naming a
file. The compiler finds `grav.toml` by looking in the current folder and its
parents.

### Where imports are found

`import "x"` is looked for in this order, and the error lists every place tried
when nothing matches:

1. relative to the importing file;
2. `std` and `std/...` in the standard library;
3. a dependency named `x` in `grav.toml` (`import "colors"`, `import "colors/sub/file"`);
4. each folder in `grav.toml`'s `paths`;
5. each folder in the `GRAV_PATH` environment variable (separated by `:`);
6. the standard library folder itself.

## Packages

A package is a git repository of Grav code, on GitHub, GitLab, any git host, or a
local path. The `grav` command manages them:

```bash
grav add github.com/someone/grav-colors@v1.2.0   # add a dependency and install it
grav add gitlab.com/team/http2                   # no version: the newest tag
grav install                                      # install everything grav.lock lists
grav update                                       # move every dependency to its newest tag
grav update colors                                # just one
grav remove colors
grav packages                                     # list dependencies and where they are
```

- **Versions** are git tags. Without tags, the default branch is used, pinned to
  its current commit.
- **`grav.lock`** records the exact commit of every dependency, including the
  dependencies of dependencies. Commit it to version control: `grav install`
  then gives everyone the same code.
- **The cache** keeps one copy of each version in `~/.local/grav/packages`
  (set `GRAV_PACKAGES` to move it), shared by all your projects.
- **Importing** a package by name (`import "colors"`) loads its entry file: the
  `lib` named in its own `grav.toml`, or else `colors.grav`, `lib.grav` or
  `src/lib.grav`. The names in the package's namespaces are then usable without
  a prefix, as with the standard library (your own declarations win a clash).
- **Local packages:** `grav add ../grav-db` uses a package straight from a folder
  on your machine, before it is published. It writes
  `grav-db = { path = "../grav-db" }` to `grav.toml`: the folder is read in
  place, so edits show at once, `grav install` installs the package's own
  dependencies, and nothing is pinned in `grav.lock`. Publish it, then switch
  with `grav add github.com/you/grav-db`. (`grav add --git <folder>` clones a
  local repository's commits instead.)

To publish a package, push a repository with a `grav.toml` (with a `lib = "..."`
entry) and tag a version: `git tag v1.0.0 && git push --tags`.

## export and decorators

`export` marks a declaration as part of a module's public interface, and
decorators like `@Deprecated("...")` attach notes to declarations. `export` is
documentation: every top-level declaration is visible to importers. Decorators
do nothing by themselves, but on classes and their members they are recorded
with their arguments, and code reads them at run time through
[reflection](../std/core/reflect.md) (`@Table("users")`, `@Column("email")`).

```grav
@Deprecated("use total()")
export fn sum(a: int, b: int) -> int { return a + b }

fn main() {
    print(sum(2, 3))
}
```

Output:

```text
5
```

## Summary

- `import "path"` loads another file once; `import "std"` loads the standard library.
- `namespace` groups declarations; `use` shortens names; `import { A as B } from`
  and `import "x" as alias` control what an import brings in.
- `grav new` creates a project with a `grav.toml` that names the entry point,
  import paths and dependencies.
- `grav add`, `install`, `update` and `remove` manage git-based packages, pinned by `grav.lock`.

## Exercises

1. Split a program into `model.grav` (a `Task` class), `store.grav` (functions that
   keep tasks in an `Array`) and `main.grav`.
2. Put `model.grav`'s class in `namespace todo` and use it from `main.grav` both
   with the prefix and with `use todo::*`.
3. Create a project with `grav new`, move your files into `src/`, and run it with
   plain `grav run`.
4. Import only `HashMap` from `std/collections` and check that `Array` is now
   unknown. Read the hint in the error.

Next: [C interop →](17-c-interop.md)
