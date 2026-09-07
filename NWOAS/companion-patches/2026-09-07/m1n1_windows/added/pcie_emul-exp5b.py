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
import os
import time
import threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET   # EXP-5 breaker: run_shell patch returns EXC_RET.HANDLED

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
_nwoas_win_size = 0x80000000                                  # 2GB (EXP-5 step 5b: window growth)
_nwoas_backing  = hv.u.ba.phys_base + hv.u.ba.mem_size - _nwoas_win_size
hv.map_hw(_nwoas_win_base, _nwoas_backing, _nwoas_win_size)   # low IPA -> top-of-RAM PA (PLACEHOLDER alias; corrected by EXP-5 §2.2 below)
print(f"[emul] NWOAS low-RAM DMA alias: IPA {_nwoas_win_base:#x} +{_nwoas_win_size:#x} -> PA {_nwoas_backing:#x} (EXP-5 PLACEHOLDER)")

# ===========================================================================
# NWOAS EXP-5 §2.2 (step 5a): 208 KB backing-skew resolution — deferred remap of
# the low-RAM DMA window alias onto the UEFI-correct backing PA.
#
# The map_hw above installs a PLACEHOLDER alias whose backing =
#   hv.u.ba.phys_base + hv.u.ba.mem_size - win_size  ==  0xbc1000000
# computed from the OUTER m1n1 boot_args captured at hv-init -- i.e. BEFORE the
# INNER (guest payload) m1n1's top_of_memory_alloc() (src/utils.c:209) shrinks the
# mem_size it hands to UEFI by 0x34000 (208 KB, incl. a guard page). So the
# placeholder is 0x34000 too HIGH; its window top [0xbe0fcc000, 0xbe1000000)
# overlaps inner m1n1's own top allocations (page tables / stack) -- DANGEROUS.
#
# The UEFI DART already uses the CORRECT, LOWER value 0xbc0fcc000, which it derives
# from the SAME arithmetic (phys_base + mem_size - win_size) applied to the GUEST
# boot_args living at guest-phys PcdBootArgsPointer = 0x840000000. That address is
# identity-mapped guest RAM (RAM-HIGH tracer OFF), so hv.iface.readmem() reads it
# directly. Reading that identical struct here and re-installing the alias makes the
# hv CPU alias and the UEFI DART backing the SAME PA -> skew becomes 0 by construction.
#
# The placeholder is harmless until this fires: nothing DMAs to IPA 0x80000000
# during early boot; the correct backing only matters once Windows programs xHCI
# DMA, much later. Trigger = rate-limited poll from the handle_sync entry (guest
# sync exceptions are plentiful once UEFI runs). The remap fires EXACTLY ONCE
# (guarded by hv._nwoas_backing_fixed); afterwards the poll returns immediately.
#
# NOTE (handler-context / TLB): map_hw -> hv_map (hv_vm.c:306) rewrites the stage-2
# PTEs but does NOT itself issue a stage-2 TLB invalidate (that lives in pt_update's
# `tlbi vmalls12e1is` / init's `alle1is`). This is safe here ONLY under the premise
# that IPA 0x80000000 is never accessed before the remap, so no stale stage-2 TLB
# entry for it exists to override. See the report for the residual risk + mitigation.
# ===========================================================================
_NWOAS_EXP5_BA_PTR   = 0x840000000        # PcdBootArgsPointer (guest RAM, identity-mapped)
_NWOAS_EXP5_WIN_BASE = _nwoas_win_base    # 0x80000000
_NWOAS_EXP5_WIN_SIZE = _nwoas_win_size    # 512MB (5a = skew only; window growth is 5b)
hv._nwoas_backing_fixed = False           # one-shot guard: remap fires exactly once
hv._nwoas_exp5_poll_ctr = 0               # rate-limit counter (poll 1 in 64 sync exceptions)


