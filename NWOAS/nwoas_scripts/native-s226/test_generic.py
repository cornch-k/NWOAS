"""Generic FID6 dictionary path when namespace exposes no cache callbacks."""
from pathlib import Path
import ctypes as C,importlib.util,json,struct,sys
S=Path(__file__).resolve().parent;sys.path.insert(0,str(S.parent/'nvme-s124'))
spec=importlib.util.spec_from_file_location('s226_generic_ref',S.parent/'nvme-s139/controller.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
class Result(C.Structure):_fields_=[('result',C.c_uint32),('bytes',C.c_uint32),('status',C.c_uint16),('changed',C.c_uint8),('deferred',C.c_bool)]
lib=C.CDLL(str(S/'out/admin.dylib'));lib.execute.argtypes=[C.c_void_p,C.c_void_p,C.POINTER(Result)];lib.use_cache_callbacks.argtypes=[C.c_int];lib.inspect.argtypes=[C.c_uint];lib.inspect.restype=C.c_uint64
lib.initialize();lib.use_cache_callbacks(0);owner=ref.Controller(object(),None,lambda x:None);count=0
for op in [9,10]:
 for sel in [0,1,3,7]:
  for value in [0,1,2,0xffffffff]:
   cmd=bytearray(64);cmd[0]=op;struct.pack_into('<II',cmd,40,6|(sel<<8),value)
   want=owner.admin(bytes(cmd));out=C.create_string_buffer(4096);r=Result();assert lib.execute(bytes(cmd),out,C.byref(r))==1
   assert (r.status,r.result,r.bytes,r.changed)==(want.status,want.result,len(want.data),0)
   assert lib.inspect(38)==owner.features[6] and lib.inspect(14)==0;count+=1
result={'pass_':True,'commands':count,'cache_callbacks':False,'set_cache_calls':0,'oracle':'Actual S139 admin generic feature dictionary path'}
(S/'generic-result.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
