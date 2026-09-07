# pcie_emul.py (NWOAS) — THE FIX ATTEMPT (no kmutil, no rebuild).
#
# Root cause (hardware-confirmed): the guest's EL1 access to the FL1100 xHCI BAR
# (0x6c000xxxx) fails with an L2C bus error, while the outer m1n1 hv (EL2) can access
# it fine (SYNC trace read = 0x401). The guest's config/ECAM access works; only the
# BAR (memory space) fails. Memory type (nGnRnE/nGnRE) is NOT the fix (tested 4 ways).
#
# Fix: map the FL1100 BAR as a software-emulated region (SPTE_MAP, via TraceMode.BYPASS
# = map_sw with no tracing). Then the guest's access traps synchronously and the outer
# hv emulates it in C via hv_pa_read/hv_pa_write (an EL2 access, which WORKS), instead
# of a HW passthrough (which L2C-errors for the guest). C-side => fast (no Python).
from m1n1.utils import irange
from m1n1.hv.types import TraceMode

print("[emul] pcie_init ->", p.pcie_init())
# FL1100 BAR is assigned at the base of the PCIe MMIO32 window (0x6c0000000). Cover
# 1MB generously (only actual accesses trap, so size is cheap).
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
print("[emul] FL1100 BAR 0x6c0000000 +0x100000 -> BYPASS (SPTE_MAP, C-side EL2 emulation)")
# Also emulate the apcie DART controller register regions (0x681008000/0x682008000/
# 0x683008000, each 0x4000) so the guest UEFI's DART-IoMmu setup writes take effect via
# the EL2 emulation path even if a direct guest EL1 access to that region would fault.
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
print("[emul] apcie DART regs 0x681000000 +0x3000000 -> BYPASS")

# NWOAS Stage 4: alias a low guest-IPA DMA window onto the TOP of real RAM, so the platform
# xHCI (DSDT SCB0/XHC0 PNP0D10) has real, 32-bit-device-reachable RAM below 4GB. The Apple
# PCIe DART is 32-bit-IOVA-only and Windows' AllocateCommonBuffer caps a 32-bit device's
# PHYSICAL allocation at 4GB (it does not relocate via ACPI _DMA _TRA), so all guest RAM at
# its true high base (~0x8_3d0f8000) is unreachable. map_hw supports IPA!=PA aliasing. This
# MUST agree with: UEFI memory map (MemoryInitPeiLib NWOAS_DMA_WIN_BASE/SIZE + reserved
# backing), the DART EBS window (AppleDartIoMmuDxe APCIE_WIN_IOVA_BASE/SIZE), and DSDT _DMA
# (_TRA=0). backing = RAM top - size == UEFI (PcdSystemMemoryBase+Size) - size.
_nwoas_win_base = 0x80000000
_nwoas_win_size = 0x20000000                                  # 512MB
_nwoas_backing  = hv.u.ba.phys_base + hv.u.ba.mem_size - _nwoas_win_size
hv.map_hw(_nwoas_win_base, _nwoas_backing, _nwoas_win_size)   # low IPA -> top-of-RAM PA (alias)
print(f"[emul] NWOAS low-RAM DMA alias: IPA {_nwoas_win_base:#x} +{_nwoas_win_size:#x} -> PA {_nwoas_backing:#x}")

# NOTE: a host-side PORTSC read-hook (RESERVED, proxy read32 per access) was tried
# (run12) to identify the storming port without a kmutil, but it was far too slow
# (proxy round-trip per port poll) -- the boot did not even reach boot.wim in 33 min.
# Reverted; PORTSC-at-stall is captured C-side in the device m1n1 diagnostic instead.

print("[emul] done -- starting guest. Guest EL1 BAR accesses now emulated by the hv from EL2.")

