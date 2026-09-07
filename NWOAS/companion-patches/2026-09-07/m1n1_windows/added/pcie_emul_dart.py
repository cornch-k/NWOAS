# pcie_emul_dart.py (NWOAS) — MMIO emulation + apcie DART bypass (no kmutil).
#
# Fixes both confirmed blockers for the FL1100 USB-A xHCI:
#  1) guest EL1 MMIO/BAR access L2C-errors -> emulate the BAR via BYPASS (SPTE_MAP, EL2).
#  2) xHCI DMA (command/event rings) blocked by the unconfigured apcie DART -> put the
#     apcie DARTs in BYPASS so device DMA to physical addresses (the stubbed guest DART
#     driver programs physical) passes through untranslated.
from m1n1.utils import irange
from m1n1.hv.types import TraceMode

print("[fix] pcie_init ->", p.pcie_init())

# DART_T8020: TCR at base+0x100+4*stream; BYPASS = BYPASS_DART(bit8) | BYPASS_DAPF(bit12)
BYPASS = (1 << 8) | (1 << 12)   # 0x1100
ENABLED_STREAMS = 0xfc
TCR_OFF = 0x100
for base in (0x681008000, 0x682008000, 0x683008000):
    try:
        old_en = p.read32(base + ENABLED_STREAMS)
        p.write32(base + ENABLED_STREAMS, 0xffffffff)
        new_en = p.read32(base + ENABLED_STREAMS)
    except Exception as e:
        old_en = new_en = "err(%s)" % e
    for s in range(16):
        try:
            p.write32(base + TCR_OFF + 4 * s, BYPASS)
        except Exception:
            pass
    # TLB invalidate all streams so the bypass takes effect (in case old translate state cached)
    STREAM_SELECT = 0x34; STREAM_COMMAND = 0x20; INVALIDATE = 1 << 20; BUSY = 1 << 2
    try:
        p.write32(base + STREAM_SELECT, 0xffffffff)
        p.write32(base + STREAM_COMMAND, INVALIDATE)
        for _ in range(100):
            if not (p.read32(base + STREAM_COMMAND) & BUSY):
                break
    except Exception as e:
        print(f"[fix] dart {base:#x} tlb-inval err: {e}")
    try:
        tcr0 = "%#x" % p.read32(base + TCR_OFF)          # verify (0x1100 if write took)
    except Exception as e:
        tcr0 = "err"
    print(f"[fix] dart {base:#x}: enabled {old_en}->{new_en}, tcr[0]={tcr0} (want 0x1100), tlb-inval done")

# MMIO/BAR emulation for the FL1100
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
print("[fix] FL1100 BAR emulated + apcie DARTs bypassed -- starting guest")
