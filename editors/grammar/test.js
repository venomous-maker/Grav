#!/usr/bin/env node
// Tests the TextMate grammar with VS Code's own engine (vscode-textmate and
// vscode-oniguruma, found in an installed VS Code-based editor, or in
// $GRAV_TEXTMATE_MODULES).
//
//   node editors/grammar/test.js                 run the fixtures in editors/grammar/tests/
//   node editors/grammar/test.js --strings F...  report files where a "..." string runs past its line,
//                                                or a region is still open at the end
//   node editors/grammar/test.js --dump F [a b]  print each token's scope (lines a..b)
//
// A fixture is Grav source with assertion comments under a line, as in VS Code's
// grammar tests: `//    ^^^ storage.type.primitive.grav` says the columns under
// the carets carry that scope; `- scope` says they must not. Each scope is
// matched against the end of a token's scope names, so `string.quoted.double`
// matches `string.quoted.double.grav`. `// <- scope` asserts the column where
// the `//` starts (for a token at the start of a line). Exit status 0 when all pass, 1 when one
// fails, 2 when the engine is not installed (the caller skips).
"use strict";
const fs = require("fs");
const path = require("path");

const CANDIDATES = [process.env.GRAV_TEXTMATE_MODULES, "/usr/share/code/resources/app/node_modules",
  "/usr/share/antigravity/resources/app/node_modules", "/usr/share/codium/resources/app/node_modules",
  "/opt/visual-studio-code/resources/app/node_modules", path.join(__dirname, "node_modules")].filter(Boolean);
const C_GRAMMARS = ["/usr/share/code/resources/app/extensions/cpp/syntaxes/c.tmLanguage.json",
  "/usr/share/antigravity/resources/app/extensions/cpp/syntaxes/c.tmLanguage.json"];
const GRAMMAR = path.join(__dirname, "..", "vscode", "syntaxes", "grav.tmLanguage.json");

const modules = CANDIDATES.find((d) => fs.existsSync(path.join(d, "vscode-textmate")) && fs.existsSync(path.join(d, "vscode-oniguruma")));
if (!modules) { console.log("grammar: skipped (vscode-textmate not found; set GRAV_TEXTMATE_MODULES)"); process.exit(2); }
const vsctm = require(path.join(modules, "vscode-textmate"));
const onig = require(path.join(modules, "vscode-oniguruma"));

async function loadGrammar() {
  await onig.loadWASM(fs.readFileSync(path.join(modules, "vscode-oniguruma", "release", "onig.wasm")).buffer);
  const cGrammar = C_GRAMMARS.find((p) => fs.existsSync(p));
  const registry = new vsctm.Registry({
    onigLib: Promise.resolve({ createOnigScanner: (p) => new onig.OnigScanner(p), createOnigString: (s) => new onig.OnigString(s) }),
    loadGrammar: async (scope) => {
      if (scope === "source.grav") return vsctm.parseRawGrammar(fs.readFileSync(GRAMMAR, "utf8"), GRAMMAR);
      if (scope === "source.c" && cGrammar) return vsctm.parseRawGrammar(fs.readFileSync(cGrammar, "utf8"), cGrammar);
      return null;
    },
  });
  return registry.loadGrammar("source.grav");
}

// Each line's tokens: [{ start, end, scopes }].
function tokenize(g, text) {
  let rule = vsctm.INITIAL;
  return text.split("\n").map((line) => {
    const r = g.tokenizeLine(line, rule);
    rule = r.ruleStack;
    return { line, tokens: r.tokens.map((t) => ({ start: t.startIndex, end: t.endIndex, scopes: t.scopes.slice(1) })), rule };
  });
}

const hasScope = (scopes, want) => scopes.some((s) => s === want || s.startsWith(want + ".") || s.endsWith("." + want) || s.includes("." + want + "."));