# NWOAS uniprocessor boot: block guest secondary CPU start.
# Windows (AIC HAL extension) starts APs by writing the Apple PMGR CPU_START MMIO, which
# the host hook cpustart_wh catches -> start_secondary -> hv_start_secondary, entering a
# physical core into Windows AP code. m1n1's Windows-guest SMP path is incomplete, so
# that AP code faults on a near-null per-CPU pointer (run7 cpu3 NULL+0xe, run8 cpu1
# elr=0x6c) -- a whack-a-mole of secondary-CPU crashes that block the boot CPU from
# reaching the winload->kernel handoff (stage 8). Neither the MADT enabled flag (run8)
# nor the inner-m1n1 spin_table guard stops this path; the host cpustart hook is the
# actual lever. Refuse to bring any secondary into the guest: the core stays parked in
# inner m1n1, cpu_state_rh reports it "not running", and Windows should fall back to a
# uniprocessor boot (boot-time APs are only a parallel-catalog-hashing optimization). If
# Windows instead hangs waiting for an AP, that tells us SMP is mandatory here.
_nwoas_orig_start_secondary = hv.start_secondary
def _nwoas_block_start_secondary(die, cluster, cpu):
    hv.log(f"HVLOG: NWOAS blocking guest secondary start {die}:{cluster}:{cpu} (uniprocessor boot)")
    # deliberately do NOT call the original / hv_start_secondary: leave the core parked.
# AP-unblock TESTED (run16) and REVERTED: allowing a guest secondary to start makes
# m1n1's own HV panic early -- "Failed to rendezvous, missing CPUs: 0x2" -- because the
# started core runs guest code and never joins m1n1's SMP rendezvous. So the block is
# required, and the pre-handoff wedge is NOT caused by it (unblocking fails far earlier).
hv.start_secondary = _nwoas_block_start_secondary
print("[emul] NWOAS uniprocessor: guest secondary CPU start BLOCKED (marker 'NWOAS blocking guest secondary')")

# ===========================================================================
# NWOAS null-loop one-shot diagnostic + clean stop hook v2 (host-side only).
#
# retest40 symptoms (both are the same 'branch/step into a null-ish target'
# family, but hit different ECs, and the crash offset varies run-to-run):
#   run1: guest DATA abort -- ldrb from NULL+0xe (FAR=0xe) in winload; stock
#         handle_dabort() only fixes atomic/exclusive aborts, returns None here,
#         so handle_exception() prints context + falls to run_shell() which (no
#         stdin) returns HANDLED without advancing ELR -> re-fault forever.
#   run2: guest INSTRUCTION abort -- cpu branched to 0x140 (ELR=FAR=0x140,
#         EC=0x20 IABORT_LOWER). handle_sync() has NO IABORT branch, so it was
#         never seen by v1's handle_dabort wrapper. Worse, the stock unhandled
#         path called u.print_context() -> disassemble_at(elr_phys-0x10) ->
#         iface.readmem(~0x130). Because the guest MMU was off, hv_translate
#         passed elr through as 0x140 (sub-DRAM), so the EL2 m1n1 took its OWN
#         data abort reading 0x130, desynced the proxy stream, and run_guest
#         died (UartRemoteError on REQ_MEMREAD).
#
# v2 fixes v1's three gaps:
#   1. IABORT coverage: wrap handle_sync (the single dispatch point for every
#      SYNC-lower EC) instead of handle_dabort, so BOTH aborts are caught.
#   2. near-NULL / low-phys IMMEDIATE trigger: an unhandled abort with elr/far
#      < 0x10000, or a nonzero sub-DRAM elr_phys/far_phys, dumps on the FIRST
#      hit (no streak wait) -- because that is exactly when the stock
#      print_context would kill the proxy before the streak could accumulate.
#   3. proxy-death prevention: EVERY memory read in the dump goes through
#      _nl_safe_phys (VA -> hv_translate -> require a DRAM-range physical). A
#      translate of 0 (fault) or a sub-DRAM/wild physical is skipped, never
#      handed to iface.readmem. So the dump itself cannot repeat run2's crash.
#
# On trigger it emits a maximal ONE-SHOT diagnostic (grep marker
# "NWOAS-NULLLOOP-DUMP") then cleanly exits the guest via the same proven path
# the interactive hv `exit()` reaches: p.exit(EXC_RET.EXIT_GUEST); run_guest
# unwinds to its shell instead of spinning.
#
# Scope guarantees (do not perturb the normal boot):
#   * Engages ONLY for SYNC-lower faults the stock dispatcher left UNHANDLED,
#     and only for EC in {DABORT_LOWER, IABORT_LOWER}. BYPASS C-side emulation,
#     vuart traps, handled MSR/HVC/step, and atomic/exclusive daborts all return
#     truthy from handle_sync and reset the streak -> never engage.
#   * The dump + stop fire at most ONCE (guarded by hv._nwoas_nl_dumped).
#   * If p.exit(EXIT_GUEST) ever fails, it degrades to loop-continue (the dump
#     still happened exactly once).
#
# Implementation is pure runtime monkey-patching from this -m script; the m1n1
# tree is UNMODIFIED. Rollback: restore pcie_emul.py from the .bak-nwoas-v1
# backup, or delete this block (lines 27 onward).
# ===========================================================================
import os as _nl_os
import traceback as _nl_tb
from m1n1.proxy import START as _NL_START, EXC as _NL_EXC, EXC_RET as _NL_EXC_RET
from m1n1.utils import chexdump as _nl_chexdump
try:
    from m1n1.asm import ARMAsm as _NL_ARMAsm
