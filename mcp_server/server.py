#!/usr/bin/env python3
"""Claude MCP server for the Grav compiler (gravc).

Exposes gravc to an MCP client (e.g. Claude Code / Claude Desktop) as tools:
transpile Grav to C, type-check, compile-and-run, run `test` blocks, search the
standard library and the docs, and manage packages. Speaks the MCP stdio
transport (newline-delimited JSON-RPC 2.0).

Deliberately dependency-free — it uses only the Python standard library, so it
runs with a bare `python3` and needs no `pip install`. Run it directly
(`python3 mcp_server/server.py`) or register it via the repo's .mcp.json.

The gravc binary is located via the GRAVC environment variable, falling back to
`<repo>/build/gravc`. Build it first with `cmake -S . -B build && cmake --build build`.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SERVER_NAME = "gravc"
SERVER_VERSION = "0.2.0"
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


def _source_args(source: str | None, path: str | None, tmp: Path) -> tuple[list[str], Path | None, str | None]:
    """The gravc input for either inline `source` or a file/project `path`.

    Returns (args, cwd, error). A directory path means "the project there": gravc
    is run inside it so grav.toml (main, paths, dependencies) applies.
    """
    if path:
        p = Path(path).expanduser()
        if not p.exists():
            return [], None, f"no such file or directory: {p}"
        if p.is_dir():
            return [], p, None
        return [str(p.resolve())], p.parent, None
    if source is None:
        return [], None, "pass either `source` (Grav code) or `path` (a .grav file or project directory)"
    src = tmp / "program.grav"
    src.write_text(source)
    return [str(src)], REPO_ROOT, None


def _clip(text: str, limit: int = 60000) -> str:
    return text if len(text) <= limit else text[:limit] + f"\n... ({len(text) - limit} more characters)"


def grav_to_c(source: str) -> str:
    """Transpile Grav source to C; on error, return the compiler diagnostics."""
    err = _ensure_built()
    if err:
        return err
    try:
        code, out, diag = _run([str(_gravc()), "-", "--no-lines"], stdin=source, cwd=REPO_ROOT)
    except subprocess.TimeoutExpired:
        return f"gravc timed out after {TIMEOUT_S}s"
    if code != 0:
        return f"gravc failed:\n{diag.strip() or out.strip()}"
    return _clip(out)


def grav_check(source: str | None = None, path: str | None = None) -> str:
    """Type-check Grav source or a file/project; return 'ok: no errors' or diagnostics."""
    err = _ensure_built()
    if err:
        return err
    with tempfile.TemporaryDirectory() as tmp:
        args, cwd, e = _source_args(source, path, Path(tmp))
        if e:
            return e
        if not args and cwd:  # a project: check its main file
            args = ["build"]
        out_c = Path(tmp) / "check.c"
        cmd = [str(_gravc())] + (args if args[:1] == ["build"] else args + ["-o", str(out_c)])
        if args[:1] == ["build"]:
            cmd += ["-o", str(Path(tmp) / "check.bin")]
        try:
            code, out, diag = _run(cmd, cwd=cwd)
        except subprocess.TimeoutExpired:
            return f"gravc timed out after {TIMEOUT_S}s"
        warnings = "\n".join(l for l in diag.splitlines() if "warning" in l)
        if code != 0:
            return diag.strip() or out.strip() or "gravc reported an error"
        return "ok: no errors" + (f"\n{warnings}" if warnings else "")


def grav_run(source: str | None = None, path: str | None = None, args: list[str] | None = None,
             stdin: str | None = None) -> str:
    """Compile a Grav program (inline, a file, or a project) and run it; return its output."""
    err = _ensure_built()
    if err:
        return err
    with tempfile.TemporaryDirectory() as tmp:
        src_args, cwd, e = _source_args(source, path, Path(tmp))
        if e:
            return e
        binary = Path(tmp) / "program"
        cmd = [str(_gravc())] + (src_args if src_args else ["build"]) + ["--emit", "bin", "-o", str(binary)]
        if not src_args:
            cmd = [str(_gravc()), "build", "-o", str(binary)]
        try:
            code, out, diag = _run(cmd, cwd=cwd)
        except subprocess.TimeoutExpired:
            return f"gravc timed out after {TIMEOUT_S}s"
        if code != 0:
            return f"build failed:\n{diag.strip() or out.strip()}"
        try:
            rcode, rout, rerr = _run([str(binary)] + list(args or []), stdin=stdin, cwd=cwd)
        except subprocess.TimeoutExpired:
            return f"program timed out after {TIMEOUT_S}s"
        result = rout
        if rerr.strip():
            result += ("\n" if result else "") + "[stderr] " + rerr.strip()
        if rcode != 0:
            result += f"\n[exit code {rcode}]"
        return _clip(result) or "(no output)"


def grav_test(source: str | None = None, path: str | None = None, filter: str | None = None) -> str:
    """Run the `test` blocks of Grav source, a file, or a project; return the report."""
    err = _ensure_built()
    if err:
        return err
    with tempfile.TemporaryDirectory() as tmp:
        src_args, cwd, e = _source_args(source, path, Path(tmp))
        if e:
            return e
        cmd = [str(_gravc()), "test"] + src_args + (["--filter", filter] if filter else [])
        try:
            code, out, diag = _run(cmd, cwd=cwd)
        except subprocess.TimeoutExpired:
            return f"tests timed out after {TIMEOUT_S}s"
        text = (out + ("\n" + diag.strip() if diag.strip() else "")).strip()
        return _clip(text) + ("" if code == 0 else f"\n[exit code {code}]")


# ---------------------------------------------------------------------------
# Standard library and docs lookup
# ---------------------------------------------------------------------------
LIB_DIR = REPO_ROOT / "lib"
DOCS_DIR = REPO_ROOT / "docs"
_DECL = re.compile(r"^\s*(?:export\s+)?(?:final\s+|abstract\s+)?(?:public\s+|private\s+|protected\s+)?"
                   r"(?:static\s+)?(class|struct|enum|interface|trait|fn|async fn|type|const)\s+([A-Za-z_][A-Za-z0-9_]*)")


def _lib_files() -> list[Path]:
    return sorted(p for p in LIB_DIR.rglob("*.grav"))


def _module_name(p: Path) -> str:
    rel = p.relative_to(LIB_DIR).with_suffix("")
    return "std/" + "/".join(rel.parts)


def _declarations(p: Path) -> list[tuple[int, str, str, str, str]]:
    """(line, kind, name, signature, doc comment) for each declaration in a file."""
    out = []
    lines = p.read_text().splitlines()
    owner = ""
    for i, line in enumerate(lines):
        m = _DECL.match(line)
        if not m:
            continue
        kind, name = m.group(1), m.group(2)
        if kind in ("class", "struct", "enum", "interface", "trait"):
            owner = name
        doc = []
        j = i - 1
        while j >= 0 and lines[j].strip().startswith("//"):
            text = lines[j].strip().lstrip("/").strip()
            if not text.startswith("──"):  # section dividers are not documentation
                doc.insert(0, text)
            j -= 1
        sig = line.strip()
        sig = sig[: sig.index("{")].strip() if "{" in sig else sig
        qual = name if kind != "fn" or not owner or not line.startswith("        ") else f"{owner}.{name}"
        out.append((i + 1, kind, qual, sig, " ".join(doc)))
    return out


def grav_lib_search(query: str, limit: int = 30) -> str:
    """Find classes, functions, and methods in the standard library by name or doc text."""
    q = query.lower()
    hits = []
    for f in _lib_files():
        for line, kind, name, sig, doc in _declarations(f):
            score = 3 if name.lower() == q or name.lower().endswith("." + q) else \
                    2 if q in name.lower() else 1 if q in doc.lower() else 0
            if score:
                hits.append((-score, _module_name(f), line, sig, doc))
    if not hits:
        return f"nothing in the standard library matches '{query}'"
    hits.sort()
    rows = []
    for _s, mod, line, sig, doc in hits[:limit]:
        rows.append(f"{mod}:{line}  {sig}" + (f"\n    {doc}" if doc else ""))
    more = f"\n... and {len(hits) - limit} more" if len(hits) > limit else ""
    return "\n".join(rows) + more + '\n(import a category with import "std/<category>", or everything with import "std")'


def grav_lib_module(module: str) -> str:
    """List a standard-library module's declarations (e.g. 'std/net/http' or 'collections')."""
    name = module.removeprefix("std/").removesuffix(".grav")
    candidates = [LIB_DIR / f"{name}.grav"] + list(LIB_DIR.rglob(f"{Path(name).name}.grav"))
    for f in candidates:
        if f.exists():
            text = f.read_text()
            head = [l for l in text.splitlines()[:25] if l.startswith("//")]
            decls = _declarations(f)
            imports = [l.strip() for l in text.splitlines() if l.strip().startswith("import ")]
            body = "\n".join(head)
            if decls:
                body += "\n\ndeclarations:\n" + "\n".join(f"  {line:4} {sig}" for line, _k, _n, sig, _d in decls)
            elif imports:
                body += "\n\nincludes:\n" + "\n".join("  " + i for i in imports)
            return f"{_module_name(f)}  ({f})\n\n{body}"
    mods = sorted({_module_name(f) for f in _lib_files()})
    return f"no module '{module}'. Modules:\n" + "\n".join("  " + m for m in mods)


