#!/usr/bin/env python3
"""Break at a PC and capture CPU/stack state at a target frame."""
import json, sys, time
from dbg import Dbg
port=int(sys.argv[1]); pc=sys.argv[2]; target=int(sys.argv[3]); out=sys.argv[4]
d=Dbg(port); d.wait_frame(target-12); print('arm',d.cmd(f'break_add {pc}'),flush=True)
end=time.time()+100
while time.time()<end:
 p=d.cmd('parked')
 if p.get('parked'):
  f=d.cmd('frame').get('frame')
  if f==target:
   res={'pc':pc,'frame':f,'parked':p,'cpu':d.cmd('get_cpu_state'),
        'stackmem':d.cmd('read_ram 1c0 64'),'wram':d.cmd('dump_ram 0 131072')}
   json.dump(res,open(out,'w')); print('saved',res['cpu'],flush=True); d.close(); sys.exit(0)
  d.cmd('break_continue')
 else:
  f=d.cmd('frame').get('frame',-1)
  if f>target+2: break
 time.sleep(0.005)
print('not reached'); d.close(); sys.exit(1)
