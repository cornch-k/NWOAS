# S122: durable completion experiment (HARDWARE-UNVERIFIED)

Independent copy of nvme-s93 Python modules; active S121 and baseline files unchanged.
Code review found accepted FUA hints ignored and CC.SHN immediately reported complete
without backend flush. VWC is advertised absent. This is a durability contract problem;
its causal role in observed post-reboot file changes is still hardware-unverified.

For normal trial-data writes S122 calls the physical NVMe flush backend before success,
for both direct DMA and host-copy paths. This conservatively honors VWC=0 and FUA without
changing Identify/features. It may cost performance. Flush failure returns WRITE_ERROR.
Shutdown changes SHST to processing, flushes backend, then reports complete; failure sets
CFS without claiming shutdown complete. Repeated completed shutdown does not reflush.
Original GPT transaction guard remains, including staged table updates before validation;
this experiment is for the already-partitioned Windows data volume, not GPT durability proof.
No LBA bounds, protected partitions, USB code, guest memory map or m1n1 binary changes.

Validation: 36 tests PASS (29 existing + 7 durability tests). New tests simulate volatile
cache loss, FUA and direct/copy ordering, rejected writes, flush errors, shutdown timing.
Physical backend may itself have defects; a passing mock test is not persistence proof.
Next: recover S121 report before deciding the next USB script, then repair/reverify sources
with this backend and compare persistence after one reboot. Do not re-run stale armed scripts.
Reference: https://nvmexpress.org/wp-content/uploads/2013/04/NVM_10e_specification.pdf
