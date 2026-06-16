#!/usr/bin/env bash
# Compiles and runs every example, checking that each builds and exits cleanly.
# A few examples are driven with canned stdin / arguments. Usage: tests/run.sh
set -u
cd "$(dirname "$0")/.."
GRAV="${GRAVC:-./build/gravc}"
[ -x "$GRAV" ] || GRAV="$HOME/.local/bin/grav"

pass=0; fail=0
run() { # name, stdin, args...
  local f="examples/$1"; shift
  local stdin="$1"; shift
  local bin="/tmp/gravtest_$(basename "$f" .grav)"
  if ! "$GRAV" "$f" --emit bin -o "$bin" >/tmp/gravtest.err 2>&1; then
    echo "FAIL (compile) $f"; sed 's/^/    /' /tmp/gravtest.err; fail=$((fail+1)); return
  fi
  if printf '%b' "$stdin" | "$bin" "$@" >/tmp/gravtest.out 2>&1; then
    echo "ok   $(basename "$f")"; pass=$((pass+1))
  else
    echo "FAIL (run) $f (exit $?)"; sed 's/^/    /' /tmp/gravtest.out; fail=$((fail+1))
  fi
}

for f in examples/*.grav; do
  base="$(basename "$f")"
  case "$base" in
    16_input.grav) run "$base" 'Ada\n7\n' demo ;;   # feed stdin + an arg
    05_strings.grav) run "$base" '' alpha beta ;;    # exercise argv
    *)               run "$base" '' ;;
  esac
done

# Golden-output check for the stdin example (verifies input() reads correctly).
bin="/tmp/gravtest_16_input"
if [ -x "$bin" ]; then
  printf '%b' 'Ada\n7\n' | "$bin" demo >/tmp/gravtest.out 2>&1
  if diff -q tests/16_input.expected /tmp/gravtest.out >/dev/null 2>&1; then
    echo "ok   16_input.grav (stdin golden)"; pass=$((pass+1))
  else
    echo "FAIL 16_input.grav golden mismatch:"
    diff tests/16_input.expected /tmp/gravtest.out | sed 's/^/    /'; fail=$((fail+1))
  fi
fi

echo "------------------------------------"
echo "passed: $pass   failed: $fail"
exit $((fail > 0))
