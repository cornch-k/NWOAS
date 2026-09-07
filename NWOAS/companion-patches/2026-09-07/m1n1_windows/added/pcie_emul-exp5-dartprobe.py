# pcie_emul-exp5-dartprobe.py (NWOAS §6-C reachability root-cause, PYTHON, kmutil-FREE)
# §6-A proved REACHABILITY: FL1100 never DMA-writes completion TRBs to the event ring at high phys
# (0xae0fc8000 all-zero). fix-C already BYPASS'd the DART (0x681/2/3008000) yet reachability still
# failed. §6-B: FL1100 = DART 0x682008000 stream 1; top suspect = DART CONFIG_LOCK (TCR writes
# silently dropped when locked). This probe answers, on the installed v1 via gentle break-ins:
#   (A) CURRENT DART state: is 0x682008000 already in BYPASS? is it LOCKED (CONFIG bit15)? ERR set?
#   (B) is usbxhci even issuing a command (CRCR) that FL1100 should complete?
#   (C) apply targeted BYPASS to 0x682008000 (all streams) + TLB-invalidate, VERIFY the TCR readback
#       (readback!=0x1100 => LOCKED => BYPASS impossible via TCR), then re-read the event ring:
#         ring populates  -> reachability FIXED by live BYPASS (fix-C hadn't persisted)
#         ring stays empty + TCR==0x1100 -> BYPASS active yet unreachable => beyond DART TCR
#         TCR!=0x1100 after write -> DART LOCKED
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[dartp] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_phys_base = hv.u.ba.phys_base
_mem_size = hv.u.ba.mem_size
_backing = _phys_base + _mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[dartp] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x}")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000
DARTS = [0x681008000, 0x682008000, 0x683008000]
FL_DART = 0x682008000  # §6-B: FL1100's DART
TRB_TYPE = {32: "XFER", 33: "CMDCOMP", 34: "PORTSC", 35: "BW", 36: "DB", 37: "HC", 38: "DEVN", 39: "MFIX"}
PTE_TARGET = ((1 << 50) - 1) & ~((1 << 14) - 1)
# DART_T8020 register map
CFG = 0x60; TCR = 0x100; ENABLED = 0xfc; ERR = 0x40; ERR_ADDR_LO = 0x50; ERR_ADDR_HI = 0x54
STREAM_SELECT = 0x34; STREAM_COMMAND = 0x20; INVALIDATE = 1 << 20; BUSY = 1 << 2
BYPASS = (1 << 8) | (1 << 12)  # 0x1100
CFG_LOCK = 1 << 15


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
    pa = None
    try:
        pte = p.hv_pt_walk(ipa & ~0x3fff)
        if pte:
            pa = (pte & PTE_TARGET) | (ipa & 0x3fff)
    except Exception:
        pa = None
    if pa is None and (ipa >> 32) == 0:
        pa = _backing + (ipa & 0xffffffff)
    return pa


def _read_darts(tag):
    for base in DARTS:
        cfg = _rd32(base + CFG); en = _rd32(base + ENABLED); err = _rd32(base + ERR)
        ea = _rd64(base + ERR_ADDR_LO)
        t0 = _rd32(base + TCR); t1 = _rd32(base + TCR + 4); t2 = _rd32(base + TCR + 8); t3 = _rd32(base + TCR + 12)
        lock = "LOCKED" if (cfg is not None and (cfg & CFG_LOCK)) else "unlocked"
        byp = "BYPASS" if (t1 == BYPASS) else ("translate" if t1 else "off")
        hv.log(f"[dartp] {tag} DART {base:#x}: CFG={_h(cfg)}({lock}) EN={_h(en)} ERR={_h(err)} ERRADDR={_h(ea)} "
               f"TCR[0..3]={_h(t0)},{_h(t1)},{_h(t2)},{_h(t3)} stream1={byp}")


def _read_cmdring(op, rt):
    crcr = _rd64(op + 0x18)  # command ring control register (base+RCS)
    hv.log(f"[dartp] CMD CRCR={_h(crcr)} (RCS={crcr & 1 if crcr else '?'})")
    if not crcr or (crcr & ~0x3f) == 0:
        return
    cr_ipa = crcr & ~0x3f
    cr_pa = _ipa_to_pa(cr_ipa)
    if cr_pa is None:
        hv.log(f"[dartp] CMD ring ipa={cr_ipa:#x} -> no PA"); return
    try:
        p.dc_ivac(cr_pa, 4 * 16)
    except Exception:
        pass
    for i in range(4):
        a = cr_pa + i * 16
        par = _rd64(a); ctrl = _rd32(a + 12)
        if ctrl is None:
            continue
        hv.log(f"[dartp]   CMD[{i}]@{a:#x}: param={_h(par)} ctrl={_h(ctrl)} type={(ctrl >> 10) & 0x3f} cyc={ctrl & 1}")


