#!/usr/bin/env bash
# Removes what install.sh installed: PREFIX/bin/grav, PREFIX/share/grav (the
# standard library, the install record, and the source checkout when
# install.sh cloned it) and the lines it added to your shell startup file.
# Packages it installed with apt stay: other programs may use them.
set -euo pipefail

PREFIX="" YES=0 PURGE=0
usage() {
    cat <<'EOF'
usage: uninstall.sh [options]

  --prefix DIR   the prefix grav was installed under (default: found from the
                 grav on PATH, else ~/.local, else /usr/local)
  --purge        also delete the package cache (GRAV_PACKAGES)
  -y, --yes      ask nothing (keeps the package cache unless --purge)
  -h, --help     this help
EOF
}
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift ;;
        --purge) PURGE=1 ;;
        -y|--yes) YES=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "uninstall.sh: unknown option '$1' (see --help)" >&2; exit 2 ;;
    esac
    shift
done

if [ -t 1 ]; then G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' B=$'\e[1m' N=$'\e[0m'; else G="" Y="" R="" B="" N=""; fi
ok()   { printf '  %s✓%s %s\n' "$G" "$N" "$*"; }
warn() { printf '  %s!%s %s\n' "$Y" "$N" "$*"; }
die()  { printf '%serror:%s %s\n' "$R" "$N" "$*" >&2; exit 1; }

TTY=""
if [ "$YES" = 0 ] && { : </dev/tty; } 2>/dev/null; then TTY=/dev/tty; fi
confirm() {   # confirm "question" default(y|n)
    local r
    if [ -z "$TTY" ]; then r="$2"; else
        printf '  %s (y/n) [%s] ' "$1" "$2" >/dev/tty
        IFS= read -r r </dev/tty || r=""
        [ -n "$r" ] || r="$2"
    fi
    case "$r" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

# ── find the install ───────────────────────────────────────────────────────────
if [ -z "$PREFIX" ]; then
    g="$(command -v grav || true)"
    if [ -n "$g" ] && [ -f "$(dirname "$(readlink -f "$g")")/../share/grav/install.env" ]; then
        PREFIX="$(cd "$(dirname "$(readlink -f "$g")")/.." && pwd)"
    elif [ -f "$HOME/.local/share/grav/install.env" ]; then PREFIX="$HOME/.local"
    elif [ -f /usr/local/share/grav/install.env ]; then PREFIX=/usr/local
    else die "no grav installed by install.sh was found (pass --prefix)"; fi
fi
RECORD="$PREFIX/share/grav/install.env"
GRAV_SRC="" GRAV_CLONED=0 GRAV_PACKAGES="" GRAV_RC_FILE=""
if [ -f "$RECORD" ]; then
    # shellcheck disable=SC1090
    . "$RECORD"
else
    warn "no install record at $RECORD; removing only the standard locations"
fi

SUDO=""
if [ ! -w "$PREFIX/bin" ] || [ ! -w "$PREFIX/share" ]; then
    if [ "$(id -u)" != 0 ]; then command -v sudo >/dev/null 2>&1 && SUDO=sudo || die "$PREFIX needs root to change"; fi
fi

printf '%sRemoving grav from %s%s\n' "$B" "$PREFIX" "$N"
[ -z "$GRAV_SRC" ] || { [ "$GRAV_CLONED" = 1 ] && echo "  and its source checkout $GRAV_SRC" || echo "  (your checkout at $GRAV_SRC stays)"; }
confirm "Go ahead?" "$([ "$YES" = 1 ] && echo y || echo n)" || { echo "Nothing removed."; exit 0; }

# ── remove ─────────────────────────────────────────────────────────────────────
if [ -e "$PREFIX/bin/grav" ]; then $SUDO rm -f "$PREFIX/bin/grav"; ok "$PREFIX/bin/grav"; fi

if [ "$GRAV_CLONED" = 1 ] && [ -n "$GRAV_SRC" ] && [ -d "$GRAV_SRC" ]; then
    case "$GRAV_SRC" in
        "$PREFIX"/share/grav/*|"$HOME"/.local/share/grav/*) $SUDO rm -rf "$GRAV_SRC"; ok "$GRAV_SRC" ;;
        *) warn "left $GRAV_SRC in place (outside grav's own folders)" ;;
    esac
elif [ -n "$GRAV_SRC" ] && [ -d "$GRAV_SRC/build-install" ]; then
    rm -rf "$GRAV_SRC/build-install" && ok "$GRAV_SRC/build-install (the install build)"
fi

if [ -d "$PREFIX/share/grav" ]; then $SUDO rm -rf "$PREFIX/share/grav"; ok "$PREFIX/share/grav"; fi
rmdir "$HOME/.local/share/grav" 2>/dev/null || true

for rc in "$GRAV_RC_FILE" "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
    [ -n "$rc" ] && [ -f "$rc" ] && grep -q '# >>> grav >>>' "$rc" || continue
    # Drop the marked block and the blank line before it.
    awk '/^# >>> grav >>>$/ {skip=1; if (prev == "") drop=1; next}
         /^# <<< grav <<<$/ {skip=0; next}
         !skip { if (NR > 1 && !drop) print prev; drop=0; prev=$0; have=1 }
         END { if (have && !drop) print prev }' "$rc" > "$rc.grav-tmp" && mv "$rc.grav-tmp" "$rc"
    ok "removed grav from $rc"
done

CACHE="${GRAV_PACKAGES:-$HOME/.local/grav/packages}"
if [ -d "$CACHE" ]; then
    if [ "$PURGE" = 1 ] || { [ "$YES" = 0 ] && confirm "Also delete the package cache at $CACHE?" n; }; then
        rm -rf "$CACHE"; rmdir "$(dirname "$CACHE")" 2>/dev/null || true
        ok "$CACHE"
    else
        echo "  kept the package cache at $CACHE"
    fi
fi

echo
echo "${G}${B}grav is uninstalled.${N} Open a new terminal to drop it from PATH."
