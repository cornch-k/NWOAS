import ctypes as C,subprocess,tempfile,random,struct
from pathlib import Path
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text('#include "pattern.h"\nuint64_t pattern(uint64_t i,uint32_t p){return s171_pattern(i,p);}\nint parse(const uint16_t*s,uint32_t*m,uint32_t*p){return s171_parse(s,m,p);}\n')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC','-I'+str(root),str(p/'test.c'),'-o',str(p/'test.dylib')],check=True)
 lib=C.CDLL(str(p/'test.dylib'));lib.pattern.argtypes=[C.c_uint64,C.c_uint32];lib.pattern.restype=C.c_uint64
 lib.parse.argtypes=[C.POINTER(C.c_uint16),C.POINTER(C.c_uint32),C.POINTER(C.c_uint32)];lib.parse.restype=C.c_int
 mask=(1<<64)-1
 def py(i,p):
  x=i^((0x9e3779b97f4a7c15*(p+1))&mask);x=((x^(x>>30))*0xbf58476d1ce4e5b9)&mask;x=((x^(x>>27))*0x94d049bb133111eb)&mask;return x^(x>>31)
 rng=random.Random(171)
 for _ in range(2000):
  i=rng.randrange(0,8192*1048576//8);v=rng.randrange(10);assert lib.pattern(i,v)==py(i,v)
 for s,result in [('MEMTEST.EXE',(4096,3)),('"D:\\MEMTEST.EXE" 4096 3',(4096,3)),('x 64 1',(64,1)),('x 8192 10',(8192,10)),('x 63',None),('x 8193',None),('x 64 0',None),('x 64 11',None),('x 64 1 2',None),('x -1',None),('x 999999999999999999',None),('x 64bad',None),('"x',None)]:
  raw=s.encode('utf-16le')+b'\0\0';a=(C.c_uint16*(len(raw)//2)).from_buffer_copy(raw);m=C.c_uint32();p=C.c_uint32();ok=lib.parse(a,C.byref(m),C.byref(p));assert bool(ok)==(result is not None),(s,ok)
  if result:assert (m.value,p.value)==result
exe=(root/'MEMTEST.EXE').read_bytes();pe=struct.unpack_from('<I',exe,0x3c)[0];assert exe[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',exe,pe+4)[0]==0xaa64
print('PASS: 2000 independent pattern vectors, 13 argument cases, ARM64 PE machine')
