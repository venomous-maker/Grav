# 1. Getting started

## Building the compiler

Grav's compiler, `gravc`, is a C++20 program built with CMake. Generated programs
are compiled by your system C compiler (`cc`, or `$CC`).

```bash
cmake -S . -B build
cmake --build build -j
cmake --install build --prefix ~/.local   # optional: installs bin/grav and the standard library
```

The installed `grav` finds the standard library in `<prefix>/share/grav/lib`. A
`gravc` run from the build tree uses the `lib/` directory it was built from; set
`GRAV_STD=/path/to/lib` to point either one somewhere else.

## Hello, world

```grav
fn main() {
    print("Hello, world!")
}
```

Output:

```text
Hello, world!
```

Save it as `hello.grav` and run it in one step:

```bash
grav run hello.grav
```

`main` is the entry point. It takes no parameters (use `argc()`/`argv(i)` for
command-line arguments) and returns nothing; call `exit(code)` to set an exit status.

## What the compiler can produce

```bash
grav run hello.grav -- a b c        # build and run, passing arguments after `--`
grav build hello.grav               # build the native binary ./hello
grav test hello.grav                # run the file's `test` blocks (chapter 17)
grav hello.grav                     # writes hello.c (the default: emit C)
grav hello.grav --emit bin          # builds the native binary ./hello
grav hello.grav --emit both         # binary + hello.c
grav hello.grav -S                  # assembly (hello.s)
grav hello.grav --run -- a b c      # build, run, pass arguments after `--`
cat hello.grav | grav -             # read stdin, print the C to stdout
```

## Compiler flags

| Flag | Meaning |
|------|---------|
| `-o <path>` | output path (C file, assembly, or executable) |
| `--emit c\|asm\|bin\|both` | what to produce (default `c`) |
| `-c`, `-S`, `-b`/`--bin` | shorthands for `--emit c` / `asm` / `bin` |
| `-r`, `--run` | build and run; the program's exit status is returned |
| `--keep-c` | keep the intermediate `.c` next to the source |
| `-O0`…`-O3`, `-Os`, `-Ofast` | optimization level (default `-O2`; `-O0` when only `-g` is given) |
| `--release` | `-O3`, `NDEBUG`, and no stack-trace bookkeeping |
| `--no-trace` | drop call-stack tracing (uncaught exceptions print no trace) |
| `--native` | tune for this machine (`-march=native`) |
| `--race` | build with ThreadSanitizer to find data races between tasks |
| `--sanitize` | build with AddressSanitizer + UBSan to find memory errors |
| `-g`, `-g3` | debug information |
| `--cc <compiler>` | C compiler to use (default `$CC`, else `cc`) |
| `-Werror` | treat warnings (such as unused variables) as errors |
| `-v` | print the C compiler command |
| `--no-lines` | omit `#line` directives (by default C compiler errors, debuggers and sanitizers point at the `.grav` source) |
| `--filter <text>` | with `grav test`: run only tests whose name contains `text` |

Inside a project with a `grav.toml` (chapter 13), `grav run`, `grav build` and
`grav test` without a file use the project's `main`.

## Source file basics

- Statements are separated by newlines or `;`. Use `;` when a statement starts with
  `*`, `&`, or `-` right after another statement on the same line.
- Comments are `// line` and `/* block */`.
- Blocks use braces; conditions use parentheses: `if (x > 0) { ... }`.
- Errors are reported with the file, line, column, and a caret:

```text
gravc: app.grav:4:13: type error: operator '+' requires numeric operands, but got string and int
        let n = "a" + 1
                ^
```

## Editor support

Syntax highlighting, go-to-definition, and a language server live in
[`../editors/`](../editors/) for VS Code, JetBrains IDEs, and Neovim.

Next: [Basics →](02-basics.md)
