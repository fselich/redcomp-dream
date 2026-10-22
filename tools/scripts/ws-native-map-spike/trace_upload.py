#!/usr/bin/env python3
"""Trace the BG1 column staging/upload path around a frame."""
import json
import sys
from dbg import Dbg

port = int(sys.argv[1]); target = int(sys.argv[2]); out = sys.argv[3]
dbg = Dbg(port)
dbg.wait_frame(1000)
for addr in (0x433, 0x455, 0x23e, 0x236, 0x238, 0x339):
    dbg.cmd(f"set_wram_watch 7e {addr:x} 1")
dbg.wait_frame(target)
res = {
  "frame": dbg.cmd("frame"),
  "ppu": dbg.cmd("get_ppu_state"),
  "cpu": dbg.cmd("get_cpu_state"),
  "w433": dbg.cmd(f"wram_watch_log_get 433 {target-20} {target+4} 1024"),
  "w455": dbg.cmd(f"wram_watch_log_get 455 {target-20} {target+4} 1024"),
  "w23e": dbg.cmd(f"wram_watch_log_get 23e {target-20} {target+4} 1024"),
  "w236": dbg.cmd(f"wram_watch_log_get 236 {target-20} {target+4} 1024"),
  "w238": dbg.cmd(f"wram_watch_log_get 238 {target-20} {target+4} 1024"),
  "w339": dbg.cmd(f"wram_watch_log_get 339 {target-20} {target+4} 1024"),
}
with open(out, "w") as f: json.dump(res, f)
for k,v in res.items():
    if isinstance(v,dict) and "events" in v: print(k,len(v["events"]))
    else: print(k,"ok")
dbg.close()
