import json, sys
d = json.load(open(sys.argv[1])); step = int(sys.argv[2]) if len(sys.argv) > 2 else 1
def series(name):
    e = d[name]["entries"]; out = {}; cur = None; i = 0
    for f in range(d[name]["from"], d[name]["to"] + 1):
        while i < len(e) and e[i]["f"] <= f: cur = bytes.fromhex(e[i]["hex"]); i += 1
        out[f] = cur
    return out
S = {k: series(k) for k in ("pos", "zoom", "mask", "oam", "hi")}
w = lambda b, o: int.from_bytes(b[o:o+2], "little")
s16 = lambda v: v - 65536 if v >= 32768 else v
prev = None
print("frame  pX    gX    camX  gX-cam  gYscr | oam99 x  y  hi99 dec | zoom mask cullL cullR")
for f in sorted(S["pos"]):
    p = S["pos"][f]; o = S["oam"][f]; hi = S["hi"][f][0]
    pX, gX, pY, gY, cX, cY = w(p,0), w(p,2), w(p,4), w(p,6), w(p,8), w(p,12)
    cl, cr = s16(w(p,16)), s16(w(p,18))
    z = w(S["zoom"][f],0)
    rel = s16((gX - cX) & 0xffff) << z
    ox, oy = o[12], o[13]; xh = (hi >> 6) & 1
    dec = ox | (xh << 8); dec = dec - 512 if dec >= 256 + 43 else dec
    row = (rel, ox, oy, xh)
    if f % step == 0 or (prev and (abs(rel - prev[0]) > 40 or (oy == 0x70) != (prev[2] == 0x70) or xh != prev[3])):
        print(f"{f:5d} {pX:5d} {gX:5d} {cX:5d} {rel:6d} {s16((gY-cY)&0xffff):5d} | {ox:3d} {oy:3d} {xh} {dec:5d} | {z} {w(S['mask'][f],0):04x} {cl} {cr}")
    prev = row
