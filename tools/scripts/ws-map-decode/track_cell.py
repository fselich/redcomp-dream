#!/usr/bin/env python3
"""Hypothesis H1 for the Uniracers BG1 track map in WRAM bank $7F.

  low  table  $7F:000F  16-bit, grid of `cols` ($04F1) x rows, row stride $04F3 bytes
  high table  $7F:800F  pieces of 4x4 BG1 cells, 16 words = 32 bytes each,
                        row pitch 8 bytes, cell (x,y) at +y*8 + x*2
  cell(wx, wy) = high[ low[wy>>2][wx>>2] ][wy&3][wx&3]

Library use: import and call TrackMap(wram_bytes, cols).cell(wx, wy).
CLI: track_cell.py wram.bin vram.bin ppu.txt -> searches the world origin of the
visible BG1 window near $00A5/$00A7 and reports the match rate.
"""
import sys, re

class TrackMap:
    def __init__(self, wram, cols, lo_base=0x000F, hi_base=0x800F):
        self.b7 = wram[0x10000:]
        self.cols = cols
        self.lo_base, self.hi_base = lo_base, hi_base
        # the last row can be partial: 0x8000 - lo_base is not a multiple of 2*cols
        self.rows = -(-(0x8000 - lo_base) // (2 * cols))   # 16-col tracks: 1024 rows

    def w16(self, off):
        return self.b7[off] | self.b7[off + 1] << 8

    def block(self, bx, by):
        if not (0 <= bx < self.cols and 0 <= by < self.rows):
            return None
        off = self.lo_base + 2 * (by * self.cols + bx)
        if off + 1 >= 0x8000:
            return None
        return self.w16(off)

    def cell(self, wx, wy):
        pid = self.block(wx >> 2, wy >> 2)
        if pid is None:
            return None
        off = self.hi_base + pid * 32 + (wy & 3) * 8 + (wx & 3) * 2
        if off + 1 >= len(self.b7):
            return None
        return self.w16(off)

def main():
    wram = open(sys.argv[1], 'rb').read(); vram = open(sys.argv[2], 'rb').read()
    ppu = dict(kv.split('=') for kv in open(sys.argv[3]).read().split())
    h, v = int(ppu['hscroll0']), int(ppu['vscroll0'])
    a5, a7 = int(ppu['a5'], 16), int(ppu['a7'], 16)
    cols = int(ppu['04f1'], 16)
    tm = TrackMap(wram, cols)
    vw = lambda a: vram[2 * a] | vram[2 * a + 1] << 8
    # visible native cells: screen x 0..255, y 0..223 (sample cell centres)
    vis = []
    for sy in range(8, 224, 16):
        for sx in range(8, 256, 16):
            mx, my = ((h + sx) >> 4) & 31, ((v + sy) >> 4) & 31
            vis.append((sx, sy, vw(0x0C00 + my * 32 + mx)))
    best = None
    for dx in range(-64, 65):
        for dy in range(-64, 65):
            ox, oy = (a5 >> 4) + dx, (a7 >> 4) + dy   # world cell of screen (0,0)
            ok = sum(1 for sx, sy, wd in vis if tm.cell(ox + (sx >> 4), oy + (sy >> 4)) == wd)
            if best is None or ok > best[0]:
                best = (ok, dx, dy)
    print(f"cols={cols} rows={tm.rows} visible={len(vis)} best match={best[0]} "
          f"at origin (a5>>4){best[1]:+d}, (a7>>4){best[2]:+d}  "
          f"[h={h} v={v} a5={a5:#x} a7={a7:#x}]")

if __name__ == '__main__':
    main()
