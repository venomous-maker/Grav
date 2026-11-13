#!/usr/bin/env bash
# What editors are told about types: `grav check --types` (the compiler's type of
# every local binding) and the inlay hints of the editor index
# (editors/lsp/grav-index.js) with and without it.
set -u
cd "$(dirname "$0")/.."
GRAV="${GRAVC:-build/gravc}"
case "$GRAV" in /*) ;; *) GRAV="$(pwd)/$GRAV" ;; esac
INDEX="$(pwd)/editors/lsp/grav-index.js"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL (editors) $1"; exit 1; }

cat > "$T/types.grav" <<'EOF'
import "std"
enum Shape { Circle(r: float), Square(side: float) }
fn first<T>(xs: Array<T>) -> T { let y = xs.get(0)
    return y }
fn main() {
    let fixed = [1, 2, 3]
    let names = new Array<string>()
    let byName = new HashMap<string, Array<int>>()
    let show = fn(n: int) -> string { return "${n}" }
    let maybe: long? = null
    let sh = Shape.Circle(1.0)
    for (i, x in fixed) { print(i + x) }
    for (s in names) { print(s) }
    for (k in byName.keys()) { print(k) }
    for (n in 0..10) { print(n) }
    try { throw new Exception("x") } catch (err: Exception) { print(err.message) }
    names.forEach(fn(v) { print(v) })
    match (sh) {
        case Circle(r): print(r)
        case Square(side): print(side)
    }
    print(first(names) + "${first(new Array<int>())}" + show(1) + "${maybe}" + "${byName.size()}")
}
EOF

# The compiler's types, printed as written in Grav (no mangled or std. names).
"$GRAV" check --types "$T/types.grav" > "$T/types.json" || fail "grav check --types on a valid file"
for want in '6 fixed int[3]' '7 names Array<string>' '8 byName HashMap<string, Array<int>>' \
            '9 show fn(int) -> string' '10 maybe long?' '11 sh Shape' '12 i int' '12 x int' \
            '13 s string' '14 k string' '15 n int' '16 err Exception' '17 v string' '19 r float' '20 side float'; do
  set -- $want; line=$1 name=$2; shift 2
  grep -qF "{\"line\": $line, \"column\": " "$T/types.json" || fail "no types on line $line"
  grep -F "\"line\": $line, " "$T/types.json" | grep -qF "\"name\": \"$name\", \"type\": \"$*\"}" ||
    fail "grav check --types: $name on line $line should be $*"
done
grep -q '"name": "y"' "$T/types.json" && fail "a generic function's local has one type per instance: leave it out"
printf 'fn main() {\n    let a = 1\n    let b: string = a\n}\n' > "$T/bad.grav"
"$GRAV" check --types "$T/bad.grav" | grep -q '"name": "a", "type": "int"' || fail "types are reported despite a type error"
"$GRAV" check --json "$T/bad.grav" | grep -q '"types"' && fail "--json alone reports no types"

command -v node >/dev/null || { echo "editors: ok (index skipped: no node)"; exit 0; }
# Inlay hints as "line: name: Type", from the index alone or after a check.
hints() { # compiler, then "check" to run it first
  node -e 'const [index, file, compiler, run] = process.argv.slice(1);
    const ws = new (require(index).Workspace)({ compiler });
    const lines = require("fs").readFileSync(file, "utf8").split("\n");
    const show = () => { for (const h of ws.inlayHints(file, 0, Infinity, { parameterNames: false })) {
      const before = lines[h.position.line].slice(0, h.position.character);
      console.log(`${h.position.line + 1}: ${before.match(/\w+$/)[0]}${h.label}`); } };
    if (run) ws.check(file, show); else show();' "$INDEX" "$T/types.grav" "$1" "${2:-}"
}
hints "$GRAV" check > "$T/checked.txt"
for want in '6: fixed: int[3]' '9: show: fn(int) -> string' '12: x: int' '14: k: string' '17: v: string' \
            '19: r: float' '20: side: float'; do
  grep -qxF "$want" "$T/checked.txt" || fail "inlay hint after a check: $want"
done
grep -q '^10: maybe' "$T/checked.txt" && fail "no hint where the type is written"
grep -q '^16: err' "$T/checked.txt" && fail "no hint for a catch variable (its type is written)"
# Without the compiler: what the source shows, and nothing invented.
hints /nonexistent/grav > "$T/source.txt"
for want in '7: names: Array<string>' '11: sh: Shape' '12: i: int' '13: s: string' '15: n: int'; do
  grep -qxF "$want" "$T/source.txt" || fail "inlay hint from the source: $want"
done
grep -qxF '14: k: string' "$T/source.txt" || fail "a generic's type parameter is bound (HashMap<string, ...>.keys() gives strings)"
# Methods on built-in values (std/types): `n.toHex()` is `Integer.toHex(n)`.
cat > "$T/values.grav" <<'EOF'
import "std"
fn main() {
    let n = 255
    let port: long = 8080
    let d = 19.99
    let maybe: int? = null
    let hex = n.toHex()
    let money = d as Decimal
    let back = "42" as int
    let parts = "a,b".split(",")
    let or = maybe.orElse(0)
    let cmp: fn(int, int) -> int = Integer.compare
    let upper = String.upper
    let big = Infinity
    print(hex + "${money}${back}${parts.size()}${or}${cmp(1, 2)}${upper("x")}${big}${port.grouped()}${(-7).abs()}")
}
EOF
"$GRAV" check "$T/values.grav" >/dev/null 2>&1 || fail "values.grav should compile"
node -e 'const [index, file] = process.argv.slice(1);
  const ws = new (require(index).Workspace)({ compiler: "/nonexistent/grav" });
  const text = require("fs").readFileSync(file, "utf8"), lines = text.split("\n");
  const at = (w, d = 1) => { const l = lines.findIndex((x) => x.includes(w)); return { line: l, character: lines[l].indexOf(w) + d }; };
  const bad = [];
  const expect = (what, got, want) => { if (got !== want) bad.push(`${what}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); };
  // completion after `x.` at the end of main
  const complete = (expr) => { const end = lines.lastIndexOf("}");
    ws.setDoc(file, [...lines.slice(0, end), "    " + expr, ...lines.slice(end)].join("\n"));
    const r = ws.completion(file, { line: end, character: 4 + expr.length }); ws.setDoc(file, text); return r; };
  const item = (expr, label) => complete(expr).find((c) => c.label === label);
  expect("n. toHex", (item("n.", "toHex") || {}).detail, "toHex() -> string");
  expect("port. grouped", (item("port.", "grouped") || {}).detail, "grouped() -> string");
  expect("d. round", !!item("d.", "round"), true);
  expect("d. no toHex", !!item("d.", "toHex"), false);
  expect("maybe. orElse", (item("maybe.", "orElse") || {}).detail, "orElse(fallback: int) -> int");
  expect("\"x\". split", (item("\"x\".", "split") || {}).detail, "split(delim: string) -> Array<string>");
  expect("hex. upper", !!item("hex.", "upper"), true);
  expect("(-7). abs", !!item("(-7).", "abs"), true);
  expect("parts. toMap (Object)", !!item("parts.", "toMap"), true);
  expect("n.toHex doc", /std\/types/.test((item("n.", "toHex") || {}).documentation || ""), true);
  // definition, hover, signature help
  const def = ws.definition(file, at("toHex"));
  expect("definition of n.toHex", def.length === 1 && /types\/integer\.grav$/.test(def[0].file), true);
  const h = ws.hover(file, at("toHex"));
  expect("hover n.toHex", h && h.markdown.includes("`n.toHex()` is `Integer.toHex(n)`"), true);
  const sig = ws.signature(file, at("orElse(", 7));
  expect("signature maybe.orElse(", sig && sig.signatures[0].label, "orElse(fallback: int) -> int");
  // the types of call results, casts and method values, from the source alone
  const hints = Object.fromEntries(ws.inlayHints(file).filter((x) => x.kind === 1).map((x) => [lines[x.position.line].trim().split(/\s+/)[1], x.label]));
  for (const [name, want] of [["hex", ": string"], ["money", ": Decimal"], ["back", ": int"], ["parts", ": Array<string>"],
                              ["or", ": int"], ["upper", ": fn(string) -> string"], ["big", ": float"]])
    expect("inlay hint " + name, hints[name], want);
  if (bad.length) { console.log(bad.join("\n")); process.exit(1); }' "$INDEX" "$T/values.grav" || fail "methods on built-in values in the editor"

# Refactoring and completion around classes: rename by declaration, organize
# imports, reflect/super/cast/decorator completion, generics bound, parameter hints.
mkdir -p "$T/ref"
cat > "$T/ref/model.grav" <<'EOF'
@Table("counters")
class Counter {
    count: int = 0
    static made: int = 0
    fn bump() { self.count = self.count + 1 }
    fn size() -> int { return self.count }
}
class Named extends Counter {
    fn size() -> int { return super.size() + 1 }
}
EOF
cat > "$T/ref/main.grav" <<'EOF'
import "std"
import "model.grav"
import "std"
fn area(width: int, height: int) -> int { return width * height }
fn main() {
    let count = 0
    let c = new Counter()
    c.bump()
    let xs = new Array<int>()
    let total = count + c.size() + c.count + xs.size() + area(3, 4)
    let info = reflect(Counter)
    let d = 19.99
    print("${total} ${info.name} ${d}")
}
EOF
"$GRAV" check "$T/ref/main.grav" >/dev/null 2>&1 || fail "ref/main.grav should compile"
node -e 'const [index, dir] = process.argv.slice(1);
  const path = require("path"), fs = require("fs");
  const ws = new (require(index).Workspace)({ root: dir, compiler: "/nonexistent/grav" });
  const main = path.join(dir, "main.grav"), model = path.join(dir, "model.grav");
  const lines = fs.readFileSync(main, "utf8").split("\n");
  const at = (w, d = 1) => { const l = lines.findIndex((x) => x.includes(w)); return { line: l, character: lines[l].indexOf(w) + d }; };
  const bad = [], expect = (what, got, want) => { if (JSON.stringify(got) !== JSON.stringify(want)) bad.push(`${what}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); };
  const where = (edits) => edits.error ? edits.error : edits.map((e) => path.basename(e.file) + ":" + (e.range.start.line + 1) + ":" + (e.range.start.character + 1)).sort();
  // rename: the local, the field and the method each only where they are meant
  expect("rename the local count", where(ws.rename(main, at("count = 0", 0))), ["main.grav:10:17", "main.grav:6:9"]);
  expect("rename the field count", where(ws.rename(main, at("c.count", 2))), ["main.grav:10:38", "model.grav:3:5", "model.grav:5:22", "model.grav:5:35", "model.grav:6:36"]);
  expect("rename Counter.size", where(ws.rename(main, at("c.size", 2))), ["main.grav:10:27", "model.grav:6:8", "model.grav:9:37"]);
  expect("a library name is not renamed", /standard library/.test(ws.rename(main, at("xs.size", 3)).error || ""), true);
  // organize imports
  expect("organize imports", (ws.organizeImports(main) || {}).newText, "import \"std\"\n\nimport \"model.grav\"");
  // completion
  const complete = (expr, file = main, line = lines.findIndex((x) => x.includes("print(")) ) => {
    const text = fs.readFileSync(file, "utf8"), ls = text.split("\n");
    ws.setDoc(file, [...ls.slice(0, line), "    " + expr, ...ls.slice(line)].join("\n"));
    const r = ws.completion(file, { line, character: 4 + expr.length }).map((c) => c.label); ws.setDoc(file, text); return r; };
  expect("reflect(T). is a ClassInfo", complete("info.").includes("fields"), true);
  expect("info.fields()[0]. is a FieldInfo", complete("info.fields()[0].").includes("decorator"), true);
  expect("d as (a float)", complete("d as ").filter((x) => ["int", "string", "Decimal"].includes(x)).sort(), ["Decimal", "int", "string"]);
  expect("@ offers used decorators", complete("@"), ["Table"]);
  const ml = fs.readFileSync(model, "utf8").split("\n").findIndex((x) => x.includes("super.size"));
  expect("super. reaches fields, methods and statics", ["count", "size", "made"].every((x) => complete("super.", model, ml).includes(x)), true);
  expect("generics bound: Array<int>.get()", ws.typeOfExpr(main, at("print("), "xs.get(0)"), "int");
  // hover on a decorator, parameter-name hints
  const ms = fs.readFileSync(model, "utf8").split("\n");
  expect("hover on @Table", /a decorator/.test((ws.hover(model, { line: 0, character: 2 }) || {}).markdown || ""), true);
  const names = ws.inlayHints(main).filter((h) => h.kind === 2).map((h) => h.label);
  expect("parameter names before literal arguments", names, ["width:", "height:"]);
  if (bad.length) { console.log(bad.join("\n")); process.exit(1); }' "$INDEX" "$T/ref" || fail "refactoring and completion around classes"

# A class gets the fields, statics and methods of the traits it implements.
cat > "$T/traits.grav" <<'EOF'
import "std"
trait Versioned {
    public version: int = 1
    static made: int = 0
    private fn bumped() -> int { return self.version + 1 }
    fn bump() { self.version = self.bumped() }
}
class Page implements Versioned {
    public body: string = ""
}
fn main() {
    let p = new Page()
    p.bump()
    print("${p.version} ${Page.made}")
}
EOF
"$GRAV" check "$T/traits.grav" >/dev/null 2>&1 || fail "traits.grav should compile"
node -e 'const [index, file] = process.argv.slice(1);
  const ws = new (require(index).Workspace)({ compiler: "/nonexistent/grav" });
  const text = require("fs").readFileSync(file, "utf8"), lines = text.split("\n"), at = 13;
  const comp = (e) => { ws.setDoc(file, [...lines.slice(0, at), "    " + e, ...lines.slice(at)].join("\n"));
    const r = ws.completion(file, { line: at, character: 4 + e.length }).map((c) => c.label); ws.setDoc(file, text); return r; };
  const bad = [];
  if (!["version", "bump", "body"].every((x) => comp("p.").includes(x))) bad.push("p. lacks the trait members");
  if (comp("p.").includes("bumped")) bad.push("a trait private helper is offered outside the class");
  if (!comp("Page.").includes("made")) bad.push("Page. lacks the trait static");
  if (ws.typeOfExpr(file, { line: at, character: 4 }, "p.version") !== "int") bad.push("p.version is not an int");
  if (bad.length) { console.log(bad.join("\n")); process.exit(1); }' "$INDEX" "$T/traits.grav" || fail "a class gets its traits' members"

# A compiler older than --types still gives diagnostics.
printf '#!/bin/sh\nfor a; do [ "$a" = --types ] && { echo "gravc: unknown option '"'"'--types'"'"'" >&2; echo "{\\"diagnostics\\": []}"; exit 2; }; done\nexec "%s" "$@"\n' "$GRAV" > "$T/oldgrav"
chmod +x "$T/oldgrav"
node -e 'const [index, file, compiler] = process.argv.slice(1);
  new (require(index).Workspace)({ compiler }).check(file, (d) => process.exit(d.some((x) => /cannot initialize/.test(x.message)) ? 0 : 1));' \
  "$INDEX" "$T/bad.grav" "$T/oldgrav" || fail "diagnostics from a compiler without --types"
# Field initializers and named decorator arguments: no false errors, and the
# fields' types (as written, `?` kept) reach the index.
cat > "$T/model.grav" <<'EOF'
import "std"
@Table("users")
class User {
    @Column("email_address", length: 255, unique: true)
    public email: string = ""
    public tags: Array<string> = new Array<string>()
    public joined: DateTime? = null
}
fn main() {
    let u = new User()
    let t = u.tags
    let j = u.joined
    print(u.email + "${t.size()} ${j}")
}
EOF
"$GRAV" check --json "$T/model.grav" | grep -q '"errors": 0' || fail "field initializers and named decorator arguments check cleanly"
node -e 'const [index, file] = process.argv.slice(1);
  const f = require(index).parseFile(require("fs").readFileSync(file, "utf8"));
  const user = f.symbols.find((s) => s.name === "User");
  const names = (user.members || []).map((m) => m.name).join(",");
  if (names !== "email,tags,joined") { console.log("members: " + names); process.exit(1); }' "$INDEX" "$T/model.grav" ||
  fail "the index lists a class's fields with initializers"
node -e 'const [index, file] = process.argv.slice(1);
  const ws = new (require(index).Workspace)({ compiler: "/nonexistent/grav" });
  const got = ws.inlayHints(file).filter((h) => h.kind === 1).map((h) => h.label).join(" ");
  process.exit(got === ": User : Array<string> : DateTime?" ? 0 : 1);' "$INDEX" "$T/model.grav" ||
  fail "types through fields with initializers"
# Properties: one member per get/set pair, typed, completed after `obj.`, and
# read through in types; `get`/`set` before `name(` hover as accessors.
cat > "$T/props.grav" <<'EOF'
import "std"
class Temp {
    private c: float = 0.0
    get celsius() -> float { return self.c }
    set celsius(v: float) { self.c = v }
    get label() -> string { return "${self.c}" }
}
fn main() {
    let t = new Temp()
    t.celsius = 1.0
    print(t.label)
}
EOF
"$GRAV" check "$T/props.grav" >/dev/null 2>&1 || fail "props.grav should compile"
node -e 'const [index, file] = process.argv.slice(1);
  const g = require(index), text = require("fs").readFileSync(file, "utf8"), lines = text.split("\n"), at = 11;
  const ws = new g.Workspace({ compiler: "/nonexistent/grav" });
  const temp = g.parseFile(text).symbols.find((s) => s.name === "Temp");
  const bad = [];
  const props = (temp.members || []).filter((m) => m.kind === g.KIND.Property).map((m) => `${m.name}:${m.type}:${m.writable}`).join(",");
  if (props !== "celsius:float:true,label:string:false") bad.push("properties: " + props);
  const comp = (e) => { ws.setDoc(file, [...lines.slice(0, at), "    " + e, ...lines.slice(at)].join("\n"));
    const r = ws.completion(file, { line: at, character: 4 + e.length }).map((c) => c.label); ws.setDoc(file, text); return r; };
  const got = comp("t.");
  if (!got.includes("celsius") || !got.includes("label") || got.some((x) => /^(get|set) |__get_/.test(x))) bad.push("t. offers " + got.join(" "));
  if (ws.typeOfExpr(file, { line: at, character: 4 }, "t.label") !== "string") bad.push("t.label is not a string");
  const h = ws.hover(file, { line: 3, character: 5 });
  if (!h || !/getter/.test(h.markdown)) bad.push("no accessor hover on get");
  if (bad.length) { console.log(bad.join("\n")); process.exit(1); }' "$INDEX" "$T/props.grav" || fail "properties in the index"
echo "editors: ok"
