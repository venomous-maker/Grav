# 1. Getting started

Grav is a statically typed language that compiles to C. You write code that
reads a lot like TypeScript or Kotlin. The `grav` compiler checks it, turns it
into C, and hands that C to your system's C compiler, which produces an ordinary
native program. There is no virtual machine and no interpreter at run time.

In this chapter you will:

- install the compiler,
- write, run and build your first program,
- learn the shape of a Grav source file,
- read a compiler error,
- meet the tools that come with the compiler.

## Installing

The compiler is a C++20 program built with CMake. Programs it compiles are
finished by your C compiler (`cc`, or whatever `$CC` names), so you need both a
C++20 compiler and a C compiler.

```bash
cmake -S . -B build
cmake --build build -j
cmake --install build --prefix ~/.local
```

The last step installs `~/.local/bin/grav` and the standard library in
`~/.local/share/grav/lib`. Make sure `~/.local/bin` is on your `PATH`. You can
also run the compiler straight from the build tree as `build/gravc`; it then uses
the `lib/` folder of the checkout it was built from.

Check that everything is in place:

```bash
grav env
```

`grav env` prints where the compiler, the standard library and installed packages
are. If the standard library is somewhere unusual, point `GRAV_STD` at it.

## Your first program

Create a file called `hello.grav`:

```grav
fn main() {
    print("Hello, world!")
}
```

Output:

```text
Hello, world!
```

Run it:

```bash
grav run hello.grav
```

Line by line:

- `fn main()` declares a function called `main`. Every program starts there. It
  takes no parameters and returns nothing.
- The body is between `{` and `}`.
- `print(...)` is a built-in function: it writes one value and a line break to
  standard output.
- `"Hello, world!"` is a string literal.

Statements end at the end of the line. You do not need semicolons, though you can
use `;` to put two statements on one line.

## Building a program

`grav run` builds the program in a temporary place and runs it. To keep the
program, build it:

```bash
grav build hello.grav     # writes ./hello, a native executable
./hello
```

The executable has no dependencies on Grav: you can copy it to another machine
with the same operating system and run it there.

Arguments for your program go after `--`:

```bash
grav run hello.grav -- first second
```

Inside the program, `argc()` is the number of arguments and `argv(i)` is
argument `i` (`argv(0)` is the program's own name):

```grav
fn main() {
    print("I was given ${argc() - 1} argument(s)")
    for (i in 1..argc()) {
        print(argv(i))
    }
}
```

Output:

```text
I was given 0 argument(s)
```

The `${...}` inside the string is *interpolation*: the value of the expression
is put into the text. `for (i in 1..argc())` counts from 1 up to, but not
including, `argc()`. Both are covered properly in chapters 3 and 4.

To end the program with a status code, call `exit(code)`. Zero means success.

## What happens when you compile

```text
hello.grav ──grav──▶ hello.c ──cc──▶ hello (native executable)
```

1. **Checking.** The compiler reads your file and everything it imports, and
   checks that every name exists and every value has the right type. Most mistakes
   are caught here, before anything runs.
2. **Generating C.** It writes a C file that does what your program says,
   including the bookkeeping the language does for you (freeing memory,
   checking array bounds, tracking the call stack for error reports).
3. **Compiling C.** It runs the C compiler (`-O2` by default), which produces the
   executable.

You can stop after step 2 and read the C that Grav writes:

```bash
grav hello.grav          # writes hello.c
```

[How Grav compiles](20-how-it-compiles.md) walks through that output.

## The shape of a source file

A Grav file holds declarations: functions, classes, structs, enums, interfaces,
global variables, imports and tests. The order does not matter: a function can
call another that is declared further down.

```grav
import "std"                    // the standard library

const GREETING: string = "Hello"   // a global constant (globals need a type)

fn greet(name: string) -> string {
    return "${GREETING}, ${name}!"
}

fn main() {
    let names: Array<string> = ["Ada", "Grace"]
    for (name in names) {
        print(greet(name))
    }
}
```

Output:

```text
Hello, Ada!
Hello, Grace!
```

A few rules worth knowing early:

- **Comments** are `// to the end of the line` and `/* anything in between */`.
- **Blocks** use braces, and conditions go in parentheses: `if (x > 0) { ... }`.
- **Statements** are separated by line breaks or `;`. Use `;` when a new
  statement on the same line starts with `*`, `&` or `-`, which could otherwise
  continue the previous one.
- **Names** are case-sensitive. By convention, types are `UpperCamelCase` and
  everything else is `lowerCamelCase`.
- **`import "std"`** makes the standard library available: collections like
  `Array`, text helpers like `Str`, files, JSON, networking and more. Programs
  that only use built-in features do not need it.

## Reading errors

When something is wrong, the compiler names the file, line and column, says what
kind of problem it is, and points at it:

```text
gravc: app.grav:2:17: type error: operator '+' requires numeric operands, but got string and int
        let n = "a" + 1
                    ^
```

Read it as: in `app.grav`, line 2, column 17, the `+` was given a string and an
int, and `+` only adds numbers (or joins two strings). The compiler keeps going
after the first error and reports everything it finds, then stops before building
anything.

**Warnings** point at things that are probably mistakes but do not stop the build.
The most common is an unused variable. `-Werror` turns warnings into errors,
which is useful in automated builds.

Problems that can only be found while the program runs, like an array index out
of range, stop the program with a message and the call stack:

```text
grav: uncaught exception: IndexError: array index 5 out of range (length 3)
  stack trace (most recent call last):
    at main (app.grav:5)
```

(That is with `import "std"`, which turns these faults into exceptions you can
catch. Without the standard library the program stops with a `panic:` message.)

[Errors and exceptions](14-errors.md) explains how to catch these.

## The tools

The `grav` command does more than compile:

| Command | What it does |
|---------|--------------|
| `grav run app.grav` | build and run |
| `grav build app.grav` | build an executable |
| `grav test app.grav` | run the file's `test` blocks ([Testing](19-testing.md)) |
| `grav check app.grav` | check for errors without building |
| `grav fmt` | re-indent every `.grav` file in the project |
| `grav new app` | start a project ([Modules and packages](16-modules-and-packages.md)) |
| `grav add github.com/owner/repo` | add a package |
| `grav env` | show where things are installed |

Every command and flag is listed in the [command-line reference](../reference/cli.md).
For editor support (highlighting, completion, hover, go to definition) see
[Editors](../tools/editors.md).

## Summary

- A program is a `.grav` file with a `fn main()`.
- `grav run` builds and runs it; `grav build` leaves a native executable.
- Grav checks your whole program before running anything, then compiles through C.
- `import "std"` brings in the standard library.
- Errors name the file, line and column, with a caret under the problem.

## Exercises

1. Change `hello.grav` to print two lines.
2. Write a program that prints how many arguments it was given and the last one
   (when there is at least one). Try it with `grav run file.grav -- a b c`.
3. Put `let n = "a" + 1` inside `main` and read the error. Then remove the line
   and add `let unused = 5`. What does the compiler say now, and does it still build?
4. Run `grav hello.grav` and open `hello.c`. Find your `main`.

Next: [Values, variables and types →](02-values-and-types.md)
