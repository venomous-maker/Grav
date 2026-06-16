#!/usr/bin/env python3
"""Claude MCP server for the Grav compiler (gravc).

Exposes gravc to an MCP client (e.g. Claude Code / Claude Desktop) as a small set
of tools: transpile Grav to C, type-check a snippet, and compile-and-run a
program. Speaks the MCP stdio transport (newline-delimited JSON-RPC 2.0).

Deliberately dependency-free — it uses only the Python standard library, so it
runs with a bare `python3` and needs no `pip install`. Run it directly
(`python3 mcp_server/server.py`) or register it via the repo's .mcp.json.

The gravc binary is located via the GRAVC environment variable, falling back to
`<repo>/build/gravc`. Build it first with `cmake -S . -B build && cmake --build build`.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SERVER_NAME = "gravc"
SERVER_VERSION = "0.1.0"
# Protocol version we fall back to if the client doesn't pin one.
DEFAULT_PROTOCOL = "2024-11-05"

# Repo root is the parent of this file's directory (…/mcp_server/server.py).
REPO_ROOT = Path(__file__).resolve().parent.parent

# How long any gravc / compiled-program invocation may run before we give up.
TIMEOUT_S = 30


# ---------------------------------------------------------------------------
# gravc plumbing
# ---------------------------------------------------------------------------
# Where the compiler is installed for general use (on PATH as `grav`). Override
# the directory with GRAV_DIR; defaults to ~/.local/bin.
INSTALL_DIR = Path(os.environ.get("GRAV_DIR") or "~/.local/bin").expanduser()
INSTALL_PATH = INSTALL_DIR / "grav"


def _sync_install() -> None:
    """Copy the freshly built compiler to ~/.local/bin/grav (kept up to date).

    The MCP server always installs the latest build executable as `grav` so that
    editors, shells, and the nvim integration can invoke a stable path instead of
    the in-repo build dir.
    """
    built = REPO_ROOT / "build" / "gravc"
    if not built.exists():
        return
    try:
        if (not INSTALL_PATH.exists()
                or INSTALL_PATH.stat().st_mtime < built.stat().st_mtime):
            INSTALL_DIR.mkdir(parents=True, exist_ok=True)
            shutil.copy2(built, INSTALL_PATH)
            INSTALL_PATH.chmod(0o755)
    except OSError:
        pass  # best effort; fall back to the build dir below


def _gravc() -> Path:
    """Resolve the compiler, preferring $GRAVC, then ~/.local/bin/grav, then build."""
    _sync_install()
    env = os.environ.get("GRAVC")
    if env:
        return Path(env)
    if INSTALL_PATH.exists():
        return INSTALL_PATH
    return REPO_ROOT / "build" / "gravc"


def _ensure_built() -> str | None:
    """Return an error string if the compiler is missing, else None."""
    binary = _gravc()
    if not binary.exists():
        return (
            f"the Grav compiler was not found at '{binary}'. Build it with "
            "`cmake -S . -B build && cmake --build build` (the MCP server then "
            "installs it as `grav` in ~/.local/bin), or set the GRAVC env var."
        )
    return None


def _run(args: list[str], *, stdin: str | None = None, cwd: Path | None = None):
    """Run a subprocess, returning (returncode, stdout, stderr)."""
    proc = subprocess.run(
        args,
        input=stdin,
        capture_output=True,
        text=True,
        timeout=TIMEOUT_S,
        cwd=str(cwd) if cwd else None,
    )
    return proc.returncode, proc.stdout, proc.stderr


def grav_to_c(source: str) -> str:
    """Transpile Grav source to C; on error, return the compiler diagnostics."""
    err = _ensure_built()
    if err:
        return err
    try:
        code, out, diag = _run([str(_gravc()), "-"], stdin=source, cwd=REPO_ROOT)
    except subprocess.TimeoutExpired:
        return f"gravc timed out after {TIMEOUT_S}s"
    if code != 0:
        return f"gravc failed:\n{diag.strip() or out.strip()}"
    return out


def grav_check(source: str) -> str:
    """Type-check Grav source; return 'ok: no errors' or the diagnostics."""
    err = _ensure_built()
    if err:
        return err
    try:
        code, _out, diag = _run([str(_gravc()), "-"], stdin=source, cwd=REPO_ROOT)
    except subprocess.TimeoutExpired:
        return f"gravc timed out after {TIMEOUT_S}s"
    if code != 0:
        return diag.strip() or "gravc reported an error"
    return "ok: no errors"


def grav_run(source: str) -> str:
    """Compile a Grav program to a native binary and run it; return its output."""
    err = _ensure_built()
    if err:
        return err
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "program.grav"
        binary = Path(tmp) / "program"
        src.write_text(source)
        try:
            code, out, diag = _run(
                [str(_gravc()), str(src), "--emit", "bin", "-o", str(binary)],
                cwd=REPO_ROOT,
            )
        except subprocess.TimeoutExpired:
            return f"gravc timed out after {TIMEOUT_S}s"
        if code != 0:
            return f"build failed:\n{diag.strip() or out.strip()}"
        try:
            rcode, rout, rerr = _run([str(binary)])
        except subprocess.TimeoutExpired:
            return f"program timed out after {TIMEOUT_S}s"
        result = rout
        if rerr.strip():
            result += ("\n" if result else "") + "[stderr] " + rerr.strip()
        if rcode != 0:
            result += f"\n[exit code {rcode}]"
        return result or "(no output)"


# Tool registry: name -> (handler, description, input schema).
_SOURCE_SCHEMA = {
    "type": "object",
    "properties": {
        "source": {"type": "string", "description": "Grav source text."}
    },
    "required": ["source"],
}

TOOLS = {
    "grav_to_c": (
        grav_to_c,
        "Transpile Grav source to C and return the generated C code "
        "(or the compiler diagnostics on error).",
        _SOURCE_SCHEMA,
    ),
    "grav_check": (
        grav_check,
        "Type-check Grav source without emitting code. Returns "
        "'ok: no errors' or the diagnostics with [line:col] locations.",
        _SOURCE_SCHEMA,
    ),
    "grav_run": (
        grav_run,
        "Compile a complete Grav program (with fn main) to a native binary "
        "and run it, returning its output. Needs a C compiler on PATH.",
        _SOURCE_SCHEMA,
    ),
}


# ---------------------------------------------------------------------------
# Minimal MCP stdio JSON-RPC server
# ---------------------------------------------------------------------------
def _result(req_id, result):
    return {"jsonrpc": "2.0", "id": req_id, "result": result}


def _error(req_id, code, message):
    return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}


def _handle(req: dict):
    """Dispatch a single JSON-RPC request; return a response dict or None."""
    method = req.get("method")
    req_id = req.get("id")
    params = req.get("params") or {}

    # Notifications (no id) get no response.
    if method == "notifications/initialized" or req_id is None:
        return None

    if method == "initialize":
        protocol = params.get("protocolVersion", DEFAULT_PROTOCOL)
        return _result(
            req_id,
            {
                "protocolVersion": protocol,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            },
        )

    if method == "ping":
        return _result(req_id, {})

    if method == "tools/list":
        tools = [
            {"name": name, "description": desc, "inputSchema": schema}
            for name, (_fn, desc, schema) in TOOLS.items()
        ]
        return _result(req_id, {"tools": tools})

    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        entry = TOOLS.get(name)
        if entry is None:
            return _error(req_id, -32602, f"unknown tool '{name}'")
        fn = entry[0]
        try:
            text = fn(**args)
        except TypeError as exc:
            return _error(req_id, -32602, f"invalid arguments: {exc}")
        except Exception as exc:  # surface tool failures as tool errors
            return _result(
                req_id,
                {"content": [{"type": "text", "text": str(exc)}], "isError": True},
            )
        return _result(req_id, {"content": [{"type": "text", "text": text}]})

    return _error(req_id, -32601, f"method not found: {method}")


def main() -> None:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue
        response = _handle(req)
        if response is not None:
            sys.stdout.write(json.dumps(response) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
