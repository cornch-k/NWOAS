"""Enable the target-local controller once; retain NS2 data/service transport."""
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
_controller=hv._nwoas_nvme[0]
_primary=_controller.ns.primary
_link=_controller.ns.link
if (_controller.cc or _controller.sq or _controller.cq or
    _primary.block_count!=61279344 or _primary.write_first!=53839104 or
    _primary.write_last!=59968629 or not _primary.cache_enabled or
    _link.block_count!=8448 or _primary.mdts!=8):
    raise RuntimeError('S229 preboot controller/policy mismatch')
if p.nwoas_nvme_fastpath(17)!=0x5332323900000001:
    raise RuntimeError('S229 target capability mismatch')
if p.nwoas_nvme_fastpath(16,_link.block_count,_primary.mdts)!=1:
    raise RuntimeError('S229 target controller initialization failed')

def _unexpected_read(address,width):
    raise RuntimeError(f'S229 unexpected host NVMe read {address:#x}/{width}')

def _unexpected_write(address,value,width):
    raise RuntimeError(f'S229 unexpected host NVMe write {address:#x}/{width}')

# Same keys/ranges replace S160's control handlers. The old Controller remains
# solely as an NS2 namespace container; it must not serve guest control accesses.
hv.add_tracer(irange(0x700000000,0x100000),'s93-nvme-ecam',TraceMode.HOOK,
              read=_unexpected_read,write=_unexpected_write)
hv.add_tracer(irange(0x700100000,0x4000),'s93-nvme-bar',TraceMode.HOOK,
              read=_unexpected_read,write=_unexpected_write)
hv.log('[S229] target owns PCI/control/admin; NS2 and host boot setup remain')
