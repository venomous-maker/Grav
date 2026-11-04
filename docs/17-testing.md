# 17. Testing

## Test blocks

A `test "name" { ... }` block at the top level of a file is a unit test. Normal
builds (`grav run`, `grav build`) leave tests out entirely; `grav test` compiles
only the tests, plus everything they use, and runs each one.

```grav
import "std"

fn slugify(s: string) -> string {
    return Str.join(Str.split(Str.lower(Str.trim(s)), " "), "-")
}

fn main() {
    print(slugify("  Hello Grav World "))
}

test "lower-cases and joins words" {
    assertEq(slugify("Hello World"), "hello-world")
}

test "trims the ends" {
    assertEq(slugify("  padded  "), "padded")
}

test "assert with a message" {
    let xs = new Array<int>()
    xs.push(1)
    assert(xs.size() == 1, "push adds one element")
}
```

Output:

```text
hello-grav-world
```

`grav test slug.grav` runs the three tests instead:

```text
running 3 tests
  ok    lower-cases and joins words
  ok    trims the ends
  ok    assert with a message
result: 3 passed, 0 failed
```

## Assertions

- `assert(cond[, message])` fails the test when `cond` is false. The report includes
  the file and line.
- `assertEq(actual, expected[, message])` compares the values with `==` (value
  equality for strings, structs and arrays) and on failure prints both:

```text
  FAIL  totals
        invoice total: expected 120 but got 110 (shop.grav:42)
```

## Failures are isolated

A test fails when an assertion fails, when it calls `panic`, or when an exception
escapes it (a bounds-checked `Array` access out of range throws `IndexError`,
for example). The runner reports the failure and moves on to the next test. `grav test` exits with
status 1 if any test failed, so it works in CI as is.

```text
  FAIL  checks out a cart
        uncaught exception: StateError: cart is empty
```

## Running some of the tests

`--filter text` runs only the tests whose names contain `text`:

```bash
grav test shop.grav --filter totals
```

Tests run on the main thread one after another; a test may start `async` tasks and
`await` them. In a project with a `grav.toml`, `grav test` with no file tests the
project's `main` file and everything it imports.
