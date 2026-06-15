# `src/lexer` — tokenizer

Turns raw source text into a flat `std::vector<Token>`.

| File            | Purpose                                                          |
|-----------------|------------------------------------------------------------------|
| `token.h`       | `TokenType` enum and the `Token` struct (lexeme + `line`/`col`). |
| `token.cpp`     | `tokenTypeName()` — human-readable names for diagnostics.        |
| `lexer.h/.cpp`  | `Lexer` — the scanner.                                          |

### What it handles

- Identifiers and keywords (`class`, `interface`, `abstract`, `namespace`, `fn`,
  `let`, `const`, `extends`, `implements`, `new`, `return`, `static`, `readonly`,
  `public`/`private`/`protected`, `self`/`this`, and control flow: `if`/`else`,
  `while`, `do`, `for`, `switch`/`match`/`case`/`default`, `break`/`continue`).
- Int and float literals; string literals with escapes (`\n`, `\t`, `\\`, `\"`).
- Operators and punctuation, including multi-char `->`, `==`, `!=`, `>=`, `<=`,
  `&&`, `||`, `!`, `++`, `--`, and `;` (for-loop headers).
- Comments: line `// …` and C-style block `/* … */` (unterminated blocks are an error).

### Notes

- Whitespace (including newlines) is insignificant — statement boundaries are
  recovered by the [parser](../parser/README.md) from the brace-delimited grammar.
- The lexer tracks `line`/`col` precisely so every later diagnostic can point at the
  offending token. It throws `GravError` on an invalid character or unterminated
  string/comment.