function runFixture(g, file) {
  const lines = tokenize(g, fs.readFileSync(file, "utf8"));
  const fails = [];
  let src = -1; // the source line the assertions are about
  lines.forEach((l, i) => {
    const m = /^\s*\/\/\s*(\^+)\s+(.+)$/.exec(l.line), back = /^(\s*)\/\/\s*<-\s+(.+)$/.exec(l.line);
    if (!m && !back) { if (!/^\s*\/\/\s*$/.test(l.line)) src = i; return; }
    const from = m ? l.line.indexOf("^") : back[1].length, to = m ? from + m[1].length : from + 1;
    const wants = (m ? m[2] : back[2]).trim().split(/\s+/);
    for (let col = from; col < to; col++) {
      const tok = lines[src].tokens.find((t) => col >= t.start && col < t.end);
      const scopes = tok ? tok.scopes : [];
      let neg = false;
      for (const w of wants) {
        if (w === "-") { neg = true; continue; }
        const ok = hasScope(scopes, w);
        if (ok === neg) {
          fails.push(`${path.relative(process.cwd(), file)}:${src + 1}:${col + 1}: '${lines[src].line.slice(col, to).slice(0, 20)}' ` +
            `${neg ? "should not be" : "should be"} ${w}, is ${scopes.join(" ") || "(nothing)"}`);
          return; // one report per assertion
        }
      }
    }
  });
  return fails;
}

// A plain string ("...", r"...", '...') ends on its line, and no rule is still
// open at the end of a file: report files where that is not so.
function runawayStrings(g, files) {
  const out = [];
  for (const file of files) {
    const lines = tokenize(g, fs.readFileSync(file, "utf8"));
    let depth = 0;
    for (let st = lines.length ? lines[lines.length - 1].rule : null; st && st.parent; st = st.parent) depth++;
    if (depth) out.push(`${file}: ${depth} rule(s) still open at the end (an unclosed region)`);
    for (let i = 0; i < lines.length; i++) {
      const probe = g.tokenizeLine("x", lines[i].rule).tokens[0].scopes.join(" ");
      if (/string\.quoted\.(double|single|raw)\.grav(?!\S)/.test(probe) && !/triple/.test(probe) && lines[i].line.trim()) {
        out.push(`${file}:${i + 1}: a string runs past this line: ${lines[i].line.trim().slice(0, 100)}`);
        break;
      }
    }
  }
  return out;
}

loadGrammar().then((g) => {
  const args = process.argv.slice(2);
  if (args[0] === "--dump") {
    const [file, a = 1, b = 1e9] = args.slice(1);
    tokenize(g, fs.readFileSync(file, "utf8")).forEach((l, i) => {
      if (i + 1 < +a || i + 1 > +b) return;
      console.log(String(i + 1).padStart(4) + "  " + l.line);
      for (const t of l.tokens) if (l.line.slice(t.start, t.end).trim())
        console.log("        " + JSON.stringify(l.line.slice(t.start, t.end)).padEnd(24) + " " + t.scopes.filter((s) => s !== "source.grav").slice(-2).join(" "));
    });
    return;
  }
  if (args[0] === "--strings") {
    const bad = runawayStrings(g, args.slice(1));
    bad.forEach((x) => console.log(x));
    console.log(`grammar: ${args.length - 1 - bad.length} of ${args.length - 1} files without a runaway string`);
    process.exit(bad.length ? 1 : 0);
  }
  const dir = path.join(__dirname, "tests");
  const files = fs.readdirSync(dir).filter((f) => f.endsWith(".grav")).map((f) => path.join(dir, f));
  let fails = [], count = 0;
  for (const f of files) {
    count += fs.readFileSync(f, "utf8").split("\n").filter((l) => /^\s*\/\/\s*(\^|<-)/.test(l)).length;
    fails = fails.concat(runFixture(g, f));
  }
  fails.forEach((x) => console.log("FAIL " + x));
  console.log(`grammar: ${count - fails.length} of ${count} assertions pass`);
  process.exit(fails.length ? 1 : 0);
});
