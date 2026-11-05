# 19. Testing

A test is a small program that checks that another piece of code does what it
should. Tests catch mistakes when you write code, and much more importantly, when
you change it months later. Grav has testing built in: no framework to install.

In this chapter you will learn:

- how to write `test` blocks with `assert` and `assertEq`,
- how to run them with `grav test`, and run just some of them,
- what happens when a test fails, throws or panics,
- how to write tests that are worth having.

## Writing tests

A `test "name" { ... }` block at the top level of a file is a test. It can sit
right next to the code it checks:

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

Building or running the file normally (`grav run`, `grav build`) leaves the
tests out completely. `grav test` does the opposite: it compiles the tests and
whatever they use, and runs each one:

```bash
grav test slug.grav
```

```text
running 3 tests
  ok    lower-cases and joins words
  ok    trims the ends
  ok    assert with a message
result: 3 passed, 0 failed
```

## Assertions

| Assertion | Fails when |
|-----------|------------|
| `assert(condition)` | `condition` is false |
| `assert(condition, "message")` | the same, and shows the message |
| `assertEq(actual, expected)` | the values differ (compared with `==`) |
| `assertEq(actual, expected, "message")` | the same, with a message |

`assertEq` shows both values when it fails, which usually tells you what went
wrong without any debugging:

```text
  FAIL  totals
        invoice total: expected 120 but got 110 (shop.grav:42)
```

Put the value you computed first and the value you expect second. Strings,
structs and arrays are compared by value.

## When a test fails

A test fails when an assertion fails, when it calls `panic`, or when an exception
escapes from it. Each failure is reported with its message and location, and the
runner goes on with the next test, so one broken test does not hide the others:

```text
  FAIL  checks out a cart
        uncaught exception: StateError: cart is empty
```

`grav test` exits with status 1 when any test failed, and 0 when all passed, so
it can be used as is in continuous integration.

To check that code *does* throw, catch the exception in the test:

```grav
import "std"

fn withdraw(balance: int, amount: int) -> int {
    if (amount > balance) { throw new ValueError("insufficient funds") }
    return balance - amount
}

test "refuses to overdraw" {
    let msg = ""
    try {
        withdraw(10, 50)
    } catch (e: ValueError) {
        msg = e.message()
    }
    assertEq(msg, "insufficient funds")
}

fn main() {
    print(withdraw(100, 30))
}
```

Output:

```text
70
```

## Running some of the tests

`--filter` runs only the tests whose name contains the given text:

```bash
grav test shop.grav --filter totals
```

In a project with a `grav.toml`, plain `grav test` tests the project's `main`
file and everything it imports. Test files can also live in a `tests/` folder and
import the code they test.

Tests run one after another on the main thread. A test may start `async` tasks
and `await` them.

## Writing good tests

- **Test behaviour, not implementation.** Check what a function returns or
  changes, not how it does it, so tests survive refactoring.
- **One idea per test**, with a name that says what should happen:
  `"refuses to overdraw"` tells you what broke when it fails.
- **Cover the edges:** empty input, one element, the largest value, invalid
  input. Bugs live at boundaries.
- **Keep tests fast and independent.** A test should not depend on another
  test having run first, or on files left over from a previous run.
- **When you fix a bug, add a test** that would have caught it.

## Summary

- `test "name" { ... }` declares a test; normal builds ignore tests.
- `assert(cond[, msg])` and `assertEq(actual, expected[, msg])` check results.
- `grav test file.grav [--filter text]` runs them; failures, exceptions and panics
  are reported per test, and the exit status tells CI the outcome.

## Exercises

1. Write `fn isPalindrome(s: string) -> bool` and tests for `""`, `"a"`,
   `"abba"`, `"abc"` and `"Abba"` (decide what that last one should be first).
2. Write a test that checks that `Array.pop()` on an empty array throws a
   `StateError`.
3. Take a function from an earlier exercise and write tests for its edge cases.
   Did you find a bug?
4. Write a failing test on purpose and read the report. Then run with
   `--filter` so that only the passing tests run.

Next: [How Grav compiles →](20-how-it-compiles.md)