def _nwoas_apply_backing_fix():
    # SHARED one-shot body (EXP-5 §2.2 5a). Re-install the low-RAM DMA window alias
    # onto the UEFI-correct backing PA read from the guest boot_args, then flush the
    # stage-2 TLB. Idempotent + defensive: callable from BOTH trigger paths -- the
    # rate-limited handle_sync poll (_nwoas_fix_backing_once, :452) and the primary
    # breaker/run_shell break-in (_exp5_run_shell) -- so it must NEVER raise.
    #
    # Returns True once the fix is in place (or was already applied); False if it could
    # not run yet (invalid/half-written boot_args, or readmem/map_hw failure) WITHOUT
    # setting the guard, so a later poll / break-in can retry. Both paths share the SAME
    # guard hv._nwoas_backing_fixed, so the fix applies EXACTLY once whichever fires.
    if hv._nwoas_backing_fixed:
        return True
    try:
        # phys_base @ +0x10, mem_size @ +0x18 (tgtypes BootArgs_r1/2/3). 0x840000000 is
        # identity-mapped guest RAM, so iface.readmem reads the physical struct directly.
        blob = hv.iface.readmem(_NWOAS_EXP5_BA_PTR + 0x10, 0x10)
        phys_base = int.from_bytes(blob[0:8], "little")
        mem_size = int.from_bytes(blob[8:16], "little")
    except Exception as e:
        # A readmem failure must NEVER perturb the caller's flow: bail out, keep the
        # (harmless) placeholder, and retry on a later poll / break-in.
        try:
            hv.log(f"NWOAS-EXP5: boot_args read failed: {e!r} (keeping placeholder, retry)")
        except Exception:
            pass
        return False
    # Validity: reject stale / zero / partially-written boot_args (readmem may catch the
    # struct half-populated). Guest RAM base is 0x800000000; a plausible mem_size is
    # 4..16 GB. Anything outside => not the real struct yet -> keep placeholder, retry.
    if not (0x800000000 <= phys_base < 0x1000000000):
        return False
    if not (0x100000000 <= mem_size < 0x400000000):
        return False
    backing = phys_base + mem_size - _NWOAS_EXP5_WIN_SIZE
    try:
        # Re-install the alias (replaces the placeholder). Wrapped defensively so a
        # remap failure can never propagate out of the caller.
        hv.map_hw(_NWOAS_EXP5_WIN_BASE, backing, _NWOAS_EXP5_WIN_SIZE)
        # NWOAS EXP-5 (adversarial-review fix): map_hw rewrites the stage-2 PTE but does
        # NOT invalidate the stage-2 TLB. If IPA WIN_BASE was accessed before this remap,
        # the DANGEROUS placeholder (0xbc1000000) could be cached in the TLB and silently
        # defeat the skew fix. pt_update() would be a no-op here (map_hw doesn't set
        # dirty_maps), so flush explicitly with the same instruction pt_update uses.
        hv.u.inst(0xd50c83df)   # tlbi vmalls12e1is (stage-1&2, current VMID, inner-shareable)
    except Exception as e:
        try:
            hv.log(f"NWOAS-EXP5: backing remap FAILED: {e!r} (keeping placeholder, retry)")
        except Exception:
            pass
        return False                              # do NOT mark fixed; retry on a later poll
    hv._nwoas_backing_fixed = True
    hv.log(f"NWOAS-EXP5: backing fixed -> {backing:#x} "
           f"(phys_base={phys_base:#x} mem_size={mem_size:#x})")
    return True


def _nwoas_fix_backing_once():
    # SECONDARY (poll) trigger: rate-limited one-shot from the handle_sync entry
    # (:452). Retained as a harmless backup to the primary breaker thread below. On the
    # tethered hv most guest traps are emulated ON-DEVICE (BYPASS / SPTE) and never
    # reach handle_sync, so this path rarely fires -- the breaker is the reliable
    # trigger. Zero overhead once fixed.
    if hv._nwoas_backing_fixed:
        return
    hv._nwoas_exp5_poll_ctr += 1
    if hv._nwoas_exp5_poll_ctr & 0x3f:            # rate-limit: act on 1 of every 64 calls
        return
    _nwoas_apply_backing_fix()


