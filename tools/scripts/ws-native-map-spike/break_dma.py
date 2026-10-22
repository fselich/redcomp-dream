#!/usr/bin/env python3
"""Pause at $81:B3F9, capture staging/VRAM, then block-step to the DMA.

Usage: break_dma.py <port> <target_frame> <out.json>
"""
import json
import sys
import time

from dbg import Dbg


def main():
    port = int(sys.argv[1])
    target = int(sys.argv[2])
    out = sys.argv[3]
    dbg = Dbg(port)
    dbg.wait_frame(max(0, target - 20))
    print("arm", dbg.cmd("break_add 81B3F9"), flush=True)
    hit = None
    deadline = time.time() + 90
    while time.time() < deadline:
        p = dbg.cmd("parked")
        if p.get("parked"):
            f = dbg.cmd("frame").get("frame")
            print("hit at", f, flush=True)
            snap = {
                "parked": p,
                "frame": f,
                "cpu": dbg.cmd("get_cpu_state"),
                "ppu": dbg.cmd("get_ppu_state"),
                "wram_433": dbg.cmd("read_ram 433 64"),
                "vram_1800": dbg.cmd("dump_vram 1800 800"),
                "stack": dbg.cmd("call_stack"),
            }
            if f == target:
                hit = snap
                break
            dbg.cmd("break_continue")
        else:
            f = dbg.cmd("frame").get("frame", -1)
            if f > target + 4:
                break
        time.sleep(0.01)
    if not hit:
        print("target breakpoint not reached", flush=True)
        with open(out, "w") as f:
            json.dump({"error": "target not reached"}, f)
        dbg.close()
        return 1

    result = {"at_b3f9": hit, "steps": []}
    print("clear breakpoint and step", dbg.cmd("break_clear"), flush=True)
    for i in range(40):
        dbg.cmd("step_block")
        time.sleep(0.02)
        p = dbg.cmd("parked")
        if not p.get("parked"):
            time.sleep(0.02)
            p = dbg.cmd("parked")
        snap = {
            "i": i,
            "frame": dbg.cmd("frame").get("frame"),
            "parked": p,
            "cpu": dbg.cmd("get_cpu_state"),
            "wram_433": dbg.cmd("read_ram 433 64"),
            "vram_1800": dbg.cmd("dump_vram 1800 800"),
        }
        result["steps"].append(snap)
        print("step", i, "parked", p.get("parked"), "frame", snap["frame"], flush=True)
        # stop once the BG1 map changes at the staging chunk area
        if i and snap["vram_1800"].get("hex") != result["steps"][0]["vram_1800"].get("hex"):
            print("vram changed at step", i, flush=True)
            break
    with open(out, "w") as f:
        json.dump(result, f)
    dbg.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
