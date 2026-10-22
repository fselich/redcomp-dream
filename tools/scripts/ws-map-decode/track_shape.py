#!/usr/bin/env python3
"""One-pixel-per-cell shape map of a track (for thumbnails and ultrawide study).

Cells whose BG1 word uses a non-zero character (tile & 0x3FF, i.e. the track
tube, slopes, posts) are drawn dark; palette-only fill cells are light. Input
is an unpacked $7F image (scan_rnc.py) or rom:<track>.

usage: track_shape.py <blob.bin | rom:track> out.png [scale]
"""
import os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from track_cell import TrackMap
import rnc

src, out = sys.argv[1], sys.argv[2]
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
if src.startswith("rom:"):
    data = rnc.track(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../Uniracers (USA).sfc"), "rb").read(), src[4:])
else:
    data = open(src, "rb").read()
wram = bytes(0x10000) + data + bytes(0x10000 - len(data))
cols = (data[0x0D] or 256) * 4
tm = TrackMap(wram, cols)
W, H = cols * 4, tm.rows * 4
cells = [[(tm.cell(x, y) or 0) & 0x3FF for x in range(W)] for y in range(H)]
ys = [y for y in range(H) if any(cells[y])]; xs = [x for x in range(W) if any(cells[y][x] for y in ys)]
x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
rows = []
for y in range(y0, y1):
    line = bytearray()
    for x in range(x0, x1):
        line += b'\x10' * scale if cells[y][x] else b'\xee' * scale
    rows += [b'\x00' + bytes(line)] * scale
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', (x1 - x0) * scale, (y1 - y0) * scale, 8, 0, 0, 0, 0)) + \
      chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
open(out, 'wb').write(png)
print(f"{out}: {(x1-x0)*scale}x{(y1-y0)*scale} (grid {cols}x{tm.rows} blocks, cells {x0}..{x1} x {y0}..{y1})")
