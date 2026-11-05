# Editors

Grav has editor support for VS Code, Neovim and JetBrains IDEs (CLion, IntelliJ
and the rest). All of them use one index and language server written without
dependencies, so they offer the same features.

## Features

- **Highlighting** of the whole language: declarations, generics, function types,
  every import form, `use`, namespaces, `test` blocks, number literals, string
  interpolation and format specs, raw and multi-line strings, character literals,
  `T?` and `x!`, labels, named arguments, and inline C with the C grammar.
- **Go to definition**, following imports into the standard library, project
  paths and installed packages. On an import string, it opens the module.
- **Hover**: a declaration's signature, its doc comment (the `//` lines above it),
  and where it lives; for a keyword, type or built-in function, what it means and
  an example.
- **Completion** of members (from the variable's type), static members,
  namespace members, import paths, keywords and built-ins, plus snippets for
  common constructs (`fn`, `class`, `match`, `test`, `ifnull`, ...).
- **Signature help** inside calls.
- **Outline**, workspace symbols (including the standard library), find
  references and rename.
- **Diagnostics** from `grav check --json` on open and save.
- **Formatting** with `grav fmt`.

## VS Code

Install the extension from `editors/vscode/` (it is packaged as a `.vsix`):

```bash
code --install-extension editors/vscode/grav-lang-1.8.1.vsix
```

Commands (Ctrl+Shift+P, then "Grav"): Run (Ctrl+Alt+R), Build, Run Tests
(Ctrl+Alt+T), Check, Format, Show Generated C, Run/Test Project, New Project, Add
Package, Install Packages. Settings: `grav.compilerPath`, `grav.checkOnSave`.

## Neovim

Add `editors/nvim` to the runtime path. `.grav` files get highlighting,
indentation and the language server automatically (Node.js must be on `PATH`).
See [`editors/nvim/README.md`](../../editors/nvim/README.md).

## JetBrains IDEs

`editors/jetbrains/` builds a plugin that adds the TextMate grammar and starts
the language server through the IDE's LSP support (available in the paid IDEs).
See [`editors/jetbrains/README.md`](../../editors/jetbrains/README.md).

## Other editors

Any editor with LSP support can run the server directly; it speaks LSP over
standard input and output:

```bash
node editors/lsp/server.js
```

The compiler is found through the `GRAV_COMPILER` environment variable or the
`compilerPath` initialisation option (default: `grav`). The TextMate grammar in
`editors/vscode/syntaxes/` works in editors that read TextMate grammars.

## Writing documentation comments

Editors show the `//` comment lines directly above a declaration when you hover
over it, or a comment at the end of the declaration's own line:

```grav
import "std"

// The price after tax, rounded to cents.
fn withTax(price: float) -> float {
    return Math.roundTo(price * 1.16, 2)
}

let retries: int = 3      // how many times to try before giving up

fn main() {
    print(retries)
}
```
