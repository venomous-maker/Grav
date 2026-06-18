# Grav for VS Code

Syntax highlighting, snippets, and build/run commands for the
[Grav language](../../README.md) (`.grav`).

## Features

- **Highlighting** — keywords, the widened numeric / `binary` types, `super`,
  `typedef`, generics and return types, PascalCase types / structs, `ALL_CAPS`
  constants, function & member calls, variables, strings with `${…}`
  interpolation, decorators, and inline-C `%{ … %}` (highlighted with the real C
  grammar).
- **Navigation & refactoring** — go-to-definition (`F12`), find all references
  (`Shift+F12`), rename symbol (`F2`), document outline (`Ctrl+Shift+O`), and
  hover. These use the shared [`../lsp/grav-index.js`](../lsp/grav-index.js)
  directly (no language-server process, no npm dependencies).
- **Snippets** — `main`, `fn`, `class`, `extends` (with `super`), `interface`,
  `trait`, `struct`, `enum`, `match`, `for`, `try`, `importstd`, `inlinec`.
- **Commands** — *Grav: Compile & Run Current File* (`Ctrl+Alt+R`) and
  *Grav: Compile Current File to C*, run through the `grav` compiler. Point
  `grav.compilerPath` at your binary if it isn't on `PATH`.

## Install locally

> Modern VS Code (1.74+) only loads extensions that are **registered** (it no
> longer auto-scans `~/.vscode/extensions/`), so dropping a folder or symlink there
> does nothing. Package a `.vsix` and install it — that's the reliable path:

```bash
cd editors/vscode
npx --yes @vscode/vsce package --no-dependencies   # -> grav-lang-0.9.0.vsix
code --install-extension grav-lang-0.9.0.vsix
# then reload VS Code  (Cmd/Ctrl+Shift+P -> "Developer: Reload Window")
```

The extension is self-contained: `grav-index.js` (the shared symbol index) is
bundled into this folder, so the `.vsix` works on its own. It mirrors
[`../lsp/grav-index.js`](../lsp/grav-index.js) — keep the two in sync if you edit
the indexer.

For iterating on the extension itself, the fastest loop is to open this folder in
VS Code and press **F5** (*Run Extension* — launches an Extension Development Host),
which loads the in-tree source directly with no packaging.

The grammar in [`syntaxes/grav.tmLanguage.json`](syntaxes/grav.tmLanguage.json) is
also reused by the CLion/JetBrains TextMate bundle — see
[`../jetbrains/`](../jetbrains/).
