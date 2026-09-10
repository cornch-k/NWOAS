# Next bounded integration: local admin completion ownership

S224 has hardware evidence for a read projection. S209, S225 and S226 remain
separate uninstalled components. Do not simply replace the Python Controller
with these files: the remaining submission/completion owner must be implemented.

1. **One controller owner.** Compose S209 control state and S226 I/O descriptor
   state under the existing HV big lock. Do not retain the S224 snapshot or the
   Python control model as independent writable authorities. The host may supply
   immutable namespace policy and boot memory mappings once before guest entry.
2. **Admin SQ0/CQ0 lifetime.** S209 setup/reset callbacks must initialize and
   invalidate a bounded local admin ring owner. Validate all mapped spans, use
   a lifecycle token across reset, clear pending CQ/AER state, and reject stale
   work. Preserve the one-unused-CQ-slot convention. AER consumes an SQ command
   while producing no CQ entry until an actual completion is supplied.
3. **Fetch and publish.** Copy each 64-byte command from validated guest RAM,
   run S226, validate at most two PRP spans for S225's <=4096-byte responses,
   and publish completion data before the phase-bearing status word with the
   existing DMA barrier convention. No arbitrary address may bypass the current
   guest-memory/carveout validator. A bad PRP produces Invalid Field; a backend
   or publication fault must stop the controller rather than fabricate success.
4. **Queue descriptor reconciliation.** Process S226 changed bits within the
   same ownership transaction as fastpath arm/disarm. Keep its full descriptor
   generations and the S149 lifecycle checks aligned. Do not resurrect a queue
   after reset or permit a late completion to target a replacement ring. Keep
   S139's rule: CQ-head acknowledgement itself never starts physical I/O.
5. **IRQ and mask.** Aggregate local admin pending state and target I/O pending
   state once, then apply INTMS/INTMC, PCI INTx mask and CQ IEN policy. Preserve
   the measured S208 50-us reassert gap during this experiment. Do not use the
   old Python CQ view or the S224 PCI pending projection as a second authority.
6. **Namespace and cache.** Keep ANS write-window validation and flush failure
   behavior unchanged. S226 set-cache callbacks must leave cache state unchanged
   on failure. S225 NS2 metadata does not implement NS2 storage or its transport;
   preserve the existing service explicitly or prepare a distinct no-NS2 boot.
7. **Acceptance.** Differentially replay actual command and completion bytes,
   including PRP errors, ring wrap, CQ pressure, reset during pending work and
   cache/flush failure. Then use a hash-pinned isolated launcher, eight-core/full
   memory and checksum-correct storage checks, a bounded soak, and a clean reboot
   proving shutdown flush. Count host admin/register callbacks rather than
   inferring independence from quiet logs. No cable-drop or standalone claim
   until all remaining callbacks and boot setup are separately eliminated.

This sequence is a proposed integration plan, not implemented behavior.

## Review-derived owner invariants

Compare full 64-bit descriptor generations and disarm/reinitialize on every
change before encoding the low 12 bits in S160 flags; truncated equality must
never hide a lifecycle change. Create resets head/tail to zero, phase to one
and pending to zero. Delete/reset discards backlog and cancels or drains old
in-flight work; old-generation completion publication is prohibited.

Recompute aggregate IRQ state on CQ deletion and reset. Resynchronize fast-path
flags on CQ IEN, descriptor-generation or cache changed-bit transitions. A
held AER advances the SQ head without publishing a CQ entry. Only dispatch
commands while control state is ready and nonfatal. The boolean cache callback
maps expected command failure to status 6; an owner needs a separate fatal
backend path that stops the controller and sets CFS instead of fabricating
success. None of these integration mechanisms is implemented by this model.
