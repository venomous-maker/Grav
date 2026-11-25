#!/usr/bin/env python3
"""Writes HPACK's static table and Huffman code (RFC 7541, appendices A and B)
into lib/net/http2.grav between the HPACK-DATA markers, from Python's hpack
package (run: tools/gen-hpack.py)."""
import re
from hpack import huffman_constants as h
from hpack.table import HeaderTable as T

def c(s): return '"' + s.decode().replace('\\', '\\\\').replace('"', '\\"') + '"'
static = ',\n        '.join('{%s, %s}' % (c(n), c(v)) for n, v in T.STATIC_TABLE)
codes = ', '.join('0x%x' % x for x in h.REQUEST_CODES)
lens = ', '.join(str(x) for x in h.REQUEST_CODES_LENGTH)
block = ('    /* HPACK (RFC 7541): the static table and the Huffman code (tools/gen-hpack.py). */\n'
         '    static const char* const grav_hp_static[61][2] = {\n        ' + static + '\n    };\n'
         '    static const uint32_t grav_hp_codes[257] = {' + codes + '};\n'
         '    static const uint8_t grav_hp_lens[257] = {' + lens + '};\n')
path = 'lib/net/http2.grav'
src = open(path).read()
src = re.sub(r'(    // HPACK-DATA BEGIN\n).*?(    // HPACK-DATA END\n)', lambda m: m.group(1) + block + m.group(2), src, flags=re.S)
open(path, 'w').write(src)
print('static', len(T.STATIC_TABLE), 'codes', len(h.REQUEST_CODES))
