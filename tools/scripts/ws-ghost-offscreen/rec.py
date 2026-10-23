import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts/ws-native-map-spike"))
from dbg import Dbg
port, first, last, out = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
d = Dbg(port, timeout=120)
d.wait_frame(last, timeout=900)
res = {}
for addr, n, name in [(0x0411, 20, "pos"), (0x03ED, 2, "zoom"), (0x0D49, 2, "mask"),
                      (0x1501, 16, "oam"), (0x1599, 1, "hi"), (0x0DDB, 1, "ddb"),
                      (0x0C67, 2, "c67"), (0x0BA1, 4, "ba1")]:
    r = d.cmd(f"wram_timeseries {addr:x} {n} {first} {last} 6000")
    res[name] = r
    print(name, len(r.get("entries", [])), str(r)[:200], flush=True)
json.dump(res, open(out, "w"))
d.close()
