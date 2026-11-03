# Grav — Neovim support

Syntax highlighting, filetype detection, and build/run keymaps for `.grav` files,
plus optional registration of the `gravc` MCP server with
[mcphub.nvim](https://github.com/ravitemer/mcphub.nvim).

## Layout

```
editors/nvim/
├── syntax/grav.vim      # highlighting: declarations, generics, type annotations, import forms, use, test blocks,
│                        #   case variants, hex/binary/exponent numbers, ${} interpolation, %{ %} C blocks
├── ftdetect/grav.lua    # *.grav -> filetype=grav
├── ftplugin/grav.{vim,lua} # comments/indent + nav keymaps + LSP autostart
├── lua/grav.lua         # LazyVim plugin spec: build/run keymaps + MCP server
├── lua/grav/goto.lua    # regex-based go-to-definition fallback
├── lua/grav/lsp.lua     # starts the Grav language server for .grav buffers
└── README.md
```

To get the richer LSP features, also copy the `lua/grav/` directory:

```bash
cp -r editors/nvim/lua/grav ~/.config/nvim/lua/
```

## Install (LazyVim)

The native `syntax`/`ftdetect`/`ftplugin` files just need to be on Neovim's
runtimepath. The quickest way:

```bash
cp -r editors/nvim/syntax editors/nvim/ftdetect editors/nvim/ftplugin ~/.config/nvim/
cp editors/nvim/lua/grav.lua ~/.config/nvim/lua/plugins/grav.lua
```

Set `GRAVC_REPO` (or `GRAVC`) in your shell if your checkout isn't at
`~/CLionProjects/Practice`, then restart Neovim and open any `.grav` file.

### Keymaps (in a `.grav` buffer)

| Key             | Action                                                   |
| --------------- | -------------------------------------------------------- |
| `gd` / `<C-]>`  | go to definition of the symbol under the cursor          |
| `<C-LeftMouse>` | go to definition (Ctrl-click)                            |
| `gr`            | references to the symbol (project-wide)                  |
| `gO`            | document outline (declarations in this file)             |
| `grn`           | rename symbol (LSP only)                                  |
| `K`             | hover / peek the definition line                         |
| `]]` / `[[`     | jump to next / previous top-level declaration            |
| `<leader>mr`    | compile to a binary and run it                           |
| `<leader>mc`    | transpile to C and show the output                       |

**LSP (recommended).** When `node` and the [Grav language server](../lsp/)
(`editors/lsp/server.js`) are found, `ftplugin/grav.lua` starts it for `.grav`
buffers and rebinds `gd`/`gr`/`gO`/`K` to real LSP requests, and adds `grn`
(rename) plus inline compiler diagnostics. The server is located by walking up
from the file for `editors/lsp/server.js`, or set `g:grav_lsp_server` /
`$GRAV_LSP_SERVER`.

**Fallback.** Without node/the server, go-to-definition stays LSP-free (pure Lua +
grep): it understands plain names (variables, parameters, functions, types) and
member accesses in chains — `self.name`, `ptr->name`, `a.b.c` — resolving the
segment under the cursor to its declaration (current buffer first, then
project-wide). `gr`/`gO`/`K` work the same way.

**Inline C** (`%{ … %}`) is highlighted with the real C syntax, and `${ … }`
string interpolation highlights its inner tokens.

### New in 1.6

`<leader>mt` runs the file's tests, `<leader>mk` type-checks, `<leader>mf`
formats with `grav fmt`, and `<leader>mp` / `<leader>mP` run / test the project
(`grav.toml`). With the language server you also get `<leader>cf` (format),
`<leader>ws` (workspace symbols, including the standard library), `<C-k>` in
insert mode (signature help), and LSP completion for members, namespaces and
import paths.

### MCP server

The `lua/grav.lua` spec registers the dependency-free `gravc` MCP server with
`mcphub.nvim` (if installed), pointing at `mcp_server/server.py` and exporting the
`GRAVC` binary path. See [`../../mcp_server/README.md`](../../mcp_server/README.md).
