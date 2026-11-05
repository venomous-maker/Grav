#!/usr/bin/env bash
# Compiles and runs every complete Grav snippet in docs/**/*.md (a ```grav block that
# contains `fn main` and does not start with a `// file:` header). When the block is followed by an "Output:" line and a
# ```text block, the program's stdout must match it exactly.
#
#   tests/docs.sh           check all chapters
#   tests/docs.sh 02        only files whose name contains "02"
set -u
cd "$(dirname "$0")/.."
GRAV="${GRAVC:-./build/gravc}"
[ -x "$GRAV" ] || GRAV="$HOME/.local/bin/grav"
exec python3 -I - "$GRAV" "${1:-}" <<'PY'
import os, re, subprocess, sys, tempfile, glob
grav, only = sys.argv[1], sys.argv[2]
passed = failed = 0
tmp = tempfile.mkdtemp()
for md in sorted(glob.glob("docs/**/*.md", recursive=True)):
    if only and only not in md:
        continue
    text = open(md).read()
    for m in re.finditer(r"```grav\n(.*?)```", text, re.S):
        code = m.group(1)
        # Multi-file illustrations start with `// file: name` and are not run alone.
        if "fn main" not in code or code.lstrip().startswith("// file:"):
            continue
        line = text[:m.start()].count("\n") + 1
        rest = text[m.end():]
        exp = re.match(r"\s*\n(?:\s*\n)*Output:\s*\n\s*\n?```text\n(.*?)```", rest, re.S)
        # Snippets live next to their page so relative imports resolve from there.
        src = os.path.join(os.path.dirname(md), f".snippet_{os.getpid()}.grav")
        open(src, "w").write(code)
        exe = os.path.join(tmp, "snip")
        where = f"{md}:{line}"
        try:
            c = subprocess.run([grav, src, "--emit", "bin", "-o", exe], capture_output=True, text=True)
            if c.returncode != 0:
                print(f"FAIL (compile) {where}\n" + "".join("    " + l + "\n" for l in c.stderr.splitlines()[:12]))
                failed += 1
                continue
            r = subprocess.run([exe], capture_output=True, text=True, input="", timeout=20)
            if r.returncode != 0:
                print(f"FAIL (exit {r.returncode}) {where}\n    {r.stdout[-300:]}{r.stderr[-300:]}")
                failed += 1
                continue
            if exp and r.stdout != exp.group(1):
                print(f"FAIL (output) {where}\n--- expected\n{exp.group(1)}--- got\n{r.stdout}")
                failed += 1
                continue
            passed += 1
        finally:
            if os.path.exists(src):
                os.remove(src)
print("------------------------------------")
print(f"doc snippets passed: {passed}   failed: {failed}")
sys.exit(1 if failed else 0)
PY