except Exception:
    _NL_ARMAsm = None
try:
    from m1n1.sysreg import ESR_EC as _NL_ESR_EC
except Exception:
    _NL_ESR_EC = None
try:
    from m1n1.sysreg import VBAR_EL12 as _NL_VBAR_EL12
except Exception:
    _NL_VBAR_EL12 = None

_NL_MARK = "NWOAS-NULLLOOP-DUMP"
_NL_THRESHOLD = int(_nl_os.environ.get("NWOAS_NULLLOOP_N", "3"))
_NL_PAGE = 0x4000              # Apple 16 KiB guest page
_NL_NEAR_NULL = 0x10000        # elr/far below this => near-NULL, dump immediately
_NL_DRAM_BASE = 0x800000000    # M1 (t8103) DRAM base; anything below is MMIO/void
_NL_PHYS_CAP = 0x1800000000    # generous upper bound (>> 16 GB) to reject wild PAs

# EC values for the two lower-EL fault classes we own. run1 = DABORT_LOWER (0x24),
# run2 = IABORT_LOWER (0x20). v1 only covered DABORT (via handle_dabort); v2 covers
# both by wrapping handle_sync (the single dispatch point for all SYNC-lower ECs).
if _NL_ESR_EC is not None:
    _NL_FAULT_ECS = (int(_NL_ESR_EC.DABORT_LOWER), int(_NL_ESR_EC.IABORT_LOWER))
else:
    _NL_FAULT_ECS = (0x24, 0x20)

# per-hv streak / one-shot state
hv._nwoas_nl_last_key = None
hv._nwoas_nl_count = 0
hv._nwoas_nl_dumped = False


def _nl_log(s):
    try:
        hv.log(f"{_NL_MARK}: {s}")
    except Exception:
        print(f"{_NL_MARK}: {s}")


def _nl_safe_phys(pa):
    # A physical address is safe to hand to iface.readmem ONLY if it lands in real
    # DRAM. Reading a sub-DRAM physical (0 < pa < DRAM base) makes the EL2 m1n1 take
    # its OWN data abort (observed run2: elr_phys=0x140 -> EL2 FAR~0x130 -> proxy
    # stream desync -> run_guest death). Return None => caller MUST skip the read.
    try:
        pa = int(pa)
    except Exception:
        return None
    if pa and _NL_DRAM_BASE <= pa < _NL_PHYS_CAP:
        return pa
    return None


def _nl_phys_dangerous(pa):
    # True iff pa is NONZERO but OUTSIDE safe DRAM (sub-DRAM MMU-off passthrough like
    # run2's 0x140, OR a wild PA) -- i.e. exactly a physical the stock print_context
    # would hand to iface.readmem and possibly crash EL2 with. pa==0 is 'unmapped';
    # print_context already skips those, so 0 is NOT dangerous.
    try:
        pa = int(pa)
    except Exception:
        return False
    return pa != 0 and _nl_safe_phys(pa) is None


