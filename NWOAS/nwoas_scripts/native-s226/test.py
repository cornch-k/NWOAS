"""Stateful differential admin-command tests against actual S139 Python owner."""
from pathlib import Path
import ctypes as C,importlib.util,json,random,struct,sys
S=Path(__file__).resolve().parent;sys.path.insert(0,str(S.parent/'nvme-s124'));sys.path.insert(0,str(S.parent/'transport-s123'))
from readonly_namespace import ReadOnlyNamespace
from writable_namespace import WindowWritableNamespace
from transport import NamespacePair
spec=importlib.util.spec_from_file_location('s226_ref',S.parent/'nvme-s139/controller.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
class Result(C.Structure):_fields_=[('result',C.c_uint32),('bytes',C.c_uint32),('status',C.c_uint16),('changed',C.c_uint8),('deferred',C.c_bool)]
lib=C.CDLL(str(S/'out/admin.dylib'));lib.execute.argtypes=[C.c_void_p,C.c_void_p,C.POINTER(Result)];lib.inspect.argtypes=[C.c_uint];lib.inspect.restype=C.c_uint64;lib.reset_state.argtypes=[C.c_int];lib.fail_flush.argtypes=[C.c_int]
class Mem:
 def contains(self,a,n):return 0x10000<=a<=0x100000 and n<=0x100000-a
class NS(NamespacePair):
 def set_cache(self,v):
  global set_calls
  set_calls+=1;self.primary.set_cache(v)
 def get_cache(self):return self.primary.get_cache()
 def flush(self):self.primary.flush()
flushes=0;set_calls=0;failure=False

def flush():
 global flushes
 flushes+=1
 if failure:raise OSError('injected flush failure')
primary=WindowWritableNamespace(61279344,lambda *a:None,lambda *a:None,flush,53839104,59968629);primary.mdts=8
c=ref.Controller(NS(primary,ReadOnlyNamespace(8448,lambda *a:None)),Mem(),lambda x:None);lib.initialize();checks=0;deferred=0;generations=[0,0]

def state():
 sq=c.sq.get(1);cq=c.cq.get(1)
 want=[int(sq is not None),sq.base if sq else 0,sq.size if sq else 0,sq.cqid if sq else 0,int(cq is not None),cq.base if cq else 0,cq.size if cq else 0,int(cq.ien) if cq else 0,int(bool(c.aer)),c.aer[0] if c.aer else 0,primary.get_cache(),flushes,*generations,set_calls]
 assert [lib.inspect(i) for i in range(15)]==want,(checks,[lib.inspect(i) for i in range(15)],want)
 assert [lib.inspect(32+i) for i in range(12)]==[c.features.get(i,0) for i in range(12)]

def check(cmd):
 global checks,deferred
 before=(c.sq.get(1),c.cq.get(1));want=c.admin(cmd);after=(c.sq.get(1),c.cq.get(1));changed=sum((int(a is not b)<<i) for i,(a,b) in enumerate(zip(before,after)))
 if cmd[0]==9 and (struct.unpack_from('<I',cmd,40)[0]&255)==6 and want is not None and want.status==0:changed|=4
 for i in range(2):generations[i]+=bool(changed&(1<<i))
 out=C.create_string_buffer(b'\xa5'*4096,4096);r=Result();assert lib.execute(cmd,out,C.byref(r))==1
 assert r.changed==changed,(checks,cmd.hex(),'changed',r.changed,changed)
 if want is None:assert r.deferred and not r.bytes;deferred+=1
 else:
  assert not r.deferred and (r.status,r.result,r.bytes)==(want.status,want.result,len(want.data)),(checks,cmd.hex(),r.status,r.result,r.bytes,want)
  assert out.raw[:r.bytes]==want.data
 if not r.bytes:assert out.raw==b'\xa5'*4096
 checks+=1;state()

def cmd(op,dw0=0,dw1=0,base=0,ns=0,cid=1):
 b=bytearray(64);b[0]=op;struct.pack_into('<H',b,2,cid);struct.pack_into('<I',b,4,ns);struct.pack_into('<Q',b,24,base);struct.pack_into('<II',b,40,dw0,dw1);return bytes(b)

def reset(full):
 before=(1 in c.sq,1 in c.cq)
 if full:c.reset()
 else:c.write(0x14,0,32)
 for i,v in enumerate(before):generations[i]+=v
 lib.reset_state(full);state()
check(cmd(5,0x00ff0001,0,0x30000));check(cmd(1,0x00ff0001,0x20001,0x40000))
for depth in [2,256]:
 check(cmd(5,((depth-1)<<16)|1,3,0x30000));check(cmd(1,((depth-1)<<16)|1,0x10001,0x40000));check(cmd(4,1));check(cmd(0,1));check(cmd(4,1))
check(cmd(12,cid=123));check(cmd(12,cid=124));check(cmd(8));reset(False)
for sel in range(8):check(cmd(10,6|(sel<<8)))
check(cmd(9,6,0));check(cmd(9,6,1));failure=True;lib.fail_flush(1);check(cmd(9,6,0));failure=False;lib.fail_flush(0)
rng=random.Random(226)
for _ in range(12000):
 if rng.randrange(20)==0:
  failure=bool(rng.getrandbits(1));lib.fail_flush(failure)
 if rng.randrange(40)==0:reset(bool(rng.getrandbits(1)));continue
 op=rng.choice([0,1,2,4,5,6,8,9,10,12,0xff]);dw0=0;dw1=0;base=0
 if op in [1,5]:dw0=((rng.choice([1,2,16,256,257,65536])-1)<<16)|rng.choice([0,1,1,2]);dw1=rng.choice([1,3,0x10001,0x10003]);base=rng.choice([0,0x30000,0x40000,0x30001,0xfffffffffffff000])
 elif op in [0,4]:dw0=rng.choice([0,1,1,2])
 elif op in [9,10]:dw0=rng.choice([0,1,2,4,5,6,7,8,9,10,11,12,0xc,0x7f,0x106,0x306]);dw1=rng.choice([0,1,2,0xffffffff])
 elif op==6:dw0=rng.choice([0,1,2,3])
 elif op==2:dw0=rng.choice([1,2,3,0xc1,0x3ff0002])
 b=bytearray(cmd(op,dw0,dw1,base,rng.choice([0,0,0,1,2,0xffffffff]),rng.randrange(65536)))
 if rng.randrange(12)==0:b[rng.choice([1,16,44,48,52,56,60])]=rng.randrange(256)
 check(bytes(b))
r={'pass_':True,'commands':checks,'deferred_aers':deferred,'state_checks':'queue descriptors, generation deltas, features, held AER, cache state, set-cache/flush calls and cache-resync signal after every command/reset','oracle':'Actual S139/S124 admin owner with current NamespacePair','scope':'Fake memory range and physical callbacks. No DMA, CQ production, IRQ, host-dependency removal or hardware installation'}
(S/'differential-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
