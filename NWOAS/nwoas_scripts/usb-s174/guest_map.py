"""S174 guest map: retain validated PCIe/low window and expose non-debug USB-C tracer."""

import os
from m1n1.utils import irange
from m1n1.hv.types import TraceMode

print('[s174] pcie_init ->', p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), 'pcie-fl1100-emul', TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), 'apcie-dart-emul', TraceMode.BYPASS)

window_size = int(os.environ.get('NWOAS_WIN_SIZE') or '0x100000000', 0)
window_base = int(os.environ.get('NWOAS_WIN_BASE') or '0', 0)
skew = int(os.environ.get('NWOAS_WIN_SKEW') or '0x34000', 0)
backing = hv.u.ba.phys_base + hv.u.ba.mem_size - window_size - skew
hv.map_hw(window_base, backing, window_size)
hv.log(f'[s174] low-window map {window_base:#x}+{window_size:#x} -> {backing:#x}')

# Equivalent non-debug controller trap range to the older D81 module.
hv.add_tracer(irange(0x502280000, 0x10000), "usbc-s174", TraceMode.BYPASS)
