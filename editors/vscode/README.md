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

## Install locally (no packaging needed)

VS Code loads any folder under its extensions directory, so a symlink or copy is
enough to test:

```bash
ln -s "$PWD/editors/vscode" ~/.vscode/extensions/grav-lang-0.9.0
# then reload VS Code  (Cmd/Ctrl+Shift+P -> "Developer: Reload Window")
```

## Package a `.vsix` (for sharing / Marketplace)

```bash
cp ../lsp/grav-index.js .   # bundle the shared indexer for packaging
npm install -g @vscode/vsce
cd editors/vscode
vsce package            # produces grav-lang-0.9.0.vsix
code --install-extension grav-lang-0.9.0.vsix
```

> The symlink install above resolves `../lsp/grav-index.js` directly, so it needs
> no copy. A packaged `.vsix` can't reference files outside the extension folder,
> so copy `grav-index.js` in first (and change the `require` to `"./grav-index"`).

The grammar in [`syntaxes/grav.tmLanguage.json`](syntaxes/grav.tmLanguage.json) is
also reused by the CLion/JetBrains TextMate bundle — see
[`../jetbrains/`](../jetbrains/).
