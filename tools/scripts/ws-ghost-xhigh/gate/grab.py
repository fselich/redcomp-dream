# Pull the last ~6000 frames of rider/camera/OAM state from the user's running game.
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools/scripts/ws-native-map-spike"))
from dbg import Dbg
out = sys.argv[1]
d = Dbg(4377, timeout=120)
last = d.cmd("frame")["frame"]
first = max(0, last - 5990)
res = {"first": first, "last": last}
for addr, n, name in [(0x0411, 20, "pos"), (0x03ED, 2, "zoom"), (0x0D49, 2, "mask"),
                      (0x1501, 16, "oam"), (0x1599, 1, "hi"), (0x0DDB, 1, "ddb"),
                      (0x00B4, 1, "b4"), (0x0EE9, 1, "ee9"), (0x0300, 2, "s300")]:
    res[name] = d.cmd(f"wram_timeseries {addr:x} {n} {first} {last} 6000")
    print(name, len(res[name].get("entries", [])), res[name].get("truncated"), flush=True)
json.dump(res, open(out, "w"))
print("frames", first, last)
