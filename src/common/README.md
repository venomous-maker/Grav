# `src/common` — shared foundations

Types and utilities used by every stage of the compiler.

| File             | Purpose                                                                 |
|------------------|-------------------------------------------------------------------------|
| `types.h/.cpp`   | `TypeRef` — a resolved type reference: a primitive (`int`/`float`/`bool`/`string`/`void`), a `Named` class/interface (carrying its **fully-qualified** name), or the internal `Error` sentinel. Includes equality and `typeRefName()` for diagnostics. |
| `diagnostics.h`  | `GravError` — an exception carrying a stage, a 1-based `[line:col]`, and a message. Reused (with stage `"warning"`) for warnings. |

### Notes

- `TypeRef` names are canonicalized to their fully-qualified form by the
  [`sema`](../sema/README.md) stage *before* code generation, so downstream code can
  trust `name` to be unambiguous (e.g. `geometry.Circle`).
- `GravError` is thrown for fatal, single-shot failures (lexing/parsing). The
  semantic stages instead *collect* `GravError` values so multiple problems are
  reported at once.
