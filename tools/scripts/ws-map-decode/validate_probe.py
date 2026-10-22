#!/usr/bin/env python3
"""Score TrackMap (track_cell.py, hypothesis H1) against renderer-probe ground truth.

Native probe spans (0 <= centre < 256, layer 0, column `real`) give world-keyed
BG1 words with world = (unwrapped_scroll + screen) >> 4, unwrapped from the
first probed frame's raw register. The absolute origin is not tied to the scroll
register, so any integer cell offset is searched (sample prefilter, then full score).
Cells outside the low-table grid are predicted as 0x0000. Spans whose map
address is outside the race ring (0x0C00-0x0FFF, i.e. HUD bands) are ignored.

usage: validate_probe.py wram.bin cols probe.csv
"""
import csv, os, sys
from collections import defaultdict
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from track_cell import TrackMap

def unwrap(pr, pa, raw):
    if pr is None: return raw
    d = (raw - pr) % 1024
    if d >= 512: d -= 1024
    return pa + d

wram = open(sys.argv[1], 'rb').read(); cols = int(sys.argv[2], 0)
tm = TrackMap(wram, cols)
rows = defaultdict(list); sc = {}
for r in csv.DictReader(open(sys.argv[3])):
    if r['frame'].startswith('#') or r['layer'] != '0': continue
    f = int(r['frame']); rows[f].append(r); sc[f] = (int(r['hscroll']), int(r['vscroll']))
ax, ay = {}, {}; ph = pv = ah = av = None
for f in sorted(rows):
    h, v = sc[f]; ah = unwrap(ph, ah, h); av = unwrap(pv, av, v); ph, pv = h, v
    ax[f], ay[f] = ah, av
truth = {}
for f, rs in rows.items():
    for r in rs:
        cx = int(r['sx']) + int(r['w']) // 2
        if not 0 <= cx < 256: continue
        # only the race ring: HUD bands repoint BG1's map base on some lines
        if not 0x0C00 <= int(r['map_word'], 16) < 0x1000: continue
        truth[((ax[f] + cx) >> 4, (ay[f] + int(r['sy'])) >> 4)] = int(r['real'], 16)

def pred(wx, wy):
    c = tm.cell(wx, wy)
    return 0 if c is None else c

import random
random.seed(1)
SLACK = int(os.environ.get('VALIDATE_SLACK', '4'))   # sample cells allowed to miss in the prefilter
items = list(truth.items())
sample = random.sample(items, min(48, len(items)))
best = None
for ox in range(-64 - min(k[0] for k in truth), tm.cols * 4 + 64):
    for oy in range(-64 - min(k[1] for k in truth), tm.rows * 4 + 64):
        hit = 0
        for (wx, wy), wd in sample:
            if pred(wx + ox, wy + oy) == wd: hit += 1
        if hit >= len(sample) - SLACK:
            ok = sum(1 for (wx, wy), wd in items if pred(wx + ox, wy + oy) == wd)
            if best is None or ok > best[0]: best = (ok, ox, oy)
if best is None:
    print("no origin with sample agreement"); sys.exit(0)
ok, ox, oy = best
bad = [(k, wd, pred(k[0] + ox, k[1] + oy)) for k, wd in truth.items() if pred(k[0] + ox, k[1] + oy) != wd]
print(f"cells={len(truth)} match={ok} ({100*ok/len(truth):.2f}%) origin_offset=({ox},{oy}) mismatches={len(bad)}")
for (k, wd, p) in sorted(bad)[:15]:
    print(f"  world=({k[0]+ox},{k[1]+oy}) probe={wd:04x} pred={p:04x} block={tm.block((k[0]+ox)>>2,(k[1]+oy)>>2)}")
