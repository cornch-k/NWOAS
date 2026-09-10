# S224 read-projection review

Read-only review. Scope: the S224 read projection that serves PCI/NVMe register
*reads* inside the existing target C, while Python remains the sole control/admin
*writer*. This review does not access hardware and does not claim any hardware
result. The offline differential run (263597 compared reads) and the sanitizer /
freestanding builds are taken as reported; nothing here implies on-device
stability.

Files reviewed: `native-s224/read_mirror.h`, `module.py`, `prepare.py`,
`test_differential.py`, and the generated `m1n1-read-s224/src/hv_vm.c` (S224
integration plus the surrounding NVMe fastpath). Compared against
`nvme-s160/guest_module.py`, `nvme-s139/controller.py`, and the S124 base
controller they extend.

## Verdict

The design is sound and minimal. The C integration is three localized
insertions (include plus mirror struct, the action 11-15 publish/counter block,
and a single read shortcut at the top of the fastpath MMIO handler). The read
path serves the same bytes the Python controller would produce, and the
differential oracle is the real imported S139/S124 controller, not a
reimplementation. I found no byte-mapping defect.

Two items must be resolved before a bounded hardware test, and three should be
confirmed. Details below.

## What the projection replaces, and where freshness comes from

Before S224, every ECAM and BAR read trapped and proxied to Python; the C
fastpath served only faulted-CSTS and the S160 local-mask register reads. S224
now serves the whole ECAM config space and the whole BAR read side from a
snapshot published by Python after each write.

Freshness holds because every field the snapshot exposes changes only on a write
that Python still owns:

- CC, CSTS, AQA, ASQ, ACQ, command, and the probe/BAR-sizing bits change only on
  config writes. Config writes are not doorbells, so they are not handled in C;
  they fall through to the proxy, run `Controller.write`, then `_s224_publish`.
  Each such write republishes.
- The interrupt mask is served live from `nwoas_nvme_fp.mask` when armed, so it
  does not depend on snapshot age.
- CSTS.CFS after a fastpath fault is served live through the existing 32-bit
  `off == 0x1c` override, which fires whether or not the mirror is enabled.

The publish/read ordering is claimed to be serialized by the HV big lock and the
proxy rendezvous. Within a single serialized sequence the enabled-flag protocol
(clear first, fill fields, set enabled last) is correct.

## Blocker 1: memory ordering of publish versus read under SMP

`read_mirror.h` uses plain loads and stores with no atomics and no barriers.
`nwoas_mirror_publish` writes `enabled = false`, fills the fields, then writes
`enabled = true`. `nwoas_mirror_read` reads `enabled` then the fields. This is
safe only if no guest read of the projection can run concurrently with a publish.

The read path is served entirely in C and never proxies, so it does not
rendezvous with Python. Publication runs when a guest write is proxied to Python
and Python calls back through action 11. A write proxied on one vCPU blocks that
vCPU, but other vCPUs keep running and can take register-read traps. If those
read traps and the action-11 publish are not inside one lock domain, a reader on
another core can observe `enabled == true` with half-written or reordered fields,
because aarch64 is weakly ordered and nothing here forces release/acquire.

The existing fastpath already mutates `nwoas_nvme_fp` from the same handler
without visible locking, so this may match an assumption the project already
relies on. That assumption is exactly what must be confirmed for the read side.
Concretely, before hardware: verify that guest MMIO exits and the action-11
publish are serialized against each other across all vCPUs. If they are not, add
release ordering on the `enabled` store in publish and acquire ordering on the
`enabled` load in read, or gate the read behind the same lock. This is the
sharpest correctness risk and the differential test cannot exercise it, since it
is single-threaded.

## Blocker 2: tracer re-registration must replace, not stack

`module.py` re-adds both tracers with the same names used by S160
(`s93-nvme-ecam`, `s93-nvme-bar`) and captures the prior write callbacks into
`_s224_pci_write` / `_s224_mmio_write`. The intent is to wrap the S160 write
path with a publish and leave reads alone.

If `hv.add_tracer` with a duplicate name and range replaces the prior hook, this
is correct: only `_s224_pci` / `_s224_mmio` fire, each calls the original write
once, then publishes. If it instead stacks a second hook, the write is processed
twice, which corrupts controller state. Confirm the replace semantics before
hardware. This is a one-line assumption with a large failure mode.

## Mask and fatal-CSTS precedence: correct, with one behavioral note

Precedence in `nwoas_mirror_read` for the BAR region is:

1. armed and faulted and `off == 0x1c` and 32-bit → return 3 (RDY|CFS). Matches
   the existing S149 override.
