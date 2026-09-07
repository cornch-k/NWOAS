# pcie_emul-exp5-evtring1.py (NWOAS §3.2 root-confirm) — confirm-before-fix diagnostic for the
# UNIVERSAL enumeration-completion livelock. The FLC trace proved usbxhci (and UEFI XhciDxe)
# livelock re-resetting ALL ports (SuperSpeed install-USB storms too, not just the Low-speed
# mouse) at a single kernel instruction, because command/transfer completions are never
# "seen". Same root as the 07-10 UEFI event-ring desync/saturation saga.
#
# THIS MODULE (read-only, no kmutil): at each break-in dump the FL1100 EVENT RING memory as the
# FL1100 actually wrote it (via the low-window backing) vs usbxhci's ERDP (consume pos), so we
# can DECIDE the mechanism:
#   (a) ring FULL of cycle=1 completion TRBs but ERDP stuck  -> consumer cycle desync / saturation
#   (b) ring EMPTY (all cycle=0 / stale)                     -> FL1100 not producing (command ring
#                                                               / doorbell / DMA-to-ring broken)
#   (c) ring has garbage TRB types                           -> stale/coherence (wrong memory)
# Address model: test runs NWOAS_WIN_BASE=0 WIN_SIZE=0x100000000, so guest low IPA [0,4GB) is
# backed by phys [_backing, _backing+4GB). DMA addr == GPA in the identity low window, so the
# event-ring physical = _backing + (gpa & 0xffffffff). We read via hv.p.read32(phys). We also
# print the raw erstba/ring bases so a wrong-window read is obvious (TRB type must be 32/33/34).
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[evtring1] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_phys_base = hv.u.ba.phys_base
_mem_size = hv.u.ba.mem_size
_backing = _phys_base + _mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[evtring1] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} | phys_base={_phys_base:#x} mem_size={_mem_size:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000
TRB_TYPE = {32: "XFER-EVT", 33: "CMDCOMP", 34: "PORTSC-CHG", 35: "BW-REQ", 36: "DOORBELL",
            37: "HC-EVT", 38: "DEV-NOTIF", 39: "MFINDEX-WRAP"}

def _rd32(a):
    try:
        return hv.p.read32(a) & 0xffffffff
    except Exception:
        return None

def _rd64(a):
    lo = _rd32(a); hi = _rd32(a + 4)
    if lo is None or hi is None:
        return None
    return lo | (hi << 32)

def _phys_of_dma(gpa):
    # low-window identity: DMA addr == GPA, backed by _backing + (gpa & 0xffffffff)
    if gpa is None:
        return None
    lo = gpa & 0xffffffff
    if (gpa >> 32) != 0:
        # high DMA addr — not in the 4GB low window; caller decides. Report both.
        return None
    return _backing + lo

def _dump_trbs(label, phys_base_of_ring, n, erdp_gpa=None):
    # dump n TRBs (16B each) from ring physical base; flag cycle=1 completion TRBs
    if phys_base_of_ring is None:
        hv.log(f"[evtring1]   {label}: (no phys mapping)")
        return
    valid1 = 0
    erdp_off = None
    if erdp_gpa is not None:
        erdp_off = (erdp_gpa & 0xffffffff) - ((phys_base_of_ring - _backing))  # rough offset in ring
    for i in range(n):
        a = phys_base_of_ring + i * 16
        param = _rd64(a); status = _rd32(a + 8); ctrl = _rd32(a + 12)
        if ctrl is None:
            hv.log(f"[evtring1]   {label}[{i}]@{a:#x}: (read fail)"); continue
        cyc = ctrl & 1
        ttype = (ctrl >> 10) & 0x3f
        cc = (status >> 24) & 0xff if status is not None else None
        tn = TRB_TYPE.get(ttype, f"?{ttype}")
        mark = " <==ERDP" if (erdp_off is not None and i * 16 == erdp_off) else ""
        if cyc == 1 and ttype in TRB_TYPE:
            valid1 += 1
        hv.log(f"[evtring1]   {label}[{i:2d}]@{a:#x}: param={param:#018x} status={status:#010x}(CC={cc}) ctrl={ctrl:#010x} cyc={cyc} type={tn}{mark}")
    hv.log(f"[evtring1]   {label}: valid(cyc=1,known-type) TRBs = {valid1}/{n}")

def _evtring_dump():
    cmd = _rd32(FL_CFG + 0x04)
    if cmd is None or not ((cmd >> 1) & 1):
        hv.log(f"[evtring1] CFG CMD={cmd} MEM-decode off; skip"); return
    caplen = _rd32(FL_BAR) & 0xff
    rtsoff = _rd32(FL_BAR + 0x18) & ~0x1f
    op = FL_BAR + caplen
    rt = FL_BAR + rtsoff
    usbcmd = _rd32(op + 0x00); usbsts = _rd32(op + 0x04)
    crcr = _rd64(op + 0x18); dcbaap = _rd64(op + 0x30)
    erstsz = _rd32(rt + 0x28); erstba = _rd64(rt + 0x30); erdp = _rd64(rt + 0x38); iman = _rd32(rt + 0x20)
    hv.log(f"[evtring1] ★OP USBCMD={usbcmd:#x}(RS={usbcmd&1 if usbcmd else '?'}) USBSTS={usbsts:#x} DCBAAP={dcbaap:#x} CRCR={crcr:#x}")
    hv.log(f"[evtring1] ★RT ERSTSZ={erstsz:#x} ERSTBA={erstba:#x} ERDP={erdp:#x} IMAN={iman:#x}")
    # ERST[0] lives at ERSTBA (a DMA addr); read via low-window backing
    erst_phys = _phys_of_dma(erstba)
    if erst_phys is None:
        hv.log(f"[evtring1] ★ERSTBA {erstba:#x} not in low 4GB window -> event ring is HIGH DMA (report only)")
        return
    ring_base = _rd64(erst_phys)      # ERST[0].RingSegmentBaseAddress
    ring_size = _rd32(erst_phys + 8)  # ERST[0].RingSegmentSize (in TRBs)
    hv.log(f"[evtring1] ★ERST[0]@phys{erst_phys:#x}: ring_base(dma)={ring_base:#x} ring_size={ring_size}")
    ring_phys = _phys_of_dma(ring_base)
    if ring_phys is None:
        hv.log(f"[evtring1] ★ring_base {ring_base:#x} not in low window (high DMA) -> cannot read via backing")
        return
    ndump = min(ring_size if ring_size and ring_size < 24 else 16, 16)
    hv.log(f"[evtring1] ★EVENT RING @dma{ring_base:#x} -> phys{ring_phys:#x} (first {ndump} of {ring_size} TRBs):")
    _dump_trbs("ER", ring_phys, ndump, erdp_gpa=(erdp & ~0xfff0) if erdp else None)
    # decode: how many contiguous cyc=1 known-type TRBs from base = producer wrote & not consumed
    hv.log(f"[evtring1] ★DECIDE: if many cyc=1 XFER/CMDCOMP but ERDP stuck -> consumer desync(a); "
           f"if all cyc=0 -> FL1100 not producing(b); if junk types -> stale/coherence(c)")

hv._n = 0
hv._dumps = 0
def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._dumps < 8 and (hv._n == 1 or (hv._n % 12) == 0):
        hv._dumps += 1
        hv.log(f"[evtring1] === DUMP #{hv._dumps} (break-in {hv._n}) ===")
        try:
            _evtring_dump()
        except Exception as e:
            try: hv.log(f"[evtring1] dump err: {e}")
            except Exception: pass
        hv.log(f"[evtring1] === dump #{hv._dumps} done ===")
    return EXC_RET.HANDLED
hv.run_shell = _diag_run_shell

def _log_has(pat):
    lp = os.environ.get("NWOAS_LOG")
    if not lp: return False
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
            hv.log("[evtring1] kernel marker not seen in 25min; bursting anyway"); break
    hv.log("[evtring1] kernel detected; settling 150s then bursting to catch the livelock")
    time.sleep(150)
    for i in range(240):
        if hv._dumps >= 8: break
        try: hv.interrupt()
        except Exception: pass
        time.sleep(3)
    hv.log(f"[evtring1] breaker done ({hv._dumps} dumps, {hv._n} break-ins)")

threading.Thread(target=_breaker, daemon=True).start()
print("[evtring1] event-ring content diagnostic armed (read-only, no kmutil)")