# ===========================================================================
# NWOAS EXP-5 §2.2 (option (a)): PRIMARY reliable trigger for the one-shot backing
# fix -- a daemon "breaker" thread modeled EXACTLY on nwoas_capture_hook.py (:819-867).
#
# WHY: the poll path above (_nwoas_fix_backing_once, from handle_sync :452) almost
# never fires on the tethered hv -- most guest traps (FL1100 BAR ec=0x24, DART, ...)
# are emulated ON-DEVICE (BYPASS / SPTE) and never forwarded to the host handle_sync.
# Real-HW test 2026-07-08 confirmed the map_hw+tlbi fix itself is safe (clean boot)
# but the marker "NWOAS-EXP5: backing fixed" NEVER appeared (0 fires). So drive the
# fix from a trigger that is INDEPENDENT of trap frequency: a timer thread that breaks
# the guest into the main thread (hv.interrupt -> run_shell), where the actual
# readmem/map_hw runs -- exactly as the capture hook does its proc-walk in run_shell.
# The breaker thread itself NEVER touches readmem/map_hw; it only calls hv.interrupt().
# Both paths share the SAME one-shot guard hv._nwoas_backing_fixed.
# ===========================================================================
_EXP5_SOFT_CAP_S    = 60     # if NWOAS_LOG unset: earliest time-based break-in (boot_args is
                            # populated in SEC/PrePi, but "early in UEFI" is minutes on this hv)
_EXP5_MARKER_WAIT_S = 600    # if NWOAS_LOG set: trust the DART marker; only time-fall-back after
                            # this long if the marker never shows (10 min)
_EXP5_HARD_CAP_S    = 1800   # absolute ceiling: stop retrying even if never fixed (30 min). The
                            # tethered hv takes ~30-50 min to reach boot.wim; boot_args is valid
                            # well before that, so 30 min bounds the retries generously.
_EXP5_RETRY_GAP_S   = 60     # min seconds between break-in attempts (retry w/o spamming interrupt)


def _exp5_log_has(patt):
    # Tail the logfile named by env NWOAS_LOG for a substring (same idiom as the capture
    # hook's _log_has). If the env var is unset, the log signal is unavailable -> return
    # False so the breaker falls back to the time caps.
    lp = os.environ.get("NWOAS_LOG")
    if not lp:
        return False
    try:
        with open(lp, "r", errors="ignore") as f:
            return patt in f.read()
    except Exception:
        return False


# Patch hv.run_shell to apply the fix on break-in. Only a genuinely non-default prior is
# chained (see the __self__ test below), so composition with the capture hook holds when
# THIS module is sourced AFTER the hook (prior = the hook's patch). If THIS module is
# sourced FIRST, the hook later overwrites hv.run_shell without calling ours -- so for
# composition load the capture hook BEFORE pcie_emul-exp5.py. (The 5a-isolation run uses
# -m pcie_emul-exp5.py alone, so this ordering caveat does not affect it.) Guard against
# double-patching so re-sourcing this module does not stack wrappers.
if not getattr(hv, "_nwoas_exp5_run_shell_patched", False):
    _exp5_prior_run_shell = hv.run_shell   # noqa: F821 (hv injected by run_guest.py -m scope)
    # Only CHAIN a prior patch that is genuinely non-default. The DEFAULT hv.run_shell
    # (HV.run_shell, __init__.py:487) drops into the INTERACTIVE console (shell.run_shell
    # @:508, which blocks on input()). Calling THAT from an automated break-in would hang
    # the guest at a ">>>" prompt and the fix below would never run -- the exact failure
    # mode observed 2026-07-08. The default is a BOUND METHOD (has __self__); a real patch
    # (e.g. the capture hook's module-level _patched_run_shell) is a plain function with no
    # __self__. So chain iff the prior has no __self__ (i.e. it was already patched).
    _exp5_chain_prior = (callable(_exp5_prior_run_shell)
                         and not hasattr(_exp5_prior_run_shell, "__self__"))

    def _exp5_run_shell(entry_msg="", exit_msg="", **kw):
        # Runs in main-thread / break-in context (NEVER the breaker thread). Chain a real
        # prior patch first (composition w/ the capture hook), but NEVER the interactive
        # default. Then apply the one-shot backing fix and return HANDLED to resume.
        if _exp5_chain_prior:
            try:
                _exp5_prior_run_shell(entry_msg, exit_msg, **kw)
            except Exception:
                try:
                    hv.log("NWOAS-EXP5: prior run_shell raised (chained); continuing")
                except Exception:
                    pass
        try:
            _nwoas_apply_backing_fix()
        except Exception:
            # _nwoas_apply_backing_fix is already defensive; belt-and-suspenders so a
            # break-in can never kill the guest.
            pass
        return EXC_RET.HANDLED

    hv.run_shell = _exp5_run_shell
    hv._nwoas_exp5_run_shell_patched = True


