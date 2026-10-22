#!/usr/bin/env python3
"""List every RNC method-1 blob in the ROM, unpack it, and classify the ones
that look like Uniracers track data: header $000D = cols/4 and $000E = rows/4
(0 means 256 in both) with cols*rows*2 == 0x8000, followed by the piece library.

usage: scan_rnc.py <rom.sfc> [out_dir]   (out_dir: also write each unpacked blob)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rnc

rom = open(sys.argv[1], 'rb').read()
out = sys.argv[2] if len(sys.argv) > 2 else None
known = {rnc.lorom(b, a): n for n, (b, a) in rnc.TRACKS.items()}
i = rom.find(b'RNC\x01')
n = 0
while i >= 0:
    bank, addr = 0x80 | (i // 0x8000), 0x8000 + i % 0x8000
    usize = int.from_bytes(rom[i + 4:i + 8], 'big'); psize = int.from_bytes(rom[i + 8:i + 12], 'big')
    try:
        data = rnc.unpack(rom, i); ok = len(data) == usize
    except Exception as e:
        data, ok = b'', False
    kind = '?'
    if ok and len(data) > 0x800F:
        cols = (data[0x0D] or 256) * 4; rows = (data[0x0E] or 256) * 4
        if cols * rows * 2 == 0x8000:
            kind = f"track {cols}x{rows} pieces={(len(data) - 0x800F) // 32}+{(len(data) - 0x800F) % 32}B"
    print(f'{n:3d} file {i:06X} snes ${bank & 0x7F:02X}:{addr:04X} unpacked {usize:6d} packed {psize:6d} '
          f'{"ok " if ok else "ERR"} {kind} {known.get(i, "")}')
    if out and ok:
        open(os.path.join(out, f'rnc-{i:06X}.bin'), 'wb').write(data)
    n += 1
    i = rom.find(b'RNC\x01', i + 4)