def _nl_read_phys(pa, size):
    # Guarded physical read: refuse sub-DRAM / wild PAs so we never crash EL2.
    safe = _nl_safe_phys(pa)
    if safe is None:
        _nl_log(f"[phys @ {int(pa):#x} +{size:#x} low/unmapped -> skipped (guard)]")
        return None
    try:
        return hv.iface.readmem(safe, size)
    except Exception as e:
        _nl_log(f"[phys read @ {safe:#x} +{size:#x} failed: {e!r}]")
        return None


def _nl_read_va(va, size):
    # Page-aware guest VA read: translate (stage1+2) per 16 KiB page, then read
    # physically THROUGH the _nl_safe_phys guard. Unmapped/low-phys pages are padded
    # with 0x00 and flagged, never read (proxy-death prevention, task gap #3).
    out = bytearray()
    cur, end = va, va + size
    while cur < end:
        page_end = (cur | (_NL_PAGE - 1)) + 1
        n = min(page_end - cur, end - cur)
        pa = 0
        try:
            pa = hv.p.hv_translate(cur, False, False)
        except Exception as e:
            _nl_log(f"[translate {cur:#x} failed: {e!r}]")
        safe = _nl_safe_phys(pa)
        if safe is None:
            _nl_log(f"[va {cur:#x} +{n:#x} -> pa {int(pa):#x} unmapped/low -> skipped]")
            out += b"\x00" * n
        else:
            chunk = _nl_read_phys(safe, n)
            out += chunk if chunk is not None else b"\x00" * n
        cur += n
    return bytes(out)


def _nl_hexblock(title, data, base):
    _nl_log(title)
    if not data:
        _nl_log("  <no data>")
        return
    _nl_chexdump(data, base, print_fn=lambda s: _nl_log("  " + s))


