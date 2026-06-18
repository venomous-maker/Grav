// Grav VS Code extension — build/run commands plus language features
// (go-to-definition, references, rename, document symbols, hover) implemented
// directly against the shared, dependency-free indexer. Plain CommonJS: no build
// step and no npm dependencies.
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const idx = require("../lsp/grav-index");

function compilerPath() {
  return vscode.workspace.getConfiguration("grav").get("compilerPath", "grav");
}

// ---- build / run commands -------------------------------------------------
function runInTerminal(makeArgs) {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "grav") {
    vscode.window.showWarningMessage("Grav: open a .grav file first.");
    return;
  }
  if (editor.document.isDirty) editor.document.save();
  const file = editor.document.fileName;
  const term =
    vscode.window.terminals.find((t) => t.name === "Grav") ||
    vscode.window.createTerminal("Grav");
  term.show(true);
  term.sendText(`${compilerPath()} ${makeArgs(file)}`);
}

// ---- shared document gathering --------------------------------------------
// Every .grav document in the workspace, in-memory versions preferred.
function allGravDocs() {
  const byPath = new Map();
  for (const doc of vscode.workspace.textDocuments) {
    if (doc.languageId === "grav" || doc.fileName.endsWith(".grav")) {
      byPath.set(doc.fileName, { uri: doc.uri, text: doc.getText() });
    }
  }
  for (const folder of vscode.workspace.workspaceFolders || []) {
    walk(folder.uri.fsPath, byPath);
  }
  return [...byPath.values()];
}
function walk(dir, byPath) {
  let entries;
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const e of entries) {
    if (e.name.startsWith(".")) continue;
    if (["build", "node_modules", "cmake-build-debug", "cmake-build-release"].includes(e.name)) continue;
    const full = path.join(dir, e.name);
    if (e.isDirectory()) walk(full, byPath);
    else if (e.isFile() && e.name.endsWith(".grav") && !byPath.has(full)) {
      try { byPath.set(full, { uri: vscode.Uri.file(full), text: fs.readFileSync(full, "utf8") }); } catch {}
    }
  }
}

function toRange(r) {
  return new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character);
}
function wordAt(document, position) {
  const w = idx.wordAtPosition(document.getText(), { line: position.line, character: position.character });
  return w ? w.word : null;
}

// ---- language feature providers -------------------------------------------
const definitionProvider = {
  provideDefinition(document, position) {
    const word = wordAt(document, position);
    if (!word) return null;
    const here = document.fileName;
    const docs = allGravDocs().sort((a, b) =>
      (a.uri.fsPath === here ? -1 : 0) - (b.uri.fsPath === here ? -1 : 0));
    const locs = [];
    for (const d of docs)
      for (const r of idx.declarationsOf(d.text, word)) locs.push(new vscode.Location(d.uri, toRange(r)));
    return locs;
  },
};

const referenceProvider = {
  provideReferences(document, position) {
    const word = wordAt(document, position);
    if (!word) return null;
    const locs = [];
    for (const d of allGravDocs())
      for (const r of idx.referencesOf(d.text, word)) locs.push(new vscode.Location(d.uri, toRange(r)));
    return locs;
  },
};

const renameProvider = {
  prepareRename(document, position) {
    const w = idx.wordAtPosition(document.getText(), { line: position.line, character: position.character });
    if (!w) throw new Error("You cannot rename this element.");
    return toRange(w.range);
  },
  provideRenameEdits(document, position, newName) {
    const word = wordAt(document, position);
    if (!word) return null;
    const edit = new vscode.WorkspaceEdit();
    for (const d of allGravDocs())
      for (const r of idx.referencesOf(d.text, word)) edit.replace(d.uri, toRange(r), newName);
    return edit;
  },
};

const documentSymbolProvider = {
  provideDocumentSymbols(document) {
    return idx.documentDeclarations(document.getText()).map((s) =>
      new vscode.SymbolInformation(
        s.name, (s.kind - 1), "", new vscode.Location(document.uri, toRange(s.range))));
  },
};

const hoverProvider = {
  provideHover(document, position) {
    const word = wordAt(document, position);
    if (!word) return null;
    for (const d of allGravDocs()) {
      const decls = idx.documentDeclarations(d.text).filter((s) => s.name === word);
      if (decls.length) {
        const line = idx.splitLines(d.text)[decls[0].range.start.line].trim();
        return new vscode.Hover({ language: "grav", value: line });
      }
    }
    return null;
  },
};

function activate(context) {
  const sel = { language: "grav" };
  context.subscriptions.push(
    vscode.commands.registerCommand("grav.run", () => runInTerminal((f) => `'${f}' --run`)),
    vscode.commands.registerCommand("grav.build", () => runInTerminal((f) => `'${f}' --emit c`)),
    vscode.languages.registerDefinitionProvider(sel, definitionProvider),
    vscode.languages.registerReferenceProvider(sel, referenceProvider),
    vscode.languages.registerRenameProvider(sel, renameProvider),
    vscode.languages.registerDocumentSymbolProvider(sel, documentSymbolProvider),
    vscode.languages.registerHoverProvider(sel, hoverProvider)
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
