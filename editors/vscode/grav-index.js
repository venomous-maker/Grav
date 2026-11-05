// NOTE: copy of editors/lsp/grav-index.js, bundled so the packaged .vsix is self-contained (editors/sync.sh).
// grav-index.js — a dependency-free index of Grav source for editors.
//
// Shared by the language server (server.js, used by Neovim, JetBrains, and any
// LSP client) and the VS Code extension (a bundled copy: editors/sync.sh).
//
//   parseFile(text)      declarations (with namespaces, members, enum variants,
//                        generics, signatures, doc comments), imports, `use` lines
//   new Workspace(opts)  resolves imports the way the compiler does (relative,
//                        std/…, grav.toml paths and dependencies, GRAV_PATH), and
//                        answers definition / hover / completion / signature help /
//                        references / symbols queries
//
// It is a careful scanner, not a full parser: strings, comments and inline C are
// masked out first, then declarations are matched per line with a bracket stack
// for nesting. Positions are LSP-style { line, character }, both 0-based.

"use strict";

const fs = require("fs");
const path = require("path");
const os = require("os");
const { execFileSync, execFile } = require("child_process");

const KIND = {
  File: 1, Module: 2, Namespace: 3, Class: 5, Method: 6, Property: 7, Field: 8,
  Constructor: 9, Enum: 10, Interface: 11, Function: 12, Variable: 13, Constant: 14,
  String: 15, EnumMember: 22, Struct: 23, Event: 24, TypeParameter: 26,
};
// LSP CompletionItemKind for a SymbolKind.
const COMPLETION_KIND = {
  [KIND.Namespace]: 9, [KIND.Class]: 7, [KIND.Method]: 2, [KIND.Field]: 5, [KIND.Property]: 10,
  [KIND.Constructor]: 4, [KIND.Enum]: 13, [KIND.Interface]: 8, [KIND.Function]: 3,
  [KIND.Variable]: 6, [KIND.Constant]: 21, [KIND.EnumMember]: 20, [KIND.Struct]: 22,
  [KIND.TypeParameter]: 25, [KIND.Event]: 3, [KIND.Module]: 9,
};

const KEYWORDS = [
  "fn", "class", "struct", "enum", "type", "interface", "trait", "abstract", "final",
  "namespace", "constructor", "destructor", "extends", "implements", "uses", "use",
  "import", "from", "export", "new", "return", "async", "await", "let", "const",
  "static", "readonly", "weak", "public", "private", "protected", "if", "else", "while", "for",
  "do", "switch", "case", "default", "match", "break", "continue", "in", "step", "try",
  "catch", "finally", "throw", "as", "is", "sizeof", "defer", "operator", "test",
  "self", "this", "super", "true", "false", "null", "free", "auto", "typedef",
];
const TYPES = [
  "int", "long", "uint", "byte", "float", "float32", "double", "bool", "string", "void",
  "binary", "any", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64",
];
const BUILTINS = [
  ["print", "print(value)", "Print a value and a newline."],
  ["str", "str(value) -> string", "Any printable value as text."],
  ["input", "input() -> string", "Read a line from standard input."],
  ["assert", "assert(condition[, message])", "Fail (or fail the test) when the condition is false."],
  ["assertEq", "assertEq(actual, expected[, message])", "Fail with both values when they differ."],
  ["panic", "panic(message)", "Stop the program with a message."],
  ["exit", "exit(code)", "Exit with a status code."],
  ["argc", "argc() -> int", "Number of command-line arguments (including the program)."],
  ["argv", "argv(i) -> string", "Command-line argument i."],
  ["typename", "typename(value) -> string", "The runtime type name of a value."],
  ["sizeof", "sizeof(T) -> int", "Byte size of a type or value."],
  ["free", "free(object)", "Release an object (runs its destructor)."],
  ["println", "println(value)", "Same as print."],
  ["env", "env(name) -> string", "An environment variable (\"\" when unset)."],
  ["cwd", "cwd() -> string", "The working directory."],
  ["isInstance", "isInstance(object, Type) -> bool", "Is the object a Type (or a subclass)?"],
  ["int", "int(value) -> int", "Convert a number, bool or enum to an int (floats are truncated)."],
  ["float", "float(value) -> float", "Convert a number or bool to a float."],
  ["bool", "bool(value) -> bool", "Convert a number to a bool (nonzero is true)."],
];
// Library-internal builtins (names starting with `__`): explained on hover, and
// offered by completion only once `__` is typed.
const INTERNAL_BUILTINS = [
  ["__retain", "__retain(value) -> value",
   "Library-internal: the value with one more reference. Containers call it when a reference-counted value (object, string, closure, any) goes into a raw buffer they manage. Does nothing for plain values, so it works in generic code."],
  ["__release", "__release(value)",
   "Library-internal: drop one reference (freeing the value when it was the last). Containers call it when a value leaves a raw buffer, and in their destructor for every element. Does nothing for plain values."],
  ["__visit", "__visit(value)",
   "Library-internal: report a value held in a raw buffer to the cycle collector, from a container's private `__visitRefs()` method, so cycles through containers can be collected."],
  ["__repr", "__repr(value) -> string",
   "Library-internal: the text of any value as it appears inside a collection — like `str(value)`, but strings are quoted (\"a\"). Used by the collections' `toString()`."],
  ["__show", "__show(value) -> string",
   "Compiler-internal: the text of a data-enum variant's field, used by the generated `toString()` (`Circle(1.5)`)."],
  ["__fmt", "__fmt(value, spec) -> string",
   "Compiler-internal: what `\"${value:spec}\"` becomes — the value formatted by a spec such as `.2f`, `05`, `x` or `>8`."],
];
// Hover explanations for keywords and built-in types: [summary, example].
const KEYWORD_DOCS = {
  fn: ["Declares a function or method; `fn(a) => e` is a lambda. Parameters may have defaults (`b: int = 1`).", "fn add(a: int, b: int = 1) -> int { return a + b }"],
  class: ["A reference type with fields, constructors, methods, single inheritance and interfaces.", "class Dog extends Animal implements Named { ... }"],
  struct: ["A value type: named fields, copied on assignment. It may have methods; inside them `self` is the struct.", "struct Point { x: int  y: int  fn sum() -> int { return self.x + self.y } }"],
  enum: ["A set of named constants (members may have values), or a data enum whose variants carry fields.", "enum Http { Ok = 200, NotFound = 404 }\nenum Shape { Circle(r: float), Empty }"],
  type: ["A type alias; fully interchangeable with what it names (`typedef` is a synonym).", "type Meters = double"],
  typedef: ["A synonym of `type`: a type alias.", "typedef Grid = int[3]"],
  interface: ["A set of method signatures a class can implement; `is` / `as` check and convert at run time.", "interface Shape { fn area() -> float }"],
  trait: ["An interface whose methods may have default bodies; `Self` is the implementing class.", "trait Comparable { fn compareTo(o: Self) -> int }"],
  abstract: ["An abstract class cannot be instantiated; an abstract method has no body and must be overridden.", "abstract class Shape { abstract fn area() -> float }"],
  final: ["A final class cannot be extended; a final method cannot be overridden (calls are direct).", "final class Version { ... }"],
  namespace: ["Groups declarations under a dotted name.", "namespace geo { class Point { ... } }"],
  constructor: ["Runs when `new` creates an object. A class may have several; a subclass without one inherits its parent's.", "constructor(name: string) { super(name) }"],
  destructor: ["Runs when the object is released with `free(obj)`, most-derived class first.", "destructor() { print(\"bye\") }"],
  extends: ["The base class(es) of a class.", "class Puppy extends Dog { }"],
  implements: ["The interfaces a class implements.", "class Box implements Drawable, Resizable { }"],
  uses: ["Composition: forwards the methods of a field's type to that field.", "class Car uses engine: Engine { }"],
  use: ["Brings names from a namespace into scope (`*` for all, `as` to rename).", "use std.collections.*"],
  import: ["Loads another file or library module: \"std\", \"std/json\", a relative path, or a package.", "import \"std\""],
  from: ["Picks names out of an import.", "import { Array, HashMap as Map } from \"std/collections\""],
  export: ["Marks a declaration as part of a package's public surface.", "export fn helper() { }"],
  new: ["Creates an object by calling a constructor (type arguments may be inferred).", "let b = new Box(5)"],
  return: ["Leaves the function, giving its value.", "return a + b"],
  async: ["An async function runs on the worker pool; calling it gives a Future<T>.", "async fn fetch(url: string) -> string { ... }"],
  await: ["Waits for a Future and gives its value (re-throwing its exception).", "let page = await fetch(url)"],
  let: ["Declares a variable (its type is inferred from the value if omitted). Patterns take values apart.", "let (q, r) = divmod(17, 5)"],
  const: ["Declares a constant: assigning it later is an error.", "const LIMIT: int = 10"],
  static: ["A member that belongs to the class rather than to its instances.", "static fn make() -> Id { ... }"],
  readonly: ["A field that can only be assigned in a constructor.", "readonly id: int"],
  weak: ["A field that does not keep its object alive (breaks reference cycles).", "weak parent: Node"],
  public: ["Visible everywhere (the default).", "public name: string"],
  private: ["Visible only inside the class; private fields get get_/set_ accessors.", "private secret: string"],
  protected: ["Visible inside the class and its subclasses.", "protected count: int"],
  if: ["Runs a block when the condition (a bool) holds; also `case x if cond:` guards.", "if (n > 0) { ... } else { ... }"],
  else: ["The branch taken when the `if` condition is false.", "else if (n == 0) { ... }"],
  while: ["Repeats a block while the condition holds.", "while (i < 10) { i++ }"],
  for: ["A C-style loop, a range `for (i in 0..n step 2)`, or for-each `for (i, x in xs)` / `for ((k, v) in pairs)`.", "for (x in xs) { print(x) }"],
  do: ["A loop whose body runs at least once.", "do { tries++ } while (tries < 3)"],
  in: ["Separates the loop variable from what it iterates.", "for (ch in \"héllo\") { }"],
  step: ["The increment of a range loop (may be negative).", "for (i in 10..0 step -2) { }"],
  switch: ["Picks a case: values, ranges (`lo..hi`), guards (`if cond`), data-enum patterns. No fall-through.", "switch (n) { case 1, 2: { } case 3..10: { } default: { } }"],
  match: ["Like `switch`; where a value is expected each case gives an expression.", "let word = match (n) { case 1: \"one\" default: \"many\" }"],
  case: ["One alternative of a switch/match: values, a range, a variant pattern, and an optional `if` guard.", "case Circle(r) if r > 1.0: print(r)"],
  default: ["The case taken when no other case matches.", "default: { print(\"other\") }"],
  break: ["Leaves the innermost loop, or the loop with the given label.", "break outer"],
  continue: ["Goes on with the next iteration of the innermost (or labeled) loop.", "continue outer"],
  try: ["Runs a block whose exceptions the `catch` clauses handle.", "try { risky() } catch (e: ValueError) { } finally { }"],
  catch: ["Handles exceptions of a class (or its subclasses) thrown in the `try` block.", "catch (e: IndexError) { print(e.message()) }"],
  finally: ["Always runs when leaving the `try`, normally or by an exception.", "finally { file.close() }"],
  throw: ["Raises an exception: any class instance.", "throw new ValueError(\"bad input\")"],
  defer: ["Runs a statement when the enclosing block exits (last registered first).", "defer { lock.unlock() }"],
  as: ["Converts: numbers, enums from ints, down/up casts, interfaces, `any` (checked at run time).", "let d = animal as Dog"],
  is: ["Tests a value's runtime type: a class, an interface, or what an `any` holds.", "if (shape is Named) { }"],
  sizeof: ["The size in bytes of a type or value.", "sizeof(Point)"],
  operator: ["Defines an operator for a class: + - * / % == < [] []= ...", "fn operator+(o: Vec) -> Vec { ... }"],
  test: ["A test block, run by `grav test`.", "test \"adds\" { assertEq(1 + 1, 2) }"],
  self: ["The object (or struct) a method was called on. `this` is a synonym.", "self.count += 1"],
  this: ["A synonym of `self`: the object a method was called on.", "this.count += 1"],
  super: ["The parent class: `super(...)` runs its constructor, `super.m()` its method.", "super(name)"],
  true: ["The boolean true.", "let ok = true"],
  false: ["The boolean false.", "let done = false"],
  null: ["No object: valid for classes, interfaces, functions and pointers.", "let next: Node = null"],
  free: ["Releases an object now (running destructors), a pointer, closure, or finished future.", "free(conn)"],
  auto: ["An inferred type or type argument.", "let xs = new Array<auto>()"],
  int: ["32-bit signed integer. Integer literals beyond its range are longs.", "let n: int = 42"],
  long: ["64-bit signed integer (int64).", "let big: long = 9000000000"],
  uint: ["32-bit unsigned integer.", "let mask: uint = 0xFFFF"],
  byte: ["8-bit unsigned integer (uint8); `s[i]` gives one.", "let b: byte = 255"],
  float: ["64-bit floating point (like double). Prints in the shortest exact form.", "let ratio = 0.5"],
  float32: ["32-bit floating point.", "let f: float32 = 1.5"],
  double: ["64-bit floating point (same as float).", "let d: double = 3.14"],
  bool: ["true or false; conditions must be bools.", "let ready: bool = false"],
  string: ["Immutable UTF-8 text: `+`, `==`, `<`, `s.length`, `s[i]`, `s[lo..hi]`, `${...}` interpolation.", "let s = \"sum=${a + b:.2f}\""],
  void: ["No value: the result of a function that returns nothing.", "fn log(msg: string) -> void { }"],
  binary: ["Raw bytes with a length (may contain zeros).", "let data: binary = File.readBinary(path)"],
  any: ["A value of any type with its runtime type; `is` tests and `as` takes it out.", "let v: any = 42"],
};
for (const t of ["int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64"])
  KEYWORD_DOCS[t] = [`${t.startsWith("u") ? "Unsigned" : "Signed"} ${t.replace(/\D/g, "")}-bit integer.`, `let v: ${t} = 1`];