2. otherwise mask overlay: `mask = armed ? fp_mask : published_mask`, then the
   per-byte register map.

This matches the controller. `Controller.read` returns `self.mask` for both
INTMS (0x0c) and INTMC (0x10) with no fault check, and the mirror returns the
mask for both without a fault check, so mirror and the Python fallback agree even
when faulted. The mask source is consistent: when armed, `fp_mask` is
authoritative (S160 pulls it into `c.mask`, or pushes `c.mask` into it on each
sync); when not armed, the published `c.mask` is used. The differential test
exercises the armed/unarmed and faulted combinations and confirms equivalence.

Behavioral note, not a blocker: because the mirror now serves 0x0c/0x10 reads
before the fastpath's local-mask read path, `nwoas_nvme_diag.local_mask_reads` no
longer increments on reads. The S158/S160 diagnostic will undercount local mask
reads. Interpret that counter accordingly; the mask value served is unchanged.

## MMIO routing: correct

The mirror is called from `nwoas_nvme_fastpath_mmio` on the read side only
(`!write`), which is reached from both the read and write SPTE hook call sites.
The write call site passes `write = true`, so the mirror is never consulted for
writes; writes proceed to the existing doorbell/mask handling or the proxy. The
mirror keys off the address itself: ECAM in [0x700000000, 0x700100000), BAR in
[0x700100000, 0x700104000). Addresses outside both ranges return false and fall
through to the existing armed check and then the proxy. Reads the mirror declines
reach the original S160 read callbacks. Routing is coherent.

## Width and bounds: correct, with an intentional ECAM/BAR asymmetry

- `width_log2 > 3` is rejected; `1 << 63` shift on the 8-byte all-ones path is
  special-cased to avoid undefined behavior.
- ECAM: an offset at or past 4096, or a read crossing 4096, returns width-masked
  all-ones, matching `Controller.pci_read` and the guest-module all-ones for
  absent functions. Offsets past the single function's 4096 bytes also return
  all-ones.
- BAR: a read crossing 0x4000 returns false and falls through; the proxy then
  returns 0 from `Controller.read`, so the guest still sees 0. The differential
  test asserts the mirror *declines* here rather than serving, which is the
  intended asymmetry with ECAM.

The mirror serves 64-bit register reads (CAP, ASQ, ACQ) that the old fastpath
rejected with its `width != 2` guard; these previously went to the proxy and are
now served directly. The test covers all four widths.

## Publication exception behavior: safe

`_s224_write` runs the base write, then publishes; on any exception it calls
`p.nwoas_nvme_fastpath(11)` with no field arguments and re-raises. With the
missing arguments zero-filled, the publish sets `flags = 0`, so bit 63 is clear
and the projection is disabled. Reads then fall to the proxy and Python, which
reflects the true (possibly partially mutated) controller state. A later
successful write republishes with bit 63 set and re-enables. This upholds the
stated invariant of never resuming the guest on a known-stale projection, and it
self-heals.

One dependency to confirm (item below): that `p.nwoas_nvme_fastpath(11)` with a
single argument actually zero-fills the remaining register arguments. This is the
usual m1n1 proxy behavior, but the disable path's safety rests on it.

## Items to confirm before hardware (lower risk)

1. **Proxy zero-fill.** Confirm `p.nwoas_nvme_fastpath(11)` passes zeros for the
   unspecified arguments so the error path truly disables the projection.
2. **PCI Status interrupt-pending bit is a frozen snapshot.** `pci_byte` at
   offset 6-7 returns the pending bit captured in `flags` bit 16 at publish time.
   I/O completions happen in the C fastpath without a Python write, so this bit
   does not track live completion state between config writes. This is not new:
   S160 already computed pending from Python's cq view, which is not
   authoritative while armed. S224 makes it staler by freezing it. Confirm the
   guest never uses PCI Status.InterruptStatus for completion detection; NVMe
   completion is normally driven by CQ phase bits in memory, so this is expected
   to be benign, but it should be stated explicitly.
3. **Controller identity guard.** `module.py` requires MAX_Q == 1,
   MAX_DEPTH == 256, and bar == 0x700100000, which pins it to the S139
   controller. The mirror's CAP encodes MQES as a hardcoded 255, so a different
   MAX_DEPTH would silently diverge. The assertion covers this today; keep it.

## Coordination

Main is preparing integration tests independently. This review is source-only and
made no changes outside this file. The differential result and manifest describe
the artifact as a source-only candidate with no hardware qualification, which is
consistent with the state I observed.
