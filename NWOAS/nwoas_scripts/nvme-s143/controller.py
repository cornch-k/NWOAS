"""S143 NVMe controller: defer I/O submitted by the completion DPC.

StorPort masks the legacy INTx line while its completion DPC runs.  StorNVMe
may submit replacement requests and ring the SQ tail before that DPC returns.
Publishing those completions immediately lets the same DPC keep consuming new
CQEs until bugcheck 0x133.  While INTx is masked, record the SQ tail only.  At
INTMC unmask, complete at most one deferred command before raising the line.
"""

import importlib.util
from pathlib import Path
import sys


_base_path = Path(__file__).parents[1] / "nvme-s139" / "controller.py"
_spec = importlib.util.spec_from_file_location("_nwoas_nvme_s139_controller", _base_path)
_base = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _base
_spec.loader.exec_module(_base)

SQ = _base.SQ
CQ = _base.CQ


class Controller(_base.Controller):
    """S139 plus a one-command handoff at legacy-interrupt unmask."""

    def _process_one(self, qid):
        sq = self.sq[qid]
        if sq.head == sq.tail:
            return False
        real_tail = sq.tail
        sq.tail = (sq.head + 1) % sq.size
        try:
            super().process(qid)
        finally:
            sq.tail = real_tail
        return True

    def write(self, offset, value, width):
        # A replacement request submitted while StorPort has INTx masked is
        # still running in its completion DPC.  Do not make a CQE visible yet.
        if (
            width == 32
            and offset >= 0x1000
            and offset < 0x1000 + 8 * (self.MAX_Q + 1)
            and not (offset & 4)
        ):
            qid = (offset - 0x1000) // 8
            if qid != 0 and self.mask & 1:
                if not (self.csts & 1) or self.csts & 2:
                    return
                q = self.sq.get(qid)
                if q is None or value >= q.size:
                    return self.fatal("invalid SQ tail")
                q.tail = value
                self.log(f"S143 defer SQ{qid} tail={value}")
                return

        # INTMC is StorPort's end-of-DPC handoff for legacy INTx.  Publish a
        # single deferred completion while the line remains masked, then let
        # the base implementation clear the mask and assert it if necessary.
        if width == 32 and offset == 0x10 and value & 1 and self.mask & 1:
            for qid in sorted(self.sq):
                if qid and self.sq[qid].head != self.sq[qid].tail:
                    self._process_one(qid)
                    self.log(f"S143 release one SQ{qid} command at INTMC")
                    break

        return super().write(offset, value, width)
