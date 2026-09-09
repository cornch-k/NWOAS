# S130, executed after the D81 USB module and hv.load_raw().
# Direct guest PRP DMA is enabled by default with a host-copy fallback. Data writes ONLY inside the
# Windows zone LBA 53839104-59968629 (gated here, in WindowWritableNamespace, and in m1n1 C);
# partition-table blocks (0-5, 61279339-61279343) writable only through GptGuard validation.
if not __debug__:raise SystemExit('S96 safety checks are asserts; refuse to run optimized')
import os,sys
from pathlib import Path
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/nvme-s124')
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/nvme-s130')
if os.environ.get('NWOAS_CONTROLLER_DIR'):
    sys.path.insert(0,os.environ['NWOAS_CONTROLLER_DIR'])
from controller import Controller
from writable_namespace import WindowWritableNamespace
import readonly_namespace as _readonly_namespace
import writable_namespace as _writable_namespace
from quiet_log import QuietLogSampler
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

# S102: the low 4 GiB window is backed by the TOP 4 GiB of guest RAM (map_hw in the D81 module).
# UEFI allocates top-down, so winload/RAMDisk/ACPI landed physically on the window's backing and
# were overwritten once Windows used the upper window pages (all three ~6 GB failures). Fix: take
# the backing out of the guest-visible RAM by shrinking boot_args.mem_size (already written to
# guest memory by load_raw; offset 24 in every BootArgs revision). Requires the S102 UEFI build,
# whose DART/HideHighRam drivers compute backing = phys_base + mem_size.
import os as _os
LOW_BACKING=u.ba.phys_base+u.ba.mem_size-0x100000000-0x34000
EXCLUDE_WINDOW=_os.environ.get('NWOAS_EXCLUDE_WINDOW','1')=='1'
if EXCLUDE_WINDOW:
    _ba=hv.guest_base+hv.bootargs_off
    _old=p.read64(_ba+24)
    assert _old==hv.tba.mem_size and p.read64(_ba+16)==hv.phys_base,'boot_args layout mismatch'
    # UEFI sees boot_args.mem_size reduced by the 0x34000 skew (observed: hv 0x2a46f4000 -> UEFI
    # 0x2A46C0000), so add it back here; UEFI then computes backing == phys_base + mem_size exactly.
    _new=LOW_BACKING-hv.phys_base+0x34000
    assert 0x100000000<_new<_old,'window exclusion would leave too little RAM'
    p.write64(_ba+24,_new);hv.tba.mem_size=_new
    hv.log(f'[S102] guest mem_size {_old:#x} -> {_new:#x}; window backing {LOW_BACKING:#x} is now outside guest RAM')
class GuestMemory:
    def __init__(self):
        self.scratch=u.memalign(0x4000,0x10000)
        self.low_backing=LOW_BACKING
        self.high_min=hv.phys_base
        self.high_max=LOW_BACKING if EXCLUDE_WINDOW else u.ba.phys_base+u.ba.mem_size
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

MAX_TRANSFER=int(_os.environ.get('NWOAS_MAX_TRANSFER','65536'),0)
if MAX_TRANSFER not in (65536,1048576):raise ValueError('unsupported NWOAS_MAX_TRANSFER')
MAX_BLOCKS=MAX_TRANSFER//4096
_readonly_namespace.MAX_TRANSFER=MAX_TRANSFER
_writable_namespace.MAX_TRANSFER=MAX_TRANSFER
memory=GuestMemory()
# S101: proxy-request watchdog. Records the request in flight; a thread reports to the log file
# (stderr, not via the proxy) if one reply is overdue, so a mini-side hang names the request.
import threading,time as _time,sys as _sys
_inflight={'op':None,'args':None,'t':None}
_orig_request=p.request
def _watched_request(opcode,*args,**kw):
    _inflight['op']=opcode;_inflight['args']=args;_inflight['t']=_time.monotonic()
    try:return _orig_request(opcode,*args,**kw)
    finally:_inflight['t']=None
