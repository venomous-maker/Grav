# 17. C interop

Grav compiles to C, so it can call C directly. That gives you every C library
ever written (system calls, compression, databases, graphics) without wrappers or
a foreign-function layer. The standard library itself is written this way. This
chapter covers the low-level tools: inline C, pointers, `sizeof` and macros.

These are sharp tools. Code inside `%{ ... %}` is not checked by Grav, and a
wrong pointer can crash the program. Keep it small and wrap it in a function
with a safe Grav interface.

In this chapter you will learn:

- inline C blocks: as declarations, statements and expressions,
- how Grav types look in C,
- pointers: `&x`, `*p`, `p->field`,
- `sizeof`,
- `#define` macros.

## Inline C

`%{ ... %}` puts C code straight into the generated program. It can appear in
three places:

```grav
%{
#include <ctype.h>
static int c_count_upper(const char* s) {
    int n = 0;
    for (; *s; s++) if (isupper((unsigned char)*s)) n++;
    return n;
}
%}

fn countUpper(s: string) -> int {
    return %{ c_count_upper(s) %} as int
}

fn main() {
    print(countUpper("Hello World From Grav"))
    let total = 0
    %{ for (int i = 1; i <= 4; i++) total += i * i; %};
    print(total)
    let answer: int = %{ 6 * 7 %}
    print(answer)
}
```

Output:

```text
4
30
42
```

- **At the top level**, a block can contain C declarations: helper functions,
  globals, types, and `#include`s (which are moved to the top of the C file).
- **As an expression**, it needs a type: `%{ ... %} as int`, or a typed `let`.
- **As a statement**, end it with `;`.
- Grav's local variables and parameters are visible inside, by the same names,
  and C code can assign to them (`total` above).

Linking a C library is a matter of passing the flag to the C compiler, for
example with an environment variable or `--cc "cc -lz"`.

## Grav types in C

| Grav | C |
|------|---|
| `int`, `long`, `byte` | `int`, `long long`, `unsigned char` |
| `uint`, `ulong` | `unsigned int`, `unsigned long long` |
| `float` / `double`, `float32` | `double`, `float` |
| `bool` | `bool` |
| `string` | `const char*` (UTF-8, zero-terminated) |
| a class `C` | a pointer to a struct whose fields have the same names |
| a struct `S` | the struct itself, by value |
| `binary` | `GravBytes { data, len }` |
| `T*` | `T*` |

Two rules matter when C code makes values for Grav:

- **Strings** that Grav will own must be allocated with `grav_str_new(len)` (or
  `grav_str_alloc(bytes)`) so reference counting works. Strings from C libraries,
  `malloc` or static buffers are treated as permanent and never freed by Grav.
- **Objects** should be created on the Grav side with `new`, and passed to C.

The standard library's sources (`lib/`) are the best examples of wrapping C
APIs: [`File`](../std/io/fs.md) wraps `stdio`, [`Process`](../std/sys/process.md)
wraps `fork` and `exec`, [`TcpSocket`](../std/net/tcp.md) wraps sockets.

## Pointers

`T*` is a pointer to a `T`. `&x` takes the address of a variable, `*p` reads or
writes what `p` points to, and `p->field` reaches a struct field through a pointer:

```grav
struct Counter { n: int }

fn bump(c: Counter*) { c->n = c->n + 1 }
fn double(p: int*) { *p = *p * 2; }

fn main() {
    let c = Counter { n: 41 }
    bump(&c);
    let x = 21
    double(&x);
    print("${c.n} ${x}")
}
```

Output:

```text
42 42
```

In plain Grav code you rarely need pointers: objects are already references, and
a struct method can change its struct. They are for talking to C, and for
updating a struct or number in place from another function.

Notice the `;` after `bump(&c)`. A statement that *starts* with `*`, `&` or `-`
could be read as continuing the previous line, so the previous statement must be
ended with `;`.

Pointers are not checked: a pointer to a variable that has gone out of scope, or
to freed memory, is a bug that `--sanitize` will find. Memory you get from
`malloc` is yours to `free(p)`.

## sizeof

`sizeof(T)` is the size of a type in bytes, as in C:

```grav
struct Pixel { r: byte  g: byte  b: byte }

fn main() {
    print("${sizeof(int)} ${sizeof(long)} ${sizeof(float)} ${sizeof(Pixel)}")
}
```

Output:

```text
4 8 8 3
```

## Macros

`#define` defines a textual macro, replaced before the program is parsed, either a
constant or a function-like macro:

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

Macros do no type checking and know nothing about scopes. Prefer `const` for
constants and functions for computations; use macros only when you really need
text substitution.

## Summary

- `%{ ... %}` embeds C: declarations at the top level, `%{ expr %} as T` as an
  expression, `%{ stmts %};` as a statement. Grav locals are visible inside.
- Grav types map directly to C types; strings for Grav must come from `grav_str_new`.
- `T*`, `&x`, `*p` and `p->field` work as in C, unchecked.
- `sizeof(T)` gives a size in bytes; `#define` makes macros.

## Exercises

1. Write `fn isLeapYear(y: int) -> bool` in plain Grav, then again with the test
   inside a `%{ ... %}` expression.
2. Call the C function `getpid()` (from `<unistd.h>`) and print the result.
3. Write `fn swap(a: int*, b: int*)` and use it to swap two variables.
4. Wrap C's `toupper` in a Grav function that upper-cases one character code.

Next: [Concurrency →](18-concurrency.md)
