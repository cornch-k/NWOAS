#!/usr/bin/env python3
"""S101: exercise nvme_rw_guest from the host using m1n1 heap pages as the "guest RAM".
Guest map is set so the heap window is the identity high range. Write 16 pages of pattern
to T..T+15 (WINTEST middle) through a guest PRP list, read them back with rw_guest into
other pages, compare, restore the original 16 blocks, verify guards. Negative: unaligned
PRP, page outside the map, LBA outside the zone must all be refused with nothing written."""
import signal,subprocess,json,struct,hashlib,time
if not __debug__:raise SystemExit('asserts required')
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True);assert r.returncode==1 and not r.stdout and not r.stderr
def dl(*_):raise TimeoutError('deadline')
signal.signal(signal.SIGALRM,dl);signal.alarm(150)
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
iface=UartInterface();p=M1N1Proxy(iface,debug=False);bootstrap_port(iface,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower();assert p.nvme_init()
FIRST,LAST=53839104,59968629;T=(FIRST+LAST)//2;N=16
data=u.memalign(0x4000,N*4096);back=u.memalign(0x4000,N*4096);lst=u.memalign(0x4000,0x4000);lst2=u.memalign(0x4000,0x4000);one=u.memalign(0x4000,0x4000)
lo=min(data,back,lst,lst2,one)&~0xfff;hi=(max(data,back,lst,lst2,one)+N*4096+0x4000)&~0xfff
p.nvme_guest_map(0,lo,hi) # heap window only; low window disabled (gpa<4G -> 0+gpa = outside)
def rd(l,n=1):signal.alarm(20);assert p.nvme_read_n(1,l,one,1) if n==1 else p.nvme_read_n(1,l,back,n);return iface.readmem(one if n==1 else back,n*4096)
def prplist(buf,n,dst):iface.writemem(dst,b''.join(struct.pack('<Q',buf+i*4096) for i in range(1,n)))
res={}
guards={l:rd(l) for l in (T-1,T+N)}
orig=rd(T,N)
pat=b''.join(bytes([0x60+i])*4096 for i in range(N));iface.writemem(data,pat);prplist(data,N,lst)
signal.alarm(20);res['write16_ok']=p.nvme_rw_guest(1,T,N,data,lst)==1
assert p.nvme_flush(1)
iface.writemem(back,bytes(N*4096));prplist(back,N,lst2)
signal.alarm(20);res['read16_ok']=p.nvme_rw_guest(0,T,N,back,lst2)==1
got=iface.readmem(back,N*4096);res['read16_matches_pattern']=(got==pat)
res['copy_path_sees_pattern']=(rd(T,N)==pat)
# 2-block form (prp2 = second page)
iface.writemem(data,b'\x77'*8192);signal.alarm(20);res['write2_ok']=p.nvme_rw_guest(1,T,2,data,data+4096)==1
iface.writemem(back,bytes(8192));signal.alarm(20);res['read2_ok']=p.nvme_rw_guest(0,T,2,back,back+4096)==1 and iface.readmem(back,8192)==b'\x77'*8192
# restore
iface.writemem(data,orig);prplist(data,N,lst);signal.alarm(20);assert p.nvme_rw_guest(1,T,N,data,lst);assert p.nvme_flush(1)
res['restored']=(rd(T,N)==orig)
res['guards_unchanged']=all(rd(l)==guards[l] for l in guards)
# negatives
signal.alarm(20);res['refuse_unaligned_prp1']=p.nvme_rw_guest(1,T,1,data+16,0)==2
signal.alarm(20);res['refuse_page_outside_map']=p.nvme_rw_guest(1,T,1,hi+0x100000,0)==2
signal.alarm(20);res['refuse_lba_outside_zone']=p.nvme_rw_guest(1,53839103,1,data,0)==2
signal.alarm(20);res['refuse_read_page_outside_map']=p.nvme_rw_guest(0,T,1,0x800000000,0)==2 if 0x800000000<lo or 0x800000000>=hi else True
signal.alarm(20);res['refuse_wrap_gpa']=p.nvme_rw_guest(0,T,1,0xfffffffffffff000,0)==2
res['still_restored']=(rd(T,N)==orig)
# timing: 16-block direct vs copy path (single request vs writemem+request)
t=time.monotonic()
for _ in range(20):p.nvme_rw_guest(0,T,N,back,lst2)
res['ms_per_direct_read16']=round((time.monotonic()-t)/20*1000,1)
t=time.monotonic()
for _ in range(20):p.nvme_read_n(1,T,back,N);iface.readmem(back,N*4096)
res['ms_per_copy_read16']=round((time.monotonic()-t)/20*1000,1)
p.nvme_shutdown()
print(json.dumps(res,indent=1));print('S101 PASS' if all(v for k,v in res.items() if not k.startswith('ms_')) else 'S101 FAIL')
