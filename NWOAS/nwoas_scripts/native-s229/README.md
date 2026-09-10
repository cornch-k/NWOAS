# S229 — target-local control/admin integration candidate

This candidate links the S228 frontend into the existing m1n1 S208 C I/O path.
It replaces host PCI/control/admin handlers with target C ownership, retaining
physical ANS I/O, the existing Windows LBA write window, S150 NS2 service and
host boot/display/memory orchestration. It is not standalone Windows and does
not add a Windows GPU/network driver. Hardware status is in manifest/results;
build and mock-adapter success alone are not a guest-boot result.

Memory spans are validated page by page using the registered NVMe guest mapping,
MCC carveout rejection, actual stage-2 translation agreement and DRAM bounds.
The complete span is checked before a callback copies bytes. CQ status uses one
volatile 16-bit store after the existing DMA store barrier. No arbitrary guest
integer is dereferenced directly. Mapping changes are prohibited during a call.

The old S149 data queue and physical command function remain. Full 64-bit admin
queue generations are compared before their low 12 bits are encoded; changed
cursors are explicitly reset. CQ1 acknowledgements remain valid after SQ1 is
deleted and do not execute I/O. NS2 link-busy or physical-fault state refuses
reset/reconciliation; the code relies on the existing synchronous physical
operation and big-lock execution model, not an unimplemented cancellation API.
Admin backlog polls at most one command per existing HV tick, outside CQ ack.
The original 50-us NVMe interrupt reassert gap is unchanged.

Actions 16 enables once before guest entry, 17 is capability, 18/19 count all
intercepted register accesses (including I/O doorbells), 20 admin command fetches,
21 published admin completions, 22 CSTS/backend-fault status. The read-only
query adapter excludes action16. NS2 lifecycle queries retain action4 unchanged.
The preboot module verifies capacity/window/MDTS/cache policy and replaces old
same-key tracers with fail-closed callbacks. No hidden host control fallback is
allowed. The old Python object survives only as an NS2 namespace container.

`test_adapter.sh` compiles the actual adapter, extracted S149 control/MMIO and
NVMe guest-page validator with fake RAM/backend under ASan/UBSan. It covers
4KiB/16KiB boundaries, carveouts, stage-2 disagreement, admin payload publication,
CQ-only ack, 4096-generation wrap, cache/masks, shutdown and fatal backend.
`test_module.py` checks preboot policy/capability gating and fallback errors.
These tests do not model real DMA, concurrent guest CPUs or hardware timings.

Build: prepare.py creates a new isolated pinned-base worktree (refuses overwrite).
The companion patch and source manifest are pinned. build.sh uses Rust1.88.0,
Clang/LLD20.1.8 with offline Rust dependencies and low-priority two-job builds.
S223 guest payload is unchanged. Use only the matching hash-pinned launcher.
Fallback remains the previously qualified S224 launcher and image.
