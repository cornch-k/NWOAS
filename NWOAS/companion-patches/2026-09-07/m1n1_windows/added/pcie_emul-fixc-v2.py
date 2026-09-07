# pcie_emul-fixc-v2.py (NWOAS §3.2 fix-C, clean) — v1 was inconclusive (flooded re-assert, poison
# high-RAM reads, guest killed early). v2 is disciplined:
#   * apply apcie DART BYPASS EXACTLY ONCE at the Windows-kernel stage (flag-guarded).
#   * CLEAR the DART ERROR latches right after (MMIO W1C -> reads are reliable, no poison).
#   * then STOP touching things and let the guest RUN FREE for a long cursor-observation window.
#   * at the very end, ONE gentle check: did NEW DART errors re-appear?
#       - no new errors  -> FL1100 now reaches the HIGH rings (reachability was the wall); if the
#                           cursor STILL doesn't move, the residual is cache COHERENCE (stale ring
#                           reads), which DART BYPASS does not fix.
#       - new errors     -> BYPASS didn't take / wrong DART -> reachability still blocked.
# NO high-RAM proxy reads (0xabad1dea poison). NO per-break-in re-assert. Minimal break-ins.
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[fcv2] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_pb = hv.u.ba.phys_base; _ms = hv.u.ba.mem_size
hv.map_hw(_wb, _pb + _ms - _ws - _sk, _ws)

APCIE = (0x681008000, 0x682008000, 0x683008000)
DART_BYPASS = (1 << 8) | (1 << 12)   # 0x1100
TCR_OFF = 0x100; EN = 0xfc; SEL = 0x34; CMD = 0x20; INVAL = 1 << 20; BUSY = 1 << 2
ERR = 0x40; ERR_LO = 0x50; ERR_HI = 0x54

def _w(a, v):
    try: hv.p.write32(a, v)
    except Exception: pass
def _r(a):
    try: return hv.p.read32(a) & 0xffffffff
    except Exception: return None

def _apply_bypass_and_clear():
    for b in APCIE:
        _w(b + EN, 0xffffffff)
        for s in range(16):
            _w(b + TCR_OFF + 4 * s, DART_BYPASS)
        _w(b + SEL, 0xffffffff); _w(b + CMD, INVAL)
        for _ in range(100):
            v = _r(b + CMD)
            if v is None or not (v & BUSY): break
    # clear latched errors (W1C: write the read-back value back)
    for b in APCIE:
        e = _r(b + ERR)
        if e:
            _w(b + ERR, e)          # W1C
            _w(b + ERR_LO, 0xffffffff); _w(b + ERR_HI, 0xffffffff)
    for i, b in enumerate(APCIE):
        tcr = _r(b + TCR_OFF); e = _r(b + ERR)
        hv.log(f"[fcv2] DART[{i}] TCR[0]={tcr:#x}(want 0x1100) ERROR-after-clear={e:#x}")

def _check_new_errors(tag):
    any_err = False
    for i, b in enumerate(APCIE):
        e = _r(b + ERR)
        if e not in (0, 0xffffffff, None):
            any_err = True
            lo = _r(b + ERR_LO) or 0; hi = _r(b + ERR_HI) or 0
            hv.log(f"[fcv2] {tag} DART[{i}] ★NEW-ERROR={e:#x} FAULT={hi:#x}:{lo:#x}")
        else:
            hv.log(f"[fcv2] {tag} DART[{i}] ERROR={e:#x} (clear)")
    hv.log(f"[fcv2] {tag} VERDICT: {'NEW FAULTS -> BYPASS not effective / still blocked' if any_err else 'NO new faults -> FL1100 reaches rings (reachability OK; if no cursor -> COHERENCE residual)'}")

hv._applied = False
hv._phase = 0   # 0=apply, 1=final-check
def _rs(entry_msg="", exit_msg="", **kw):
    if hv._phase == 0 and not hv._applied:
        hv._applied = True
        hv.log("[fcv2] === kernel stage: apply DART BYPASS ONCE + clear errors ===")
        _apply_bypass_and_clear()
        hv.log("[fcv2] === applied; guest now RUNS FREE. ★WATCH CURSOR for ~90s ===")
    elif hv._phase == 1:
        hv.log("[fcv2] === final DART-error re-check (after free-run cursor window) ===")
        _check_new_errors("FINAL")
        hv._phase = 2
    return EXC_RET.HANDLED
hv.run_shell = _rs

def _log_has(pat):
    lp = os.environ.get("NWOAS_LOG")
    if not lp: return False
    try:
        with open(lp, "r", errors="ignore") as f: return pat in f.read()
    except Exception: return False

def _land(n):
    # land ~1 break-in with a tiny burst (interrupt() no-ops while in-handler)
    for _ in range(n):
        try: hv.interrupt()
        except Exception: pass
        time.sleep(1.5)

def _breaker():
    t = 0
    while not _log_has("elr=0xfffff8"):
        time.sleep(5); t += 5
        if t > 1500:
            hv.log("[fcv2] kernel not seen 25min; applying anyway"); break
    hv.log("[fcv2] kernel; settle 60s then apply BYPASS once")
    time.sleep(60)
    _land(6)                     # apply window (idempotent; lands ~1)
    hv.log("[fcv2] BYPASS applied -> FREE-RUN 90s (WATCH CURSOR)")
    time.sleep(90)               # ★ clean cursor-observation window, guest fully free
    hv._phase = 1
    _land(6)                     # final error re-check
    hv.log("[fcv2] breaker done; guest still running (not killed) -- keep watching cursor")

threading.Thread(target=_breaker, daemon=True).start()
print("[fcv2] fix-C v2 armed (apply-once + clear + free-run cursor window + final fault check)")
