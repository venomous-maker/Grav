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

### Language

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

### Fixes

- A line that starts with `(` or an interpolated string begins a new statement
  instead of calling the previous line's value.

### Editors

- VS Code extension 1.15.0: `yield` highlighted everywhere; hover and snippets
  for generators, `if`/`try` values, async methods and private constructors.

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
- Editors (VS Code 1.14.3, Neovim, JetBrains): typed loop variables, red
  squiggles for loops over non-iterable values, and inlay type hints.
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

## 1.6.0 (2026-11-03) and earlier

Before 1.7.0 there were no release tags; see the git history.
