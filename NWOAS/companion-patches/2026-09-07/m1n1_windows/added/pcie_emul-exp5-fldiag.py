# pcie_emul-exp5-fldiag.py (NWOAS §3.2 diagnostic) — deferred FL1100 kernel-phase register dump.
# Same exp5 setup as pcie_emul-exp5.py (pcie_init + FL1100/DART BYPASS tracers + map_hw window) but
# WITHOUT the disruptive backing-fix breaker (which broke in at t=45s pre-kernel). Instead a dedicated
# breaker waits for the KERNEL phase (elr=0xfffff8 in NWOAS_LOG), settles so Windows programs the
# FL1100 and tries USB, then breaks in ONCE and dumps the REAL FL1100 (0x6c0000000, USB-A, where the
# mouse/install-USB are — NOT the DWC3/USB-C at 0x502280000 the hv_exc.c [usb-bridge] watches):
#   CMD.MEM (decode on?), USBCMD.RS (running?), USBSTS.HSE/HCH (error/halt?), DCBAAP/ERSTBA (buffer
#   placement: window<4GB reachable vs high>4GB unreachable), all PORTSC (device connected? CCS),
#   IMAN.IP (interrupt pending?). This decides the §3.2 root: addressing/HSE vs enumeration vs interrupt.
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[fldiag] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0x80000000", 0)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x20000000", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[fldiag] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000   # FL1100 ECAM: bus2 dev0 fn0 (from fl1100_dump scan)
def _fldump():
    rd32 = lambda a: hv.p.read32(a) & 0xffffffff
    # FL1100 requires 32-bit MMIO accesses (DSDT _DSM fn6); a single 64-bit read returns garbage.
    # Read 64-bit registers (DCBAAP/CRCR/ERSTBA/ERDP) as two 32-bit halves.
    rd64 = lambda a: rd32(a) | (rd32(a + 4) << 32)
    cmd = rd32(FL_CFG + 0x04) & 0xffff
    bar0 = rd32(FL_CFG + 0x10)
    hv.log(f"[fldiag] ★FL1100 CFG: CMD={cmd:#06x}(MEM={(cmd>>1)&1} BM={(cmd>>2)&1}) BAR0={bar0:#x}")
    if not ((cmd >> 1) & 1):
        hv.log("[fldiag] ★CMD.MEM=0 -> FL1100 MMIO decode OFF (Windows didn't enable / reset it off). §3.2=controller-not-started.")
        return
    cap0 = rd32(FL_BAR); caplen = cap0 & 0xff
    op = FL_BAR + caplen
    rtsoff = rd32(FL_BAR + 0x18) & ~0x1f; rt = FL_BAR + rtsoff
    hcs1 = rd32(FL_BAR + 0x04); nports = (hcs1 >> 24) & 0xff
    usbcmd = rd32(op + 0x00); usbsts = rd32(op + 0x04)
    dcbaap = rd64(op + 0x30); crcr = rd64(op + 0x18); config = rd32(op + 0x38)
    erstsz = rd32(rt + 0x28); erstba = rd64(rt + 0x30); erdp = rd64(rt + 0x38); iman = rd32(rt + 0x20)
    hv.log(f"[fldiag] ★OP: USBCMD={usbcmd:#x}(RS={usbcmd&1} INTE={(usbcmd>>2)&1}) USBSTS={usbsts:#x}(HCH={usbsts&1} HSE={(usbsts>>2)&1} EINT={(usbsts>>3)&1}) caplen={caplen:#x} nports={nports}")
    hv.log(f"[fldiag] ★DCBAAP={dcbaap:#x} CRCR={crcr:#x} CONFIG={config:#x}")
    hv.log(f"[fldiag] ★ERST: sz={erstsz:#x} ERSTBA={erstba:#x} ERDP={erdp:#x} IMAN={iman:#x}(IP={iman&1} IE={(iman>>1)&1})")
    for n in range(min(max(nports, 1), 4)):
        ps = rd32(op + 0x400 + n * 0x10)
        hv.log(f"[fldiag] ★PORTSC[{n}]={ps:#x}(CCS={ps&1} PED={(ps>>1)&1} PLS={(ps>>5)&0xf} PP={(ps>>9)&1} CSC={(ps>>17)&1})")
    for nm, v in (("DCBAAP", dcbaap), ("ERSTBA", erstba)):
        loc = "HIGH>4GB(UNREACHABLE by 32bit FL1100!)" if v >= 0x100000000 else ("WINDOW<4GB(reachable)" if v else "UNSET")
        hv.log(f"[fldiag] ★{nm} placement: {v:#x} = {loc}")

hv._fldiag_n = 0        # break-in counter
hv._fldiag_dumps = 0    # dumps emitted
def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    # Multi-snapshot: dump on the 1st break-in and then ~every 20 break-ins (~60s at 3s bursts),
    # up to 5 dumps, so we catch the FL1100 BEFORE and AFTER usbxhci.sys programs it (avoids a single
    # too-early shot showing DCBAAP=0). Each snapshot is one-shot within its window via the counter.
    hv._fldiag_n += 1
    if hv._fldiag_dumps < 5 and (hv._fldiag_n == 1 or (hv._fldiag_n % 20) == 0):
        hv._fldiag_dumps += 1
        hv.log(f"[fldiag] === DUMP #{hv._fldiag_dumps} (break-in {hv._fldiag_n}) ===")
        try:
            _fldump()
        except Exception as e:
            try: hv.log(f"[fldiag] dump err: {e}")
            except Exception: pass
        hv.log(f"[fldiag] === dump #{hv._fldiag_dumps} done ===")
    return EXC_RET.HANDLED
hv.run_shell = _diag_run_shell

def _log_has(pat):
    lp = os.environ.get("NWOAS_LOG")
    if not lp:
        return False
    try:
        with open(lp, "r", errors="ignore") as f:
            return pat in f.read()
    except Exception:
        return False

def _breaker():
    t = 0
    while not _log_has("elr=0xfffff8"):
        time.sleep(5); t += 5
        if t > 1500:
            hv.log("[fldiag] kernel marker not seen in 25min; bursting anyway")
            break
    hv.log("[fldiag] kernel detected; settling 180s (usbxhci.sys finishes init/reset+program) then bursting")
    time.sleep(180)
    for i in range(140):        # ~7min of 3s bursts -> ~5 spaced dumps across usbxhci.sys init + wedge
        if hv._fldiag_dumps >= 5:
            break
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(3)
    hv.log(f"[fldiag] breaker done ({hv._fldiag_dumps} dumps, {hv._fldiag_n} break-ins)")

threading.Thread(target=_breaker, daemon=True).start()
print("[fldiag] FL1100 kernel-phase dump module armed")
