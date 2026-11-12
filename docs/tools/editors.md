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
  paths and the `grav.toml` dependencies of the open file's project: git packages
  from the cache at their `grav.lock` commit, local `{ path = ... }` ones in their
  folder. On an import string, it opens the module.
- **Hover**: a declaration's signature, its doc comment (the `//` lines above it),
  and where it lives; for a keyword, type or built-in function, what it means and
  an example. On a local variable, its type, even where none is written:
  `let xs: Array<int>`, a `for (row in rows)` loop variable (`row: Row`), a
  `catch (e: IoError)` variable, a lambda parameter, a `case Some(x):` binding.
- **Inlay hints** show those types after each binding that does not write one
  (`let names = new Array<string>()` shows `names: Array<string>`). The types
  come from the compiler (`grav check --types`, run on open and save) and are
  printed the way they are written in Grav: `HashMap<string, any>`,
  `fn(int) -> string`, `long?`, `int[3]`. Before the first check, and for code
  the compiler cannot check, they are read from the source where it shows them.
- **Completion** of members (from the variable's type, including loop and
  lambda variables), static members,
  namespace members, import paths, keywords and built-ins, plus snippets for
  common constructs (`fn`, `class`, `match`, `test`, `ifnull`, ...).
- **Signature help** inside calls.
- **Outline**, workspace symbols (including the standard library), find
  references and rename.
- **Diagnostics** from `grav check --json` on open and save. (A compiler older
  than `--types` still gives diagnostics, without the compiler's types.)
- **Formatting** with `grav fmt`.

## VS Code

Install the extension from `editors/vscode/` (it is packaged as a `.vsix`):

```bash
code --install-extension editors/vscode/grav-lang-1.10.0.vsix
```

Commands (Ctrl+Shift+P, then "Grav"): Run (Ctrl+Alt+R), Build, Run Tests
(Ctrl+Alt+T), Check, Format, Show Generated C, Run/Test Project, New Project, Add
Package, Install Packages. Settings: `grav.compilerPath`, `grav.checkOnSave`.

## Neovim

Add `editors/nvim` to the runtime path. `.grav` files get highlighting,
indentation and the language server automatically (Node.js must be on `PATH`).
Inlay hints are on in Neovim 0.10 and later; set `vim.g.grav_inlay_hints = false`
to turn them off, or toggle them with `vim.lsp.inlay_hint.enable(false)`.
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
