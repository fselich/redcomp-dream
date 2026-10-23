#!/usr/bin/env python3
"""Uniracers SRAM (save.srm) tool. See docs/UNIRACERS_SAVE_FORMAT.md.

usage:
  srm.py dump   SAVE              decode the progress blocks
  srm.py verify SAVE [SAVE...]    check every block checksum (exit 1 on error)
  srm.py diff   A B               list differing bytes inside the known blocks
  srm.py set    IN OUT OFF=HEX... write a copy with raw bytes changed and the
                                  affected checksums recomputed
  srm.py unlock IN OUT            write a copy with every tour revealed
                                  ($10D3 raised to 3; nothing else changes)

The tool never writes to its input: OUT must be a different path, and an
input with a bad checksum is refused.
"""
import os
import sys

SIZE = 0x2000
MAGIC = b"ASJIver3.30"

# (name, start, end): the checksum is the 16-bit sum of the little-endian
# words in [start, end), stored as a word at `end`. Verified on a fresh
# save and two progressed saves.
BLOCKS = [
    ("names", 0x000C, 0x016C),     # rival names
    ("progress", 0x0230, 0x02B0),  # race counter and totals
    ("records", 0x02B2, 0x054E),   # per-track record times
    ("tracks", 0x0550, 0x05E6),    # 150-byte track table
    ("tours", 0x05E8, 0x073C),     # per-tour state
]

# Tour reveal stage, outside every checksummed block: 0 = the four starting
# tours, 1 = + Jumper/Bounder, 2 = + Runner/Sprinter, >= 3 = + Hunter.
TOUR_STAGE = 0x10D3
TOUR_STAGE_ALL = 3
LAST_LEVEL = 0x10D1  # 0 Bronze, 1 Silver, 2 Gold


def word(d, i):
    return d[i] | d[i + 1] << 8


def block_sum(d, start, end):
    return sum(word(d, i) for i in range(start, end, 2)) & 0xFFFF


def load(path):
    with open(path, "rb") as f:
        d = bytearray(f.read())
    if len(d) != SIZE:
        sys.exit(f"{path}: expected {SIZE} bytes, got {len(d)}")
    return d


def check(d):
    """[(name, stored, computed)] for every block."""
    return [(n, word(d, e), block_sum(d, s, e)) for n, s, e in BLOCKS]


def valid(d):
    return d[:len(MAGIC)] == MAGIC and all(a == b for _, a, b in check(d))


def fix_checksums(d):
    for _, s, e in BLOCKS:
        v = block_sum(d, s, e)
        d[e], d[e + 1] = v & 0xFF, v >> 8


def block_of(off):
    for n, s, e in BLOCKS:
        if s <= off < e + 2:
            return n
    return None


def cmd_verify(paths):
    bad = 0
    for p in paths:
        d = load(p)
        magic = d[:len(MAGIC)] == MAGIC
        res = check(d)
        ok = magic and all(a == b for _, a, b in res)
        bad |= not ok
        print(f"{p}: {'OK' if ok else 'BAD'}" + ("" if magic else " (no magic)"))
        for n, a, b in res:
            if a != b:
                print(f"  {n}: stored {a:04X} computed {b:04X}")
    return 1 if bad else 0


def cmd_dump(path):
    d = load(path)
    print(f"magic    {d[:len(MAGIC)].decode('latin-1')!r}")
    for n, a, b in check(d):
        print(f"block    {n:<9} checksum {a:04X} {'ok' if a == b else f'BAD (computed {b:04X})'}")
    print(f"counter  $0230 = {word(d, 0x0230)}   $0232 = {word(d, 0x0232)}"
          f"   $0236 = {word(d, 0x0236)}")
    print("tracks   $0550, groups of 5 ('#' = $00, '.' = $10, else hex):")
    for g in range(30):
        cells = []
        for t in range(5):
            b = d[0x0550 + g * 5 + t]
            cells.append("#" if b == 0 else "." if b == 0x10 else f"{b:02X}")
        print(f"  group {g:2d} ${0x0550 + g * 5:04X}  {' '.join(cells)}")
    flags = [d[0x069C + n * 0x20] for n in range(4)]
    print("medals   $069C+n*$20:", " ".join(f"{v:02X}" for v in flags))
    print(f"stage    ${TOUR_STAGE:04X} = {d[TOUR_STAGE]}   last level "
          f"${LAST_LEVEL:04X} = {d[LAST_LEVEL]}")
    return 0


def cmd_diff(pa, pb):
    a, b = load(pa), load(pb)
    for i in range(SIZE):
        if a[i] != b[i]:
            print(f"${i:04X} {a[i]:02X} -> {b[i]:02X}  {block_of(i) or '-'}")
    return 0


def load_for_edit(src, dst):
    if os.path.abspath(src) == os.path.abspath(dst):
        sys.exit("srm.py: OUT must differ from IN")
    d = load(src)
    if not valid(d):
        sys.exit(f"{src}: invalid save (magic or checksum); refusing")
    return d


def cmd_unlock(src, dst):
    d = load_for_edit(src, dst)
    if d[TOUR_STAGE] < TOUR_STAGE_ALL:
        d[TOUR_STAGE] = TOUR_STAGE_ALL
    with open(dst, "wb") as f:
        f.write(d)
    return 0


def cmd_set(src, dst, edits):
    d = load_for_edit(src, dst)
    for e in edits:
        off, _, hexbytes = e.partition("=")
        off = int(off, 16)
        data = bytes.fromhex(hexbytes)
        for i, v in enumerate(data):
            if block_of(off + i) is None:
                sys.exit(f"srm.py: ${off + i:04X} is outside the known blocks")
            d[off + i] = v
    fix_checksums(d)
    with open(dst, "wb") as f:
        f.write(d)
    return 0


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    cmd, args = argv[1], argv[2:]
    if cmd == "verify" and args:
        return cmd_verify(args)
    if cmd == "dump" and len(args) == 1:
        return cmd_dump(args[0])
    if cmd == "diff" and len(args) == 2:
        return cmd_diff(*args)
    if cmd == "unlock" and len(args) == 2:
        return cmd_unlock(*args)
    if cmd == "set" and len(args) >= 2:
        return cmd_set(args[0], args[1], args[2:])
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
