# S150 target-owned mixed-namespace NVMe path

S150 extends the S149 target-side I/O queue fastpath to coexist with the dynamic
host-RAM namespace 2. Target m1n1 remains the only owner of SQ1/CQ1 indexes,
phase bits, completions, and IRQ state. Namespace 1 reads, writes, and flushes
stay on the direct ANS2 target path.

For namespace 2 only, target m1n1 snapshots the 64-byte command and sends a
dedicated synchronous hypervisor event to the MacBook host. Python executes the
existing `LinkNamespace` data operation and validated PRP copies, writes the
status/result into the request, and returns. Target verifies the request magic,
version, sequence, and SQ/CQ generations before advancing SQ head and publishing
the CQE status word last.

This avoids handing a live Windows doorbell back to Python's stale SQ/CQ model.
It preserves the dynamic mailbox used by `NWOS.EXE` and the RAM-backed FAT tools
volume while keeping physical Windows storage off the USB/Python path.
