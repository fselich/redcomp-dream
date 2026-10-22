#!/usr/bin/env python3
"""Find who fills WRAM bank $7F at track load (map-decode spike, task 4.1).

Starts the isolated build (build-decode) with the debug server on 9998, arms
WRAM write watches on the first words of the low table ($7F:000F..) and the
high table ($7F:800F..) plus a few interior samples, runs the route through the
track load and prints each watched write: frame, writer PC, enclosing function,
registers.

usage: trace_loader.py <route.txt> <stop_frame> <out.json> [low_sample high_sample ...]
"""
import json, os, subprocess, sys, time
sys.path.insert(0, "<workshop>/work/ws-native-map-spike/scripts")
from dbg import Dbg

ROOT = "<workshop>"
BUILD = ROOT + "/work/ws-map-decode/project/UniracersSNESRecomp/build-decode"
route, stop, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
addrs = [int(a, 16) for a in sys.argv[4:]] or [0x000F, 0x0010, 0x0011, 0x0400, 0x2000,
                                               0x800F, 0x8010, 0x802F, 0x8400, 0xA000]
for f in ("saves/save.srm", "saves/save.srm.bak"):
    try: os.remove(os.path.join(BUILD, f))
    except FileNotFoundError: pass
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
           SNESRECOMP_NO_LAUNCHER="1", SNESRECOMP_RUN_FRAMES=str(stop + 3000),
           SNESRECOMP_DEBUG_PORT="9998")
proc = subprocess.Popen(["./UniracersSNESRecomp", "--rom", ROOT + "/Uniracers (USA).sfc",
                         "--no-launcher", "--script", route], cwd=BUILD, env=env,
                        stdout=subprocess.DEVNULL, stderr=open(out + ".stderr", "w"))
d = None
for _ in range(200):
    try: d = Dbg(port=9998); break
    except OSError: time.sleep(0.05)
if d is None: sys.exit("no debug server")
for a in addrs:
    for byte in (0, 1):
        d.cmd(f"set_wram_watch 7F {a + byte:04x} 1")
print("armed", len(addrs) * 2, "watches at frame", d.cmd("frame").get("frame"))
d.wait_frame(stop, timeout=600)
print("pause:", d.cmd("pause"))
res = {}
for a in addrs:
    for byte in (0, 1):
        r = d.cmd(f"wram_watch_log_get {a + byte:04x} 0 {stop} 64")
        res[f"7F:{a + byte:04X}"] = r.get("events", r)
json.dump(res, open(out, "w"), indent=1)
d.close(); proc.terminate(); proc.wait(timeout=20)
for k, ev in res.items():
    if not isinstance(ev, list): print(k, ev); continue
    frames = sorted({e["frame"] for e in ev})
    pcs = sorted({(e["pc24"], e["func_pc"]) for e in ev})
    print(f"{k}: {len(ev)} writes, frames {frames[:3]}..{frames[-1:]} writers(pc,func) {pcs[:4]}"
          if ev else f"{k}: no writes")
