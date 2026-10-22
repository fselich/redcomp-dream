#!/usr/bin/env python3
"""Capture WRAM timeseries for camera/world fields."""
import json
import sys
from dbg import Dbg

port = int(sys.argv[1])
first = int(sys.argv[2])
last = int(sys.argv[3])
out = sys.argv[4]
dbg = Dbg(port)
dbg.wait_frame(last)
result = {}
for addr, length, name in [
    (0x041D, 4, "scroll_world"),
    (0x00A5, 4, "track_world"),
    (0x04F1, 4, "track_stride"),
    (0x023E, 2, "source_ptr"),
]:
    r = dbg.cmd(f"wram_timeseries {addr:x} {length} {first} {last} 4096")
    result[name] = r
    print(name, "entries", len(r.get("entries", [])), flush=True)
with open(out, "w") as f:
    json.dump(result, f)
dbg.close()
