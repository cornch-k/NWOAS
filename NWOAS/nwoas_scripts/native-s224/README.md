# S224 — target-local PCI/NVMe read projection

This incremental step removes host callbacks for supported PCI configuration
and NVMe register **reads**, while Python remains the single control/admin
writer. It is not the S209 full controller, a native Windows driver, or
standalone boot. The NS2 service and setup-time host paths remain.

`read_mirror.h` holds a bounded read projection, published after each existing
Python PCI or BAR write handler completes (including queue/admin processing,
fastpath generation changes and IRQ synchronization). Existing target-local
mask writes are read from target state whenever the fastpath is armed. The
existing 32-bit fatal CSTS override is retained. Writes and unsupported wide
reads use the previous owner. Publication is serialized with guest execution
by the existing proxy rendezvous / HV lock. The overlay disables the projection
if a write or publication raises rather than resuming with known stale data.

Target actions: 11 publishes or disables, 12 returns capability, 13 counts
locally served PCI reads, 14 counts locally served register reads, and 15 counts
snapshot publications. The read-only query adapter explicitly excludes action
11. Counters measure this projection only, not all host transport traffic.

The incremental tradeoff is one snapshot publication per host-owned write.
This reduces read callbacks but does not establish lower wall-clock latency
or performance improvement until measured. The original I/O, write-window,
queue ownership, EOI gap, payload, display and USB configuration are retained.

## Offline evidence

- 263,597 reads across 200 states compare byte-for-byte with the actual imported
  S139/S124 controller, including mixed widths, offsets, absent PCI functions,
  mask overlay and the fatal CSTS override.
- ASan/UBSan boundary matrix and freestanding ARM64 object pass.
- Actual Python overlay callback wiring passes owner-transition ordering,
  pending/command projection, owner exception, publication failure and invalid
  state tests. These are fake-target tests, not a hardware result.

See `manifest.json` and subsequent hardware result files for live qualification.

The actual S160 callback / S139 owner sequence also matches 204 reads across
queue create/delete, mask, shutdown and reset transitions. Real DictRangeMap
semantics confirm that the overlay replaces each existing same-key tracer
rather than stacking callbacks. See claude-review.md and claude-resolution.md.

## Interpretation

See EXECUTION-MODE.md for the current boot chain and the distinction between
ARM64 execution on the physical Mini and hypervisor-free standalone Windows.
S225/S226 are future admin-owner foundations only. They are not in this image.
