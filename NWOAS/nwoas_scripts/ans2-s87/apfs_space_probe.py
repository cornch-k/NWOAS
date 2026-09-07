#!/usr/bin/env python3
"""S91: read-only APFS NX/checkpoint/spaceman accounting, never shrink APFS.
Checksums and current checkpoint mapping required. Not a resize-limit estimate.
"""
from pathlib import Path
import os,struct,json,signal,subprocess,datetime
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
root=Path(__file__).resolve().parent
meta=json.loads(sorted(root.glob('run-*/metadata.json'))[-1].read_text())
assert meta['status']=='PRIMARY_GPT_AND_BACKUP_HEADER_CRC_PASS'
part=[p for p in meta['partitions'] if p['guid']=='c624fd70-53bd-4c27-adc7-1ac2f9288cbc'][0]
out=root/('space-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
port='/dev/cu.usbmodemC07HL05SQ6NY1';r=subprocess.run(['lsof','-t',port],capture_output=True);assert r.returncode==1 and not r.stdout and not r.stderr
def deadline(*_):raise TimeoutError('S91 deadline')
signal.signal(signal.SIGALRM,deadline);signal.alarm(30)
i=UartInterface();p=M1N1Proxy(i);bootstrap_port(i,p);u=ProxyUtils(p);assert 'j274' in str(u.adt.compatible).lower()
assert p.nvme_init();buf=u.memalign(0x4000,0x10000);cache={}
def read(block):
 assert 0<=block<part['bytes']//4096
 if block not in cache:
  signal.alarm(15);assert p.nvme_read(1,part['start_lba']+block,buf)
  cache[block]=i.readmem(buf,4096)
 return cache[block]
def valid(b):
 if b[:8] in [b'\0'*8,b'\xff'*8]:return False
 w=struct.unpack('<'+'I'*(len(b)//4),b);a=c=0
 for x in (*w[2:],*w[:2]):a+=x;c+=a
 return a%0xffffffff==0 and c%0xffffffff==0
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def u64(b,o):return struct.unpack_from('<Q',b,o)[0]
try:
 nx=read(0);assert nx[32:36]==b'NXSB' and valid(nx) and u32(nx,36)==4096
 count=u32(nx,104);base=u64(nx,112);assert 0<count<=1024 and not count&0x80000000
 desc=[(base+j,read(base+j)) for j in range(count)]
 candidates=[(addr,b) for addr,b in desc if b[32:36]==b'NXSB' and valid(b)]
 assert candidates
 addr,nx=max(candidates,key=lambda x:u64(x[1],16));xid=u64(nx,16);oid=u64(nx,152)
 maps=[(addr,b) for addr,b in desc if u32(b,24)&0xffff==12 and u64(b,16)==xid and valid(b)]
 matches=[]
 for addr,b in maps:
  n=u32(b,36);assert 40+n*40<=4096
  for k in range(n):
   off=40+k*40
   if u64(b,off+24)==oid:matches.append((u64(b,off+32),u32(b,off+8)))
 assert len(matches)==1,matches
 smaddr,size=matches[0];assert size==4096,'unsupported multi-block spaceman'
 sm=read(smaddr);assert valid(sm) and u64(sm,16)==xid and u32(sm,32)==4096
 blocks=u64(sm,48);free=u64(sm,72);assert 0<=free<=blocks==u64(nx,40)
 result={'experiment':'S91','checkpoint_xid':xid,'container_bytes':blocks*4096,'free_bytes':free*4096,'allocated_bytes':(blocks-free)*4096,'all_referenced_checksums_valid':True,'is_resize_limit':False,'writes_to_storage':False,'blocks_read':len(cache)}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 for a in [0,addr,smaddr]:(out/f'block-{a}.bin').write_bytes(read(a))
 print(json.dumps(result,indent=2),flush=True)
finally:
 signal.alarm(30);p.nvme_shutdown();signal.alarm(0);i.dev.close()
