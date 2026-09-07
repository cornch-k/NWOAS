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

## Current status — 2026-09-07

Windows11 ARM64 Setup boots on the M1 Mac mini with Tahoe firmware. USB-A
keyboard and a directly attached USB-C Magic Trackpad have user-confirmed input.
Setup reaches disk selection after the documented single-core compatibility workaround.

S93 adds a **host-mediated NVMe controller** over the internal ANS2 SSD: Windows
enumerated it and completed real I/O reads; the Setup disk list showing the
internal SSD was user-confirmed (S94). In S95 the APFS container was shrunk from
Recovery and a 25 GB test partition WINTEST was created. S96 adds a write path
that is **restricted to the WINTEST LBA window at three layers** (m1n1 C guard,
relay namespace, guest module); a host-side round-trip and refusal test passed,
and Windows Setup then formatted WINTEST to NTFS through the relay
(user-confirmed, zero out-of-window writes). S97 moves to 16-block
commands; the same format then took about 10 s instead of 10 min (≈5.8 MB/s).
Installation, standalone storage operation, SMP, throughput adequate for an
install, and the requested macOS/Windows dual-boot layout remain unfinished.

Start with the [fable5.1 handoff](NWOAS/NWOAS-HANDOFF-FABLE5.1-2026-09-07.md),
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
