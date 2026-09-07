# pcie_emul-exp5-dartcheck.py (NWOAS §6-C) — READ-ONLY check of the C-side DART fix on the installed
# m1n1. The outer m1n1 now has nwoas_dart_fixup() which SHOULD wire identity L2 tables into the
# FL1100 DART L1 when Windows programs the ring pointers. But the event ring stayed empty. This
# probe (gentle break-ins, NO writes, NO hv_pt_walk-on-physical) walks the DART page tables for the
# ring / DCBAA / command-ring IOVAs to answer: did the C fixup actually wire them (L1 valid + L2
# identity), and is the COMMAND ring reachable (FL1100 must read it to post any completion)?
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[dchk] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[dchk] map_hw -> backing {_backing:#x}")

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


def _pa(iova):
    # high RAM is identity (IOVA==PA); low window backs to _backing
    if iova is None:
        return None
    return iova if (iova >> 32) else (_backing + (iova & 0xffffffff))


def _walk(name, l1phys, iova):
    if not iova:
        hv.log(f"[dchk]   {name}: (not programmed)"); return
    if (iova >> 32) == 0:
        hv.log(f"[dchk]   {name}={iova:#x}: LOW-window (<4GB, C fixup skips this)"); return
    l1i = (iova >> 25) & 0x7ff
    l1e = _r64(l1phys + 8 * l1i)
    if l1e is None or not (l1e & 1):
        hv.log(f"[dchk]   {name}={iova:#x}: L1[{l1i}]={_h(l1e)} INVALID => C fixup did NOT wire it"); return
    l2base = l1e & PTE_ADDR
    l2i = (iova >> 14) & 0x7ff
    l2e = _r64(l2base + 8 * l2i)
    want = (iova & ~0x3fff) | 0x3 | (0xfff << 40)
    tag = "IDENTITY-OK (wired!)" if l2e == want else "MISMATCH"
    hv.log(f"[dchk]   {name}={iova:#x}: L1[{l1i}] valid -> L2[{l2i}]={_h(l2e)} want={want:#x} => {tag}")


def _check():
    caplen = (_r32(FL_BAR) or 0) & 0xff
    op = FL_BAR + caplen
    rt = FL_BAR + ((_r32(FL_BAR + 0x18) or 0) & ~0x1f)
    usbcmd = _r32(op); dcbaap = _r64(op + 0x30); crcr = _r64(op + 0x18); erstba = _r64(rt + 0x30)
    ttbr = _r32(FL_DART + 0x200 + 16 * 1)
    l1phys = ((ttbr & 0x7fffffff) << 12) if (ttbr and (ttbr & 0x80000000)) else 0
    tcr = _r32(FL_DART + 0x100 + 4 * 1)
    hv.log(f"[dchk] USBCMD={_h(usbcmd)} DCBAAP={_h(dcbaap)} CRCR={_h(crcr)} ERSTBA={_h(erstba)}")
    hv.log(f"[dchk] DART TTBR1={_h(ttbr)} l1phys={_h(l1phys)} TCR1={_h(tcr)}(translate={0x80==(tcr or 0)&0x80})")
    if not l1phys:
        hv.log("[dchk] no valid TTBR -> cannot walk"); return
    # ring base from ERST[0]
    ring = None
    ep = _pa(erstba & ~0x3f) if erstba else None
    if ep:
        rb = _r64(ep)
        ring = (rb & ~0x3f) if rb else None
    _walk("RING", l1phys, ring)
    _walk("DCBAA", l1phys, (dcbaap & ~0x3f) if dcbaap else None)
    _walk("CMDRING", l1phys, (crcr & ~0x3f) if crcr else None)
    # is the command ring populated (usbxhci queued a command) and does FL1100 read it?
    if crcr and (crcr >> 32):
        cp = _pa(crcr & ~0x3f)
        if cp:
            c0 = _r64(cp); c1 = _r32(cp + 12)
            hv.log(f"[dchk]   CMD[0]@pa{cp:#x}: param={_h(c0)} ctrl={_h(c1)} type={((c1 or 0)>>10)&0x3f} cyc={(c1 or 0)&1}")
    # event ring content
    if ring:
        rp = _pa(ring)
        nz = sum(1 for i in range(16) if (_r32(rp + i * 16 + 12) or 0))
        hv.log(f"[dchk]   EVENT RING pa={rp:#x}: nonzero TRBs={nz}/16")


hv._n = 0
hv._d = 0
MAX = 4


def _shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._d >= MAX:
        return EXC_RET.HANDLED
    hv._d += 1
    hv.log(f"[dchk] ==== BREAK-IN #{hv._n} @{time.strftime('%H:%M:%S')} ====")
    try:
        _check()
    except Exception as e:
        try: hv.log(f"[dchk] err: {e}")
        except Exception: pass
    hv.log("[dchk] ==== done ====")
    return EXC_RET.HANDLED


hv.run_shell = _shell


def _log_has(pat):
    lp = os.environ.get("NWOAS_LOG")
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
    hv.log("[dchk] kernel detected; settle 120s then read-only break-ins (cap 4)")
    time.sleep(120)
    tries = 0
    while hv._d < MAX and tries < 10:
        tries += 1
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(15)
    hv.log(f"[dchk] breaker done ({hv._d} checks, {hv._n} break-ins)")


threading.Thread(target=_breaker, daemon=True).start()
