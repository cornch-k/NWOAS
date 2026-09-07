# S93, executed after D83 USB module and hv.load_raw(). HARDWARE-UNVERIFIED.
# Guest DMA is host-copy only; storage backend has only nvme_read, never writes.
import sys
from pathlib import Path
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/nvme-s93')
from controller import Controller
from readonly_namespace import ReadOnlyNamespace
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
def backend(lba):
    global reads
    if not 0<=lba<61279344:raise ValueError('namespace bound')
    if not p.nvme_read(1,lba,nsbuf):raise OSError('ANS2 read failed')
    reads+=1
    if reads<=12 or reads%128==0:hv.log(f'[S93] ANS READ lba={lba} count={reads}')
    return iface.readmem(nsbuf,4096)
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
c=Controller(ReadOnlyNamespace(61279344,backend),memory,irq,log)
def pci_read(addr,width):
    off=addr-ECAM
    value=c.pci_read(off,width) if off<4096 else (1<<width)-1
    if off<4096:log(f'PCI R {off:x}/{width}={value:x}')
    return value
def pci_write(addr,value,width):
    off=addr-ECAM
    if off<4096:log(f'PCI W {off:x}/{width}={value:x}');c.pci_write(off,value,width)
def mmio_read(addr,width):
    value=c.read(addr-BAR,width);log(f'MMIO R {addr-BAR:x}/{width}={value:x}');return value
def mmio_write(addr,value,width):
    log(f'MMIO W {addr-BAR:x}/{width}={value:x}');c.write(addr-BAR,value,width)
hv.add_tracer(irange(ECAM,0x100000),'s93-nvme-ecam',TraceMode.HOOK,read=pci_read,write=pci_write)
hv.add_tracer(irange(BAR,0x4000),'s93-nvme-bar',TraceMode.HOOK,read=mmio_read,write=mmio_write)
hv._nwoas_nvme=(c,memory,nsbuf)
hv.log('[S93] READ-ONLY ANS2 namespace armed: PCI1:00:00.0, INTx900, 251000193024 bytes')
