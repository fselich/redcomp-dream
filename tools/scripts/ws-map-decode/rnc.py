#!/usr/bin/env python3
"""Rob Northen Compression (ProPack) method 1 unpacker, plus Uniracers track
extraction (map-decode spike, task 4.3).

The track data loaded into WRAM $7F:0000 at race load is RNC1-packed in ROM
bank $18 (LoROM) and unpacked by the game's routine at $81:B8F1. Algorithm
follows the public-domain dernc.c (Simon Tatham): LSB-first 16-bit bit stream,
per chunk three Huffman tables (literal-run length, distance, match length),
literal runs copied straight from the byte stream.

Header (18 bytes, big-endian): "RNC", method, unpacked size (4), packed size (4),
unpacked CRC (2), packed CRC (2), leeway (1), chunk count (1).

usage:
  rnc.py unpack <rom.sfc> <snes_bank:addr> <out.bin>
  rnc.py track  <rom.sfc> <track> <out.bin>       (dragster|zoomzoo|bowl|switcher|monster)
  rnc.py verify <rom.sfc> <track> <wram.bin>      (compare with a WRAM dump's $7F)
"""
import sys

TRACKS = {  # $81:B8F1 call arguments captured at load (dest always $7F:0000)
    "dragster": (0x18, 0x8000), "zoomzoo": (0x18, 0x8183), "bowl": (0x18, 0x9B4A),
    "switcher": (0x18, 0xA07E), "monster": (0x18, 0xC6DA),
}


def lorom(bank, addr):
    return (bank & 0x7F) * 0x8000 + (addr - 0x8000)


class Bits:
    def __init__(self, data, pos):
        self.d, self.p = data, pos
        self.buf, self.cnt = self.lword(pos), 16

    def lword(self, p):
        b = self.d[p:p + 4] + bytes(4)
        return b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24

    def peek(self, mask):
        return self.buf & mask

    def advance(self, n):
        self.buf >>= n
        self.cnt -= n
        if self.cnt < 16:
            self.p += 2
            self.buf = (self.buf | (self.lword(self.p) << self.cnt)) & 0xFFFFFFFF
            self.cnt += 16

    def read(self, mask, n):
        v = self.peek(mask)
        self.advance(n)
        return v

    def fix(self):
        self.cnt -= 16
        self.buf &= (1 << self.cnt) - 1
        self.buf = (self.buf | (self.lword(self.p) << self.cnt)) & 0xFFFFFFFF
        self.cnt += 16


def mirror(x, n):
    r = 0
    for _ in range(n):
        r = (r << 1) | (x & 1)
        x >>= 1
    return r


def read_table(bs, table):
    num = bs.read(0x1F, 5)
    if not num:
        return table
    lens = [bs.read(0x0F, 4) for _ in range(num)]
    table, code = [], 0
    for L in range(1, max(lens + [1]) + 1):
        for v, l in enumerate(lens):
            if l == L:
                table.append((mirror(code, L), L, v))
                code += 1
        code <<= 1
    return table


def huf(bs, table):
    for code, L, v in table:
        if bs.peek((1 << L) - 1) == code:
            bs.advance(L)
            if v >= 2:
                base = 1 << (v - 1)
                v = base | bs.read(base - 1, v - 1)
            return v
    raise ValueError("RNC huffman decode error at %#x" % bs.p)


def unpack(data, off=0):
    if data[off:off + 3] != b"RNC" or data[off + 3] != 1:
        raise ValueError("not RNC method 1")
    usize = int.from_bytes(data[off + 4:off + 8], "big")
    out = bytearray()
    bs = Bits(data, off + 18)
    bs.advance(2)
    raw = dist = ln = []
    while len(out) < usize:
        raw, dist, ln = read_table(bs, raw), read_table(bs, dist), read_table(bs, ln)
        count = bs.read(0xFFFF, 16)
        while True:
            n = huf(bs, raw)
            if n:
                out += data[bs.p:bs.p + n]
                bs.p += n
                bs.fix()
            count -= 1
            if count <= 0:
                break
            posn = huf(bs, dist) + 1
            n = huf(bs, ln) + 2
            for _ in range(n):
                out.append(out[-posn])
    return bytes(out[:usize])


def track(rom, name):
    b, a = TRACKS[name]
    return unpack(rom, lorom(b, a))


if __name__ == "__main__":
    cmd, rom = sys.argv[1], open(sys.argv[2], "rb").read()
    if cmd == "unpack":
        b, a = (int(x, 16) for x in sys.argv[3].split(":"))
        res = unpack(rom, lorom(b, a))
        open(sys.argv[4], "wb").write(res)
        print(len(res), "bytes")
    elif cmd == "track":
        res = track(rom, sys.argv[3])
        open(sys.argv[4], "wb").write(res)
        print(len(res), "bytes")
    elif cmd == "verify":
        res = track(rom, sys.argv[3])
        w7f = open(sys.argv[4], "rb").read()[0x10000:0x10000 + len(res)]
        diff = sum(1 for x, y in zip(res, w7f) if x != y)
        print(f"{sys.argv[3]}: unpacked {len(res)} bytes, differing from WRAM $7F: {diff}")
