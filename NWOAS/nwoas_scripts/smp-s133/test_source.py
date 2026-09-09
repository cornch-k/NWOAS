#!/usr/bin/env python3
from pathlib import Path

root = Path("/Volumes/X31/NWOAS")
hv_c = (root / "m1n1_windows/src/hv.c").read_text()
hv_exc = (root / "m1n1_windows/src/hv_exc.c").read_text()
psci = (root / "m1n1_windows/src/hv_psci.c").read_text()
gate = (root / "nwoas_scripts/smp-s133/pmgr_gate.py").read_text()
madt_builder = (root / "nwoas_scripts/smp-s131/build_candidate.py").read_text()

checks = {
    "PSCI uses persistent per-CPU register frames": "psci_cpu_on_regs[MAX_CPUS][4]" in psci,
    "PSCI dispatches through hypervisor AP initialization": "hv_start_secondary(cpu_identifier" in psci,
    "AP start clears stale exit request": "hv_should_exit[cpu] = false;" in hv_c,
    "guest-active mask uses target CPU": "BIT(cpu), __ATOMIC_ACQUIRE" in hv_c,
    "PMGR gate replaces only Python start hook": "hv.start_secondary = _nwoas_gate_pmgr_start" in gate,
    "PMGR gate does not invoke original starter": "_nwoas_orig_start_secondary" not in gate,
    "PMCC cap is 32": "if (nwoas_pmcc_n < 32)" in hv_exc,
    "S131 builder enables all MADT CPUs": "EFI_ACPI_6_3_GIC_ENABLED" in madt_builder,
}

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(("PASS" if ok else "FAIL") + ": " + name)
if failed:
    raise SystemExit(1)
