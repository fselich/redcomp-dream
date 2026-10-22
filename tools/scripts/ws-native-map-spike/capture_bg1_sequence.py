#!/usr/bin/env python3
"""Capture BG1 map columns for many historical frames of a race.

Usage: capture_bg1_sequence.py <port> <first> <last> <step> <out.json>
The trace build keeps 6000 frames of VRAM history. We only fetch the BG1 map
(1 KB words at VRAM byte 0x1800), not the whole VRAM.
"""
import json
import sys

from dbg import Dbg

BG1_BYTE_BASE = 0x1800
BG1_BYTES = 32 * 32 * 2


def main():
    port = int(sys.argv[1])
    first = int(sys.argv[2])
    last = int(sys.argv[3])
    step = int(sys.argv[4])
    out = sys.argv[5]
    dbg = Dbg(port)
    dbg.wait_frame(last)
    frames = []
    for f in range(first, last + 1, step):
        r = dbg.cmd(f"dump_frame_vram {f} {BG1_BYTE_BASE:x} {BG1_BYTES}")
        if "hex" not in r:
            print(f"skip {f}: {r}", flush=True)
            continue
        frames.append({"f": f, "bg1": r["hex"]})
        if len(frames) % 20 == 0:
            print(f"captured {len(frames)} frames (through {f})", flush=True)
    # Scroll/PPU registers in small chunks: the debug reply is capped at
    # 32 KB, so a large range silently truncates.
    scroll = []
    a = first
    while a <= last:
        b = min(last, a + 99)
        r = dbg.cmd(f"get_frame_range_extended {a} {b}")
        scroll.extend(r.get("frames", []))
        a = b + 1
    by_frame = {f["f"]: f for f in frames}
    for s in scroll:
        if s["f"] in by_frame:
            by_frame[s["f"]]["ppu"] = s
    with open(out, "w") as fh:
        json.dump({"bg1_byte_base": BG1_BYTE_BASE,
                   "first": first, "last": last, "step": step,
                   "frames": frames}, fh)
    print(f"wrote {out}: {len(frames)} frames, {len(scroll)} scroll rows", flush=True)
    dbg.close()


if __name__ == "__main__":
    main()
