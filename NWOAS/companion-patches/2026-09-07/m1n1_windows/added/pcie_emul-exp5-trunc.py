# pcie_emul-exp5-trunc.py (NWOAS §6-C) — READ-ONLY. The C DART fix correctly identity-maps the
# event ring (dartcheck: IDENTITY-OK, TCR=translate) yet the ring stays empty. Test the two live
# theories for "DART maps it but FL1100 still doesn't land the write":
#   (T) 40-bit address TRUNCATION: FL1100/apcie writes to iova&0xffffffff (e.g. 0xae0fc8000 ->
#       0xe0fc8000) which the DART's flat low-L2 maps somewhere else -> the real ring stays 0. We
#       walk the DART for the truncated IOVA and read TRBs there.
#   (F) DART FAULT: FL1100 IS attempting but the DART rejects it -> DART ERROR FLAG(bit31)=1 + a
#       fault address. We read ERROR(0x40)+ERRADDR(0x50/0x54).
# If neither: FL1100 isn't attempting the DMA at all (upstream: no event generated / controller state).
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[trc] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
hv.map_hw(0, _backing, _ws)
hv.log(f"[trc] backing={_backing:#x}")

FL_BAR = 0x6c0000000
FL_DART = 0x682008000
PTE_ADDR = 0x000000FFFFFFC000


def _r32(a):
    try:
        return p.read32(a) & 0xffffffff
    except Exception:
        return None


def _r64(a):
    lo = _r32(a); hi = _r32(a + 4)
    return None if lo is None or hi is None else lo | (hi << 32)


def _h(x):
    return f"{x:#x}" if x is not None else "None"


def _dart_pa(l1phys, iova):
    # translate iova through the DART page tables (read-only) -> host PA, or None
    l1i = (iova >> 25) & 0x7ff
    l1e = _r64(l1phys + 8 * l1i)
    if not l1e or not (l1e & 1):
        return None, f"L1[{l1i}]={_h(l1e)} INVALID"
    l2e = _r64((l1e & PTE_ADDR) + 8 * ((iova >> 14) & 0x7ff))
    if not l2e or not (l2e & 1):
        return None, f"L2={_h(l2e)} INVALID"
    return (l2e & PTE_ADDR) | (iova & 0x3fff), f"L2={_h(l2e)}"


def _trbs(pa, tag):
    if pa is None:
        hv.log(f"[trc]   {tag}: no PA"); return
    nz = 0; first = None
    for i in range(16):
        c = _r32(pa + i * 16 + 12)
        if c:
            nz += 1
            if first is None:
                first = (i, c, (c >> 10) & 0x3f, c & 1)
    hv.log(f"[trc]   {tag} pa={pa:#x}: nonzero={nz}/16 first={first}")


def _check():
    caplen = (_r32(FL_BAR) or 0) & 0xff
    op = FL_BAR + caplen
    rt = FL_BAR + ((_r32(FL_BAR + 0x18) or 0) & ~0x1f)
    erstba = _r64(rt + 0x30)
    ttbr = _r32(FL_DART + 0x200 + 16)
    l1phys = ((ttbr & 0x7fffffff) << 12) if (ttbr and (ttbr & 0x80000000)) else 0
    # DART ERROR: FLAG(bit31)=real fault
    err = _r32(FL_DART + 0x40); ea = _r64(FL_DART + 0x50)
    flag = (err >> 31) & 1 if err is not None else "?"
    hv.log(f"[trc] DART ERR={_h(err)} FLAG={flag} ERRADDR={_h(ea)} l1phys={_h(l1phys)} ERSTBA={_h(erstba)}")
    if not l1phys or not erstba:
        return
    # real ring
    ep = erstba if (erstba >> 32) else (_backing + (erstba & 0xffffffff))
    ring = (_r64(ep) or 0) & ~0x3f
    if not ring:
        hv.log("[trc]   ring not set"); return
    rpa, rt_ = _dart_pa(l1phys, ring)
    hv.log(f"[trc]   REAL ring iova={ring:#x} dart:{rt_}")
    _trbs(rpa if rpa else (ring if (ring >> 32) else None), "REAL")
    # truncated ring (T hypothesis): iova & 0xffffffff
    tr = ring & 0xffffffff
    tpa, tt_ = _dart_pa(l1phys, tr)
    hv.log(f"[trc]   TRUNC ring iova={tr:#x} dart:{tt_}")
    _trbs(tpa, "TRUNC")


hv._n = 0; hv._d = 0
MAX = 4


def _shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._d >= MAX:
        return EXC_RET.HANDLED
    hv._d += 1
    hv.log(f"[trc] ==== BREAK-IN #{hv._n} @{time.strftime('%H:%M:%S')} ====")
    try:
        _check()
    except Exception as e:
        try: hv.log(f"[trc] err: {e}")
        except Exception: pass
    hv.log("[trc] ==== done ====")
    return EXC_RET.HANDLED


hv.run_shell = _shell


def _log_has(pat):
    try:
        with open(os.environ.get("NWOAS_LOG"), "r", errors="ignore") as f:
            return pat in f.read()
    except Exception:
        return False


def _breaker():
    t = 0
    while not _log_has("elr=0xfffff8"):
        time.sleep(5); t += 5
        if t > 1800:
            break
    hv.log("[trc] kernel detected; settle 120s then read-only break-ins (cap 4)")
    time.sleep(120)
    n = 0
    while hv._d < MAX and n < 10:
        n += 1
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(15)
    hv.log(f"[trc] breaker done ({hv._d} checks)")


threading.Thread(target=_breaker, daemon=True).start()
