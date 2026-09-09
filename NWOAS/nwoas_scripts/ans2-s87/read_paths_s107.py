#!/usr/bin/env python3
"""S107: storage READS ONLY. Compare contiguous copy and guest PRP DMA paths.
Run only on idle J274 proxy; test memory lies in unused target guest-RAM space.
No NVMe write/format/flush, no partition modification, no guest boot.
"""
import signal,subprocess,json,struct,hashlib,time
from pathlib import Path
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True);assert r.returncode==1 and not r.stdout and not r.stderr
signal.signal(signal.SIGALRM,lambda *_:(_ for _ in ()).throw(TimeoutError('S107 deadline')));signal.alarm(60)
i=UartInterface();p=M1N1Proxy(i);bootstrap_port(i,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower();assert p.nvme_init()
scratch=u.memalign(0x4000,0x10000);plist=u.memalign(0x4000,0x4000)
low=u.ba.phys_base+u.ba.mem_size-0x100000000-0x34000
carve=[];cp=p.mcc_get_carveouts()
for _ in range(64):
 a=p.read64(cp);n=p.read64(cp+8)
 if not a:break
 carve.append((a,a+n));cp+=16
else:raise ValueError('carveouts')
# All mappings are for this isolated test only. Scratch/list live in proxy heap.
p.nvme_guest_map(low,u.ba.phys_base,low)
layouts={
 'high_9_contiguous':[0x900000000+j*4096 for j in range(16)],
 'high_a_scattered':[0xa00000000+j*0x8000 for j in range(16)],
 'low_alias':[0x20000000+j*4096 for j in range(16)],
 'mixed':[0x900100000+j*0x4000 if j%2 else 0x30000000+j*0x4000 for j in range(16)]}
def physical(g):return low+g if g<0x100000000 else g
for pages in layouts.values():
 for g in pages:
  a=physical(g);assert u.heap_top+0x1000000<a and a+4096<=u.ba.phys_base+u.ba.mem_size
  assert not any(a<hi and lo<a+4096 for lo,hi in carve)
# List itself uses an identity address inside registered high RAM.
assert u.ba.phys_base<=plist<low
start=55974436;results=[];t=time.monotonic()
try:
 for name,pages in layouts.items():
  for count in (1,2,3,8,16):
   for iteration in range(8):
    signal.alarm(30);lba=start+iteration*257
    assert p.nvme_read_n(1,lba,scratch,count)
    ref=i.readmem(scratch,count*4096)
    for g in pages[:count]:p.memset8(physical(g),0xa5,4096)
    prp2=0
    if count==2:prp2=pages[1]
    elif count>2:
     i.writemem(plist,b''.join(struct.pack('<Q',g) for g in pages[1:count]));prp2=plist
    rc=p.nvme_rw_guest(0,lba,count,pages[0],prp2)
    assert rc==1,(name,count,iteration,rc)
    got=[]
    for g in pages[:count]:
     p.memcpy8(scratch,physical(g),4096);got.append(i.readmem(scratch,4096))
    actual=b''.join(got)
    if actual!=ref:
     report={'result':'READ_PATH_MISMATCH','layout':name,'count':count,'iteration':iteration,'lba':lba,'expected_sha256':hashlib.sha256(ref).hexdigest(),'actual_sha256':hashlib.sha256(actual).hexdigest()}
     print(json.dumps(report),flush=True);Path('/tmp/nwoas-s107-read-failure.json').write_text(json.dumps(report,indent=2));raise ValueError('DMA read mismatch')
   results.append({'layout':name,'blocks':count,'repetitions':8,'pass':True});print('PASS',name,count,flush=True)
 report={'experiment':'S107','result':'READ_PATHS_PASS','cases':results,'storage_writes':False,'elapsed_seconds':time.monotonic()-t}
 Path('/tmp/nwoas-s107-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
finally:
 try:signal.alarm(30);p.nvme_shutdown()
 finally:signal.alarm(0);i.dev.close()
