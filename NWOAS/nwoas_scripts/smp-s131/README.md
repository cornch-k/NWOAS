# S131 eight-core candidate

S131 keeps the hardware-verified S129 internal-SSD-first UEFI path and the 4 GiB USB DMA safety window. It changes two CPU bring-up inputs:

- all eight M1 CPU GICC entries are enabled in MADT;
- `hv_psci_initialize_power_domain_node()` initializes the indexed per-CPU PSCI record instead of repeatedly overwriting CPU0.
- PSCI `CPU_ON` uses `hv_start_secondary()` so each AP receives its EL2/vGIC setup before entering the Windows kernel with `context_id` in X0;
- the guest-CPU rendezvous mask records the AP being started rather than CPU0, and the asynchronous AP register frame uses persistent per-CPU storage.

The original S129 payload and S103 hypervisor remain available as the one-core rollback pair. Hardware success requires Windows Task Manager to report 8 cores and 8 logical processors, followed by an idle/restart/storage/input stability check. A build alone is not proof of SMP stability.