// Completion snippets (LSP snippet syntax): [label, description, body].
const SNIPPETS = [
  ["fn", "function", "fn ${1:name}(${2}) -> ${3:void} {\n\t$0\n}"],
  ["main", "entry point", "fn main() {\n\t$0\n}"],
  ["class", "class with a constructor", "class ${1:Name} {\n\tpublic ${2:value}: ${3:int}\n\tconstructor(${2:value}: ${3:int}) { self.${2:value} = ${2:value} }\n\t$0\n}"],
  ["struct", "struct with a method", "struct ${1:Name} {\n\t${2:x}: ${3:int}\n\tfn ${4:describe}() -> string { return \"${self.${2:x}}\" }\n}"],
  ["enum", "enum", "enum ${1:Name} { ${2:A}, ${3:B} }"],
  ["enumdata", "data enum with a match", "enum ${1:Shape} {\n\t${2:Circle}(${3:r}: float),\n\t${4:Empty},\n}"],
  ["interface", "interface", "interface ${1:Name} {\n\tfn ${2:method}() -> ${3:int}\n}"],
  ["if", "if / else", "if (${1:cond}) {\n\t$0\n} else {\n}"],
  ["for", "for-each loop", "for (${1:x} in ${2:xs}) {\n\t$0\n}"],
  ["fori", "range loop", "for (${1:i} in ${2:0}..${3:n}) {\n\t$0\n}"],
  ["forkv", "destructuring loop", "for ((${1:k}, ${2:v}) in ${3:pairs}) {\n\t$0\n}"],
  ["while", "while loop", "while (${1:cond}) {\n\t$0\n}"],
  ["match", "match statement", "match (${1:value}) {\n\tcase ${2:pattern}: ${3}\n\tdefault: ${0}\n}"],
  ["matchexpr", "match expression", "let ${1:result} = match (${2:value}) { case ${3:1}: ${4:\"one\"} default: ${5:\"other\"} }"],
  ["try", "try / catch / finally", "try {\n\t$1\n} catch (e: ${2:Exception}) {\n\tprint(e.message())\n} finally {\n\t$0\n}"],
  ["test", "test block", "test \"${1:does something}\" {\n\tassertEq(${2:actual}, ${3:expected})\n}"],
  ["lambda", "lambda", "fn(${1:x}) => ${0}"],
  ["defer", "defer block", "defer { $0 }"],
  ["label", "labeled loop", "${1:outer}: for (${2:x} in ${3:xs}) {\n\t$0\n}"],
  ["letd", "destructuring let", "let (${1:a}, ${2:b}) = ${0}"],
  ["fmt", "formatted interpolation", "\"\\${${1:value}:${2:.2f}}\""],
  ["async", "async function", "async fn ${1:name}(${2}) -> ${3:int} {\n\t$0\n}"],
  ["extends", "subclass", "class ${1:Name} extends ${2:Base} {\n\tconstructor(${3}) { super(${4}) }\n\t$0\n}"],
  ["trait", "trait with a default method", "trait ${1:Name} {\n\tfn ${2:method}() -> ${3:int}\n\tfn ${4:helper}() -> ${3:int} { return self.${2:method}() }\n}"],
  ["importstd", "import the standard library", "import \"std\""],
  ["inlinec", "inline C", "%{ $0 %}"],
];
const KEYWORD_SET = new Set([...KEYWORDS, ...TYPES]);
const IDENT = "[A-Za-z_][A-Za-z0-9_]*";

