#!/usr/bin/env python3
"""Switches the Grav code fences in READMEs between ```grav and a stand-in.

GitHub colours a fenced block only for languages it knows (github/linguist).
Until it knows Grav, README blocks are fenced as TypeScript, the closest
match it highlights: keywords, types, function names and strings coloured,
nothing marked as an error, and no language label shown on the block.

    tools/readme-fences.py typescript [dir ...]   # ```grav -> ```typescript
    tools/readme-fences.py grav [dir ...]         # back, once GitHub knows Grav

Only README.md files are touched (docs/ keeps ```grav: tests/docs.sh runs
those blocks). With no dirs: this repo and every apogee-* and grav-* folder
beside it. Prints each file changed.
"""
import glob, os, re, sys

STAND_IN = "typescript"

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in (STAND_IN, "grav"):
        sys.exit(__doc__)
    to = sys.argv[1]
    frm = "grav" if to == STAND_IN else STAND_IN
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    dirs = sys.argv[2:] or [root] + sorted(glob.glob(os.path.join(root, "apogee-*")) + glob.glob(os.path.join(root, "grav-*")))
    for d in dirs:
        p = os.path.join(d, "README.md")
        if not os.path.isfile(p):
            continue
        text = open(p, encoding="utf-8").read()
        new = re.sub(r"^(\s*)```" + frm + r"[ \t]*$", r"\1```" + to, text, flags=re.M)
        if new != text:
            open(p, "w", encoding="utf-8").write(new)
            print(os.path.relpath(p, root))

if __name__ == "__main__":
    main()
