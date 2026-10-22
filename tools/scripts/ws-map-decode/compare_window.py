#!/usr/bin/env python3
"""Pixel-compare a ROM-rendered BG1 window with an emulator screenshot (task 5.2).

Only pixels where the render's BG1 is opaque (not the CGRAM-0 backdrop) are
compared; BG2/BG3/OBJ and colour math elsewhere make the rest meaningless.
Screen pixels covered by the HUD text or sprites will show up as differences.

Alignment: screen row r shows BG line vscroll + r + 1 (the renderer walks lines
1..224), so crop the raw render one row lower than the scroll suggests.
Verified on Bowl 2300: 3,741/3,741 opaque BG1 pixels within 8/255.

usage: compare_window.py render.png native.ppm backdrop_hex(rrggbb)
"""
import struct, sys, zlib
def readpng(p):
    d = open(p, 'rb').read(); i = 8; idat = b''
    while i < len(d):
        n = struct.unpack('>I', d[i:i + 4])[0]; t = d[i + 4:i + 8]; c = d[i + 8:i + 8 + n]
        if t == b'IHDR': w, h = struct.unpack('>II', c[:8])
        if t == b'IDAT': idat += c
        i += 12 + n
    raw = zlib.decompress(idat); s = w * 3 + 1
    return w, h, b''.join(raw[y * s + 1:(y + 1) * s] for y in range(h))
w, h, ren = readpng(sys.argv[1])
nat = open(sys.argv[2], 'rb').read().split(b'\n', 3)[3]
bd = bytes.fromhex(sys.argv[3])
op = eq = 0; near = 0
for i in range(0, w * h * 3, 3):
    a = ren[i:i + 3]
    if a == bd: continue
    op += 1; b = nat[i:i + 3]
    if a == b: eq += 1
    elif all(abs(x - y) <= 8 for x, y in zip(a, b)): near += 1
print(f"opaque BG1 pixels {op}: exact {eq} ({100*eq/op:.1f}%), within 8/255 {near}, other {op-eq-near}")
