# `src/ast` — abstract syntax tree

The node types produced by the [parser](../parser/README.md) and consumed by
[sema](../sema/README.md) and [codegen](../codegen/README.md).

| File          | Purpose                                                |
|---------------|--------------------------------------------------------|
| `ast.h`       | All node definitions.                                  |
| `ast.cpp`     | Small helpers (`binaryOpSymbol`, `accessName`, …).     |

### Shape of the tree

- **Expressions** (`Expr`): literals, `NameExpr`, `SelfExpr`, `BinaryExpr`,
  `CastExpr`, `MemberExpr` (`a.b`), `NewExpr` (`new C(...)`), `CallExpr`.
- **Statements** (`Stmt`): `LetStmt` (also `const`), `AssignStmt`, `ReturnStmt`,
  `ExprStmt`, grouped in a `Block`.
- **Declarations** (`Decl`): `ClassDecl`, `InterfaceDecl`, `FunctionDecl`, plus the
  nested `FieldDecl`, `MethodDecl`, `ConstructorDecl`, `Param`. A `Program` owns the
  top-level decls.

### Annotation contract

Nodes start out lightly populated by the parser and are **enriched in place** by sema:

- every `Expr` gets its resolved `TypeRef type`;
- `CallExpr`/`MemberExpr` get a resolved `kind` plus the target/owner/slot info that
  codegen needs (free fn vs method vs static vs interface dispatch);
- `NewExpr.className` and `LetStmt.resolvedType`/declared types are rewritten to
  fully-qualified names;
- `BinaryExpr.stringConcat` marks `string + string`.

Each `Decl` carries `fqName`, the fully-qualified name assembled from the enclosing
namespace during parsing.