p.request=_watched_request
def _watchdog():
    warned=None
    while True:
        _time.sleep(5)
        t=_inflight['t']
        if t is not None and _time.monotonic()-t>20 and warned!=t:
            warned=t
            print(f"[S101] WATCHDOG proxy request overdue {_time.monotonic()-t:.0f}s: op=0x{_inflight['op']:x} args={[hex(a) if isinstance(a,int) else a for a in (_inflight['args'] or ())]}",file=_sys.stderr,flush=True)
threading.Thread(target=_watchdog,daemon=True).start()
assert p.nvme_init(),'ANS2 init failed'
# S101: zero-copy. m1n1 applies the same GPA->PA rules as GuestMemory.pa and checks carveouts itself.
p.nvme_guest_map(memory.low_backing,memory.high_min,memory.high_max)
direct_ok=direct_fallback=0
DIRECT_READ=_os.environ.get('NWOAS_DIRECT_READ','1')=='1'   # Set 0 for an explicit copy-path experiment
DIRECT_WRITE=_os.environ.get('NWOAS_DIRECT_WRITE','1')=='1'
hv.log(f'[S103] direct read={DIRECT_READ} write={DIRECT_WRITE}')
def backend_direct(write,lba,n,prp1,prp2):
    global direct_ok,direct_fallback
    if (write and not DIRECT_WRITE) or (not write and not DIRECT_READ):return False
    if not (1<=n<=MAX_BLOCKS and 0<=lba and lba+n<=61279344):return False
    if write and not (WINTEST_FIRST<=lba and lba+n-1<=WINTEST_LAST):return False
    r=p.nvme_rw_guest(write,lba,n,prp1,prp2)
    if r==0:raise OSError('ANS2 direct rw failed after submission') # -> Controller.process -> CFS, never re-submit
    ok=(r==1)
    if ok:
        direct_ok+=1
        if direct_ok<=4 or direct_ok%2000==0:hv.log(f'[S101] DIRECT {"W" if write else "R"} lba={lba} n={n} ok={direct_ok} fallback={direct_fallback}')
    else:
        direct_fallback+=1
        if direct_fallback<=8 or direct_fallback%256==0:hv.log(f'[S101] DIRECT refused -> copy path: {"W" if write else "R"} lba={lba} n={n} prp1={prp1:x} prp2={prp2:x} count={direct_fallback}')
    return ok
nsbuf=u.memalign(0x4000,0x10000)
reads=0
def backend(lba,n=1):
    global reads
    if not (1<=n<=MAX_BLOCKS and 0<=lba and lba+n<=61279344):raise ValueError('namespace bound')
    if not p.nvme_read_n(1,lba,nsbuf,n):raise OSError('ANS2 read failed')
    reads+=n
    # Preserve the first requests and then sample at 4096-block boundaries.
    # At eight cores the former 128-block interval alone emitted hundreds of
    # lines per minute and needlessly serialized storage on the diagnostic UART.
    if reads<=12 or reads//4096!=(reads-n)//4096:hv.log(f'[S93] ANS READ lba={lba} n={n} count={reads}')
    return iface.readmem(nsbuf,n*4096)
# S96/S98: Windows zone = WINTEST (GPT slot3, S95) plus the unallocated 118-block gap up to the
# block before Apple RecoveryOS (59968630). Windows fills free extents to their end.
WINTEST_FIRST,WINTEST_LAST=53839104,59968629
writes=0
def backend_write(lba,data):
    global writes
    n=len(data)//4096
    if len(data)%4096 or not 1<=n<=MAX_BLOCKS:raise ValueError('write size')
    if not (WINTEST_FIRST<=lba and lba+n-1<=WINTEST_LAST):raise ValueError('write outside WINTEST')
    iface.writemem(nsbuf,data)
    if not p.nvme_write_n(1,lba,nsbuf,n):raise OSError('ANS2 write failed')
    writes+=n
    if writes<=12 or writes//4096!=(writes-n)//4096:hv.log(f'[S96] ANS WRITE lba={lba} n={n} count={writes}')
def backend_flush():
    global flushes
    if not p.nvme_flush(1):raise OSError('ANS2 flush failed')
    flushes+=1
    if flushes<=8 or flushes%256==0:hv.log(f'[S96] ANS FLUSH count={flushes}')
