# pcie_emul-exp5-fldiag2.py (NWOAS §3.2 diagnostic v2) — deferred FL1100 kernel-phase register dump
# PLUS two disambiguating reads the v1 module lacked:
#   (1) FL1100 HCCPARAMS1.AC64 (BAR+0x10 bit0): does the controller advertise 64-bit DMA capability?
#       If AC64=1, Windows usbxhci may set HighestAcceptableAddress with a 64-bit constraint and place
#       the common buffer >4GB (DART-unreachable) -> candidate-B (AC64 spoof) is live. If AC64=0 the
#       controller already claims 32-bit-only and the buffer-placement story must be elsewhere.
#   (2) dwc3 (USB-C) GCTL.PRTCAPDIR (0x50228C110 bits[13:12]): 0x1000=Host 0x2000=Device 0x3000=OTG.
#       Track 2 probe: is USB-C forced into Device mode (m1n1 gadget) and thus never a host xHCI?
# Everything else is identical to pcie_emul-exp5-fldiag.py (exp5 setup, no backing-fix breaker, kernel-
# phase multi-snapshot FL1100 dump). Read via the host proxy (hv.p.read32) since the FL1100 BAR is
# BYPASS-mapped; this reports the TRUE hardware value (what Windows sees today, pre-spoof).
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[fldiag2] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0x80000000", 0)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x20000000", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[fldiag2] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000   # FL1100 ECAM: bus2 dev0 fn0
DWC3_GCTL = 0x50228C110  # USB-C dwc3 global GCTL (BAR 0x502280000 + 0xC110)

def _dwc3_gctl():
    try:
        g = hv.p.read32(DWC3_GCTL) & 0xffffffff
        m = (g >> 12) & 0x3
        name = {0: "reserved", 1: "HOST", 2: "DEVICE", 3: "OTG"}.get(m, "?")
        hv.log(f"[fldiag2] ★DWC3(USB-C) GCTL={g:#x} PRTCAPDIR={m}({name}) -- Track2: {'need Host switch' if name=='DEVICE' else name}")
    except Exception as e:
        hv.log(f"[fldiag2] dwc3 gctl read err: {e}")

def _fldump():
    rd32 = lambda a: hv.p.read32(a) & 0xffffffff
    rd64 = lambda a: rd32(a) | (rd32(a + 4) << 32)
    cmd = rd32(FL_CFG + 0x04) & 0xffff
    bar0 = rd32(FL_CFG + 0x10)
    hv.log(f"[fldiag2] ★FL1100 CFG: CMD={cmd:#06x}(MEM={(cmd>>1)&1} BM={(cmd>>2)&1}) BAR0={bar0:#x}")
    if not ((cmd >> 1) & 1):
        hv.log("[fldiag2] ★CMD.MEM=0 -> FL1100 MMIO decode OFF. §3.2=controller-not-started.")
        return
    cap0 = rd32(FL_BAR); caplen = cap0 & 0xff
    hccp1 = rd32(FL_BAR + 0x10)   # HCCPARAMS1
    ac64 = hccp1 & 1
    csz = (hccp1 >> 2) & 1
    hv.log(f"[fldiag2] ★HCCPARAMS1={hccp1:#x} AC64={ac64}({'64bit-DMA-capable=>Windows may place buf>4GB' if ac64 else '32bit-only'}) CSZ={csz}")
    op = FL_BAR + caplen
    rtsoff = rd32(FL_BAR + 0x18) & ~0x1f; rt = FL_BAR + rtsoff
    hcs1 = rd32(FL_BAR + 0x04); nports = (hcs1 >> 24) & 0xff
    usbcmd = rd32(op + 0x00); usbsts = rd32(op + 0x04)
    dcbaap = rd64(op + 0x30); crcr = rd64(op + 0x18); config = rd32(op + 0x38)
    erstsz = rd32(rt + 0x28); erstba = rd64(rt + 0x30); erdp = rd64(rt + 0x38); iman = rd32(rt + 0x20)
    hv.log(f"[fldiag2] ★OP: USBCMD={usbcmd:#x}(RS={usbcmd&1} INTE={(usbcmd>>2)&1}) USBSTS={usbsts:#x}(HCH={usbsts&1} HSE={(usbsts>>2)&1} EINT={(usbsts>>3)&1}) caplen={caplen:#x} nports={nports}")
    hv.log(f"[fldiag2] ★DCBAAP={dcbaap:#x} CRCR={crcr:#x} CONFIG={config:#x}")
    hv.log(f"[fldiag2] ★ERST: sz={erstsz:#x} ERSTBA={erstba:#x} ERDP={erdp:#x} IMAN={iman:#x}(IP={iman&1} IE={(iman>>1)&1})")
    for n in range(min(max(nports, 1), 4)):
        ps = rd32(op + 0x400 + n * 0x10)
        hv.log(f"[fldiag2] ★PORTSC[{n}]={ps:#x}(CCS={ps&1} PED={(ps>>1)&1} PLS={(ps>>5)&0xf} PP={(ps>>9)&1} CSC={(ps>>17)&1})")
    for nm, v in (("DCBAAP", dcbaap), ("ERSTBA", erstba)):
        loc = "HIGH>4GB(UNREACHABLE by 32bit FL1100!)" if v >= 0x100000000 else ("WINDOW<4GB(reachable)" if v else "UNSET")
        hv.log(f"[fldiag2] ★{nm} placement: {v:#x} = {loc}")

hv._fldiag_n = 0
hv._fldiag_dumps = 0
def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    hv._fldiag_n += 1
    if hv._fldiag_dumps < 5 and (hv._fldiag_n == 1 or (hv._fldiag_n % 20) == 0):
        hv._fldiag_dumps += 1
        hv.log(f"[fldiag2] === DUMP #{hv._fldiag_dumps} (break-in {hv._fldiag_n}) ===")
        try:
            if hv._fldiag_dumps == 1:
                _dwc3_gctl()   # Track2 probe on first dump only
            _fldump()
        except Exception as e:
            try: hv.log(f"[fldiag2] dump err: {e}")
            except Exception: pass
        hv.log(f"[fldiag2] === dump #{hv._fldiag_dumps} done ===")
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
            hv.log("[fldiag2] kernel marker not seen in 25min; bursting anyway")
            break
    hv.log("[fldiag2] kernel detected; settling 180s then bursting")
    time.sleep(180)
    for i in range(140):
        if hv._fldiag_dumps >= 5:
            break
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(3)
    hv.log(f"[fldiag2] breaker done ({hv._fldiag_dumps} dumps, {hv._fldiag_n} break-ins)")

threading.Thread(target=_breaker, daemon=True).start()
print("[fldiag2] FL1100 kernel-phase dump module (v2: +AC64 +GCTL) armed")
