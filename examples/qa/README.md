# QA — comprehensive multi-file programs

End-to-end programs that exercise (almost) every Grav feature at once, across
multiple files. Driven by [`tests/run.sh`](../../tests/run.sh) with a golden check.

| File            | What it shows                                                          |
| --------------- | ---------------------------------------------------------------------- |
| `runtime.grav`  | macros, a `const` + a **mutable global** shared across modules, and a top-level inline-C PRNG whose state is read back from Grav (variable sharing). |
| `creatures.grav`| enums, type aliases, a `trait` (+ `Self`), an interface, an abstract class, inheritance, **composition** (`uses`), **multiple inheritance** (`extends A, B`), and a **generic** class. |
| `main.grav`     | imports the modules and ties everything together: variadics, exceptions, async/await, pointers, `null`/`??`/`?.`, casts, string interpolation, command-line args, and inline-C value sharing. |

```bash
grav examples/qa/main.grav --run -- alpha beta
```
