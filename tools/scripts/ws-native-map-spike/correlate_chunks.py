#!/usr/bin/env python3
"""Correlate stage chunks, DMA destinations and map rows from a capture JSON."""
import json
import sys


def be(v, x, y):
    p = 2 * (y * 32 + x)
    return int.from_bytes(v[p:p + 2], "big")


def main():
    d = json.load(open(sys.argv[1]))
    for fr in d["frames"]:
        f = fr["f"]
        if not fr.get("stage") or not fr.get("map"):
            continue
        w = bytes.fromhex(fr["stage"])
        v = bytes.fromhex(fr["map"])
        stage = [int.from_bytes(w[0x33 + 2 * i:0x35 + 2 * i], "big") for i in range(17)]
        dm = [x for x in fr["dmas"].get("dmas", [])
              if x["src"].lower().endswith(":0433")]
        dest = dm[0]["dest"] if dm else None
        match = [(y, x0) for y in range(32) for x0 in range(16)
                 if all(be(v, x0 + i, y) == stage[i] for i in range(17))]
        print(f"{f}: dest={dest} match={match[:2]}")


if __name__ == "__main__":
    main()
