#!/usr/bin/env python3
"""Compare a ghost-served margin 16x16 block with the same world cell's native
block when it later becomes visible. Both are aligned to the world cell, so the
tile pixels should match exactly."""
import csv, sys
from collections import defaultdict

def readppm(p):
    f = open(p, 'rb'); assert f.readline().strip() == b'P6'
    line = f.readline()
    while line.startswith(b'#'): line = f.readline()
    w, h = map(int, line.split()); f.readline(); return w, h, f.read()

def block(img, w, x, y, n=16):
    return [img[((y+j)*w + x)*3:((y+j)*w + x)*3+3] for j in range(n) for _ in [0]]

ghost_probe, mask_log, ghost_ppm, ring_probe, ring_ppm = sys.argv[1:6]
served = {}
with open(mask_log) as f:
    for l in f:
        if l.startswith('#'): continue
        p = l.strip().split(',')
        if len(p) < 8 or p[7] != '0': continue
        if int(p[0]) != 2500: continue
        served[(int(p[1]), int(p[2]))] = (int(p[3]), int(p[4]), int(p[5],16), int(p[6],16))
# Gather 16-line runs of a served margin cell in the ghost probe.
runs = defaultdict(list)
with open(ghost_probe) as f:
    for r in csv.DictReader(f):
        if int(r['frame']) != 2500: continue
        cell = (int(r['map_x']), int(r['map_y']))
        if cell not in served: continue
        sx = int(r['sx'])
        if sx < 256: continue
        runs[cell].append((int(r['sy']), sx, int(r['w']), int(r['served'],16)))
candidates = []
for cell, rows in runs.items():
    rows.sort()
    for i in range(len(rows)-15):
        seg = rows[i:i+16]
        if all(seg[j][0] == seg[0][0]+j and seg[j][1] == seg[0][1] and
               seg[j][2] == seg[0][2] for j in range(16)):
            candidates.append((cell, seg[0][1], seg[0][0], seg[0][2], seg[0][3]))
if not candidates:
    print('no 16-line served margin run'); raise SystemExit(1)
for c in candidates[:10]:
    print('ghost candidate', c)
# Pick the first candidate with a full 16px span inside the margin.
cell, gx, gsy, gw, gtile = next(c for c in candidates if c[3] >= 16)
gx = gx; gy = gsy - 1
gw_img, gh_img, gimg = readppm(ghost_ppm)
# Find the same ring cell in the native view at frame 2506, with the same tile.
native = defaultdict(list)
with open(ring_probe) as f:
    for r in csv.DictReader(f):
        if int(r['frame']) != 2506: continue
        if (int(r['map_x']), int(r['map_y'])) != cell: continue
        sx = int(r['sx'])
        if 0 <= sx and sx + int(r['w']) <= 256:
            native[int(r['sy'])].append((sx, int(r['w']), int(r['served'],16), int(r['real'],16)))
rows = []
for sy in sorted(native):
    for sx,w,sv,re in native[sy]:
        if sv == gtile or re == gtile:
            rows.append((sy,sx,w,sv))
if not rows:
    print('native match not found for cell', cell, 'tile', hex(gtile)); raise SystemExit(1)
# Contiguous 16-line run.
rows.sort()
nseg = None
for i in range(len(rows)-15):
    if all(rows[i+j][0] == rows[0][0]+j and rows[i+j][1] == rows[i][1] for j in range(16)):
        nseg = rows[i:i+16]; break
if nseg is None:
    print('no contiguous native 16-line run', rows[:5]); raise SystemExit(1)
nsy, nx = nseg[0][0], nseg[0][1]
nw_img, nh_img, nimg = readppm(ring_ppm)
g = block(gimg, gw_img, gx + 43, gy, 16)
n = block(nimg, nw_img, nx + 43, nsy-1, 16)
diff = sum(1 for a,b in zip(g,n) if a != b)
print(f'cell {cell} tile=0x{gtile:04x} ghost x={gx+43} y={gy} native x={nx+43} y={nsy-1} '
      f'block_bytes={len(g)*3} differing_pixels={diff}')
if diff:
    for i,(a,b) in enumerate(zip(g,n)):
        if a!=b:
            print(' first diff', i%16, i//16, a.hex(), b.hex()); break
sys.exit(0 if diff == 0 else 2)
