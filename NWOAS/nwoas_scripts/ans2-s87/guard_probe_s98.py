#!/usr/bin/env python3
"""S98 C-guard boundary probe: gap block 59968629 writable (pattern then restore); 59968630 (RecoveryOS
first), 6 (ISC first), 53839103 refused; LBA 1 header rewrite with identical bytes accepted; spans crossing
the zone end (59968629 count=2) refused."""
import signal,subprocess,json,hashlib
if not __debug__:raise SystemExit('asserts required')
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True);assert r.returncode==1 and not r.stdout and not r.stderr
def dl(*_):raise TimeoutError('deadline')
signal.signal(signal.SIGALRM,dl);signal.alarm(90)
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
iface=UartInterface();p=M1N1Proxy(iface,debug=False);bootstrap_port(iface,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower();assert p.nvme_init()
buf=u.memalign(0x4000,0x10000)
def rd(l,n=1):signal.alarm(20);assert p.nvme_read_n(1,l,buf,n);return iface.readmem(buf,n*4096)
def wr(l,data):signal.alarm(20);iface.writemem(buf,data);return p.nvme_write_n(1,l,buf,len(data)//4096)
res={}
g_before={l:rd(l) for l in (59968630,6,53839103,59968628)}
orig=rd(59968629);res['gap_orig_zero']=orig==bytes(4096)
res['gap_write_ok']=bool(wr(59968629,b'\x9c'*4096)) and rd(59968629)==b'\x9c'*4096
res['gap_restored']=bool(wr(59968629,orig)) and rd(59968629)==orig
res['refuse_recovery_first']=wr(59968630,b'\x9c'*4096)==0
res['refuse_isc_first']=wr(6,b'\x9c'*4096)==0
res['refuse_gap_before_zone']=wr(53839103,b'\x9c'*4096)==0
res['refuse_span_over_zone_end']=wr(59968629,b'\x9c'*8192)==0
h1=rd(1);res['hdr_identical_rewrite_ok']=bool(wr(1,h1)) and rd(1)==h1
res['refuse_span_table_to_isc']=wr(4,bytes(4096*3))==0  # 4..6 would touch ISC: never accepted
res['guards_unchanged']=all(rd(l)==g_before[l] for l in g_before)
assert p.nvme_flush(1);p.nvme_shutdown()
print(json.dumps(res,indent=1));print('S98-GUARD PASS' if all(res.values()) else 'S98-GUARD FAIL')
