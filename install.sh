#!/usr/bin/env bash
# Installs the Grav compiler (grav) and its standard library on Linux.
#
#   ./install.sh                 from a checkout: builds that checkout
#   curl -fsSL https://raw.githubusercontent.com/venomous-maker/Grav/main/install.sh | bash
#                                anywhere else: clones the source first
#
# It checks the system, offers to install what is missing (apt), asks where to
# put things (or takes the defaults with --yes), builds, installs, tries a
# program, and records the install so `grav self-update` and uninstall.sh can
# find it again. Run with --help for the options.
set -euo pipefail

REPO_DEFAULT="https://github.com/venomous-maker/Grav.git"
MIN_CMAKE="3.20"

# ── options ────────────────────────────────────────────────────────────────────
PREFIX="" SRC="" REPO="" REF="" PACKAGES="" JOBS="" OPTIONAL="" EDIT_PATH=""
YES=0 CHECK_ONLY=0 DEPS_ONLY=0 UPDATE=0 REBUILD=0 FORCE=0

usage() {
    cat <<'EOF'
usage: install.sh [options]

  --prefix DIR      install grav under DIR/bin and DIR/share/grav
                    (default: ~/.local, or /usr/local when run as root)
  --src DIR         the source checkout to build (default: the checkout this
                    script is in, else PREFIX/share/grav/src, cloned)
  --repo URL        where to clone from (default: the GitHub repository)
  --ref REF         what to build and follow: "latest" (the newest release
                    tag, v*), a tag, or a branch (default: latest when
                    cloning; the checked-out branch for your own checkout)
  --packages DIR    the package cache, GRAV_PACKAGES (default: ~/.local/grav/packages)
  --jobs N          parallel build jobs (default: CPU cores)
  --optional        also install the optional libraries (TLS, zlib, SQLite,
                    PostgreSQL, MySQL/MariaDB clients)
  --no-optional     do not ask about the optional libraries
  --no-path         do not add grav to PATH in your shell's startup file
  -y, --yes         take every default, ask nothing
  --check           only check the system; change nothing
  --deps-only       only install the packages needed to use grav (for images)
  --update          pull the recorded install's source and rebuild it if it
                    changed (what `grav self-update` runs)
  --force           with --update: rebuild even when nothing changed
  -h, --help        this help
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift ;;
        --src) SRC="$2"; shift ;;
        --repo) REPO="$2"; shift ;;
        --ref) REF="$2"; shift ;;
        --packages) PACKAGES="$2"; shift ;;
        --jobs) JOBS="$2"; shift ;;
        --optional) OPTIONAL=1 ;;
        --no-optional) OPTIONAL=0 ;;
        --no-path) EDIT_PATH=0 ;;
        -y|--yes) YES=1 ;;
        --check) CHECK_ONLY=1 ;;
        --deps-only) DEPS_ONLY=1 ;;
        --update) UPDATE=1 ;;
        --rebuild) REBUILD=1 ;;          # (internal: --update after the pull)
        --force) FORCE=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install.sh: unknown option '$1' (see --help)" >&2; exit 2 ;;
    esac
    shift
done

