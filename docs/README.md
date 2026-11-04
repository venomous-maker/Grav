# The Grav Language

Grav is a statically typed, object-oriented language that compiles to C. It
reads like TypeScript, builds to a native binary through your system C compiler,
and gives you classes, interfaces, generics, lambdas, exceptions, and a
standard library — with C's speed and an escape hatch straight into C.

```grav
import "../lib/std.grav"

class Greeter {
    private name: string
    constructor(name: string) { self.name = name }
    fn greet(times: int) -> string {
        let sb = new StringBuilder()
        for (i in 0..times) { sb.append("Hello, ${self.name}! ") }
        return sb.toString()
    }
}

fn main() {
    let names = new Array<string>()
    for (n in ["Ada", "Grace"]) { names.push(n) }
    names.forEach(fn(n) => print(new Greeter(n).greet(1)))
}
```

Output:

```text
Hello, Ada! 
Hello, Grace! 
```

## Chapters

| # | Chapter | What you'll learn |
|---|---------|-------------------|
| 1 | [Getting started](01-getting-started.md) | building `gravc`, compiling and running programs, compiler flags |
| 2 | [Basics](02-basics.md) | types, variables, operators, casts, strings and interpolation |
| 3 | [Control flow](03-control-flow.md) | `if`, loops, ranges, for-each, `switch`/`match`, `defer` |
| 4 | [Functions](04-functions.md) | parameters, overloading, variadics, recursion, globals, `async` basics |
| 5 | [Data types](05-data-types.md) | enums, data enums & pattern matching, `Result`/`Option`, structs, aliases, arrays, pointers, `null`, `binary` |
| 6 | [Classes](06-classes.md) | fields, methods, access control, statics, constructors, destructors |
| 7 | [Inheritance & interfaces](07-inheritance.md) | subclasses, abstract classes, interfaces, traits, composition, RTTI |
| 8 | [Overloading & operators](08-overloading.md) | method/constructor overloading, operator methods, `toString` |
| 9 | [Generics](09-generics.md) | generic types, functions, methods, constraints |
| 10 | [Lambdas & closures](10-lambdas.md) | function types, closures, callbacks, higher-order collections |
| 11 | [Exceptions](11-exceptions.md) | `throw`, `try`/`catch`/`finally`, custom error hierarchies |
| 12 | [Memory](12-memory.md) | automatic reference counting, the cycle collector and `weak` fields, `free`, structs, strings, `defer` |
| 13 | [Modules, namespaces & C interop](13-modules.md) | `import` forms, library categories, `use`, `grav.toml`, packages (`grav add`), `namespace`, macros, inline C |
| 14 | [Standard library](14-standard-library.md) | conventions, 16 collection types, strings, formatting, math, random, files, processes, dates, terminal |
| 15 | [How Grav compiles](15-how-it-compiles.md) | the pipeline, the generated C, performance, diagnostics |
| 16 | [Concurrency](16-concurrency.md) | async tasks on a worker pool, `GRAV_MAXPROCS`, mutexes, atomics, channels, `--race` |
| 17 | [Testing](17-testing.md) | `test` blocks, `assertEq`, `grav test`, filtering |
| 18 | [Networking, JSON, threads & processes](18-networking.md) | `Json`, `Thread`/`WaitGroup`/`Semaphore`, `Command`, TCP, `HttpServer`/`HttpClient`, WebSockets |
| — | [Writing operators](operators.md) | every overloadable operator, indexing with several keys, compound assignment, derived comparisons |
| — | [Reference](reference.md) | keywords, operator precedence, built-ins, grammar summary |

Runnable programs for every chapter live in [`../examples/`](../examples/README.md).
Every complete snippet in these docs (anything with a `fn main`) is compiled and run
by [`tests/docs.sh`](../tests/docs.sh), so the examples here stay correct.
