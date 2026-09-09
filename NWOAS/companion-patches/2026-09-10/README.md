# Tested companion source, 2026-09-10

These patches reconstruct the S192 UEFI candidate source and S163 hypervisor source against the base commits in `manifest.json`. The bases are published in [cornch-k/m1n1_windows](https://github.com/cornch-k/m1n1_windows/tree/nwoas-stage8-instrumentation) and [cornch-k/apple_silicon_platforms_mu](https://github.com/cornch-k/apple_silicon_platforms_mu/tree/nwoas-native-windows). Initialise submodules at the listed commits, apply each patch inside its corresponding repository, and apply the two submodule patches inside MU_BASECORE and Silicon/ARM/TIANO. All four patches passed clean-index apply checks; an independent 49-check audit verified the nested build overrides byte-for-byte.

The tested HV requires both explicit flags:

```
EXTRA_CFLAGS="-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50"
```

The source default for reassert spacing is zero. Historical S131/S133 main-checkout build scripts do not select the tested S163 configuration. S205 performed two clean builds from the pinned source and flags; both reproduced the tested hypervisor binary byte-for-byte. Recipe and hashes are under `nwoas_scripts/rebuild-s205`.

S192's outer manifest records an intermediate NwoasHideHighRamDxe source hash; the subsequent S131 builder enables NVMe. This export includes the final inner override, not the intermediate NVME=0 file. The RTC library mapping is a RealTimeClockRuntimeDxe component-scoped override; the common default mapping remains in the DSC intentionally.

The original tested payload contains an unrecorded dirty guest m1n1 prefix (`59fb544-dirty`). Its exact bytes cannot be reconstructed from the published original source. S197 provides a replacement rebuilt from tracked source, with separate hardware qualification recorded in its later stage reports. Payload/EXE launcher hashes pin the author's measured artifacts; a different toolchain may produce different hashes and should be qualified independently. Bootable binary reproduction is not established by the source export alone.

The UEFI still boots under EL2 mediation and the MacBook transport. Native RTC and P-state initialization are firmware improvements, not a standalone Windows storage, network or graphics driver stack.
