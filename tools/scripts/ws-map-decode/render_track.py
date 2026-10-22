#!/usr/bin/env python3
"""Render a full Uniracers track's BG1 layer from decoded WRAM tables.

Uses track_cell.TrackMap (piece grid), the race's static BG1 CHR from a VRAM
dump (BG1 char base word 0x2000, 4bpp, 16x16 tiles = chars c, c+1, c+16, c+17)
and CGRAM (BGR555) from a dump. Colour 0 is drawn as the backdrop (CGRAM 0).
Writes a PNG (stdlib zlib, no PIL).

usage: render_track.py wram.bin vram.bin cgram.bin cols out.png [scale_div] [cx0 cx1 cy0 cy1]
       render_track.py rom:<track> vram.bin cgram.bin auto out.png [...]
  rom:<track> unpacks the track from the ROM (rnc.py) instead of a WRAM dump;
  blob:<file> uses an unpacked $7F image (scan_rnc.py writes unpacked/rnc-<offset>.bin);
  cols `auto` reads the grid width from the unpacked header ($7F:000D = cols/4,
  0 means 256; $7F:000E = rows/4).
(optional crop in world cells; default = occupied bounding box)
"""
import sys, zlib, struct
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from track_cell import TrackMap

ROM = __file__.rsplit('/', 3)[0] + "/../Uniracers (USA).sfc"
if sys.argv[1].startswith("blob:"):          # an unpacked $7F image (scan_rnc.py output)
    data = open(sys.argv[1][5:], "rb").read()
    wram = bytes(0x10000) + data + bytes(0x10000 - len(data))
elif sys.argv[1].startswith("rom:"):
    import rnc
    data = rnc.track(open(ROM, "rb").read(), sys.argv[1][4:])
    wram = bytes(0x10000) + data + bytes(0x10000 - len(data))
else:
    wram = open(sys.argv[1], 'rb').read()
vram, cgram = (open(p, 'rb').read() for p in sys.argv[2:4])
if sys.argv[4] == "auto":
    cols = (wram[0x1000D] or 256) * 4
else:
    cols = int(sys.argv[4], 0)
out = sys.argv[5]
div = int(sys.argv[6]) if len(sys.argv) > 6 else 1
tm = TrackMap(wram, cols)
vw = lambda a: vram[2 * (a & 0x7FFF)] | vram[2 * (a & 0x7FFF) + 1] << 8
pal = []
for i in range(256):
    c = cgram[2 * i] | cgram[2 * i + 1] << 8
    pal.append(((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31))
CHR = 0x2000
cache = {}
def tile_pixels(word):
    if word in cache: return cache[word]
    px = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            tx, ty = (15 - x if word & 0x4000 else x), (15 - y if word & 0x8000 else y)
            ch = ((word & 0x3FF) + (tx >> 3) + ((ty >> 3) << 4)) & 0x3FF
            a = CHR + ch * 16 + (ty & 7)
            p01, p23 = vw(a), vw(a + 8); b = 7 - (tx & 7)
            idx = (p01 >> b & 1) | (p01 >> (8 + b) & 1) << 1 | (p23 >> b & 1) << 2 | (p23 >> (8 + b) & 1) << 3
            px[y][x] = pal[((word >> 10) & 7) * 16 + idx] if idx else pal[0]
    cache[word] = px
    return px

W, H = tm.cols * 4, tm.rows * 4                     # cells
# crop to the occupied bounding box
occ = [(bx, by) for by in range(tm.rows) for bx in range(tm.cols) if tm.block(bx, by)]
bx0 = min(b[0] for b in occ); bx1 = max(b[0] for b in occ); by0 = min(b[1] for b in occ); by1 = max(b[1] for b in occ)
cx0, cx1, cy0, cy1 = bx0 * 4, bx1 * 4 + 4, by0 * 4, by1 * 4 + 4
if len(sys.argv) > 10:
    cx0, cx1, cy0, cy1 = (int(v) for v in sys.argv[7:11])
pw, ph = (cx1 - cx0) * 16 // div, (cy1 - cy0) * 16 // div
rows = []
for py in range(ph):
    y = py * div; wy = cy0 + y // 16
    line = bytearray([0])
    for px_ in range(pw):
        x = px_ * div; wx = cx0 + x // 16
        word = tm.cell(wx, wy) or 0
        line += bytes(tile_pixels(word)[y & 15][x & 15])
    rows.append(bytes(line))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', pw, ph, 8, 2, 0, 0, 0)) + \
      chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
open(out, 'wb').write(png)
print(f"{out}: {pw}x{ph} px (cells {cx0}..{cx1} x {cy0}..{cy1}, div {div})")
