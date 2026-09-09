# S219 — boundary between the control model and a physical ANS driver

Read-only primary-source cross-check, not a driver implementation.
The reviewed Linux/Asahi driver is pinned by commit and content hash in
`source-reference.json`; its code was not copied into S209.

The T8103 implementation uses one admin queue and one I/O queue with a shared
command-tag budget. Submission entries are indexed by tag, and each submitted
command also needs an NVMMU TCB; conventional NVMe register emulation alone
cannot drive it. The driver also contains RTKit/SART setup and command/queue
lifetime synchronization. See the [Linux Apple ANS driver](https://github.com/torvalds/linux/blob/8ce883fd068b7ba9ab493cd3ecca3a7ea868c375/drivers/nvme/host/apple.c),
especially its T8103 path, rather than borrowing the distinct T8015 path.

Local implication: S209's256-entry advertised queue model describes the
existing synthetic Windows controller. It is not a physical ANS queue-depth
setting. The current target fastpath converts those requests into the existing
m1n1 physical storage backend, with validated write windows and a bounded
completion lifecycle. A native control/admin replacement must preserve that
separation and the physical tag ownership. Increasing virtual depth or adding
PCI reads cannot establish native storage support.

Next implementation acceptance should require target-local admin completion
production, reset/generation synchronization with the existing I/O fastpath,
NS2 removal or local ownership, and observed absence of host callbacks during
a specifically prepared run. The current overnight results do not prove that.