def _nl_dump(ctx):
    try:
        esr = ctx.esr
        spsr = ctx.spsr
        el = spsr.M >> 2
        _nl_log("==================== NULL-LOOP ONE-SHOT DUMP ====================")
        _nl_log(f"trigger: unhandled sync fault x{hv._nwoas_nl_count} "
                f"(threshold {_NL_THRESHOLD}) cpu={ctx.cpu_id} mpidr={ctx.mpidr:#x}")
        _nl_log(f"ELR ={ctx.elr:#x} (phys {ctx.elr_phys:#x})")
        _nl_log(f"FAR ={ctx.far:#x} (phys {ctx.far_phys:#x})")
        _nl_log(f"ESR ={esr.value:#x} EC={esr.EC!s} ISS={esr.ISS:#x}")
        _nl_log(f"SPSR={spsr.value:#x} ({spsr.M.name}) AFSR1={ctx.afsr1:#x}")
        _nl_log(f"SP_EL{el}={ctx.sp[el]:#x} (phys {ctx.sp_phys:#x})")

        # VBAR_EL1 (guest vector base, read as VBAR_EL12 from EL2 -- a sysreg read,
        # never touches guest memory). Attributes an IABORT target: if ELR == VBAR
        # + a vector offset the cpu faulted taking an exception; a bare 0x140 with
        # VBAR unset means an absolute branch into a null-ish vector page.
        try:
            if _NL_VBAR_EL12 is not None:
                vbar = hv.u.mrs(_NL_VBAR_EL12)
                _nl_log(f"VBAR_EL1={vbar:#x}")
        except Exception as e:
            _nl_log(f"[VBAR_EL1 read failed: {e!r}]")

        # Faulting instruction bytes + disassembly -- ONLY if elr_phys is real DRAM
        # (an IABORT/MMU-off elr_phys is sub-DRAM and would crash EL2; we skip it).
        safe_elr = _nl_safe_phys(ctx.elr_phys)
        if safe_elr is not None:
            raw = _nl_read_phys(safe_elr, 4)
            if raw and len(raw) == 4:
                word = int.from_bytes(raw, "little")
                _nl_log(f"insn @ELR = {word:#010x}")
                if _NL_ARMAsm is not None:
                    try:
                        c = _NL_ARMAsm(f".inst {word}", ctx.elr)
                        _nl_log(f"insn disas = {'; '.join(c.disassemble())}")
                    except Exception as e:
                        _nl_log(f"[disas failed: {e!r}]")
        else:
            _nl_log(f"insn @ELR: elr_phys {ctx.elr_phys:#x} not DRAM -> skipped "
                    f"(IABORT / MMU-off target)")

        # Full GPR file (x0..x31).
        for i in range(0, 32, 4):
            j = min(31, i + 3)
            _nl_log(f"x{i}-x{j} = " + " ".join(f"{ctx.regs[k]:016x}" for k in range(i, j + 1)))

        # ELR +/-0x80 window, read PHYSICALLY (guarded), ground-truth bytes.
        if safe_elr is not None:
            base = safe_elr - 0x80
            _nl_hexblock(f"mem @ELR-0x80..+0x80 (phys {base:#x}):",
                         _nl_read_phys(base, 0x100), base)

        # x30 (LR / caller) +/-0x100 CODE bytes. THE decisive datum for offline
        # attribution of who branched/returned into the fault: x30 should point at
        # mapped code even when ELR/FAR is a bogus near-NULL target.
        x30 = ctx.regs[30]
        _nl_hexblock(f"code @x30-0x100..+0x100 {x30 - 0x100:#x} (0x200):",
                     _nl_read_va(x30 - 0x100, 0x200), x30 - 0x100)

        # x21 (suspected per-CPU / loader struct pointer) forward 0x200.
        x21 = ctx.regs[21]
        _nl_hexblock(f"data @x21 {x21:#x} (0x200):", _nl_read_va(x21, 0x200), x21)

        # x19 / x20 (callee-saved, often base/struct pointers) +/-0x40 => 0x80 each.
        x19 = ctx.regs[19]
        _nl_hexblock(f"data @x19-0x40 {x19 - 0x40:#x} (0x80):",
                     _nl_read_va(x19 - 0x40, 0x80), x19 - 0x40)
        x20 = ctx.regs[20]
        _nl_hexblock(f"data @x20-0x40 {x20 - 0x40:#x} (0x80):",
                     _nl_read_va(x20 - 0x40, 0x80), x20 - 0x40)

        # SP_EL1 stack (0x200).
        sp = ctx.sp[el]
        _nl_hexblock(f"stack @SP {sp:#x} (0x200):", _nl_read_va(sp, 0x200), sp)

        # Frame from x29 (0x100).
        x29 = ctx.regs[29]
        _nl_hexblock(f"frame @x29 {x29:#x} (0x100):", _nl_read_va(x29, 0x100), x29)

        _nl_log("================== END NULL-LOOP ONE-SHOT DUMP ==================")
    except Exception as e:
        _nl_log(f"[dump error: {e!r}]")
        try:
            _nl_tb.print_exc()
        except Exception:
            pass


class _NWOASNullLoopStop(BaseException):
    """Raised out of handle_sync to request a clean guest exit.

    Subclasses BaseException (not Exception) on purpose: HV.handle_exception's
    inner `except Exception` must NOT swallow it, so it propagates out of the
    original handle_exception -- BEFORE the proxy-killing print_context runs --
    and is caught by the wrapper installed below."""
    pass


_nl_orig_handle_sync = hv.handle_sync  # bound original


def _nl_ec_int(ctx):
    # Exception class as a plain int, robust to enum/adapter quirks.
    try:
        return int(ctx.esr.EC)
    except Exception:
        try:
            return (int(ctx.esr.value) >> 26) & 0x3f
        except Exception:
            return None