def _dump_ring(op, rt):
    erstba = _rd64(rt + 0x30); erdp = _rd64(rt + 0x38)
    usbcmd = _rd32(op); dcbaap = _rd64(op + 0x30)
    hv.log(f"[dartp] OP USBCMD={_h(usbcmd)} DCBAAP={_h(dcbaap)} ERSTBA={_h(erstba)} ERDP={_h(erdp)}")
    if not erstba:
        return
    ep = _ipa_to_pa(erstba & ~0x3f)
    if ep is None:
        return
    try:
        p.dc_civac(ep, 16)
    except Exception:
        pass
    ring_ipa = _rd64(ep); rsize = _rd32(ep + 8)
    if not ring_ipa:
        hv.log("[dartp] ring not programmed"); return
    rp = _ipa_to_pa(ring_ipa & ~0x3f)
    if rp is None:
        hv.log(f"[dartp] ring ipa={ring_ipa:#x} -> no PA"); return
    try:
        p.dc_ivac(rp, 16 * 16)
    except Exception:
        pass
    valid = 0
    for i in range(16):
        ctrl = _rd32(rp + i * 16 + 12)
        if ctrl and (ctrl & 1) and (((ctrl >> 10) & 0x3f) in TRB_TYPE):
            valid += 1
    hv.log(f"[dartp] EVENT RING pa={rp:#x} size={rsize}: valid DRAM TRBs = {valid}/16 "
           f"=> {'FL1100 WROTE (reach OK now!)' if valid else 'still empty (unreachable)'}")
    if valid:
        for i in range(16):
            par = _rd64(rp + i * 16); st = _rd32(rp + i * 16 + 8); ctrl = _rd32(rp + i * 16 + 12)
            if ctrl:
                hv.log(f"[dartp]   ER[{i}]: param={_h(par)} status={_h(st)} ctrl={_h(ctrl)} "
                       f"type={(ctrl >> 10) & 0x3f} cyc={ctrl & 1} cc={(st >> 24) & 0xff if st else '?'}")


def _apply_bypass(base):
    hv.log(f"[dartp] APPLY BYPASS on {base:#x} (all streams)")
    cfg = _rd32(base + CFG)
    if cfg is not None and (cfg & CFG_LOCK):
        hv.log(f"[dartp]   CFG={_h(cfg)} = LOCKED before write (TCR writes may be dropped)")
    p.write32(base + ENABLED, 0xffffffff)
    for s in range(16):
        p.write32(base + TCR + 4 * s, BYPASS)
    # TLB invalidate all streams
    p.write32(base + STREAM_SELECT, 0xffffffff)
    p.write32(base + STREAM_COMMAND, INVALIDATE)
    for _ in range(200):
        if not (p.read32(base + STREAM_COMMAND) & BUSY):
            break
    # verify
    rb = _rd32(base + TCR + 4)  # stream 1 readback
    rb0 = _rd32(base + TCR)
    err = _rd32(base + ERR)
    verdict = "BYPASS TOOK" if rb == BYPASS else "WRITE DROPPED -> DART LOCKED/read-only"
    hv.log(f"[dartp]   after: TCR[0]={_h(rb0)} TCR[1]={_h(rb)} (want {BYPASS:#x}) ERR={_h(err)} => {verdict}")


def _bringup(op, rt):
    caplen = _rd32(FL_BAR) & 0xff
    return FL_BAR + caplen, FL_BAR + (_rd32(FL_BAR + 0x18) & ~0x1f)


hv._n = 0
hv._dumps = 0
MAX_DUMPS = 7


def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    hv._n += 1
    if hv._dumps >= MAX_DUMPS:
        return EXC_RET.HANDLED
    hv._dumps += 1
    stage = hv._dumps
    cmd = _rd32(FL_CFG + 0x04)
    if cmd is None or not ((cmd >> 1) & 1):
        hv.log(f"[dartp] #{stage}: CFG CMD={_h(cmd)} MEM-decode off; skip"); return EXC_RET.HANDLED
    op, rt = _bringup(FL_BAR, FL_BAR)
    hv.log(f"[dartp] ==== BREAK-IN #{hv._n} stage#{stage} @{time.strftime('%H:%M:%S')} ====")
    try:
        if stage == 1:  # baseline: DART state + command ring + event ring
            _read_darts("BASELINE")
            _read_cmdring(op, rt)
            _dump_ring(op, rt)
        elif stage == 2:  # apply targeted BYPASS on FL1100's DART + verify
            _read_darts("PRE-APPLY")
            _apply_bypass(FL_DART)
            _read_darts("POST-APPLY")
        else:  # stages 3..7: re-read event ring after BYPASS (give FL1100 time between break-ins)
            _dump_ring(op, rt)
            _read_darts(f"RECHECK#{stage}")
    except Exception as e:
        try: hv.log(f"[dartp] stage#{stage} err: {e}")
        except Exception: pass
    hv.log(f"[dartp] ==== stage#{stage} done ====")
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
            hv.log("[dartp] kernel not seen in 30min; proceeding"); break
    hv.log("[dartp] kernel detected; settle 120s then staged break-ins (cap 7, 15s apart)")
    time.sleep(120)
    tries = 0
    while hv._dumps < MAX_DUMPS and tries < 12:
        tries += 1
        try:
            hv.interrupt()
        except Exception:
            pass
        time.sleep(15)
    hv.log(f"[dartp] breaker done ({hv._dumps} stages, {hv._n} break-ins)")


threading.Thread(target=_breaker, daemon=True).start()