function splitLines(text) { return text.split(/\r\n|\r|\n/); }

// ---------------------------------------------------------------------------
// Masking: strings, comments and inline C become spaces (same length), so
// regexes and bracket counting only see code. Interpolations `${...}` in strings
// are kept (they are code) except for the quotes around them.
// ---------------------------------------------------------------------------
function mask(text) {
  const out = text.split("");
  let i = 0;
  const n = text.length;
  const blank = (a, b) => { for (let k = a; k < b; k++) if (out[k] !== "\n") out[k] = " "; };
  function skipString(start) { // start at the opening quote; returns index after closing
    let j = start + 1;
    while (j < n && text[j] !== "\n") {
      const c = text[j];
      if (c === "\\") { j += 2; continue; }
      if (c === '"') { blank(start, j + 1); return j + 1; }
      if (c === "$" && text[j + 1] === "{") {
        blank(start, j + 2);
        let depth = 1;
        j += 2;
        while (j < n && depth > 0) {
          if (text[j] === '"') { j = skipString(j); continue; }
          if (text[j] === "{") depth++;
          else if (text[j] === "}") depth--;
          j++;
        }
        start = j - 1; // the closing } of the interpolation stays; mask from here on
        continue;
      }
      j++;
    }
    blank(start, j);
    return j;
  }
  while (i < n) {
    const c = text[i];
    if (c === "/" && text[i + 1] === "/") { const e = text.indexOf("\n", i); const end = e < 0 ? n : e; blank(i, end); i = end; continue; }
    if (c === "/" && text[i + 1] === "*") { const e = text.indexOf("*/", i + 2); const end = e < 0 ? n : e + 2; blank(i, end); i = end; continue; }
    if (c === "%" && text[i + 1] === "{") { const e = text.indexOf("%}", i + 2); const end = e < 0 ? n : e + 2; blank(i, end); out[i] = "C"; i = end; continue; }
    if (c === '"') { i = skipString(i); continue; }
    if (c === "'") { let j = i + 1; while (j < n && text[j] !== "'" && text[j] !== "\n") j += text[j] === "\\" ? 2 : 1; blank(i, j + 1); i = j + 1; continue; }
    i++;
  }
  return out.join("");
}

// The `// ...` comment lines directly above line `i` (the doc comment).
function docAbove(lines, i) {
  const doc = [];
  for (let j = i - 1; j >= 0; j--) {
    const t = lines[j].trim();
    if (!t.startsWith("//")) break;
    const body = t.replace(/^\/\/+\s?/, "");
    if (/^[─━-]{3,}|^──/.test(body)) break; // section dividers end a doc comment
    doc.unshift(body);
  }
  return doc.join("\n").trim();
}

function range(line, ch, len) {
  return { start: { line, character: ch }, end: { line, character: ch + len } };
}

