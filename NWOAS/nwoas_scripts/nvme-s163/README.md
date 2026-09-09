# S163 — bounded NVMe level interrupt reassert spacing

Experimental, not a finished driver fix. S160b performance improvement retained;
S162 CQ attribute probe retained. Source default NWOAS_NVME_REASSERT_US=0.
Hardware candidates explicitly build with 200 or 50 microseconds.

After virtual EOI900 is observed in the existing maintenance path, record its
physical counter time. The existing IRQ900 bridge leaves the source asserted,
but waits until at least200us have elapsed before injecting a new level IRQ.
The5kHzpoll/exception exits revisit the condition. No queue/head/phase/mask
mutation, no Windows binary patch, no watchdogdisable, no timer/USB throttling.
LRfull and outstandingIRQ checks retain pending source for a laterattempt.

Motivation: exact storport/stornvme images show INTMC unmask before the DPC's
byte19 guardclear. An immediate virtual IRQ can preempt that epilogue, while
NVMeMaskInterrupt sees the old guard1 and skips re-masking. This may be live
queue completions, not stale entries. Waiting after EOI gives the interrupted
guest code an opportunity to finish. Physical time does not guarantee a fixed
count of retired guest instructions; this is a bounded scheduling comparison.

Build passed (existing3unusedfunctionwarnings). test_irq_gap.py compiles actual
EOI/bridge functions with fakeclock/GIC; macro0,50,and200pass affinity,disabledline,
capacity,dedup,exactdeadline,levelcancellation,eventualdelivery,counterwrap.
Those tests do not prove Windows stability.

Manifest pins binary andflags. nvme.o remains verified256block backend.
Mainrepo source notpromoted. Live run and results recorded in Sept10status.

A separate read-only query wrapper in ../target-query-s163 permits counters
to be requested at an existing NS2 rendezvous without an interactive HV pause.
It remains opt-in. Do not use its requests during timed comparisons.
The original 200us run does not load the wrapper; subsequent q/g50 launchers do.

## 01:10 review findings

The direct IRQ entry does not populate exc_info.elr in the enabled vGIC path.
The current S163 last_eoi_pc diagnostic therefore contains stale stack data;
do not use it as evidence. The EOI timestamp/counter do not depend on that PC.
A subsequent candidate should capture hv_get_elr() directly at this site.
The IRQ handler also does not take bhl; the gap state is read/written on the
fixed IRQ900 CPU, not universally protected by bhl as earlier prose implied.

On the INTMC data-abort return, hv_update_fiq/IRQ bridge runs before ctx.elr is
written back. Therefore same-exit injection logs the STR PC, not STR+4.
The existing gap is measured from EOI, so a long DPC can outlast it before
unmask. Fable proposed an unmask/PC-anchored candidate; this remains a design,
not proven. In particular a different observed PC inside another interrupt
handler does not prove the interrupted DPC epilogue has retired. Finish the
200us and 50us controls before promoting a more complex gate.
