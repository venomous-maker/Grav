#!/usr/bin/env bash
# Releases a chain of Grav packages in dependency order:
#
#   tools/release-chain.sh plan.txt [--base DIR] [--from NAME] [-y]
#
# plan.txt holds one "<package> <X.Y.Z>" per line, each package after the ones
# it depends on ("#" starts a comment). For each, in its checkout DIR/<package>
# (DIR defaults to the current directory), it reinstalls the dependencies from their tags (so grav.lock records the
# commits the releases just pushed), commits the lock if it changed, and runs
# tools/release.sh for the version. It stops at the first failure; --from NAME
# picks up there.
set -euo pipefail

PLAN="" BASE="$PWD" FROM="" YES=""
while [ $# -gt 0 ]; do
    case "$1" in
        --base) BASE="$2"; shift ;;
        --from) FROM="$2"; shift ;;
        -y|--yes) YES="-y" ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "release-chain: unknown option '$1'" >&2; exit 2 ;;
        *) PLAN="$1" ;;
    esac
    shift
done
[ -f "$PLAN" ] || { echo "usage: tools/release-chain.sh plan.txt [--base DIR] [--from NAME] [-y]" >&2; exit 2; }
RELEASE="$(cd "$(dirname "$0")" && pwd)/release.sh"

started=$([ -z "$FROM" ] && echo 1 || echo 0)
while read -r name version _; do
    [ -z "$name" ] || [ "${name:0:1}" = "#" ] && continue
    [ "$name" = "$FROM" ] && started=1
    [ "$started" = 1 ] || continue
    dir="$BASE/$name"
    [ -f "$dir/grav.toml" ] || { echo "release-chain: no package at $dir" >&2; exit 1; }
    if git -C "$dir" rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
        printf '\n\e[1m==> %s v%s is already released\e[0m\n' "$name" "$version"; continue
    fi
    printf '\n\e[1m==> %s v%s: dependencies from their tags\e[0m\n' "$name" "$version"
    (cd "$dir" && rm -f grav.lock && grav install)
    if [ -n "$(git -C "$dir" status --porcelain grav.lock)" ]; then
        git -C "$dir" add grav.lock
        git -C "$dir" commit -q -m "deps: lock the released versions"
    fi
    bash "$RELEASE" "$version" "$dir" $YES
done < "$PLAN"
