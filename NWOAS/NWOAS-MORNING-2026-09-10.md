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

## 10:19–11:30 KST extension — S227–S230

The user extended this live work until 11:30 KST; no scheduler was created.
S227 bounded admin rings and S228 control/admin/IRQ composition were built,
differential-tested and reviewed with Claude Code Fable5.1. S229 connected
those components to the existing target C S149 ANS data path and booted Windows.
Supported PCI, control and admin requests now stay on the target. The host
Python controller no longer owns those requests. NS2 service/RAM transport,
host boot/DCP setup and EL2 mediation still remain. This is not a standalone
Windows ANS driver, native GPU/network completion or measured overall speedup.

S230 fixes the S229 review's tick-path printf and post-shutdown doorbell
handling. First boot passed 8-core integration, P12, 256 MiB complete-word
write/read verification and 10 GiB memory across three passes. It also verified
the 256 MiB archive saved on S229 before a normal Windows restart.
The S230 ten-minute mixed soak and second-boot qualification are pending at
this checkpoint; final evidence will follow. Current launcher is
`nwoas_scripts/native-s230-admin-guest-test.sh`; host runtime must remain alive.
HV SHA256: `2d763bfb3aa6c55d8fbcd3d8ed90eeb4bb322f177d87ae279c1c53efbe7fa2c1`.
The unchanged S223 payload SHA256 is
`2343f7fc35acd0401bafd63bfdc10d047ad8c4bc78207056c42e478051a8b5e7`.

S224 is the previous bounded-qualified fallback. Earlier paragraphs saying
S225/S226 are uninstalled describe their earlier checkpoint; they are now
linked into S229/S230. No new Cinebench run was made during this extension.

## 11:22 KST — S230 bounded qualification complete

Two S230 Windows boots passed. The first completed 8-core/P12 integration,
256 MiB full-word write/read, 10 GiB memory x3, and 11 mixed CPU/read samples
over602.505147s. Normal Windows restart preserved the exact256MiB archive hash.
The second boot passed2555 consecutive logical64MiB reads over300030ms, median
125558us and maximum1177709us. The maximum remains an unexplained latency
outlier; these results do not prove raw SSD throughput or overall speedup.
Final target I/O error count was0 and CSTS was1. Admin fetch/completion counters
were112/111; a held AER accounts for a possible one-command difference.

Claude Code Fable5.1 reviews of S227–S230 prompted bounded-ring/IRQ lifecycle
corrections, the target tick-log and shutdown fixes, and stronger test coverage.
The final extracted production process/poll/IRQ/link harness passed28 directed
ASan/UBSan cases with physical/proxy/memory callbacks mocked. Source hashes and
review dispositions are recorded. The first soak assessment's default30-minute
completion marker was corrected to the actual10-minute marker; checksums,
exit0 and the600-second duration threshold were unchanged.

Current runtime: `native-s230-admin-20260910-111548.TRiw43.link`; launcher
`nwoas_scripts/native-s230-admin-guest-test.sh`. Keep the MacBook runtime alive.
No pending reboot/mutation remains after acknowledged second-boot checks.
The previous S224 runtime is the fallback, not the current execution.

Windows still sees15,081,889,792B and8cores. USB Input Device code10 and two
unnamed code28 entries remain; VideoController inventory is empty. No new GPU,
network or USB hotplug support is claimed. C: free was3,584,143,360B.
No new Cinebench was run;1888.454 remains the earlier S216 CPU multi-core result.
Target C now owns synthetic PCI/control/admin plus the existing ANS data path;
NS2 transport and host boot/DCP/EL2 dependencies remain. `native-s230/NEXT.md`
records the exact dynamic NS2 sectors and RAM coherence requirements for the
next migration. Earlier status paragraphs are historical checkpoints.
