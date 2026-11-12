#!/usr/bin/env bash
# Syntax coloring: the TextMate grammar (VS Code, JetBrains) against the fixtures
# in editors/grammar/tests/ with VS Code's own engine, every .grav file for strings
# that run past their line or regions left open, and Neovim's syntax file against
# the same fixtures. Each part is skipped when its tool is not installed.
set -u
cd "$(dirname "$0")/.."
fail() { echo "FAIL (grammar) $1"; exit 1; }
out=""
if command -v node >/dev/null; then
  node editors/grammar/test.js > /tmp/grav-grammar.$$ 2>&1; rc=$?
  if [ $rc = 2 ]; then out="$out textmate skipped;"
  else
    [ $rc = 0 ] || { cat /tmp/grav-grammar.$$; rm -f /tmp/grav-grammar.$$; fail "the TextMate grammar's fixtures"; }
    files=$(git ls-files '*.grav' 2>/dev/null || find lib examples tests -name '*.grav')
    node editors/grammar/test.js --strings $files > /tmp/grav-grammar.$$ 2>&1 ||
      { cat /tmp/grav-grammar.$$; rm -f /tmp/grav-grammar.$$; fail "a string or region runs past its end"; }
    out="$out textmate ok ($(tail -1 /tmp/grav-grammar.$$ | grep -o '^grammar: [0-9]* of [0-9]*' | sed 's/grammar: //') files);"
  fi
  rm -f /tmp/grav-grammar.$$
else out="$out textmate skipped (no node);"; fi
if command -v nvim >/dev/null; then
  res=$(timeout 120 nvim --headless --clean -u NONE -l editors/grammar/nvim-test.lua 2>&1) || { echo "$res"; fail "Neovim's syntax agrees with the grammar"; }
  out="$out neovim ok;"
else out="$out neovim skipped;"; fi
echo "grammar:${out%;}"
