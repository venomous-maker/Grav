# The grav command

`grav` is the compiler and the project tool. (Built from source and not yet
installed, it is `build/gravc`; both behave the same.)

## Commands

| Command | Does |
|---------|------|
| `grav run [file.grav] [-- args]` | builds and runs a program; arguments after `--` go to the program |
| `grav build [file.grav]` | builds a native executable next to the source |
| `grav test [file.grav] [--filter text]` | runs the `test` blocks ([Testing](../learn/19-testing.md)) |
| `grav bench [file.grav] [--filter text]` | times the `bench` blocks, built with `-O2` ([Benchmarks](../learn/19-testing.md#benchmarks)) |
| `grav check [--json] [file.grav]` | type-checks without building; prints `gravc: ok` or the errors |
| `grav lint [--fix] [file.grav]` | the warnings and lints; `--fix` applies their fixes to your own files ([Warnings and lints](#warnings-and-lints)) |
| `grav fmt [--check] [paths]` | formats source files; `grav fmt -` formats standard input |
| `grav env [--json]` | shows where the compiler, standard library and packages are |
| `grav self-update [--check] [--force] [--ref REF]` | updates a grav that `install.sh` installed ([Installing](../tools/install.md#updating-grav-self-update)) |
| `grav new <name>` | creates a project folder with `grav.toml` and `src/main.grav` |
| `grav add <repo>[@version]` | adds a dependency and installs it |
| `grav add <directory>` | uses a package from a folder on this machine, as it is (`--git` clones its commits instead) |
| `grav install` | installs the dependencies listed in `grav.lock` / `grav.toml` |
| `grav update [name]` | moves dependencies to their newest tags |
| `grav remove <name>` | removes a dependency |
| `grav packages` | lists dependencies and where they are installed |
| `grav <file.grav> [options]` | compiles; by default writes C (`file.c`) |
| `grav -` | reads source from standard input, writes C to standard output |

Inside a project (a folder with a `grav.toml`, found by searching upwards),
`run`, `build` and `test` without a file use the project's `main`.

## Options

### Output

| Option | Meaning |
|--------|---------|
| `-o <path>` | the output path (C file, assembly, or executable) |
| `--emit c\|asm\|bin\|both` | what to produce; the default is `c` (`both` is the executable and the C) |
| `-c`, `-S`, `-b` / `--bin` | short for `--emit c`, `asm`, `bin` |
| `-r`, `--run` | build and run; the program's exit status is returned |
| `--keep-c` | keep the generated `.c` when building an executable |

### Code generation

These are passed to the C compiler.

| Option | Meaning |
|--------|---------|
| `-O0` `-O1` `-O2` `-O3` `-Os` `-Ofast` `-Og` | optimisation level; default `-O2`, `-Og` for `grav test` (it compiles about three times as fast), and `-O0` when `-g` is given alone |
| `--release` | `-O3`, `NDEBUG`, and no stack-trace bookkeeping |
| `--no-trace` | no call-stack tracing: uncaught exceptions print no stack trace |
| `--native` | tune for this machine (`-march=native`) |
| `--no-lines` | leave out `#line` directives (errors in the C then point at the C file) |
| `--race` | build with ThreadSanitizer to find data races |
| `--sanitize` | build with AddressSanitizer and UBSan to find memory errors and undefined behaviour |
| `-g`, `-g3` | include debug information |
| `--cc <compiler>` | the C compiler to use (default: `$CC`, else `cc`) |

### Diagnostics

| Option | Meaning |
|--------|---------|
| `-Werror` | treat warnings as errors |
| `-v`, `--verbose` | print the C compiler command (instead of the progress lines a build prints in a terminal) |
| `-h`, `--help` | show the usage summary |

### check --json

`grav check --json file.grav` prints the diagnostics as JSON, for editors and
scripts:

```text
{"diagnostics": [
  {"file": "j.grav", "line": 2, "column": 18, "severity": "error", "stage": "type",
   "message": "cannot initialize 'x' of type int with a value of type string"}
], "errors": 1, "warnings": 0}
```

With `--types` (which implies `--json`) the report also lists the type of every
local binding in the file (`let`, `for` and `catch` variables, lambda and
function parameters, `match` bindings), written as in Grav. Editors use it for
hover and inlay hints. It is printed even when the file has type errors:

```text
{"diagnostics": [], "errors": 0, "warnings": 0, "types": [
  {"line": 3, "column": 5, "name": "names", "type": "Array<string>"},
  {"line": 4, "column": 5, "name": "s", "type": "string"}
]}
```

### Warnings and lints

Besides errors, `grav check` and the build report warnings, each with the line
it is about. A lint's warning names itself in brackets and suggests a fix:

```text
gravc: app.grav:12:5: warning: this `if` returns its own condition [needless-bool]
        if (n > 10) { return true } else { return false }
        ^
    help: write `return n > 10`
```

- **Unused variables**, and **unused imports**: an import nothing in the file
  names (a package counts with everything it brings in; a file made only of
  imports re-exports and is left alone; `import "std"` always is).
- **Probable mistakes**: `identical-operands` (`a == a`, `x - x` on integers),
  `self-assignment` (`x = x`), `unreachable-code` (after a `return`, `throw`,
  `break` or `continue`), `empty-if`, `identical-branches` (`c ? v : v`).
- **Plainer ways to say it**: `bool-comparison` (`ok == true` is `ok`),
  `needless-bool` (`c ? true : false`, an `if` returning `true` or `false`),
  `negated-comparison` (`!(a == b)` is `a != b`), `double-negation`,
  `len-zero` (`xs.size() == 0` is `xs.isEmpty()`, `s.length == 0` is `s == ""`),
  `assign-op` (`x = x + y` is `x += y`), `let-and-return`,
  `needless-range-loop` (`for (i in 0..xs.size())` that only reads `xs[i]`),
  `collapsible-else-if` (`else { if ... }`), `redundant-cast` (`n as int` on an
  int), `redundant-str` (`str(s)` on a string), `slow-char-at` (`Str.charAt(s, i)`
  is a call per byte; `s[i]` is not).
- **Dead weight**: `unused-private` (a private field nothing in its class reads,
  or a private method nothing calls; inline C that names it counts),
  `unused-parameter` (a parameter its function never uses; not for a method
  that overrides or implements another, one that `for`, `x[i]` and the like
  call by name, a function passed around as a value, an empty body, or a name
  starting with `_`), and `empty-catch` (a `catch` with nothing in it hides the
  error; a comment beside it, saying why, makes it fine).

#### grav lint --fix

`grav lint` reports the same warnings as `grav check`. With `--fix` it rewrites
your own files (never the standard library or a package) with the fixes that
keep the program meaning the same: `bool-comparison`, `needless-bool` and
`identical-branches` on a `?:`, `negated-comparison`, `double-negation`,
`len-zero`, `assign-op`, `redundant-cast`, `redundant-str`, `slow-char-at`, and
unused imports (the line goes). Brackets are added where the neighbouring code
would otherwise group the new text differently (`ok == !(a == b)` becomes
`ok == (a != b)`). It then looks again, as fixes can make others possible, until
nothing more is fixable, and prints what it changed:

```text
$ grav lint --fix app.grav
fixed app.grav:12: len-zero
fixed app.grav:15: bool-comparison
gravc: ok
```

If a round's changes ever stopped the program compiling, every file is put back
as it was before `grav lint --fix` began. Lints that need judgement (an unused
parameter, an empty `catch`, unreachable code) are reported, never changed.

Lints and unused imports are reported for the program's own files, not the
standard library's or a package's, and only once it type-checks. Write
`// allow(name)` on a line to keep one lint quiet there (`// allow(lints)` for
all), and `// allow(unused)` after an import. With `-Werror` warnings are fatal;
in `--json` output a lint's suggestion is the diagnostic's `note`.

Like every command, it reads the `grav.toml` found from the file's folder
upward, so `import "<package>"` resolves to the installed copy (or a `path`
dependency's folder) without fetching anything.

### fmt

`grav fmt` lays code out and re-indents it:

- **Spacing within a line**: one space around binary operators, `=`, `->` and
  `=>`, after `,` and `:`, between `if`/`for`/`while`/`match`/`catch` and their
  `(`, and inside a `{ ... }` kept on one line (`} else {`); none inside `( )`
  and `[ ]`, around `.` and `..`, before a call's `(`, or after a prefix
  operator. `<` and `>` in type arguments, an operator method's name
  (`operator[]=`), map literals' insides and gaps of two spaces or more (lining
  things up) are kept as written.
- **Blocks**: a `{ a; b }` written on one line becomes one statement per line
  (dropping the `;`s, except after a bare `return`, `break`, `continue` or
  `yield`). A declared function's, method's, constructor's or destructor's
  body written on one line, `fn f() -> int { return 1; }`, becomes a block too
  (its `;` kept); an empty `{}`, a lambda's body and an `if`'s `{ return x }`
  stay on their line. A `{` followed by code that runs
  past its line, and a `}` after code on an earlier block's last line, get a
  line break.
- **Indentation**: 4 spaces per level (brackets opened on one line indent the
  following lines once), continuation lines one level more after a trailing
  operator or before a leading `.`, `?`, `:`, `&&` or `||`, unless already set
  further in (lined up under an open bracket). Trailing spaces and extra blank
  lines go.

It only changes whitespace, line breaks and those `;`s: strings (`"""` ones
line by line), comments, inline C and preprocessor lines are untouched, and
formatting the result again changes nothing. With no paths it formats every
`.grav` file of the project. `--check` lists unformatted files and exits with 1
without changing anything, for use in CI.

## grav.toml

```toml
name = "inventory"            # the project's name
main = "src/main.grav"        # the entry file for run, build and test
lib = "src/lib.grav"          # for a package: the file `import "name"` loads
paths = ["src", "vendor"]     # extra folders to search for imports

[dependencies]
colors = "github.com/someone/grav-colors@v1.2.0"
http2 = { git = "https://gitlab.com/team/http2", version = "v0.3.1" }
db = { path = "../grav-db" }  # a local folder, used in place (relative to grav.toml)
```

`grav.lock` is written by `grav add`, `install` and `update`; it pins every
dependency (and their dependencies) to an exact commit. Commit it. A `path`
dependency is read from its folder each time and is not pinned.

## Environment variables

| Variable | Read by | Meaning |
|----------|---------|---------|
| `CC` | the compiler | the C compiler to use |
| `GRAV_STD` | the compiler | the standard library folder |
| `GRAV_HOME` | the compiler | a folder whose `lib/` holds the standard library |
| `GRAV_PATH` | the compiler | extra import folders, separated by `:` |
| `GRAV_PACKAGES` | the compiler | the package cache (default `~/.local/grav/packages`) |
| `GRAV_MAXPROCS` | programs | the number of worker threads for `async` tasks (default: CPU cores) |
| `GRAV_PANIC_RECOVERY` | programs | `off` disables panic recovery points |
| `GRAV_TEST_FILTER` | test runs | the same as `--filter` |
| `GRAV_TIMES` | the compiler | `1` prints how long each front-end phase takes, per generic-inference pass (stderr) |
| `GRAV_CACHE` | the compiler | the build cache folder (default `$XDG_CACHE_HOME/grav/builds`, else `~/.cache/grav/builds`) |
| `GRAV_BUILD_CACHE` | the compiler | `0` turns the build cache off |
| `GRAV_CACHE_MAX` | the compiler | how many built programs the cache keeps (default 40, the most recently used) |
| `GRAV_CACHE_LTO_ENTRIES` | the compiler | how many of GCC's link-time partitions the cache keeps (default 1024; a large program uses a few hundred) |
| `GRAV_PRUNE` | the compiler | `0` keeps every generated function in the C (by default those nothing calls are left out) |
| `GRAV_FOLD` | the compiler | `0` writes every generic instance's methods in full (by default those that compile to the same code are written once) |
| `GRAV_LTO` | the compiler | `1` always optimizes on every core (`-flto=auto`), `0` never; by default GCC does for large programs |

## The build cache

Builds keep their binaries, GCC's link-time partitions and inferred type arguments in
`~/.cache/grav/builds` (`GRAV_CACHE` moves it), so building the same program again is quick. Each
part is capped (`GRAV_CACHE_MAX`, `GRAV_CACHE_LTO_ENTRIES`). `grav cache` shows how much each
holds; `grav cache clean` empties it, or one part of it (`grav cache clean lto`). Nothing breaks
when it is emptied: the next build of each program just takes longer.

```bash
grav cache
```

```bash
grav cache clean
```

## Build speed

Most of building a large program is the C compiler optimizing the generated C.
Two things keep that short:

- **Every core.** When the C compiler is GCC and the generated C is large (over
  16 MB, as with a framework and its packages), it is optimized in parallel
  partitions (`-flto=auto`): about five times faster on eight cores. With GCC 15
  or later, the optimized partitions are also kept between builds
  (`-flto-incremental`, in the build cache): after an edit, only the code that
  changed is optimized again. `--sanitize` builds are optimized on every core
  too; debug (`-g`, `-O0`) and `--race` builds compile as before.
- **A build cache.** A program built before is reused: when its source files,
  the compiler and the options are all the same, `build`, `run` and `test` copy
  the earlier binary without compiling anything; when only the compiler's front
  end has to run again (a change that leaves the generated C the same), the C
  compiler is still skipped. Change any file the program uses and it is built
  afresh. Warnings are printed when a program is compiled, not when it is reused.
- **Less C.** A generic instance brings every method of its template, and most
  are never called: functions nothing reaches are left out of the generated C,
  which for a framework-sized program is less than half as long. Debug builds
  (and `grav test`'s `-Og`) compile about a quarter faster for it.
- **Fewer front-end passes.** Each generic type argument the compiler infers
  (`mapOf("a", 1)`) makes it compile the program again with the argument filled
  in, so a large program takes three passes. The inferences are kept with the
  build: as long as the program's declarations (fields, signatures, imports)
  are unchanged, those made in functions and methods whose text is unchanged are
  reused, and an edit inside function bodies usually compiles in one pass. If a
  build that reused them fails, it is compiled again without them.

`grav test` builds with `-Og`, which compiles about three times as fast as the
`-O2` of `run` and `build`; pass `-O1` (or `-Og`) to `run` or `build` for quicker
edit-and-run cycles on a very large program, and `--release` for the fastest code.

## Exit status

| Situation | Status |
|-----------|--------|
| the compiler found errors | 1 |
| a program returned from `main` | 0 |
| a program called `exit(n)` | `n` |
| an uncaught exception or a panic | 1 |
| `grav test` with any failed test | 1 |
| `grav fmt --check` with unformatted files | 1 |
