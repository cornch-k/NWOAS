# pcie_emul-exp5-dartfix.py (NWOAS §6-C reachability FIX test, PYTHON, kmutil-FREE)
# dartpt PROVED the root cause: DART 0x682008000 stream1 is in TRANSLATE mode (TCR=0x80) with a
# valid TTBR/L1, but L1[ring>>25]=0 -> the high-RAM identity L2 table for the xHCI ring region is
# NOT installed (UEFI WIDE-DART didn't wire it / Windows cleared it). So FL1100 DMA to 0xae0fc8000
# hits NO_PTE and is dropped -> event ring empty -> livelock.
# FIX (UEFI AppleDartIoMmuDxe format, :523/:634): allocate a 16KB L2 table, fill it with IDENTITY
# page PTEs = (pa&~0x3fff)|0x3|(0xfff<<40), and wire L1[idx] = (l2phys&~0x3fff)|0x3, for every L1
# region the xHCI structures (DCBAA/CMD/ERST/RING) live in. Keep TRANSLATE (never bypass). Then the
# event ring should populate with completion TRBs -> cursor should move.
import os, time, threading, struct
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[dfix] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[dfix] map_hw -> backing {_backing:#x} phys_base={hv.u.ba.phys_base:#x}")

FL_BAR = 0x6c0000000
FL_DART = 0x682008000
SID = 1
TCR = 0x100; TTBR = 0x200; SEL = 0x34; SCMD = 0x20; INVAL = 1 << 20; BUSY = 1 << 2
TRANSLATE = 0x80
PTE_ADDR = 0x000000FFFFFFC000
PTE_SP = 0xfff << 40
PT_TGT = ((1 << 50) - 1) & ~((1 << 14) - 1)
L2_PAGE = lambda pa: (pa & ~0x3fff) | 0x3 | PTE_SP
L1_ENT = lambda l2: (l2 & ~0x3fff) | 0x3

# pre-allocate a pool of 16KB-aligned L2 tables (host phys, DART-reachable). Each covers one 32MB
# L1 region. 6 covers ring/ERST/DCBAA/CMD + slack.
_pool = []
for _ in range(6):
    raw = hv.u.malloc(0x8000)
    _pool.append((raw + 0x3fff) & ~0x3fff)
hv.log(f"[dfix] L2 table pool @ {[hex(x) for x in _pool]}")
_wired = {}  # l1i -> l2phys
# DART page tables + our L2 pool are HOST-PHYSICAL (in EL2's identity RAM map) — dc_civac them
# DIRECTLY. NEVER pass them to hv_pt_walk (guest stage-2 walk asserts on non-guest addrs -> EL2
# crash, the v3 bug). Sanity-bound every physical addr before touching it.
RAM_LO = hv.u.ba.phys_base & ~0xffffffff
RAM_HI = 0xc00000000  # 48GB safe ceiling


def _ok(a):
    return a is not None and RAM_LO <= a < RAM_HI


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
            return (pte & PT_TGT) | (ipa & 0x3fff)
    except Exception:
        pass
    return _backing + (ipa & 0xffffffff) if (ipa >> 32) == 0 else None


def _l1phys():
    ttbr = _r32(FL_DART + TTBR + 16 * SID)
    if ttbr is None or not (ttbr & (1 << 31)):
        return None
    return (ttbr & 0x7fffffff) << 12


def _ensure(iova):
    """Ensure the DART leaf PTE for iova is an IDENTITY page. Handles L1-missing (alloc L2 table +
    wire) AND L1-present-but-L2-PTE-wrong (fix the leaf). Returns action string."""
    l1p = _l1phys()
    if not _ok(l1p):
        hv.log(f"[dfix]   bad TTBR/L1phys={_h(l1p)}"); return "no-ttbr"
    l1i = (iova >> 25) & 0x7ff
    l1e = _r64(l1p + 8 * l1i)
    if not l1e or not (l1e & 1):
        # L1 missing -> allocate an L2 table, fill identity for the 32MB region, wire L1
        if not _pool:
            hv.log(f"[dfix]   L1[{l1i}] missing but L2 pool exhausted"); return "pool-empty"
        l2 = _pool.pop(0)
        if not _ok(l2):
            _pool.insert(0, l2); hv.log(f"[dfix]   pool addr {_h(l2)} out of RAM"); return "err"
        region = l1i << 25
        data = b"".join(struct.pack("<Q", L2_PAGE(region + j * 0x4000)) for j in range(2048))
        try:
            hv.iface.writemem(l2, data)   # bulk write is on iface, not p
            p.dc_civac(l2, 0x4000)         # l2 is HOST-PHYSICAL -> dc_civac directly (no hv_pt_walk)
            ent = L1_ENT(l2)
            p.write64(l1p + 8 * l1i, ent)
            p.dc_civac((l1p + 8 * l1i) & ~0x3f, 64)  # l1p is HOST-PHYSICAL
            _tlb()
            _wired[l1i] = l2
            hv.log(f"[dfix]   ★WIRED-L1 iova={iova:#x} L1[{l1i}] region={region:#x} -> L2@{l2:#x} "
                   f"rb L1={_h(_r64(l1p+8*l1i))} L2[0]={_h(_r64(l2))}")
            return "wired-l1"
        except Exception as e:
            _pool.insert(0, l2)  # give the table back so a retry can reuse it
            hv.log(f"[dfix]   wire err: {e}"); return "err"
    # L1 valid -> verify/fix the L2 leaf PTE for this exact page
    l2base = l1e & PTE_ADDR
    if not _ok(l2base):
        hv.log(f"[dfix]   L1[{l1i}]={_h(l1e)} -> bad l2base {_h(l2base)}"); return "err"
    l2i = (iova >> 14) & 0x7ff
    l2a = l2base + 8 * l2i
    l2e = _r64(l2a)
    want = L2_PAGE(iova)
    if l2e == want:
        hv.log(f"[dfix]   L2-OK iova={iova:#x} L1[{l1i}]->{l2base:#x} L2[{l2i}]={_h(l2e)} (identity)")
        return "l2-ok"
    try:
        p.write64(l2a, want)
        p.dc_civac(l2base, 0x4000)  # l2base is HOST-PHYSICAL
        _tlb()
        hv.log(f"[dfix]   ★FIXED-L2 iova={iova:#x} L2[{l2i}]@{l2a:#x}: {_h(l2e)} -> {_h(_r64(l2a))} want={want:#x}")
        return "fixed-l2"
    except Exception as e:
        hv.log(f"[dfix]   l2 fix err: {e}"); return "err"


def _tlb():
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


def _iovas():
    op, rt = _xhci()
    dcbaap = _r64(op + 0x30); erstba = _r64(rt + 0x30); crcr = _r64(op + 0x18)
    ivs = {}
    if dcbaap:
        ivs["DCBAA"] = dcbaap & ~0x3f
    if crcr and (crcr & ~0x3f):
        ivs["CMD"] = crcr & ~0x3f
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


def _check():
    op, rt, ivs = _iovas()
    if "RING" not in ivs:
        hv.log(f"[dfix] ring not programmed (USBCMD={_h(_r32(op))})"); return 0
    rp = _ipa_pa(ivs["RING"])
    if rp is None:
        return 0
    try: p.dc_ivac(rp, 16 * 16)
    except Exception: pass
    valid = 0; nz = None
    for i in range(16):
        ctrl = _r32(rp + i * 16 + 12)
        if ctrl:
            if nz is None:
                nz = (i, ctrl)
            if (ctrl & 1) and (32 <= ((ctrl >> 10) & 0x3f) <= 39):
                valid += 1
    hv.log(f"[dfix] EVENT RING iova={ivs['RING']:#x} pa={rp:#x}: valid={valid}/16 firstNZ={nz} "
           f"=> {'★★★POPULATED — reachability FIXED!' if valid else 'empty'}")
    if valid:
        for i in range(min(valid + 2, 16)):
            par = _r64(rp + i * 16); st = _r32(rp + i * 16 + 8); ct = _r32(rp + i * 16 + 12)
            if ct:
                hv.log(f"[dfix]   ER[{i}] param={_h(par)} status={_h(st)} ctrl={_h(ct)} type={(ct>>10)&0x3f} cc={(st>>24)&0xff if st else '?'}")
    return valid


hv._n = 0; hv._st = 0
MAX = 10


def _shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._st >= MAX:
        return EXC_RET.HANDLED
    hv._st += 1
    st = hv._st
    tcr1 = _r32(FL_DART + TCR + 4 * SID)
    hv.log(f"[dfix] ==== BREAK-IN #{hv._n} stage#{st} @{time.strftime('%H:%M:%S')} TCR[{SID}]={_h(tcr1)} ====")
    try:
        op, rt, ivs = _iovas()
        usbcmd = _r32(op)
        hv.log(f"[dfix] USBCMD={_h(usbcmd)}(RS={usbcmd&1 if usbcmd else '?'}) IOVAs: "
               f"{{ {', '.join(f'{k}={v:#x}' for k,v in ivs.items())} }}")
        if st == 1:
            _check()
        elif st in (2, 3, 4, 5):  # ensure identity leaf PTE for each xHCI IOVA (ring may move on HCRST)
            if tcr1 != TRANSLATE:
                p.write32(FL_DART + TCR + 4 * SID, TRANSLATE)
            for k, v in ivs.items():
                _ensure(v)
            _tlb()
            _check()
        else:
            _check()
    except Exception as e:
        try: hv.log(f"[dfix] stage#{st} err: {e}")
        except Exception: pass
    hv.log(f"[dfix] ==== stage#{st} done ====")
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
    hv.log("[dfix] kernel detected; settle 120s then staged break-ins (wire+recheck, cap 10)")
    time.sleep(120)
    tries = 0
    while hv._st < MAX and tries < 16:
        tries += 1
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(15)
    hv.log(f"[dfix] breaker done ({hv._st} stages, {hv._n} break-ins)")


threading.Thread(target=_breaker, daemon=True).start()
