# Built-in functions

These functions are part of the language: they need no import. Everything else
is in the [standard library](../std/README.md).

| Function | Gives | Summary |
|----------|-------|---------|
| [`print(value)`](#print) | | writes a value and a line break |
| [`println(value)`](#print) | | the same as `print` |
| [`str(value)`](#str) | `string` | a value as text |
| [`input()`](#input) | `string` | one line from standard input |
| [`argc()`](#argc-and-argv) | `int` | the number of command-line arguments, program name included |
| [`argv(i)`](#argc-and-argv) | `string` | argument `i` |
| [`env(name)`](#env-and-cwd) | `string` | an environment variable, or `""` |
| [`cwd()`](#env-and-cwd) | `string` | the current directory |
| [`exit(code)`](#exit) | | ends the program with a status code |
| [`panic(message)`](#panic) | | stops the program: a bug was found |
| [`assert(cond[, message])`](#assert-and-asserteq) | | panics (or fails a test) when `cond` is false |
| [`assertEq(actual, expected[, message])`](#assert-and-asserteq) | | the same, when the values differ |
| [`typename(value)`](#typename-and-isinstance) | `string` | the class name of an object, or the type held by an `any` |
| [`isInstance(object, Type)`](#typename-and-isinstance) | `bool` | the same as `object is Type` |
| [`int(x)`, `long(x)`, `float(x)`, `byte(x)`, ...](#conversions) | | conversions |
| [`sizeof(T)`](#sizeof) | `int` | a type's size in bytes |
| [`free(x)`](#free) | | destroys an object now, or frees a pointer |

## print

```text
print(value)
println(value)
```

Writes `value` and a line break to standard output. Any value that can be shown
works: numbers, booleans, strings, enums, structs, fixed arrays, objects (through
their `toString()` method, or their class name), collections, `any`, `T?` and
`null`. Exactly one argument is taken; build a string to print several values.
To print without a line break, use `printRaw` from the standard library; for
standard error, `IO.eprint`.

```grav
struct Point { x: int  y: int }

fn main() {
    print(42)
    print(Point { x: 1, y: 2 })
    print([1.5, 2.0])
    print("${1 + 1} things")
}
```

Output:

```text
42
Point { x: 1, y: 2 }
[1.5, 2]
2 things
```

## str

```text
str(value) -> string
```

The text `print` would write, as a string. Floats use the shortest form that
reads back as the same number.

```grav
fn main() {
    let s = str(3) + "/" + str(true) + "/" + str(0.1 + 0.2)
    print(s)
}
```

Output:

```text
3/true/0.30000000000000004
```

## input

```text
input() -> string
```

Reads one line from standard input, without the line break. At the end of input
it returns `""`. `IO.readLine()` is the same; `IO.readLines()` reads everything.

## argc and argv

```text
argc() -> int
argv(i: int) -> string
```

`argc()` counts the command-line arguments including the program name, which is
`argv(0)`. With `grav run file.grav -- a b`, `argc()` is 3 and `argv(1)` is `"a"`.

```grav
fn main() {
    for (i in 1..argc()) {
        print("argument ${i}: ${argv(i)}")
    }
    print("done")
}
```

Output:

```text
done
```

## env and cwd

```text
env(name: string) -> string
cwd() -> string
```

`env` returns an environment variable's value, or `""` when it is not set.
`cwd` returns the current working directory. `OS` in the standard library has
more ([`OS`](../std/sys/os.md)).

## exit

```text
exit(code: int)
```

Ends the program immediately with the status `code` (0 means success). Deferred
code and `finally` blocks do not run.

## panic

```text
panic(message: string)
```

Stops the program with `panic: message` and the call stack. Use it for
situations that mean the program itself is wrong. A `catch` does not catch a
panic; a recovery point (`Panic.recover`, an HTTP handler, a thread) does. See
[Errors](../learn/14-errors.md#panics).

## assert and assertEq

```text
assert(condition: bool[, message: string])
assertEq(actual, expected[, message: string])
```

`assert` fails when `condition` is false, reporting the message and the file and
line. `assertEq` fails when `actual != expected` and reports both values. Inside
a `test` block, a failure fails that test; elsewhere, it panics.

```text
assertion failed: math broke (app.grav:2)
```

## typename and isInstance

```text
typename(value) -> string
isInstance(object, Type) -> bool
```

`typename` gives the run-time class name of an object, or the name of the type
an `any` holds. `isInstance(obj, Cat)` is the same as `obj is Cat`.

```grav
class Animal { }
class Cat extends Animal { }

fn main() {
    let a: Animal = new Cat()
    print(typename(a))
    print(isInstance(a, Animal))
    let v: any = 3.5
    print(typename(v))
}
```

Output:

```text
Cat
true
float
```

## Conversions

```text
int(x)  long(x)  uint(x)  byte(x)  int8(x) ... uint64(x)  float(x)  double(x)  float32(x)  bool(x)  binary(x)
```

Every built-in type name can be called like a function to convert, with the same
rules as `x as T` and `(T)x`. When a function or variable of that name is in
scope (say, your own `fn double(x: int)`), the name means yours. See
[Types](types.md#conversions).

```grav
fn main() {
    print(long(3.9))
    print(byte(300))
    print(double(7) / 2)
    print(uint(-1))
}
```

Output:

```text
3
44
3.5
4294967295
```

## sizeof

```text
sizeof(Type) -> int
sizeof(expression) -> int
```

The size in bytes, as C's `sizeof`. For a class, it is the size of the object,
not of the reference.

## free

```text
free(object)
free(pointer)
```

On an object: runs its destructor (and its parents') now and clears its fields;
the memory is released when the last reference goes, so other references never
dangle. On a pointer: C's `free`. On a `Future`: releases a finished task's
record. Objects are freed automatically, so `free` is only needed to release a
resource at a precise moment. See [Memory](../learn/15-memory.md#closing-things-on-time-free-and-defer).

## Library-internal built-ins

These are used inside the standard library; ordinary programs do not need them:

| Built-in | Purpose |
|----------|---------|
| `__retain(x)`, `__release(x)` | adjust the reference count of a value kept in raw memory |
| `__visit(x)` | report a reference held in raw memory to the cycle collector |
| `__repr(x)` | text of a value as shown inside a collection (strings quoted) |
| `__show(x)` | text of a data-enum field |
| `__fmt(x, spec)` | what `${x:spec}` becomes |
| `__unwrap(x)` | what `x!` becomes |
