"""S224 opt-in target read projection; Python still owns control/admin writes.
Run after the existing S160 controller module; replace only its tracer callbacks.
"""
from m1n1.hv.types import TraceMode
from m1n1.utils import irange
_s224_controller = hv._nwoas_nvme[0]
_s224_pci_write = pci_write
_s224_mmio_write = mmio_write
_s224_signature = 0x5332323400000001
if p.nwoas_nvme_fastpath(12) != _s224_signature:
    raise RuntimeError('S224 read-projection capability mismatch')
if _s224_controller.MAX_Q != 1 or _s224_controller.MAX_DEPTH != 256 or _s224_controller.bar != 0x700100000:
    raise RuntimeError('S224 projection identity mismatch')

def _s224_publish():
    c = _s224_controller
    fields = [c.cc, c.csts, c.aqa, c.mask]
    if any(type(x) is not int or not 0 <= x <= 0xffffffff for x in fields):
        raise ValueError('S224 non-dword controller state')
    pending = any(q.ien and q.pending for q in c.cq.values())
    flags = c.command | (bool(pending) << 16) | (bool(c.probe_low) << 17) | (bool(c.probe_high) << 18) | (1 << 63)
    if p.nwoas_nvme_fastpath(11, c.cc | (c.csts << 32), c.aqa | (c.mask << 32), c.asq, c.acq, flags) != _s224_signature:
        raise RuntimeError('S224 snapshot publication failed')

def _s224_write(base, addr, value, width):
    try:
        base(addr, value, width)
        _s224_publish()
    except Exception:
        # Never resume the guest with a known stale projection.
        p.nwoas_nvme_fastpath(11)
        raise

def _s224_pci(addr, value, width):
    return _s224_write(_s224_pci_write, addr, value, width)

def _s224_mmio(addr, value, width):
    return _s224_write(_s224_mmio_write, addr, value, width)

hv.add_tracer(irange(0x700000000, 0x100000), 's93-nvme-ecam', TraceMode.HOOK, read=pci_read, write=_s224_pci)
hv.add_tracer(irange(0x700100000, 0x4000), 's93-nvme-bar', TraceMode.HOOK, read=mmio_read, write=_s224_mmio)
_s224_publish()
hv.log('[S224] target-local PCI/register reads enabled; host control/admin writes remain')
