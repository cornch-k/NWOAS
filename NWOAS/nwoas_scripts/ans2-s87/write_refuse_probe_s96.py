#!/usr/bin/env python3
"""S96 negative test: ask m1n1 to write one block OUTSIDE WINTEST (the unused alignment
gap block 53839103 between APFS and WINTEST) and require refusal + unchanged content.
Also asks for an in-window write with nsid=2 and expects refusal."""
import os,signal,subprocess,json,hashlib
if not __debug__:raise SystemExit('asserts required')
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True)
assert r.returncode==1 and not r.stdout and not r.stderr,'STOP: serial owned'
def deadline(*_):raise TimeoutError('deadline')
signal.signal(signal.SIGALRM,deadline);signal.alarm(60)
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
iface=UartInterface();p=M1N1Proxy(iface,debug=False);bootstrap_port(iface,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower()
GAP=53839103;IN=56903807
assert p.nvme_init()
buf=u.memalign(0x4000,0x4000)
def read(lba):
    signal.alarm(15);assert p.nvme_read(1,lba,buf);return iface.readmem(buf,4096)
before=read(GAP)
iface.writemem(buf,b'\xee'*4096)
signal.alarm(15);refused_gap=(p.nvme_write(1,GAP,buf)==0)
signal.alarm(15);refused_nsid=(p.nvme_write(2,IN,buf)==0)
after=read(GAP)
p.nvme_shutdown()
res={'refused_gap_write':refused_gap,'refused_nsid2_write':refused_nsid,'gap_unchanged':before==after,'gap_sha':hashlib.sha256(before).hexdigest()[:16]}
print(json.dumps(res));print('S96-REFUSE PASS' if all(res[k] for k in ('refused_gap_write','refused_nsid2_write','gap_unchanged')) else 'S96-REFUSE FAIL')
