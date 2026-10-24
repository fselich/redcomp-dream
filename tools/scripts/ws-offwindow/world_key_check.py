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
       world_key_check.py probe.csv --bottom-line 114 ...   # 2P split screen

With --bottom-line N the probe is scored per viewport: rows with sy < N use
the top-half scroll, rows with sy >= N use the bottom-half scroll, and each
half keeps its own native ground truth and margin counters. The default mode
is the original single-viewport 1P scorer and its output is unchanged.
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


def score_viewports(path, shift, masked, served, bottom_line):
    """2P mode: score the top and bottom halves independently."""
    word_col = "served" if served else "real"
    frames = defaultdict(lambda: defaultdict(list))
    with open(path) as f:
        for r in csv.DictReader(f):
            if r["frame"].startswith("#") or r["layer"] != "0":
                continue
            fr = int(r["frame"])
            sy = int(r["sy"])
            if sy >= 512:
                sy -= 1024
            # The two lines before the bottom viewport are the split divider;
            # they belong to neither viewport and the margin hook skips them.
            if bottom_line - 2 <= sy < bottom_line:
                continue
            vp = 1 if sy >= bottom_line else 0
            frames[fr][vp].append(r)

    absx, absy = {}, {}
    prev_raw, prev_abs = {}, {}
    for fr in sorted(frames):
        for vp in (0, 1):
            rows = frames[fr].get(vp)
            if not rows:
                if (fr, vp) not in absx and vp in prev_abs:
                    absx[(fr, vp)], absy[(fr, vp)] = prev_abs[vp]
                continue
            h = int(rows[0]["hscroll"])
            v = int(rows[0]["vscroll"])
            ph, pv = prev_raw.get(vp, (None, None))
            ah, av = prev_abs.get(vp, (None, None))
            ah = unwrap(ph, ah, h)
            av = unwrap(pv, av, v)
            absx[(fr, vp)], absy[(fr, vp)] = ah, av
            prev_raw[vp] = (h, v)
            prev_abs[vp] = (ah, av)

    native = defaultdict(lambda: defaultdict(set))
    margin = defaultdict(list)
    for fr, by_vp in frames.items():
        for vp, rows in by_vp.items():
            for r in rows:
                sx, w, sy = int(r["sx"]), int(r["w"]), int(r["sy"])
                if sy >= 512:
                    sy -= 1024
                word = int(r[word_col], 16)
                cx = sx + w // 2
                wx = (absx[(fr, vp)] + cx) >> shift
                wy = (absy[(fr, vp)] + sy) >> shift
                if 0 <= cx < 256:
                    native[vp][(wx, wy)].add(word)
                else:
                    margin[vp].append((fr, "W" if cx < 0 else "E",
                                       (wx, wy), word,
                                       (int(r["map_x"]), int(r["map_y"]))))

    print(f"frames={len(frames)}")
    for vp in (0, 1):
        conflicts = sum(1 for s in native[vp].values() if len(s) > 1)
        print(f"viewport {vp}: native_cells={len(native[vp])} "
              f"conflicting_cells={conflicts}")
    for vp in (0, 1):
        stats = defaultdict(lambda: [0, 0, 0, 0])
        for fr, side, cell, word, mapcell in margin[vp]:
            if (fr, mapcell[0], mapcell[1]) in masked:
                stats[side][3] += 1
                continue
            truth = native[vp].get(cell)
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
            print(f"margin vp{vp} {side}: correct={ok} wrong={bad} "
                  f"unverifiable={unk} masked={msk} "
                  f"({pct:.1f}% of verifiable unmasked)")


def main():
    path = sys.argv[1]
    shift = int(arg_value("--shift", "4"))
    masked = load_mask_log(arg_value("--mask-log"))
    served = "--served" in sys.argv
    word_col = "served" if served else "real"
    bottom_line = int(arg_value("--bottom-line", "0"))
    if bottom_line > 0:
        score_viewports(path, shift, masked, served, bottom_line)
        return
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
