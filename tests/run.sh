#!/usr/bin/env bash
# Builds and runs every example, comparing its output with the golden file in
# tests/expected/<name>.out (stdout + stderr). Examples without a golden file
# only need to build and exit cleanly.
#
#   tests/run.sh            run everything
#   tests/run.sh --update   rewrite the golden files from the current output
#   JOBS=4 tests/run.sh     at most 4 builds at a time (default: one per CPU)
#
# Builds run in parallel; each test's result is printed in the usual order once
# all are done. Test files that time themselves (Time.monotonicMs() - ...) run
# after the rest, one at a time, so other builds do not slow them down.
set -u
cd "$(dirname "$0")/.."
GRAV="${GRAVC:-./build/gravc}"
[ -x "$GRAV" ] || GRAV="$HOME/.local/bin/grav"
UPDATE=0; [ "${1:-}" = "--update" ] && UPDATE=1
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p tests/expected "$TMP/jobs"

# ── jobs ────────────────────────────────────────────────────────────────────
# Each job writes what it prints to jobs/N.out and its outcome to jobs/N.res
# ("pass", "fail" or "none"), in its own scratch directory jobs/N.d.
n=0
running=0
spawn() { # job function and its arguments
  local id; id=$(printf '%05d' "$n"); n=$((n+1))
  mkdir -p "$TMP/jobs/$id.d"
  ( J="$TMP/jobs/$id.d"; "$@" >"$TMP/jobs/$id.out" 2>&1; ) &
  running=$((running+1))
  if [ "$running" -ge "$JOBS" ]; then wait -n; running=$((running-1)); fi
}
note() { # a line printed in order among the results, counting for nothing
  local id; id=$(printf '%05d' "$n"); n=$((n+1))
  printf '%s\n' "$1" >"$TMP/jobs/$id.out"; echo none >"$TMP/jobs/$id.res"
}
settle() { wait; running=0; }
res() { echo "$1" >"${J%.d}.res"; }

