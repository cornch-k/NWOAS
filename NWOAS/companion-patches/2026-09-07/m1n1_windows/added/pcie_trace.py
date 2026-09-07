# pcie_trace.py (NWOAS) — run_guest -m hook.
# pcie_init() then SYNC-trace the PCIe MMIO BAR window so every guest access to
# 0x6c0000000+0x10000 is logged (address + guest PC) immediately. Goal: catch the exact
# access that takes the SError (0x6c0008330) and which driver issues it. Reference:
# firmware-helper BAR regs are BAR+0x3000..0x3010; an xHCI runtime reg (~0x8330) => XhciDxe.
import time
from m1n1.utils import irange
from m1n1.hv.types import TraceMode

print("[trace] pcie_init() ...")
try:
    print("[trace] pcie_init ->", p.pcie_init())
except Exception as e:
    print("[trace] pcie_init err:", e)

print("[trace] SYNC-tracing PCIe MMIO 0x6c0000000 + 0x10000 ...")
hv.trace_range(irange(0x6c0000000, 0x10000), mode=TraceMode.SYNC, name="pcie-bar")
time.sleep(0.3)
print("[trace] done -- starting guest (traced). Watch for the last MMIO before the SError.")
