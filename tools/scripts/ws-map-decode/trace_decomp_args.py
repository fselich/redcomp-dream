#!/usr/bin/env python3
"""Capture the arguments of every call to the $81:B8F1 decompressor during a
track load (map-decode spike, task 4.2): the entry stores dest -> $86, source
bank -> $84, source address -> $82 (before the +0x11 header skip). Watches the
direct-page words $7E:0082/0084/0086 and pairs the writes made at the entry PCs
($81:B8F5/B8F9/B8FD).

usage: trace_decomp_args.py <route.txt> <from_frame> <stop_frame> <out.json>

Known sources (LoROM, all dest $7F:0000): Dragster $18:8000, Zoom Zoo $18:8183,
Bowl $18:9B4A, Switcher $18:A07E, Monster $18:C6DA (see work/ws-map-decode/notes/findings.md).
"""
import json, os, subprocess, sys, time
from pathlib import Path
_REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(_REPO / "tools/scripts/ws-native-map-spike"))
from dbg import Dbg
ROOT = os.environ.get("UNIRACERS_ROOT", str(_REPO))
BUILD = os.environ.get("UNIRACERS_BUILD", ROOT + "/game/build-debug")
route, f0, stop, out = os.path.abspath(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
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
for a in (0x82, 0x84, 0x86):
    d.cmd(f"set_wram_watch 7E {a:04x} 2")
d.wait_frame(stop, timeout=900)
ev = {}
for a in (0x82, 0x84, 0x86):
    # the log returns at most 1024 events oldest-first: query one frame at a time
    r = {"events": []}
    for fr in range(f0, stop + 1):
        r["events"] += d.cmd(f"wram_watch_log_get {a:04x} {fr} {fr} 1024").get("events", [])
    ev[a] = r.get("events", [])
json.dump(ev, open(out + ".raw.json", "w"), indent=1)
ev = {a: [e for e in v if e["pc24"] in ("0x81B8F5", "0x81B8F9", "0x81B8FD")] for a, v in ev.items()}
d.close(); proc.terminate(); proc.wait(timeout=20)
calls = []
for e82 in ev[0x82]:
    idx = e82["idx"]
    e84 = max((e for e in ev[0x84] if e["idx"] < idx), key=lambda e: e["idx"], default=None)
    e86 = max((e for e in ev[0x86] if e["idx"] < idx), key=lambda e: e["idx"], default=None)
    calls.append({"frame": e82["frame"], "src_addr": e82["A"],
                  "src_bank": e84 and e84["A"], "dest": e86 and e86["A"]})
json.dump(calls, open(out, "w"), indent=1)
for c in calls: print(c)