run_example() { # file, stdin, args...
  local f="$1"; shift
  local stdin="$1"; shift
  local name; name="$(echo "${f#examples/}" | sed 's|^tests/||; s|/|_|g; s|\.grav$||')"
  local bin="$J/$name" out="$J/$name.out" gold="tests/expected/$name.out"
  if ! "$GRAV" "$f" --emit bin -o "$bin" >"$J/err" 2>&1; then
    echo "FAIL (compile) $f"; sed 's/^/    /' "$J/err"; res fail; return
  fi
  if ! printf '%b' "$stdin" | "$bin" "$@" >"$out" 2>&1; then
    echo "FAIL (exit)    $f"; sed 's/^/    /' "$out" | tail -20; res fail; return
  fi
  if [ $UPDATE = 1 ]; then cp "$out" "$gold"; echo "updated $gold"; res pass; return; fi
  if [ -f "$gold" ] && ! diff -q "$gold" "$out" >/dev/null; then
    echo "FAIL (output)  $f"; diff "$gold" "$out" | sed 's/^/    /' | head -30; res fail; return
  fi
  echo "ok   $f"; res pass
}
# `grav test` files: the runner's report is golden-checked (it may exit 1 on
# purpose, since some of these tests are meant to fail).
run_unit() {
  local f="$1" name; name="unit_$(basename "$f" .grav)"; local gold="tests/expected/$name.out"
  "$GRAV" test "$f" >"$J/$name.out" 2>&1
  if [ $UPDATE = 1 ]; then cp "$J/$name.out" "$gold"; echo "updated $gold"; res pass; return; fi
  if [ ! -f "$gold" ] || ! diff -q "$gold" "$J/$name.out" >/dev/null; then
    echo "FAIL (test)    $f"; diff "$gold" "$J/$name.out" 2>&1 | sed 's/^/    /' | head -30; res fail
  else echo "ok   $f (grav test)"; res pass; fi
}
# Every standard-library module type-checks on its own (as an editor checks it).
run_lib() {
  if ! "$GRAV" check "$1" >"$J/err" 2>&1; then
    echo "FAIL (lib)     $1"; grep -v '^ ' "$J/err" | head -5 | sed 's/^/    /'; res fail
  else res pass; fi
}
# Programs that must not compile: `grav check`'s diagnostics are golden-checked.
run_error() {
  local f="$1" name; name="errors_$(basename "$f" .grav)"; local gold="tests/expected/$name.out"
  "$GRAV" check "$f" >"$J/$name.out" 2>&1 && echo "    (compiled, but should not)" >>"$J/$name.out"
  if [ $UPDATE = 1 ]; then cp "$J/$name.out" "$gold"; echo "updated $gold"; res pass; return; fi
  if [ ! -f "$gold" ] || ! diff -q "$gold" "$J/$name.out" >/dev/null; then
    echo "FAIL (errors)  $f"; diff "$gold" "$J/$name.out" 2>&1 | sed 's/^/    /' | head -30; res fail
  else echo "ok   $f (errors)"; res pass; fi
}
# Project test suites run with `grav test` from the project directory.
run_project() {
  local p="$1" name; name="project_$(basename "$p")"; local gold="tests/expected/$name.out"
  local G; G="$(cd "$(dirname "$GRAV")" && pwd)/$(basename "$GRAV")"
  (cd "$p" && "$G" test tests/*.grav) >"$J/$name.out" 2>&1
  if [ $UPDATE = 1 ]; then cp "$J/$name.out" "$gold"; echo "updated $gold"; res pass; return; fi
  if [ ! -f "$gold" ] || ! diff -q "$gold" "$J/$name.out" >/dev/null; then
    echo "FAIL (test)    $p"; diff "$gold" "$J/$name.out" 2>&1 | sed 's/^/    /' | head -30; res fail
  else echo "ok   $p (grav test)"; res pass; fi
}
# Every example must also build in release mode (no tracing, -O3).
run_release() {
  if ! "$GRAV" "$1" --release --emit bin -o "$J/rel" >"$J/err" 2>&1; then
    echo "FAIL (release) $1"; sed 's/^/    /' "$J/err"; res fail
  else res none; fi
}
# The documentation's snippets must compile and print what the docs claim
# (tests/docs.sh runs its own builds in parallel).
run_docs() {
  if bash tests/docs.sh >"$J/docs.out" 2>&1; then tail -1 "$J/docs.out"; res none
  else cat "$J/docs.out"; res fail; fi
}
times_itself() { grep -q 'Time\.monotonicMs() -' "$1"; }

# ── the tests ───────────────────────────────────────────────────────────────
# The snippets of the documentation first: they run alongside everything else,
# with their own pool of builds.
docs_id=""
if [ $UPDATE = 0 ] && [ -x tests/docs.sh ]; then docs_id=$(printf '%05d' "$n"); spawn run_docs; fi
for f in examples/*.grav examples/apps/*.grav; do
  case "$(basename "$f")" in
    14_input_args.grav) spawn run_example "$f" 'Ada\n7\n3.5\nyes\nextra1\nextra2\n' alpha beta ;;
    *)                  spawn run_example "$f" '' ;;
  esac
done
spawn run_example examples/project/main.grav ''
# The bank_server project: its self-test (a scripted API session) and its unit tests.
spawn run_example examples/apps/bank_server/src/main.grav ''
# Library tests: every standard-library module, golden-checked like the examples.
for f in tests/lib/*.grav; do spawn run_example "$f" ''; done
timed=()
for f in tests/unit/*.grav; do
  if times_itself "$f"; then timed+=("$f"); note "@timed $f"; else spawn run_unit "$f"; fi
done
for f in lib/*/*.grav; do spawn run_lib "$f"; done
note "ok   lib/*/*.grav (each checks on its own)"
for f in tests/errors/*.grav; do spawn run_error "$f"; done
for p in examples/apps/bank_server; do spawn run_project "$p"; done
for f in examples/*.grav examples/apps/*.grav examples/project/main.grav examples/apps/bank_server/src/main.grav; do
  spawn run_release "$f"
done
settle
# Tests that time themselves, alone.
for f in "${timed[@]}"; do
  id=$(grep -lx "@timed $f" "$TMP"/jobs/*.out | head -1); id=$(basename "$id" .out)
  ( J="$TMP/jobs/$id.d"; mkdir -p "$J"; run_unit "$f" >"$TMP/jobs/$id.out" 2>&1 )
done

# ── the report, in order ────────────────────────────────────────────────────
pass=0; fail=0
for out in "$TMP"/jobs/*.out; do
  id=$(basename "$out" .out)
  [ "$id" = "$docs_id" ] && continue              # (printed with the other checks below)
  cat "$out"
  case "$(cat "$TMP/jobs/$id.res" 2>/dev/null || echo fail)" in
    pass) pass=$((pass+1)) ;;
    fail) fail=$((fail+1)) ;;
  esac
done

echo "------------------------------------"
echo "passed: $pass   failed: $fail"

# The package manager, against local git repositories.
if [ $UPDATE = 0 ] && [ -x tests/packages.sh ]; then
  if bash tests/packages.sh >"$TMP/pkg.out" 2>&1; then tail -1 "$TMP/pkg.out"
  else cat "$TMP/pkg.out"; fail=$((fail+1)); fi
fi

# The standard-library pages' API sections must match the library source.
if command -v node >/dev/null; then
  if node docs/std/gen-api.js --check >"$TMP/api.out" 2>&1; then echo "ok   docs/std API sections"; pass=$((pass+1))
  else echo "FAIL (docs)    docs/std API sections are stale: run node docs/std/gen-api.js"; sed 's/^/    /' "$TMP/api.out"; fail=$((fail+1)); fi
fi

if [ -n "$docs_id" ]; then
  cat "$TMP/jobs/$docs_id.out"
  [ "$(cat "$TMP/jobs/$docs_id.res" 2>/dev/null || echo fail)" = fail ] && fail=$((fail+1))
fi
exit $((fail > 0))
