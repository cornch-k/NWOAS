# S209 — target-local NVMe control state foundation

Main implemented this source-only component on 2026-09-10. It is **not installed
in the HV or a Windows driver**. It removes **no runtime host dependency yet**.

`model.c` implements the current S139/S124 PCI configuration identity, fixed
64-bit BAR sizing, PCI command/INTx mask, and NVMe CAP/VS/INTMS/INTMC/CC/CSTS/
AQA/ASQ/ACQ state. It has no allocation, libc calls, direct hardware access,
DMA, namespace reads/writes, or filesystem access. All calls for one instance, including pending updates, must be serialized by
the owner (the current target path uses the HV big lock). Callbacks must not
reenter the model. The component adds no locks or host transport.

The owner supplies bounded
queue validation/setup/reset, shutdown flush, and IRQ callbacks. SQ/CQ
doorbells return `NWOAS_NOT_HANDLED`, never a fabricated completion.

## Validation

Run `bash nwoas_scripts/native-s209/test.sh` from any working directory.

- The actual C implementation is compiled as a host shared library and driven
  against the real imported S139 Controller, not a rewritten Python oracle.
  Directed transitions plus 10,000 seeded stateful operations compare PCI and
  register bytes, interrupt level and callback count, shutdown flush count, and
  admin SQ/CQ addresses, depths and active state.
- Replaying the parsed S200 control accesses matches all267 recorded reads
  and48 control writes;16 unimplemented doorbells explicitly return
  NOT_HANDLED. This is a partial control replay, not an admin/I/O replay.
- Actual C code also passes directed plain and ASan+UBSan harnesses including
  callback failure, enable/disable/shutdown, INTx masking, unsupported accesses,
  and width/offset overflow matrices.
- The freestanding `aarch64-none-elf` object has no undefined imports.
- No hardware stability/performance result is claimed for this component.

## Compatibility and defined malformed-input handling

Widths are 8/16/32/64 bits for reads and PCI accesses; register writes are
32/64 bits and dword aligned. Out-of-range or unsupported-width operations
return `NWOAS_INVALID`, PCI reads return width-sized all ones and register
reads return zero. All byte assembly is explicitly little endian. Values are
truncated to the stated access width.

The Python model can store oversized integers on 64-bit writes to dword
registers or 64-bit writes at the high half of ASQ/ACQ, then throw while packing
future reads. C rejects high-half 64-bit writes and nonzero upper dwords for
non-ASQ/ACQ registers. It also rejects queue spans which wrap uint64 before
calling the owner's range validator. These are intentional defined boundaries,
not claims of equivalence for inputs where Python throws.

The existing Python PCI command overlap predicate ignores a byte write at
address 5 (while a write starting at 4 can update both command bytes). This
quirk is preserved and tested for differential compatibility; correcting it
would require an explicitly separate behavior change.

The `pending` input is the aggregate of interrupt-enabled CQs with outstanding
completions. PCI status reflects it even when INTx is disabled. Failure sets
CSTS.CFS and deasserts the line, like the current Python model. An absent or
failed queue-setup/flush callback fails closed. Queue reset means clear SQ/CQ
and AER state, while the owner controls feature lifetime on full reset versus
CC disable; this component does not implement admin features.

## Required integration before enabling it

1. Establish a single owner for PCI command, interrupt mask, CC/admin queue
   descriptors and outstanding-CQ state. The current Python controller and
   target fastpath already synchronize subsets; independent duplicate state
   would lose interrupt transitions or acknowledge stale completions.
2. Connect validated target-local admin queue creation and reset, with lifetime
   and generation handling for the existing I/O fastpath. The setup callback
   must be transactional: on failure it must leave no partially active queues.
3. Implement admin commands, completion publication and NS2 behavior locally
   or remove NS2 from a specifically prepared standalone configuration. This
   model deliberately does not execute them. Preserve S139's rule that a CQ
   acknowledgement does not perform new SSD I/O in the completion DPC.
4. Connect shutdown flush to the existing write-window-checked physical storage
   implementation, and prove error propagation with real reset/reboot tests.
5. Run replay, fault injection and staged hardware tests before any launcher
   changes. C build/test success alone does not establish Windows compatibility.

The broader boot path still needs host DCP setup, memory/ADT preparation and
other orchestration; converting this model alone cannot make boot standalone.
