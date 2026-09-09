"""S138 guest map: retain FL1100/PCIe and omit the SoC USB-C MMIO tracer."""

import os
from m1n1.utils import irange
from m1n1.hv.types import TraceMode

print('[s138] pcie_init ->', p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), 'pcie-fl1100-emul', TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), 'apcie-dart-emul', TraceMode.BYPASS)

window_size = int(os.environ.get('NWOAS_WIN_SIZE') or '0x100000000', 0)
window_base = int(os.environ.get('NWOAS_WIN_BASE') or '0', 0)
skew = int(os.environ.get('NWOAS_WIN_SKEW') or '0x34000', 0)
backing = hv.u.ba.phys_base + hv.u.ba.mem_size - window_size - skew
hv.map_hw(window_base, backing, window_size)
hv.log(f'[s138] USB-A-only low-window map {window_base:#x}+{window_size:#x} -> {backing:#x}')
