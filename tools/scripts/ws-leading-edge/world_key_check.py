#!/usr/bin/env python3
"""World-key consistency check for BG1 from a renderer bgprobe CSV.

Each probe row is one span of screen pixels that the renderer fetched from a
single VRAM map word. With BG1 in 16x16 mode the world cell of a span is

    world_x = (unwrapped_hscroll + sx) >> 4
    world_y = (unwrapped_vscroll + sy) >> 4

The scroll registers are unwrapped frame-to-frame (delta taken mod 1024).
A world cell is "conflicting" when two observations of it carry different
map words. Native spans (0 <= sx < 256) are ground truth; margin spans
(sx < 0 or sx >= 256, only present in a wide run) are scored against it.

With --mask-log (the CSV written by SNESRECOMP_WS_OFFWINDOW_LOG), spans whose
map cell was masked to background by the host presentation hook are counted
separately and never as wrong. --served scores the tile the renderer actually
used (the probe's `served` column) instead of the raw VRAM entry (`real`).

usage: world_key_check.py probe.csv [--shift 4] [--mask-log path] [--served]
"""
import csv
import sys
from collections import defaultdict


def unwrap(prev_raw, prev_abs, raw, mod=1024):
    if prev_raw is None:
        return raw
    d = (raw - prev_raw) % mod
    if d >= mod // 2:
        d -= mod
    return prev_abs + d


def arg_value(name, default=None):
    if name not in sys.argv:
        return default
    return sys.argv[sys.argv.index(name) + 1]


def load_mask_log(path):
    masked = set()
    if not path:
        return masked
    with open(path) as f:
        for line in f:
            if not line or line.startswith("#"):
                continue
            p = line.strip().split(",")
            if len(p) < 3:
                continue
            if len(p) >= 8 and p[7].strip() == "0":
                continue
            masked.add((int(p[0]), int(p[1]), int(p[2])))
    return masked


def main():
    path = sys.argv[1]
    shift = int(arg_value("--shift", "4"))
    masked = load_mask_log(arg_value("--mask-log"))
    served = "--served" in sys.argv
    word_col = "served" if served else "real"
    frames = defaultdict(list)
    scroll = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            if r["frame"].startswith("#") or r["layer"] != "0":
                continue
            fr = int(r["frame"])
            frames[fr].append(r)
            scroll[fr] = (int(r["hscroll"]), int(r["vscroll"]))

    absx = {}
    absy = {}
    ph = pv = ah = av = None
    for fr in sorted(frames):
        h, v = scroll[fr]
        ah = unwrap(ph, ah, h)
        av = unwrap(pv, av, v)
        ph, pv = h, v
        absx[fr], absy[fr] = ah, av

    native = defaultdict(set)   # world cell -> set of words seen natively
    margin = []                 # (frame, side, cell, word, map cell)
    for fr, rows in frames.items():
        for r in rows:
            sx, w, sy = int(r["sx"]), int(r["w"]), int(r["sy"])
            if sy >= 512:
                sy -= 1024
            word = int(r[word_col], 16)
            # sample the span at its centre to avoid fine-phase edges
            cx = sx + w // 2
            wx = (absx[fr] + cx) >> shift
            wy = (absy[fr] + sy) >> shift
            if 0 <= cx < 256:
                native[(wx, wy)].add(word)
            else:
                margin.append((fr, "W" if cx < 0 else "E", (wx, wy), word,
                               (int(r["map_x"]), int(r["map_y"]))))

    conflicts = sum(1 for s in native.values() if len(s) > 1)
    print(f"frames={len(frames)} native_cells={len(native)} "
          f"conflicting_cells={conflicts}")
    if not margin:
        return
    stats = defaultdict(lambda: [0, 0, 0, 0])  # side -> ok, wrong, unknown, masked
    for fr, side, cell, word, mapcell in margin:
        if (fr, mapcell[0], mapcell[1]) in masked:
            stats[side][3] += 1
            continue
        truth = native.get(cell)
        if not truth:
            stats[side][2] += 1
        elif word in truth:
            stats[side][0] += 1
        else:
            stats[side][1] += 1
    for side in sorted(stats):
        ok, bad, unk, msk = stats[side]
        tot = ok + bad
        pct = 100.0 * ok / tot if tot else 0.0
        print(f"margin {side}: correct={ok} wrong={bad} unverifiable={unk} "
              f"masked={msk} ({pct:.1f}% of verifiable unmasked)")


if __name__ == "__main__":
    main()
