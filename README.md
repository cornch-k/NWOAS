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

The current frontier is the loader-to-kernel handoff step, blocked on
vGIC interrupt/maintenance-interrupt handling.

## Layout

- `NWOAS/` — this project: autounattend/Windows PE answer files, prebuilt
  m1n1 test binaries, hardware reference driver headers, and helper
  scripts used across boot experiments.
- `macvdmtool/` — a small macOS CLI tool for driving another Apple Silicon
  device's DFU/serial console over USB-C (see below).

Two larger companion components this project builds on are maintained as
their own repositories rather than duplicated here:

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

`autounattend*.xml` files use a placeholder Windows account password
(`CHANGE_ME_BEFORE_USE`) — set your own before using them to install
Windows.
