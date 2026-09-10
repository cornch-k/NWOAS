"""Compare pure C Identify/Get Log Page payloads to current Python stack."""
from pathlib import Path
import ctypes as C,importlib.util,json,random,struct,sys
S=Path(__file__).resolve().parent;sys.path.insert(0,str(S.parent/'nvme-s124'));sys.path.insert(0,str(S.parent/'transport-s123'))
from readonly_namespace import ReadOnlyNamespace
from writable_namespace import WindowWritableNamespace
from transport import NamespacePair,CAPACITY
spec=importlib.util.spec_from_file_location('s225_ref',S.parent/'nvme-s139/controller.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
class Policy(C.Structure):_fields_=[('primary_lbas',C.c_uint64),('link_lbas',C.c_uint64),('mdts',C.c_uint8),('paired',C.c_bool),('primary_readonly',C.c_bool),('volatile_cache',C.c_bool)]
class Result(C.Structure):_fields_=[('status',C.c_uint16),('bytes',C.c_uint32)]
lib=C.CDLL(str(S/'out/payload.dylib'));lib.nwoas_admin_payload.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Policy),C.c_void_p,C.c_size_t,C.POINTER(Result)]
rng=random.Random(225);checks=0;errors=0;successes=0
for paired in [False,True]:
 primary=WindowWritableNamespace(61279344,lambda *x:None,lambda *x:None,lambda:None,53839104,59968629);primary.mdts=8
 link=ReadOnlyNamespace(CAPACITY,lambda *x:None)
 ns=NamespacePair(primary,link) if paired else primary
 controller=ref.Controller(ns,None,lambda v:None)
 policy=Policy(61279344,CAPACITY,8,paired,False,True)
 def check(cmd):
  global checks,errors,successes
  want=controller.admin(cmd);out=C.create_string_buffer(b'\xa5'*4096,4096);r=Result();ret=lib.nwoas_admin_payload(cmd,len(cmd),C.byref(policy),out,4096,C.byref(r))
  assert ret==1 and (r.status,r.bytes)==(want.status,len(want.data)),(paired,cmd.hex(),ret,r.status,r.bytes,want)
  assert out.raw[:r.bytes]==want.data
  if not r.bytes:assert out.raw==b'\xa5'*4096
  checks+=1;errors+=int(bool(r.status));successes+=int(not r.status)
 for cns in [0,1,2,3,255,256,0xffffffff]:
  for nsid in [0,1,2,3,0xffffffff]:
   cmd=bytearray(64);cmd[0]=6;struct.pack_into('<I',cmd,4,nsid);struct.pack_into('<I',cmd,40,cns);check(bytes(cmd))
 for lid in [0,1,2,3,4,0xc1,255]:
  for numd in [0,1,2,3,1023,1024,0xffff,0xffffffff]:
   cmd=bytearray(64);cmd[0]=2;struct.pack_into('<II',cmd,40,lid|((numd&65535)<<16),numd>>16);check(bytes(cmd))
 for _ in range(6000):
  cmd=bytearray(64);cmd[0]=rng.choice([2,6]);struct.pack_into('<I',cmd,4,rng.choice([0,1,2,0xffffffff]));struct.pack_into('<I',cmd,40,rng.choice([0,1,2,3,255,1023<<16|2]))
  if rng.randrange(3)==0:cmd[rng.choice([1,16,17,40,41,44,48,52,56,60])]=rng.randrange(256)
  check(bytes(cmd))
# Invalid API calls are separate from command-status equivalence.
out=C.create_string_buffer(4096);r=Result();cmd=bytes(64);policy=Policy(1,1,8,True,False,True)
assert lib.nwoas_admin_payload(cmd,63,C.byref(policy),out,4096,C.byref(r))==-1
assert lib.nwoas_admin_payload(cmd,64,C.byref(policy),out,4095,C.byref(r))==-1
assert lib.nwoas_admin_payload(cmd,64,C.byref(policy),out,4096,C.byref(r))==0
result={'pass_':True,'commands':checks,'success_payloads':successes,'command_errors':errors,'oracle':'Actual S139 Controller + WindowWritableNamespace + NamespacePair','scope':'Pure payload construction only; no queues, DMA, device writes or hardware integration'}
(S/'differential-result.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
