#!/usr/bin/env bash
# Builds and runs every example, comparing its output with the golden file in
# tests/expected/<name>.out (stdout + stderr). Examples without a golden file
# only need to build and exit cleanly.
#
#   tests/run.sh            run everything
#   tests/run.sh --update   rewrite the golden files from the current output
set -u
cd "$(dirname "$0")/.."
GRAV="${GRAVC:-./build/gravc}"
[ -x "$GRAV" ] || GRAV="$HOME/.local/bin/grav"
UPDATE=0; [ "${1:-}" = "--update" ] && UPDATE=1
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p tests/expected

pass=0; fail=0
run() { # file, stdin, args...
  local f="$1"; shift
  local stdin="$1"; shift
  local name; name="$(echo "${f#examples/}" | sed 's|^tests/||; s|/|_|g; s|\.grav$||')"
  local bin="$TMP/$name" out="$TMP/$name.out" gold="tests/expected/$name.out"
  if ! "$GRAV" "$f" --emit bin -o "$bin" >"$TMP/err" 2>&1; then
    echo "FAIL (compile) $f"; sed 's/^/    /' "$TMP/err"; fail=$((fail+1)); return
  fi
  if ! printf '%b' "$stdin" | "$bin" "$@" >"$out" 2>&1; then
    echo "FAIL (exit)    $f"; sed 's/^/    /' "$out" | tail -20; fail=$((fail+1)); return
  fi
  if [ $UPDATE = 1 ]; then cp "$out" "$gold"; echo "updated $gold"; pass=$((pass+1)); return; fi
  if [ -f "$gold" ] && ! diff -q "$gold" "$out" >/dev/null; then
    echo "FAIL (output)  $f"; diff "$gold" "$out" | sed 's/^/    /' | head -30; fail=$((fail+1)); return
  fi
  echo "ok   $f"; pass=$((pass+1))
}

for f in examples/*.grav examples/apps/*.grav; do
  case "$(basename "$f")" in
    14_input_args.grav) run "$f" 'Ada\n7\n3.5\nyes\nextra1\nextra2\n' alpha beta ;;
    *)                  run "$f" '' ;;
  esac
done
run examples/project/main.grav ''
# Library tests: every standard-library module, golden-checked like the examples.
for f in tests/lib/*.grav; do run "$f" ''; done

# `grav test` files: the runner's report is golden-checked (it may exit 1 on
# purpose, since some of these tests are meant to fail).
for f in tests/unit/*.grav; do
  name="unit_$(basename "$f" .grav)"; gold="tests/expected/$name.out"
  "$GRAV" test "$f" >"$TMP/$name.out" 2>&1
  if [ $UPDATE = 1 ]; then cp "$TMP/$name.out" "$gold"; echo "updated $gold"; pass=$((pass+1)); continue; fi
  if [ ! -f "$gold" ] || ! diff -q "$gold" "$TMP/$name.out" >/dev/null; then
    echo "FAIL (test)    $f"; diff "$gold" "$TMP/$name.out" 2>&1 | sed 's/^/    /' | head -30; fail=$((fail+1))
  else echo "ok   $f (grav test)"; pass=$((pass+1)); fi
done

# Every example must also build in release mode (no tracing, -O3).
for f in examples/*.grav examples/apps/*.grav examples/project/main.grav; do
  if ! "$GRAV" "$f" --release --emit bin -o "$TMP/rel" >"$TMP/err" 2>&1; then
    echo "FAIL (release) $f"; sed 's/^/    /' "$TMP/err"; fail=$((fail+1))
  fi
done

echo "------------------------------------"
echo "passed: $pass   failed: $fail"

# The documentation's snippets must compile and print what the docs claim.
if [ $UPDATE = 0 ] && [ -x tests/docs.sh ]; then
  if bash tests/docs.sh >"$TMP/docs.out" 2>&1; then tail -1 "$TMP/docs.out"
  else cat "$TMP/docs.out"; fail=$((fail+1)); fi
fi
exit $((fail > 0))
