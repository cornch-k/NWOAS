# pcie_emul-exp5-dartpt.py (NWOAS §6-C reachability, PYTHON, kmutil-FREE) — the decisive test.
# Source analysis (AppleWOA AppleDartIoMmuDxe.c) proved: the apcie DART does NOT support bypass;
# TCR=0x1100 is a no-op that only DISABLED UEFI's working TRANSLATE. UEFI reaches high RAM via
# TRANSLATE_ENABLE(0x80) + TTBR + a WIDE-DART identity map of [phys_base, backing). Yet at baseline
# (TCR=0x80) the ring is EMPTY -> either the identity L2 PTE for the ring IOVA is MISSING, or it is
# present and the blocker is NOT the DART. This probe WALKS the DART page tables for each xHCI DMA
# IOVA and reports the L2 PTE; if missing it INJECTS the identity PTE (never bypasses), then rechecks.
#   ring fills after inject  -> DART map was the blocker (fix = ensure identity PTE, keep TRANSLATE)
#   PTE already identity + ring empty -> NOT the DART (device/endpoint/snoop) -> redirect §3.2
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[dpt] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[dpt] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} phys_base={hv.u.ba.phys_base:#x}")

FL_BAR = 0x6c0000000
FL_DART = 0x682008000
SID = 1
TCR = 0x100; TTBR = 0x200; TTBR_CNT = 4; ENABLED = 0xfc
SEL = 0x34; SCMD = 0x20; INVAL = 1 << 20; BUSY = 1 << 2
TRANSLATE = 0x80
PTE_ADDR = 0x000000FFFFFFC000  # bits[39:14] address field (T8020)
PTE_SP = 0xfff << 40           # sub-page protect full
PTE_ID = lambda pa: (pa & ~0x3fff) | PTE_SP | 0x3  # identity page PTE (VALID)
PTE_TARGET = ((1 << 50) - 1) & ~((1 << 14) - 1)


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


def _ipa_pa(ipa):
    if ipa is None:
        return None
    try:
        pte = p.hv_pt_walk(ipa & ~0x3fff)
        if pte:
            return (pte & PTE_TARGET) | (ipa & 0x3fff)
    except Exception:
        pass
    return _backing + (ipa & 0xffffffff) if (ipa >> 32) == 0 else None


def _dart_walk(iova, inject=False):
    """Walk FL_DART stream SID for iova; return (l2_pte_addr, l2_pte). Optionally inject identity."""
    ttbr = _r32(FL_DART + TTBR + 16 * SID + 4 * 0)
    if ttbr is None or not (ttbr & (1 << 31)):
        hv.log(f"[dpt]   walk iova={iova:#x}: TTBR[{SID}][0]={_h(ttbr)} INVALID"); return None, None
    l1phys = (ttbr & 0x7fffffff) << 12
    l1i = (iova >> 25) & 0x7ff
    l1pte = _r64(l1phys + 8 * l1i)
    if l1pte is None or not (l1pte & 1):
        hv.log(f"[dpt]   walk iova={iova:#x}: L1phys={l1phys:#x} L1[{l1i}]={_h(l1pte)} INVALID (no L2)"); return None, None
    l2base = l1pte & PTE_ADDR
    l2i = (iova >> 14) & 0x7ff
    l2a = l2base + 8 * l2i
    l2pte = _r64(l2a)
    want = PTE_ID(iova)
    ok = "IDENTITY-OK" if (l2pte == want) else ("VALID-but-diff" if (l2pte and (l2pte & 1)) else "MISSING/invalid")
    hv.log(f"[dpt]   iova={iova:#x}: L1[{l1i}]->{l2base:#x} L2[{l2i}]@{l2a:#x}={_h(l2pte)} want={want:#x} => {ok}")
    if inject and l2pte != want:
        try:
            p.write32(l2a, want & 0xffffffff)
            p.write32(l2a + 4, (want >> 32) & 0xffffffff)
            pa = _ipa_pa(l2a & ~0x3fff)  # flush the L2 table line so the DART re-reads it
            if pa:
                p.dc_civac(pa, 64)
            hv.log(f"[dpt]   INJECTED L2[{l2i}]={_h(_r64(l2a))}")
        except Exception as e:
            hv.log(f"[dpt]   inject err: {e}")
    return l2a, l2pte


def _tlb_inval():
    try:
        p.write32(FL_DART + SEL, 0xffffffff)
        p.write32(FL_DART + SCMD, INVAL)
        for _ in range(200):
            if not (p.read32(FL_DART + SCMD) & BUSY):
                break
    except Exception:
        pass


def _xhci():
    caplen = _r32(FL_BAR) & 0xff
    return FL_BAR + caplen, FL_BAR + (_r32(FL_BAR + 0x18) & ~0x1f)


def _ring_iovas():
    op, rt = _xhci()
    dcbaap = _r64(op + 0x30); erstba = _r64(rt + 0x30); crcr = _r64(op + 0x18)
    ivs = {}
    if dcbaap:
        ivs["DCBAA"] = dcbaap & ~0x3f
    if crcr and (crcr & ~0x3f):
        ivs["CMDRING"] = crcr & ~0x3f
    if erstba:
        ivs["ERST"] = erstba & ~0x3f
        ep = _ipa_pa(erstba & ~0x3f)
        if ep:
            try: p.dc_civac(ep, 16)
            except Exception: pass
            rb = _r64(ep)
            if rb:
                ivs["RING"] = rb & ~0x3f
    return op, rt, ivs


def _check_ring():
    op, rt, ivs = _ring_iovas()
    usbcmd = _r32(op)
    if "RING" not in ivs:
        hv.log(f"[dpt] ring not programmed (USBCMD={_h(usbcmd)})"); return 0
    rp = _ipa_pa(ivs["RING"])
    if rp is None:
        return 0
    try: p.dc_ivac(rp, 16 * 16)
    except Exception: pass
    valid = 0
    for i in range(16):
        ctrl = _r32(rp + i * 16 + 12)
        if ctrl and (ctrl & 1) and (32 <= ((ctrl >> 10) & 0x3f) <= 39):
            valid += 1
    hv.log(f"[dpt] EVENT RING iova={ivs['RING']:#x} pa={rp:#x}: valid TRBs={valid}/16 "
           f"=> {'★★POPULATED (reachability FIXED via DART translate+PTE)' if valid else 'empty'}")
    return valid


hv._n = 0; hv._st = 0
MAX = 8


def _shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._st >= MAX:
        return EXC_RET.HANDLED
    hv._st += 1
    st = hv._st
    tcr1 = _r32(FL_DART + TCR + 4 * SID)
    hv.log(f"[dpt] ==== BREAK-IN #{hv._n} stage#{st} @{time.strftime('%H:%M:%S')} TCR[{SID}]={_h(tcr1)} ====")
    try:
        op, rt, ivs = _ring_iovas()
        if st == 1:  # baseline: TCR + walk every xHCI IOVA (are they identity-mapped?)
            hv.log(f"[dpt] IOVAs: {{ {', '.join(f'{k}={v:#x}' for k,v in ivs.items())} }}")
            for k, v in ivs.items():
                _dart_walk(v)
            _check_ring()
        elif st == 2:  # ensure TRANSLATE on (NOT bypass), inject identity PTE for any missing IOVA
            if tcr1 != TRANSLATE:
                p.write32(FL_DART + TCR + 4 * SID, TRANSLATE)
                hv.log(f"[dpt] restored TCR[{SID}]={_h(_r32(FL_DART+TCR+4*SID))} (TRANSLATE)")
            for k, v in ivs.items():
                _dart_walk(v, inject=True)
            _tlb_inval()
            hv.log("[dpt] injected+invalidated; letting FL1100 retry")
            _check_ring()
        else:  # recheck: did the ring fill?
            _check_ring()
    except Exception as e:
        try: hv.log(f"[dpt] stage#{st} err: {e}")
        except Exception: pass
    hv.log(f"[dpt] ==== stage#{st} done ====")
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
    hv.log("[dpt] kernel detected; settle 120s then staged break-ins (cap 8, 15s apart)")
    time.sleep(120)
    tries = 0
    while hv._st < MAX and tries < 14:
        tries += 1
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(15)
    hv.log(f"[dpt] breaker done ({hv._st} stages, {hv._n} break-ins)")


threading.Thread(target=_breaker, daemon=True).start()
