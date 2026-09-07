# pcie_emul-exp5-bmeprobe.py (NWOAS §6-C reachability, PYTHON, kmutil-FREE) — stage 2.
# dartprobe proved: DART unlocked, TCR BYPASS *takes and persists* yet FL1100 still writes NOTHING
# to the event ring -> reachability failure is BEYOND the DART TCR. Leading hypothesis: FL1100 is
# not DMA-mastering (CMD.BME=bit2 clear) -> MMIO works (MSE=1) but no DMA at all, so bypass can't
# help. Test, on installed v1 via gentle break-ins:
#   stage1 baseline: FL1100 CMD (BME?), DART state(x3), event ring, command ring.
#   stage2 FORCE: set CMD.BME; BYPASS all 3 DARTs (all streams); CLEAR all DART ERRs; TLB-inval.
#   stage3..: recheck. KEY DISCRIMINATOR after the fix:
#     * DART ERR GROWS again  -> FL1100 IS attempting DMA but faulting (address/xlate) -> not BME
#     * ERR stays clear + ring still empty -> FL1100 NOT attempting -> BME/command/doorbell path
#     * ring POPULATES -> reachability FIXED (BME was it) -> cursor should move
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[bmep] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[bmep] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000
DARTS = [0x681008000, 0x682008000, 0x683008000]
TRB_TYPE = {32, 33, 34, 35, 36, 37, 38, 39}
PTE_TARGET = ((1 << 50) - 1) & ~((1 << 14) - 1)
CFG = 0x60; TCR = 0x100; ENABLED = 0xfc; ERR = 0x40; ERRA = 0x50
SEL = 0x34; SCMD = 0x20; INVAL = 1 << 20; BUSY = 1 << 2
BYPASS = 0x1100; CFG_LOCK = 1 << 15


def _rd32(a):
    try:
        return p.read32(a) & 0xffffffff
    except Exception:
        return None


def _rd64(a):
    lo = _rd32(a); hi = _rd32(a + 4)
    return None if lo is None or hi is None else lo | (hi << 32)


def _h(x):
    return f"{x:#x}" if x is not None else "None"


def _ipa_to_pa(ipa):
    if ipa is None:
        return None
    try:
        pte = p.hv_pt_walk(ipa & ~0x3fff)
        if pte:
            return (pte & PTE_TARGET) | (ipa & 0x3fff)
    except Exception:
        pass
    return _backing + (ipa & 0xffffffff) if (ipa >> 32) == 0 else None


def _cmd():
    c = _rd32(FL_CFG + 0x04)
    return c


def _darts(tag):
    for b in DARTS:
        cfg = _rd32(b + CFG); err = _rd32(b + ERR); t1 = _rd32(b + TCR + 4)
        byp = "BYPASS" if t1 == BYPASS else "translate"
        hv.log(f"[bmep] {tag} DART {b:#x}: CFG={_h(cfg)} ERR={_h(err)} TCR1={_h(t1)}({byp})")


def _ring(tag):
    caplen = _rd32(FL_BAR) & 0xff
    op = FL_BAR + caplen; rt = FL_BAR + (_rd32(FL_BAR + 0x18) & ~0x1f)
    usbcmd = _rd32(op); erstba = _rd64(rt + 0x30); erdp = _rd64(rt + 0x38); dcbaap = _rd64(op + 0x30)
    crcr = _rd64(op + 0x18)
    hv.log(f"[bmep] {tag} USBCMD={_h(usbcmd)} DCBAAP={_h(dcbaap)} ERSTBA={_h(erstba)} ERDP={_h(erdp)} CRCR={_h(crcr)}")
    if not erstba:
        return
    ep = _ipa_to_pa(erstba & ~0x3f)
    if ep is None:
        return
    try:
        p.dc_civac(ep, 16)
    except Exception:
        pass
    ring_ipa = _rd64(ep)
    if not ring_ipa:
        hv.log(f"[bmep] {tag} ring not programmed"); return
    rp = _ipa_to_pa(ring_ipa & ~0x3f)
    if rp is None:
        return
    try:
        p.dc_ivac(rp, 16 * 16)
    except Exception:
        pass
    valid = 0; firstnz = None
    for i in range(16):
        ctrl = _rd32(rp + i * 16 + 12)
        if ctrl:
            if firstnz is None:
                firstnz = (i, ctrl)
            if (ctrl & 1) and (((ctrl >> 10) & 0x3f) in TRB_TYPE):
                valid += 1
    hv.log(f"[bmep] {tag} EVENT RING pa={rp:#x}: valid TRBs={valid}/16 firstNZ={firstnz} "
           f"=> {'★POPULATED (reach FIXED)' if valid else 'empty'}")


