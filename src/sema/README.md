# `src/sema` — symbols & type checking

The semantic stage: it resolves names, builds the type/class model, enforces the type
system, and annotates the AST for codegen.

| File                     | Responsibility                                              |
|--------------------------|------------------------------------------------------------|
| `symbols.h/.cpp`         | `Registry` — the symbol tables and name resolution.        |
| `typechecker.h`          | `TypeChecker` interface + scope/local tracking.            |
| `typechecker.cpp`        | Driver + statement checking.                               |
| `typechecker_expr.cpp`   | Expression checking and call/member resolution.            |

### `Registry` (build order matters)

1. **registerDecls** — record every class/interface/function by fully-qualified name;
   note namespace prefixes.
2. **canonicalize** — rewrite every `Named` type (in the AST *and* the registry) to its
   fully-qualified form; resolve `extends`/`implements` targets.
3. **synthesizeAccessors** — add `get_x`/`set_x` methods for private fields.
4. **computeSlots** — lay out each class's vtable slots, base-first, so a base
   pointer's vtable view is layout-compatible with any subclass.
5. **checkHierarchies** — abstract-method completeness, override-signature matching,
   and interface conformance.

It also answers the queries everyone else needs: `resolveType`/`resolveFunc`/
`resolveNamespace` (namespace-aware), `findField`/`findMethod` (walk the chain),
`findMethodImpl`, `isSubclass`, `classImplements`.

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
