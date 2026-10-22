#!/usr/bin/env python3
"""Capture a historical frame from the Uniracers trace build.

Usage: capture_frame.py <port> <target_frame> <out_dir> [prefix]
Writes JSON responses plus decoded .bin payloads for WRAM/VRAM.
"""
import json
import os
import sys

from dbg import Dbg


def decode_hex(result, path):
    if "hex" in result:
        data = bytes.fromhex(result["hex"])
        with open(path, "wb") as f:
            f.write(data)
        return len(data)
    return 0


def save_json(result, path):
    with open(path, "w") as f:
        json.dump(result, f, indent=1, sort_keys=True)
        f.write("\n")


def main():
    port = int(sys.argv[1])
    target = int(sys.argv[2])
    out_dir = sys.argv[3]
    prefix = sys.argv[4] if len(sys.argv) > 4 else f"frame-{target}"
    os.makedirs(out_dir, exist_ok=True)
    dbg = Dbg(port)
    at = dbg.wait_frame(target)
    print(f"reached {at} (target {target})", flush=True)

    cmds = {
        "frame": "frame",
        "frame-extended": f"get_frame_range_extended {target} {target}",
        "ppu-state-live": "get_ppu_state",
        "cpu-state-live": "get_cpu_state",
        "dma-state-live": "get_dma_state",
        "oam-live": "dump_oam",
        "cgram-hist": f"dump_frame_cgram {target}",
        "wram-hist": f"dump_frame_wram {target} 0 131072",
        "vram-hist": f"dump_frame_vram {target} 0 65536",
    }
    for name, command in cmds.items():
        r = dbg.cmd(command)
        path = os.path.join(out_dir, f"{prefix}-{name}.json")
        save_json(r, path)
        if name == "wram-hist":
            print("wram bytes", decode_hex(r, os.path.join(out_dir, f"{prefix}-wram.bin")), flush=True)
        elif name == "vram-hist":
            print("vram bytes", decode_hex(r, os.path.join(out_dir, f"{prefix}-vram.bin")), flush=True)
        elif name == "oam-live":
            print("oam bytes", decode_hex(r, os.path.join(out_dir, f"{prefix}-oam.bin")), flush=True)
        elif name == "cgram-hist":
            print("cgram bytes", decode_hex(r, os.path.join(out_dir, f"{prefix}-cgram.bin")), flush=True)
        print(name, "ok", flush=True)
    dbg.close()


if __name__ == "__main__":
    main()
