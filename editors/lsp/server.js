#!/usr/bin/env node
// server.js — a dependency-free Language Server for Grav.
//
// Speaks LSP over stdio (no npm packages). Built on grav-index.js, it follows
// imports into the standard library, project paths and installed packages, and
// provides:
//   • go to definition / declaration (also on import strings: opens the module)
//   • hover with signatures, doc comments and the declaring module
//   • completion: members after `.` (by inferred type), namespace members,
//     visible declarations, keywords, builtins, and import paths
//   • signature help for calls and constructors
//   • document outline (nested), workspace symbols (including the library)
//   • find references, rename
//   • inlay hints: the types of bindings that do not write one (let, for-in,
//     lambda parameters, match bindings), from `grav check --types`
//   • formatting (`grav fmt -`)
//   • diagnostics from `grav check --json` on open and save
//
// Used by Neovim (built-in LSP), JetBrains (LSP API), and any LSP client. Set the
// compiler with GRAV_COMPILER or the `compilerPath` initialization option.

"use strict";

const fs = require("fs");
const path = require("path");
const idx = require("./grav-index");

// ---- transport ------------------------------------------------------------
let buffer = Buffer.alloc(0);
process.stdin.on("data", (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  for (;;) {
    const headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd === -1) return;
    const header = buffer.slice(0, headerEnd).toString("ascii");
    const m = /Content-Length:\s*(\d+)/i.exec(header);
    if (!m) { buffer = buffer.slice(headerEnd + 4); continue; }
    const len = parseInt(m[1], 10);
    const start = headerEnd + 4;
    if (buffer.length < start + len) return;
    const body = buffer.slice(start, start + len).toString("utf8");
    buffer = buffer.slice(start + len);
    let msg;
    try { msg = JSON.parse(body); } catch (e) { log("parse error: " + e); continue; }
    try { handle(msg); } catch (e) {
      log("error handling " + msg.method + ": " + (e && e.stack || e));
      if (msg.id !== undefined) send({ jsonrpc: "2.0", id: msg.id, error: { code: -32603, message: String(e) } });
    }
  }
});

function send(msg) {
  const payload = Buffer.from(JSON.stringify(msg), "utf8");
  process.stdout.write(`Content-Length: ${payload.length}\r\n\r\n`);
  process.stdout.write(payload);
}
function reply(id, result) { send({ jsonrpc: "2.0", id, result }); }
function notify(method, params) { send({ jsonrpc: "2.0", method, params }); }
function log(s) { process.stderr.write(`[grav-lsp] ${s}\n`); }

// ---- state ----------------------------------------------------------------
let ws = new idx.Workspace({ compiler: process.env.GRAV_COMPILER || "grav", log });
const published = new Map(); // uri -> true (files we sent diagnostics for)
let refreshHints = false;     // the client takes workspace/inlayHint/refresh
let nextRequest = 1;

