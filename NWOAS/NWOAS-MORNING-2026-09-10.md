# 2026-09-10 morning handoff

## Current execution

The Mac mini is running Windows ARM64 with eight logical processors and 15,081,889,792 bytes of Windows-visible RAM. The MacBook host runtime remains required. This is not standalone native boot, and native GPU/network/USB-C hotplug drivers are not complete.

The current bounded-qualified S223 launcher is `nwoas_scripts/loader-s223-fd-tail-guest-test.sh`; its matching results record integration, 10 GiB x3 memory, 15-minute mixed and five-minute active-read PASS. It uses the S208 gap50 HV and appends 480 KiB of deterministic 0xff padding to the otherwise unchanged S216 payload. The complete copy span is supplied by the payload. This does not establish a cause for historical watchdog failures.

Keep the active MacBook host terminal/runtime alive. Do not start another launcher against the occupied serial port. No scheduled automation was created or enabled. No automatic reboot or follow-up worker mutation is intended after final inventory.

## Measured fallback

`nwoas_scripts/firmware-s216-handoff-guest-test.sh` is the S216 fallback. It passed two short boot/integration runs, 10 GiB memory across three passes, a five-minute active read test and a thirty-minute mixed CPU/read test. A low-priority host build overlapped part of the mixed test, so those timings are not isolated performance measurements.

S218 Cinebench 2026.1.3 ARM64 CPUX measured **1888.454**, native process exit 0, on **S216**, not S223. The prior S196 sample was 1905.281 (difference -0.883%). Single samples across different firmware/CPU initialization settings do not prove a performance regression or improvement. No same-Mac-mini macOS baseline has been measured. The reported 30 MHz CPU metadata is not a measured hardware clock.

## Reproducibility and next work

S215 rebuilds the guest prefix. S216 reserves the BootArgs/ADT handoff range and guards relocation copies. S221 recovers readable DTS that reproduces the original static DTB byte-for-byte, but the historical upstream DTS revision remains unknown. The complete S216 payload rebuilt to the same SHA-256 after switching to that recovered source.

S209 is a tested freestanding C control-state model, not an installed NVMe driver: doorbell submission remains unimplemented and no runtime host dependency was removed by it. Next milestones are integrating a bounded native control path, moving physical I/O ownership out of the host runtime, and validating native Windows drivers. Preserve current recoverable boot configurations while doing so.

See `NWOAS-STATUS-2026-09-10-SESSION.md`, `nwoas_scripts/bench-s218/result.json`, and `nwoas_scripts/publish-s217/companion/README.md` for evidence and source patches. Raw device logs and identity-bearing benchmark output are intentionally not public artifacts.
