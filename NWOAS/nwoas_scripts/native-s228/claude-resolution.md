# Review disposition

The independent review found no defect in tested composed semantics and raised
interface/reentry items. Changes after its snapshot:

- F1: Unsupported 64-bit or qid>=2 doorbell writes are now handled/ignored,
  matching Python. NOT_HANDLED is restricted to ready, nonfatal 32-bit SQ1/CQ1.
  Enabled/disabled return codes have explicit assertions.
- F2: Added a control_busy guard. Pending is recomputed after the outer control
  operation, so setup/reset callbacks no longer reenter the control model.
  The S229 adapter separately suppresses its I/O pending callback while it calls
  the existing fastpath control functions from an owner callback.
- F3: Added full-reset failure, init/reset failure and a mid-batch publication
  failure. Actual held-AER executions are traced and recorded (170), rather
  than inferring them from submissions.
- F4: Documented controller/feature reset preserving PCI command/probe state.

Local differential, sanitizer and freestanding build checks passed after these
changes. No independent re-review of this changed snapshot or hardware result
is claimed here. S229 is a separately tested integration candidate.
