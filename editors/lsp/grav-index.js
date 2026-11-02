// grav-index.js — a dependency-free symbol index for Grav source.
//
// Pure, editor-agnostic helpers shared by the language server (server.js) and the
// VS Code extension's providers. It is regex/heuristic based (no full parse), but
// good enough for go-to-declaration, find-references, rename, and an outline.
//
// Positions are LSP-style { line, character }, both 0-based.

"use strict";

const IDENT = "[A-Za-z_][A-Za-z0-9_]*";

// Declaration patterns. Each yields the declared name + a SymbolKind.
// `kind` uses LSP SymbolKind numbers.
const KIND = {
  File: 1, Namespace: 3, Class: 5, Method: 6, Field: 8, Enum: 10,
  Interface: 11, Function: 12, Variable: 13, Constant: 14, Struct: 23,
  TypeParameter: 26, EnumMember: 22,
};

// Whole-line declaration matchers: [regex, nameGroup, kind].
const DECL_PATTERNS = [
  [new RegExp(`\\bfn\\s+(${IDENT})`), 1, KIND.Function],
  [new RegExp(`\\b(?:class)\\s+(${IDENT})`), 1, KIND.Class],
  [new RegExp(`\\b(?:struct)\\s+(${IDENT})`), 1, KIND.Struct],
  [new RegExp(`\\b(?:enum)\\s+(${IDENT})`), 1, KIND.Enum],
  [new RegExp(`\\b(?:interface)\\s+(${IDENT})`), 1, KIND.Interface],
  [new RegExp(`\\b(?:trait)\\s+(${IDENT})`), 1, KIND.Interface],
  [new RegExp(`\\b(?:type|typedef)\\s+(${IDENT})`), 1, KIND.TypeParameter],
  [new RegExp(`\\b(?:namespace)\\s+(${IDENT})`), 1, KIND.Namespace],
  [new RegExp(`\\bconst\\s+(${IDENT})`), 1, KIND.Constant],
  [new RegExp(`\\blet\\s+(${IDENT})`), 1, KIND.Variable],
];

// A class/struct member field declaration, e.g. `  private balance: int`.
const FIELD_RE = new RegExp(
  `^\\s*(?:public\\s+|private\\s+|protected\\s+|readonly\\s+|static\\s+)*(${IDENT})\\s*:\\s*\\S`
);
// A method declaration inside a class body, e.g. `public fn deposit(n: int)`.
const METHOD_RE = new RegExp(`\\bfn\\s+(${IDENT})`);

const KEYWORDS = new Set([
  "fn", "class", "struct", "enum", "type", "typedef", "interface", "trait",
  "abstract", "namespace", "constructor", "extends", "implements", "uses",
  "import", "export", "new", "return", "async", "await", "let", "const",
  "static", "readonly", "public", "private", "protected", "if", "else",
  "while", "for", "do", "switch", "case", "default", "match", "break",
  "continue", "in", "try", "catch", "finally", "throw", "as", "is", "sizeof",
  "defer", "step", "final", "destructor", "operator",
  "self", "this", "Self", "super", "true", "false", "null",
  "int", "float", "bool", "string", "void", "binary", "long", "double",
  "uint", "unsigned", "signed", "byte",
]);

function splitLines(text) {
  return text.split(/\r\n|\r|\n/);
}

// Returns the identifier under `position` plus its range, or null.
function wordAtPosition(text, position) {
  const line = splitLines(text)[position.line];
  if (line === undefined) return null;
  const isWord = (c) => c && /[A-Za-z0-9_]/.test(c);
  let start = position.character;
  let end = position.character;
  if (!isWord(line[start]) && start > 0 && isWord(line[start - 1])) start--;
  if (!isWord(line[start])) return null;
  while (start > 0 && isWord(line[start - 1])) start--;
  while (end < line.length && isWord(line[end])) end++;
  const word = line.slice(start, end);
  if (!word) return null;
  return {
    word,
    range: { start: { line: position.line, character: start },
             end: { line: position.line, character: end } },
  };
}

// All top-level / member declarations in a document, with positions.
function documentDeclarations(text) {
  const lines = splitLines(text);
  const out = [];
  let depth = 0;
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const line = raw.replace(/\/\/.*$/, ""); // strip line comments
    let matched = false;
    for (const [re, group, kind] of DECL_PATTERNS) {
      const m = re.exec(line);
      if (m) {
        const name = m[group];
        if (KEYWORDS.has(name)) continue;
        const ch = line.indexOf(name, m.index);
        // A `fn` inside a class body is a method.
        const k = kind === KIND.Function && depth > 0 ? KIND.Method : kind;
        out.push(makeSymbol(name, k, i, ch));
        matched = true;
        break;
      }
    }
    if (!matched && depth > 0) {
      const fm = FIELD_RE.exec(line);
      if (fm && !KEYWORDS.has(fm[1]) && !/\bfn\b/.test(line)) {
        const ch = line.indexOf(fm[1]);
        out.push(makeSymbol(fm[1], KIND.Field, i, ch));
      }
    }
    // Track brace depth (cheap; ignores braces in strings/comments).
    for (const c of line) { if (c === "{") depth++; else if (c === "}") depth = Math.max(0, depth - 1); }
  }
  return out;
}

function makeSymbol(name, kind, line, character) {
  const range = {
    start: { line, character },
    end: { line, character: character + name.length },
  };
  return { name, kind, range, selectionRange: range };
}

// Locations (in one doc) where `name` is declared.
function declarationsOf(text, name) {
  return documentDeclarations(text).filter((s) => s.name === name).map((s) => s.range);
}

// Every whole-word occurrence of `name` in a doc (definitions + uses).
function referencesOf(text, name) {
  const lines = splitLines(text);
  const out = [];
  const re = new RegExp(`(?<![A-Za-z0-9_])${escapeRe(name)}(?![A-Za-z0-9_])`, "g");
  for (let i = 0; i < lines.length; i++) {
    const stripped = lines[i].replace(/\/\/.*$/, "");
    let m;
    while ((m = re.exec(stripped)) !== null) {
      out.push({
        start: { line: i, character: m.index },
        end: { line: i, character: m.index + name.length },
      });
    }
  }
  return out;
}

function escapeRe(s) { return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"); }

module.exports = {
  KIND, splitLines, wordAtPosition, documentDeclarations,
  declarationsOf, referencesOf,
};
