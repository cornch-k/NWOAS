#!/usr/bin/env python3
"""S96: host-side single-block write round-trip inside WINTEST only.
Target block T is in the middle of WINTEST (GPT slot3, verified S95).
Guards (APFS last block, alignment gap, WINTEST last, Recovery first) are read
before and after and must be byte-identical. T is restored to its original
content at the end. Never touches any LBA outside T.
"""
from pathlib import Path
import datetime,hashlib,json,os,signal,subprocess
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True)
assert r.returncode==1 and not r.stdout and not r.stderr,'STOP: serial owned or uncertain'
out=Path(__file__).resolve().parent/('write-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
print('OUTPUT',out,flush=True)
def deadline(*_):raise TimeoutError('S96 deadline; do not auto-reboot')
signal.signal(signal.SIGALRM,deadline);signal.alarm(60)
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
iface=UartInterface();p=M1N1Proxy(iface,debug=False);bootstrap_port(iface,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower(),'STOP wrong target'
if not __debug__:raise SystemExit('S96 probe safety checks are asserts; refuse to run optimized')
FIRST,LAST=53839104,59968511
T=(FIRST+LAST)//2
GUARDS=[53838942,53839103,FIRST,T-1,T+1,LAST,59968630,61279343]
assert FIRST<T<LAST and T not in GUARDS
assert p.nvme_init(),'STOP: NVMe init failed'
buf=u.memalign(0x4000,0x4000)
def read(lba):
    signal.alarm(15);assert p.nvme_read(1,lba,buf),f'read failed {lba}';return iface.readmem(buf,4096)
def write(lba,data):
    signal.alarm(15);assert lba==T and len(data)==4096
    iface.writemem(buf,data);assert p.nvme_write(1,lba,buf),f'write failed {lba}'
h=read(1);assert h[:8]==b'EFI PART','STOP: GPT missing'
import uuid;assert uuid.UUID(bytes_le=h[56:72])==uuid.UUID('898f172d-77bf-4007-b865-ac81a141eb3c'),'STOP: wrong disk'
e=read(2)[256:384];import struct
assert struct.unpack_from('<QQ',e,32)==(FIRST,LAST),'STOP: WINTEST slot3 mismatch'
before={g:read(g) for g in GUARDS};orig=read(T)
(out/'orig.bin').write_bytes(orig)
stamp=f'NWOAS-S96 {datetime.datetime.now().isoformat()} T={T} '.encode()
pattern=(stamp*(4096//len(stamp)+1))[:4096]
write(T,pattern);assert p.nvme_flush(1),'flush failed'
got=read(T);(out/'after-pattern.bin').write_bytes(got)
pattern_ok=got==pattern
write(T,orig);assert p.nvme_flush(1),'flush failed'
restored=read(T)==orig
after={g:read(g) for g in GUARDS}
guards_ok=all(before[g]==after[g] for g in GUARDS)
p.nvme_shutdown()
res={'experiment':'S96','target_lba':T,'pattern_roundtrip':pattern_ok,'restored':restored,'guards_unchanged':guards_ok,
     'guards':{str(g):hashlib.sha256(before[g]).hexdigest()[:16] for g in GUARDS},'orig_sha256':hashlib.sha256(orig).hexdigest()}
(out/'result.json').write_text(json.dumps(res,indent=2)+'\n');print(json.dumps(res,indent=2))
print('S96 PASS' if pattern_ok and restored and guards_ok else 'S96 FAIL',flush=True)
