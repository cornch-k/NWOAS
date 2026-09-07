# S93/S96, executed after D83 USB module and hv.load_raw(). Write path HARDWARE-UNVERIFIED.
# Guest DMA is host-copy only. Storage backend: nvme_read anywhere, nvme_write/flush ONLY
# inside WINTEST LBA 53839104-59968511 (gated here, in WindowWritableNamespace, and in m1n1 C).
if not __debug__:raise SystemExit('S96 safety checks are asserts; refuse to run optimized')
import sys
from pathlib import Path
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/nvme-s93')
from controller import Controller
from writable_namespace import WindowWritableNamespace
from m1n1.utils import irange
from m1n1.hv.types import TraceMode

assert 'j274' in str(u.adt.compatible).lower()
ECAM=0x700000000;BAR=0x700100000
# Software apertures lie below host DRAM (0x8_0000_0000+) and outside physical PCI.
assert BAR+0x4000<hv.ram_base
carveouts=[];cursor=p.mcc_get_carveouts()
for _ in range(64):
    base=p.read64(cursor);size=p.read64(cursor+8)
    if not base:break
    carveouts.append((base,base+size));cursor+=16
else:raise RuntimeError('unterminated carveout table')
# Verify chosen virtual interrupt is not described as a physical peripheral IRQ.
def check_irqs(node):
    assert 900 not in getattr(node,'interrupts',[]),f'IRQ900 conflicts with {node.name}'
    for child in node:check_irqs(child)
check_irqs(u.adt)

class GuestMemory:
    def __init__(self):
        self.scratch=u.memalign(0x4000,0x10000)
        self.low_backing=u.ba.phys_base+u.ba.mem_size-0x100000000-0x34000
        self.high_min=hv.phys_base
        self.high_max=u.ba.phys_base+u.ba.mem_size
    def pa(self,addr,n):
        if not isinstance(addr,int) or not 0<n<=65536:raise ValueError('invalid DMA size')
        end=addr+n
        if 0<=addr<end<=0x100000000:physical=self.low_backing+addr
        elif self.high_min<=addr<end<=self.high_max:physical=addr
        else:raise ValueError(f'DMA outside guest RAM {addr:x}+{n:x}')
        if any(physical<b and a<physical+n for a,b in carveouts):raise ValueError('DMA intersects carveout')
        return physical
    def contains(self,addr,n):
        try:self.pa(addr,n);return True
        except ValueError:return False
    def read(self,addr,n):
        pa=self.pa(addr,n);p.dc_civac(pa,n);p.memcpy8(self.scratch,pa,n)
        return iface.readmem(self.scratch,n)
    def write(self,addr,data):
        pa=self.pa(addr,len(data));iface.writemem(self.scratch,data)
        p.dc_civac(pa,len(data));p.memcpy8(pa,self.scratch,len(data));p.dc_civac(pa,len(data))

memory=GuestMemory()
assert p.nvme_init(),'ANS2 init failed'
nsbuf=u.memalign(0x4000,0x10000)
reads=0
def backend(lba,n=1):
    global reads
    if not (1<=n<=16 and 0<=lba and lba+n<=61279344):raise ValueError('namespace bound')
    if not p.nvme_read_n(1,lba,nsbuf,n):raise OSError('ANS2 read failed')
    reads+=n
    if reads<=12 or reads//128!=(reads-n)//128:hv.log(f'[S93] ANS READ lba={lba} n={n} count={reads}')
    return iface.readmem(nsbuf,n*4096)
# S96: writes allowed only inside WINTEST (GPT slot3, verified S95 run-20260907-160641).
WINTEST_FIRST,WINTEST_LAST=53839104,59968511
writes=0
def backend_write(lba,data):
    global writes
    n=len(data)//4096
    if len(data)%4096 or not 1<=n<=16:raise ValueError('write size')
    if not (WINTEST_FIRST<=lba and lba+n-1<=WINTEST_LAST):raise ValueError('write outside WINTEST')
    iface.writemem(nsbuf,data)
    if not p.nvme_write_n(1,lba,nsbuf,n):raise OSError('ANS2 write failed')
    writes+=n
    if writes<=12 or writes//128!=(writes-n)//128:hv.log(f'[S96] ANS WRITE lba={lba} n={n} count={writes}')
