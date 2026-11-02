# `src/sema` — symbols & type checking

The semantic stage: it resolves names, builds the type/class model, enforces the type
system, and annotates the AST for codegen.

| File                     | Responsibility                                              |
|--------------------------|------------------------------------------------------------|
| `symbols.h/.cpp`         | `Registry` — the symbol tables and name resolution.        |
| `typechecker.h`          | `TypeChecker` interface + scope/local tracking.            |
| `typechecker.cpp`        | Driver + statement checking.                               |
| `typechecker_expr.cpp`   | Expression checking and call/member resolution.            |
| `typechecker_lambda.cpp` | Lambdas: parameter inference from the expected type, capture analysis (read-only copies), named functions as values, calls through function values. |
| `typechecker_ops.cpp`    | Operator overloading, for-each/`step` loops, `defer`, `toString`/enum display, and the missing-return analysis. |

### `Registry` (build order matters)

1. **registerDecls** — record every class/interface/function by fully-qualified name;
   note namespace prefixes.
2. **canonicalize** — rewrite every `Named` type (in the AST *and* the registry) to its
   fully-qualified form; resolve `extends`/`implements` targets.
3. **assignMethodNames** — method overloading: overrides inherit the base method's
   internal name (same vtable slot); the first method of a name keeps it; other
   signatures become `name__<signature>`. `findMethodsBySource` lists the overloads.
4. **synthesizeAccessors** — add `get_x`/`set_x` methods for private fields.
5. **computeSlots** — lay out each class's vtable slots, base-first, so a base
   pointer's vtable view is layout-compatible with any subclass.
6. **checkHierarchies** — abstract-method completeness, override-signature matching,
   `final` classes/methods, and interface conformance.

It also answers the queries everyone else needs: `resolveType`/`resolveFunc`/
`resolveNamespace` (namespace-aware), `findField`/`findMethod` (walk the chain),
`findMethodImpl`, `isSubclass`, `classImplements`, plus `isOverridden` (does any
subclass replace a method? — drives devirtualization) and `findDestructor`.

### `TypeChecker`

- Walks function/method/constructor bodies with a stack of lexical scopes.
- Enforces: declare-before-use, no implicit conversions, matching operand types,
  access control (`private`/`protected`), `readonly`, `const`, abstract instantiation,
  argument/return type compatibility (allowing class→base and class→interface).
- Resolves each call to one of: free function, builtin (`print`/`typename`/
  `isInstance`), instance method (vtable or interface dispatch), or static method —
  and stamps the resolution onto the `CallExpr`.
- Emits **warnings** for unused local variables (fatal under `-Werror`).
- Collects all diagnostics (sorted by location) rather than stopping at the first.
