# Grav examples

Every program here is runnable and deterministic, so `tests/run.sh` checks each
one's output against a golden file in `tests/expected/`.

```bash
grav examples/01_basics.grav --run        # or: ./build/gravc <file> --run
```

## Language tour (one area per file)

| File | Covers |
|------|--------|
| [01_basics.grav](01_basics.grav) | primitives and wide numeric types, `let`/`const`, inference, arithmetic/bitwise/shift/compound operators, `**`, ternary, casts, strings, escapes, interpolation |
| [02_control_flow.grav](02_control_flow.grav) | `if`/`else if`, `while`, `do/while`, C `for`, ranges with `..`/`..=`/`step`, for-each with index, `switch`/`match` on ints/strings/enums, `break`/`continue`, `defer` |
| [03_functions.grav](03_functions.grav) | parameters/returns, recursion, overloading, variadics, generic and bounded functions, globals, `async`/`await` |
| [04_data_types.grav](04_data_types.grav) | enums, structs (value semantics), aliases/`typedef`, fixed arrays, pointers, `null`/`??`/`?.`, `sizeof`, `binary` |
| [05_classes.grav](05_classes.grav) | fields/methods, access modifiers, `readonly`, generated accessors, statics, constructor overloads, destructors, `toString`, `final` |
| [06_inheritance.grav](06_inheritance.grav) | virtual dispatch, abstract classes, `super`, interfaces, traits + `Self`, `uses` composition, multiple inheritance, RTTI (`typename`, `isInstance`, `is`, `as`) |
| [07_overloading.grav](07_overloading.grav) | method/static/constructor overloading, operator methods (`+ - * / % == < <= > >= [] []=`) |
| [08_generics.grav](08_generics.grav) | generic structs/classes/interfaces, generic inheritance, bounds, generic aliases, generic functions and methods |
| [09_lambdas.grav](09_lambdas.grav) | function types, lambdas, closures, function references, callbacks, higher-order collection methods |
| [10_exceptions.grav](10_exceptions.grav) | `throw`, multiple `catch` clauses, catch by base class, `finally` on every path, re-throw, nesting |
| [11_memory.grav](11_memory.grav) | `new`/`free`, destructor chains, `defer` cleanup, automatic string/closure freeing, `StringBuilder` |
| [12_collections.grav](12_collections.grav) | `Array`, `List`, `Stack`, `Queue`, `Set`, `Map`, `HashMap` |
| [13_stdlib.grav](13_stdlib.grav) | `Str`, `Format`, `Math`, `Random`, `DateTime`, `Time`/`Stopwatch`, `Path`, `File`, `Process`, `OS`, `IO`, `Term` |
| [14_input_args.grav](14_input_args.grav) | `argc`/`argv`, `input()`, `IO.prompt`/`readInt`/`readFloat`/`readBool` (feed it stdin) |
| [15_inline_c_macros.grav](15_inline_c_macros.grav) | `#define` macros, inline C at top level / as expressions / as statements, decorators, `export` |
| [16_async.grav](16_async.grav) | async tasks on the worker pool, `GRAV_MAXPROCS`, exceptions across threads, `Mutex`, `AtomicInt`, `Channel`, `awaitAll` |
| [17_auto_any.grav](17_auto_any.grav) | inferred generic type arguments (`auto`), dynamic values (`any`) with `is`/`as` |
| [18_data_structures.grav](18_data_structures.grav) | choosing a collection: `Array`, `Deque`, `Stack`/`Queue`, `HashMap`/`HashSet`, `SortedMap`, `PriorityQueue`, `LinkedList`, `BitSet`, `Pair`, `Optional` |
| [19_enums_match.grav](19_enums_match.grav) | data enums as a syntax tree, recursive `match`, `Result`/`Option` for errors, constant folding |

## A multi-file project

[project/](project/) — `main.grav` imports modules from sub-folders
(`util/text.grav`, `model/shapes.grav`) that use nested namespaces, `export`, and a
module-level global shared across files.

## Complete programs

| File | What it is |
|------|------------|
| [apps/bank.grav](apps/bank.grav) | accounts with inheritance, custom exceptions, overdraft rules, interest/fees, statements |
| [apps/word_freq.grav](apps/word_freq.grav) | tokenizing text, word counts with `HashMap`, sorting with lambdas, statistics |
| [apps/life.grav](apps/life.grav) | Conway's Game of Life on a wrapping grid |
| [apps/todo.grav](apps/todo.grav) | a command-driven task manager: string `switch`, enums, operator overloading, exceptions |
