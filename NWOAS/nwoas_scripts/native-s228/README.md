# S228 — composed local PCI/control/admin frontend

Uninstalled C composition of S209 control registers, S227 admin SQ/CQ engine,
S226 command state and S225 payload construction. One object owns those states.
It neither maps guest memory nor executes physical I/O; the separate data owner
must implement validated ANS/NS2 operations and descriptor reconciliation.

Admin pending and interrupt-enabled I/O pending are aggregated before applying
PCI INTx and controller masks. Fatal state suppresses all later IRQ updates.
Delete/recreate CQ clears I/O pending. A supplied reset callback must cancel or
drain old I/O before ring/mapping reuse; failure is latched as CFS after the
control model's reset callback returns. The caller must keep mapping lifetimes
valid even on failed cancellation. Cache Set distinguishes 1=success,
0=expected NVMe command failure, -1=fatal backend failure. A fatal cache result
prevents subsequent CQ publication. Reset/feature and shutdown flush semantics
match the inherited control model on ordinary successful paths.

SQ0/CQ0 doorbells are handled locally; SQ1/CQ1 return NOT_HANDLED to the data
owner when enabled, and are ignored when disabled/fatal. CQ0 ack never drains
backlog. Poll must be explicitly scheduled outside the completion DPC. The
external I/O-pending caller must hold the same lock and reject stale generations.
There are no locks or asynchronous jobs inside the component.

Initialization validates callbacks and policy before mutation. A failed external
reset callback leaves an initialized faulted object and returns false; do not
start a guest using it. All other entry points require successful initialization.
Callbacks cannot reenter and all software objects/mappings are caller-owned.

`bash nwoas_scripts/native-s228/test.sh` passes 11,663 state comparisons / 5,867
submissions against actual S139 full fake RAM, PCI/control registers and IRQ
level. Directed ASan/UBSan tests exercise aggregate pending, masks, CQ deletion,
reset cancellation failure/recovery, shutdown flush, expected/fatal cache
failure, fetch/write/reconcile failures and post-fault IRQ suppression. 50,000
additional malformed register accesses and an import-free combined ARM64 link
also pass. These are offline results, not hardware or standalone-boot evidence.
