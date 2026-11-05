// Grav VS Code extension. Language features run in-process on the shared,
// dependency-free index (grav-index.js, a copy of editors/lsp/grav-index.js):
// it follows imports into the standard library, project paths and installed
// packages. Diagnostics come from `grav check --json`, formatting from `grav fmt`.
// Plain CommonJS: no build step and no npm dependencies.
"use strict";

const vscode = require("vscode");
const path = require("path");
const idx = require("./grav-index");

const SEL = { language: "grav" };
let ws = null;
let diagnostics = null;

function compilerPath() {
  return vscode.workspace.getConfiguration("grav").get("compilerPath", "grav");
}
function workspace() {
  if (!ws) {
    const folder = (vscode.workspace.workspaceFolders || [])[0];
    ws = new idx.Workspace({ root: folder ? folder.uri.fsPath : null, compiler: compilerPath() });
    for (const doc of vscode.workspace.textDocuments) if (doc.languageId === "grav") ws.setDoc(doc.fileName, doc.getText());
  }
  return ws;
}
const pos = (p) => ({ line: p.line, character: p.character });
const toRange = (r) => new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character);
const toLocation = (d) => new vscode.Location(vscode.Uri.file(d.file), toRange(d.range));
const SYMBOL_KIND = (k) => Math.max(0, k - 1); // LSP SymbolKind -> vscode.SymbolKind
const COMPLETION_KIND = (k) => Math.max(0, k - 1);

