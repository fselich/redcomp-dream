#!/usr/bin/env python3
"""Dump the ROM table that lists every track blob (map-decode spike, task 4.4).

Table at file 0x0136F9 (SNES $82:B6F9), 5-byte entries:
  bank (u8, with $80 bit) | address (u16 LE) | blob length incl. 18-byte RNC header (u16 LE)
Entry index = the game's track/blob number (to be confirmed against the caller).

usage: track_table.py <rom.sfc>
"""
import sys
rom = open(sys.argv[1], 'rb').read()
T = 0x0136F9
names = {0: 'Dragster', 1: 'Zoom Zoo', 2: 'Bowl', 3: 'Switcher', 4: 'Monster'}  # from load traces
i, n = T, 0
while True:
    bank, addr, ln = rom[i], rom[i + 1] | rom[i + 2] << 8, rom[i + 3] | rom[i + 4] << 8
    if not (0x98 <= bank <= 0x9F and addr >= 0x8000):
        print(f"end at file {i:06X}: {rom[i:i + 12].hex(' ')}")
        break
    off = (bank & 0x7F) * 0x8000 + addr - 0x8000
    ok = rom[off:off + 4] == b'RNC\x01'
    plen = int.from_bytes(rom[off + 8:off + 12], 'big') + 18 if ok else -1
    print(f"{n:2d} file {i:06X}  ${bank:02X}:{addr:04X}  len {ln:5d}  RNC={'ok' if ok else 'NO'} "
          f"len==hdr+18:{'yes' if plen == ln else 'no'}  {names.get(n, '')}")
    i += 5; n += 1
