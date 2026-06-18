#!/usr/bin/env node
// server.js — a dependency-free Language Server for Grav.
//
// Speaks LSP over stdio (no npm packages). Provides:
//   • go-to-declaration   (textDocument/definition)
//   • find references      (textDocument/references)
//   • rename / refactor    (textDocument/rename, prepareRename)
//   • document outline     (textDocument/documentSymbol)
//   • hover                (textDocument/hover)
//   • diagnostics          (by running the `grav` compiler on open/save)
//
// Used by Neovim (built-in LSP) and CLion (LSP plugin API). VS Code uses the same
// index module directly via providers. Configure the compiler with the env var
// GRAV_COMPILER or the `compilerPath` initialization option (default: "grav").

"use strict";

const fs = require("fs");
const path = require("path");
const { execFile } = require("child_process");
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
    try { handle(JSON.parse(body)); } catch (e) { log("parse error: " + e); }
  }
});

function send(msg) {
  const json = JSON.stringify(msg);
  const payload = Buffer.from(json, "utf8");
  process.stdout.write(`Content-Length: ${payload.length}\r\n\r\n`);
  process.stdout.write(payload);
}
function reply(id, result) { send({ jsonrpc: "2.0", id, result }); }
function notify(method, params) { send({ jsonrpc: "2.0", method, params }); }
function log(s) { process.stderr.write(`[grav-lsp] ${s}\n`); }

// ---- state ----------------------------------------------------------------
const docs = new Map();          // uri -> { text, version }
let rootPath = null;
let compiler = process.env.GRAV_COMPILER || "grav";

