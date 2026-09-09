# NWOAS — Native Windows on Apple Silicon

Personal research project to boot Windows 11 ARM natively on a
user-owned Apple Silicon Mac (Mac mini M1), in the same spirit as Asahi
Linux or the broader Windows-on-ARM community: legitimate bootloader,
firmware, and driver development built on public hardware documentation
and open-source components (m1n1, Project Mu). All work targets only
the author's own hardware.

## Boot chain

```
Apple Boot ROM -> iBoot -> m1n1 (EL2 mediation)
  -> guest m1n1 -> Project Mu UEFI -> internal-SSD Windows Boot Manager
  -> Windows 11 ARM64 desktop
```

A second Mac still supplies boot orchestration and runtime service transport.

## Current status — 2026-09-10

Windows 11 ARM64 boots from the Mac mini M1 internal SSD with Tahoe firmware
retained. All eight CPU cores execute Windows workloads, and Windows sees
15.08 GB (about 14.05 GiB) of RAM. Native UEFI code reads the hardware PMU clock
at boot and selects performance-core P-state 12. S193/S200 boot and checksum
tests passed with both initial and late host CPU-state writes removed.

Official Cinebench 2026.1.3 ARM64 CPU multi-core completed at **1905.281 points**
(S196, one run, 629.8-second render, native exit 0). This is native ARM64 CPU
execution under the current EL2-mediated system. A matching macOS baseline has
not been measured. CPU speed/name metadata in Windows remains incorrect.

The storage path still presents an emulated NVMe interface, with a target-side
fast data path and host-side control/service transport. The S163 50µs interrupt
reassert delay passed the recorded 30-minute soak; it mitigates a watchdog
failure without establishing its complete cause. A dedicated 256MiB write-through
file passed verification of every 64-bit word in S193 and S200, with identical
SHA-256 hashes. These bounded tests are not a production stability claim.

Standalone boot, native networking and GPU drivers, reliable USB-C and display
hotplug, power management, and the intended SSD partition layout remain work in
progress. The active Windows volume is about 24.8 GB, not a completed 128/128 GB
split. The live keyboard and trackpad were enumerated under USB-A; an empty
USB-C root hub does not demonstrate USB-C device-transfer success.

Start with the [session record](NWOAS/NWOAS-STATUS-2026-09-10-SESSION.md),
[Cinebench evidence](NWOAS/nwoas_scripts/bench-s196/result.json),
[host CPU-init removal](NWOAS/nwoas_scripts/cpufreq-s193/hardware-result.json), and
[companion source patches](NWOAS/companion-patches/2026-09-10/README.md).
Historical stage files preserve unsuccessful experiments and are not current
installation instructions. Build success alone is not hardware success.

## Layout

- `NWOAS/` — this project: autounattend/Windows PE answer files, prebuilt
  m1n1 test binaries, hardware reference driver headers, and helper
  scripts used across boot experiments.
- `macvdmtool/` — a small macOS CLI tool for driving another Apple Silicon
  device's DFU/serial console over USB-C (see below).

Two larger companion components this project builds on are maintained as
their own repositories. This snapshot includes pinned-base patches and additional
source files for local modifications (including modified submodules):

- [AppleWOA/apple_silicon_platforms_mu](https://github.com/AppleWOA/apple_silicon_platforms_mu) — Project Mu-based UEFI firmware for Apple Silicon
- [AppleWOA/m1n1_windows](https://github.com/AppleWOA/m1n1_windows) — a fork of [m1n1](https://github.com/AsahiLinux/m1n1) adapted for Windows boot

Raw diagnostic run logs and Windows installation media/binaries used
locally during development are not included here (large and/or not
suitable for redistribution).

## macvdmtool

A serial-console/DFU helper tool that lets one Apple Silicon Mac control
another over a single USB-C cable — see
[`macvdmtool/main.cpp`](macvdmtool/main.cpp) and its `Makefile`. Build
with the Xcode command-line tools and `make`; run with
`sudo ./macvdmtool <command>` (see source for the full command list).

This is a fork based on portions of
[ThunderboltPatcher](https://github.com/osy/ThunderboltPatcher) and
[AsahiLinux/macvdmtool](https://github.com/AsahiLinux/macvdmtool),
licensed under Apache-2.0:

- Copyright (C) 2019 osy86. All rights reserved.
- Copyright (C) 2021 The Asahi Linux Contributors

## Note

Historical root `NWOAS/autounattend.xml` contains automatic disk-wipe directives;
do not reuse it for the current dual-boot work. The current guarded Setup package
is under `NWOAS/nwoas_scripts/setup-repair/`. No Windows binaries or modified
installation images are included in this update.
