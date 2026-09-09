"""Keep APs parked until Windows requests them through PSCI.

Windows' Apple AIC HAL writes the Apple PMGR CPU_START registers during early
bring-up.  The outer Python hypervisor normally translates those writes into
hv_start_secondary(), entering the AP at the firmware RVBAR.  Windows later
issues PSCI CPU_ON for the same AP, which caused the S131/S132 double-start
deadlock at ``HV: Initializing secondary 1``.

MADT still advertises all eight CPUs.  This gate blocks only the legacy/direct
PMGR dispatch path.  PSCI CPU_ON is handled inside the C hypervisor and calls
hv_start_secondary() directly, so it remains able to start each parked AP at
Windows' requested entry point.
"""

_nwoas_pmstart_seen = 0


def _nwoas_gate_pmgr_start(die, cluster, cpu):
    global _nwoas_pmstart_seen
    _nwoas_pmstart_seen += 1
    # Keep enough evidence to prove Windows attempted the PMGR path, without
    # flooding the shared serial link if it retries.
    if _nwoas_pmstart_seen <= 16:
        hv.log(
            "HVLOG: NWOAS-S133 gate PMGR AP start "
            f"{die}:{cluster}:{cpu}; waiting for PSCI CPU_ON"
        )


hv.start_secondary = _nwoas_gate_pmgr_start
print("[s133] PMGR AP-start gate armed; PSCI CPU_ON remains enabled")
