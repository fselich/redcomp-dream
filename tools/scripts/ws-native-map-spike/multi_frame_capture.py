#!/usr/bin/env python3
"""Capture stage/DMA/VRAM at $B3F9 for a range of frames in one run."""
import json, sys, time
from dbg import Dbg
port=int(sys.argv[1]); first=int(sys.argv[2]); last=int(sys.argv[3]); out=sys.argv[4]
d=Dbg(port); d.wait_frame(first-16); d.cmd('break_add 81B3F9')
frames=[]; end=time.time()+100
while time.time()<end:
    p=d.cmd('parked')
    if p.get('parked'):
        f=d.cmd('frame').get('frame')
        if first <= f <= last:
            snap={'frame':f,'w433':d.cmd('read_ram 433 64'),
                  'vram':d.cmd('dump_vram 1800 2048'),
                  'dmas':d.cmd(f'ppu_dmas frame={f} count=128')}
            frames.append(snap); print('saved',f,flush=True)
        if f>last: break
        d.cmd('break_continue')
    else:
        f=d.cmd('frame').get('frame',-1)
        if f>last+2: break
    time.sleep(0.005)
json.dump(frames,open(out,'w')); print('done',len(frames)); d.close()
