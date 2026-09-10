"""Actual S139 ring/command oracle, fake RAM; no hardware or external I/O."""
from pathlib import Path
import ctypes as C,importlib.util,struct,json,random,sys
S=Path(__file__).resolve().parent
sys.path[:0]=[str(S.parent/'nvme-s124'),str(S.parent/'transport-s123')]
from readonly_namespace import ReadOnlyNamespace
from writable_namespace import WindowWritableNamespace
from transport import NamespacePair
spec=importlib.util.spec_from_file_location('s227_ref',S.parent/'nvme-s139/controller.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
lib=C.CDLL(str(S/'out/ring.dylib'));lib.configure.argtypes=[C.c_uint64,C.c_uint,C.c_uint64,C.c_uint];lib.guest_write.argtypes=[C.c_uint64,C.c_void_p,C.c_uint];lib.memory_view.restype=C.c_void_p;lib.inspect.argtypes=[C.c_uint];lib.inspect.restype=C.c_uint64
BASE=0x10000;SIZE=0x90000;SQ=0x10000;CQ=0x20000
class Mem:
 def __init__(self):self.data=bytearray(SIZE);self.reads=0
 def contains(self,a,n):return BASE<=a<=BASE+SIZE and 0<n<=BASE+SIZE-a
 def read(self,a,n):
  assert self.contains(a,n);self.reads+=1;return bytes(self.data[a-BASE:a-BASE+n])
 def write(self,a,b):
  assert self.contains(a,len(b));self.data[a-BASE:a-BASE+len(b)]=b
class NS(NamespacePair):
 def set_cache(self,v):
  global sets
  sets+=1;self.primary.set_cache(v)
 def get_cache(self):return self.primary.get_cache()
 def flush(self):self.primary.flush()
actual_aers=0;checks=0;boots=0;commands=0;acks=0;full_cq=0;wraps=0;deferred=0

def setup(ns=8,nc=8):
 global c,mem,primary,sets,flushes,flush_fail,irq,boots
 sets=flushes=0;flush_fail=False;irq=False;mem=Mem()
 def flush():
  global flushes
  flushes+=1
  if flush_fail:raise OSError('fake cache flush')
 def interrupt(v):
  global irq
  irq=bool(v)
 primary=WindowWritableNamespace(61279344,lambda *a:None,lambda *a:None,flush,53839104,59968629);primary.mdts=8
 c=ref.Controller(NS(primary,ReadOnlyNamespace(8448,lambda *a:None)),mem,interrupt)
 original_admin=c.admin
 def traced_admin(cmd):
  global actual_aers
  result=original_admin(cmd)
  if result is None:actual_aers+=1
  return result
 c.admin=traced_admin
 assert lib.initialize();assert lib.configure(SQ,ns,CQ,nc)
 c.write(0x24,(ns-1)|((nc-1)<<16),32);c.write(0x28,SQ,64);c.write(0x30,CQ,64);c.write(0x14,0x460001,32);boots+=1

def compare():
 global checks
 sq=c.sq[0];cq=c.cq[0]
 want=[sq.head,sq.tail,cq.head,cq.tail,cq.pending,cq.phase,1,bool(c.csts&2)]
 assert [lib.inspect(i) for i in range(8)]==want,(checks,'ring',[lib.inspect(i) for i in range(8)],want)
 assert lib.inspect(9)==irq
 assert (lib.inspect(15),lib.inspect(16))==(bool(c.aer),c.aer[0] if c.aer else 0)
 assert [lib.inspect(i) for i in [17,18,19]]==[primary.get_cache(),flushes,sets]
 sq=c.sq.get(1);cq=c.cq.get(1);want=[bool(sq),sq.base if sq else 0,sq.size if sq else 0,sq.cqid if sq else 0,bool(cq),cq.base if cq else 0,cq.size if cq else 0,cq.ien if cq else 0]
 assert [lib.inspect(i) for i in range(20,28)]==want
 assert [lib.inspect(32+i) for i in range(12)]==[c.features.get(i,0) for i in range(12)]
 got=C.string_at(lib.memory_view(),SIZE)
 assert got==mem.data,(checks,'RAM mismatch',next(i for i,(a,b) in enumerate(zip(got,mem.data)) if a!=b))
 checks+=1

def write(a,b):mem.write(a,b);assert lib.guest_write(a,b,len(b))
def command(op,cid=1,ns=0,dw0=0,dw1=0,prp1=0,prp2=0):
 b=bytearray(64);b[0]=op;struct.pack_into('<H',b,2,cid);struct.pack_into('<I',b,4,ns);struct.pack_into('<QQ',b,24,prp1,prp2);struct.pack_into('<II',b,40,dw0,dw1);return bytes(b)
def submit(cmd):
 global commands,full_cq,wraps,deferred
 sq=c.sq[0];cq=c.cq[0]
 if (sq.tail+1)%sq.size==sq.head:return False
 old=cq.phase;before=mem.reads
 write(sq.base+sq.tail*64,cmd);tail=(sq.tail+1)%sq.size
 if cq.pending==cq.size-1:full_cq+=1
 c.write(0x1000,tail,32);lib.tail(tail);commands+=1;wraps+=old!=cq.phase
 deferred+=cmd[0]==12 and bool(c.aer);compare();return True

def acknowledge(count=None):
 global acks
 cq=c.cq[0];n=cq.pending if count is None else count;head=(cq.head+n)%cq.size
 before=lib.inspect(11);before_py=mem.reads;c.write(0x1004,head,32);lib.ack(head)
 assert lib.inspect(11)==before and mem.reads==before_py,'CQ ack fetched commands'
 acks+=1;compare()

def redrive():
 tail=c.sq[0].tail;c.write(0x1000,tail,32);lib.tail(tail);compare()

if __name__=='__main__':
 # PRP payload lengths/boundaries, all bytes and phase wrap compared to Python.
 for ns,nc in [(2,2),(4,2),(2,4),(8,8),(256,256)]:
  setup(ns,nc)
  for a,b in [(0x40000,0),(0x40ffc,0x50000),(0x40ffc,0),(0x40002,0),(0,0),(0x9fffc,0x100000),(0xfffffffffffffffC,0x50000)]:
   for cmd in [command(6,ns=1,dw0=0,prp1=a,prp2=b),command(6,dw0=1,prp1=a,prp2=b),command(2,dw0=0x3ff0002,prp1=a,prp2=b)]:
    if not submit(cmd):acknowledge();redrive();assert submit(cmd)
    acknowledge();redrive()
 # CQ full, AER without completion, no DPC work on ack, reset then reuse.
 setup(8,2)
 for op in [12,12,8,8]:submit(command(op))
 assert lib.inspect(4)==1;before=lib.inspect(11);acknowledge();assert lib.inspect(11)==before;redrive();acknowledge();redrive()
 for depth in [2,4,8,16,256]:
  for seed in [227,228,229]:
   setup(depth,depth);rng=random.Random(seed+depth)
   for j in range(700):
    if rng.randrange(12)==0:
     flush_fail=bool(rng.getrandbits(1));lib.inject(0,0,0,flush_fail)
    if rng.randrange(5)==0:
     acknowledge(rng.randrange(c.cq[0].pending+1));continue
    if rng.randrange(30)==0:
     c.write(0x14,0,32);lib.reset_state(0);assert not lib.inspect(6) and not lib.inspect(15)
     c.write(0x14,0x460001,32);assert lib.configure(SQ,depth,CQ,depth);compare();continue
    op=rng.choice([0,1,2,4,5,6,8,9,10,12,255]);a=0;b=0;ns=0;dw0=0;dw1=0
    if op in [1,5]:a=rng.choice([0x60000,0x70000,0,0x60004]);dw0=((rng.choice([2,8,256])-1)<<16)|1;dw1=3 if op==5 else 0x10001
    elif op in [0,4]:dw0=1
    elif op in [9,10]:dw0=rng.choice([1,6,7,0x106,0x306,255]);dw1=rng.choice([0,1,2,0xffffffff])
    elif op==6:ns=rng.choice([0,1,2]);dw0=rng.choice([0,1,2,3]);a=rng.choice([0x40000,0x40ffc,0,0x40001]);b=0x50000
    elif op==2:dw0=rng.choice([1,2,3,0x3ff0002,0x4000002]);a=0x40ffc;b=rng.choice([0x50000,0,0x50004])
    if not submit(command(op,j,ns,dw0,dw1,a,b)):acknowledge();redrive()
   acknowledge();redrive();acknowledge()
 r={'pass_':True,'actual_held_aers':actual_aers,'state_comparisons':checks,'submitted_commands':commands,'configurations':boots,'cq_acknowledgements_without_fetch':acks,'full_cq_submissions':full_cq,'observed_phase_wraps':wraps,'scope':'Actual S139 Controller memory bytes and ring/admin state; fake RAM/callbacks only. No hardware.'}
 (S/'differential-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
