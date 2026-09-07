#!/usr/bin/env python3
"""S97: multi-block (1..16) read/write through P_NVME_READ_N/WRITE_N.
Reads: 16-block read of LBA 0..15 must equal 16 single reads (GPT area, read-only).
Writes: 16-block pattern at T..T+15 inside WINTEST, read back, restore original, verify;
guards T-1 and T+16 unchanged. Timing: single vs 16-block round trips."""
import os,signal,subprocess,json,hashlib,time
if not __debug__:raise SystemExit('asserts required')
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True)
assert r.returncode==1 and not r.stdout and not r.stderr,'STOP: serial owned'
def dl(*_):raise TimeoutError('deadline')
signal.signal(signal.SIGALRM,dl);signal.alarm(120)
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
iface=UartInterface();p=M1N1Proxy(iface,debug=False);bootstrap_port(iface,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower()
FIRST,LAST=53839104,59968511;T=(FIRST+LAST)//2;N=16
assert FIRST<=T-1 and T+N<=LAST
assert p.nvme_init()
buf=u.memalign(0x4000,0x10000)
def rd1(lba):
    signal.alarm(20);assert p.nvme_read(1,lba,buf);return iface.readmem(buf,4096)
def rdn(lba,n):
    signal.alarm(20);assert p.nvme_read_n(1,lba,buf,n),f'read_n failed {lba}+{n}';return iface.readmem(buf,n*4096)
def wrn(lba,data):
    signal.alarm(20);assert FIRST<=lba and lba+len(data)//4096-1<=LAST and len(data)%4096==0
    iface.writemem(buf,data);assert p.nvme_write_n(1,lba,buf,len(data)//4096),f'write_n failed {lba}'
h=rd1(1);assert h[:8]==b'EFI PART'
single=b''.join(rd1(i) for i in range(N))
multi=rdn(0,N);read16_ok=(single==multi)
two=rdn(0,2);read2_ok=(two==single[:8192])
g_before=(rd1(T-1),rd1(T+N))
orig=rdn(T,N)
pat=b''.join(bytes([0x50+i])*4096 for i in range(N))
wrn(T,pat);assert p.nvme_flush(1)
got=rdn(T,N);pat_ok=(got==pat)
wrn(T,orig);assert p.nvme_flush(1)
rest_ok=(rdn(T,N)==orig)
g_after=(rd1(T-1),rd1(T+N));guards_ok=(g_before==g_after)
t=time.monotonic()
for _ in range(20):rd1(0)
t1=(time.monotonic()-t)/20
t=time.monotonic()
for _ in range(20):rdn(0,N)
t16=(time.monotonic()-t)/20
p.nvme_shutdown()
res=dict(read16_matches_singles=read16_ok,read2_ok=read2_ok,write16_roundtrip=pat_ok,restored=rest_ok,guards_unchanged=guards_ok,
         ms_per_single_4k=round(t1*1000,1),ms_per_16blk_64k=round(t16*1000,1),KiBps_single=round(4/t1),KiBps_16blk=round(64/t16))
print(json.dumps(res,indent=1));print('S97 PASS' if all(res[k] for k in ('read16_matches_singles','read2_ok','write16_roundtrip','restored','guards_unchanged')) else 'S97 FAIL')