function uriToPath(uri) {
  return decodeURIComponent(uri.replace(/^file:\/\//, ""));
}
function pathToUri(p) {
  return "file://" + p.split(path.sep).map(encodeURIComponent).join("/").replace(/%2F/g, "/");
}

// Text for a uri: prefer the in-memory (possibly unsaved) version.
function textOf(uri) {
  if (docs.has(uri)) return docs.get(uri).text;
  try { return fs.readFileSync(uriToPath(uri), "utf8"); } catch { return null; }
}

// Every .grav file in the workspace, plus any open docs outside it.
function workspaceUris() {
  const uris = new Set(docs.keys());
  if (rootPath) walk(rootPath, uris);
  return [...uris];
}
function walk(dir, uris) {
  let entries;
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const e of entries) {
    if (e.name.startsWith(".")) continue;
    if (["build", "node_modules", "cmake-build-debug", "cmake-build-release"].includes(e.name)) continue;
    const full = path.join(dir, e.name);
    if (e.isDirectory()) walk(full, uris);
    else if (e.isFile() && e.name.endsWith(".grav")) uris.add(pathToUri(full));
  }
}

// ---- dispatch -------------------------------------------------------------
function handle(msg) {
  const { id, method, params } = msg;
  switch (method) {
    case "initialize": return onInitialize(id, params);
    case "initialized": return;
    case "shutdown": return reply(id, null);
    case "exit": return process.exit(0);
    case "textDocument/didOpen": return onDidOpen(params);
    case "textDocument/didChange": return onDidChange(params);
    case "textDocument/didSave": return onDidSave(params);
    case "textDocument/didClose": return docs.delete(params.textDocument.uri);
    case "textDocument/definition": return onDefinition(id, params);
    case "textDocument/references": return onReferences(id, params);
    case "textDocument/documentSymbol": return onDocumentSymbol(id, params);
    case "textDocument/hover": return onHover(id, params);
    case "textDocument/prepareRename": return onPrepareRename(id, params);
    case "textDocument/rename": return onRename(id, params);
    default: if (id !== undefined) reply(id, null);
  }
}

function onInitialize(id, params) {
  rootPath = params.rootPath ||
    (params.rootUri ? uriToPath(params.rootUri) :
      (params.workspaceFolders && params.workspaceFolders[0]
        ? uriToPath(params.workspaceFolders[0].uri) : null));
  const opts = params.initializationOptions || {};
  if (opts.compilerPath) compiler = opts.compilerPath;
  reply(id, {
    capabilities: {
      textDocumentSync: 1, // full
      definitionProvider: true,
      referencesProvider: true,
      documentSymbolProvider: true,
      hoverProvider: true,
      renameProvider: { prepareProvider: true },
    },
    serverInfo: { name: "grav-lsp", version: "0.9.0" },
  });
}

// ---- document lifecycle ---------------------------------------------------
function onDidOpen(params) {
  const d = params.textDocument;
  docs.set(d.uri, { text: d.text, version: d.version });
  diagnose(d.uri);
}
function onDidChange(params) {
  const d = params.textDocument;
  const change = params.contentChanges[params.contentChanges.length - 1];
  if (change) docs.set(d.uri, { text: change.text, version: d.version });
}
function onDidSave(params) { diagnose(params.textDocument.uri); }

// ---- features -------------------------------------------------------------
function onDefinition(id, params) {
  const word = wordAt(params);
  if (!word) return reply(id, null);
  const locs = [];
  // Prefer declarations in the current document first.
  const here = params.textDocument.uri;
  for (const uri of [here, ...workspaceUris().filter((u) => u !== here)]) {
    const text = textOf(uri);
    if (text == null) continue;
    for (const range of idx.declarationsOf(text, word)) locs.push({ uri, range });
  }
  reply(id, locs.length === 1 ? locs[0] : locs);
}

function onReferences(id, params) {
  const word = wordAt(params);
  if (!word) return reply(id, null);
  const locs = [];
  for (const uri of workspaceUris()) {
    const text = textOf(uri);
    if (text == null) continue;
    for (const range of idx.referencesOf(text, word)) locs.push({ uri, range });
  }
  reply(id, locs);
}

function onDocumentSymbol(id, params) {
  const text = textOf(params.textDocument.uri) || "";
  const syms = idx.documentDeclarations(text).map((s) => ({
    name: s.name, kind: s.kind, range: s.range, selectionRange: s.selectionRange,
  }));
  reply(id, syms);
}

function onHover(id, params) {
  const word = wordAt(params);
  if (!word) return reply(id, null);
  for (const uri of workspaceUris()) {
    const text = textOf(uri);
    if (text == null) continue;
    const decls = idx.documentDeclarations(text).filter((s) => s.name === word);
    if (decls.length) {
      const line = idx.splitLines(text)[decls[0].range.start.line].trim();
      return reply(id, { contents: { kind: "markdown", value: "```grav\n" + line + "\n```" } });
    }
  }
  reply(id, null);
}

function onPrepareRename(id, params) {
  const w = idx.wordAtPosition(textOf(params.textDocument.uri) || "", params.position);
  reply(id, w ? w.range : null);
}

function onRename(id, params) {
  const word = wordAt(params);
  if (!word) return reply(id, null);
  const changes = {};
  for (const uri of workspaceUris()) {
    const text = textOf(uri);
    if (text == null) continue;
    const edits = idx.referencesOf(text, word).map((range) => ({ range, newText: params.newName }));
    if (edits.length) changes[uri] = edits;
  }
  reply(id, { changes });
}

function wordAt(params) {
  const text = textOf(params.textDocument.uri);
  if (text == null) return null;
  const w = idx.wordAtPosition(text, params.position);
  return w ? w.word : null;
}

// ---- diagnostics (delegate to the gravc compiler) -------------------------
function diagnose(uri) {
  const file = uriToPath(uri);
  if (!file.endsWith(".grav") || !fs.existsSync(file)) return;
  const out = path.join(require("os").tmpdir(), "grav_lsp_check.c");
  execFile(compiler, [file, "--emit", "c", "-o", out], { timeout: 15000 }, (err, stdout, stderr) => {
    const diags = parseDiagnostics(stderr || "", file);
    notify("textDocument/publishDiagnostics", { uri, diagnostics: diags });
  });
}

function parseDiagnostics(stderr, file) {
  const diags = [];
  const re = /^gravc:\s+(.+?):(\d+):(\d+):\s+(?:\w+\s+)?(error|warning):\s+(.*)$/;
  for (const line of stderr.split(/\r?\n/)) {
    const m = re.exec(line);
    if (!m) continue;
    if (path.resolve(m[1]) !== path.resolve(file)) continue; // only this file
    const ln = Math.max(0, parseInt(m[2], 10) - 1);
    const col = Math.max(0, parseInt(m[3], 10) - 1);
    diags.push({
      range: { start: { line: ln, character: col }, end: { line: ln, character: col + 1 } },
      severity: m[4] === "error" ? 1 : 2,
      source: "gravc",
      message: m[5],
    });
  }
  return diags;
}

log("started");
