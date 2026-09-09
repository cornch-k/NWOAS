# S185 review — NwoasHardwareBootRtcLib (direct SERA boot read + CNTPCT advance)

Read-only review, 2026-09-10. Scope: `rtc-s185/NwoasHardwareBootRtcLib/`, the
S131-pattern build in `rtc-s185/s131-build/`, the payload
`m1n1-payload-s185-hwrtc.bin`, and the S183/S184 evidence it depends on.
No code, build, DSC, launcher or hardware state was changed. New files are
limited to `rtc-s185/review-tests/` and this document.

## Verdict

No blocking defect found. The library does what its header claims: one
bounded read-only SPMI snapshot in DXE, plain-value globals, runtime GetTime
from CNTPCT_EL0 only, explicit errors instead of invented dates. Six
non-blocking observations are listed at the end; two of them (EFI_NOT_READY
status choice, Windows `RealTimeIsUniversal` dependency) deserve a decision
before this becomes the default payload.

## What was verified

**Sources are the reviewed ones.** `nwoas_sera_rtc.[ch]` are byte-identical to
`rtc-s184/`, `NwoasRtcSeedCore.[ch]` to `rtc-s181/` (`cmp`). The S184 change
(one 100 ms deadline shared by all three response words of a transaction,
deadline checked before ready data is accepted, poll cap kept) is in the copy.

**Build and payload.** `s131-build/build.log` shows the module-scoped DSC
override for `RealTimeClockRuntimeDxe` switched to
`NwoasHardwareBootRtcLib.inf` (log lines 275/348/396), all three sources
compiled with `-Wall -Werror` and no diagnostics, and the `.lib` archived
(04:52). `RealTimeClock.efi` (04:53) contains the four `NwoasHwRtc` strings.
The payload sha256 matches `manifest.json`; the LZMA-compressed DXE volume
inside the payload decompresses to a 7.2 MB image that contains
`NwoasHwRtc: DIRECT SERA` and no `NwoasSeedRtc` string, so the payload carries
this library and not the S181 seed variant. `dsc-before.bin` equals the
current DSC (restored). Note: the tree was rebuilt again at 04:57 (S187), so
the on-disk `J274MACMINI2020_EFI.fd` no longer matches this payload; the
verification above was done against the payload bytes themselves.
`rtc-s185/build.log` (top level) contains two manifest JSON dumps, not a build
log; the real log is `s131-build/build.log`.

**Callback ABI.** `EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClock.c` calls
`LibRtcInitialize(ImageHandle, SystemTable)` and returns any error verbatim
before installing `gEfiRealTimeClockArchProtocolGuid`; the library always
returns `EFI_SUCCESS`, so a failed read never removes the RTC protocol. The
driver pre-fills `TimeZone`/`Daylight` from its variable-backed settings and
the library overrides both unconditionally (verified in test with garbage
pre-fill). `SetTime` mutates the driver's zone copy before `LibSetTime` fails
with `EFI_UNSUPPORTED`; harmless because GetTime overrides. Prototypes match
`MdeModulePkg/Include/Library/RealTimeClockLib.h`. `LibRtcVirtualNotifyEvent`
is declared nowhere in that header and called by nobody; it is a dead symbol
kept for parity, not a hook.

**RuntimeDxe integration / no retained pointers.** Runtime state is four plain
values (`mSeedValid`, `mSeed`, `mFreqHz`, `mSeedNs`). `LibGetTime` touches only
CNTFRQ/CNTPCT system registers and those globals: no MMIO, no DEBUG, no boot
services, nothing to convert at SetVirtualAddressMap (test confirms zero
STATUS/RSP/CMD accesses during GetTime). `AppleDTLib` uses the fixed
`PcdAdtPointer` only inside `LibRtcInitialize`. The SPMI window
`0x23d0d9300` is inside the T810X device mapping (`0x200000000` + 2 GiB,
`MemoryInitPeiLib.c:437`) and the m1n1 HV maps every `/arm-io` range as HW by
default (`hv/__init__.py:1888`), so the DXE MMIO is a direct hardware access.
No other module in `Silicon/Apple` or `Platform` references the SPMI
controller, so there is no in-firmware bus contention; the FIFO-idle check
still guards against leftovers from iBoot/m1n1.

