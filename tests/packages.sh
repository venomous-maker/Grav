#!/usr/bin/env bash
# The package manager against local git repositories and directories (no network
# needed): add (with a transitive dependency), import, update, install from
# grav.lock, remove, and packages used in place from a local directory. With
# node, the editor index (editors/lsp/grav-index.js) must resolve the same imports.
set -u
cd "$(dirname "$0")/.."
REPO="$(pwd)" INDEX="$(pwd)/editors/lsp/grav-index.js"
GRAV="${GRAVC:-build/gravc}"
case "$GRAV" in /*) ;; *) GRAV="$(pwd)/$GRAV" ;; esac
command -v git >/dev/null || { echo "packages: skipped (no git)"; exit 0; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
export GRAV_PACKAGES="$T/cache" GIT_CONFIG_NOSYSTEM=1 HOME="$T"
git config --global user.email t@t; git config --global user.name t; git config --global init.defaultBranch main
fail() { echo "FAIL (packages) $1"; exit 1; }
# Where the editor's go to definition goes from the first `word` in a file (on an
# import string: the module it opens); "-" without node.
edef() {
  command -v node >/dev/null || { echo -; return; }
  node -e 'const [index, compiler, file, word] = process.argv.slice(1);
    const idx = require(index), lines = require("fs").readFileSync(file, "utf8").split("\n");
    const line = lines.findIndex((l) => l.includes(word));
    const ws = new idx.Workspace({ root: require("path").dirname(file), compiler });
    console.log(ws.definition(file, { line, character: lines[line].indexOf(word) + 1 }).map((d) => d.file).join(" "));' \
    "$INDEX" "$GRAV" "$1" "$2"
}

mkdir "$T/colors" && cd "$T/colors" && git init -q &&
  printf 'fn red(s: string) -> string { return "<" + s + ">" }\n' > lib.grav && git add -A && git commit -qm c && git tag v0.1.0
mkdir "$T/greet" && cd "$T/greet" && git init -q &&
  printf 'name = "greet"\nlib = "src/greet.grav"\n\n[dependencies]\ncolors = "%s/colors"\n' "$T" > grav.toml &&
  mkdir src && printf 'import "colors"\nfn greet(n: string) -> string { return red("hello " + n) }\n' > src/greet.grav &&
  git add -A && git commit -qm one && git tag v1.0.0 &&
  sed -i 's/hello /hi /' src/greet.grav && git commit -qam two && git tag v1.1.0

cd "$T" && "$GRAV" new app >/dev/null 2>&1 && cd app || fail "grav new"
"$GRAV" add "$T/greet@v1.0.0" >/dev/null 2>&1 || fail "grav add"
printf 'import "greet"\nfn main() { print(greet("grav")) }\n' > src/main.grav
[ "$("$GRAV" run 2>/dev/null)" = "<hello grav>" ] || fail "import of an added package"
grep -q 'colors = ' grav.lock || fail "transitive dependency in grav.lock"
"$GRAV" update greet >/dev/null 2>&1 || fail "grav update"
[ "$("$GRAV" run 2>/dev/null)" = "<hi grav>" ] || fail "update to the newest tag"
rm -rf "$GRAV_PACKAGES"
"$GRAV" run >/dev/null 2>&1 && fail "a missing package should be an error"
"$GRAV" install >/dev/null 2>&1 || fail "grav install"
[ "$("$GRAV" run 2>/dev/null)" = "<hi grav>" ] || fail "install from grav.lock"
# grav.toml edited by hand to another version than grav.lock holds: the lock's
# commit is the old version's, so install fetches the one asked for (an older
# grav checked out the lock's commit and filed it under the new version's name).
sed -i 's/greet@v1.1.0/greet@v1.0.0/' grav.toml
"$GRAV" install >/dev/null 2>&1 || fail "install after editing the version"
[ "$("$GRAV" run 2>/dev/null)" = "<hello grav>" ] || fail "install of a hand-edited version"
sed -i 's/greet@v1.0.0/greet@v1.1.0/' grav.toml
"$GRAV" install >/dev/null 2>&1 || fail "install after editing the version back"
[ "$("$GRAV" run 2>/dev/null)" = "<hi grav>" ] || fail "install of the version edited back"
case "$(edef src/main.grav '"greet"')" in -|"$GRAV_PACKAGES"/*/greet@v1.1.0/src/greet.grav) ;; *) fail "editor: import of a git package";; esac
case "$(edef src/main.grav 'greet(')" in -|"$GRAV_PACKAGES"/*/greet@v1.1.0/src/greet.grav) ;; *) fail "editor: definition in a git package";; esac
case "$(edef "$(find "$GRAV_PACKAGES" -path '*/greet@v1.1.0/src/greet.grav')" 'red(')" in -|"$GRAV_PACKAGES"/*/colors@*/lib.grav) ;; *) fail "editor: a package's own dependency";; esac
"$GRAV" remove greet >/dev/null 2>&1 || fail "grav remove"
grep -q greet grav.toml && fail "remove left the dependency in grav.toml"

# A package in a local directory: used in place (no clone, edits show at once),
# its own dependencies installed, its namespace's names usable without a prefix.
mkdir -p "$T/shapes/src" && cd "$T/shapes" &&
  printf 'name = "shapes"\nlib = "src/shapes.grav"\n\n[dependencies]\ncolors = "%s/colors"\n' "$T" > grav.toml &&
  printf 'import "colors"\nnamespace shapes {\n    fn square(n: int) -> string { return red("${n * n}") }\n}\n' > src/shapes.grav
cd "$T" && "$GRAV" new app2 >/dev/null 2>&1 && cd app2 || fail "grav new app2"
grep -q 'name = "app2"' grav.toml || fail "grav new names the project after its directory"
"$GRAV" add ../shapes >/dev/null 2>&1 || fail "grav add of a local directory"
grep -q 'shapes = { path = "../shapes" }' grav.toml || fail "a local directory is recorded as { path = ... }"
printf 'import "shapes"\nfn main() { print(square(4)) }\n' > src/main.grav
[ "$("$GRAV" run 2>/dev/null)" = "<16>" ] || fail "import of a local package (and its git dependency)"
case "$(edef src/main.grav '"shapes"')" in -|"$T/shapes/src/shapes.grav") ;; *) fail "editor: import of a local package";; esac
case "$(edef src/main.grav 'square(')" in -|"$T/shapes/src/shapes.grav") ;; *) fail "editor: definition in a local package";; esac
# The MCP server's grav_check: inline code compiled in a project sees its packages.
mcp_check() { GRAVC="$GRAV" GRAV_DIR="$T/bin" python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import server
print(server.grav_check(source="import \"shapes\"\nfn main() { print(square(2)) }\n", project=sys.argv[2] or None))' \
  "$REPO/mcp_server" "$1" 2>&1; }
if command -v python3 >/dev/null; then
  mcp_check "$T/app2" | grep -q "^ok: no errors" || fail "mcp: inline source checked in a project"
  ls -A "$T/app2" | grep -q grav-mcp && fail "mcp: the inline source file was left in the project"
  mcp_check "" | grep -q "pass \`project\`" || fail "mcp: inline source outside a project hints at project"
fi
sed -i 's/n \* n/n + n/' "$T/shapes/src/shapes.grav"
[ "$("$GRAV" run 2>/dev/null)" = "<8>" ] || fail "an edit to a local package shows without reinstalling"
"$GRAV" packages 2>/dev/null | grep -q "shapes  path ../shapes  (local)" || fail "grav packages lists the local package"
# The program's own declaration of a package wins over a package's: a local
# checkout of colors replaces the git one shapes asks for (one copy for all).
mkdir -p "$T/colors-dev" && printf 'fn red(s: string) -> string { return "[" + s + "]" }\n' > "$T/colors-dev/lib.grav"
cp grav.toml grav.toml.bak && printf 'colors = { path = "%s/colors-dev" }\n' "$T" >> grav.toml
"$GRAV" install >/dev/null 2>&1 || fail "install with a local override"
[ "$("$GRAV" run 2>/dev/null)" = "[8]" ] || fail "the project's own package replaces a dependency's"
mv grav.toml.bak grav.toml
# Two dependencies declaring the same package, one as a local path and one from
# git: the program gets one copy (the local one), not two.
mkdir -p "$T/paint/src" && printf 'name = "paint"\nlib = "src/paint.grav"\n\n[dependencies]\ncolors = { path = "%s/colors-dev" }\n' "$T" > "$T/paint/grav.toml" &&
  printf 'import "colors"\nfn paint(s: string) -> string { return red(s) }\n' > "$T/paint/src/paint.grav"
cp grav.toml grav.toml.bak && printf 'paint = { path = "%s/paint" }\n' "$T" >> grav.toml
printf 'import "shapes"\nimport "paint"\nfn main() { print(square(3) + paint("x")) }\n' > src/main.grav
[ "$("$GRAV" run 2>/dev/null)" = "[6][x]" ] || fail "a package declared twice in the graph is loaded once (the local copy)"
mv grav.toml.bak grav.toml
printf 'import "shapes"\nfn main() { print(square(4)) }\n' > src/main.grav
mv "$T/shapes" "$T/shapes-moved"
"$GRAV" run 2>&1 | grep -q "local package 'shapes': no directory" || fail "a missing local package is named"

# `grav = "..."` in grav.toml: the compiler versions a project or package builds with.
have="$("$GRAV" env | awk '/^GRAV_VERSION/ {print $2}')"
IFS=. read -r maj min pat <<<"$have"
mkdir -p "$T/vers/src" && cd "$T/vers" && printf 'fn main() { print("ok") }\n' > src/main.grav
need() { printf 'name = "vers"\nmain = "src/main.grav"\ngrav = "%s"\n' "$1" > grav.toml; }
for req in ">=$have" "$have" "=$have" "^$maj" "~$maj.$min" "<99" ">=1.0, <99" ">$maj.$((min - 1))"; do
  need "$req"; [ "$("$GRAV" run 2>/dev/null)" = "ok" ] || fail "grav = \"$req\" should build with grav $have"
done
for req in ">$have" "<$have" "^$((maj + 1))" "~$maj.$((min + 1))" ">=1.0, <$maj" "=$maj.$min.$((pat + 1))"; do
  need "$req"; "$GRAV" run >/dev/null 2>&1 && fail "grav = \"$req\" should not build with grav $have"
done
need ">=99.0"
"$GRAV" run 2>&1 | grep -q "vers needs grav >=99.0, and this is grav $have (update it with: grav self-update)" ||
  fail "an unmet requirement of the project names both versions"
mkdir -p "$T/future" && printf 'name = "future"\nlib = "lib.grav"\ngrav = "^99"\n' > "$T/future/grav.toml" &&
  printf 'fn soon() -> string { return "soon" }\n' > "$T/future/lib.grav"
need ">=$have"
"$GRAV" add ../future 2>&1 | grep -q "future needs grav ^99" || fail "adding a package this grav cannot build is an error"
need ">=$have" && printf '\n[dependencies]\nfuture = { path = "../future" }\n' >> grav.toml
printf 'import "future"\nfn main() { print(soon()) }\n' > src/main.grav
"$GRAV" run 2>&1 | grep -q "future needs grav ^99" || fail "building with a package this grav cannot build is an error"
sed -i "s/\\^99/>=$have/" "$T/future/grav.toml"
[ "$("$GRAV" run 2>/dev/null)" = "soon" ] || fail "a package whose requirement is met builds"
echo "packages: ok"
