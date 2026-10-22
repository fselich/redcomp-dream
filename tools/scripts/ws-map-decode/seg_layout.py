#!/usr/bin/env python3
"""Locate BG1 map row segments inside WRAM bank $7F (same dump).

For every BG1 map row (map base 0x0C00, 32x32, VRAM dump read LITTLE-endian,
which is what the renderer uses), take each 4-cell window with >=2 distinct
words and search it as raw little-endian bytes in bank $7F (WRAM 0x10000..).
Prints the $7F offset of the first hit ('*' = several hits, '---' = none,
'.' = flat window skipped).

Observed (Zoom Zoo 3504): +2 bytes per cell to the right only within groups
of 4 cells, +8 bytes per map row downwards -> the high table at $7F800F holds
4-cell-wide tile-word pieces with a 4-word row pitch.

usage: seg_layout.py <wram.bin> <vram.bin> [map_base_hex]
"""
import struct, sys
w = open(sys.argv[1], 'rb').read(); v = open(sys.argv[2], 'rb').read()
base = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x0C00
b7 = w[0x10000:]
vw = lambda a: v[2 * a] | v[2 * a + 1] << 8
mapw = [vw(base + i) for i in range(1024)]

def finds(pat):
    out = []; i = b7.find(pat)
    while i >= 0 and len(out) < 4:
        out.append(i); i = b7.find(pat, i + 1)
    return out

for row in range(32):
    ws = mapw[row * 32:row * 32 + 32]
    line = []
    for c0 in range(29):
        seg = ws[c0:c0 + 4]
        if len(set(seg)) < 2: line.append('  .  '); continue
        f = finds(b''.join(struct.pack('<H', x) for x in seg))
        line.append(('%04X' % f[0]) + ('*' if len(f) > 1 else ' ') if f else ' --- ')
    print('%2d' % row, ' '.join(line))
