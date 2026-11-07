# `src/codegen` — C code generation

Translates a type-checked `Program` into a single self-contained C translation unit.

| File                   | Responsibility                                                  |
|------------------------|----------------------------------------------------------------|
| `mangle.h/.cpp`        | Name mangling (`a.b.C` → `a__b__C`) and C type spelling.        |
| `codegen.h`            | The `CodeGen` class and its output sections.                   |
| `codegen_runtime.cpp`  | The runtime prelude (strings, RTTI, tracing, exceptions).      |
| `codegen_types.cpp`    | Struct layouts, vtable & itable types/instances, RTTI, enums.  |
| `codegen_flow.cpp`     | Blocks/`defer`, loops & `for … in`, try/catch/finally, the cleanup stack that `return`/`break`/`continue` unwind, `volatile` analysis, line-mark filtering. |
| `codegen_lambda.cpp`   | Closures: lifted `grav_lambda_N` functions, `GravEnv_N` capture structs, `grav_mk_lambda_N` constructors, function references, and per-signature `grav_call_N` helpers. |
| `codegen_order.cpp`    | Left-to-right evaluation order: hoists call arguments and string-chain pieces into temporaries where C's order is unspecified. |
| `codegen_dispatch.cpp` | Method calls (devirtualized direct calls, single-evaluation thunks), fused string building, `print`/`str`. |
| `codegen_func.cpp`     | Prototypes, functions, methods, constructors, accessors, `main`.|
| `codegen_stmt.cpp`     | Statement emission.                                            |
| `codegen_expr.cpp`     | Expression emission, calls, upcasts/boxing.                    |

### Output is assembled in ordered sections

`typedefs → structs → vtableTypes → prototypes → vtables → defs`, so forward
declarations always precede use.

### The object & dispatch model

- **Object header**: every class struct begins with
  `const void* __vt; const GravTypeInfo* __type;` — compatible with the shared
  `struct GravObject`. Inherited fields are flattened base-first, so a `Derived*`
  is layout-compatible with `Base*`.
- **Virtual dispatch**: one `const` vtable struct per class, slots laid out base-first.
  A call `a.m(x)` whose method no subclass of `a`'s static type overrides becomes a
  direct `Impl_m_m((Impl*)a, x)`; otherwise it calls a `static inline` thunk
  `SlotOwner__vcall__m((SlotOwner*)a, x)` that loads the slot (so `a` is evaluated
  once even when it is a call). Interface calls use `I__icall__m(v, x)` the same way.
- **Interfaces**: an interface value is a `GravIface { void* obj; const void* itab; }`
  fat pointer. Each (class × interface) pair gets an itable instance; a call becomes
  `((I_ITAB*)v.itab)->m(v.obj, …)`. Boxing a class into an interface builds the fat
  pointer at the assignment/argument site.
- **RTTI**: a per-class `GravTypeInfo { name, base }` is linked into every object's
  `__type`; `typename`/`isInstance` read it.
- **Constructors**: `Class_new(...)` `calloc`s the struct, sets `__vt`/`__type`, runs
  the constructor body, returns the pointer.
- **Destructors**: `Class__drop(self)` runs the body then the nearest base's drop;
  `GravTypeInfo.drop` points at the dynamic type's chain, which `free(obj)`
  (`grav_free_obj`) calls before releasing memory.
- **Function values**: `GravFn { fn, env }`; a lambda's body becomes
  `grav_lambda_N(void* env, …)` which re-binds its captures as locals, so it is
  compiled like any function. Calls go through `grav_call_N(f, …)` (null-checked).
- **Control flow with cleanups**: try/finally, catch-with-finally, `defer` and loops
  push entries on a cleanup stack; `return` unwinds all of them (restoring the
  `jmp_buf` stack and emitting finally/deferred code inline), `break`/`continue`
  unwind to the innermost loop.
- **The release list**: call results and held field/element reads used inside
  an expression go on a per-thread release list (`grav_autorelease`), drained
  when their statement ends (`grav_pool_drain`). An early `return`, `break` or
  `continue` skips the drain of the statements it leaves, so a function or loop
  where that can happen (`exitSkipsDrain`) records the list's height on entry
  and drains back to it on every way out.
- **Results are +1, except `return self`**: a function's reference result
  belongs to the caller, which releases it (or puts it on the release list). A
  method whose every return is `return self`, that overrides nothing, is
  overridden by nothing and is no interface's method (`returnsSelfBorrowed`)
  returns its receiver borrowed instead, so fluent chains such as
  `sb.append(a).append(b)` do no reference counting.
- **Unwinding through frames**: an exception is a `longjmp`, which skips the
  releases of every frame between the throw and its handler. So an owned local
  whose scope can still throw (a call, or a fault such as an index out of
  range or a null access) registers its address and a release
  function (`grav_uw_push`; popped with `grav_uw_top--` where its scope ends).
  Each handler (`grav_try_push`, recovery points, the test runner) records the
  registry height, and `grav_throw`/`grav_rethrow` release the entries above it
  before jumping, while the frames are still intact. `_new` registers the object
  it builds as `grav_uw_unbuilt`: a throwing constructor releases its fields but
  not its destructor.

### Runtime prelude

A small, dependency-light prelude is emitted at the top of every program: string
helpers (`grav_concatn`/`grav_fmt` for fused `+` chains, `grav_streq`), RTTI
(`grav_typename`, `grav_is_instance`), `grav_free_obj`, call-stack tracing
(compiled out when `GRAV_NO_TRACE` is defined, i.e. `--release`/`--no-trace`), and
the exception runtime.

### Type spelling

`cType` (free, in `mangle`) maps primitives and class pointers; `CodeGen::cTy` wraps it
to render interface types as the `GravIface` value type. Use `cTy` inside codegen.
