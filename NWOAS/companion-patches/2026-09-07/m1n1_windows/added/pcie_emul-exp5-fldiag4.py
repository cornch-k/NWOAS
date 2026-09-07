# pcie_emul-exp5-fldiag4.py (NWOAS §3.2 wide-DART diagnostic) — fldiag3 + apcie DART ERROR read +
# corrected DCBAAP placement labels for the wide-DART build (where a HIGH DCBAAP in guest RAM is now
# REACHABLE via the DART identity map, not unreachable). Keeps the low-window map_hw alias (the minimal
# wide-DART is additive: window stays for bootmgfw). Reads are host-side EL2 proxy (hv.p/hv.iface),
# READ-ONLY.
#   PASS (wide-DART wins): DCBAAP != 0, value inside [phys_base, backing) (= advertised high RAM,
#     DART-identity-mapped) OR inside the [0,4GB) window, AND all 3 apcie DART ERROR.FLAG == 0, AND
#     USBCMD.RS flips 1. (Enable Slot / port enumerate still needed for real USB.)
#   FAIL (missing-adapter branch): DCBAAP stays 0 -> geometry/ceiling was not the wall; pivot.
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[fldiag4] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0x80000000", 0)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x20000000", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_phys_base = hv.u.ba.phys_base
_mem_size = hv.u.ba.mem_size
_backing = _phys_base + _mem_size - _ws - _sk
_ram_top = _phys_base + _mem_size            # true top (backing carve is below this)
_adv_top = _phys_base + _mem_size - _ws       # advertised high-RAM top (== mApcieBackingPa target)
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[fldiag4] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} | phys_base={_phys_base:#x} mem_size={_mem_size:#x} adv_top={_adv_top:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000
DWC3_GCTL = 0x50228C110
APCIE_DART_BASES = (0x681008000, 0x682008000, 0x683008000)
DART_ERROR = 0x40
DART_ERROR_ADDR_LO = 0x50
DART_ERROR_ADDR_HI = 0x54

def _dart_errors():
    for i, b in enumerate(APCIE_DART_BASES):
        try:
            e = hv.p.read32(b + DART_ERROR) & 0xffffffff
            flag = (e >> 31) & 1
            if e not in (0, 0xffffffff):
                lo = hv.p.read32(b + DART_ERROR_ADDR_LO) & 0xffffffff
                hi = hv.p.read32(b + DART_ERROR_ADDR_HI) & 0xffffffff
                hv.log(f"[fldiag4] ★DART[{i}]@{b:#x} ERROR={e:#x}(FLAG={flag}) ADDR={hi:#x}:{lo:#x}")
            else:
                hv.log(f"[fldiag4] DART[{i}]@{b:#x} ERROR={e:#x} (clear)")
        except Exception as ex:
            hv.log(f"[fldiag4] DART[{i}] err read fail: {ex}")

def _place(v):
    if v == 0:
        return "UNSET"
    if v < 0x100000000:
        return "WINDOW<4GB(reachable via window->backing)"
    if _phys_base <= v < _adv_top:
        return "HIGH-RAM(reachable via WIDE-DART identity!)"
    if _adv_top <= v < _ram_top:
        return "BACKING-CARVE(>adv_top, likely UNMAPPED to device!)"
    return "HIGH>advertised(UNMAPPED/UNREACHABLE?)"

def _dwc3_gctl():
    try:
        g = hv.p.read32(DWC3_GCTL) & 0xffffffff
        m = (g >> 12) & 0x3
        hv.log(f"[fldiag4] ★DWC3 GCTL={g:#x} PRTCAPDIR={m}({ {0:'rsvd',1:'HOST',2:'DEVICE',3:'OTG'}.get(m) })")
    except Exception as e:
        hv.log(f"[fldiag4] dwc3 gctl err: {e}")

def _fldump():
    rd32 = lambda a: hv.p.read32(a) & 0xffffffff
    rd64 = lambda a: rd32(a) | (rd32(a + 4) << 32)
    cmd = rd32(FL_CFG + 0x04) & 0xffff
    hv.log(f"[fldiag4] ★FL1100 CFG: CMD={cmd:#06x}(MEM={(cmd>>1)&1} BM={(cmd>>2)&1})")
    if not ((cmd >> 1) & 1):
        hv.log("[fldiag4] ★CMD.MEM=0 -> decode OFF. controller-not-started.")
        _dart_errors(); return
    cap0 = rd32(FL_BAR); caplen = cap0 & 0xff
    hccp1 = rd32(FL_BAR + 0x10); ac64 = hccp1 & 1
    op = FL_BAR + caplen
    rtsoff = rd32(FL_BAR + 0x18) & ~0x1f; rt = FL_BAR + rtsoff
    usbcmd = rd32(op + 0x00); usbsts = rd32(op + 0x04)
    dcbaap = rd64(op + 0x30); crcr = rd64(op + 0x18); config = rd32(op + 0x38)
    erstba = rd64(rt + 0x30); iman = rd32(rt + 0x20)
    hv.log(f"[fldiag4] ★HCCPARAMS1={hccp1:#x} AC64={ac64} | USBCMD={usbcmd:#x}(RS={usbcmd&1} INTE={(usbcmd>>2)&1}) USBSTS={usbsts:#x}(HCH={usbsts&1} HSE={(usbsts>>2)&1})")
    hv.log(f"[fldiag4] ★DCBAAP={dcbaap:#x} CRCR={crcr:#x} CONFIG={config:#x} ERSTBA={erstba:#x} IMAN={iman:#x}")
    hv.log(f"[fldiag4] ★DCBAAP placement: {dcbaap:#x} = {_place(dcbaap)}")
    hv.log(f"[fldiag4] ★ERSTBA placement: {erstba:#x} = {_place(erstba)}")
    for n in range(4):
        ps = rd32(op + 0x400 + n * 0x10)
        hv.log(f"[fldiag4] ★PORTSC[{n}]={ps:#x}(CCS={ps&1} PED={(ps>>1)&1} PLS={(ps>>5)&0xf})")
    _dart_errors()
    # verdict hint
    if dcbaap != 0 and usbcmd & 1:
        hv.log("[fldiag4] ★★VERDICT-HINT: DCBAAP!=0 AND RS=1 -> §3.2 progress! confirm DART ERR clear + Enable Slot.")
    elif dcbaap != 0:
        hv.log("[fldiag4] ★VERDICT-HINT: DCBAAP!=0 but RS=0 -> alloc succeeded, controller not yet running (check next dumps).")
    else:
        hv.log("[fldiag4] ★VERDICT-HINT: DCBAAP=0 -> wide-DART did NOT move it -> missing-adapter branch (geometry not the wall).")

hv._fldiag_n = 0
hv._fldiag_dumps = 0
def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    hv._fldiag_n += 1
    if hv._fldiag_dumps < 6 and (hv._fldiag_n == 1 or (hv._fldiag_n % 15) == 0):
        hv._fldiag_dumps += 1
        hv.log(f"[fldiag4] === DUMP #{hv._fldiag_dumps} (break-in {hv._fldiag_n}) ===")
        try:
            if hv._fldiag_dumps == 1:
                _dwc3_gctl()
            _fldump()
        except Exception as e:
            try: hv.log(f"[fldiag4] dump err: {e}")
            except Exception: pass
        hv.log(f"[fldiag4] === dump #{hv._fldiag_dumps} done ===")
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
            hv.log("[fldiag4] kernel marker not seen in 25min; bursting anyway"); break
    hv.log("[fldiag4] kernel detected; settling 180s then bursting")
    time.sleep(180)
    for i in range(180):
        if hv._fldiag_dumps >= 6: break
        try: hv.interrupt()
        except Exception: pass
        time.sleep(3)
    hv.log(f"[fldiag4] breaker done ({hv._fldiag_dumps} dumps, {hv._fldiag_n} break-ins)")

threading.Thread(target=_breaker, daemon=True).start()
print("[fldiag4] wide-DART FL1100 dump (+DART ERROR +corrected placement) armed")
