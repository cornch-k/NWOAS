"""Additional actual-oracle multi-command and fatal-path comparisons."""
import test as t
import json
from pathlib import Path

def batch(commands):
 sq=t.c.sq[0];tail=sq.tail
 for b in commands:
  assert (tail+1)%sq.size!=sq.head
  t.write(sq.base+tail*64,b);tail=(tail+1)%sq.size
 t.c.write(0x1000,tail,32);t.lib.tail(tail);t.compare()

def fatal(off,value):
 t.c.write(off,value,32);(t.lib.tail if off==0x1000 else t.lib.ack)(value);t.compare()
 assert t.lib.inspect(7) and t.c.csts&2
 before=t.lib.inspect(11);t.c.write(0x1000,0,32);t.lib.tail(0);t.compare();assert t.lib.inspect(11)==before
 t.c.write(0x14,0,32);t.lib.reset_state(0);t.c.write(0x14,0x460001,32);assert t.lib.configure(t.SQ,8,t.CQ,8);t.compare()

for depth in [2,4,8]:
 t.setup(8,depth)
 cmds=[t.command(12,cid=1),t.command(12,cid=2),t.command(6,cid=3,dw0=1,prp1=0x40ffc,prp2=0x50000),t.command(2,cid=4,dw0=0x3ff0002,prp1=0x40000),t.command(8,cid=5),t.command(9,cid=6,dw0=6,dw1=0),t.command(8,cid=7)]
 batch(cmds)
 for _ in range(8):t.acknowledge();t.redrive()
t.setup(8,8)
batch([t.command(5,dw0=0x70001,dw1=3,prp1=0x60000),t.command(1,dw0=0x70001,dw1=0x10001,prp1=0x70000),t.command(0,dw0=1),t.command(4,dw0=1)])
for off,value in [(0x1000,8),(0x1004,8),(0x1004,1)]:
 t.setup(8,8);fatal(off,value)
# Read and pre-publication write failures have a matching Python failure point.
for kind,at in [('read',1),('write',1),('write',2),('write',3)]:
 t.setup(8,8);original=getattr(t.mem,kind);calls=[0]
 def fail(*args):
  calls[0]+=1
  if calls[0]==at:raise OSError('injected memory failure')
  return original(*args)
 b=t.command(6,dw0=1,prp1=0x40ffc,prp2=0x50000);t.write(t.SQ,b)
 setattr(t.mem,kind,fail);t.lib.inject(at if kind=='read' else 0,at if kind=='write' else 0,0,0)
 t.c.write(0x1000,1,32);t.lib.tail(1);t.compare();assert t.lib.inspect(7)
r={'pass_':True,'comparisons':t.checks,'scope':'Multi-command full-CQ/AER/PRP batches, create/delete batch, invalid doorbells, fetch and three logical publication failures against actual S139 oracle. Fake RAM only.'}
Path(__file__).with_name('extra-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
