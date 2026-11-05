# gravc MCP server

A small [Model Context Protocol](https://modelcontextprotocol.io) server that
exposes the Grav compiler (`gravc`) to an MCP client such as Claude Code or
Claude Desktop. It lets the assistant transpile, type-check, and run Grav code
without leaving the conversation.

It is **dependency-free**: a single Python file using only the standard library,
speaking the MCP stdio transport (newline-delimited JSON-RPC 2.0). No
`pip install` required.

## Tools

| Tool              | What it does                                                                 |
|-------------------|------------------------------------------------------------------------------|
| `grav_to_c`       | Transpile Grav source to C and return the generated C.                       |
| `grav_check`      | Type-check inline `source`, a `.grav` file, or a project directory (`path`). |
| `grav_run`        | Build and run a program (inline, file, or project) with `args` and `stdin`.  |
| `grav_test`       | Run the `test` blocks of a program or project (optional `filter`).           |
| `grav_lib_search` | Find standard-library classes, functions, and methods by name or doc text.   |
| `grav_lib_module` | Show a module's documentation and declarations (`std/net/http`, `json`, …).  |
| `grav_docs`       | Search the documentation (`docs/`: guide, reference, std pages) by topic.    |
| `grav_packages`   | `list` / `add` / `install` / `update` / `remove` packages, or `new` project. |

`grav_check`, `grav_run` and `grav_test` take either `source` (Grav text) or
`path`. A directory path means the project there: its `grav.toml` main file,
import paths and dependencies apply.

## Prerequisites

- Build the compiler first:
  ```bash
  cmake -S . -B build && cmake --build build
  ```
- Python 3.10+ (standard library only).
- `grav_run` also needs a C compiler on `PATH` (or `$CC`).

The server finds `gravc` via the `GRAVC` environment variable, falling back to
`<repo>/build/gravc`.

## Use with Claude Code

This repo ships a project-scoped [`.mcp.json`](../.mcp.json) that registers the
server, so from the repo root Claude Code will offer to start it automatically.
Verify it with:

```bash
claude mcp list
```

## Run manually

```bash
python3 mcp_server/server.py     # speaks MCP over stdio
```

## Smoke test

Drive a request straight in (the server reads one JSON-RPC message per line):

```bash
printf '%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{}}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"grav_run","arguments":{"source":"fn main(){ print(21 + 21) }"}}}' \
  | python3 mcp_server/server.py
```

## Use from Neovim (LazyVim + mcphub.nvim)

The repo ships a Neovim integration under [`editors/nvim/`](../editors/nvim/)
(syntax, filetype, build/run keymaps) and registers this server with
[mcphub.nvim](https://github.com/ravitemer/mcphub.nvim). A global server registry
is written to `~/.config/mcphub/servers.json`:

```json
{
  "mcpServers": {
    "gravc": {
      "command": "python3",
      "args": ["/abs/path/to/mcp_server/server.py"],
      "env": { "GRAVC": "/abs/path/to/build/gravc" }
    }
  }
}
```

Open Neovim and run `:MCPHub` to see the `gravc` tools (`grav_run`, `grav_to_c`,
`grav_check`). The `GRAVC` env var points the server at your built compiler.
