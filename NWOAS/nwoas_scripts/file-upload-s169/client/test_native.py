"""Compile actual client frame/ACK helpers and cross-check with Python receiver."""
import ctypes,subprocess,tempfile,re,sys,hashlib
from pathlib import Path
O=Path(__file__).resolve().parent;sys.path.insert(0,str(O.parent));from receiver import Receiver,CAP,WINDOW
s=(O/'nwput.c').read_text()
def function(name):
 a=s.index('static ',s.index(name)-15) if False else s.index('static ',s.rfind('\nstatic ',0,s.index(name))+1)
 # locate this function's full balanced body
 b=s.index('{',s.index(name));depth=1;i=b+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
head='#include <stdint.h>\n#include "'+str(O.parent.parent/'file-delivery-s168/client/wire.h')+'"\ntypedef uint8_t U8;typedef uint32_t U32;typedef uint64_t U64;\n#define SIZE 3987720636ULL\n'
head+=re.search(r'static const U8 digest\[32\]=\{[^}]+\};',s).group()+'\n'
head+=re.search(r'static const U8 magic\[16\]=[^;]+;',s).group()+'\nstatic U8 *frame,*ack,token[16];\n'
head+='\n'.join(function(n) for n in ['eq(','header(','ack_ok('])
head+='''
void make_frame(U8 *out,const U8 *t,U32 seq,U32 kind,U64 off,const U8 *data,U32 n){frame=out;for(int i=0;i<16;i++)token[i]=t[i];for(U32 i=0;i<n;i++)frame[128+i]=data[i];header(seq,kind,off,n);}
int check_ack(U8 *a,const U8*t,U32 seq,U32 kind,U64 off){ack=a;for(int i=0;i<16;i++)token[i]=t[i];return ack_ok(seq,kind,off);}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(head)
 subprocess.run(['clang','-dynamiclib','-O1','-Wall','-Wextra','-Werror','-Wno-unused-const-variable',str(p/'test.c'),'-o',str(p/'test.dylib')],check=True)
 lib=ctypes.CDLL(str(p/'test.dylib'));P=ctypes.c_void_p;U=ctypes.c_uint32;Q=ctypes.c_uint64
 lib.make_frame.argtypes=[P,P,U,U,Q,P,U];lib.check_ack.argtypes=[P,P,U,U,Q];lib.check_ack.restype=ctypes.c_int
 token=b'T'*16;rx=Receiver(token,3987720636,'8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c',p/'backup')
 try:
  for seq,kind,off,data in [(1,0,0,b''),(2,1,0,b'X'*CAP),(3,1,CAP,b'Y'*CAP)]:
   out=ctypes.create_string_buffer(WINDOW);lib.make_frame(out,token,seq,kind,off,data,len(data))
   assert rx.accept(out.raw);assert rx.accept(out.raw)
   assert lib.check_ack(rx.ack,token,seq,kind,off+len(data))==1
   assert lib.check_ack(rx.ack,token,seq+1,kind,off+len(data))==0
   bad=bytearray(rx.ack);bad[41]^=1;assert lib.check_ack(bytes(bad),token,seq,kind,off+len(data))==0
  assert rx.received==2*CAP and not rx.done
 finally:rx.close()
print('PASS: actual native BEGIN/DATA frames accepted by Python receiver; ACK CRC/id guards verified')
