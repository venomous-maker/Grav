# gravc MCP server

A small [Model Context Protocol](https://modelcontextprotocol.io) server that
exposes the Grav compiler (`gravc`) to an MCP client such as Claude Code or
Claude Desktop. It lets the assistant transpile, type-check, and run Grav code
without leaving the conversation.

It is **dependency-free**: a single Python file using only the standard library,
speaking the MCP stdio transport (newline-delimited JSON-RPC 2.0). No
`pip install` required.

## Tools

| Tool          | What it does                                                        |
|---------------|--------------------------------------------------------------------|
| `grav_to_c`   | Transpile Grav source to C and return the generated C.             |
| `grav_check`  | Type-check a snippet; returns `ok: no errors` or diagnostics.      |
| `grav_run`    | Compile a program to a native binary and run it; returns output.   |

Each tool takes a single `source` string (Grav source text).

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
