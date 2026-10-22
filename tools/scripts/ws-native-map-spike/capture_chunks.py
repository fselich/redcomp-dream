#!/usr/bin/env python3
"""Capture staging/map/DMA history around a frame to infer chunk mapping."""
import json
import sys
from dbg import Dbg

port = int(sys.argv[1]); target = int(sys.argv[2]); out = sys.argv[3]
lo, hi = target - 16, target
dbg = Dbg(port)
dbg.wait_frame(hi)
frames = []
for f in range(lo, hi + 1):
    w = dbg.cmd(f"dump_frame_wram {f} 400 256")
    v = dbg.cmd(f"dump_frame_vram {f} 1800 2048")
    d = dbg.cmd(f"ppu_dmas frame={f} count=64")
    frames.append({"f": f, "stage": w.get("hex"), "map": v.get("hex"), "dmas": d})
    print(f, "stage" if w.get("hex") else w, "dmas", len(d.get("dmas", [])), flush=True)
with open(out, "w") as fh:
    json.dump({"lo": lo, "hi": hi, "frames": frames}, fh)
dbg.close()
