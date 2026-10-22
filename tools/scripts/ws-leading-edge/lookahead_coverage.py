#!/usr/bin/env python3
"""Upper-bound coverage of a K-frame lookahead harvest for 16:9 margin cells.

For every margin span at frame t (wide probe CSV), ask: was the world cell it
needs visible in the native window at some frame in [t-H, t+K]? (t-H..t =
history, which Option A already serves; t+1..t+K = what a K-frame speculative
run-ahead would reveal with the same input). Same-run capture, so the
speculative camera path equals the real one (input-hold routes).

usage: lookahead_coverage.py wide_probe.csv [K_max]
"""
import csv, sys
from collections import defaultdict

def unwrap(pr, pa, raw):
    if pr is None: return raw
    d = (raw - pr) % 1024
    if d >= 512: d -= 1024
    return pa + d

rows = defaultdict(list); sc = {}
for r in csv.DictReader(open(sys.argv[1])):
    if r['frame'].startswith('#') or r['layer'] != '0': continue
    f = int(r['frame']); rows[f].append(r); sc[f] = (int(r['hscroll']), int(r['vscroll']))
ax = {}; ay = {}; ph = pv = ah = av = None
for f in sorted(rows):
    h, v = sc[f]; ah = unwrap(ph, ah, h); av = unwrap(pv, av, v); ph, pv = h, v
    ax[f], ay[f] = ah, av
first_seen = {}   # world cell -> frames where visible natively
seen = defaultdict(list)
margin = []
for f in sorted(rows):
    for r in rows[f]:
        cx = int(r['sx']) + int(r['w']) // 2
        k = ((ax[f] + cx) >> 4, (ay[f] + int(r['sy'])) >> 4)
        if 0 <= cx < 256: seen[k].append(f)
        else: margin.append((f, 'W' if cx < 0 else 'E', k))
kmax = int(sys.argv[2]) if len(sys.argv) > 2 else 6
f0, f1 = min(rows), max(rows)
for side in 'WE':
    out = []
    for K in range(0, kmax + 1):
        ok = tot = 0
        for f, s, k in margin:
            if s != side or f + kmax > f1 or f - 60 < f0: continue
            tot += 1
            if any(f - 10_000 <= g <= f + K for g in seen.get(k, ())): ok += 1
        out.append(f"K={K}:{100*ok/tot:.1f}%")
    print(side, ' '.join(out))