function uriToPath(uri) { return decodeURIComponent(uri.replace(/^file:\/\//, "")); }
function pathToUri(p) { return "file://" + path.resolve(p).split(path.sep).map(encodeURIComponent).join("/").replace(/%2F/g, "/"); }
const loc = (d) => ({ uri: pathToUri(d.file), range: d.range });

// ---- dispatch -------------------------------------------------------------
function handle(msg) {
  const { id, method, params } = msg;
  if (!method) return; // a response to one of our requests
  switch (method) {
    case "initialize": return onInitialize(id, params);
    case "initialized": return;
    case "shutdown": return reply(id, null);
    case "exit": return process.exit(0);
    case "textDocument/didOpen": ws.setDoc(uriToPath(params.textDocument.uri), params.textDocument.text); return diagnose(params.textDocument.uri);
    case "textDocument/didChange": {
      const change = params.contentChanges[params.contentChanges.length - 1];
      if (change) ws.setDoc(uriToPath(params.textDocument.uri), change.text);
      return;
    }
    case "textDocument/didSave": return diagnose(params.textDocument.uri);
    case "textDocument/didClose": return ws.closeDoc(uriToPath(params.textDocument.uri));
    case "textDocument/definition":
    case "textDocument/declaration":
      return reply(id, ws.definition(uriToPath(params.textDocument.uri), params.position).map(loc));
    case "textDocument/references":
      return reply(id, ws.references(uriToPath(params.textDocument.uri), params.position, false).map(loc));
    case "textDocument/documentSymbol": return reply(id, ws.outline(uriToPath(params.textDocument.uri)));
    case "workspace/symbol":
      return reply(id, ws.workspaceSymbols(params.query || "", true).map(({ file, sym }) => ({
        name: sym.name, kind: sym.kind, containerName: sym.container || sym.namespace || "",
        location: { uri: pathToUri(file), range: sym.range },
      })));
    case "textDocument/hover": {
      const h = ws.hover(uriToPath(params.textDocument.uri), params.position);
      return reply(id, h ? { contents: { kind: "markdown", value: h.markdown }, range: h.range } : null);
    }
    case "textDocument/completion": {
      const items = ws.completion(uriToPath(params.textDocument.uri), params.position).map((c) => ({
        label: c.label, kind: c.kind, detail: c.detail,
        documentation: c.documentation ? { kind: "markdown", value: c.documentation } : undefined,
        insertText: c.insertText, filterText: c.filterText, sortText: c.sortText,
        insertTextFormat: c.snippet ? 2 : 1,   // 2: snippet syntax ($1, ${2:name})
      }));
      return reply(id, { isIncomplete: false, items });
    }
    case "textDocument/signatureHelp": {
      const sh = ws.signature(uriToPath(params.textDocument.uri), params.position);
      return reply(id, sh ? { signatures: sh.signatures, activeSignature: 0, activeParameter: sh.activeParameter } : null);
    }
    case "textDocument/prepareRename": {
      const text = ws.text(uriToPath(params.textDocument.uri)) || "";
      const w = idx.wordAtPosition(text, params.position);
      return reply(id, w && !idx.KEYWORDS.includes(w.word) ? w.range : null);
    }
    case "textDocument/rename": {
      const changes = {};
      for (const r of ws.references(uriToPath(params.textDocument.uri), params.position, false)) {
        const u = pathToUri(r.file);
        (changes[u] = changes[u] || []).push({ range: r.range, newText: params.newName });
      }
      return reply(id, { changes });
    }
    case "textDocument/inlayHint": {
      const r = params.range;
      return reply(id, ws.inlayHints(uriToPath(params.textDocument.uri), r.start.line, r.end.line)
        .map((h) => ({ position: h.position, label: h.label, kind: h.kind, paddingLeft: false })));
    }
    case "textDocument/formatting": {
      const file = uriToPath(params.textDocument.uri);
      const text = ws.text(file) || "";
      const out = ws.format(text);
      if (out == null || out === text) return reply(id, []);
      const lines = idx.splitLines(text);
      return reply(id, [{ range: { start: { line: 0, character: 0 }, end: { line: lines.length, character: 0 } }, newText: out }]);
    }
    default: if (id !== undefined) reply(id, null);
  }
}

function onInitialize(id, params) {
  const root = params.rootPath ||
    (params.rootUri ? uriToPath(params.rootUri) :
      (params.workspaceFolders && params.workspaceFolders[0] ? uriToPath(params.workspaceFolders[0].uri) : null));
  const opts = params.initializationOptions || {};
  const caps = params.capabilities || {};
  refreshHints = !!(caps.workspace && caps.workspace.inlayHint && caps.workspace.inlayHint.refreshSupport);
  ws = new idx.Workspace({ root, compiler: opts.compilerPath || process.env.GRAV_COMPILER || "grav", log });
  reply(id, {
    capabilities: {
      textDocumentSync: { openClose: true, change: 1, save: { includeText: false } },
      definitionProvider: true,
      declarationProvider: true,
      referencesProvider: true,
      documentSymbolProvider: true,
      workspaceSymbolProvider: true,
      hoverProvider: true,
      completionProvider: { triggerCharacters: [".", "\"", "/"] },
      signatureHelpProvider: { triggerCharacters: ["(", ","] },
      renameProvider: { prepareProvider: true },
      documentFormattingProvider: true,
      inlayHintProvider: true,
    },
    serverInfo: { name: "grav-lsp", version: "1.6.0" },
  });
}

// ---- diagnostics (grav check --json) ---------------------------------------
function diagnose(uri) {
  const file = uriToPath(uri);
  if (!file.endsWith(".grav") || !fs.existsSync(file)) return;
  ws.check(file, (diags) => {
    const byFile = new Map([[path.resolve(file), []]]);
    for (const d of diags) {
      const f = path.resolve(d.file);
      if (!byFile.has(f)) byFile.set(f, []);
      byFile.get(f).push({
        range: { start: { line: d.line, character: d.character }, end: { line: d.line, character: d.character + 1 } },
        severity: d.severity === "error" ? 1 : 2, source: "grav", message: d.message,
      });
    }
    // Clear files that had diagnostics from this file's last check but none now.
    for (const [u] of published) if (!byFile.has(uriToPath(u)) && u.startsWith("file://")) {
      notify("textDocument/publishDiagnostics", { uri: u, diagnostics: [] });
      published.delete(u);
    }
    for (const [f, list] of byFile) {
      const u = pathToUri(f);
      notify("textDocument/publishDiagnostics", { uri: u, diagnostics: list });
      if (list.length) published.set(u, true); else published.delete(u);
    }
    // the check also brought the compiler's types of the file's bindings
    if (refreshHints) send({ jsonrpc: "2.0", id: "grav-" + nextRequest++, method: "workspace/inlayHint/refresh" });
  });
}

log("started");
