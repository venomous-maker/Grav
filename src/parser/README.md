# `src/parser` — recursive-descent parser

Turns the token stream into a `Program` AST. Split into three files so none grows
unwieldy:

| File               | Responsibility                                                   |
|--------------------|------------------------------------------------------------------|
| `parser.h`         | The `Parser` class interface and token-cursor state.             |
| `parser_core.cpp`  | Token cursor, top-level & namespaces, class/interface/function/field/method/constructor, type & qualified-name parsing. |
| `parser_stmt.cpp`  | Statements: `let`/`const`, `return`, expression/assignment.      |
| `parser_expr.cpp`  | Expressions via precedence climbing, plus postfix (`.`/calls), `new`, casts. |

### Grammar highlights

- **Top level** is a sequence of declarations: `namespace`, `class`,
  `abstract class`, `interface`, `fn`.
- **Namespaces** push a path prefix; every declaration inside is recorded with its
  fully-qualified `fqName`. Dotted (`namespace a.b { … }`) and nested forms both work.
- **Classes** parse optional `extends Base`, `implements I, J`, then members. Each
  member may carry an access modifier and `static`/`readonly`/`abstract`.
- **Expressions** layer comparison < additive < multiplicative < unary < postfix <
  primary. Postfix builds `MemberExpr`/`CallExpr` chains; `obj.m(args)` is
  `Call(Member(obj, m), args)`.

### Notes

- The grammar is brace-delimited, so the parser doesn't rely on newlines.
- Assignability of an lvalue is checked here only structurally (must be a name or
  member); real type/const/readonly rules live in [sema](../sema/README.md).
- Syntax errors throw `GravError` with the offending token's location.
