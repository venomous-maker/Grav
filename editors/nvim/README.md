# Grav — Neovim support

Syntax highlighting, filetype detection, and build/run keymaps for `.grav` files,
plus optional registration of the `gravc` MCP server with
[mcphub.nvim](https://github.com/ravitemer/mcphub.nvim).

## Layout

```
editors/nvim/
├── syntax/grav.vim      # highlighting (keywords, strings + ${} interpolation, %{ %} C blocks, decorators)
├── ftdetect/grav.lua    # *.grav -> filetype=grav
├── ftplugin/grav.vim    # comments, indentation
├── lua/grav.lua         # LazyVim plugin spec: build/run keymaps + MCP server
└── README.md
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
| `gr`            | references to the symbol (project-wide quickfix)         |
| `gO`            | document outline (declarations in this file)             |
| `K`             | peek the definition line in a float                      |
| `]]` / `[[`     | jump to next / previous top-level declaration            |
| `<leader>mr`    | compile to a binary and run it                           |
| `<leader>mc`    | transpile to C and show the output                       |

**Go to definition** is LSP-free (pure Lua + grep). It understands both plain
names (variables, parameters, functions, types) and member accesses in chains —
`self.name`, `ptr->name`, `ptr->object.name`, `a.b.c` — resolving the segment
under the cursor to its field / method / variable declaration (current buffer
first, then project-wide).

**Inline C** (`%{ … %}`) is highlighted with the real C syntax, and `${ … }`
string interpolation highlights its inner tokens.

### MCP server

The `lua/grav.lua` spec registers the dependency-free `gravc` MCP server with
`mcphub.nvim` (if installed), pointing at `mcp_server/server.py` and exporting the
`GRAVC` binary path. See [`../../mcp_server/README.md`](../../mcp_server/README.md).