# ── output ─────────────────────────────────────────────────────────────────────
if [ -t 1 ]; then B=$'\e[1m' G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' D=$'\e[2m' N=$'\e[0m'; else B="" G="" Y="" R="" D="" N=""; fi
step() { printf '\n%s==>%s %s%s%s\n' "$B" "$N" "$B" "$*" "$N"; }
ok()   { printf '  %s✓%s %s\n' "$G" "$N" "$*"; }
warn() { printf '  %s!%s %s\n' "$Y" "$N" "$*"; }
bad()  { printf '  %s✗%s %s\n' "$R" "$N" "$*"; }
die()  { printf '\n%serror:%s %s\n' "$R" "$N" "$*" >&2; exit 1; }

# Questions read the terminal, so `curl ... | bash` can still ask; with no
# terminal (or --yes) every question takes its default.
TTY=""
if [ "$YES" = 0 ] && { : </dev/tty; } 2>/dev/null; then TTY=/dev/tty; fi
ask() {   # ask "question" default -> REPLY
    local q="$1" def="$2"
    if [ -z "$TTY" ]; then REPLY="$def"; return; fi
    printf '  %s %s[%s]%s ' "$q" "$D" "$def" "$N" >/dev/tty
    IFS= read -r REPLY </dev/tty || REPLY=""
    [ -n "$REPLY" ] || REPLY="$def"
}
confirm() {   # confirm "question" y|n -> status
    ask "$1 (y/n)" "$2"
    case "$REPLY" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

SUDO=""
as_root() {   # runs a command as root (sudo when needed)
    if [ "$(id -u)" = 0 ]; then "$@"
    elif command -v sudo >/dev/null 2>&1; then sudo "$@"
    else die "this step needs root and sudo is not installed: $*"; fi
}

version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }

here="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || true)"
in_checkout=0
if [ -n "$here" ] && [ -f "$here/CMakeLists.txt" ] && grep -q "add_executable(gravc" "$here/CMakeLists.txt" 2>/dev/null; then in_checkout=1; fi

# ── the install record ─────────────────────────────────────────────────────────
# PREFIX/share/grav/install.env: shell assignments, read by --update,
# uninstall.sh and `grav self-update`.
load_record() {
    local f="$1/share/grav/install.env"
    [ -f "$f" ] || return 1
    # shellcheck disable=SC1090
    . "$f"
}

if [ "$UPDATE" = 1 ]; then
    if [ -z "$PREFIX" ]; then
        g="$(command -v grav || true)"
        [ -n "$g" ] || die "grav is not on PATH; pass --prefix"
        PREFIX="$(cd "$(dirname "$(readlink -f "$g")")/.." && pwd)"
    fi
    load_record "$PREFIX" || die "no install record at $PREFIX/share/grav/install.env: this grav was not installed by install.sh"
    SRC="${SRC:-$GRAV_SRC}" REPO="${REPO:-$GRAV_REPO}" REF="${REF:-$GRAV_REF}"
    PACKAGES="${PACKAGES:-$GRAV_PACKAGES}" EDIT_PATH=0 OPTIONAL=0 YES=1 TTY=""
fi

# ── the system ─────────────────────────────────────────────────────────────────
step "Checking the system"
OS="$(uname -s)" ARCH="$(uname -m)"
case "$OS" in
    Linux) ok "Linux ($ARCH)" ;;
    Darwin) die "macOS is not supported yet (Linux only for now). The Docker image works there: see docs/tools/install.md." ;;
    MINGW*|MSYS*|CYGWIN*) die "Windows is not supported natively yet. Use WSL (wsl --install, then run this script inside Ubuntu) or the Docker image." ;;
    *) die "unsupported system: $OS (Linux only for now)" ;;
esac
case "$ARCH" in
    x86_64|aarch64|arm64) ;;
    *) warn "$ARCH is untested; the build may still work" ;;
esac
if grep -qi microsoft /proc/version 2>/dev/null; then ok "running under WSL"; fi

DISTRO="unknown" DISTRO_VER="" FAMILY=""
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    DISTRO="${ID:-unknown}" DISTRO_VER="${VERSION_ID:-}"
    case " ${ID:-} ${ID_LIKE:-} " in
        *" debian "*|*" ubuntu "*) FAMILY=debian ;;
    esac
fi
if [ "$FAMILY" = debian ]; then
    ok "${PRETTY_NAME:-$DISTRO $DISTRO_VER} (apt)"
    case "$DISTRO" in
        debian) [ -z "$DISTRO_VER" ] || version_ge "$DISTRO_VER" 12 || warn "Debian $DISTRO_VER is older than 12: its CMake is too old (need $MIN_CMAKE)" ;;
        ubuntu) version_ge "$DISTRO_VER" 22.04 || warn "Ubuntu $DISTRO_VER is older than 22.04: its CMake is too old (need $MIN_CMAKE)" ;;
    esac
