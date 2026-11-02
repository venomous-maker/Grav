# 15. How Grav compiles

Grav compiles a program to a single, self-contained C file, then hands it to your C
compiler. Knowing what the output looks like helps when reading performance numbers,
debugging, or writing inline C.

## The pipeline

```text
source files ──▶ load (+imports) ──▶ lexer ──▶ preprocessor (#define) ──▶ parser
     ──▶ monomorphizer (generics) ──▶ registry (symbols, overloads, vtables)
     ──▶ type checker ──▶ identifier sanitizer ──▶ code generator ──▶ C ──▶ cc ──▶ binary
```

| Stage | Source | Job |
|-------|--------|-----|
| loader | `main.cpp` | inline `import`ed files once each, keep a line map for diagnostics |
| lexer / preprocessor | `src/lexer/` | tokens, string interpolation, `#define` expansion |
| parser | `src/parser/` | the AST |
| monomorphizer | `src/sema/monomorph.cpp` | one concrete copy of each generic type/function/method per type-argument list |
| registry | `src/sema/symbols.cpp` | names, namespaces, overload names, vtable slots, hierarchy checks |
| type checker | `src/sema/typechecker*.cpp` | types, overload resolution, captures, missing returns, access rules |
| code generator | `src/codegen/` | C output, devirtualization, string fusion, ownership-based freeing |

## What the generated C looks like

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

`grav demo.grav --release` produces (abridged):

```c
struct Square {
    const void* __vt;              /* vtable */
    const GravTypeInfo* __type;    /* runtime type info */
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
    double grav_tmp0 = Square_m_area((Square*)(sq));          /* direct call */
    double grav_tmp1 = vf_total((struct GravIface){ (void*)(sq), &Square__itab__Shape }, 2);
    const char* label = grav_fmt("area=%g total=%g", (double)(grav_tmp0), (double)(grav_tmp1));
    bool label__own = 1;
    puts(label);
    if (label__own) free((void*)label);                       /* freed automatically */
}
```

What to notice:

- **Classes** become structs with a vtable pointer and a type-info pointer; base-class
  fields come first, so a subclass pointer is also a valid base pointer.
- **Direct calls**: `sq.area()` is a plain call because no subclass of `Square`
  overrides `area`. Only calls that may reach different implementations go through
  a vtable (or, for interfaces, an itable via a small `static inline` helper that
  evaluates the receiver once).
- **Interfaces** are fat pointers `{object, method table}` built where a class
  value becomes an interface value.
- **Strings**: the whole interpolation is one `grav_fmt` call (a single allocation),
  and `print` of a string is a `puts`/`printf` with no allocation at all.
- **Evaluation order**: the two calls are computed into temporaries first, so they
  run left to right (C leaves argument order unspecified).
- **Ownership**: `label` is owned by `main`, so it is freed when it goes out of scope.
- Everything is `static` and tables are `const`, so the C compiler can inline freely.

## Generics

`Box<int>` and `Box<string>` become two separate structs (`Box$int`, `Box$string`)
with their own methods — no boxing, no runtime type checks. Generic methods are
instantiated per call-site type list (`map$string`).

## Closures

A lambda becomes a top-level C function `grav_lambda_N(void* env, ...)`. Its
captured variables are copied into a heap `GravEnv_N` struct when the lambda is
created; a function value is the pair `{code pointer, environment}`.

## Exceptions

`try` uses `setjmp`/`longjmp` with a stack of handlers. Locals modified inside a `try`
are declared `volatile` so they keep their values in `catch` at any optimization
level. `finally` blocks are emitted on every exit path (normal, handled, propagating,
`return`/`break`/`continue`).

## Performance notes

- Builds default to `-O2`; `--release` uses `-O3` and removes stack-trace
  bookkeeping (one store per call-capable statement, plus a push/pop per call).
- Calls that cannot be overridden (non-overridden methods, `final`) are direct and
  inlinable; virtual calls cost one indirect call.
- `+` chains and interpolation allocate once; `print` of them allocates nothing.
  Use `StringBuilder` to build large text incrementally.
- `HashMap` is O(1) on average; `Map` is a simple ordered list (O(n) lookup) meant
  for small maps.
- Collection access is bounds-checked (one predictable branch).

Inspect the C for any program with `grav file.grav -o out.c` (or `--keep-c` when
building), and add `-v` to see the C compiler command.

## Diagnostics

The compiler collects all errors (sorted by position) instead of stopping at the first:

```text
gravc: shop.grav:12:9: type error: cannot assign a value of type string to 'total' of type int
        total = "none"
        ^
gravc: shop.grav:20:5: type error: function 'price' may reach the end without returning a value of type float
gravc: 2 error(s); aborting
```

Warnings (unused variables) are printed but do not stop the build unless you pass
`-Werror`. At run time, an uncaught exception prints a stack trace with file and line
for each frame.

[← Back to the index](README.md)
