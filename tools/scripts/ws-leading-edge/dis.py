#!/usr/bin/env python3
"""Linear 65816 disassembly with REP/SEP tracking.
usage: dis.py ROM BANK:ADDR COUNT [m x]"""
import sys
from pathlib import Path
_REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, sys.argv[5] if len(sys.argv) > 5 and '/' in sys.argv[5] else
    str(_REPO / "game/snesrecomp/recompiler"))
import snes65816 as s
rom = s.load_rom(sys.argv[1])
bank, addr = (int(v, 16) for v in sys.argv[2].split(':'))
n = int(sys.argv[3])
m = x = 0
if len(sys.argv) > 4 and sys.argv[4] in ('mx', 'm', 'x', '-'):
    m = 1 if 'm' in sys.argv[4] else 0
    x = 1 if 'x' in sys.argv[4] else 0
for _ in range(n):
    off = s.rom_offset(bank, addr)
    i = s.decode_insn(rom, off, addr, bank, m, x)
    if not i:
        print(f"{bank:02X}:{addr:04X}  db ${rom[off]:02X}"); addr += 1; continue
    raw = rom[off:off + i.length].hex()
    print(f"{bank:02X}:{addr:04X}  {raw:<10} {i.mnem:<5} mode={i.mode} op=${i.operand:X}  [m{m}x{x}]")
    if i.mnem == 'REP':
        if i.operand & 0x20: m = 0
        if i.operand & 0x10: x = 0
    if i.mnem == 'SEP':
        if i.operand & 0x20: m = 1
        if i.operand & 0x10: x = 1
    addr += i.length
