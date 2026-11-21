# Changelog

Every release of the Grav compiler and its standard library. Versions follow
[semantic versioning](https://semver.org/): a minor release adds language
features or library APIs, a patch release fixes bugs, and a major release may
break working programs. Each release is a `vX.Y.Z` tag with a GitHub release
carrying its section of this file.

Packages state the compiler versions they need in `grav.toml`
(`grav = ">=1.7.0"`); see [Installing Grav](docs/tools/install.md) and
[Releasing](docs/tools/releasing.md).

## Unreleased

### Fixes

- The cycle collector disposes of a dead cycle's objects in order, an object
  before the objects it holds, as plain reference counting would: a destructor
  no longer finds what its object holds already torn down. (A database
  connection pool caught in a cycle crashed or corrupted memory when its
  connections' destructors walked their statement caches.)
- A bad memory access other than a null reference or a stack overflow ends the
  program at once with its report, in tests and recovery regions too:
  unwinding from it freed memory and could hang, when the fault was inside
  `malloc`.
- `grav install` after a dependency's version was changed by hand in
  `grav.toml`: the old version's commit from `grav.lock` is no longer checked
  out and filed under the new version's name, and a cached package folder that
  is not at its tag (left by an older grav) is fetched again.

## 1.10.0 (2026-11-20)

### Language

- **Generic local functions use the block's variables**: `fn join<T>(a: T, b: T)`
  inside a block can read the variables declared before it. Each call passes
  their values (hidden leading parameters), so it cannot assign them; a call
  where one of them is shadowed by another variable of that name is an error.
- **Tuple patterns in `match`**: `case (0, y) if y > 1:`, `case (_, "go", 1..=3):`,
  nested `case ((a, 2), s):`, several patterns per case (`case (0, _), (_, 0):`)
  and `case _:`, on a `Pair`, `Triple` or `Quad`; `match (a, b)` matches the
  tuple of the two. A case after one that matches every tuple is an error, and
  a tuple `match` used as a value needs one.

### Tools

- **`grav fmt` lays code out**, not only indents it: spacing within lines
  (operators, commas, colons, keywords and their parentheses, `} else {`), and a
  `{ a; b }` on one line becomes one statement per line. `<`/`>` in type
  arguments, map literals, preprocessor lines and wider gaps (alignment) are
  kept; continuation lines lined up under a bracket keep their place. It only
  changes whitespace, line breaks and the `;`s it splits at; formatting its
  result again changes nothing.
- Editors (VS Code extension 1.16.0, the language server): a tuple pattern's
  names have their types in hovers and inlay hints. `grav check --types` no
  longer lists the compiler's hidden locals.
- Generated C builds without warnings under Clang (`CC=clang`), and the
  compiler builds with `clang++`; it finds its own executable on macOS too, and
  the networking library falls back where macOS lacks `SOCK_CLOEXEC` and
  `MSG_NOSIGNAL`. (Untested on a Mac.)

### Standard library

- `StringBuilder.appendInt` writes the digits straight into the buffer instead
  of making a string of them; `Json.parse` of a small document no longer sets up
  its shared-key table (about 16% faster for a three-field object).

### Fixes

- `grav fmt` keeps the lines of a `"""` string as they are (it re-indented
  them, changing the string), and no longer indents the line after a `"""`
  string or a multi-line `%{ %}` block as a continuation.

## 1.9.0 (2026-11-20)

### Standard library

- More things can be walked one at a time instead of read whole:
  `File.lines()` and `FileReader` (a file's lines), `IO.lines()` (standard
  input), `Dir.entries(path)` and `Dir.files(path)` (a directory's names, and
  every file under it), `Regex.findEach(text)` (matches found as they are asked
  for), and JSON values (`for (item in json)`, `for ((k, v) in obj.entries())`).

### Fixes

- A class in a namespace can implement a generic interface of a type from that
  namespace (`class Items implements Iterator<Item>` in `namespace shop`).

## 1.8.0 (2026-11-19)

### Language

- **Generic methods in interfaces** (and traits, with default bodies): calls
  through the interface reach each implementing class's own copy for the type
  arguments the program uses.

### Build speed

- **Large programs compile several times faster.** The front end is about four
  times faster (name lookups are cached, and AST node tests are a pointer
  comparison instead of `dynamic_cast`), and GCC optimizes a large program's C
  on every core (`-flto=auto`). The example shop app (framework and all its
  packages, 1.5 million lines of C) went from 7 minutes to under 1.5.
- **Edits rebuild faster**: generated C no longer changes outside the code that
  changed (temporaries are numbered per function, lambdas and call adapters are
  named after where they are, and a build's C names itself the same way each
  time), and GCC 15+
  keeps its optimized partitions between builds (`-flto-incremental`). Editing
  one function of the example app rebuilds it in about 40 seconds.
- **One front-end pass instead of three** for most edits: inferred generic type
  arguments are kept between builds and reused for every function and method
  whose text (and the program's declarations) is unchanged. `grav check` of the
  example app after an edit inside a function: about 4 seconds instead of 9.
- **Less generated C**: functions nothing can reach (most methods of generic
  instances) are left out; the example app's C is 60% smaller, and `-Og` builds
  of it compile about 27% faster. A build to a new output path reuses GCC's
  optimized partitions too.
- **A build cache** (`~/.cache/grav/builds`): building, running or testing a
  program that has not changed reuses its binary in a fraction of a second; a
  change that leaves the generated C the same skips the C compiler. `GRAV_CACHE`,
  `GRAV_BUILD_CACHE=0`, `GRAV_CACHE_MAX` and `GRAV_LTO` control it.

### Fixes

- A generic method's type bounds (`fn m<T: Named>(...)`) are checked: a call
  with a type that breaks them is an error at the call, instead of an error
  inside the method body (or in an unrelated class with a method of that name).

## 1.7.0 (2026-11-19)

### Breaking changes

- The standard library **throws exceptions instead of panicking** on misuse:
  `IndexError` (an index out of range), `StateError` (`pop` on an empty
  container, `unwrap()` on `Err`, a closed channel), `NotFound` (`Map.get` of a
  missing key), `ValueError` (a sort comparison with no order). An uncaught one
  prints `uncaught exception: ...` instead of `panic: ...`, and a broad
  `catch (e: Exception)` now catches them.
- `for (x in v)` on a class with both `get(i)` and `iterator()` uses the
  iterator: a `LinkedList` loop is O(n) instead of O(n²), and a map with `int`
  keys is walked by its entries instead of by index.
- Maps hand out `Entry<K, V>` values (fields `first` and `second`) instead of
  `Pair<K, V>` objects when walked.
- `case 1:` followed directly by another `case` is an error; write `case 1, 2:`.
- String fields start out empty instead of null.

### Language

- **Iterators**: `for (x in v)` walks anything with `hasNext()`/`next()`, or an
  `iterator()` that returns one; std `Iterator<T>` and `Iterable<T>`;
  `for ((k, v) in map)` on `HashMap`, `Map` and `SortedMap`, allocating nothing
  per entry.
- An interface method may be implemented with a narrower return type.
- A `case` runs every statement up to the next `case`, without braces.
- Nullable types `T?` with `x!`, `x ?? y` and narrowing after null checks.
- Default values and named arguments; methods as values; spreading an array into
  a variadic parameter (`sum(...xs)`).
- `match` with guards, ranges, tuples and destructuring, and as an expression;
  enum members with values; exhaustiveness warnings for `switch` on an enum.
- Struct methods; slices `s[lo..hi]`; labeled `break`/`continue`; local
  functions; self-recursive lambdas.
- Literals: raw `r"..."` and multi-line `"""..."""` strings, character literals,
  hex/binary/separated numbers, format specs in interpolation (`${x:.2f}`),
  collection and map literals that take their type from context.
- Operators: `obj[k1, k2]` with several keys, compound assignment through
  `operator[]`, comparisons from `operator<`; strings and enums are ordered.
- Generics: bounds, static members of instances, inference through interfaces,
  `Self` in classes and generic statics.
- Classes: inherited constructors, `get`/`set` properties, weak fields, traits
  with fields and statics, decorators with named arguments, field initializers.
- Built-ins: `long(x)`, `byte(x)`, `double(x)` conversions, `nameof(T)`,
  `reflect(Class)`.
- **Closures can assign the variables they capture.** A variable a lambda
  assigns is shared (a heap cell) by the function and all its lambdas, so
  `let n = 0; xs.forEach(fn(x: int) { n += x })` works and counters outlive
  their function. Variables lambdas only read are still copied when the lambda
  is made.
- **Generators**: a function or method returning `Iterator<T>` can `yield`
  values one at a time; `for` loops walk it, and a loop that stops early ends it
  (its `finally` blocks run). Each value costs a thread switch.
- **`if` and `try` as values**: `if (c) a else b` (with `else if` chains) and
  `try value catch fallback` / `catch (e: T) fallback`; branches may be blocks
  whose last line is the value.
- **`async` methods**, static ones included.
- **`private constructor`**: only the class's own code can call it.
- **Extension methods**: `fn Point.norm() -> float { ... }` adds a method to a
  class, struct, interface or built-in type (`fn int.twice()`,
  `fn Array<int>.total()`) from outside it; `self` is the receiver. A type's own
  method of the same name wins.
- `type`, `typedef`, `trait`, `match`, `export` and `uses` can name locals,
  parameters, loop and catch variables.
- **Nested types**: a class, struct, enum or interface declared inside a class
  body; `Inner` inside the class, `Outer.Inner` outside it.
- **Generic local functions**: `fn twice<T>(x: T)` inside a block (they cannot
  use the block's variables).
- A fixed array's length may be a constant expression: `int[SIZE * 2]`.
- Tuples of four: `(a, b, c, d)`, the std `Quad<A, B, C, D>`.

### Memory and runtime

- **Reference counting** for objects, closures, strings and value structs, with
  a cycle collector and weak references, replacing the old ownership pass.
- Exceptions release the locals of every frame they unwind through; panic
  recovery points.
- Left-to-right evaluation everywhere C leaves the order open; signed overflow
  wraps (`-fwrapv`).
- Run-time checks for fixed-array indexes, integer division, null access, enum
  conversions and casts.
- O(1) string length and indexing; faster formatting, allocation and hash-map
  keys; floats print in their shortest exact form.

### Compiler and tools

- `grav self-update`; `install.sh`/`uninstall.sh` for Debian and Ubuntu,
  `install.ps1`/`uninstall.ps1` for Windows (through WSL), and Docker images
  ([Installing Grav](docs/tools/install.md)).
- `grav.toml` takes `grav = "..."`: the compiler versions a project or package
  builds with, checked when building and when installing packages.
- `grav check` (with `--json` and `--types`), `grav fmt`, `grav env`.
- The package manager: `grav new | add | install | update | remove | packages`,
  `grav.lock`, packages from local folders, one copy of each package per
  program.
- Selective imports, aliases, `use`, namespaces and a std prelude.
- `grav test` builds at `-Og`; gravc's own build defaults to RelWithDebInfo.
- Editors (VS Code extension 1.15.0, Neovim, JetBrains): typed loop variables,
  red squiggles for loops over non-iterable values, inlay type hints; `yield`,
  extension methods and nested types highlighted, outlined and completed.
- The MCP server for AI assistants.

### Standard library

- Exceptions instead of panics (see above); iterators for every collection.
- Reflection: creating objects, calling methods and converting values by name,
  `ClassInfo.forName`/`all`, properties and decorators.
- `std/types` (`Integer`, `Float`, `String`, ...), `Decimal`, `JsonValue`.
- Networking: an `HttpServer` limited only by the descriptor limit, idle
  connections that give back their buffers, `TcpStream.readText`.
- HMAC-SHA256, PBKDF2 and constant-time comparison; `Path.realPath`;
  `File.setModified`; DateTime microseconds and zones; trapped signals and
  memory usage; `Term.readHidden`.
- Fixes: `Str.join` and `HashMap` lookups no longer leak or write; a `Regex` can
  be shared between threads.

### Fixes

- A line that starts with `(` or an interpolated string begins a new statement
  instead of calling the previous line's value.

## 1.6.0 (2026-11-03) and earlier

Before 1.7.0 there were no release tags; see the git history.