def _nl_handle_sync(ctx):
    # v2 single decision point for ALL SYNC-lower ECs: covers both DABORT (run1)
    # and IABORT (run2). Run the stock dispatcher first; only engage when it leaves
    # the fault UNHANDLED (stock code then prints context + run_shell -> re-fault
    # forever, and for a near-NULL/low-phys elr also crashes the EL2 proxy via the
    # code-context read).
    #
    # F2 PRE-DISPATCH GUARD (added post-review): the stock handle_dabort reads
    # read32(ctx.elr_phys) (and, for atomic aborts, read32(far_phys)) with NO guard
    # (__init__.py:1223/1226). A DABORT whose elr_phys/far_phys is sub-DRAM would
    # kill the EL2 proxy INSIDE the stock dispatcher -- before the post-call
    # immediate check below could fire. So detect near-NULL / dangerous-phys faults
    # up-front and never call stock for them. Non-immediate faults still go through
    # stock (their elr_phys is 0 or real DRAM, so the stock read is safe).
    _ec_pre = _nl_ec_int(ctx)
    if _ec_pre in _NL_FAULT_ECS:
        _elr_pre = int(ctx.elr)
        _far_pre = int(ctx.far)
        if ((_elr_pre < _NL_NEAR_NULL) or (_far_pre < _NL_NEAR_NULL)
                or _nl_phys_dangerous(getattr(ctx, "elr_phys", 0))
                or _nl_phys_dangerous(getattr(ctx, "far_phys", 0))):
            _key_pre = (_ec_pre, _elr_pre, _far_pre)
            if _key_pre == hv._nwoas_nl_last_key:
                hv._nwoas_nl_count += 1
            else:
                hv._nwoas_nl_last_key = _key_pre
                hv._nwoas_nl_count = 1
            if not hv._nwoas_nl_dumped:
                hv._nwoas_nl_dumped = True
                _nl_log(f"detected near-NULL/dangerous-phys fault EC={_ec_pre:#x} "
                        f"elr={_elr_pre:#x} far={_far_pre:#x} "
                        f"elr_phys={int(getattr(ctx, 'elr_phys', 0)):#x} "
                        f"-> IMMEDIATE one-shot dump + clean stop (pre-dispatch, F2 guard)")
                _nl_dump(ctx)
            else:
                _nl_log(f"near-NULL/dangerous-phys fault re-entry EC={_ec_pre:#x} "
                        f"elr={_elr_pre:#x} far={_far_pre:#x} (already dumped) "
                        f"-> clean stop, no re-dump (pre-dispatch)")
            raise _NWOASNullLoopStop()

    r = _nl_orig_handle_sync(ctx)
    if r:
        # Handled (MSR/HVC/step/atomic-dabort/...) -> ELR advances -> not a loop.
        hv._nwoas_nl_last_key = None
        hv._nwoas_nl_count = 0
        return r

    ec = _nl_ec_int(ctx)
    if ec not in _NL_FAULT_ECS:
        # Not an instruction/data abort we own (e.g. an unhandled MSR): leave the
        # stock unhandled path (print_context / run_shell) exactly as-is.
        return r

    elr = int(ctx.elr)
    far = int(ctx.far)
    key = (ec, elr, far)
    if key == hv._nwoas_nl_last_key:
        hv._nwoas_nl_count += 1
    else:
        hv._nwoas_nl_last_key = key
        hv._nwoas_nl_count = 1

    # IMMEDIATE conditions -- ANY of these means the stock unhandled path
    # (u.print_context -> disassemble_at -> iface.readmem(elr_phys-0x10)) could hand
    # the EL2 m1n1 a physical it must NOT read, killing the proxy (run2). For these we
    # ALWAYS raise the clean-stop sentinel to PREEMPT print_context (dumping only
    # once), even after a prior dump or a degraded exit -- otherwise a re-entering
    # near-NULL fault would fall through to the killer:
    #   * near-NULL elr/far (< 0x10000): execution/access that low cannot be
    #     legitimate; also a hard diagnostic requirement (task gap #2).
    #   * elr_phys/far_phys nonzero but OUTSIDE safe DRAM (sub-DRAM MMU-off
    #     passthrough like run2's 0x140, or a wild PA) == exactly what the dump
    #     refuses to read, hence exactly what print_context must not read either.
    near_null = (elr < _NL_NEAR_NULL) or (far < _NL_NEAR_NULL)
    danger_phys = (_nl_phys_dangerous(getattr(ctx, "elr_phys", 0))
                   or _nl_phys_dangerous(getattr(ctx, "far_phys", 0)))
    immediate = near_null or danger_phys

    if immediate:
        if not hv._nwoas_nl_dumped:
            hv._nwoas_nl_dumped = True
            _nl_log(f"detected near-NULL/dangerous-phys fault EC={ec:#x} elr={elr:#x} "
                    f"far={far:#x} elr_phys={int(getattr(ctx, 'elr_phys', 0)):#x} "
                    f"-> IMMEDIATE one-shot dump + clean stop")
            _nl_dump(ctx)
        else:
            _nl_log(f"near-NULL/dangerous-phys fault re-entry EC={ec:#x} elr={elr:#x} "
                    f"far={far:#x} (already dumped) -> clean stop, no re-dump "
                    f"(preempt print_context)")
        raise _NWOASNullLoopStop()

    # STREAK path: non-immediate (elr_phys is either 0 or real DRAM, so the stock
    # print_context read is safe). Dump+stop once when the SAME fault repeats
    # NWOAS_NULLLOOP_N times -- the classic run1-style re-fault-forever loop.
    if not hv._nwoas_nl_dumped and hv._nwoas_nl_count >= _NL_THRESHOLD:
        hv._nwoas_nl_dumped = True
        _nl_log(f"detected null-loop streak EC={ec:#x} elr={elr:#x} far={far:#x} "
                f"repeated {hv._nwoas_nl_count}x (threshold {_NL_THRESHOLD}) "
                f"-> one-shot dump + clean stop")
        _nl_dump(ctx)
        raise _NWOASNullLoopStop()
    return r


