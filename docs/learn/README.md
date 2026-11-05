# Learning Grav

This guide teaches Grav from the ground up, one idea at a time. Each chapter
explains the *why* along with the *how*, shows small programs with their exact
output, and ends with a summary and exercises. The early chapters assume no
experience with statically typed languages; later ones build on earlier ones,
so reading in order works best.

Every complete program in these pages is compiled and run by the test suite, so
what you read is what the compiler does.

## Part 1: The basics

| # | Chapter | You will learn |
|---|---------|----------------|
| 1 | [Getting started](01-getting-started.md) | installing, your first program, building, reading errors, the `grav` tools |
| 2 | [Values, variables and types](02-values-and-types.md) | `let` and `const`, numbers, booleans, conversions, operators |
| 3 | [Strings and text](03-strings-and-text.md) | literals, raw and multi-line strings, interpolation, formatting, characters, `Str` |
| 4 | [Control flow](04-control-flow.md) | `if`, loops, ranges, `break` and labels, `switch`/`match`, `defer` |
| 5 | [Functions](05-functions.md) | parameters, defaults and named arguments, overloading, variadics, recursion |
| 6 | [Lambdas and closures](06-lambdas-and-closures.md) | function types, closures, `map`/`filter`/`reduce` |

## Part 2: Organising data

| # | Chapter | You will learn |
|---|---------|----------------|
| 7 | [Arrays and collections](07-arrays-and-collections.md) | fixed arrays, `Array`, maps, sets, collection literals, choosing a collection |
| 8 | [Structs, enums and tuples](08-structs-enums-tuples.md) | value types, enums, data enums and pattern matching, tuples, `any` |
| 9 | [Null safety](09-null-safety.md) | `null`, `??`, `?.`, nullable types `T?`, narrowing, `Option` |

## Part 3: Objects

| # | Chapter | You will learn |
|---|---------|----------------|
| 10 | [Classes](10-classes.md) | fields, constructors, methods, access control, statics, destructors |
| 11 | [Inheritance and interfaces](11-inheritance-and-interfaces.md) | subclasses, abstract classes, interfaces, traits, `is`/`as` |
| 12 | [Operators and overloading](12-operators-and-overloading.md) | method overloading, operator methods, indexing |
| 13 | [Generics](13-generics.md) | generic classes and functions, bounds, inference |

## Part 4: Writing real programs

| # | Chapter | You will learn |
|---|---------|----------------|
| 14 | [Errors and exceptions](14-errors.md) | `throw`/`try`/`catch`/`finally`, `Result`, panics and recovery |
| 15 | [Memory](15-memory.md) | reference counting, cycles and `weak`, `free` and `defer` |
| 16 | [Modules and packages](16-modules-and-packages.md) | `import`, namespaces, `use`, projects, packages |
| 17 | [C interop](17-c-interop.md) | inline C, pointers, `sizeof`, macros |
| 18 | [Concurrency](18-concurrency.md) | `async`/`await`, the worker pool, mutexes, channels, threads |
| 19 | [Testing](19-testing.md) | `test` blocks, assertions, `grav test` |
| 20 | [How Grav compiles](20-how-it-compiles.md) | the pipeline, the generated C, performance |

## After the guide

- The [language reference](../reference/README.md) has the precise rules:
  keywords, types, operators, statements, every built-in function, the
  command line, and run-time errors.
- The [standard library reference](../std/README.md) has a page for every
  module.
- [`examples/`](../../examples/README.md) has longer programs, one per topic,
  plus a few small applications.
