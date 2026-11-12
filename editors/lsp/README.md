# Grav Language Server

A small, **dependency-free** [Language Server](https://microsoft.github.io/language-server-protocol/)
for Grav. One implementation gives the same navigation/refactoring features to
every editor:

| Feature | LSP method |
| ------- | ---------- |
| Go to definition / declaration — follows `import` lines into the standard library, `grav.toml` paths and packages (installed git ones and `{ path = ... }` folders); resolves `import { A as B }` and `use … as` aliases; on an import string, opens the module | `textDocument/definition`, `declaration` |
| Hover — signature, doc comment, namespace and module; what a keyword, type or built-in (including the internal `__retain`/`__release`/...) means, with an example | `textDocument/hover` |
| Completion — members by inferred type, static members, namespace/alias members, import paths, keywords and built-ins (documented), snippets for common constructs | `textDocument/completion` |
| Signature help for calls and constructors | `textDocument/signatureHelp` |
| Inlay hints — the type after each `let`, `for`, lambda parameter and `match` binding that does not write one, from `grav check --types` (refreshed after each check) | `textDocument/inlayHint` |
| Outline with classes, members and enum variants nested | `textDocument/documentSymbol` |
| Workspace symbols, including the standard library | `workspace/symbol` |
| Find references, rename | `textDocument/references`, `rename` (+ `prepareRename`) |
| Formatting (`grav fmt -`) | `textDocument/formatting` |
| Diagnostics (`grav check --json --types`, with "declared in std.x" notes) on open and save | `textDocument/publishDiagnostics` |

It is a careful scanner rather than a full parser: strings, comments and inline C
are masked first, then declarations are matched with a bracket stack for nesting.
Zero npm dependencies. Files:

- [`grav-index.js`](grav-index.js) — the index and import resolver (a copy is
  bundled into the VS Code extension; `editors/sync.sh` refreshes it).
- [`server.js`](server.js) — the stdio LSP server (Neovim, JetBrains, any client).

## Requirements

Node.js on `PATH`. Diagnostics, formatting, and locating the standard library (`grav env --json`) need the `grav` compiler — set it with the
`GRAV_COMPILER` env var or the `compilerPath` init option (default: `grav`).

## Use it

- **Neovim** — starts automatically for `.grav` buffers (see
  [`../nvim/`](../nvim/)); override the server path with `g:grav_lsp_server` or
  `$GRAV_LSP_SERVER`.
- **VS Code** — the [extension](../vscode/) runs the same `grav-index.js`
  in-process (no server process).
- **CLion / JetBrains** — the [plugin](../jetbrains/) registers this server
  through the IDE's LSP API (paid IDEs only).

Run it standalone (for debugging) — it speaks LSP on stdio:

```bash
node editors/lsp/server.js
```
