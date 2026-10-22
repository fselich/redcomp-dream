#!/usr/bin/env python3
"""Trace live stage-2 WRAM writes and BG1 VRAM writes around a frame."""
import json
import sys
from dbg import Dbg

port = int(sys.argv[1])
target = int(sys.argv[2])
out = sys.argv[3]
dbg = Dbg(port)
dbg.wait_frame(1000)
for addr in (0x230, 0x232, 0x260, 0x262):
    print(addr, dbg.cmd(f"set_wram_watch 7e {addr:x} 2"), flush=True)
dbg.wait_frame(target)
result = {
    "frame": dbg.cmd("frame"),
    "cpu": dbg.cmd("get_cpu_state"),
    "ppu": dbg.cmd("get_ppu_state"),
    "writes_230": dbg.cmd(f"wram_watch_log_get 230 {target-100} {target+4} 1024"),
    "writes_260": dbg.cmd(f"wram_watch_log_get 260 {target-100} {target+4} 1024"),
    "vram_writes": dbg.cmd("vwring_get 1800 1fff 1024"),
}
with open(out, "w") as f:
    json.dump(result, f)
for k, v in result.items():
    if isinstance(v, dict) and "events" in v:
        print(k, "events", len(v["events"]))
    elif isinstance(v, dict) and "log" in v:
        print(k, "log", len(v["log"]))
    else:
        print(k, "ok")
dbg.close()
