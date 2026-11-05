# 20. How Grav compiles

You can write Grav without knowing what happens inside the compiler, just as you
can drive without knowing how an engine works. But knowing helps when you read
performance numbers, debug something odd, or write inline C. This chapter opens
the bonnet.

In this chapter you will learn:

- the stages a program goes through, from source to executable,
- what the generated C looks like for classes, interfaces and strings,
- how generics, closures, async tasks and exceptions are implemented,
- what is fast, what costs something, and how to inspect the output yourself.

## The pipeline

```text
source files ─▶ loader ─▶ lexer ─▶ macros ─▶ parser ─▶ generics ─▶ symbols ─▶ type checker
                                                                                   │
              executable ◀─ C compiler ◀─ C file ◀─ code generator ◀───────────────┘
```

| Stage | What it does |
|-------|--------------|
| loader | reads the file and everything it imports, each once, remembering which line came from where |
| lexer | splits text into tokens; expands string interpolation, raw and multi-line strings |
| macros | applies `#define`s |
| parser | builds the syntax tree, and rewrites some sugar (local functions, tuples, struct methods) |
| generics | makes one concrete copy of each generic type, function and method per set of type arguments (*monomorphization*) |
| symbols | collects every class, method and namespace; resolves overloads and builds method tables |
| type checker | checks every expression's type, inference, access rules, missing returns, null safety |
| code generator | writes C, including reference counting, bounds checks and stack-trace bookkeeping |
| C compiler | your `cc` turns the C into machine code, with `-O2` by default |

The front end may run several times for one build: when a generic type argument
is inferred from a later use, the compiler records it and runs again with the new
information.

## A look at the generated C

For this program:

```grav
interface Shape { fn area() -> float }

class Square implements Shape {
    private side: float
    constructor(side: float) { self.side = side }
    fn area() -> float { return self.side * self.side }
}

fn total(s: Shape, n: int) -> float { return s.area() * n }

fn main() {
    let sq = new Square(3.0)
    let label = "area=${sq.area()} total=${total(sq, 2)}"
    print(label)
}
```

Output:

```text
area=9 total=18
```

`grav demo.grav --release --no-lines` writes `demo.c`. The interesting parts:

```c
struct Square {
    const void* __vt;              /* the method table */
    const GravTypeInfo* __type;    /* run-time type information */
    long long __rc;                /* the reference count */
    double side;
};

static inline double Shape__icall__area(struct GravIface self) {
    return ((const Shape_ITAB*)self.itab)->area(self.obj);
}

static double Square_m_area(Square* self) {
    return ((self)->side * (self)->side);
}

static double vf_total(struct GravIface s, int n) {
    return (Shape__icall__area(s) * n);
}

static void vf_main(void) {
    Square* sq = Square_new(3.0);
    double grav_tmp0 = Square_m_area((Square*)(sq));     /* a direct call */
    double grav_tmp1 = vf_total((struct GravIface){ (void*)(sq), &Square__itab__Shape }, 2);
    const char* label = grav_concatn(4, "area=", grav_dtoa(grav_tmp0, (char[32]){0}),
                                     " total=", grav_dtoa(grav_tmp1, (char[32]){0}));
    puts(label);
    grav_str_release(label);       /* reference counting, inserted for you */
    grav_release((void*)(sq));
}
```

(Some bookkeeping lines are left out.) What to notice:

- **A class is a C struct** with three header fields: its method table, its type
  information (used by `is`, `as`, `typename` and printing), and its reference count.
  Parent-class fields come first, so a pointer to a subclass is also a valid pointer
  to its parent.
- **Calls are direct when they can be.** `sq.area()` calls `Square_m_area`
  directly, because nothing overrides `area`. Only calls that may reach different
  methods go through a table.
- **An interface value is a pair**: the object and the table of its methods for
  that interface. The pair is built where a `Square` becomes a `Shape`.
- **Interpolation is one allocation**: the pieces are joined in a single call.
  `print` of a string is a plain `puts`.
- **Evaluation order is fixed.** The two calls are made into temporaries first, so
  they run left to right; C itself leaves argument order unspecified.
- **Memory is managed in the C**: the string and the object are released when
  `main` ends.
- Everything is `static` and tables are `const`, so the C compiler can inline freely.

## Other features under the hood

**Generics.** `Box<int>` and `Box<string>` become two separate structs, each with
its own copies of the methods. There is no boxing and no run-time type check:
generic code is as fast as hand-written code for each type.

**Closures.** A lambda becomes an ordinary C function that takes a pointer to an
*environment*: a small heap block holding copies of the captured variables. A
function value is the pair `{code, environment}`, and the environment is reference
counted.

**Data enums.** An abstract base class plus one class per variant; `match` checks
a tag and casts.

**Async tasks.** An `async fn f` gets a companion function that copies the
arguments into a task record, queues it for the worker pool and returns the task
as the `Future`. `await` runs the task itself if no worker has started it yet,
then waits, rethrows a stored exception, and reads the result.

**Exceptions.** `try` uses C's `setjmp`/`longjmp` with a stack of handlers. Local
variables changed inside a `try` are marked `volatile` so they keep their values
in `catch`. `finally` code is emitted on every way out of the block.

**Run-time checks.** Array indexing, string indexing, slices, integer division and
enum conversions are checked; a failing check calls a small out-of-line function
that throws (or panics without the standard library). A signal handler turns a
null-reference access or a stack overflow into a panic with the Grav call stack.

**Line numbers.** The C file carries `#line` directives pointing back at your
`.grav` source, so C compiler errors, debuggers (`gdb`) and sanitizers report Grav
file and line numbers. `--no-lines` turns them off.

## Performance notes

- Builds use `-O2` by default. `--release` uses `-O3` and drops the stack-trace
  bookkeeping, which costs a little on every call.
- Method calls that cannot be overridden are direct and can be inlined. Virtual and
  interface calls cost one indirect call.
- Each object is one allocation. Reference counting adds an increment or
  decrement when a reference is stored or dropped; the compiler skips the ones it
  can prove unnecessary.
- String building with `+` chains and interpolation allocates once per
  expression. For text built in a loop, use `StringBuilder`.
- `HashMap` lookups are O(1) on average. `Map` is a plain list, meant for small maps.
- Bounds checks are one predictable branch each.

## Inspecting the output yourself

```bash
grav app.grav                  # writes app.c
grav app.grav -o out.c         # choose the name
grav build app.grav --keep-c   # build, and keep the C next to the binary
grav app.grav -S               # assembly
grav app.grav --emit bin -v    # print the C compiler command
```

Reading the C for a small program is a good way to learn what your code costs.

## Summary

- Grav checks the whole program, then writes one self-contained C file and
  compiles it with your C compiler.
- Classes are structs with a method table, type info and a reference count;
  interfaces are `{object, table}` pairs; generics are copied per type.
- Calls are direct whenever no override is possible; strings are built in one
  allocation; evaluation is left to right.
- `--release`, `--keep-c`, `-S` and `-v` help with performance work.

Back to the [guide's contents](README.md).
