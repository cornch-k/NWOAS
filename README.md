# NWOAS — Native Windows on Apple Silicon

Personal research project to boot Windows 11 ARM natively on a
user-owned Apple Silicon Mac (Mac mini M1), in the same spirit as Asahi
Linux or the broader Windows-on-ARM community: legitimate bootloader,
firmware, and driver development built on public hardware documentation
and open-source components (m1n1, Project Mu). All work targets only
the author's own hardware.

## Boot chain

```
Apple Boot ROM -> iBoot -> m1n1 (EL2 hypervisor, vGIC)
  -> UEFI (Project Mu, EL1) -> Windows install media (+ AIC HAL extension driver)
  -> Windows Boot Manager (USB-C external boot) -> Windows loader + RAMDisk
  -> loader-to-kernel handoff
```

## Current status — 2026-09-09

Windows 11 ARM64 now reaches the desktop from the Mac mini M1 internal SSD while
retaining Tahoe firmware. S124 made the guarded host-mediated NVMe path durable
enough to apply and verify the Windows image. S126 added a RAM-backed command
transport, S128 exposed the internal SSD to UEFI, and S129 selected it before USB.
The observed sequence was installed-OS user-space handshake, a Windows-initiated
restart, OOBE, offline local setup, and the Windows 11 desktop.

This is an important boot milestone, but it is not yet a production native-driver
stack. The current desktop reports one CPU core, one logical processor, 4.1 GB of
RAM, and an unreliable 0.04 GHz speed value. Storage is still relayed through the
host process, USB-C hot-unplug/replug does not recover reliably, and networking,
GPU acceleration, SMP, full memory, standalone storage, and benchmark performance
remain unfinished. The next work is bottleneck measurement and CPU/memory exposure,
followed by native device drivers.

Start with the [latest session status](NWOAS/NWOAS-STATUS-2026-09-09-SESSION.md),
[hardware evidence](NWOAS/nwoas_scripts/nvme-s93/hardware-evidence.json), and
[companion source patches](NWOAS/companion-patches/2026-09-07/README.md).
Build success alone is never treated as hardware success.

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
