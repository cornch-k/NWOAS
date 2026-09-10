# 2026-09-10 morning handoff

## Current execution

The Mac mini is running Windows ARM64 with eight logical processors and 15,081,889,792 bytes of Windows-visible RAM. The MacBook host runtime remains required. This is not standalone native boot, and native GPU/network/USB-C hotplug drivers are not complete.

The previous bounded-qualified S223 fallback launcher is `nwoas_scripts/loader-s223-fd-tail-guest-test.sh`; its matching results record integration, 10 GiB x3 memory, 15-minute mixed and five-minute active-read PASS. It uses the S208 gap50 HV and appends 480 KiB of deterministic 0xff padding to the otherwise unchanged S216 payload. The complete copy span is supplied by the payload. This does not establish a cause for historical watchdog failures.

Keep the active MacBook host terminal/runtime alive. Do not start another launcher against the occupied serial port. No scheduled automation was created or enabled. No automatic reboot or follow-up worker mutation is intended after final inventory.

## Measured fallback

`nwoas_scripts/firmware-s216-handoff-guest-test.sh` is the S216 fallback. It passed two short boot/integration runs, 10 GiB memory across three passes, a five-minute active read test and a thirty-minute mixed CPU/read test. A low-priority host build overlapped part of the mixed test, so those timings are not isolated performance measurements.

S218 Cinebench 2026.1.3 ARM64 CPUX measured **1888.454**, native process exit 0, on **S216**, not S223. The prior S196 sample was 1905.281 (difference -0.883%). Single samples across different firmware/CPU initialization settings do not prove a performance regression or improvement. No same-Mac-mini macOS baseline has been measured. The reported 30 MHz CPU metadata is not a measured hardware clock.

## Reproducibility and next work

S215 rebuilds the guest prefix. S216 reserves the BootArgs/ADT handoff range and guards relocation copies. S221 recovers readable DTS that reproduces the original static DTB byte-for-byte, but the historical upstream DTS revision remains unknown. The complete S216 payload rebuilt to the same SHA-256 after switching to that recovered source.

S209 is a tested freestanding C control-state model, not an installed NVMe driver: doorbell submission remains unimplemented and no runtime host dependency was removed by it. Next milestones are integrating a bounded native control path, moving physical I/O ownership out of the host runtime, and validating native Windows drivers. Preserve current recoverable boot configurations while doing so.

See `NWOAS-STATUS-2026-09-10-SESSION.md`, `nwoas_scripts/bench-s218/result.json`, and `nwoas_scripts/publish-s217/companion/README.md` for evidence and source patches. Raw device logs and identity-bearing benchmark output are intentionally not public artifacts.

## 10:12 KST extension checkpoint

Current live session is S224, launched by
`nwoas_scripts/native-s224-read-mirror-guest-test.sh`, log
`native-s224-read-mirror-20260910-101056.Tq5Myx`. Its host runtime must remain
running. The first S224 boot passed integration, 10 GiB x3 full-word memory,
2253 active reads over five minutes, and 31 mixed CPU/read samples spanning
1807.49 seconds. Normal Windows reboot preserved the 256 MiB test archive hash;
the second boot again confirmed eight cores, 15,081,889,792 bytes and P12.
A second five-minute active-read test is running at this checkpoint.

S224 serves supported PCI/register reads locally (308 cumulative reads at the
end of the first soak) but retains the host control/admin writer, NS2 and boot
setup. Source-built HV SHA256 is
6679506629bc77a3e428b33a5f4229009abdaef6b1e71087e4cebeaa6c222955;
the S223 payload remains unchanged. There is no S224 Cinebench or isolated
whole-system speedup measurement. Small host-side offline builds overlapped
parts of first-boot validation, so timings are qualification observations.

S225/S226 local-admin payload/state foundations passed offline differential,
sanitizer and freestanding ARM64 checks with Claude Code review corrections.
They are not installed. `native-s226/INTEGRATION.md` records the remaining
completion/DMA/IRQ/lifecycle work. Current problem-device inventory still has
USB Input Device code10 and two unnamed code28 entries. VideoController returned
no rows; this does not establish GPU driver support. C: free was 4,130,156,544 B.
Do not infer pointer operation or hotplug success from these checks.

## Final S224 extension result

Second-boot active reads passed 2229 iterations over 300093 ms, median 125800 us (logical compressed-file reads, not raw SSD throughput). Final read-only target query returned the S224 capability and zero target I/O error count. Both boot sessions have acknowledged test exit0. The current MacBook runtime remains running and required; no reboot or mutation job remains queued. Source, build and first-boot/second-boot evidence are included. No S224 Cinebench, native GPU/network driver, standalone boot or macOS parity result is claimed.