def backend_flush():
    if not p.nvme_flush(1):raise OSError('ANS2 flush failed')
    hv.log('[S96] ANS FLUSH')
# Confirm target identity before publishing the namespace.
import uuid
h=backend(1)
assert h[:8]==b'EFI PART' and uuid.UUID(bytes_le=h[56:72])==uuid.UUID('898f172d-77bf-4007-b865-ac81a141eb3c')
last_irq=None
def irq(level):
    global last_irq
    if level!=last_irq:p.request(0xc30,int(level));last_irq=level
log_count=0
def log(msg):
    global log_count
    log_count+=1
    if log_count<=200 or log_count%128==0 or msg.startswith(('CFS','MMIO W','EN ','CREATE ','CMD ')):hv.log('[S93] '+msg)
import struct as _st
gpt=backend(2)+backend(3)
e=gpt[2*128:3*128]
assert e[:16]==uuid.UUID('ebd0a0a2-b9e5-4433-87c0-68b6b72699c7').bytes_le and _st.unpack_from('<QQ',e,32)==(WINTEST_FIRST,WINTEST_LAST),'WINTEST GPT slot3 mismatch; refusing writable namespace'
c=Controller(WindowWritableNamespace(61279344,backend,backend_write,backend_flush,WINTEST_FIRST,WINTEST_LAST),memory,irq,log)
def pci_read(addr,width):
    off=addr-ECAM
    value=c.pci_read(off,width) if off<4096 else (1<<width)-1
    if off<4096:log(f'PCI R {off:x}/{width}={value:x}')
    return value
def pci_write(addr,value,width):
    off=addr-ECAM
    if off<4096:log(f'PCI W {off:x}/{width}={value:x}');c.pci_write(off,value,width)
# S97 timing: host = inside our trap handlers; guest = wall time between our handlers
# (guest execution + IRQ delivery + ISR). Logged every 64 I/O doorbell writes.
import time as _t
st=dict(host=0.0,guest=0.0,traps=0,db=0,last_exit=None)
def _enter():
    now=_t.monotonic()
    if st['last_exit'] is not None:st['guest']+=now-st['last_exit']
    st['traps']+=1;return now
def _leave(t0):
    now=_t.monotonic();st['host']+=now-t0;st['last_exit']=now
def mmio_read(addr,width):
    t0=_enter();value=c.read(addr-BAR,width);log(f'MMIO R {addr-BAR:x}/{width}={value:x}');_leave(t0);return value
def mmio_write(addr,value,width):
    t0=_enter();off=addr-BAR;log(f'MMIO W {off:x}/{width}={value:x}');c.write(off,value,width)
    if 0x1008<=off<0x1800 and off%8==0:
        st['db']+=1
        if st['db']%64==0:
            hv.log(f"[S97] STAT iodb={st['db']} traps={st['traps']} host_ms/db={st['host']*1000/st['db']:.1f} guest_ms/db={st['guest']*1000/st['db']:.1f} traps/db={st['traps']/st['db']:.1f}")
    _leave(t0)
hv.add_tracer(irange(ECAM,0x100000),'s93-nvme-ecam',TraceMode.HOOK,read=pci_read,write=pci_write)
hv.add_tracer(irange(BAR,0x4000),'s93-nvme-bar',TraceMode.HOOK,read=mmio_read,write=mmio_write)
hv._nwoas_nvme=(c,memory,nsbuf)
hv.log(f'[S96] ANS2 namespace armed: PCI1:00:00.0, INTx900, 251000193024 bytes, writes only LBA {WINTEST_FIRST}-{WINTEST_LAST}')
