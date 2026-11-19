#!/usr/bin/env bash
# Cuts a release of the Grav compiler or of a Grav package:
#
#   tools/release.sh 1.7.0                     the compiler (this checkout)
#   tools/release.sh 0.2.0 ../grav-orbit       a package's checkout
#
# It checks the checkout (on main, clean, up to date, the version newer than the
# last tag), takes the version's section from CHANGELOG.md (an "Unreleased"
# section becomes it), sets GRAV_VERSION for the compiler, runs the tests,
# commits "release: vX.Y.Z", tags it with the changelog section, and, after you
# confirm, pushes the branch and the tag and creates the GitHub release.
set -euo pipefail

VERSION="" DIR="" DRY=0 TEST=1 YES=0
usage() {
    cat <<'EOF'
usage: tools/release.sh <version> [checkout] [options]

  <version>    X.Y.Z (no leading v)
  [checkout]   the repository to release (default: the current directory)
  --dry-run    check, test and show what would happen; change nothing
  --no-test    skip the tests
  -y, --yes    do not ask before pushing
EOF
}
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1 ;;
        --no-test) TEST=0 ;;
        -y|--yes) YES=1 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "release: unknown option '$1'" >&2; exit 2 ;;
        *) if [ -z "$VERSION" ]; then VERSION="$1"; elif [ -z "$DIR" ]; then DIR="$1"; else usage; exit 2; fi ;;
    esac
    shift
done
[ -n "$VERSION" ] || { usage; exit 2; }
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "release: the version must be X.Y.Z, not '$VERSION'" >&2; exit 2; }
TAG="v$VERSION"
cd "${DIR:-.}"
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

step() { printf '\n\e[1m==> %s\e[0m\n' "$*"; }
ok()   { printf '  \e[32m✓\e[0m %s\n' "$*"; }
die()  { printf '\n\e[31merror:\e[0m %s\n' "$*" >&2; exit 1; }

if grep -q 'GRAV_VERSION=' CMakeLists.txt 2>/dev/null; then KIND=compiler NAME=grav
elif [ -f grav.toml ]; then KIND=package NAME="$(sed -n 's/^name *= *"\(.*\)"/\1/p' grav.toml | head -1)"; NAME="${NAME:-$(basename "$ROOT")}"
else die "$ROOT is neither the Grav compiler nor a package (no grav.toml)"; fi