// ---- commands -------------------------------------------------------------
function terminal() {
  const t = vscode.window.terminals.find((x) => x.name === "Grav") || vscode.window.createTerminal("Grav");
  t.show(true);
  return t;
}
function quote(s) { return "'" + s.replace(/'/g, "'\\''") + "'"; }
async function withFile(run) {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "grav") {
    vscode.window.showWarningMessage("Grav: open a .grav file first.");
    return;
  }
  if (editor.document.isDirty) await editor.document.save();
  run(editor.document.fileName);
}
function inProject(cmd) {
  const folder = (vscode.workspace.workspaceFolders || [])[0];
  const t = terminal();
  if (folder) t.sendText(`cd ${quote(folder.uri.fsPath)}`);
  t.sendText(`${compilerPath()} ${cmd}`);
}

const COMMANDS = {
  "grav.run": () => withFile((f) => terminal().sendText(`${compilerPath()} run ${quote(f)}`)),
  "grav.build": () => withFile((f) => terminal().sendText(`${compilerPath()} build ${quote(f)}`)),
  "grav.emitC": () => withFile((f) => terminal().sendText(`${compilerPath()} ${quote(f)} --emit c`)),
  "grav.test": () => withFile((f) => terminal().sendText(`${compilerPath()} test ${quote(f)}`)),
  "grav.check": () => withFile((f) => checkFile(f, true)),
  "grav.fmt": () => vscode.commands.executeCommand("editor.action.formatDocument"),
  "grav.runProject": () => inProject("run"),
  "grav.testProject": () => inProject("test"),
  "grav.newProject": async () => {
    const name = await vscode.window.showInputBox({ prompt: "Project name (a new folder)", placeHolder: "my-app" });
    if (name) inProject(`new ${quote(name)}`);
  },
  "grav.addPackage": async () => {
    const src = await vscode.window.showInputBox({
      prompt: "Package source: a git repository, optionally @version",
      placeHolder: "github.com/owner/repo@v1.0.0",
    });
    if (src) inProject(`add ${quote(src)}`);
  },
  "grav.installPackages": () => inProject("install"),
};

// ---- diagnostics ----------------------------------------------------------
function checkFile(file, announce) {
  workspace().check(file, (diags) => {
    const byFile = new Map([[path.resolve(file), []]]);
    for (const d of diags) {
      const f = path.resolve(d.file);
      if (!byFile.has(f)) byFile.set(f, []);
      const r = new vscode.Range(d.line, d.character, d.line, d.character + 1);
      const item = new vscode.Diagnostic(r, d.message,
        d.severity === "error" ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning);
      item.source = "grav";
      byFile.get(f).push(item);
    }
    for (const [f, list] of byFile) diagnostics.set(vscode.Uri.file(f), list);
    if (announce) {
      const errors = diags.filter((d) => d.severity === "error").length;
      vscode.window.setStatusBarMessage(errors ? `Grav: ${errors} error(s)` : "Grav: no errors", 4000);
    }
  });
}

// ---- providers ------------------------------------------------------------
const providers = {
  definition: {
    provideDefinition(doc, p) { return workspace().definition(doc.fileName, pos(p)).map(toLocation); },
  },
  references: {
    provideReferences(doc, p) { return workspace().references(doc.fileName, pos(p), false).map(toLocation); },
  },
  rename: {
    prepareRename(doc, p) {
      const w = idx.wordAtPosition(doc.getText(), pos(p));
      if (!w || idx.KEYWORDS.includes(w.word)) throw new Error("You cannot rename this element.");
      return toRange(w.range);
    },
    provideRenameEdits(doc, p, newName) {
      const edit = new vscode.WorkspaceEdit();
      for (const r of workspace().references(doc.fileName, pos(p), false))
        edit.replace(vscode.Uri.file(r.file), toRange(r.range), newName);
      return edit;
    },
  },
  symbols: {
    provideDocumentSymbols(doc) {
      const conv = (n) => {
        const s = new vscode.DocumentSymbol(n.name, n.detail || "", SYMBOL_KIND(n.kind), toRange(n.range), toRange(n.selectionRange));
        s.children = (n.children || []).map(conv);
        return s;
      };
      return workspace().outline(doc.fileName).map(conv);
    },
  },
  workspaceSymbols: {
    provideWorkspaceSymbols(query) {
      return workspace().workspaceSymbols(query, true).map(({ file, sym }) =>
        new vscode.SymbolInformation(sym.name, SYMBOL_KIND(sym.kind), sym.container || sym.namespace || "",
          new vscode.Location(vscode.Uri.file(file), toRange(sym.range))));
    },
  },
  hover: {
    provideHover(doc, p) {
      const h = workspace().hover(doc.fileName, pos(p));
      return h ? new vscode.Hover(new vscode.MarkdownString(h.markdown), toRange(h.range)) : null;
    },
  },
  completion: {
    provideCompletionItems(doc, p) {
      return workspace().completion(doc.fileName, pos(p)).map((c) => {
        const item = new vscode.CompletionItem(c.label, COMPLETION_KIND(c.kind));
        item.detail = c.detail;
        if (c.documentation) item.documentation = new vscode.MarkdownString(c.documentation);
        if (c.insertText) item.insertText = c.snippet ? new vscode.SnippetString(c.insertText) : c.insertText;
        if (c.sortText) item.sortText = c.sortText;
        if (c.filterText) item.filterText = c.filterText;
        return item;
      });
    },
  },
  signature: {
    provideSignatureHelp(doc, p) {
      const sh = workspace().signature(doc.fileName, pos(p));
      if (!sh) return null;
      const help = new vscode.SignatureHelp();
      help.signatures = sh.signatures.map((s) => {
        const info = new vscode.SignatureInformation(s.label, s.documentation || undefined);
        info.parameters = s.parameters.map((x) => new vscode.ParameterInformation(x.label));
        return info;
      });
      help.activeSignature = 0;
      help.activeParameter = sh.activeParameter;
      return help;
    },
  },
  formatting: {
    provideDocumentFormattingEdits(doc) {
      const text = doc.getText();
      const out = workspace().format(text);
      if (out == null) {
        vscode.window.showWarningMessage(`Grav: formatting needs the compiler (${compilerPath()} fmt).`);
        return [];
      }
      if (out === text) return [];
      return [vscode.TextEdit.replace(new vscode.Range(0, 0, doc.lineCount, 0), out)];
    },
  },
};

function activate(context) {
  diagnostics = vscode.languages.createDiagnosticCollection("grav");
  const subs = context.subscriptions;
  subs.push(diagnostics);
  for (const [name, fn] of Object.entries(COMMANDS)) subs.push(vscode.commands.registerCommand(name, fn));
  subs.push(
    vscode.languages.registerDefinitionProvider(SEL, providers.definition),
    vscode.languages.registerReferenceProvider(SEL, providers.references),
    vscode.languages.registerRenameProvider(SEL, providers.rename),
    vscode.languages.registerDocumentSymbolProvider(SEL, providers.symbols),
    vscode.languages.registerWorkspaceSymbolProvider(providers.workspaceSymbols),
    vscode.languages.registerHoverProvider(SEL, providers.hover),
    vscode.languages.registerCompletionItemProvider(SEL, providers.completion, ".", "\"", "/"),
    vscode.languages.registerSignatureHelpProvider(SEL, providers.signature, "(", ","),
    vscode.languages.registerDocumentFormattingEditProvider(SEL, providers.formatting),
    vscode.workspace.onDidOpenTextDocument((d) => {
      if (d.languageId !== "grav") return;
      workspace().setDoc(d.fileName, d.getText());
      if (vscode.workspace.getConfiguration("grav").get("checkOnSave", true)) checkFile(d.fileName, false);
    }),
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (e.document.languageId === "grav") workspace().setDoc(e.document.fileName, e.document.getText());
    }),
    vscode.workspace.onDidSaveTextDocument((d) => {
      if (d.languageId === "grav" && vscode.workspace.getConfiguration("grav").get("checkOnSave", true)) checkFile(d.fileName, false);
    }),
    vscode.workspace.onDidCloseTextDocument((d) => { if (d.languageId === "grav") workspace().closeDoc(d.fileName); }),
    vscode.workspace.onDidChangeConfiguration((e) => { if (e.affectsConfiguration("grav")) ws = null; }),
  );
  for (const d of vscode.workspace.textDocuments) if (d.languageId === "grav") checkFile(d.fileName, false);
}

function deactivate() {}

module.exports = { activate, deactivate };
