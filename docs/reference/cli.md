# The grav command

`grav` is the compiler and the project tool. (Built from source and not yet
installed, it is `build/gravc`; both behave the same.)

## Commands

| Command | Does |
|---------|------|
| `grav run [file.grav] [-- args]` | builds and runs a program; arguments after `--` go to the program |
| `grav build [file.grav]` | builds a native executable next to the source |
| `grav test [file.grav] [--filter text]` | runs the `test` blocks ([Testing](../learn/19-testing.md)) |
| `grav check [--json] [file.grav]` | type-checks without building; prints `gravc: ok` or the errors |
| `grav fmt [--check] [paths]` | formats source files; `grav fmt -` formats standard input |
| `grav env [--json]` | shows where the compiler, standard library and packages are |
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
| `-O0` `-O1` `-O2` `-O3` `-Os` `-Ofast` | optimisation level; default `-O2` (`-O0` when `-g` is given alone) |
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
| `-v`, `--verbose` | print the C compiler command |
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

Like every command, it reads the `grav.toml` found from the file's folder
upward, so `import "<package>"` resolves to the installed copy (or a `path`
dependency's folder) without fetching anything.

### fmt

`grav fmt` re-indents code with 4 spaces per level (brackets opened on one line
indent the following lines once), indents continuation lines after a trailing
operator, removes trailing spaces and extra blank lines, and otherwise leaves
lines alone. Strings, comments and inline C are untouched. With no paths it
formats every `.grav` file of the project. `--check` lists unformatted files and
exits with 1 without changing anything, for use in CI.

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

## Exit status

| Situation | Status |
|-----------|--------|
| the compiler found errors | 1 |
| a program returned from `main` | 0 |
| a program called `exit(n)` | `n` |
| an uncaught exception or a panic | 1 |
| `grav test` with any failed test | 1 |
| `grav fmt --check` with unformatted files | 1 |