# ── the checkout ───────────────────────────────────────────────────────────────
step "Checking $NAME ($KIND) for $TAG"
branch="$(git symbolic-ref -q --short HEAD || true)"
[ "$branch" = main ] || die "release from main (this is '${branch:-a detached HEAD}')"
[ -z "$(git status --porcelain)" ] || die "the checkout has uncommitted changes"
git fetch -q --tags origin
[ "$(git rev-list --count HEAD..origin/main)" = 0 ] || die "main is behind origin/main; pull first"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && die "$TAG already exists"
last="$(git tag -l 'v*' --sort=-v:refname | head -1)"
if [ -n "$last" ]; then
    [ "$(printf '%s\n%s\n' "${last#v}" "$VERSION" | sort -V | tail -1)" = "$VERSION" ] && [ "${last#v}" != "$VERSION" ] ||
        die "$TAG is not newer than the last release, $last"
    ok "last release $last, $(git rev-list --count "$last..HEAD") commits since"
else
    ok "first release"
fi
if [ "$KIND" = package ]; then
    grep -q '^grav *=' grav.toml && ok "needs $(sed -n 's/^grav *= *//p' grav.toml)" ||
        die "grav.toml does not say which compilers it needs: add grav = \">=X.Y.Z\""
fi

# ── the changelog ──────────────────────────────────────────────────────────────
[ -f CHANGELOG.md ] || die "no CHANGELOG.md"
section() {   # the body of "## <title>..." up to the next "## "
    awk -v want="$1" 'index($0, "## " want) == 1 {on=1; next} on && /^## / {exit} on {print}' CHANGELOG.md
}
notes="$(section "$VERSION")"
if [ -z "$(tr -d '[:space:]' <<<"$notes")" ]; then
    notes="$(section "Unreleased")"
    [ -n "$(tr -d '[:space:]' <<<"$notes")" ] || die "CHANGELOG.md has no '## $VERSION' section and nothing under '## Unreleased'"
    if [ "$DRY" = 0 ]; then
        # "## Unreleased" becomes this version; a new empty one goes above it.
        awk -v head="## $VERSION ($(date +%Y-%m-%d))" '/^## Unreleased/ && !done {print; print ""; print head; done=1; next} {print}' \
            CHANGELOG.md > CHANGELOG.md.tmp && mv CHANGELOG.md.tmp CHANGELOG.md
    fi
    ok "the Unreleased section becomes $VERSION"
else
    ok "CHANGELOG.md has a $VERSION section"
fi
notes="$(sed -e '/./,$!d' <<<"$notes")"   # (no leading blank lines)

if [ "$KIND" = compiler ]; then
    cur="$(sed -n 's/.*GRAV_VERSION="\([^"]*\)".*/\1/p' CMakeLists.txt)"
    if [ "$cur" != "$VERSION" ]; then
        [ "$DRY" = 1 ] || sed -i "s/GRAV_VERSION=\"$cur\"/GRAV_VERSION=\"$VERSION\"/" CMakeLists.txt
        ok "GRAV_VERSION $cur -> $VERSION"
    else
        ok "GRAV_VERSION is $VERSION"
    fi
fi

# ── tests ──────────────────────────────────────────────────────────────────────
if [ "$TEST" = 1 ]; then
    step "Testing"
    if [ "$KIND" = compiler ]; then
        cmake --build build -j "$(nproc)" >/dev/null || die "the build failed"
        log="$(mktemp)"
        if ! bash tests/run.sh >"$log" 2>&1; then grep -E '^FAIL|^passed' "$log"; die "tests/run.sh failed (log: $log)"; fi
        grep -E '^passed|^packages|^doc snippets' "$log"
    else
        for t in tests/*.grav; do
            [ -f "$t" ] || continue
            out="$(grav test "$t" 2>&1)" || { echo "$out" | tail -20; die "$t failed"; }
            ok "$t: $(echo "$out" | grep '^result:' | tail -1)"
        done
    fi
fi

# ── commit, tag, push ──────────────────────────────────────────────────────────
step "Releasing $NAME $TAG"
echo "$notes" | sed 's/^/    /' | head -40
if [ "$DRY" = 1 ]; then
    git checkout -q -- CHANGELOG.md CMakeLists.txt 2>/dev/null || true
    echo; echo "Dry run: nothing committed, tagged or pushed."; exit 0
fi
if [ -n "$(git status --porcelain)" ]; then
    git commit -qam "release: $TAG"
    ok "committed release: $TAG"
fi
git tag -a "$TAG" --cleanup=verbatim -F - <<<"$NAME $TAG

$notes"
ok "tagged $TAG"

if [ "$YES" = 0 ]; then
    printf '\n  Push main and %s to %s and create the GitHub release? (y/n) [n] ' "$TAG" "$(git remote get-url origin)"
    read -r r </dev/tty || r=""
    case "$r" in [Yy]*) ;; *) echo "  Not pushed. Undo with: git tag -d $TAG && git reset --hard HEAD~1 (if a release commit was made)"; exit 0 ;; esac
fi
git push -q origin main
git push -q origin "$TAG"
ok "pushed main and $TAG"
if ! git remote get-url origin | grep -q 'github\.com'; then
    echo "  origin is not on GitHub: no GitHub release created"
elif command -v gh >/dev/null 2>&1; then
    gh release create "$TAG" --title "$NAME $TAG" --notes "$notes" >/dev/null
    ok "created the GitHub release"
else
    echo "  gh is not installed: create the release from the tag on GitHub"
fi
