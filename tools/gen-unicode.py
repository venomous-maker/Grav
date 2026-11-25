#!/usr/bin/env python3
"""Generates the Unicode normalization tables in lib/text/unicode.grav from
Python's unicodedata (run: tools/gen-unicode.py; it rewrites the block between
the UNICODE-DATA markers).

Packed as varints, then Base64, in a C string decoded on first use:
  decompositions: count, then per entry: cp delta, (len << 1 | compat), each target cp
  combining classes: count, then per entry: cp delta, class
  composites: count, then per entry: the cp (a canonical pair that recomposes)
"""
import base64, re, sys, unicodedata as u

def varint(n, out):
    while True:
        b = n & 0x7f; n >>= 7
        if n: out.append(b | 0x80)
        else: out.append(b); return

SBase, SCount = 0xAC00, 11172
dec, comb, comp = [], [], []
for cp in range(0x110000):
    if SBase <= cp < SBase + SCount: continue          # (Hangul: computed)
    ch = chr(cp)
    d = u.decomposition(ch)
    if d:
        compat = d.startswith('<')
        parts = [int(x, 16) for x in d.split() if not x.startswith('<')]
        dec.append((cp, compat, parts))
        # a canonical pair that NFC puts back together (not excluded, not a singleton)
        if not compat and len(parts) == 2 and u.normalize('NFC', chr(parts[0]) + chr(parts[1])) == ch:
            comp.append(cp)
    c = u.combining(ch)
    if c: comb.append((cp, c))

out = bytearray()
varint(len(dec), out); last = 0
for cp, compat, parts in dec:
    varint(cp - last, out); last = cp
    varint(len(parts) << 1 | (1 if compat else 0), out)
    for p in parts: varint(p, out)
varint(len(comb), out); last = 0
for cp, c in comb:
    varint(cp - last, out); last = cp; varint(c, out)
varint(len(comp), out); last = 0
for cp in comp:
    varint(cp - last, out); last = cp

b64 = base64.b64encode(bytes(out)).decode()
lines = [b64[i:i + 100] for i in range(0, len(b64), 100)]
block = ('    /* Unicode %s normalization data (tools/gen-unicode.py): %d decompositions, %d combining\n'
         '       classes, %d composites; %d bytes packed. In a function, so a program that\n'
         '       never normalizes leaves it out. */\n'
         '    static const char* grav_uni_packed(void) {\n        static const char d[] =\n'
         % (u.unidata_version, len(dec), len(comb), len(comp), len(out)) +
         '\n'.join('            "%s"' % l for l in lines) + ';\n        return d;\n    }\n')
path = 'lib/text/unicode.grav'
src = open(path).read()
src = re.sub(r'(    // UNICODE-DATA BEGIN\n).*?(    // UNICODE-DATA END\n)', lambda m: m.group(1) + block + m.group(2), src, flags=re.S)
open(path, 'w').write(src)
print('decompositions', len(dec), 'combining', len(comb), 'composites', len(comp), 'packed bytes', len(out), 'base64', len(b64))