def _exp5_breaker():
    # Daemon: wait for a signal that boot_args @0x840000000 is valid, break the guest in so
    # _exp5_run_shell applies the one-shot fix, and RETRY until fixed or the hard cap. A
    # too-early / invalid boot_args read is harmless -- the fix is validity-gated and simply
    # resumes -- so we keep re-attempting (spaced out) instead of giving up early. NEVER
    # touches readmem/map_hw itself; only calls hv.interrupt().
    t0 = time.time()
    log_mode = bool(os.environ.get("NWOAS_LOG"))   # is the reliable marker path available?
    last_interrupt = None                          # wall-clock of the last break-in attempt
    while True:
        time.sleep(5)
        if hv._nwoas_backing_fixed:
            return                                 # done -- stop the thread
        elapsed = time.time() - t0
        if elapsed >= _EXP5_HARD_CAP_S:
            try:
                hv.log(f"NWOAS-EXP5: breaker hard-cap {int(elapsed)}s reached without a fix; "
                       f"poll path (:452) remains as backup")
            except Exception:
                pass
            return                                 # absolute ceiling -- stop retrying
        # Decide whether to break in now.
        if _exp5_log_has("NWOAS DART IOVA") or _exp5_log_has("backing PA 0x"):
            reason = "log-marker"                  # UEFI DART read the SAME struct -> valid now
        elif log_mode and elapsed < _EXP5_MARKER_WAIT_S:
            continue                               # NWOAS_LOG set: trust the marker, wait for it
        elif elapsed >= _EXP5_SOFT_CAP_S:
            reason = "time"                        # no marker (unset/absent) -> time-based
        else:
            continue                               # too early, no signal yet -> keep waiting
        # Rate-limit break-ins: a too-early boot_args gets re-attempted later WITHOUT
        # spamming hv.interrupt() (which also no-ops while _in_handler).
        now = time.time()
        if last_interrupt is not None and (now - last_interrupt) < _EXP5_RETRY_GAP_S:
            continue
        last_interrupt = now
        try:
            hv.log(f"NWOAS-EXP5: breaker signal ({reason}) t={int(elapsed)}s "
                   f"-> break-in to apply backing fix (retries until valid boot_args)")
        except Exception:
            pass
        try:
            hv.interrupt()   # noqa: F821 -- fix runs in _exp5_run_shell (break-in ctx)
        except Exception:
            pass
        # Loop: next iteration returns if the fix landed, else re-attempts after the gap.


if not getattr(hv, "_nwoas_exp5_breaker_started", False):
    hv._nwoas_exp5_breaker_started = True
    threading.Thread(target=_exp5_breaker, daemon=True, name="nwoas-exp5-breaker").start()
    print("[emul] NWOAS-EXP5 breaker thread started (primary one-shot backing trigger; "
          "run_shell patched + chained)")


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
    _nwoas_fix_backing_once()  # EXP-5 §2.2 (5a): rate-limited, one-shot backing-skew remap
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
