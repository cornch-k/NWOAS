# NWOAS EXP-5 5c — deterministic bootmgr busy-wait probe (kmutil-free, host-side).
#
# The mid-read stall is DETERMINISTIC (two runs: identical last LBA 0x13622, identical stuck PC
# 0x100ad574) and guest-side (bootmgr code, FL1100 not halted, no UEFI USB block). Prior hv note
# (hv_exc.c:778-806) classed the same-family wedge as "a busy-wait on some unchanging flag". This
# hook breaks in AT the stall (detected: NWOAS MAP count frozen in NWOAS_LOG after Enable Slot) and
# dumps the full guest register context + the fixed bootmgr object the loop spins on (PMCC showed
# x0=0x10005610 constant, x1 in 0x10005500..0x10005960), to identify WHAT flag never changes.
#
# Load after pcie_emul-exp5.py (chains its run_shell so the early backing fix still applies):
#   run_guest.py -r m1n1-payload-exp5c-v7.bin -m pcie_emul-exp5.py -m nwoas_stall_probe.py
import threading, time, os, traceback
from m1n1.proxy import EXC_RET


def _rd(hv, va, n):
    # Method A memory split: UEFI code/data in the low window [0,4GB) is a hv alias to the backing
    # PA; the UEFI stack lives in the high reserved region (>4GB, identity PA). hv.readmem (guest-VA
    # page walk) fails for EL1 UEFI addresses, so fall back to a PHYSICAL read via the backing xlate.
    try:
        b = hv.readmem(va, n)
        if b:
            return b
    except Exception:
        pass
    try:
        # backing base of the low window (matches pcie_emul-exp5.py: phys_base+mem_size-win-skew).
        backing = hv.u.ba.phys_base + hv.u.ba.mem_size - 0x100000000 - 0x34000
        pa = (backing + va) if va < 0x100000000 else va   # window IPA -> backing ; high -> identity
        return hv.iface.readmem(pa, n)
    except Exception:
        return None


def _hexdump(b, base):
    if not b:
        return "  <read failed>"
    out = []
    for off in range(0, len(b), 16):
        row = b[off:off+16]
        hexs = " ".join(f"{x:02x}" for x in row)
        out.append(f"  {base+off:#010x}: {hexs}")
    return "\n".join(out)


def _probe(hv, tag):
    try:
        ctx = hv.ctx
        if ctx is None:
            print(f"NWOAS-STALL {tag}: no ctx (break-in not in guest context)")
            return
        # ExcInfo (proxy.py:83): regs=Array(32), elr=Int64ul (scalar), sp=Array(3) [EL0,EL1,EL2].
        elr = int(ctx.elr)
        regs = [int(ctx.regs[i]) for i in range(31)]
        sp = [int(x) for x in ctx.sp]
        print(f"NWOAS-STALL {tag}: cpu={getattr(ctx,'cpu_id','?')} ELR={elr:#x} "
              f"SP_EL0={sp[0]:#x} SP_EL1={sp[1]:#x}")
        for i in range(0, 31, 4):
            seg = " ".join(f"x{j}={regs[j]:#x}" for j in range(i, min(i+4, 31)))
            print(f"NWOAS-STALL {tag}: {seg}")
        # the fixed bootmgr object the wait loop reads (PMCC x0=0x10005610, x1 in 0x10005500..960)
        print(f"NWOAS-STALL {tag}: --- obj 0x10005500..0x10005a00 (busy-wait object) ---")
        print(_hexdump(_rd(hv, 0x10005500, 0x300), 0x10005500))
        # STACK: the caller/return-address chain reveals the OUTER wait loop (telemetry is a leaf).
        # The guest stack may be UEFI-high (>4GB, e.g. 0xae0fcbxxx) OR bootmgr-low; read whatever
        # SP_EL1 points at (no range gate -- that was the bug that skipped the UEFI stack).
        el1 = sp[1]
        if el1 > 0x10000:
            b = _rd(hv, el1, 0x180)
            print(f"NWOAS-STALL {tag}: --- stack @SP_EL1={el1:#x} (return-addr chain = wait loop) ---")
            print(_hexdump(b, el1) if b else f"  <read failed @{el1:#x}>")
        # follow every plausible pointer register (no 4GB gate -- UEFI ptrs are 0xff.../0xfef8...)
        for name, va in (("x0", regs[0]), ("x1", regs[1]), ("x2", regs[2]), ("x5", regs[5]),
                         ("x8", regs[8]), ("x19", regs[19]), ("x21", regs[21]), ("x22", regs[22])):
            if va > 0x10000 and (va & 0x3) == 0:
                b = _rd(hv, va & ~0xf, 0x40)
                if b:
                    print(f"NWOAS-STALL {tag}: *{name}({va:#x}):")
                    print(_hexdump(b, va & ~0xf))
    except Exception:
        traceback.print_exc()


# --- chain run_shell (pcie_emul backing fix first, then our probe) ---
_sp_prior = hv.run_shell            # noqa: F821
_sp_n = {"n": 0}

def _sp_run_shell(entry_msg="", exit_msg="", **kw):
    try:
        _sp_prior(entry_msg=entry_msg, exit_msg=exit_msg, **kw)
    except Exception:
        traceback.print_exc()
    _sp_n["n"] += 1
    _probe(hv, f"BK#{_sp_n['n']}")      # noqa: F821
    return EXC_RET.HANDLED

hv.run_shell = _sp_run_shell            # noqa: F821


# --- breaker: break in when boot.wim reads have frozen (the deterministic stall) ---
def _sp_logcount(patt):
    lp = os.environ.get("NWOAS_LOG")
    if not lp:
        return 0
    try:
        with open(lp, "r", errors="ignore") as f:
            return f.read().count(patt)
    except Exception:
        return 0

def _sp_breaker():
    # TARGET the bootmgfw early-init hang: UEFI BDS loads bootmgfw.efi at LBA 0x13622, then
    # bootmgfw renders the blue GOP background and hangs in early init (before any boot.wim read).
    # So wait for the bootmgfw load line, let it settle into the hang, then break in 3x (spaced)
    # to capture the STACK (outer wait loop) and the retry object -- NOT the early xHCI-init delays.
    # Trigger on the WEDGE SIGNAL itself: once bootmgfw is spinning in its early-init wedge it
    # repeatedly reads PMCCNTR inside its ETW/timing helpers (rva 0xad4xx/0xad574), which trap and
    # log "NWOAS-PMCC elr=0x100ad..". Accumulating >=15 of those == bootmgfw is loaded AND wedged
    # (not the early UEFI PCIe/xHCI delays). Robust vs the load-line's leading-zero format
    # ("EntryPoint=0x00010023490") that broke the previous string match. Break in immediately.
    while _sp_logcount("NWOAS-PMCC elr=0x100ad") < 15:
        time.sleep(5)
    for attempt in range(4):
        landed = _sp_n["n"]
        for _ in range(40):         # ~16s burst
            if _sp_n["n"] > landed:
                break
            if not getattr(hv, "_in_handler", False):   # noqa: F821
                try:
                    hv.interrupt()                       # noqa: F821
                except Exception:
                    pass
            time.sleep(0.4)
        time.sleep(45)              # let the guest sit in the same wedge, then re-probe

if not getattr(hv, "_nwoas_sp_started", False):   # noqa: F821
    hv._nwoas_sp_started = True                   # noqa: F821
    threading.Thread(target=_sp_breaker, daemon=True, name="nwoas-stall-probe").start()
    print("[emul] NWOAS-STALL probe installed (break-in at Maps-frozen stall; dumps ctx + wait object)")
