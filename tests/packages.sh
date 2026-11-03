#!/usr/bin/env bash
# The package manager against local git repositories (no network needed):
# add (with a transitive dependency), import, update, install from grav.lock, remove.
set -u
cd "$(dirname "$0")/.."
GRAV="$(pwd)/${GRAVC:-build/gravc}"
command -v git >/dev/null || { echo "packages: skipped (no git)"; exit 0; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
export GRAV_PACKAGES="$T/cache" GIT_CONFIG_NOSYSTEM=1 HOME="$T"
git config --global user.email t@t; git config --global user.name t; git config --global init.defaultBranch main
fail() { echo "FAIL (packages) $1"; exit 1; }

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
"$GRAV" remove greet >/dev/null 2>&1 || fail "grav remove"
grep -q greet grav.toml && fail "remove left the dependency in grav.toml"
echo "packages: ok"
