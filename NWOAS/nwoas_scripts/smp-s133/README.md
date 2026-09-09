# S133: single-owner AP startup

S131 proved all seven secondary M1 cores can enter EL2 and initialize their
virtual GIC state, but Windows first started them through the Apple PMGR hook
and later requested CPU1 again through PSCI. S132 then deadlocked on that
second start because CPU1 was already executing the guest firmware entry.

S133 keeps the MADT eight-core declaration and the repaired C PSCI path. The
`pmgr_gate.py` run-guest module suppresses only Python's PMGR-to-guest dispatch,
leaving each physical AP in m1n1's spin table until Windows supplies its real
entry point with PSCI CPU_ON. The C hypervisor also clears a stale exit request
when an AP is restarted. PMCC diagnostics are capped at 32 lines to reduce UART
contention before a higher baud rate is tested separately.

Success evidence is:

1. `NWOAS-S133 gate PMGR AP start` for early direct writes.
2. `PSCI DEBUG: turning on CPU1` followed by both `HV: Initializing secondary 1`
   and `HV: Entering guest secondary 1` without a stall.
3. Equivalent PSCI/enter markers for CPUs 2 through 7.
4. Windows Task Manager reports eight logical processors and remains responsive.

The first three checks can be repeated without touching a live target:

```sh
python3 nwoas_scripts/smp-s133/summarize_log.py \
  nwoas_scripts/logs/usb-s133-YYYYMMDD-HHMMSS.XXXXXX
```

Exit status 0 requires exactly seven gated PMGR requests, exactly one guest
entry for every secondary CPU, NVMe trap activity on CPU0 through CPU7, and no
fatal marker. The Task Manager check is still a separate visual confirmation.
