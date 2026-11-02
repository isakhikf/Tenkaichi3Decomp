#!/usr/bin/env python3
"""Hybrid v5: the game side (src/battle/btl_char_hit.c, BtlHit_PortUdeRecipe) and the port side (port/src/ude_recipe.h)
must agree on the snapshot layout: every R[n] = ... /* UDE_X_NAME */ must be field n of the enum, every field must be
written, and UR_COUNT must equal UDE_X_COUNT."""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parents[3]
h = (root / 'port/src/ude_recipe.h').read_text()
body = h[h.index('enum {'):h.index('UDE_X_COUNT')]
names = re.findall(r'UDE_X_(\w+)', body)
c = (root / 'src/battle/btl_char_hit.c').read_text()
count = int(re.search(r'#define UR_COUNT (\d+)', c).group(1))
seen = {}
for n, name in re.findall(r'R\[(\d+)\] = [^;]*;\s*/\* (?:UDE_X_)?(\w+)', c):
    seen[int(n)] = name
bad = []
if count != len(names): bad.append('UR_COUNT %d != UDE_X_COUNT %d' % (count, len(names)))
for i, name in enumerate(names):
    if i not in seen: bad.append('field %d (%s) is not written with its name' % (i, name))
    elif seen[i] != name: bad.append('R[%d] is %s on the game side, %s on the port side' % (i, seen[i], name))
print('recipe snapshot fields=%d game=%d problems=%d' % (len(names), len(seen), len(bad)))
for b in bad: print('  ' + b)
sys.exit(1 if bad else 0)