else
    warn "${PRETTY_NAME:-$DISTRO} is not Debian or Ubuntu: missing tools are listed, not installed"
fi

mem_kb="$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
if [ "${mem_kb:-0}" -gt 0 ] && [ "$mem_kb" -lt 1000000 ]; then
    warn "$((mem_kb / 1024)) MB of memory: building the compiler wants about 1 GB (try --jobs 1)"
fi

# ── requirements ───────────────────────────────────────────────────────────────
# Each check sets MISSING (apt packages) for what is required and OPT_MISSING
# for what is optional.
MISSING=() OPT_MISSING=() HARD_FAIL=0
need_pkg() { MISSING+=("$@"); }

# The first of several candidate packages apt knows (names differ between
# releases, e.g. libssl3 / libssl3t64).
apt_pick() {
    local p
    for p in "$@"; do
        if apt-cache show "$p" >/dev/null 2>&1; then echo "$p"; return; fi
    done
}

APT_UPDATED=0
apt_update() {
    [ "$APT_UPDATED" = 1 ] && return
    as_root apt-get update -qq
    APT_UPDATED=1
}

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check_build_tools() {
    if command -v git >/dev/null 2>&1; then ok "git $(git --version | awk '{print $3}')"
    elif [ "$UPDATE" = 0 ] && { [ -f "${SRC:-/nonexistent}/CMakeLists.txt" ] || { [ -z "$SRC" ] && [ "$in_checkout" = 1 ]; }; }; then
        warn "git missing (only needed to clone or update)"
    else bad "git missing"; need_pkg git ca-certificates; fi

    if command -v cmake >/dev/null 2>&1; then
        local v; v="$(cmake --version | awk 'NR==1 {print $3}')"
        if version_ge "$v" "$MIN_CMAKE"; then ok "cmake $v"
        else bad "cmake $v is older than $MIN_CMAKE"; need_pkg cmake; HARD_FAIL=1; fi
    else bad "cmake missing"; need_pkg cmake; fi

    if command -v make >/dev/null 2>&1 || command -v ninja >/dev/null 2>&1; then ok "make/ninja"
    else bad "make missing"; need_pkg make; fi

    # A C++20 compiler: try one.
    local cxx="${CXX:-c++}"
    printf '#include <filesystem>\n#include <optional>\nint main() { std::optional<int> o; auto f = [](auto x) { return x; }; return f(o.value_or(0)); }\n' > "$tmp/t.cpp"
    if command -v "$cxx" >/dev/null 2>&1 && "$cxx" -std=c++20 "$tmp/t.cpp" -o "$tmp/t" >/dev/null 2>&1; then
        ok "C++20 compiler ($("$cxx" --version | head -1))"
    else bad "no C++20 compiler (g++ 10 or newer)"; need_pkg g++; fi
}

check_use_tools() {
    # grav turns programs into C and builds them with cc: C11, pthreads, libm.
    printf '#include <pthread.h>\n#include <math.h>\n#include <stdatomic.h>\nstatic void *f(void *a) { return a; }\nint main(void) { pthread_t t; pthread_create(&t, 0, f, 0); pthread_join(t, 0); return (int)sqrt(0.0); }\n' > "$tmp/t.c"
    if command -v cc >/dev/null 2>&1 && cc -std=c11 -pthread "$tmp/t.c" -o "$tmp/tc" -lm >/dev/null 2>&1; then
        ok "C compiler for programs ($(cc --version | head -1))"
    else bad "no working C compiler (cc) with C headers"; need_pkg gcc libc6-dev; fi
}

