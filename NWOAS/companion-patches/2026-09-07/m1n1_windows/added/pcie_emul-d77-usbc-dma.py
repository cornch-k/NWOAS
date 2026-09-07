# pcie_emul-exp5-evtdump.py (NWOAS §6-A) — CLEAN observe harness for the C-side EL2 event-ring
# dump. This module ONLY reproduces the exact environment that put the FL1100 event ring at high
# physical RAM (~0xae0f_c000) and then LETS THE GUEST RUN FREELY. It does NOT break in, does NOT
# read guest memory from Python (that path returns poison 0xabad1dea for high RAM), and does NOT
# reprogram the DART.
#
# The actual event-ring dump is done by the OUTER m1n1 (EL2 hypervisor, kmutil-installed
# build/m1n1.bin), in src/hv_vm.c::nwoas_dump_evtring: it latches the ERSTBA/DCBAAP the guest
# programs (SPTE_MAP write path), then during the USBSTS-poll livelock reads the physical ring
# straight out of DRAM with a `dc civac` (clean+invalidate to PoC) so we see what FL1100 actually
# DMA-wrote, not the guest's cached view. Because guest RAM is identity-mapped (IPA==PA:
# hv/__init__.py RAM-HIGH TraceMode.OFF), the latched ERSTBA value IS the host physical address,
# and EL2 (unlike the Python proxy) can read it. Decides:
#   EVTDUMP ring all-zero          -> FL1100 never landed the write   -> REACHABILITY
#   EVTDUMP valid completion TRBs  -> FL1100 wrote, guest reads stale  -> COHERENCE / cycle-desync
#   EVTDUMP garbage TRB types      -> wrong translation / wrong memory
# (§6-B established FL1100 sits behind DART 0x682008000 stream 1, that fix-C's BYPASS did cover it,
#  and that the apcie DART ERROR was clear -> coherence is the leading hypothesis this run tests.)
#
# Address model (identical to the evtring1 run that produced the canonical ring-at-high-RAM data):
# NWOAS_WIN_BASE=0 WIN_SIZE=0x100000000 -> guest low IPA [0,4GB) backed by [_backing,+4GB); guest
# high IPA (where Windows put the ring, ~0xae0f_c000) is identity. The FL1100 BAR BYPASS tracer is
# what routes guest MMIO through the EL2 SPTE_MAP path where the C-side instrumentation lives.
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode

print("[evtdump] pcie_init ->", p.pcie_init())
# BYPASS == SPTE_MAP that traps to EL2 and forwards to real HW (hv_vm.c hv_pa_read/write). This is
# the same path the existing NWOAS-FLC instrumentation uses, and where nwoas_dump_evtring hooks.
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x100000000", 0)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_phys_base = hv.u.ba.phys_base
_mem_size = hv.u.ba.mem_size
_backing = _phys_base + _mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[evtdump] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} | "
       f"phys_base={_phys_base:#x} mem_size={_mem_size:#x}")
hv.log("[evtdump] SETUP DONE — guest runs FREE, NO break-in. Event-ring dump comes from the "
       "outer m1n1 (EL2) as 'HVLOG: EVTDUMP ...' lines during the USBSTS livelock.")
hv.log("[evtdump] ★ USER: when the guest reaches the Windows installer / spinner, plug the mouse "
       "and watch the cursor for ~90s. Cursor moving = §3.2 breakthrough. The EVTDUMP lines tell "
       "us reachability vs coherence regardless.")


# Passive liveness heartbeat only — NO guest reads, NO break-in (both perturb / poison). Just so
# the log shows the run is alive and the harness can time out cleanly.
def _heartbeat():
    n = 0
    while True:
        time.sleep(30)
        n += 30
        hv.log(f"[evtdump] alive t={n}s (waiting for guest livelock + EL2 EVTDUMP lines)")


threading.Thread(target=_heartbeat, daemon=True).start()

# D77: route only non-debug USB-C xHCI MMIO through bounded EL2 DMA-start hook.
hv.add_tracer(irange(0x502280000, 0x4000), "usbc-d77-dma", TraceMode.BYPASS)