def _force():
    # (1) set CMD.BME (bus master enable, bit2) + keep MSE
    c = _cmd()
    hv.log(f"[bmep] FL1100 CMD before = {_h(c)} (MSE={ (c>>1)&1 if c is not None else '?'} BME={(c>>2)&1 if c is not None else '?'})")
    if c is not None:
        try:
            p.write32(FL_CFG + 0x04, c | 0x6)  # MSE|BME
        except Exception as e:
            hv.log(f"[bmep]   CMD write err: {e}")
        c2 = _cmd()
        hv.log(f"[bmep] FL1100 CMD after  = {_h(c2)} (BME={(c2>>2)&1 if c2 is not None else '?'})")
    # (2) bypass ALL 3 DARTs, clear ERR, TLB-inval
    for b in DARTS:
        try:
            e0 = _rd32(b + ERR)
            p.write32(b + ENABLED, 0xffffffff)
            for s in range(16):
                p.write32(b + TCR + 4 * s, BYPASS)
            if e0:
                p.write32(b + ERR, e0)   # write-1-to-clear the latched error
            p.write32(b + SEL, 0xffffffff)
            p.write32(b + SCMD, INVAL)
            for _ in range(200):
                if not (p.read32(b + SCMD) & BUSY):
                    break
            hv.log(f"[bmep] FORCED bypass+clear {b:#x}: TCR1={_h(_rd32(b+TCR+4))} ERR:{_h(e0)}->{_h(_rd32(b+ERR))}")
        except Exception as e:
            hv.log(f"[bmep]   dart {b:#x} force err: {e}")


hv._n = 0; hv._st = 0
MAX = 8


def _shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._st >= MAX:
        return EXC_RET.HANDLED
    hv._st += 1
    st = hv._st
    c = _cmd()
    if c is None or not ((c >> 1) & 1):
        hv.log(f"[bmep] #{st}: CMD={_h(c)} MSE off; skip"); return EXC_RET.HANDLED
    hv.log(f"[bmep] ==== BREAK-IN #{hv._n} stage#{st} @{time.strftime('%H:%M:%S')} CMD={_h(c)}(BME={(c>>2)&1}) ====")
    try:
        if st == 1:
            _darts("BASE"); _ring("BASE")
        elif st == 2:
            _force(); _darts("POST"); _ring("POST")
        else:
            _ring(f"RE{st}"); _darts(f"RE{st}")
    except Exception as e:
        try: hv.log(f"[bmep] stage#{st} err: {e}")
        except Exception: pass
    hv.log(f"[bmep] ==== stage#{st} done ====")
    return EXC_RET.HANDLED


hv.run_shell = _shell


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
        if t > 1800:
            break
    hv.log("[bmep] kernel detected; settle 120s then staged break-ins (cap 8, 15s apart)")
    time.sleep(120)
    tries = 0
    while hv._st < MAX and tries < 14:
        tries += 1
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(15)
    hv.log(f"[bmep] breaker done ({hv._st} stages, {hv._n} break-ins)")


threading.Thread(target=_breaker, daemon=True).start()
