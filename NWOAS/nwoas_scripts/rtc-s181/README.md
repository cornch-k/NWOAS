# S181 — boot-seeded UEFI RTC library (OFFLINE CANDIDATE)

Status: **candidate only. Not built into firmware, not run on hardware, not
wired into any launcher.** Everything in this directory was produced without
touching `apple_silicon_platforms_mu/` (read-only) and without hardware access.

**This is a boot seed, not a native RTC.** The S178 bounded SPMI probe that
would show whether PMU register 0xd002 really is the RTC counter has not
produced evidence yet. This library does not depend on that answer: it only
consumes whatever UTC epoch the host module chooses to place in the ADT.

## Files

| Path | Purpose |
|---|---|
| `NwoasSeedRealTimeClockLib/NwoasSeedRealTimeClockLib.c` | UEFI `RealTimeClockLib` implementation (candidate) |
| `NwoasSeedRealTimeClockLib/NwoasSeedRealTimeClockLib.inf` | Module description for the candidate build |
| `NwoasSeedRealTimeClockLib/NwoasRtcSeedCore.[ch]` | Pure C99 core: seed parse/validate, counter advance, calendar. No UEFI, no libc, no hardware. Shared by the library and the host tests |
| `tests/test_rtc_core.c` | Boundary tests (183 checks + ~600k sweep samples) |
| `run_tests.sh` | Plain, ASan+UBSan, C++ hygiene, TimeBaseLib cross-check, arm64 syntax check of the UEFI file |
| `build/` | Test binaries and logs (generated) |

## Seed contract (input)

ADT `/chosen` property `nwoas,rtc-snapshot`, exactly 32 bytes, little endian,
Python `struct` format `<IIQQII` (matches `rtc-s178/rtc_math.py`):

```
u32 magic    = 0x4E525443
u32 version  = 1
u64 epoch    = Unix UTC seconds when cntpct was sampled
u64 cntpct   = CNTPCT_EL0 at that moment
u32 cntfrq   = CNTFRQ_EL0 in Hz
u32 flags    = bit0 valid (required), bit1 read via SPMI, bit2 read via SMC CLKM
```

Rejected at DXE init (GetTime then returns `EFI_NOT_READY`):
size != 32, wrong magic, version != 1, valid bit clear, any flag bit above
bit2, cntfrq == 0, cntfrq != live `CNTFRQ_EL0`, epoch outside
2000-01-01..2099-12-31, or live `CNTPCT_EL0` already below the seed value.

The frequency mismatch is a hard reject. PLAN.md suggested "prefer guest
frequency and warn"; that was changed because a mismatch means the seed's
`cntpct` is in a different tick domain and any date derived from it would be
a guess. Under m1n1 HV host and guest `CNTFRQ_EL0` are identical, so a real
seed never hits this.

## Runtime behaviour

* `LibRtcInitialize` (DXE, before ExitBootServices): looks up `/chosen`
  (NULL-checked first because `AppleDTLib dt_get_prop()` dereferences a NULL
  node), copies the 32 bytes out of the ADT, parses/validates, does one trial
  advance+calendar conversion, then stores the values in module globals.
  **No ADT pointer survives this function.** Always returns `EFI_SUCCESS`
  (see "error propagation").
* `LibGetTime`: reads `CNTFRQ_EL0` and `CNTPCT_EL0` through
  `ArmGenericTimerCounterLib` (mapped to `AppleArmGenericTimerPhyCounterLib`,
  i.e. the physical counter; m1n1 only offsets the virtual counter with
  stolen time). Computes `epoch + delta/freq`, converts to `EFI_TIME`, sets
  `Nanosecond` from the remainder, forces `TimeZone = 2047` (unspecified) and
  `Daylight = 0`. No DEBUG, no boot services, no MMIO: safe after
  SetVirtualAddressMap.
  Returns `EFI_NOT_READY` without a seed, `EFI_DEVICE_ERROR` if the frequency
  changed, the counter went backwards, or the time left 2000..2099.
* Capabilities: `Resolution = 1` (the seed is integer seconds, so 1 Hz is the
  honest granularity), `Accuracy = 100,000,000` (UEFI unit is 1e-6 ppm, so
  this is a 100 ppm upper bound, not a measurement), `SetsToZero = FALSE`.
* `LibSetTime`, `LibGetWakeupTime`, `LibSetWakeupTime`: `EFI_UNSUPPORTED`.
  No PMU or SMC access anywhere in this code.

## RuntimeDxe error propagation and global linkage (checked)

`EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClock.c`:

