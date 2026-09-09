# S132 PSCI CPU lifecycle candidate

S131 proved that UEFI started all seven APs and each reached the common guest secondary entry. It then stalled when Windows issued `CPU_ON` for CPU1 because UEFI's earlier `CPU_OFF` physically slept the AP without clearing the hypervisor's active-CPU state.

S132 keeps the S131 eight-core UEFI payload and routes guest `CPU_OFF` through `hv_exit_cpu()`. The normal exception-return path removes the AP from the guest-active mask, marks it inactive, and returns it to m1n1's parking loop. A subsequent Windows `CPU_ON` can then run the existing `hv_start_secondary()` path without double-starting the core.

The S131 hardware log is retained as a failed lifecycle probe. S129/S103 remains the one-core rollback pair.
