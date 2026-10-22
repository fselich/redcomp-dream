#!/usr/bin/env python3
"""Stitch BG1 32-column VRAM snapshots into a world-keyed tilemap.

Usage: stitch_bg1.py <bg1-seq.json> <out-prefix>

The sequence is captured during a live race. PPU hScroll gives the candidate
world tile column; the 32-column ring gives the content. We unwrap the scroll
and verify overlaps, then emit:
  <out-prefix>.json   world columns and per-frame alignments
  <out-prefix>.txt    ASCII coverage map
"""
import json
import sys
from collections import Counter


def decode_columns(hex_data):
    data = bytes.fromhex(hex_data)
    cols = []
    for x in range(32):
        col = []
        for y in range(32):
            p = 2 * (y * 32 + x)
            col.append(int.from_bytes(data[p:p + 2], "little"))
        cols.append(tuple(col))
    return cols


def main():
    src = sys.argv[1]
    prefix = sys.argv[2]
    d = json.load(open(src))
    world = {}
    conflicts = 0
    frames = []
    prev_a = None
    prev_raw = None

    for fr in d["frames"]:
        h = fr["ppu"]["ppu_hscroll"][0]
        raw = h // 8
        cols = decode_columns(fr["bg1"])
        if prev_a is None:
            cand = [raw]
        else:
            delta = (raw - prev_raw) % 128
            base = prev_a + delta
            cand = [base + k * 32 for k in (-2, -1, 0, 1, 2)]
        best = None
        for a in cand:
            hits = 0
            mism = 0
            for m, col in enumerate(cols):
                wc = a + ((m - a) % 32)
                old = world.get(wc)
                if old is None:
                    continue
                if old == col:
                    hits += 1
                else:
                    mism += 1
            score = hits * 4 - mism * 8 - abs(a - (prev_a + ((raw - prev_raw) % 128) if prev_a is not None else raw)) * 0.01
            if best is None or score > best[0]:
                best = (score, a, hits, mism)
        _, a, hits, mism = best
        for m, col in enumerate(cols):
            wc = a + ((m - a) % 32)
            old = world.get(wc)
            if old is not None and old != col:
                conflicts += 1
            world[wc] = col
        frames.append({"f": fr["f"], "h": h, "raw": raw, "a": a,
                       "hits": hits, "mism": mism})
        prev_a, prev_raw = a, raw

    lo, hi = min(world), max(world)
    # ASCII: one char per world column, per 32 rows. '.' blank, '#' track-ish.
    lines = []
    for y in range(32):
        s = []
        for wc in range(lo, hi + 1):
            col = world.get(wc)
            if col is None:
                s.append("?")
            else:
                w = col[y]
                s.append("." if w == 0 else ("#" if (w & 0x3ff) == 0 else "x"))
        lines.append("".join(s))
    ascii_map = "\n".join(lines) + "\n"
    with open(prefix + ".txt", "w") as f:
        f.write(f"world columns {lo}..{hi} ({hi - lo + 1}), conflicts={conflicts}\n")
        f.write(ascii_map)
    with open(prefix + ".json", "w") as f:
        json.dump({"source": src, "lo": lo, "hi": hi, "conflicts": conflicts,
                   "frames": frames,
                   "world": {str(k): list(v) for k, v in sorted(world.items())}},
                  f)
    print(f"world {lo}..{hi} ({hi - lo + 1} cols), conflicts={conflicts}")
    print(ascii_map)
    print("worst frames:")
    for x in sorted(frames, key=lambda z: z["mism"], reverse=True)[:10]:
        print(x)


if __name__ == "__main__":
    main()
