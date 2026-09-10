"""Additional policy and full-command coverage requested by source review."""
from pathlib import Path
import ctypes as C,importlib.util,json,random,struct,sys
S=Path(__file__).resolve().parent;sys.path.insert(0,str(S.parent/'nvme-s124'));sys.path.insert(0,str(S.parent/'transport-s123'))
from readonly_namespace import ReadOnlyNamespace
from writable_namespace import WindowWritableNamespace
from transport import NamespacePair
spec=importlib.util.spec_from_file_location('s225_profile_ref',S.parent/'nvme-s139/controller.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
class Policy(C.Structure):_fields_=[('primary_lbas',C.c_uint64),('link_lbas',C.c_uint64),('mdts',C.c_uint8),('paired',C.c_bool),('primary_readonly',C.c_bool),('volatile_cache',C.c_bool)]
class Result(C.Structure):_fields_=[('status',C.c_uint16),('bytes',C.c_uint32)]
lib=C.CDLL(str(S/'out/payload.dylib'));lib.nwoas_admin_payload.argtypes=[C.c_void_p,C.c_size_t,C.POINTER(Policy),C.c_void_p,C.c_size_t,C.POINTER(Result)]
checks=0;rng=random.Random(225225)
for ro in [False,True]:
 for paired in [False,True]:
  for mdts in [0,4,8]:
   primary=ReadOnlyNamespace(61279344,lambda *a:None) if ro else WindowWritableNamespace(61279344,lambda *a:None,lambda *a:None,lambda:None,53839104,59968629)
   primary.mdts=mdts;ns=NamespacePair(primary,ReadOnlyNamespace(8448,lambda *a:None)) if paired else primary
   owner=ref.Controller(ns,None,lambda x:None);policy=Policy(61279344,8448,mdts,paired,ro,not ro)
   def check(cmd):
    global checks
    want=owner.admin(cmd);out=C.create_string_buffer(b'\xa5'*4096,4096);r=Result();ret=lib.nwoas_admin_payload(cmd,64,C.byref(policy),out,4096,C.byref(r))
    assert ret==1 and (r.status,r.bytes)==(want.status,len(want.data));assert out.raw[:r.bytes]==want.data;assert out.raw[r.bytes:]==b'\xa5'*(4096-r.bytes);checks+=1
   for cns in range(256):
    for nsid in [0,1,2,3,0xffffffff]:
     cmd=bytearray(64);cmd[0]=6;struct.pack_into('<I',cmd,4,nsid);struct.pack_into('<I',cmd,40,cns);check(bytes(cmd))
   for lid in range(256):
    for numd in [0,1,2,3,1023,1024,0xffffffff]:
     cmd=bytearray(64);cmd[0]=2;struct.pack_into('<II',cmd,40,lid|((numd&65535)<<16),numd>>16);check(bytes(cmd))
   for _ in range(1000):
    cmd=bytearray(rng.randbytes(64));cmd[0]=rng.choice([2,6])
    if rng.randrange(2):cmd[1]=0;cmd[16:24]=bytes(8)
    check(bytes(cmd))
r={'pass_':True,'commands':checks,'profiles':12,'axes':'ReadOnly/WindowWritable; single/paired; MDTS 0,4,8','coverage':'All CNS/LID bytes, partial-page untouched tails, full random command bytes'}
(S/'profiles-result.json').write_text(json.dumps(r,indent=2)+'\n');print(r)
