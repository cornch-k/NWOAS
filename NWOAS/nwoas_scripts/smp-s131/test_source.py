#!/usr/bin/env python3
"""Static regression checks for the S131 SMP candidate sources."""

from pathlib import Path


ROOT = Path("/Volumes/X31/NWOAS")
psci = (ROOT / "m1n1_windows/src/hv_psci.c").read_text()
hvc = (ROOT / "m1n1_windows/src/hv.c").read_text()
madt_candidate = (ROOT / "nwoas_scripts/smp-s131/MADT_Static.aslc.candidate").read_text()

assert "cpu_data = &psci_cpu_data_array[node_index];" in psci
assert "cpu_data = psci_cpu_data_array;" not in psci
assert "hv_start_secondary(cpu_identifier, (void *)entry_point," in psci
assert "write64(release_addr, entry_point);" not in psci
assert "CPU_OFF parking CPU%d in hypervisor" in psci
assert "hv_exit_cpu(index);" in psci
assert "BIT(cpu), __ATOMIC_ACQUIRE" in hvc
assert madt_candidate.count("EFI_ACPI_6_3_GIC_ENABLED") == 8
assert "AP disabled for uniprocessor boot" not in madt_candidate
print("S131 source checks: PASS")
