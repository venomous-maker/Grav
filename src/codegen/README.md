# `src/codegen` — C code generation

Translates a type-checked `Program` into a single self-contained C translation unit.

| File                   | Responsibility                                                  |
|------------------------|----------------------------------------------------------------|
| `mangle.h/.cpp`        | Name mangling (`a.b.C` → `a__b__C`) and C type spelling.        |
| `codegen.h`            | The `CodeGen` class and its output sections.                   |
| `codegen_types.cpp`    | Prelude, struct layouts, vtable & itable types/instances, RTTI.|
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
- **Virtual dispatch**: one vtable struct per class, slots laid out base-first.
  A call `a.m(x)` becomes
  `((SlotOwner_VT*)((struct GravObject*)a)->__vt)->m((SlotOwner*)a, x)`.
- **Interfaces**: an interface value is a `GravIface { void* obj; const void* itab; }`
  fat pointer. Each (class × interface) pair gets an itable instance; a call becomes
  `((I_ITAB*)v.itab)->m(v.obj, …)`. Boxing a class into an interface builds the fat
  pointer at the assignment/argument site.
- **RTTI**: a per-class `GravTypeInfo { name, base }` is linked into every object's
  `__type`; `typename`/`isInstance` read it.
- **Constructors**: `Class_new(...)` `calloc`s the struct, sets `__vt`/`__type`, runs
  the constructor body, returns the pointer.

### Runtime prelude

A small, dependency-light prelude is emitted at the top of every program:
`grav_str_concat` (string `+`), `grav_typename`, `grav_is_instance`, and the
`GravTypeInfo`/`GravObject`/`GravIface` definitions.

### Type spelling

`cType` (free, in `mangle`) maps primitives and class pointers; `CodeGen::cTy` wraps it
to render interface types as the `GravIface` value type. Use `cTy` inside codegen.
