# Grav for VS Code

Syntax highlighting, snippets, and build/run commands for the
[Grav language](../../README.md) (`.grav`).

## Features

- **Highlighting** — keywords, the widened numeric / `binary` types, `super`,
  `typedef`, strings with `${…}` interpolation, decorators, and inline-C `%{ … %}`
  blocks (highlighted with the real C grammar).
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
npm install -g @vscode/vsce
cd editors/vscode
vsce package            # produces grav-lang-0.9.0.vsix
code --install-extension grav-lang-0.9.0.vsix
```

The grammar in [`syntaxes/grav.tmLanguage.json`](syntaxes/grav.tmLanguage.json) is
also reused by the CLion/JetBrains TextMate bundle — see
[`../jetbrains/`](../jetbrains/).