# Confirm target identity before publishing the namespace.
import uuid
h=backend(1)
assert h[:8]==b'EFI PART' and uuid.UUID(bytes_le=h[56:72])==uuid.UUID('898f172d-77bf-4007-b865-ac81a141eb3c')
last_irq=None
def irq(level):
    global last_irq
    if level!=last_irq:p.request(0xc30,int(level));last_irq=level
log_sampler=QuietLogSampler()
def log(msg):
    if log_sampler.should_emit(msg):hv.log('[S130] '+msg)
# S98: validated partition-table writes. Protected entries = ISC / macOS APFS / RecoveryOS,
# raw bytes captured read-only in S95 (ans2-s87/run-20260907-160641/lba-2.bin slots 1,2,4).
from gpt_guard import GptGuard
PROTECTED=[bytes.fromhex(h) for h in (
 '616964690067aa11aa1100306543ecac448ff356555cda48aff097af9bb0a578060000000000000005f40100000000000000000000000000690042006f006f007400530079007300740065006d0043006f006e007400610069006e00650072000000000000000000000000000000000000000000000000000000000000000000',
 'ef57347c0000aa11aa1100306543ecac70fd24c6bd53274cadc71ac2f9288cbc06f40100000000005e843503000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',
 '727663520079aa11aa1100306543ecac350d13593224cb4f82067efdf9903d4d760c9303000000006a0ca7030000000000000000000000005200650063006f0076006500720079004f00530043006f006e007400610069006e006500720000000000000000000000000000000000000000000000000000000000000000000000')]
gpt_writes=0
def backend_gpt_write(lba,data):
    global gpt_writes
    assert len(data)==4096 and (lba<=5 or 61279339<=lba<=61279343),'gpt write outside table blocks'
    iface.writemem(nsbuf,data)
    if not p.nvme_write_n(1,lba,nsbuf,1):raise OSError('ANS2 gpt write failed')
    gpt_writes+=1;hv.log(f'[S98] GPT COMMIT lba={lba} count={gpt_writes}')
gpt=GptGuard(backend,backend_gpt_write,61279344,WINTEST_FIRST,WINTEST_LAST,PROTECTED) # raises if on-disk table already violates
assert gpt.gpt_lbas==set(range(6))|set(range(61279339,61279344)),'GPT block set derived from disk differs from the fixed layout'
_namespace=WindowWritableNamespace(61279344,backend,backend_write,backend_flush,WINTEST_FIRST,WINTEST_LAST,gpt,backend_direct)
_namespace.mdts=MAX_TRANSFER.bit_length()-1-12
c=Controller(_namespace,memory,irq,log)
def pci_read(addr,width):
    off=addr-ECAM
    value=c.pci_read(off,width) if off<4096 else (1<<width)-1
    if off<4096:log(f'PCI R {off:x}/{width}={value:x}')
    return value
def pci_write(addr,value,width):
    off=addr-ECAM
    if off<4096:log(f'PCI W {off:x}/{width}={value:x}');c.pci_write(off,value,width)
# S97 timing: host = inside our trap handlers; guest = wall time between our handlers
# (guest execution + IRQ delivery + ISR). Log early milestones, then every 4096
# I/O doorbells so measurement itself does not become an SMP serial bottleneck.
import time as _t
flushes=0
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
        if st['db'] in (64,256,1024) or st['db']%4096==0:
            hv.log(f"[S97] STAT iodb={st['db']} traps={st['traps']} host_ms/db={st['host']*1000/st['db']:.1f} guest_ms/db={st['guest']*1000/st['db']:.1f} traps/db={st['traps']/st['db']:.1f}")
    _leave(t0)
hv.add_tracer(irange(ECAM,0x100000),'s93-nvme-ecam',TraceMode.HOOK,read=pci_read,write=pci_write)
hv.add_tracer(irange(BAR,0x4000),'s93-nvme-bar',TraceMode.HOOK,read=mmio_read,write=mmio_write)
hv._nwoas_nvme=(c,memory,nsbuf)
hv.log(f'[S98] ANS2 namespace armed: PCI1:00:00.0, INTx900, 251000193024 bytes; data writes LBA {WINTEST_FIRST}-{WINTEST_LAST}, GPT blocks via validator')
