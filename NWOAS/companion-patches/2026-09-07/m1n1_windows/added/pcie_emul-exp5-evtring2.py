# pcie_emul-exp5-evtring2.py (NWOAS §3.2 root-confirm v2) — evtring1 found DCBAAP/ERSTBA/ERDP
# all at HIGH physical (~0xae0fc_x000, ~46.7GB), OUTSIDE the low 4GB DART window. 0xAE0FC8000 is
# the exact addr [[exp5-hse-root]] flagged as FL1100-DMA-unreachable. HYPOTHESIS(b): Windows put
# the xHCI DMA rings HIGH; FL1100 can't reach them via apcie DART -> no completion events -> the
# universal enumeration livelock. UEFI worked (stage5) because its buffers were LOW/reachable.
#
# THIS v2 (read-only, no kmutil) DECIDES it:
#   1. read ERSTBA/ERDP/ring DIRECTLY as physical (high GPA==HPA identity for Apple RAM), dump the
#      event-ring TRBs: if FL1100 wrote completions there -> reachable; if all-zero/stale -> NOT.
#   2. read apcie DART ERROR regs: if ERROR.addr == the high ring addr -> SMOKING GUN (FL1100
#      faulted trying to DMA the ring at that unreachable high address).
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[evtring2] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_phys_base = hv.u.ba.phys_base
_mem_size = hv.u.ba.mem_size
_backing = _phys_base + _mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[evtring2] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} | phys_base={_phys_base:#x} mem_size={_mem_size:#x} ramtop={_phys_base+_mem_size:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000
APCIE_DART_BASES = (0x681008000, 0x682008000, 0x683008000)
DART_ERROR = 0x40; DART_ERR_ADDR_LO = 0x50; DART_ERR_ADDR_HI = 0x54
TRB_TYPE = {32: "XFER-EVT", 33: "CMDCOMP", 34: "PORTSC-CHG", 35: "BW-REQ", 36: "DOORBELL",
            37: "HC-EVT", 38: "DEV-NOTIF", 39: "MFINDEX-WRAP"}

def _rd32(a):
    try: return hv.p.read32(a) & 0xffffffff
    except Exception: return None
def _rd64(a):
    lo = _rd32(a); hi = _rd32(a + 4)
    return None if (lo is None or hi is None) else (lo | (hi << 32))

def _phys_of(gpa):
    # Apple RAM is identity (GPA==HPA); low-window GPA is backed by _backing+gpa.
    if gpa is None: return None
    if _phys_base <= gpa < (_phys_base + _mem_size): return gpa
    if gpa < 0x100000000: return _backing + gpa
    return None

def _dart_errors(tag):
    for i, b in enumerate(APCIE_DART_BASES):
        e = _rd32(b + DART_ERROR)
        if e is None: continue
        if e not in (0, 0xffffffff):
            lo = _rd32(b + DART_ERR_ADDR_LO) or 0; hi = _rd32(b + DART_ERR_ADDR_HI) or 0
            hv.log(f"[evtring2] ★★DART[{i}]@{b:#x} ERROR={e:#x}(FLAG={(e>>31)&1}) FAULT-ADDR={hi:#x}:{lo:#x}  <-- {tag}")
        else:
            hv.log(f"[evtring2] DART[{i}] ERROR={e:#x} (clear)")

def _dump_ring(erstba, erdp):
    erst_phys = _phys_of(erstba)
    if erst_phys is None:
        hv.log(f"[evtring2] ERSTBA {erstba:#x} not resolvable"); return
    ring_base = _rd64(erst_phys); ring_size = _rd32(erst_phys + 8)
    hv.log(f"[evtring2] ★ERST[0]@phys{erst_phys:#x}: ring_base={ring_base:#x} ring_size={ring_size}")
    ring_phys = _phys_of(ring_base)
    if ring_phys is None:
        hv.log(f"[evtring2] ring_base {ring_base:#x} not resolvable"); return
    erdp_ptr = (erdp & ~0xf) if erdp else 0
    valid = 0; nz = 0
    n = 16
    hv.log(f"[evtring2] ★EVENT RING @{ring_base:#x} -> phys{ring_phys:#x}, ERDP->{erdp_ptr:#x}:")
    for i in range(n):
        a = ring_phys + i * 16
        param = _rd64(a); status = _rd32(a + 8); ctrl = _rd32(a + 12)
        if ctrl is None: continue
        cyc = ctrl & 1; ttype = (ctrl >> 10) & 0x3f
        cc = (status >> 24) & 0xff if status is not None else 0
        if (param or status or ctrl): nz += 1
        tn = TRB_TYPE.get(ttype, f"?{ttype}")
        if cyc == 1 and ttype in TRB_TYPE: valid += 1
        mark = " <==ERDP" if (ring_base + i*16) == erdp_ptr else ""
        hv.log(f"[evtring2]   ER[{i:2d}]@{a:#x}: p={param:#018x} st={status:#010x}(CC={cc}) c={ctrl:#010x} cyc={cyc} {tn}{mark}")
    hv.log(f"[evtring2] ★RING SUMMARY: nonzero={nz}/{n} valid(cyc1,known)={valid}/{n}  "
           f"-> {'FL1100 IS producing completions (reachable)' if valid else ('ring NONZERO but no valid TRB (cycle/coherence)' if nz else 'ring ALL-ZERO = FL1100 NOT writing (UNREACHABLE / mechanism-b)')}")

def _dump():
    cmd = _rd32(FL_CFG + 0x04)
    if cmd is None or not ((cmd >> 1) & 1):
        hv.log(f"[evtring2] CMD={cmd} decode off; skip"); _dart_errors("decode-off"); return
    caplen = _rd32(FL_BAR) & 0xff; rtsoff = _rd32(FL_BAR + 0x18) & ~0x1f
    op = FL_BAR + caplen; rt = FL_BAR + rtsoff
    usbcmd = _rd32(op + 0x00); usbsts = _rd32(op + 0x04)
    dcbaap = _rd64(op + 0x30); crcr = _rd64(op + 0x18)
    erstba = _rd64(rt + 0x30); erdp = _rd64(rt + 0x38); iman = _rd32(rt + 0x20)
    hv.log(f"[evtring2] ★USBCMD={usbcmd:#x}(RS={usbcmd&1 if usbcmd is not None else '?'}) USBSTS={usbsts:#x}(HCH={usbsts&1 if usbsts is not None else '?'}) DCBAAP={dcbaap:#x} CRCR={crcr:#x}")
    hv.log(f"[evtring2] ★ERSTBA={erstba:#x} ERDP={erdp:#x} IMAN={iman:#x}")
    _dump_ring(erstba, erdp)
    _dart_errors("post-ring-read")

hv._n = 0; hv._dumps = 0
def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._dumps < 8 and (hv._n == 1 or (hv._n % 12) == 0):
        hv._dumps += 1
        hv.log(f"[evtring2] === DUMP #{hv._dumps} (break-in {hv._n}) ===")
        try: _dump()
        except Exception as e:
            try: hv.log(f"[evtring2] dump err: {e}")
            except Exception: pass
        hv.log(f"[evtring2] === dump #{hv._dumps} done ===")
    return EXC_RET.HANDLED
hv.run_shell = _diag_run_shell

def _log_has(pat):
    lp = os.environ.get("NWOAS_LOG")
    if not lp: return False
    try:
        with open(lp, "r", errors="ignore") as f: return pat in f.read()
    except Exception: return False

def _breaker():
    t = 0
    while not _log_has("elr=0xfffff8"):
        time.sleep(5); t += 5
        if t > 1500:
            hv.log("[evtring2] kernel marker not seen 25min; bursting"); break
    hv.log("[evtring2] kernel detected; settling 150s then bursting")
    time.sleep(150)
    for i in range(240):
        if hv._dumps >= 8: break
        try: hv.interrupt()
        except Exception: pass
        time.sleep(3)
    hv.log(f"[evtring2] breaker done ({hv._dumps} dumps, {hv._n} break-ins)")

threading.Thread(target=_breaker, daemon=True).start()
print("[evtring2] event-ring reachability diagnostic armed (reads high ring + DART errors)")
