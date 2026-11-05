# Grav documentation

Grav is a statically typed language that compiles to C. It reads like TypeScript
or Kotlin, checks your whole program before it runs, and builds a native
executable through your system's C compiler: no virtual machine, no garbage
collector pauses, and C libraries one `%{ ... %}` away.

```grav
import "std"

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
    let names: Array<string> = ["Ada", "Grace"]
    names.forEach(fn(n) => print(new Greeter(n).greet(1)))
}
```

Output:

```text
Hello, Ada! 
Hello, Grace! 
```

## What Grav gives you

- **Static types with inference**: mistakes are caught at compile time, but you
  rarely have to write a type down.
- **Classes, interfaces, traits and generics**, with virtual dispatch only where
  it is needed and generics specialised per type.
- **Data enums and pattern matching**, checked for exhaustiveness.
- **Null safety**: `int`, `string` and structs are never null; `T?`, `??`, `?.`,
  `x!` and narrowing handle values that may be missing.
- **Lambdas and closures**, with `map`, `filter` and `reduce` on collections.
- **Exceptions**, `Result`, and panics with recovery for servers and threads.
- **Automatic reference counting** with a cycle collector: memory is freed at a
  predictable moment, with no pauses.
- **Concurrency**: `async`/`await` on a worker pool, threads, mutexes, atomics and channels.
- **C interop**: inline C, pointers and C types, for any C library.
- **A standard library** with collections, text, JSON, files, processes, dates,
  TCP, HTTP and WebSockets.
- **Tools**: tests built into the language, a formatter, a package manager, and
  editor support for VS Code, Neovim and JetBrains.

## How these docs are organised

| Section | Read it when |
|---------|--------------|
| [**Learn Grav**](learn/README.md) | you want to learn the language, from your first program to concurrency, one chapter at a time, with exercises |
| [**Language reference**](reference/README.md) | you need the precise rules: syntax, keywords, types, operators, statements, built-in functions, the command line |
| [**Standard library**](std/README.md) | you want to know what a module offers: one page per module, with examples and its full API |
| [**Tools**](tools/editors.md) | you are setting up an editor, or the [MCP server](tools/mcp.md) for AI assistants |

New to Grav? Start with [Getting started](learn/01-getting-started.md).

## Quick links

- [Install and run your first program](learn/01-getting-started.md)
- [Every built-in function](reference/builtins.md)
- [Every `grav` command and option](reference/cli.md)
- [Operator precedence](reference/operators.md#precedence)
- [Strings](std/text/string.md), [Array](std/collections/array.md), [HashMap](std/collections/hashmap.md), [JSON](std/data/json.md), [Files](std/io/fs.md), [HTTP](std/net/http.md)
- [Longer example programs](../examples/README.md)

Every complete program in these pages (anything with a `fn main`) is compiled
and run by [`tests/docs.sh`](../tests/docs.sh), and its output is checked against
the page, so the examples stay correct.
