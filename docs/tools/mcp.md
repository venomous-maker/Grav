# The MCP server

`mcp_server/server.py` is a [Model Context Protocol](https://modelcontextprotocol.io)
server that gives an AI assistant (such as Claude Code or Claude Desktop) access to
the Grav compiler. The assistant can then check, run and test Grav code, and look
things up in the standard library and these docs, instead of guessing.

It is a single Python file using only the standard library.

## Tools

| Tool | Does |
|------|------|
| `grav_to_c` | translates Grav source to C |
| `grav_check` | type-checks source text, a file, or a project folder |
| `grav_run` | builds and runs a program, with arguments and standard input |
| `grav_test` | runs `test` blocks, optionally filtered |
| `grav_lib_search` | finds standard-library classes, functions and methods by name or doc text |
| `grav_lib_module` | shows a module's documentation and declarations |
| `grav_docs` | searches this documentation by topic |
| `grav_packages` | lists, adds, installs, updates or removes packages, or creates a project |

## Setup

Build the compiler first. In this repository, a project-scoped `.mcp.json`
already registers the server, so Claude Code offers to start it when run from the
repository root (`claude mcp list` shows it).

The server finds the compiler through the `GRAVC` environment variable, falling
back to `build/gravc` in the repository. `grav_run` also needs a C compiler. See
[`mcp_server/README.md`](../../mcp_server/README.md) for other clients.
