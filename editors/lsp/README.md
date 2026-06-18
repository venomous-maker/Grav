# Grav Language Server

A small, **dependency-free** [Language Server](https://microsoft.github.io/language-server-protocol/)
for Grav. One implementation gives the same navigation/refactoring features to
every editor:

| Feature | LSP method |
| ------- | ---------- |
| Go to declaration | `textDocument/definition` |
| Find references (project-wide) | `textDocument/references` |
| Rename / refactor | `textDocument/rename` (+ `prepareRename`) |
| Document outline | `textDocument/documentSymbol` |
| Hover (shows the declaration line) | `textDocument/hover` |
| Diagnostics | runs the `grav` compiler on open/save |

It is heuristic (regex-based, no full parse) — accurate for the common cases and
fast, with zero npm dependencies. Files:

- [`grav-index.js`](grav-index.js) — the shared symbol index (also used directly
  by the VS Code extension's providers).
- [`server.js`](server.js) — the stdio LSP server (used by Neovim and CLion).

## Requirements

Node.js on `PATH`. Diagnostics also need the `grav` compiler — set it with the
`GRAV_COMPILER` env var or the `compilerPath` init option (default: `grav`).

## Use it

- **Neovim** — starts automatically for `.grav` buffers (see
  [`../nvim/`](../nvim/)); override the server path with `g:grav_lsp_server` or
  `$GRAV_LSP_SERVER`.
- **VS Code** — the [extension](../vscode/) calls `grav-index.js` directly, so no
  server process is spawned.
- **CLion / JetBrains** — the [plugin](../jetbrains/) registers this server
  through the IDE's LSP API (paid IDEs only).

Run it standalone (for debugging) — it speaks LSP on stdio:

```bash
node editors/lsp/server.js
```