hv.handle_sync = _nl_handle_sync


_nl_orig_handle_exception = hv.handle_exception  # bound original


def _nl_clean_stop():
    # Replicate the tail of HV.handle_exception's guest-exit path -- the exact
    # sequence the interactive `exit()` command reaches after run_shell() returns
    # EXC_RET.EXIT_GUEST: pt_update(); _commit_context(); ctx=None; p.exit(EXIT_GUEST).
    _nl_log("issuing clean guest exit (EXC_RET.EXIT_GUEST) -- run_guest will unwind")
    try:
        hv.pt_update()
    except Exception as e:
        _nl_log(f"[pt_update failed: {e!r}]")
    try:
        hv._commit_context()
    except Exception as e:
        _nl_log(f"[_commit_context failed: {e!r}]")
    hv.ctx = None
    hv.exc_orig_cpu = None
    try:
        hv.p.exit(_NL_EXC_RET.EXIT_GUEST)
    except Exception as e:
        _nl_log(f"[p.exit(EXIT_GUEST) failed: {e!r}] -- degrading to loop-continue")
    hv._in_handler = False


def _nl_handle_exception(reason, code, info):
    try:
        return _nl_orig_handle_exception(reason, code, info)
    except _NWOASNullLoopStop:
        _nl_clean_stop()
        return None


# init() bound the ORIGINAL handle_exception into iface.handlers, so shadowing the
# attribute alone would not intercept it -- re-register on the SYNC lower-EL path
# (the only path that reaches handle_sync and can raise the null-loop sentinel).
hv.iface.set_handler(_NL_START.EXCEPTION_LOWER, _NL_EXC.SYNC, _nl_handle_exception)
hv.handle_exception = _nl_handle_exception  # keep the attribute consistent too
print(f"[emul] NWOAS null-loop hook v2 armed (N={_NL_THRESHOLD}, "
      f"near-NULL<{_NL_NEAR_NULL:#x}, DRAM>={_NL_DRAM_BASE:#x}, marker '{_NL_MARK}') "
      f"-- IABORT+DABORT via handle_sync, low-phys reads guarded")
