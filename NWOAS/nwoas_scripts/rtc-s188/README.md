# rtc-s188 — NwoasHardwareBootRtcLib with spec-listed unseeded GetTime status

Bounded offline task, 2026-09-10 (host tests run 2026-09-09 20:10 UTC).
`rtc-s188/` is a copy of `rtc-s185/NwoasHardwareBootRtcLib/` and its
`review-tests/` with exactly one behavioural change, taken from S185
`REVIEW.md` observation 1:

> `EFI_NOT_READY` from GetTime is not a spec-listed status. UEFI GetTime
> defines SUCCESS, INVALID_PARAMETER, DEVICE_ERROR, UNSUPPORTED.

**Change:** `LibGetTime` returns `EFI_DEVICE_ERROR` instead of
`EFI_NOT_READY` when no valid boot seed exists (topology rejected, SPMI
read/decode failure, integration window guard, or a later failed re-init).

**Unchanged (verified by the same test suite):** `LibRtcInitialize` always
returns `EFI_SUCCESS` so RealTimeClockRuntimeDxe still installs the protocol;
all seed / CNTPCT advance / nanosecond-carry / Gregorian arithmetic; the
bounded read-only SPMI snapshot; no PMU or RTC writes anywhere; capabilities
still filled on the unseeded path; `SetTime`, `GetWakeupTime`,
`SetWakeupTime` still `EFI_UNSUPPORTED`.

Nothing in `rtc-s185/`, `rtc-s181/`, `rtc-s184/`, `usb-s187/`, the UEFI
repository, launchers, payloads or hardware state was modified. No firmware
build, no hardware boot, no `.env` read. No other agents were spawned.

## Files

| path | origin | change |
|---|---|---|
| `NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.c` | copy of S185 | one return value, its `@retval` doc, three `DEBUG` strings (`GetTime NOT_READY` → `GetTime DEVICE_ERROR`), header note |
| `NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.inf` | copy of S185 | none (byte-identical) |
| `NwoasHardwareBootRtcLib/NwoasRtcSeedCore.[ch]` | copy of S185 (= S181) | none (byte-identical) |
| `NwoasHardwareBootRtcLib/nwoas_sera_rtc.[ch]` | copy of S185 (= S184) | none (byte-identical) |
| `review-tests/test_s188_integration.c` | copy of `test_s185_integration.c` | unseeded expectations updated, see below |
| `review-tests/run_review_tests.sh` | copy of S185 runner | output names `s188_*`, comments |
| `review-tests/shim/` | copy of S185 shim headers | none |
| `review-tests/build/` | generated | binaries and logs from the run below |

The complete library diff against S185 is:

```
-    return EFI_NOT_READY;
+    return EFI_DEVICE_ERROR;
```

plus the three log strings, the `@retval` comment and a header paragraph.
`diff -u ../rtc-s185/NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.c
NwoasHardwareBootRtcLib/NwoasHardwareBootRtcLib.c` reproduces it.

## Test changes (isolated to `rtc-s188/review-tests/`)

`expect_not_ready()` became `expect_unseeded_device_error()`. For each of
the 28 unseeded cases (14 topology mismatches, 11 bus/decode failures,
2 window-guard jumps, 1 re-init) it now checks:

- init returns `EFI_SUCCESS` (protocol install preserved);
- `LibGetTime` returns `EFI_DEVICE_ERROR` and explicitly not `EFI_NOT_READY`;
- the unseeded path samples neither the counter nor CNTFRQ (new; keeps it
  distinguishable from the seeded `EFI_DEVICE_ERROR` paths: frequency change,
  counter backwards, year > 2099);
- capabilities are still filled (`Resolution=1`, `Accuracy=100000000`,
  `SetsToZero=FALSE`);
- the last DEBUG line contains `GetTime DEVICE_ERROR` and not `NOT_READY`.

Everything else in the test (S183 hardware vector arithmetic, seeded GetTime,
nanosecond carry, Gregorian rollovers, bounded CMD writes, no FIFO drain) is
the S185 test verbatim. `S185_REVIEW_VERBOSE` became `S188_REVIEW_VERBOSE`.

## Results (exact)

Host: macOS, Apple clang 21.0.0 (clang-2100.1.1.101), arm64. Command:
`review-tests/run_review_tests.sh` (exit 0, full output in
`review-tests/build/run_review_tests.log`). Warning flags as in S185:
`-std=c11 -Wall -Wextra -Wno-unused-parameter -Wshadow -Wconversion
-Wsign-conversion -Werror`; sanitizer run `-O1 -g -fsanitize=address,undefined
-fno-sanitize-recover=all`.

| suite | sources | plain -O2 | ASan + UBSan |
|---|---|---|---|
| S188 integration (`test_s188_integration.c`) | S188 library | **470 checks, 0 failures** | **470 checks, 0 failures** |
| S184 helper (`rtc-s184/tests/test_sera_rtc.c`) | unmodified `rtc-s184/` | 449 checks, 0 failures | 449 checks, 0 failures |
| S181 core (`rtc-s181/tests/test_rtc_core.c`) | S188 copy of `NwoasRtcSeedCore.c` | 182 checks passed, 0 failed | 182 checks passed, 0 failed |

Check count vs S185 (358): +112 = 28 unseeded cases × 4 new checks.
No sanitizer reports. No compiler warnings under `-Werror`.

**Negative cross-check.** The S188 test compiled against the *unmodified*
S185 library (`build/s188test_vs_s185lib`, read-only use of S185 sources,
output only in `rtc-s188/review-tests/build/`) gives
`470 checks, 113 failures`, exit 1 (log: `build/crosscheck_vs_s185.log`):
the 28 status checks, 28 `!= EFI_NOT_READY` checks, 28 `GetTime DEVICE_ERROR`
log checks, 28 `NOT_READY`-absent log checks, plus the re-init case.
So the test detects exactly the intended change.

## Qualification notes

**UTC registry dependency.** The library reports UTC with
`TimeZone = EFI_UNSPECIFIED_TIMEZONE`, always. Windows interprets that as UTC
only because `HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation\
RealTimeIsUniversal = 1` was set by an S183 job. If that value is lost
(repair install, image refresh, in-place upgrade) Windows will treat the UTC
value as local time and show a -9 h error in KST. This is a deployment
dependency of any payload built from this library, not something the library
can fix.

**Unsupported and unqualified, unchanged from S185.**

- `SetTime` → `EFI_UNSUPPORTED`. Windows time sync cannot write the hardware
  clock; the PMU offset cell is never written.
- `GetWakeupTime` / `SetWakeupTime` → `EFI_UNSUPPORTED`. No RTC alarm.
- Suspend / resume: CNTPCT continuity across sleep is not qualified; the
  clock is a boot-session clock (one SERA snapshot at DXE init, advanced by
  the physical counter).
- Drift: `Accuracy = 100 ppm` is a nominal placeholder, not measured.
- Sub-second: `Nanosecond` is populated but unqualified (S184 notes the
  fraction is 1/65536 s low for odd raw counters); `Resolution = 1`.

**Not verified here.** How Windows Boot Manager / the kernel react to
`EFI_DEVICE_ERROR` from GetTime on real hardware (the S185 hardware boot
exercised only the seeded path, and no S188 hardware boot has been done).
Behaviourally both codes mean "no time available" and neither invents a
date; the S188 code is the one the UEFI specification lists.

## Next step (main's decision)

If S188 is adopted, an S131-pattern build and a hardware boot with a
deliberately failed seed (e.g. topology guard tripped) would show what
Windows does with `EFI_DEVICE_ERROR`. That is outside this task's bounds.
