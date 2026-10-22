#!/usr/bin/env python3
"""Pause at $81:B3F9 at a target frame and save the exact pre-DMA state."""
import json, sys, time
from dbg import Dbg
port=int(sys.argv[1]); target=int(sys.argv[2]); out=sys.argv[3]
d=Dbg(port); d.wait_frame(target-16); print('arm',d.cmd('break_add 81B3F9'),flush=True)
end=time.time()+100
while time.time()<end:
 p=d.cmd('parked')
 if p.get('parked'):
  f=d.cmd('frame').get('frame')
  if f==target:
   res={'frame':f,'parked':p,'cpu':d.cmd('get_cpu_state'),'ppu':d.cmd('get_ppu_state'),'stack':d.cmd('call_stack'),'w433':d.cmd('read_ram 433 64'),'full_wram':d.cmd('dump_ram 0 131072'),'vram':d.cmd('dump_vram 1800 2048'),'dmas':d.cmd(f'ppu_dmas frame={f} count=128')}
   json.dump(res,open(out,'w')); print('saved'); d.close(); sys.exit(0)
  d.cmd('break_continue')
 else:
  f=d.cmd('frame').get('frame',-1)
  if f>target+2: break
 time.sleep(0.005)
print('not reached'); d.close(); sys.exit(1)