**Physical counter domain.** `ArmGenericTimerCounterLib` is mapped to
`AppleArmGenericTimerPhyCounterLib`, whose `GetSystemCount` is
`ArmReadCntPct` (`mrs cntpct_el0`). m1n1 `hv.c:126-132` sets
`CNTHCTL_EL2.EL1PCTEN` in both the ECV and non-ECV branch (T8103 takes the
non-ECV branch), stolen time goes only to `CNTVOFF_EL2` (`hv_exc.c:1820`) and
`CNTPOFF_EL2` is never written. Guest CNTPCT is therefore the same physical
counter the S183 host probe sampled, and it keeps running through HV pauses,
so GetTime stays correct across proxy stalls. Continuity across real sleep
states is not qualified and not claimed.

**Topology guard (ADT only).** Checks `/chosen chip-id == 0x8103`,
`/arm-io/nub-spmi reg == {0x3d0d9300, 0x100}` (arm-io-relative, matching the
S178 ADT dump and Linux `t8103.dtsi` `spmi@23d0d9300`), `spmi-pmu reg == 15`,
`info-rtc == 0xd002`, `info-rtc_scrpad == 0xd100`. All node pointers are
NULL-checked before `dt_node_prop` (which would dereference NULL). The guard
reads only ADT properties; no Linux DT index is carried across schemas, so the
S183 SMC `reg[1]` confusion cannot recur here. The arm-io base `0x200000000`
is assumed rather than read from `ranges`; acceptable while `chip-id` is
pinned to one SoC.

**Gregorian carry.** Sub-second carry is done by incrementing the epoch and
re-running `nwoas_rtc_epoch_to_calendar` (Hinnant `civil_from_days`), so
second/minute/hour/day/month/year and leap-day rollovers are one code path.
Tested: 2027-12-31T23:59:59.999984741 + 0.999999958 s -> 2028-01-01, Feb 28 ->
Feb 29 (2028, 2096) and Feb 29 -> Mar 1, 2029 non-leap, hourly sweep over
2026-2030 against host `gmtime`, and 2099-12-31T23:59:59 + carry ->
`EFI_DEVICE_ERROR` (no wrapped year).

**Truthful GetTime errors.** `EFI_INVALID_PARAMETER` (NULL Time),
`EFI_NOT_READY` (no seed: topology rejected, any bus/decode failure, window
guard), `EFI_DEVICE_ERROR` (CNTFRQ changed, counter below seed, time past
2099). Capabilities are filled even on NOT_READY. Re-initialisation clears an
earlier valid seed. See observation 1 on the NOT_READY choice.

**Overflow / uninitialised use.** `Nanosecond += mSeedNs` peaks at
1,999,984,740 < 2^32 and carries exactly once; `secs > EPOCH_MAX - epoch` is
checked before the add; `rem * 1e9 < 2^62`; `deadline_ticks <= 4e11`;
`(UINT32)freq` is safe because the helper rejects CNTFRQ > 4 GHz first;
`nwoas_sera_rtc_read` clears `result` before any early return, so the failure
DEBUG line prints defined values; `cal` and `reg` are only read after success
paths. Only cosmetic: the failure log's `span=%lu` prints a wrapped value if
`after < before` (that branch is itself the rejection reason).

**Integration window guard.** The helper's per-transaction deadline does not
cover the instant of its final `out->cntpct` sample; the integration's
`after - before > freq` check closes that gap. A mock jump of 2 s exactly at
that sample is rejected with `read failed ok ... GetTime NOT_READY`; a jump
after `after` is not observed by init at all (as intended).

