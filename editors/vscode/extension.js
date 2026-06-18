// Grav VS Code extension — adds "compile" / "compile & run" commands on top of
// the declarative grammar + snippets. Plain CommonJS, so no build step is needed.
const vscode = require("vscode");

function compilerPath() {
  return vscode.workspace.getConfiguration("grav").get("compilerPath", "grav");
}

// Runs the compiler on the active .grav file in an integrated terminal.
function runInTerminal(label, makeArgs) {
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

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand("grav.run", () =>
      runInTerminal("run", (f) => `'${f}' --run`)
    ),
    vscode.commands.registerCommand("grav.build", () =>
      runInTerminal("build", (f) => `'${f}' --emit c`)
    )
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
