#!/usr/bin/env python3
"""Wait for a live frame, then fetch historical WRAM/VRAM for an earlier frame."""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..",
                                "ws-native-map-spike", "scripts"))
from dbg import Dbg

port, live, hist, out, prefix = (int(sys.argv[1]), int(sys.argv[2]),
                                 int(sys.argv[3]), sys.argv[4], sys.argv[5])
os.makedirs(out, exist_ok=True)
d = Dbg(port)
at = d.wait_frame(live)
print("reached", at, flush=True)
for name, cmd in [
    ("wram", f"dump_frame_wram {hist} 0 131072"),
    ("vram", f"dump_frame_vram {hist} 0 65536"),
    ("cpu", "get_cpu_state"),
    ("dma", "get_dma_state"),
]:
    r = d.cmd(cmd)
    with open(os.path.join(out, f"{prefix}-{name}.json"), "w") as f:
        json.dump(r, f, indent=1, sort_keys=True)
    if "hex" in r:
        with open(os.path.join(out, f"{prefix}-{name}.bin"), "wb") as f:
            f.write(bytes.fromhex(r["hex"]))
        print(name, len(r["hex"]) // 2, flush=True)
    else:
        print(name, "json", flush=True)
d.close()