**S183 evidence re-derived.** `hardware-result.json` hex fields are raw reply
bytes (little-endian). direct `0x08507afc885e` - 2 x CLKM `0x04283d7e43fa` =
106 ticks; direct + (offset `0x31289eae70e3` << 1) mod 2^49 >> 16 =
1788983385 = 2026-09-09T19:49:45Z, which the library reproduces end to end
over the mock bus. The probe's `subticks` 13533 is the CLKM 33.15 fraction;
the direct 32.16 fraction is 2 x 13533 + 106 = 27172. The old Linux-formula
misread that produced 1928475477 (2031) in the same session's
`rtc-bounded-probe.json` is reproduced as a negative check. Only seconds are
proven; the +2.2 s (S183) and +0.9 s unsynchronised (S185 job1) offsets are
properties of the PMU clock versus the host, not of this library.

## Tests added (isolated, new files only)

`rtc-s185/review-tests/`: `test_s185_integration.c` compiles the real,
unmodified `NwoasHardwareBootRtcLib.c` against shim UEFI headers
(`shim/PiDxe.h`, `shim/Library/*.h`) with a mock SPMI controller, mock ADT
and mock physical counter. `run_review_tests.sh` runs it plain `-O2` and
ASan+UBSan, then rebuilds the unmodified S184 and S181 suites into
`review-tests/build/` (their own `build/` dirs untouched).

| suite | plain | ASan+UBSan |
|---|---|---|
| S185 integration (new) | 358 checks, 0 failures | 358 checks, 0 failures |
| S184 helper (`rtc-s184/tests`) | 449 checks, 0 failures | 449 checks, 0 failures |
| S181 core (`rtc-s181/tests`) vs S185 copy | 182 checks, 0 failures | 182 checks, 0 failures |

The library builds warning-free under `-Wall -Wextra -Wshadow -Wconversion
-Wsign-conversion -Werror` on the host except `-Wunused-parameter` in the
stub SetTime/wakeup/notify functions, which the firmware flags do not enable.

## Observations (non-blocking)

1. **`EFI_NOT_READY` from GetTime is not a spec-listed status.** UEFI GetTime
   defines SUCCESS, INVALID_PARAMETER, DEVICE_ERROR, UNSUPPORTED. The unseeded
   path has never been exercised under Windows Boot Manager (the S185 hardware
   boot exercised only the seeded path). Decide whether the unseeded case
   should return `EFI_DEVICE_ERROR` for conformance; behaviourally either code
   means "no time" and both avoid inventing a date.
2. **Windows interprets the RTC as UTC only because `RealTimeIsUniversal=1`
   was set by an S183 job.** The library always reports UTC with
   `TimeZone = unspecified`. If that registry value is lost (repair install,
   image refresh), Windows will read UTC as KST and show a -9 h error. Record
   this as a deployment dependency of the payload.
3. **Accuracy = 100 ppm is a nominal placeholder.** Nothing in S183/S185
   measures drift; the field must not be quoted as measured precision.
   `Resolution = 1` while `Nanosecond` is populated is a mild inconsistency,
   defensible because the sub-second value is unqualified (S184 notes the
   fraction is 1/65536 s low for odd raw counters).
4. **Boot-session clock only.** No persistence, `SetTime` unsupported, no
   wakeup. Windows time sync will fail to write the hardware clock; the
   library comment says so and nothing here contradicts it.
5. **Guard hardcodes the arm-io translation** (`0x200000000`) instead of
   reading `/arm-io ranges`; fine for T8103, worth reading if `chip-id` is
   ever widened.
6. **Housekeeping.** `rtc-s185/build.log` is a manifest dump, not a log;
   `LibRtcVirtualNotifyEvent` is dead code; `freq == 0` and
   `result.cntpct > after` in `LibRtcInitialize` are unreachable after the
   helper's own checks (harmless defence).

## Not verified here

Hardware behaviour (main's trial ran independently), Windows handling of
`EFI_NOT_READY`, counter continuity across sleep, long-term drift, and any
claim about the PMU clock's absolute accuracy.
