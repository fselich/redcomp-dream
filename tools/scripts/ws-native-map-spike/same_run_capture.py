#!/usr/bin/env python3
"""Capture pre-DMA at frame N and post-DMA at N+1 in one process."""
import json, sys, time
from dbg import Dbg
port=int(sys.argv[1]); target=int(sys.argv[2]); out=sys.argv[3]
d=Dbg(port); d.wait_frame(target-16); print('arm',d.cmd('break_add 81B3F9'),flush=True)
pre=None; post=None; seen=False; end=time.time()+100
while time.time()<end:
    p=d.cmd('parked')
    if p.get('parked'):
        f=d.cmd('frame').get('frame')
        snap={'frame':f,'cpu':d.cmd('get_cpu_state'),'w433':d.cmd('read_ram 433 64'),
              'vram':d.cmd('dump_vram 1800 2048'),'wram':d.cmd('dump_ram 0 131072'),
              'dmas':d.cmd(f'ppu_dmas frame={f} count=128')}
        if not seen and f==target:
            pre=snap; seen=True; print('pre saved',f,flush=True); d.cmd('break_continue')
        elif seen and f>=target+1:
            post=snap; print('post saved',f,flush=True); break
        else:
            d.cmd('break_continue')
    else:
        f=d.cmd('frame').get('frame',-1)
        if f>target+3: break
    time.sleep(0.005)
json.dump({'pre':pre,'post':post},open(out,'w'))
print('done',bool(pre),bool(post)); d.close()
