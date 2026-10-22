#!/usr/bin/env python3
"""Dump the two $7F level tables to JSON/ASCII for offline analysis."""
import json
import sys


def table(w, base, stride, rows, cols):
    out = []
    for r in range(rows):
        row = []
        for c in range(cols):
            a = (base + r * stride + c * 2) % 0x10000
            row.append(int.from_bytes(w[0x10000 + a:0x10000 + a + 2], "little"))
        out.append(row)
    return out


def main():
    wram, stride, cols, prefix = (sys.argv[1], int(sys.argv[2], 0),
                                  int(sys.argv[3], 0), sys.argv[4])
    w = open(wram, "rb").read()
    lo = table(w, 0x1000F, stride, 32, cols)
    hi = table(w, 0x1800F, stride, 32, cols)
    with open(prefix + ".json", "w") as f:
        json.dump({"stride": stride, "rows": 32, "cols": cols,
                   "low_base": "0x7F000F", "high_base": "0x7F800F",
                   "low": lo, "high": hi}, f)
    lines = []
    for r in range(32):
        s = []
        for c in range(cols):
            v = hi[r][c]
            if v == 0 and lo[r][c] == 0:
                s.append(".")
            elif v:
                s.append("#")
            else:
                s.append("x")
        lines.append("".join(s))
    with open(prefix + ".txt", "w") as f:
        f.write(f"stride=0x{stride:x} cols={cols} rows=32\n")
        f.write("\n".join(lines) + "\n")
    nz_lo = sum(1 for r in lo for v in r if v)
    nz_hi = sum(1 for r in hi for v in r if v)
    print(f"low nonzero {nz_lo}, high nonzero {nz_hi}, cells {32 * cols}")


if __name__ == "__main__":
    main()
