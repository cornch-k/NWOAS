"""S139 NVMe controller: keep completion work out of Windows' completion DPC.

The S137 dump resolves to stornvme!NVMeCompletionDpcRoutine calling
storport!StorPortWriteRegisterUlong for the CQ-head doorbell.  S124 handled that
acknowledgement by synchronously executing more physical SSD I/O and publishing
more completions before returning to the guest.  With eight CPUs, the DPC could
therefore keep refilling the same CQ until watchdog bugcheck 0x133.

An SQ and its CQ have equal depth in the Windows configuration, while the
controller leaves one CQ slot unused.  Every legal outstanding SQ command can
therefore be completed from the SQ-tail doorbell.  A CQ-head acknowledgement
only retires entries and updates the interrupt level; it must not start I/O.
"""

import importlib.util
from pathlib import Path
import sys


_base_path = Path(__file__).parents[1] / "nvme-s124" / "controller.py"
_spec = importlib.util.spec_from_file_location("_nwoas_nvme_s124_controller", _base_path)
_base = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _base
_spec.loader.exec_module(_base)

SQ = _base.SQ
CQ = _base.CQ


class Controller(_base.Controller):
    """S124 semantics with a DPC-safe CQ-head acknowledgement path."""

    # StorNVMe used four 256-entry SQs sharing one 256-entry CQ in the first
    # S139 hardware run.  A CQ acknowledgement cannot synchronously drain those
    # SQ backlogs without returning to the same DPC loop.  Advertise one I/O
    # queue pair until completion production is moved to an async host worker.
    MAX_Q = 1

    def write(self, offset, value, width):
        if (
            width == 32
            and offset >= 0x1000
            and offset < 0x1000 + 8 * (self.MAX_Q + 1)
            and offset & 4
        ):
            qid = (offset - 0x1000) // 8
            if not (self.csts & 1) or self.csts & 2:
                return
            q = self.cq.get(qid)
            if q is None or value >= q.size:
                return self.fatal("invalid CQ head")
            consumed = (value - q.head) % q.size
            if consumed > q.pending:
                return self.fatal("CQ head advances beyond completions")
            q.head = value
            q.pending -= consumed
            self.update_irq()

            # Do not call process() here.  This write is issued from
            # NVMeCompletionDpcRoutine; physical I/O here extends that DPC and
            # can immediately reassert the same interrupt before it can yield.
            backlog = sum(
                sq.head != sq.tail
                for sq in self.sq.values()
                if sq.cqid == qid
            )
            if backlog:
                self.log(f"S139 CQ{qid} ack left {backlog} SQ backlog")
            return
        return super().write(offset, value, width)
