"""Read projection against actual S139 controller bytes; no hardware."""
from pathlib import Path
import ctypes as C, importlib.util, json, random, sys
S=Path(__file__).resolve().parent
sys.path.insert(0,str(S.parent/'nvme-s124'))
spec=importlib.util.spec_from_file_location('s224_reference',S.parent/'nvme-s139/controller.py')
ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
p=ref.Controller(None,None,lambda level:None)
lib=C.CDLL(str(S/'out/projection.dylib'))
lib.publish.argtypes=[C.c_uint64]*5
lib.read_projection.argtypes=[C.c_uint64,C.c_uint,C.c_int,C.c_int,C.c_uint32,C.POINTER(C.c_uint64)]
lib.counter.argtypes=[C.c_uint];lib.counter.restype=C.c_uint64
rng=random.Random(224);checks=0
out=C.c_uint64(0xabc)
assert not lib.read_projection(0x700000000,2,0,0,0,C.byref(out)) and out.value==0xabc
for state in range(200):
 p.cc,p.csts,p.aqa,p.mask=[rng.getrandbits(32) for _ in range(4)]
 p.asq,p.acq=[rng.getrandbits(64) for _ in range(2)]
 p.command=rng.getrandbits(16)&0x407;p.probe_low=bool(rng.getrandbits(1));p.probe_high=bool(rng.getrandbits(1))
 p.cq={0:ref.CQ(0x20000,256,bool(rng.getrandbits(1)),pending=rng.randrange(256)),1:ref.CQ(0x30000,256,bool(rng.getrandbits(1)),pending=rng.randrange(256))}
 pending=any(q.ien and q.pending for q in p.cq.values())
 flags=p.command|(bool(pending)<<16)|(p.probe_low<<17)|(p.probe_high<<18)|(1<<63)
 saved_mask=p.mask;lib.publish(p.cc|(p.csts<<32),p.aqa|(p.mask<<32),p.asq,p.acq,flags)
 for pci in [True,False]:
  bound=4096 if pci else 0x4000;base=0x700000000 if pci else 0x700100000
  for off in list(range(80))+[bound-8,bound-4,bound-2,bound-1]+[rng.randrange(bound) for _ in range(80)]:
   for w in range(4):
    armed=bool(rng.getrandbits(1));faulted=bool(rng.getrandbits(1));mask=rng.getrandbits(32)
    p.mask=mask if armed else saved_mask
    want=(p.pci_read if pci else p.read)(off,8<<w)
    if not pci and armed and faulted and off==0x1c and w==2:want=3
    got=lib.read_projection(base+off,w,armed,faulted,mask,C.byref(out))
    if not pci and off+(1<<w)>bound:assert not got;continue
    assert got and out.value==want,(state,pci,off,w,hex(out.value),hex(want));checks+=1
 for off in [4096,0x1fff,0xffff8]:
  for w in range(4):
   assert lib.read_projection(0x700000000+off,w,0,0,0,C.byref(out)) and out.value==(1<<(8<<w))-1;checks+=1
for address in [0,0x6ffffffff,0x700104000,0xffffffffffffffff]:
 assert not lib.read_projection(address,2,0,0,0,C.byref(out))
for w in [4,5,6,31,32,0xffffffff]:assert not lib.read_projection(0x700000000,w,0,0,0,C.byref(out))
lib.publish(0,0,0,0,0);assert not lib.read_projection(0x700000000,2,0,0,0,C.byref(out))
r={'pass_':True,'states':200,'compared_reads':checks,'oracle':'Actual imported S139/S124 controller','covered':'Widths, arbitrary slices, bounds, absent PCI functions, mask overlay, existing fatal CSTS override, disabled projection','scope':'Offline byte equivalence, not hardware stability'}
(S/'differential-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
