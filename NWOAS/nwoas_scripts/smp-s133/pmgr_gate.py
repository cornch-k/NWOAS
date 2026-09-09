"""Keep APs parked until Windows requests them through PSCI.

Observed legacy PMGR CPU_START writes occur in the guest m1n1 prefix, before
UEFI storage initialization, and match smp_start_cpu(). Earlier documentation
attributed them to Windows; that attribution was not supported by the trace.
The outer Python hypervisor otherwise dispatches these writes through
hv_start_secondary() at the firmware RVBAR. This gate suppresses that legacy
start path, preserving parked APs for later Windows PSCI CPU_ON requests.

MADT still advertises all eight CPUs. PSCI CPU_ON is handled inside the C
hypervisor and calls hv_start_secondary() directly. This Python gate remains
a host-side boot dependency; it is not a native Windows CPU-start driver.
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