def grav_docs(query: str) -> str:
    """Search the Grav language documentation; return the best matching sections."""
    q = query.lower()
    sections = []
    for f in sorted(DOCS_DIR.rglob("*.md")):
        title, buf = f.stem, []
        for line in f.read_text().splitlines() + ["## "]:
            if line.startswith("## ") or line.startswith("# "):
                if buf:
                    text = "\n".join(buf)
                    score = (5 if q in title.lower() else 0) + text.lower().count(q)
                    if score:
                        sections.append((-score, str(f.relative_to(DOCS_DIR)), title, text))
                title, buf = line.lstrip("# ").strip(), [line]
            else:
                buf.append(line)
    if not sections:
        return f"no documentation section mentions '{query}'"
    sections.sort()
    return _clip("\n\n---\n\n".join(f"[{name}] {text}" for _s, name, _t, text in sections[:3]), 30000)


def grav_packages(action: str, project: str | None = None, source: str | None = None,
                  name: str | None = None) -> str:
    """Manage a project's packages: list | add | install | update | remove | new."""
    err = _ensure_built()
    if err:
        return err
    cwd = Path(project).expanduser() if project else Path.cwd()
    if action == "new":
        if not name:
            return "action 'new' needs `name` (the project directory to create)"
        cmd = [str(_gravc()), "new", name]
    elif action in ("list", "packages"):
        cmd = [str(_gravc()), "packages"]
    elif action == "add":
        if not source:
            return "action 'add' needs `source`, e.g. github.com/owner/repo@v1.0.0"
        cmd = [str(_gravc()), "add", source] + (["--name", name] if name else [])
    elif action in ("install", "update"):
        cmd = [str(_gravc()), action] + ([name] if name and action == "update" else [])
    elif action == "remove":
        if not name:
            return "action 'remove' needs `name`"
        cmd = [str(_gravc()), "remove", name]
    else:
        return "action must be one of: list, add, install, update, remove, new"
    try:
        code, out, diag = _run(cmd, cwd=cwd)
    except subprocess.TimeoutExpired:
        return f"grav {action} timed out after {TIMEOUT_S}s"
    text = (out + diag).strip() or "(done)"
    return text + ("" if code == 0 else f"\n[exit code {code}]")


