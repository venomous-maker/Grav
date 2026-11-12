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
    const show = () => { for (const h of ws.inlayHints(file)) {
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
grep -q '^14: k' "$T/source.txt" && fail "a type parameter (Array<K>) is not a hint"
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
  const hints = Object.fromEntries(ws.inlayHints(file).map((x) => [lines[x.position.line].trim().split(/\s+/)[1], x.label]));
  for (const [name, want] of [["hex", ": string"], ["money", ": Decimal"], ["back", ": int"], ["parts", ": Array<string>"],
                              ["or", ": int"], ["upper", ": fn(string) -> string"], ["big", ": float"]])
    expect("inlay hint " + name, hints[name], want);
  if (bad.length) { console.log(bad.join("\n")); process.exit(1); }' "$INDEX" "$T/values.grav" || fail "methods on built-in values in the editor"

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
  const got = ws.inlayHints(file).map((h) => h.label).join(" ");
  process.exit(got === ": User : Array<string> : DateTime?" ? 0 : 1);' "$INDEX" "$T/model.grav" ||
  fail "types through fields with initializers"
echo "editors: ok"
