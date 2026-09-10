# Current execution mode: physical M1 hardware, EL2-mediated Windows guest

This project aims at independent Windows-on-Apple-Silicon support. The current
result must not be described as hypervisor-free native Windows. Windows ARM64
instructions execute on the Mac mini CPU under m1n1 EL2 mediation. A MacBook
provides boot orchestration and runtime services; this dependency remains.

## Boot and platform interfaces

Apple Boot ROM / iBoot -> installed m1n1 -> uploaded m1n1 EL2 hypervisor ->
guest m1n1 prefix -> Project Mu UEFI -> Windows Boot Manager / OS loader ->
Windows ARM64 kernel and desktop.

The kernel sees a vGICv3 and PSCI interface provided by the hypervisor. This is
not a completed Windows HAL Extension for Apple's AIC. ACPI describes CPU,
interrupt, timer, PCI and device interfaces expected by Windows. The source
includes T810X MADT/GTDT and MacMini2020 FADT/MCFG/DSDT tables. Device exposure
combines synthetic interfaces and selected mapped hardware; a table alone is
not proof of a native driver.

Memory setup includes EL2 stage-2 mappings, a low-address DMA alias window,
reserved/carveout ranges and modified UEFI/BootArgs/ADT memory handling. Recent
Windows inventory reports 8 logical processors and 15,081,889,792 usable bytes
(about 14.05 GiB) on the 16 GB machine. This is usable RAM, not a claim that all
physical bytes can be handed to Windows. CPU frequency/name metadata is wrong;
Task Manager's displayed frequency is not a hardware clock measurement.

Storage is exposed as a synthetic NVMe controller. S230 owns its supported
PCI/configuration/control/admin path in target C, including queue completion
and aggregate interrupts, and reuses the existing S149 target ANS data path.
S225/S226/S227/S228 are now linked into this runtime. NS2 RAM/service traffic
and host boot/DCP setup remain; this is partial host-dependency removal.

Native GPU acceleration and networking are incomplete. A displayed desktop or
an ARM64 Cinebench result does not prove those drivers exist. The measured
1888.454 Cinebench 2026.1.3 result is CPU multi-core on S216, not a GPU score,
not a single-core score, and not a measurement of the newer S230 configuration.
No matching macOS benchmark on this Mini has established performance parity.

## Evidence to inspect

- ../native-s207/README.md: source-level boot and host-dependency census.
- ../firmware-s216/README.md: memory reservation and firmware changes.
- ../bench-s218/result.json: actual Cinebench command/result provenance.
- hardware-result.json and related bounded test results: S230 hardware evidence.
- ../publish-s217/companion/: pinned companion source/patch build provenance.

Historical filenames containing native/nohost describe an experiment's scope;
they must not be interpreted as a system-wide standalone-boot claim. Raw boot
logs are retained locally; public evidence is curated to exclude machine IDs
and unrelated personal data.
