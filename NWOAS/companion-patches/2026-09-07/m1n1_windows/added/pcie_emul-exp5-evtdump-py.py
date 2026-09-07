# pcie_emul-exp5-evtdump-py.py (NWOAS §6-A, PYTHON-side, kmutil-FREE) — decisive
# reachability-vs-coherence probe that runs on the ALREADY-INSTALLED m1n1 (no new hv build, no
# chainload, no kmutil). Fixes the two reasons the old evtring1 module failed:
#   (1) high-RAM ring read poison  -> we now STAGE-2 TRANSLATE the guest IPA to the host PA via
#       p.hv_pt_walk BEFORE reading (poison came from reading the untranslated guest IPA). This
#       handles both the low window (guest IPA [0,4GB) -> backing) and identity high RAM.
#   (2) 284 break-ins -> BSOD      -> we break in GENTLY, kernel-gated, HARD-CAPPED at a few.
# Coherence probe: p.dc_ivac (invalidate-only, never writes back) the ring lines, then read = the
# DRAM bytes FL1100 actually DMA'd. Compare to a pre-invalidate read (cached view).
#   ring DRAM all-zero      -> FL1100 never landed the write   -> REACHABILITY (or DART misroute)
#   valid completion TRBs   -> FL1100 wrote it                 -> not reachability (coherence/desync)
#   cached != DRAM          -> guest reads stale               -> COHERENCE (direct proof)
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[evtpy] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_phys_base = hv.u.ba.phys_base
_mem_size = hv.u.ba.mem_size
_backing = _phys_base + _mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[evtpy] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} | phys_base={_phys_base:#x} mem_size={_mem_size:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000
TRB_TYPE = {32: "XFER-EVT", 33: "CMDCOMP", 34: "PORTSC-CHG", 35: "BW-REQ", 36: "DOORBELL",
            37: "HC-EVT", 38: "DEV-NOTIF", 39: "MFINDEX-WRAP"}
PTE_TARGET = ((1 << 50) - 1) & ~((1 << 14) - 1)  # bits 49..14 (16KB granule)


def _rd32(a):
    try:
        return p.read32(a) & 0xffffffff
    except Exception:
        return None


def _rd64(a):
    lo = _rd32(a); hi = _rd32(a + 4)
    if lo is None or hi is None:
        return None
    return lo | (hi << 32)


def _ipa_to_pa(ipa):
    # Stage-2 translate the guest IPA to host PA (handles low window AND identity high RAM). Cross-
    # check against the known low-window backing for < 4GB IPAs; fall back to it if pt_walk fails.
    if ipa is None:
        return None
    pa = None
    try:
        pte = p.hv_pt_walk(ipa & ~0x3fff)
        if pte:
            pa = (pte & PTE_TARGET) | (ipa & 0x3fff)
    except Exception:
        pa = None
    if (ipa >> 32) == 0:  # low window: guest IPA [0,4GB) -> backing + offset
        wpa = _backing + (ipa & 0xffffffff)
        if pa is None:
            pa = wpa
        elif pa != wpa:
            hv.log(f"[evtpy]   xlate note: ipa={ipa:#x} pt_walk={pa:#x} backing={wpa:#x} (using pt_walk)")
    return pa


def _dump_ring(label, ring_ipa, n):
    ring_pa = _ipa_to_pa(ring_ipa)
    if ring_pa is None:
        hv.log(f"[evtpy]   {label}: ring ipa={ring_ipa:#x} -> no PA (unmapped)"); return
    hv.log(f"[evtpy]   {label}: ring ipa={ring_ipa:#x} -> pa={ring_pa:#x}, {n} TRBs:")
    # cached view first, then invalidate-only + re-read = DRAM (what FL1100 actually wrote)
    cached = []
    for i in range(n):
        cached.append((_rd64(ring_pa + i * 16), _rd32(ring_pa + i * 16 + 12)))
    try:
        p.dc_ivac(ring_pa, n * 16)
    except Exception as e:
        hv.log(f"[evtpy]   dc_ivac err: {e}")
    valid = 0
    for i in range(n):
        a = ring_pa + i * 16
        param = _rd64(a); status = _rd32(a + 8); ctrl = _rd32(a + 12)
        if ctrl is None:
            hv.log(f"[evtpy]   {label}[{i}]@{a:#x}: read fail"); continue
        cyc = ctrl & 1; ttype = (ctrl >> 10) & 0x3f
        cc = (status >> 24) & 0xff if status is not None else -1
        tn = TRB_TYPE.get(ttype, f"?{ttype}")
        cch = cached[i][1]
        div = " DIVERGE=COHERENCE" if (cch is not None and cch != ctrl) else ""
        cchs = f"{cch:#010x}" if cch is not None else "None"
        params = f"{param:#018x}" if param is not None else "None"
        stats = f"{status:#010x}" if status is not None else "None"
        if cyc == 1 and ttype in TRB_TYPE:
            valid += 1
        hv.log(f"[evtpy]   {label}[{i:2d}]@{a:#x}: dram param={params} status={stats}(CC={cc}) "
               f"ctrl={ctrl:#010x} cyc={cyc} type={tn} | cache ctrl={cchs}{div}")
    hv.log(f"[evtpy]   {label}: valid(cyc=1,known-type) DRAM TRBs = {valid}/{n}  "
           f"=> {'FL1100 WROTE (not reachability; coherence/desync)' if valid else 'ALL-ZERO/STALE (reachability or DART-misroute)'}")


def _h(x):
    return f"{x:#x}" if x is not None else "None"


def _evtring_dump():
    cmd = _rd32(FL_CFG + 0x04)
    if cmd is None or not ((cmd >> 1) & 1):
        hv.log(f"[evtpy] CFG CMD={cmd} MEM-decode off; skip"); return
    caplen = _rd32(FL_BAR) & 0xff
    rtsoff = _rd32(FL_BAR + 0x18) & ~0x1f
    op = FL_BAR + caplen
    rt = FL_BAR + rtsoff
    usbcmd = _rd32(op + 0x00); usbsts = _rd32(op + 0x04)
    dcbaap = _rd64(op + 0x30)
    erstsz = _rd32(rt + 0x28); erstba = _rd64(rt + 0x30); erdp = _rd64(rt + 0x38); iman = _rd32(rt + 0x20)
    hv.log(f"[evtpy] OP USBCMD={_h(usbcmd)}(RS={usbcmd & 1 if usbcmd else '?'}) USBSTS={_h(usbsts)} DCBAAP={_h(dcbaap)}")
    hv.log(f"[evtpy] RT ERSTSZ={_h(erstsz)} ERSTBA={_h(erstba)} ERDP={_h(erdp)} IMAN={_h(iman)}")
    if not erstba:
        hv.log("[evtpy] ERSTBA=0, controller not set up"); return
    erst_pa = _ipa_to_pa(erstba & ~0x3f)
    if erst_pa is None:
        hv.log(f"[evtpy] ERSTBA {erstba:#x} -> no PA"); return
    try:
        p.dc_civac(erst_pa, 16)  # ERST is guest-authored -> clean+invalidate for its latest value
    except Exception:
        pass
    ring_base = _rd64(erst_pa); ring_size = _rd32(erst_pa + 8)
    hv.log(f"[evtpy] ERST[0]@pa{erst_pa:#x}: ring_base(ipa)={ring_base:#x} ring_size={ring_size}")
    if not ring_base or not ring_size:
        hv.log("[evtpy] ring not programmed yet"); return
    n = min(ring_size, 16)
    _dump_ring("ER", ring_base & ~0x3f, n)


# --- gentle, kernel-gated, hard-capped break-in driver --------------------------------------------
hv._n = 0
hv._dumps = 0
MAX_DUMPS = 4  # hard cap: << the 284 that BSOD'd


def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._dumps < MAX_DUMPS:
        hv._dumps += 1
        hv.log(f"[evtpy] === DUMP #{hv._dumps} (break-in {hv._n}) @{time.strftime('%H:%M:%S')} ===")
        try:
            _evtring_dump()
        except Exception as e:
            try: hv.log(f"[evtpy] dump err: {e}")
            except Exception: pass
        hv.log(f"[evtpy] === dump #{hv._dumps} done ===")
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
        if t > 1800:
            hv.log("[evtpy] kernel marker not seen in 30min; giving one break-in anyway"); break
    hv.log("[evtpy] kernel detected; settle 120s for the livelock, then GENTLE break-ins (cap 4)")
    time.sleep(120)
    tries = 0
    while hv._dumps < MAX_DUMPS and tries < 8:  # at most 8 interrupts total, spaced 10s
        tries += 1
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(10)
    hv.log(f"[evtpy] breaker done ({hv._dumps} dumps, {hv._n} break-ins)")


threading.Thread(target=_breaker, daemon=True).start()
