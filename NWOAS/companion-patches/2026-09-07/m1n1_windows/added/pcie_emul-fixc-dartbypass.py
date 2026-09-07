# pcie_emul-fixc-dartbypass.py (NWOAS §3.2 fix-C, no kmutil) — minimal-hypothesis test.
#
# ROOT (evtring1-confirmed): Windows put the xHCI DMA rings HIGH (DCBAAP/ERSTBA/ERDP ~0xae0fc_x000
# = ~46.7GB, at the very top of the 34-48GB Apple RAM band), OUTSIDE the low DART window. The FL1100
# can't DMA to those high rings via the guest-programmed apcie DART -> command completions never
# posted -> usbxhci livelocks re-resetting ALL ports -> no device ever stabilizes. (MMIO PORTSC is
# visible via BYPASS, so the driver SEES devices/laser-on but never completes enumeration.)
#
# FIX-C: put the apcie DART in BYPASS (identity, no translation) so the FL1100's DMA to bus addr X
# lands at physical X. Windows' rings at 0xae0fc9000 are real RAM at physical 0xae0fc9000 (Apple RAM
# is identity in stage-2), so BYPASS makes them reachable. Applied ONLY at the Windows-kernel stage
# (NOT at load) so UEFI's low-window DMA (boot.wim stage5, which relies on translation) is untouched.
# GENTLE break-ins (~8, not 284) to avoid the KMODE_EXCEPTION perturbation BSOD.
#
# JUDGE: mouse cursor moves = fix-C is the answer (§3.2 solved at hv level, device-agnostic, no
# Windows mod). Also confirms via: DART TCR[0]==0x1100, DART ERROR clear, and HIGH ring now shows
# valid completion TRBs (cyc=1 type XFER/CMDCOMP) after BYPASS.
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[fixc] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_phys_base = hv.u.ba.phys_base; _mem_size = hv.u.ba.mem_size
_backing = _phys_base + _mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[fixc] window IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} | RAM {_phys_base:#x}..{_phys_base+_mem_size:#x}")

FL_BAR = 0x6c0000000
APCIE_DART_BASES = (0x681008000, 0x682008000, 0x683008000)
DART_BYPASS = (1 << 8) | (1 << 12)   # 0x1100 BYPASS_DART | BYPASS_DAPF (DART_T8020 TCR)
TCR_OFF = 0x100; ENABLED_STREAMS = 0xfc
STREAM_SELECT = 0x34; STREAM_COMMAND = 0x20; INVALIDATE = 1 << 20; BUSY = 1 << 2
DART_ERROR = 0x40; DART_ERR_LO = 0x50; DART_ERR_HI = 0x54
TRB_TYPE = {32: "XFER", 33: "CMDCOMP", 34: "PORTSC", 36: "DOORBELL", 37: "HC-EVT"}

def _w32(a, v):
    try: hv.p.write32(a, v); return True
    except Exception: return False
def _r32(a):
    try: return hv.p.read32(a) & 0xffffffff
    except Exception: return None
def _r64(a):
    lo = _r32(a); hi = _r32(a + 4)
    return None if (lo is None or hi is None) else (lo | (hi << 32))

def _set_dart_bypass():
    for base in APCIE_DART_BASES:
        _w32(base + ENABLED_STREAMS, 0xffffffff)
        for s in range(16):
            _w32(base + TCR_OFF + 4 * s, DART_BYPASS)
        _w32(base + STREAM_SELECT, 0xffffffff)
        _w32(base + STREAM_COMMAND, INVALIDATE)
        for _ in range(100):
            b = _r32(base + STREAM_COMMAND)
            if b is None or not (b & BUSY): break

def _verify():
    for i, base in enumerate(APCIE_DART_BASES):
        tcr = _r32(base + TCR_OFF); err = _r32(base + DART_ERROR)
        m = f"[fixc] DART[{i}] TCR[0]={tcr:#x}(want 0x1100) ERROR={err:#x}"
        if err not in (0, 0xffffffff, None):
            lo = _r32(base + DART_ERR_LO) or 0; hi = _r32(base + DART_ERR_HI) or 0
            m += f" ★FAULT-ADDR={hi:#x}:{lo:#x}"
        hv.log(m)

def _peek_ring():
    caplen = _r32(FL_BAR) & 0xff if _r32(FL_BAR) is not None else 0
    if not caplen: hv.log("[fixc] (caplen 0, skip ring peek)"); return
    rtsoff = _r32(FL_BAR + 0x18) & ~0x1f; rt = FL_BAR + rtsoff
    erstba = _r64(rt + 0x30); erdp = _r64(rt + 0x38)
    if not erstba: hv.log("[fixc] (ERSTBA 0, skip)"); return
    # ERST[0] and ring live in Apple RAM (identity in stage-2): read physical directly.
    ring_base = _r64(erstba);
    if not ring_base: hv.log(f"[fixc] ERST@{erstba:#x} unreadable"); return
    valid = 0; nz = 0
    for j in range(8):
        st = _r32(ring_base + j * 16 + 8); ctrl = _r32(ring_base + j * 16 + 12)
        if ctrl is None: continue
        if st or ctrl: nz += 1
        if (ctrl & 1) and ((ctrl >> 10) & 0x3f) in TRB_TYPE: valid += 1
    hv.log(f"[fixc] ★RING@{ring_base:#x} ERDP={erdp:#x}: nonzero={nz}/8 validTRB={valid}/8 "
           f"-> {'FL1100 NOW PRODUCING (reachable!)' if valid else 'still empty (not reachable yet)'}")

hv._applied = False; hv._n = 0
def _rs(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if not hv._applied:
        hv._applied = True
        hv.log("[fixc] === kernel stage: applying apcie DART BYPASS (identity) ===")
        _set_dart_bypass(); _verify()
    else:
        _set_dart_bypass()   # re-assert (cheap) in case guest touched it
        if hv._n % 2 == 0:
            _verify(); _peek_ring()
    return EXC_RET.HANDLED
hv.run_shell = _rs

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
            hv.log("[fixc] kernel not seen 25min; applying anyway"); break
    hv.log("[fixc] kernel detected; settle 60s (let usbxhci init) then apply BYPASS")
    time.sleep(60)
    # GENTLE: ~8 break-ins over ~40s to set + hold BYPASS through the enumeration retry storm,
    # then STOP and let the guest run freely so the mouse can enumerate + cursor move.
    for i in range(8):
        try: hv.interrupt()
        except Exception: pass
        time.sleep(5)
    hv.log("[fixc] breaker done -- BYPASS applied, guest running free. ★WATCH THE MOUSE CURSOR.")

threading.Thread(target=_breaker, daemon=True).start()
print("[fixc] DART-BYPASS fix-C armed (applies at kernel stage; WATCH MOUSE CURSOR)")