function splitParams(s) {
  // "a: int, f: fn(int) -> bool, m: Map<K, V>" -> top-level comma split
  const out = [];
  let depth = 0, cur = "";
  for (const c of s) {
    if ("(<[{".includes(c)) depth++;
    else if (")>]}".includes(c)) depth--;
    if (c === "," && depth === 0) { out.push(cur.trim()); cur = ""; } else cur += c;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}

// "(a: int, b: string) -> bool" from a declaration line starting at the "(".
function signatureFrom(line, at) {
  let depth = 0, j = at;
  for (; j < line.length; j++) {
    if (line[j] === "(") depth++;
    else if (line[j] === ")") { depth--; if (depth === 0) break; }
  }
  const params = line.slice(at + 1, j);
  const rest = line.slice(j + 1);
  const ret = /^\s*->\s*([^{=;]+?)\s*(?:\{|=>|$|;)/.exec(rest);
  return { params: splitParams(params), ret: ret ? ret[1].trim() : "" };
}

// ---------------------------------------------------------------------------
// parseFile: declarations, imports, uses
// ---------------------------------------------------------------------------
const MODS = "(?:(?:export|public|private|protected|static|abstract|final|async|readonly|weak)\\s+)*";
const RE = {
  namespace: new RegExp(`^\\s*namespace\\s+(${IDENT}(?:\\.${IDENT})*)`),
  classLike: new RegExp(`^\\s*(?:@${IDENT}(?:\\([^)]*\\))?\\s*)*${MODS}(class|struct|interface|trait|enum)\\s+(${IDENT})\\s*(<[^>{]*>)?`),
  alias: new RegExp(`^\\s*${MODS}type\\s+(${IDENT})\\s*(<[^>=]*>)?\\s*=\\s*(.+)$`),
  fn: new RegExp(`^\\s*(?:@${IDENT}(?:\\([^)]*\\))?\\s*)*${MODS}fn\\s+(operator\\s*\\S+?|${IDENT})\\s*(<[^>(]*>)?\\s*\\(`),
  ctor: /^\s*constructor\s*\(/,
  dtor: /^\s*destructor\s*\(/,
  global: new RegExp(`^\\s*${MODS}(const|let)\\s+(${IDENT})\\s*(?::\\s*([^=]+?))?\\s*=`),
  local: new RegExp(`\\b(let|const)\\s+(${IDENT})\\s*(?::\\s*([^=]+?))?\\s*=\\s*(.*)$`),
  field: new RegExp(`^\\s*(?:(?:public|private|protected|readonly|static|weak)\\s+)*(${IDENT})\\s*:\\s*([^=]+?)\\s*(?:=.*)?$`),
  test: /^\s*test\s+"([^"]*)"/,
  forVar: new RegExp(`\\bfor\\s*\\(\\s*(${IDENT})(?:\\s*,\\s*(${IDENT}))?\\s+in\\b`),
  catchVar: new RegExp(`\\bcatch\\s*\\(\\s*(${IDENT})\\s*:\\s*(${IDENT}(?:\\.${IDENT})*)`),
  lambdaParams: /\bfn\s*\(([^()]*)\)\s*(?:->\s*[^{=]+)?\s*(?:=>|\{)/g,
};

function parseFile(text) {
  const lines = splitLines(text);
  const masked = splitLines(mask(text));
  const symbols = [];    // flat list
  const imports = [];    // { spec, alias, names: [{name, as}], range, specRange }
  const uses = [];       // { target, alias, glob, namespace }
  // Bracket stack entries: { ch, ctx } where ctx is the symbol that opened a body.
  const stack = [];
  let pendingCtx = null; // a declaration whose `{` has not been seen yet

  const ctxOf = () => { for (let k = stack.length - 1; k >= 0; k--) if (stack[k].ctx) return stack[k].ctx; return null; };
  const nsOf = () => {
    const parts = [];
    for (const s of stack) if (s.ctx && s.ctx.kind === KIND.Namespace) parts.push(s.ctx.name);
    return parts.join(".");
  };
  const add = (sym) => { symbols.push(sym); return sym; };

  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const m = masked[i];
    const ctx = ctxOf();
    const inType = ctx && [KIND.Class, KIND.Struct, KIND.Interface, KIND.Enum].includes(ctx.kind);
    const inEnum = ctx && ctx.kind === KIND.Enum;
    const inBody = ctx && [KIND.Function, KIND.Method, KIND.Constructor, KIND.Event].includes(ctx.kind);
    const ns = nsOf();
    const container = ctx && ctx.kind !== KIND.Namespace ? ctx : null;
    let mm;

    // ---- imports (read from the raw line: the spec is a string) ----
    const it = raw.trim();
    if (/^import\b/.test(it)) {
      const imp = { spec: "", alias: "", names: [], line: i };
      const sel = /^import\s*\{([^}]*)\}\s*from\s*"([^"]*)"/.exec(it);
      const plain = /^import\s*"([^"]*)"(?:\s+as\s+(\w+))?/.exec(it);
      if (sel) {
        imp.spec = sel[2];
        for (const part of sel[1].split(",")) {
          const p = /^\s*(\w+)(?:\s+as\s+(\w+))?\s*$/.exec(part);
          if (p) imp.names.push({ name: p[1], as: p[2] || p[1] });
        }
      } else if (plain) {
        imp.spec = plain[1];
        imp.alias = plain[2] || "";
      }
      if (imp.spec) {
        const q = raw.indexOf('"' + imp.spec + '"');
        imp.specRange = range(i, q + 1, imp.spec.length);
        imports.push(imp);
      }
      continue;
    }
    // ---- use ----
    if ((mm = /^\s*use\s+([\w.:]+?)(\.\*|::\*)?(?:\s+as\s+(\w+))?\s*;?\s*$/.exec(m))) {
      const target = mm[1].replace(/::/g, ".");
      uses.push({ target, glob: !!mm[2], alias: mm[3] || (mm[2] ? "" : target.split(".").pop()), namespace: ns, line: i });
    }

    // ---- declarations ----
    let opened = null; // a symbol whose body may start on this line
    if ((mm = RE.namespace.exec(m))) {
      const name = mm[1];
      opened = add({ name, kind: KIND.Namespace, line: i, ch: m.indexOf(name, mm.index + 9), namespace: ns,
        detail: "namespace " + (ns ? ns + "." : "") + name, doc: docAbove(lines, i), children: [] });
    } else if ((mm = RE.classLike.exec(m)) && !inBody) {
      const kw = mm[1], name = mm[2];
      const kind = { class: KIND.Class, struct: KIND.Struct, interface: KIND.Interface, trait: KIND.Interface, enum: KIND.Enum }[kw];
      const decl = raw.trim().replace(/\s*\{.*$/, "");
      const ext = /\bextends\s+([\w.<>, ]+?)(?:\s+implements|\s+uses|\s*\{|$)/.exec(m);
      opened = add({ name, kind, keyword: kw, line: i, ch: m.indexOf(name, mm.index + mm[0].indexOf(kw) + kw.length),
        namespace: ns, container: container && container.name, typeParams: mm[3] || "",
        base: ext ? ext[1].trim().replace(/<.*$/, "") : "", detail: decl, doc: docAbove(lines, i), children: [] });
      // a one-line enum: enum Color { Red, Green }
      if (kw === "enum") {
        const ob = m.indexOf("{"), cb = m.lastIndexOf("}");
        if (ob >= 0 && cb > ob) {
          const inner = m.slice(ob + 1, cb);
          for (const v of inner.matchAll(new RegExp(`(?:^|,)\\s*(${IDENT})\\s*(\\([^)]*\\))?`, "g"))) {
            if (v[1] === "fn" || KEYWORD_SET.has(v[1])) continue;
            const ch = ob + 1 + v.index + v[0].indexOf(v[1]);
            const variant = add({ name: v[1], kind: KIND.EnumMember, line: i, ch, namespace: ns, container: name,
              detail: `${name}.${v[1]}${v[2] ? raw.slice(ch + v[1].length, ch + v[1].length + v[2].length) : ""}`, doc: "" });
            opened.children.push(variant);
          }
        }
      }
    } else if ((mm = RE.alias.exec(m)) && !inBody) {
      add({ name: mm[1], kind: KIND.TypeParameter, line: i, ch: m.indexOf(mm[1], mm.index + 4), namespace: ns,
        detail: raw.trim(), doc: docAbove(lines, i), aliasOf: mm[3].trim() });
    } else if ((mm = RE.test.exec(raw)) && !inBody && !inType) {
      opened = add({ name: `test "${mm[1]}"`, kind: KIND.Event, line: i, ch: raw.indexOf("test"), namespace: ns,
        detail: `test "${mm[1]}"`, doc: "", children: [] });
    } else if ((mm = RE.fn.exec(m)) && !inBody) {
      const name = mm[1].replace(/\s+/g, "");
      const paren = m.indexOf("(", mm.index + mm[0].length - 1);
      const sig = signatureFrom(raw, paren);
      const kind = inType ? KIND.Method : KIND.Function;
      const isStatic = /\bstatic\s+(?:\w+\s+)*fn\b/.test(m);
      opened = add({ name, kind, line: i, ch: m.indexOf(mm[1], mm.index), namespace: ns,
        container: container && container.name, typeParams: mm[2] || "", params: sig.params, returns: sig.ret,
        isStatic, detail: raw.trim().replace(/\s*(\{.*|=>.*)$/, ""), doc: docAbove(lines, i), children: [] });
      addParams(opened, sig.params, i, raw, paren);
    } else if ((RE.ctor.test(m) || RE.dtor.test(m)) && inType) {
      const isCtor = RE.ctor.test(m);
      const paren = m.indexOf("(");
      const sig = signatureFrom(raw, paren);
      opened = add({ name: isCtor ? "constructor" : "destructor", kind: KIND.Constructor, line: i,
        ch: m.search(/\S/), namespace: ns, container: container && container.name, params: sig.params,
        detail: `${container ? container.name : ""}(${sig.params.join(", ")})`, doc: docAbove(lines, i), children: [] });
      addParams(opened, sig.params, i, raw, paren);
    } else if (inEnum && stack.length && stack[stack.length - 1].ctx === ctx &&
               (mm = new RegExp(`^\\s*(${IDENT})\\s*(\\(.*?\\))?\\s*,?\\s*$`).exec(m)) && !KEYWORD_SET.has(mm[1])) {
      add({ name: mm[1], kind: KIND.EnumMember, line: i, ch: m.indexOf(mm[1]), namespace: ns, container: ctx.name,
        detail: `${ctx.name}.${mm[1]}${mm[2] ? raw.trim().replace(/,\s*$/, "").slice(mm[1].length) : ""}`,
        doc: docAbove(lines, i), payload: mm[2] || "" });
    } else if ((mm = RE.global.exec(m)) && !inBody && !inType) {
      add({ name: mm[2], kind: mm[1] === "const" ? KIND.Constant : KIND.Variable, line: i,
        ch: m.indexOf(mm[2], mm.index + mm[0].indexOf(mm[1]) + mm[1].length), namespace: ns,
        type: mm[3] ? mm[3].trim() : "", detail: raw.trim(), doc: docAbove(lines, i) });
    } else if (inType && !inEnum && stack.length && stack[stack.length - 1].ctx === ctx &&
               (mm = RE.field.exec(m)) && !KEYWORD_SET.has(mm[1])) {
      add({ name: mm[1], kind: KIND.Field, line: i, ch: m.indexOf(mm[1]), namespace: ns, container: ctx.name,
        type: mm[2].trim(), isStatic: /\bstatic\b/.test(m), detail: raw.trim(), doc: docAbove(lines, i) });
    }
    // ---- locals inside bodies: let/const, for-in variables, catch variables, lambda params ----
    if (inBody || (opened && [KIND.Function, KIND.Method, KIND.Event, KIND.Constructor].includes(opened.kind))) {
      const owner = inBody ? ctx : opened;
      for (const lm of m.matchAll(new RegExp(RE.local.source, "g"))) {
        const ch = lm.index + lm[0].indexOf(lm[2], lm[1].length);
        const init = raw.slice(lm.index + lm[0].length - lm[4].length).trim();
        add({ name: lm[2], kind: lm[1] === "const" ? KIND.Constant : KIND.Variable, line: i, ch, local: owner,
          type: lm[3] ? lm[3].trim() : inferType(init), detail: raw.trim(), doc: "" });
      }
      if ((mm = RE.forVar.exec(m))) for (const g of [mm[1], mm[2]]) if (g)
        add({ name: g, kind: KIND.Variable, line: i, ch: m.indexOf(g, mm.index), local: owner, type: "", detail: raw.trim(), doc: "" });
      if ((mm = RE.catchVar.exec(m)))
        add({ name: mm[1], kind: KIND.Variable, line: i, ch: m.indexOf(mm[1], mm.index), local: owner, type: mm[2], detail: raw.trim(), doc: "" });
      for (const lp of m.matchAll(RE.lambdaParams)) {
        if (opened && lp.index === m.indexOf("(", 0) - 2) continue;
        for (const p of splitParams(lp[1])) {
          const pm = new RegExp(`^(${IDENT})\\s*(?::\\s*(.+))?$`).exec(p);
          if (pm) add({ name: pm[1], kind: KIND.Variable, line: i, ch: m.indexOf(pm[1], lp.index), local: owner,
            type: pm[2] || "", detail: `${pm[1]}${pm[2] ? ": " + pm[2] : ""} (lambda parameter)`, doc: "" });
        }
      }
    }

    // ---- brackets: bodies of the declarations opened above ----
    if (opened) pendingCtx = opened;
    for (let c = 0; c < m.length; c++) {
      const ch = m[c];
      if (ch === "{" || ch === "(" || ch === "[") {
        let entry = { ch, ctx: null };
        if (ch === "{" && pendingCtx) { entry.ctx = pendingCtx; pendingCtx = null; }
        stack.push(entry);
      } else if (ch === "}" || ch === ")" || ch === "]") {
        const top = stack.pop();
        if (top && top.ctx) top.ctx.endLine = i;
      }
    }
    // a bodiless declaration (abstract fn, interface method, `=>` lambda body) closes now
    if (pendingCtx && !/\{\s*$/.test(m) && pendingCtx.line === i && !m.includes("{")) pendingCtx.endLine = i, pendingCtx = null;
  }
  // Ranges and parent links.
  const byContainer = new Map();
  for (const s of symbols) {
    s.range = range(s.line, Math.max(0, s.ch), s.name.length);
    s.selectionRange = s.range;
    s.fqName = (s.namespace ? s.namespace + "." : "") + (s.container ? s.container + "." : "") + s.name;
    if (s.container && !s.local) {
      if (!byContainer.has(s.container)) byContainer.set(s.container, []);
      byContainer.get(s.container).push(s);
    }
  }
  for (const s of symbols) if (!s.local && [KIND.Class, KIND.Struct, KIND.Interface, KIND.Enum].includes(s.kind))
    s.members = (byContainer.get(s.name) || []).filter((x) => x.namespace === s.namespace);
  return { symbols, imports, uses };

  function addParams(owner, params, line, rawLine, at) {
    for (const p of params) {
      const pm = new RegExp(`^(?:\\.\\.\\.)?(${IDENT})\\s*:\\s*(.+)$`).exec(p);
      if (!pm) continue;
      const ch = rawLine.indexOf(pm[1], at);
      symbols.push({ name: pm[1], kind: KIND.Variable, line, ch, local: owner, type: pm[2].trim(),
        detail: `${pm[1]}: ${pm[2].trim()} (parameter of ${owner.name})`, doc: "", isParam: true });
    }
  }
}

// The type of `new T<...>(...)`, `T.of(...)`-style or literal initializers.
function inferType(init) {
  let m;
  if ((m = /^new\s+([\w.]+)\s*(<[^(]*>)?/.exec(init))) return m[1] + (m[2] || "");
  if (/^"/.test(init)) return "string";
  if (/^-?\d+\.\d/.test(init)) return "float";
  if (/^-?\d/.test(init)) return "int";
  if (/^(true|false)\b/.test(init)) return "bool";
  if ((m = /^([A-Z]\w*)\.(\w+)\(/.exec(init))) return { call: m[1], method: m[2] };
  if ((m = /^([A-Z]\w*)\.(\w+)\b/.exec(init))) return m[1];   // Enum.Variant
  return "";
}

// ---------------------------------------------------------------------------
// Words and positions
// ---------------------------------------------------------------------------
function wordAtPosition(text, position) {
  const line = splitLines(text)[position.line];
  if (line === undefined) return null;
  const isWord = (c) => c && /[A-Za-z0-9_]/.test(c);
  let start = position.character, end = position.character;
  if (!isWord(line[start]) && start > 0 && isWord(line[start - 1])) start--;
  if (!isWord(line[start])) return null;
  while (start > 0 && isWord(line[start - 1])) start--;
  while (end < line.length && isWord(line[end])) end++;
  // the qualifier chain before it: a.b.Word
  let q = start, qualifier = "";
  while (q > 1 && line[q - 1] === ".") {
    let s = q - 1;
    if (line[s - 1] === ")") break;
    while (s > 0 && isWord(line[s - 1])) s--;
    if (s === q - 1) break;
    qualifier = line.slice(s, q - 1) + (qualifier ? "." + qualifier : "");
    q = s;
  }
  return { word: line.slice(start, end), qualifier, range: range(position.line, start, end - start) };
}

// ---------------------------------------------------------------------------
// Workspace
// ---------------------------------------------------------------------------
class Workspace {
  // opts: { root, compiler, log }
  constructor(opts = {}) {
    this.root = opts.root || null;
    this.compiler = opts.compiler || process.env.GRAV_COMPILER || "grav";
    this.log = opts.log || (() => {});
    this.docs = new Map();      // path -> text (open editors, possibly unsaved)
    this.cache = new Map();     // path -> { mtime, text, info }
    this.envInfo = null;
  }

  setDoc(file, text) { this.docs.set(path.resolve(file), text); }
  closeDoc(file) { this.docs.delete(path.resolve(file)); }

  // `grav env --json`: where the standard library and packages live.
  env() {
    if (this.envInfo) return this.envInfo;
    let info = {};
    try {
      info = JSON.parse(execFileSync(this.compiler, ["env", "--json"], { timeout: 5000, cwd: this.root || undefined }).toString());
    } catch (e) {
      this.log("grav env failed: " + e.message);
    }
    if (!info.std) info.std = process.env.GRAV_STD || this.guessStd();
    if (!info.packages) info.packages = process.env.GRAV_PACKAGES || path.join(os.homedir(), ".local", "grav", "packages");
    this.envInfo = info;
    return info;
  }
  guessStd() {
    for (const c of [path.join(__dirname, "..", "..", "lib"), path.join(os.homedir(), ".local", "share", "grav", "lib")])
      if (fs.existsSync(path.join(c, "std.grav"))) return c;
    return "";
  }

  text(file) {
    const p = path.resolve(file);
    if (this.docs.has(p)) return this.docs.get(p);
    try { return fs.readFileSync(p, "utf8"); } catch { return null; }
  }
  info(file) {
    const p = path.resolve(file);
    const text = this.text(p);
    if (text == null) return null;
    const c = this.cache.get(p);
    if (c && c.text === text) return c.info;
    const info = parseFile(text);
    this.cache.set(p, { text, info });
    return info;
  }
  isLibrary(file) {
    const std = this.env().std;
    const pk = this.env().packages;
    const p = path.resolve(file);
    return (std && p.startsWith(path.resolve(std) + path.sep)) || (pk && p.startsWith(path.resolve(pk) + path.sep));
  }

  // ---- grav.toml / packages ----
  project(dir) {
    for (let d = path.resolve(dir); ; d = path.dirname(d)) {
      const toml = path.join(d, "grav.toml");
      if (fs.existsSync(toml)) return this.readProject(d);
      if (path.dirname(d) === d) return null;
    }
  }
  readProject(root) {
    const proj = { root, paths: [], deps: [], lib: "", main: "", lock: {} };
    let section = "";
    try {
      for (const l of fs.readFileSync(path.join(root, "grav.toml"), "utf8").split(/\r?\n/)) {
        const t = l.trim();
        if (!t || t.startsWith("#")) continue;
        if (t.startsWith("[")) { section = t.slice(1, t.indexOf("]")).trim(); continue; }
        const eq = t.indexOf("=");
        if (eq < 0) continue;
        const key = t.slice(0, eq).trim(), val = t.slice(eq + 1).trim();
        const strs = [...val.matchAll(/"([^"]*)"/g)].map((x) => x[1]);
        if (section === "dependencies") {
          let source = "", version = "";
          if (val.startsWith("{")) {
            const g = /(?:git|source|path)\s*=\s*"([^"]*)"/.exec(val), v = /(?:version|tag|branch|rev)\s*=\s*"([^"]*)"/.exec(val);
            source = g ? g[1] : ""; version = v ? v[1] : "";
          } else {
            const spec = strs[0] || "", at = spec.lastIndexOf("@");
            if (at > 0 && !spec.slice(at).includes("/") && !(spec.startsWith("git@") && at === 3)) { source = spec.slice(0, at); version = spec.slice(at + 1); }
            else source = spec;
          }
          proj.deps.push({ name: key, source, version });
        } else if (key === "paths") proj.paths = strs.map((s) => path.join(root, s));
        else if (key === "lib" && strs[0]) proj.lib = path.join(root, strs[0]);
        else if (key === "main" && strs[0]) proj.main = path.join(root, strs[0]);
      }
      const lock = path.join(root, "grav.lock");
      if (fs.existsSync(lock))
        for (const l of fs.readFileSync(lock, "utf8").split(/\r?\n/)) {
          const m = /^(\S+)\s*=\s*\{.*version = "([^"]*)".*commit = "([^"]*)"/.exec(l);
          if (m) proj.lock[m[1]] = { version: m[2], commit: m[3] };
        }
    } catch {}
    return proj;
  }
  static sourceKey(source) {
    let s = source.replace(/^(https?|ssh|git|file):\/\//, "");
    if (s.startsWith("git@")) s = s.slice(4).replace(":", "/");
    s = s.replace(/\.git$/, "").replace(/\/+$/, "");
    if (s.startsWith("/")) s = "local" + s;
    return s;
  }
  packageDir(proj, dep) {
    const key = Workspace.sourceKey(dep.source);
    const base = path.join(this.env().packages, path.dirname(key));
    const repo = path.basename(key);
    const lock = proj.lock[dep.name];
    const want = lock ? (lock.version || (lock.commit || "").slice(0, 12)) : dep.version;
    if (want && fs.existsSync(path.join(base, `${repo}@${want}`))) return path.join(base, `${repo}@${want}`);
    try {
      const found = fs.readdirSync(base).filter((n) => n.startsWith(repo + "@"))
        .map((n) => path.join(base, n)).sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
      return found[0] || null;
    } catch { return null; }
  }

  // Resolve an import spec written in `fromFile`, like the compiler's loader.
  resolveImport(fromFile, spec) {
    const tryFile = (p) => {
      if (fs.existsSync(p) && fs.statSync(p).isFile()) return p;
      if (!path.extname(p) && fs.existsSync(p + ".grav")) return p + ".grav";
      return null;
    };
    const fromDir = path.dirname(path.resolve(fromFile));
    if (path.isAbsolute(spec)) return tryFile(spec);
    const std = this.env().std;
    if (spec === "std" || spec.startsWith("std/")) return std ? tryFile(spec === "std" ? path.join(std, "std.grav") : path.join(std, spec.slice(4))) : null;
    let r = tryFile(path.join(fromDir, spec));
    if (r) return r;
    const proj = this.project(fromDir);
    if (proj) {
      const head = spec.split("/")[0];
      const dep = proj.deps.find((d) => d.name === head);
      if (dep) {
        const dir = this.packageDir(proj, dep);
        if (!dir) return null;
        if (spec === head) {
          const sub = this.readProject(dir);
          for (const c of [sub.lib, path.join(dir, head + ".grav"), path.join(dir, "lib.grav"), path.join(dir, "src", "lib.grav"), path.join(dir, "mod.grav")])
            if (c && fs.existsSync(c)) return c;
          return null;
        }
        return tryFile(path.join(dir, spec.slice(head.length + 1)));
      }
      for (const p of proj.paths) if ((r = tryFile(path.join(p, spec)))) return r;
    }
    for (const p of (process.env.GRAV_PATH || "").split(":").filter(Boolean)) if ((r = tryFile(path.join(p, spec)))) return r;
    if (std && (r = tryFile(path.join(std, spec)))) return r;
    return null;
  }

  // The file and everything it imports (transitively).
  reachable(file) {
    const out = [], seen = new Set();
    const stack = [path.resolve(file)];
    const std = this.env().std;
    if (std && fs.existsSync(path.join(std, "prelude.grav"))) stack.push(path.join(std, "prelude.grav"));
    while (stack.length && out.length < 400) {
      const f = stack.pop();
      if (seen.has(f)) continue;
      seen.add(f);
      out.push(f);
      const info = this.info(f);
      if (!info) continue;
      for (const imp of info.imports) {
        const r = this.resolveImport(f, imp.spec);
        if (r) stack.push(path.resolve(r));
      }
    }
    return out;
  }

  // Every .grav file under the workspace root, plus open documents.
  workspaceFiles() {
    const out = new Set(this.docs.keys());
    const walk = (dir, depth) => {
      if (depth > 12) return;
      let entries;
      try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
      for (const e of entries) {
        if (e.name.startsWith(".") || ["build", "node_modules"].includes(e.name) || e.name.startsWith("cmake-build")) continue;
        const full = path.join(dir, e.name);
        if (e.isDirectory()) walk(full, depth + 1);
        else if (e.isFile() && e.name.endsWith(".grav")) out.add(full);
      }
    };
    if (this.root) walk(this.root, 0);
    return [...out];
  }

  // Files searched for a name used in `file`: what it imports first, then the workspace.
  searchOrder(file) {
    const r = this.reachable(file);
    const set = new Set(r);
    return [...r, ...this.workspaceFiles().filter((f) => !set.has(path.resolve(f)))];
  }

  // ---- queries ----
  symbolsNamed(file, name, qualifier) {
    const out = [];
    for (const f of this.searchOrder(file)) {
      const info = this.info(f);
      if (!info) continue;
      for (const s of info.symbols) {
        if (s.name !== name) continue;
        if (s.local && path.resolve(f) !== path.resolve(file)) continue;
        out.push({ file: f, sym: s });
      }
    }
    if (qualifier) { // prefer those whose namespace/container matches the qualifier's last part
      const last = qualifier.split(".").pop();
      const q = out.filter((x) => x.sym.container === last || (x.sym.namespace || "").split(".").pop() === last ||
                                  (x.sym.namespace || "").endsWith(qualifier));
      if (q.length) return q;
    }
    return out;
  }

  // Locals visible at a position: those declared above it in the enclosing function.
  localsAt(file, pos) {
    const info = this.info(file);
    if (!info) return [];
    const owners = info.symbols.filter((s) => s.children && s.line <= pos.line && (s.endLine === undefined || s.endLine >= pos.line));
    const owner = owners[owners.length - 1];
    return info.symbols.filter((s) => s.local && s.local === owner && s.line <= pos.line);
  }

  definition(file, pos) {
    const text = this.text(file) || "";
    const line = splitLines(text)[pos.line] || "";
    // on an import string: open the module
    const info = this.info(file);
    for (const imp of (info ? info.imports : [])) {
      if (imp.line === pos.line && pos.character >= imp.specRange.start.character - 1 && pos.character <= imp.specRange.end.character + 1) {
        const r = this.resolveImport(file, imp.spec);
        return r ? [{ file: r, range: range(0, 0, 0) }] : [];
      }
    }
    const w = wordAtPosition(text, pos);
    if (!w) return [];
    if (!w.qualifier) {
      const locals = this.localsAt(file, pos).filter((s) => s.name === w.word);
      if (locals.length) return [{ file, range: locals[locals.length - 1].range }];
    } else if (w.qualifier === "self" || w.qualifier.startsWith("self.")) {
      const cls = this.enclosingClass(file, pos);
      if (cls) {
        const m = this.membersOf(file, cls.name).filter((x) => x.sym.name === w.word);
        if (m.length) return m.map((x) => ({ file: x.file, range: x.sym.range }));
      }
    } else {
      const t = this.typeOfExpr(file, pos, w.qualifier);
      if (t) {
        const m = this.membersOf(file, t).filter((x) => x.sym.name === w.word);
        if (m.length) return m.map((x) => ({ file: x.file, range: x.sym.range }));
      }
    }
    // an alias from `import { A as B }` or `use a.A as B` means A
    let name = w.word, qualifier = w.qualifier;
    if (!qualifier && info) {
      for (const imp of info.imports) for (const n of imp.names) if (n.as === name && n.name !== name) name = n.name;
      for (const u of info.uses) if (!u.glob && u.alias === name && u.target.split(".").pop() !== name) {
        qualifier = u.target.split(".").slice(0, -1).join(".");
        name = u.target.split(".").pop();
      }
    }
    // a qualifier that is a `use … as` alias: sh.Rect -> geometry.shapes.Rect
    if (qualifier && info) {
      const head = qualifier.split(".")[0];
      for (const u of info.uses) if (!u.glob && u.alias === head) qualifier = u.target + qualifier.slice(head.length);
    }
    let found = this.symbolsNamed(file, name, qualifier).filter((x) => !x.sym.isParam);
    found = this.rank(file, found, qualifier);
    void line;
    return found.map((x) => ({ file: x.file, range: x.sym.range }));
  }

  // Narrow candidate declarations the way the compiler would choose: what the
  // file imports beats the rest of the workspace; a qualifier must match the
  // namespace; unqualified names prefer the global scope and `use`d namespaces.
  rank(file, found, qualifier) {
    if (found.length < 2) return found;
    const reach = new Set(this.reachable(file).map((f) => path.resolve(f)));
    let r = found.filter((x) => reach.has(path.resolve(x.file)));
    if (!r.length) r = found;
    if (qualifier) {
      const exact = r.filter((x) => x.sym.namespace === qualifier || (x.sym.namespace || "").endsWith("." + qualifier) || x.sym.container === qualifier.split(".").pop());
      if (exact.length) r = exact;
    } else if (r.length > 1) {
      const info = this.info(file);
      const globs = new Set((info ? info.uses : []).filter((u) => u.glob).map((u) => u.target));
      const pref = r.filter((x) => !x.sym.container && (!x.sym.namespace || globs.has(x.sym.namespace) || x.sym.namespace.startsWith("std.")));
      if (pref.length) r = pref;
    }
    return r;
  }

  enclosingClass(file, pos) {
    const info = this.info(file);
    if (!info) return null;
    const cands = info.symbols.filter((s) => [KIND.Class, KIND.Struct, KIND.Enum, KIND.Interface].includes(s.kind) && !s.local &&
      s.line <= pos.line && (s.endLine === undefined || s.endLine >= pos.line));
    return cands[cands.length - 1] || null;
  }

  // Members of a type (and its bases), by type name.
  // What a name written in `file` really refers to, after `use a.B as C` and
  // `import { B as C } from ...` aliases: { name, qualifier }.
  unalias(file, name, qualifier) {
    const info = this.info(file);
    if (!info || qualifier) return { name, qualifier };
    for (const imp of info.imports) for (const n of imp.names) if (n.as === name && n.name !== name) return { name: n.name, qualifier: "" };
    for (const u of info.uses) {
      if (u.glob || u.alias !== name) continue;
      const parts = u.target.split(".");
      if (parts[parts.length - 1] !== name || parts.length > 1) return { name: parts.pop(), qualifier: parts.join(".") };
    }
    return { name, qualifier };
  }

  membersOf(file, typeName, seen = new Set()) {
    let base = String(typeName).replace(/<.*$/, "").replace(/\?$/, "").trim();
    let short = base.split(".").pop();
    let qual = base.includes(".") ? base.slice(0, base.lastIndexOf(".")) : "";
    ({ name: short, qualifier: qual } = this.unalias(file, short, qual));   // Dict -> std.collections.HashMap
    if (!short || seen.has(short)) return [];
    seen.add(short);
    const out = [];
    let found = this.symbolsNamed(file, short, qual).filter((x) =>
      [KIND.Class, KIND.Struct, KIND.Interface, KIND.Enum].includes(x.sym.kind));
    found = this.rank(file, found, qual);
    for (const { file: f, sym } of found) {
      if (![KIND.Class, KIND.Struct, KIND.Interface, KIND.Enum].includes(sym.kind)) continue;
      for (const m of sym.members || []) out.push({ file: f, sym: m });
      if (sym.base) out.push(...this.membersOf(file, sym.base, seen));
      // a data enum variant's fields live on Shape$Circle: none in source; its methods on the base
      break;
    }
    return out;
  }

  // Best-effort type of `a`, `a.b`, `a.b()` at a position (locals, params, fields, returns).
  typeOfExpr(file, pos, expr) {
    const parts = expr.replace(/\([^()]*\)/g, "()").split(".");
    let type = "";
    const first = parts[0].replace(/\(\)$/, "");
    if (first === "self") {
      const c = this.enclosingClass(file, pos);
      type = c ? c.name : "";
    } else {
      const local = this.localsAt(file, pos).filter((s) => s.name === first).pop();
      if (local) type = typeof local.type === "object" ? this.returnOf(file, local.type.call, local.type.method) : local.type;
      else {
        const decl = this.symbolsNamed(file, first).find((x) => [KIND.Class, KIND.Struct, KIND.Enum, KIND.Interface, KIND.Namespace].includes(x.sym.kind));
        if (decl) type = decl.sym.name;
        else {
          const g = this.symbolsNamed(file, first).find((x) => x.sym.type);
          if (g) type = g.sym.type;
        }
      }
    }
    for (const p of parts.slice(1)) {
      if (!type) return "";
      const name = p.replace(/\(\)$/, "");
      const m = this.membersOf(file, type).find((x) => x.sym.name === name);
      if (!m) return "";
      type = m.sym.kind === KIND.Field ? m.sym.type : (m.sym.returns || "");
    }
    return typeof type === "string" ? type : "";
  }
  returnOf(file, cls, method) {
    const m = this.membersOf(file, cls).find((x) => x.sym.name === method);
    return m ? (m.sym.returns || "") : "";
  }

  hover(file, pos) {
    const text = this.text(file) || "";
    const w = wordAtPosition(text, pos);
    if (!w) return null;
    if (KEYWORD_SET.has(w.word)) {
      const d = KEYWORD_DOCS[w.word];
      return d ? { markdown: `**${w.word}** — ${d[0]}\n\n\`\`\`grav\n${d[1]}\n\`\`\``, range: w.range } : null;
    }
    const b = BUILTINS.find((x) => x[0] === w.word) || INTERNAL_BUILTINS.find((x) => x[0] === w.word);
    const defs = this.definition(file, pos);
    for (const d of defs) {
      const info = this.info(d.file);
      const sym = info && info.symbols.find((s) => s.range.start.line === d.range.start.line && s.range.start.character === d.range.start.character);
      if (!sym) continue;
      const where = [];
      if (sym.namespace) where.push("namespace " + sym.namespace);
      if (sym.container) where.push("in " + sym.container);
      if (this.isLibrary(d.file)) where.push(this.moduleName(d.file));
      let md = "```grav\n" + sym.detail + "\n```";
      if (sym.type && sym.kind === KIND.Variable && !sym.detail.includes(":")) md += `\n\ntype: \`${typeof sym.type === "string" ? sym.type : ""}\``;
      if (sym.doc) md += "\n\n" + sym.doc;
      if (where.length) md += "\n\n*" + where.join(" · ") + "*";
      return { markdown: md, range: w.range };
    }
    if (b) return { markdown: "```grav\n" + b[1] + "\n```\n\n" + b[2], range: w.range };
    return null;
  }
  moduleName(file) {
    const std = this.env().std;
    if (std && path.resolve(file).startsWith(path.resolve(std))) {
      const rel = path.relative(std, file).replace(/\.grav$/, "");
      return "std/" + rel.split(path.sep).join("/");
    }
    return path.basename(file);
  }

  // Completion items at a position.
  completion(file, pos) {
    const text = this.text(file) || "";
    const lineText = (splitLines(text)[pos.line] || "").slice(0, pos.character);
    const items = [];
    const seen = new Set();
    const push = (label, kind, detail, doc, insert) => {
      const key = label + "|" + kind;
      if (seen.has(key)) return;
      seen.add(key);
      items.push({ label, kind, detail: detail || "", documentation: doc || "", insertText: insert });
    };
    // import paths: import "std/<cursor>
    let m = /\bimport\b[^"]*"([^"]*)$/.exec(lineText);
    if (m) {
      const typed = m[1];
      const dir = typed.includes("/") ? typed.slice(0, typed.lastIndexOf("/")) : "";
      const std = this.env().std;
      const bases = [];
      if (typed.startsWith("std")) { if (std) bases.push([path.join(std, dir.slice(3).replace(/^\//, "")), dir ? dir + "/" : "std/"]); if (!dir) push("std", 9, "the whole standard library"); }
      else {
        bases.push([path.join(path.dirname(file), dir), dir ? dir + "/" : ""]);
        if (!dir) push("std", 9, "the whole standard library (except networking)");
        const proj = this.project(path.dirname(file));
        if (proj && !dir) for (const d of proj.deps) push(d.name, 9, `package ${d.source}${d.version ? "@" + d.version : ""}`);
      }
      for (const [base, prefix] of bases) {
        try {
          for (const e of fs.readdirSync(base, { withFileTypes: true })) {
            if (e.name.startsWith(".")) continue;
            if (e.isDirectory()) push(prefix + e.name, 19, "folder");
            else if (e.name.endsWith(".grav") && e.name !== "prelude.grav") {
              const first = (this.text(path.join(base, e.name)) || "").split("\n")[0].replace(/^\/\/\s*/, "");
              push(prefix + e.name.replace(/\.grav$/, ""), 17, first);
            }
          }
        } catch {}
      }
      return items.map((it) => ({ ...it, insertText: it.label.slice(typed.lastIndexOf("/") + 1), filterText: it.label.slice(typed.lastIndexOf("/") + 1) }));
    }
    // members after a dot: expr.<cursor>
    m = /([A-Za-z_][\w]*(?:\([^()]*\))?(?:\.[A-Za-z_]\w*(?:\([^()]*\))?)*)\.(\w*)$/.exec(lineText);
    if (m) {
      const expr = m[1];
      const t = this.typeOfExpr(file, pos, expr);
      const first = expr.split(".")[0];
      const isTypeName = /^[A-Z]/.test(first) && !this.localsAt(file, pos).some((s) => s.name === first) && expr.indexOf("(") < 0 && !expr.includes(".");
      // a namespace or alias prefix: std.collections.<cursor>, col.<cursor>
      const nsItems = this.namespaceMembers(file, expr);
      for (const s of nsItems) push(s.name, COMPLETION_KIND[s.kind] || 6, s.detail, s.doc);
      if (t) {
        for (const { sym } of this.membersOf(file, t)) {
          if (sym.kind === KIND.Constructor || sym.name.startsWith("operator")) continue;   // used as syntax, not by name
          if (isTypeName && sym.kind !== KIND.EnumMember && !sym.isStatic) continue;   // Type.<static members>
          if (!isTypeName && sym.isStatic) continue;
          const cls = this.enclosingClass(file, pos);
          if (/^\s*private\b/.test(sym.detail) && (!cls || cls.name !== sym.container)) continue;
          push(sym.name, COMPLETION_KIND[sym.kind] || 6, sym.detail, sym.doc,
            [KIND.Method, KIND.Function].includes(sym.kind) ? undefined : undefined);
        }
      }
      return items;
    }
    // everything visible: locals, then declarations from reachable files, keywords, builtins
    for (const s of this.localsAt(file, pos)) push(s.name, COMPLETION_KIND[s.kind] || 6, s.detail, "");
    for (const f of this.reachable(file)) {
      const info = this.info(f);
      if (!info) continue;
      for (const s of info.symbols) {
        if (s.local || s.container || s.kind === KIND.Event) continue;
        push(s.name, COMPLETION_KIND[s.kind] || 6, s.detail, s.doc);
      }
    }
    const kwDoc = (k) => KEYWORD_DOCS[k] ? `${KEYWORD_DOCS[k][0]}\n\n\`\`\`grav\n${KEYWORD_DOCS[k][1]}\n\`\`\`` : "";
    for (const k of KEYWORDS) push(k, 14, "keyword", kwDoc(k));
    for (const t of TYPES) push(t, 25, "type", kwDoc(t));
    if (!/\S$/.test(lineText) || /^\s*\w*$/.test(lineText))   // statement position: offer snippets
      for (const [label, desc, body] of SNIPPETS)
        items.push({ label, kind: 15, detail: desc, documentation: "```grav\n" + body.replace(/\$\{\d+:?([^}]*)\}|\$\d/g, "$1") + "\n```",
                     insertText: body, snippet: true, sortText: "~" + label });
    for (const b of BUILTINS) push(b[0], 3, b[1], b[2]);
    if (/(^|[^\w])__\w*$/.test(lineText)) for (const b of INTERNAL_BUILTINS) push(b[0], 3, b[1], b[2]);
    return items;
  }

  // Declarations directly inside a namespace (or alias) written as `prefix`.
  namespaceMembers(file, prefix) {
    const info = this.info(file);
    let ns = prefix;
    for (const u of (info ? info.uses : [])) if (!u.glob && u.alias === prefix) ns = u.target;
    for (const imp of (info ? info.imports : [])) {
      if (imp.alias === prefix) {
        const r = this.resolveImport(file, imp.spec);
        const ri = r && this.info(r);
        const own = ri && ri.symbols.find((s) => s.kind === KIND.Namespace);
        if (own) ns = (own.namespace ? own.namespace + "." : "") + own.name;
        else if (ri) for (const i2 of ri.imports) { const r2 = this.resolveImport(r, i2.spec); const o2 = r2 && this.info(r2) && this.info(r2).symbols.find((s) => s.kind === KIND.Namespace); if (o2) { ns = (o2.namespace ? o2.namespace + "." : "") + o2.name; break; } }
      }
    }
    const out = [];
    for (const f of this.searchOrder(file)) {
      const fi = this.info(f);
      if (!fi) continue;
      for (const s of fi.symbols) {
        if (s.local || s.container) continue;
        if (s.namespace === ns) out.push(s);
        else if (s.kind === KIND.Namespace && s.namespace === ns) out.push(s);
      }
    }
    return out;
  }

  // Signature help: the call around the cursor.
  signature(file, pos) {
    const text = this.text(file) || "";
    const before = (splitLines(text)[pos.line] || "").slice(0, pos.character);
    let depth = 0, i = before.length - 1, commas = 0;
    for (; i >= 0; i--) {
      const c = before[i];
      if (c === ")") depth++;
      else if (c === "(") { if (depth === 0) break; depth--; }
      else if (c === "," && depth === 0) commas++;
    }
    if (i < 0) return null;
    const head = /((?:[A-Za-z_]\w*\s*\.\s*)*)([A-Za-z_]\w*)\s*(?:::<[^>]*>)?\s*$/.exec(before.slice(0, i));
    if (!head) return null;
    const name = head[2];
    const qual = head[1].replace(/\s/g, "").replace(/\.$/, "");
    let cands = [];
    if (/^new\s*$/.test(before.slice(0, i - name.length).trimEnd().slice(-3))) {
      cands = this.membersOf(file, name).filter((x) => x.sym.kind === KIND.Constructor).map((x) => x.sym);
    } else if (qual) {
      const t = this.typeOfExpr(file, pos, qual);
      cands = (t ? this.membersOf(file, t) : []).filter((x) => x.sym.name === name).map((x) => x.sym);
      if (!cands.length) cands = this.symbolsNamed(file, name, qual).map((x) => x.sym).filter((s) => s.params);
    } else {
      cands = this.symbolsNamed(file, name).map((x) => x.sym).filter((s) => s.params && !s.container);
      const b = BUILTINS.find((x) => x[0] === name);
      if (!cands.length && b) return { signatures: [{ label: b[1], documentation: b[2], parameters: [] }], activeParameter: commas };
    }
    if (!cands.length) return null;
    return {
      signatures: cands.slice(0, 8).map((s) => ({
        label: `${s.container && s.kind !== KIND.Constructor ? s.container + "." : ""}${s.kind === KIND.Constructor ? s.container : s.name}(${s.params.join(", ")})${s.returns ? " -> " + s.returns : ""}`,
        documentation: s.doc || "",
        parameters: s.params.map((p) => ({ label: p })),
      })),
      activeParameter: commas,
    };
  }

  // Outline: top-level symbols with members nested.
  outline(file) {
    const info = this.info(file);
    if (!info) return [];
    const top = [];
    const nsNodes = new Map();
    const node = (s) => ({
      name: s.name, detail: s.detail.length > 80 ? s.detail.slice(0, 80) + "…" : s.detail, kind: s.kind,
      range: { start: { line: s.line, character: 0 }, end: { line: s.endLine !== undefined ? s.endLine : s.line, character: 200 } },
      selectionRange: s.range, children: [],
    });
    const byName = new Map();
    for (const s of info.symbols) {
      if (s.local || s.isParam) continue;
      const n = node(s);
      if (s.container && byName.has(s.container)) byName.get(s.container).children.push(n);
      else if (s.namespace && nsNodes.has(s.namespace)) nsNodes.get(s.namespace).children.push(n);
      else if (s.kind === KIND.Namespace && s.namespace && nsNodes.has(s.namespace)) nsNodes.get(s.namespace).children.push(n);
      else top.push(n);
      if (s.kind === KIND.Namespace) nsNodes.set((s.namespace ? s.namespace + "." : "") + s.name, n);
      if ([KIND.Class, KIND.Struct, KIND.Interface, KIND.Enum].includes(s.kind)) byName.set(s.name, n);
    }
    return top;
  }

  // Workspace symbols matching a query.
  workspaceSymbols(query, includeLibrary) {
    const q = query.toLowerCase();
    const out = [];
    const files = this.workspaceFiles();
    if (includeLibrary) {
      const std = this.env().std;
      const walk = (d) => { try { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const f = path.join(d, e.name); if (e.isDirectory()) walk(f); else if (e.name.endsWith(".grav")) files.push(f); } } catch {} };
      if (std) walk(std);
    }
    for (const f of files) {
      const info = this.info(f);
      if (!info) continue;
      for (const s of info.symbols) {
        if (s.local || s.isParam) continue;
        if (q && !s.name.toLowerCase().includes(q)) continue;
        out.push({ file: f, sym: s });
        if (out.length > 500) return out;
      }
    }
    return out;
  }

  // Whole-word occurrences of a name (code only, not strings/comments).
  references(file, pos, includeLibrary) {
    const w = wordAtPosition(this.text(file) || "", pos);
    if (!w) return [];
    const re = new RegExp(`(?<![A-Za-z0-9_])${w.word.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}(?![A-Za-z0-9_])`, "g");
    const out = [];
    const files = new Set([...this.workspaceFiles(), ...this.reachable(file)]);
    for (const f of files) {
      if (!includeLibrary && this.isLibrary(f) && path.resolve(f) !== path.resolve(file)) continue;
      const text = this.text(f);
      if (text == null) continue;
      const lines = splitLines(mask(text));
      for (let i = 0; i < lines.length; i++) {
        let m;
        while ((m = re.exec(lines[i])) !== null) out.push({ file: f, range: range(i, m.index, w.word.length) });
      }
    }
    return out;
  }

  // ---- compiler-backed features ----
  // Diagnostics from `grav check --json` (for a file saved on disk).
  check(file, cb) {
    execFile(this.compiler, ["check", "--json", file], { timeout: 20000, cwd: path.dirname(file) }, (err, stdout) => {
      let diags = [];
      try { diags = JSON.parse(stdout).diagnostics || []; } catch { diags = []; }
      cb(diags.map((d) => ({
        file: d.file ? path.resolve(path.dirname(file), d.file) : file,
        line: Math.max(0, (d.line || 1) - 1), character: Math.max(0, (d.column || 1) - 1),
        severity: d.severity, message: d.message + (d.note ? "\n" + d.note : ""), stage: d.stage,
      })));
    });
  }
  // `grav fmt -`: the formatted text (or null when the compiler is unavailable).
  format(text) {
    try { return execFileSync(this.compiler, ["fmt", "-"], { input: text, timeout: 10000 }).toString(); } catch { return null; }
  }
}

module.exports = {
  KIND, COMPLETION_KIND, KEYWORDS, TYPES, BUILTINS, INTERNAL_BUILTINS, KEYWORD_DOCS, SNIPPETS, splitLines, mask, parseFile, wordAtPosition,
  Workspace, inferType,
};