# Optional: loaded at run time when a program uses them.
OPT_LIBS=(
    "libssl.so.3 libssl.so.1.1 libssl.so|TLS (https, wss)|libssl3t64 libssl3 libssl1.1"
    "libz.so.1 libz.so|gzip and deflate|zlib1g"
    "libsqlite3.so.0 libsqlite3.so|SQLite (grav-db)|libsqlite3-0"
    "libpq.so.5 libpq.so|PostgreSQL (grav-db)|libpq5"
    "libmariadb.so.3 libmysqlclient.so.21 libmysqlclient.so|MySQL/MariaDB (grav-db)|libmariadb3"
)
has_lib() {
    local n
    for n in "$@"; do
        if ldconfig -p 2>/dev/null | grep -q " $n "; then return 0; fi
        for d in /lib /usr/lib /lib64 /usr/lib64 /usr/local/lib /lib/*-linux-gnu /usr/lib/*-linux-gnu; do
            [ -e "$d/$n" ] && return 0
        done
    done
    return 1
}
check_optional() {
    local entry names what pkgs
    for entry in "${OPT_LIBS[@]}"; do
        IFS='|' read -r names what pkgs <<<"$entry"
        # shellcheck disable=SC2086
        if has_lib $names; then ok "$what"
        else
            warn "$what: not installed (optional)"
            OPT_MISSING+=("$pkgs")
        fi
    done
}

[ "$DEPS_ONLY" = 1 ] || check_build_tools
check_use_tools
check_optional

if [ "$CHECK_ONLY" = 1 ] && [ "$UPDATE" = 0 ]; then
    echo
    if [ ${#MISSING[@]} -eq 0 ]; then echo "${G}Ready to install.${N}"; exit 0; fi
    echo "Missing: ${MISSING[*]}"
    [ "$FAMILY" = debian ] && echo "Install them with: sudo apt-get install ${MISSING[*]}"
    exit 1
fi

# ── install what is missing ────────────────────────────────────────────────────
if [ ${#MISSING[@]} -gt 0 ]; then
    if [ "$FAMILY" != debian ]; then
        die "install these with your package manager, then run this again: ${MISSING[*]}"
    fi
    step "Installing required packages"
    echo "  ${MISSING[*]}"
    confirm "Install them with apt?" y || die "required packages are missing: ${MISSING[*]}"
    apt_update
    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${MISSING[@]}"
    if [ "$HARD_FAIL" = 1 ]; then
        v="$(cmake --version | awk 'NR==1 {print $3}')"
        version_ge "$v" "$MIN_CMAKE" || die "this release's cmake is $v, older than $MIN_CMAKE. Install a newer one (pip install cmake, or apt.kitware.com) and run this again."
    fi
fi

if [ ${#OPT_MISSING[@]} -gt 0 ] && [ "$FAMILY" = debian ] && [ "$OPTIONAL" != 0 ]; then
    if [ "$OPTIONAL" = 1 ] || { [ -n "$TTY" ] && confirm "Install the missing optional libraries?" n; }; then
        step "Installing optional libraries"
        apt_update
        picked=()
        for group in "${OPT_MISSING[@]}"; do
            # shellcheck disable=SC2086
            p="$(apt_pick $group)"
            if [ -n "$p" ]; then picked+=("$p"); else warn "none of these is available: $group"; fi
        done
        if [ ${#picked[@]} -gt 0 ]; then
            as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${picked[@]}"
            ok "${picked[*]}"
        fi
    fi
fi

if [ "$DEPS_ONLY" = 1 ]; then echo; echo "${G}Everything needed to use grav is installed.${N}"; exit 0; fi

# ── settings ───────────────────────────────────────────────────────────────────

step "Settings"
if [ "$UPDATE" = 0 ]; then
    if [ -z "$PREFIX" ]; then
        if [ "$(id -u)" = 0 ]; then PREFIX=/usr/local; else PREFIX="$HOME/.local"; fi
        ask "Install prefix (grav goes in PREFIX/bin)" "$PREFIX"; PREFIX="$REPLY"
    fi
    PREFIX="${PREFIX/#\~/$HOME}"
    if [ -z "$SRC" ]; then
        if [ "$in_checkout" = 1 ]; then SRC="$here"
        elif mkdir -p "$PREFIX" 2>/dev/null && [ -w "$PREFIX" ]; then SRC="$PREFIX/share/grav/src"
        else SRC="$HOME/.local/share/grav/src"; fi
        ask "Source checkout to build" "$SRC"; SRC="$REPLY"
    fi
    SRC="${SRC/#\~/$HOME}"
    if [ -z "$PACKAGES" ]; then
        PACKAGES="${GRAV_PACKAGES:-$HOME/.local/grav/packages}"
        ask "Package cache (GRAV_PACKAGES)" "$PACKAGES"; PACKAGES="$REPLY"
    fi
    PACKAGES="${PACKAGES/#\~/$HOME}"
    REPO="${REPO:-$REPO_DEFAULT}"
    if [ -z "$REF" ]; then
        # Your own checkout keeps following its branch; a clone follows releases.
        REF=latest
        if [ -d "$SRC/.git" ]; then
            b="$(git -C "$SRC" symbolic-ref -q --short HEAD 2>/dev/null || true)"
            [ -z "$b" ] || REF="$b"
        fi
    fi
    if [ -z "$EDIT_PATH" ]; then
        EDIT_PATH=1
        [ -z "$TTY" ] || confirm "Add grav to PATH in your shell's startup file?" y || EDIT_PATH=0
    fi
fi
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
ok "prefix   $PREFIX"
ok "source   $SRC"
ok "packages $PACKAGES"

# Root is needed to write the prefix only when it is not ours.
mkdir -p "$PREFIX" 2>/dev/null || true
if [ -w "$PREFIX" ]; then SUDO=""; else SUDO=as_root; fi

# ── source ─────────────────────────────────────────────────────────────────────
# What an update moves to: sets new (a commit), TARGET_NAME and TARGET_IS_TAG.
# "latest" is the newest v* tag (main while there are none); a tag is that
# tag; anything else is a branch.
resolve_target() {
    TARGET_IS_TAG=0
    git -C "$SRC" fetch -q --tags --force origin
    if [ "$REF" = latest ]; then
        TARGET_NAME="$(git -C "$SRC" tag -l 'v*' --sort=-v:refname | head -1)"
        if [ -n "$TARGET_NAME" ]; then TARGET_IS_TAG=1; new="$(git -C "$SRC" rev-parse "$TARGET_NAME^{commit}")"; return; fi
        REF_BRANCH=main
    elif git -C "$SRC" rev-parse -q --verify "refs/tags/$REF" >/dev/null; then
        TARGET_IS_TAG=1 TARGET_NAME="$REF"
        new="$(git -C "$SRC" rev-parse "$REF^{commit}")"
        return
    else
        REF_BRANCH="$REF"
    fi
    git -C "$SRC" fetch -q origin "$REF_BRANCH" || die "no branch or tag '$REF_BRANCH' in $(git -C "$SRC" remote get-url origin)"
    new="$(git -C "$SRC" rev-parse FETCH_HEAD)"
    TARGET_NAME="$REF_BRANCH"
}
CLONED="${GRAV_CLONED:-0}"
if [ "$UPDATE" = 1 ] && [ "$REBUILD" = 0 ]; then
    step "Checking for updates"
    [ -d "$SRC/.git" ] || die "$SRC is not a git checkout; cannot update it"
    resolve_target
    old="$(git -C "$SRC" rev-parse HEAD)"
    if [ "$old" = "$new" ] && [ "$FORCE" = 0 ]; then
        ok "grav is up to date ($TARGET_NAME, ${old:0:7})"
        exit 0
    fi
    if [ "$CHECK_ONLY" = 1 ]; then
        echo "  an update is available: $TARGET_NAME (${old:0:7} -> ${new:0:7}, $(git -C "$SRC" rev-list --count "$old..$new") commits)"
        git -C "$SRC" log --oneline "$old..$new" | head -15 | sed 's/^/    /'
        echo "  run: grav self-update"
        exit 0
    fi
    if [ "$old" != "$new" ]; then
        if [ -n "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]; then
            die "$SRC has uncommitted changes; commit or stash them, then run grav self-update again"
        fi
        echo "  $TARGET_NAME: ${old:0:7} -> ${new:0:7}"
        git -C "$SRC" log --oneline "$old..$new" | head -15 | sed 's/^/    /'
        if [ "$TARGET_IS_TAG" = 1 ]; then
            git -C "$SRC" checkout -q --detach "$new"
        else
            git -C "$SRC" checkout -q "$REF" 2>/dev/null || git -C "$SRC" checkout -q -b "$REF" "$new"
            git -C "$SRC" merge -q --ff-only "$new" || die "$SRC has commits of its own on $REF; update it by hand"
        fi
    fi
    # The new script may know more than this one.
    args=(--update --rebuild --prefix "$PREFIX" --ref "$REF")
    [ "$FORCE" = 0 ] || args+=(--force)
    [ -z "$JOBS" ] || args+=(--jobs "$JOBS")
    exec bash "$SRC/install.sh" "${args[@]}"
elif [ "$UPDATE" = 0 ]; then
    if [ -f "$SRC/CMakeLists.txt" ]; then
        ok "using the checkout at $SRC"
    else
        what="$REF"
        if [ "$REF" = latest ]; then
            what="$(git ls-remote --tags --refs "$REPO" 'v*' 2>/dev/null | awk -F/ '{print $3}' | sort -V | tail -1)"
            if [ -z "$what" ]; then what=main; warn "no release tags yet: building main"; fi
        fi
        step "Cloning $REPO ($what)"
        mkdir -p "$(dirname "$SRC")"
        git -c advice.detachedHead=false clone -q --branch "$what" "$REPO" "$SRC" || true
        [ -f "$SRC/CMakeLists.txt" ] || die "cloning $REPO ($what) failed"
        CLONED=1
        ok "$(git -C "$SRC" log --oneline -1)"
    fi
fi

# ── build and install ──────────────────────────────────────────────────────────
step "Building grav ($JOBS jobs)"
BUILD="$SRC/build-install"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$tmp/cmake.log" 2>&1 || { cat "$tmp/cmake.log"; die "cmake configure failed"; }
cmake --build "$BUILD" -j "$JOBS" >"$tmp/build.log" 2>&1 || { tail -40 "$tmp/build.log"; die "the build failed (full log above)"; }
ok "built"

OLD_VERSION="$("$PREFIX/bin/grav" env 2>/dev/null | awk '/^GRAV_VERSION/ {print $2}' || true)"
step "Installing into $PREFIX"
# Install into a staging folder, then rename each piece into place: a grav
# that is running meanwhile keeps its old file, and std files that were
# deleted upstream do not linger.
stage="$tmp/stage"
cmake --install "$BUILD" --prefix "$stage" >"$tmp/install.log" 2>&1 || { cat "$tmp/install.log"; die "install failed"; }
$SUDO mkdir -p "$PREFIX/bin" "$PREFIX/share/grav"
$SUDO cp "$stage/bin/grav" "$PREFIX/bin/.grav.new"
$SUDO mv -f "$PREFIX/bin/.grav.new" "$PREFIX/bin/grav"
ok "$PREFIX/bin/grav"
$SUDO rm -rf "$PREFIX/share/grav/lib.new" "$PREFIX/share/grav/lib.old"
$SUDO cp -R "$stage/share/grav/lib" "$PREFIX/share/grav/lib.new"
if [ -d "$PREFIX/share/grav/lib" ]; then $SUDO mv "$PREFIX/share/grav/lib" "$PREFIX/share/grav/lib.old"; fi
$SUDO mv "$PREFIX/share/grav/lib.new" "$PREFIX/share/grav/lib"
$SUDO rm -rf "$PREFIX/share/grav/lib.old"
ok "$PREFIX/share/grav/lib"

COMMIT="$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)"

# Environment: one file the shell startup sources.
ENV_FILE="$PREFIX/share/grav/env.sh"
$SUDO tee "$ENV_FILE" >/dev/null <<EOF
# Written by Grav's install.sh; removed by uninstall.sh.
case ":\$PATH:" in *":$PREFIX/bin:"*) ;; *) export PATH="$PREFIX/bin:\$PATH" ;; esac
export GRAV_PACKAGES="\${GRAV_PACKAGES:-$PACKAGES}"
EOF

RC_FILE="${GRAV_RC_FILE:-}"
if [ "$EDIT_PATH" = 1 ]; then
    case "$(basename "${SHELL:-bash}")" in
        zsh) RC_FILE="$HOME/.zshrc" ;;
        bash) RC_FILE="$HOME/.bashrc" ;;
        *) RC_FILE="$HOME/.profile" ;;
    esac
    if ! grep -q '# >>> grav >>>' "$RC_FILE" 2>/dev/null; then
        printf '\n# >>> grav >>>\n[ -f "%s" ] && . "%s"\n# <<< grav <<<\n' "$ENV_FILE" "$ENV_FILE" >> "$RC_FILE"
        ok "added grav to $RC_FILE"
    else
        ok "$RC_FILE already loads grav"
    fi
fi

$SUDO tee "$PREFIX/share/grav/install.env" >/dev/null <<EOF
# Written by Grav's install.sh: read by grav self-update and uninstall.sh.
GRAV_PREFIX='$PREFIX'
GRAV_SRC='$SRC'
GRAV_CLONED='$CLONED'
GRAV_REPO='$REPO'
GRAV_REF='$REF'
GRAV_COMMIT='$COMMIT'
GRAV_PACKAGES='$PACKAGES'
GRAV_RC_FILE='$RC_FILE'
GRAV_INSTALLED_AT='$(date -u +%Y-%m-%dT%H:%M:%SZ)'
EOF

# ── try it ─────────────────────────────────────────────────────────────────────
step "Trying it"
printf 'import "std"\nfn main() {\n    let m = new HashMap<string, int>()\n    m.put("grav", 1)\n    for ((k, v) in m) { println("${k} ${v} works") }\n}\n' > "$tmp/hello.grav"
if out="$(cd "$tmp" && GRAV_PACKAGES="$PACKAGES" "$PREFIX/bin/grav" run hello.grav 2>&1)" && grep -qx "grav 1 works" <<<"$out"; then
    ok "a test program builds and runs"
else
    echo "$out" | sed 's/^/    /'
    die "grav is installed but could not build a test program"
fi
std="$("$PREFIX/bin/grav" env | awk '/^GRAV_STD/ {print $2}')"
[ "$std" = "$PREFIX/share/grav/lib" ] || warn "grav finds its standard library at $std, not $PREFIX/share/grav/lib"

echo
if [ "$UPDATE" = 1 ]; then
    NEW_VERSION="$("$PREFIX/bin/grav" env | awk '/^GRAV_VERSION/ {print $2}')"
    echo "${G}${B}grav is updated${N}: ${OLD_VERSION:-?} -> $NEW_VERSION (${GRAV_COMMIT:0:7} -> ${COMMIT:0:7})."
else
    echo "${G}${B}grav $("$PREFIX/bin/grav" env | awk '/^GRAV_VERSION/ {print $2}') is installed${N} (${COMMIT:0:7}, following $REF)."
    case ":$PATH:" in
        *":$PREFIX/bin:"*) echo "Try: grav run hello.grav" ;;
        *) if [ "$EDIT_PATH" = 1 ]; then echo "Open a new terminal (or run: . \"$ENV_FILE\"), then: grav --help"
           else echo "Add $PREFIX/bin to PATH (or run: . \"$ENV_FILE\"), then: grav --help"; fi ;;
    esac
    echo "Update later with: grav self-update"
fi