* `InitializeRealTimeClock` returns any error from `LibRtcInitialize`
  verbatim, and the RTC architectural protocol is only installed after that.
  A failing init would therefore leave DXE without `gEfiRealTimeClockArchProtocolGuid`.
  That is why a missing seed is not an init error here.
* `GetTime` pre-fills `TimeZone`/`Daylight` from its own `mTimeSettings` and
  passes the struct to `LibGetTime`; the library overrides both fields.
* `SetTime` updates its in-memory `mTimeSettings` **before** calling
  `LibSetTime`, so a caller that gets `EFI_UNSUPPORTED` back has still changed
  the driver's zone/daylight copy. Overriding the fields in `LibGetTime` makes
  the library immune to that.
* The driver does not call `LibRtcVirtualNotifyEvent`; it is defined as a
  no-op only for parity with the existing AppleSiliconPkg library. The
  library's globals are plain values inside the `DXE_RUNTIME_DRIVER` image
  (runtime-services data), so no pointer conversion is needed.
* `DXE_RUNTIME_DRIVER` links `DxeRuntimeDebugLibSerialPort`, which silences
  itself after ExitBootServices; `LibGetTime` still emits no DEBUG.

## Integration (candidate build only; not done here)

1. Copy `NwoasSeedRealTimeClockLib/` to
   `Silicon/Apple/AppleSiliconPkg/Library/NwoasSeedRealTimeClockLib/`.
2. In `AppleSiliconPkg.dsc.inc` change the module-scoped override for
   `EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf` from
   `AppleSiliconPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.inf`
   to `AppleSiliconPkg/Library/NwoasSeedRealTimeClockLib/NwoasSeedRealTimeClockLib.inf`.
3. Build with the `smp-s131/build_candidate.py` pattern (swap files, build,
   restore originals, write `.before/.candidate`, hash the payload).
4. The host side must add the seed (Phase A in `rtc-s178/PLAN.md`,
   `rtc_snapshot_module.py`). Without it the candidate boots with GetTime
   returning `EFI_NOT_READY`.

Hardware acceptance (future session, owner-run): serial log line
`NwoasSeedRtc: BOOT SEED (not native RTC) ...`, Windows
`(Get-Date).ToUniversalTime()` within a few seconds of host UTC, no
2022-05-07 regression, Authenticode `NotTimeValid` gone. None of that proves
persistence across power-off; that remains PMU territory (Phase C).

## Known limitations and open risks

* **Behaviour change vs. today.** The active library always returns
  `EFI_SUCCESS` with a fixed 2019 date (and `Day = 0` right after boot).
  This candidate returns `EFI_NOT_READY` when unseeded. How Windows Boot
  Manager / winload react to `EFI_NOT_READY` from `GetTime` has not been
  observed. PLAN.md preferred "keep current behaviour when unseeded"; the S181
  brief chose NOT_READY. Decide before the first hardware run.
* The seed's plausibility window (2024..2036) is enforced by the producer,
  not here; the library only enforces the EFI-representable 2000..2099.
* Time is only as good as the seed source. If the host module's UTC comes
  from an unproven PMU read, the reported time inherits that uncertainty.
* Nothing persists. Reboot re-seeds from the host; power-off relies on the
  PMU, which this code never touches.
* Nanosecond is computed from a 24 MHz counter but the seed is quantised to
  1 s plus the SPMI read window; sub-second values are consistent, not
  absolute.

## Tests

```
./run_tests.sh
```

Covers: exact-size rejection (0/31/33/64), magic (swapped, off-by-one, zero),
version 0/2, flag bit rules, LE decode of full 64-bit fields, frequency zero /
too wide / mismatch by ±1 Hz, epoch window bounds ±1 s, counter underflow
(by 1 and to 0, and at `UINT64_MAX`), overflow at 2099-12-31T23:59:59 and
with `UINT64_MAX` tick deltas, max-frequency remainder arithmetic,
leap-year rules (2000, 2024, 2023, 2100, 1900, 2400), month lengths,
`Day = 0`, Feb 29/30, month 0/13, hour 24, min/sec 60, ns 1e9, TZ ±1441 /
2046 / 2047, daylight 2/4/255, exhaustive day sweep 2000..2099 × 8 times of
day against libc `gmtime_r` plus round-trip, 200k random epochs, full-year
day-validity round-trip, and a 109,575-sample comparison against the tree's
own `EpochToEfiTime()` / `IsTimeValid()` compiled read-only from
`Common/TIANO/EmbeddedPkg/Library/TimeBaseLib/TimeBaseLib.c`.

Last run (2026-09-10, Apple clang 21, arm64 host): all stages PASS,
183 checks, 0 failures, no sanitizer findings.
