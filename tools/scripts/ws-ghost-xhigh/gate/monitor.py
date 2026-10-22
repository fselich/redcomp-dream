import sys, time, socket
sys.path.insert(0, "<workshop>/work/ws-native-map-spike/scripts")
from dbg import Dbg
end = time.time() + 900
d = None
while d is None and time.time() < end:
    try: d = Dbg(4377, timeout=5)
    except OSError: time.sleep(1)
print("connected", flush=True)
last = None; t0 = time.time()
while time.time() < end:
    try:
        f = d.cmd("frame").get("frame")
        r = lambda a, n: d.cmd(f"read_ram {a:x} {n}")["hex"].replace(" ", "")
        cur = (r(0xB4, 1), r(0xEE9, 1), r(0x300, 2), r(0x3ED, 2), r(0x421, 4), r(0xDDB, 1))
    except Exception as ex:
        print("closed", ex, flush=True); break
    if cur != last:
        print(f"t={time.time()-t0:6.1f} f={f} b4={cur[0]} ee9={cur[1]} 300={cur[2]} zoom={cur[3]} cull={cur[4]} ddb={cur[5]}", flush=True)
        last = cur
    time.sleep(0.2)