# Tool registry: name -> (handler, description, input schema).
_SOURCE_SCHEMA = {
    "type": "object",
    "properties": {
        "source": {"type": "string", "description": "Grav source text."}
    },
    "required": ["source"],
}
_SOURCE_OR_PATH = {
    "source": {"type": "string", "description": "Grav source text (or use `path`)."},
    "path": {"type": "string", "description": "A .grav file, or a project directory with grav.toml."},
}


def _schema(props: dict, required: list[str] | None = None) -> dict:
    return {"type": "object", "properties": props, "required": required or []}


TOOLS = {
    "grav_to_c": (
        grav_to_c,
        "Transpile Grav source to C and return the generated C code "
        "(or the compiler diagnostics on error).",
        _SOURCE_SCHEMA,
    ),
    "grav_check": (
        grav_check,
        "Type-check Grav code without running it: inline `source`, a .grav file, or a "
        "project directory. Returns 'ok: no errors' (plus warnings) or the diagnostics "
        "with file:line:col locations.",
        _schema(_SOURCE_OR_PATH),
    ),
    "grav_run": (
        grav_run,
        "Compile a Grav program (inline `source`, a .grav file, or a project directory) "
        "to a native binary and run it with optional `args` and `stdin`; returns its output.",
        _schema({**_SOURCE_OR_PATH,
                 "args": {"type": "array", "items": {"type": "string"}, "description": "Program arguments."},
                 "stdin": {"type": "string", "description": "Text fed to the program's standard input."}}),
    ),
    "grav_test": (
        grav_test,
        "Run the `test \"name\" { ... }` blocks of Grav code (inline, a file, or a project) "
        "with `grav test`; returns the ok/FAIL report. `filter` runs only matching tests.",
        _schema({**_SOURCE_OR_PATH, "filter": {"type": "string", "description": "Substring of test names."}}),
    ),
    "grav_lib_search": (
        grav_lib_search,
        "Search the Grav standard library for classes, functions, and methods by name or "
        "documentation text (e.g. 'HashMap', 'websocket', 'parse json'). Returns module, "
        "line, signature, and doc comment.",
        _schema({"query": {"type": "string"}, "limit": {"type": "integer"}}, ["query"]),
    ),
    "grav_lib_module": (
        grav_lib_module,
        "Show a standard-library module: its header documentation and every declaration "
        "(e.g. 'std/net/http', 'collections/hashmap', 'json').",
        _schema({"module": {"type": "string"}}, ["module"]),
    ),
    "grav_docs": (
        grav_docs,
        "Search the Grav documentation (docs/: the guide, the reference, and a page per standard-library module) and return the best matching "
        "sections, e.g. 'pattern matching', 'grav.toml', 'closures'.",
        _schema({"query": {"type": "string"}}, ["query"]),
    ),
    "grav_packages": (
        grav_packages,
        "Manage third-party packages of a project (git repositories from GitHub, GitLab, any "
        "git host, or a local path, cached in ~/.local/grav/packages): action = list | add | "
        "install | update | remove | new. `source` is e.g. github.com/owner/repo@v1.2.0.",
        _schema({"action": {"type": "string", "enum": ["list", "add", "install", "update", "remove", "new"]},
                 "project": {"type": "string", "description": "Project directory (default: current)."},
                 "source": {"type": "string"},
                 "name": {"type": "string"}}, ["action"]),
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
