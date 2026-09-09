# S170 UEFI CPU operating-point (P12) helper

Header-only C helper that requests the existing, already-supported **P12**
operating point on the T8103 (Apple M1) P-core cluster (PCPU0) at the firmware
stage, plus host tests and a fake-device stub. This is the offline, firmware-
stage successor to the hardware-validated Python module
`../cpufreq-s164/pstate12.py`.

The helper is inert until called. Main-agent integration in `ready_to_boot.inc` and
`build_candidate.py` has now built a hash-pinned candidate, with
`../cpufreq-s170-guest-test.sh` prepared. It is not hardware-validated yet.

## Why a firmware-stage helper

`../cpufreq-s164/pstate12.py` ran over USB RPC *before* UEFI. On hardware, that
pre-UEFI P12 request was **overwritten — reset back to P7**. The inner m1n1
payload's own cpufreq initialization is the leading source-code explanation;
the exact overwriting instruction has not been traced (`preboot-p12-reverted-p7.json` shows the
four P-cores back near 90 ms). The P12 gain was therefore **validated only after
Windows had booted**: `../cpufreq-s164/late_pstate12.py` re-applied P12 once the
payload init was complete and the Windows worker had connected, and measured the
four P-cores improving ~1.53x (90 ms -> 59 ms in `live-p12.json`) with all 40
workload checksums matching — the same workload at a higher operating point.

The proposed firmware fix is to request P12 **after the inner payload finishes its
initialization but before control leaves firmware** — a UEFI **ReadyToBoot**
callback is the natural hook (see below). This helper is that request, written
so it can run with no RPC transport.

## What it does and does NOT do

* **T8103 only.** `chip_id` must be `0x8103`; anything else returns
  `NwoasPstateUnsupportedChip` and performs no MMIO.
* Operates solely on the PCPU0 command register `0x211e20020` and its status
  register at `+0x30` (`0x211e20050`). Register facts are taken from
  `../../m1n1_windows/src/cpufreq.c` (`t8103_clusters[]` base `0x211e00000` +
  `CLUSTER_PSTATE 0x20020`; `BUSY BIT(31)`, `SET BIT(25)`, `M1_APSC_DIS BIT(22)`,
  `FIXED_FREQ_PLL_RECLOCK BIT(42)`, `DESIRED1[4:0]` + `DESIRED2[15:12]`).
* Programs **only** the `DESIRED1|DESIRED2` fields (`0xf01f`) plus the `SET`
  trigger, via ReadModifyWrite. Every other bit — including the protected
  `M1_APSC_DIS` (bit 22) and `FIXED_FREQ_PLL_RECLOCK` (bit 42) — is preserved and
  then verified unchanged.
* Accepts only the validated **P7 baseline** (same field check as the Python:
  not BUSY, and `DESIRED == 7|(7<<12)`).
* **No** SMC / MCC / thermal-feature (ppt/llc/amx) writes, **no** voltage-table
  edits, **no** turbo. P12 is a normal supported operating point.
* Verifies success three ways: `DESIRED` readback == P12, STATUS `current[7:4]`
  and `target[3:0]` == 12, and protected bits unchanged.

## Success and rollback contract

Success is decided from a **single** captured STATUS read: `current[7:4]` and
`target[3:0]` must both be P12 and the protected bits must be intact. The helper
validates exactly the value it stores in the snapshot — it does not read STATUS a
second time, so a STATUS that changes between reads can never make it claim P12
while recording P7.

On any P12 failure the helper attempts a **bounded** P7 restoration, but only if
the controller is idle. It waits for BUSY to clear before *every* request,
including the rollback, so it never writes a new request into a live transition.
The rollback outcomes are explicit and distinct:

* `NwoasPstateRolledBack` — P7 written, controller idle, **STATUS is 7/7 and
  protected bits intact**. A matching command field alone is not sufficient.
* `NwoasPstateRollbackBusy` — controller still BUSY *before* the P7 write, so
  **P7 was not written**. `rollback_write_issued` is `FALSE`.
* `NwoasPstateRollbackUnverified` — the P7 write **was** issued but the post-write
  idle wait timed out, so restoration cannot be confirmed.
  `rollback_write_issued` is `TRUE`.
* `NwoasPstateRollbackFailed` — P7 written and idle, but the command/STATUS did
  not verify as P7.

The helper never reports `NwoasPstateRolledBack` while the controller is BUSY.

A NULL or incomplete callback interface (missing `read64`/`write64`) is rejected
with `NwoasPstateInvalidIo` before any pointer is dereferenced. Every return
(success or failure) fills a `NWOAS_CPU_PSTATE_RESULT` snapshot: overall status,
the original P12-failure reason, the rollback result and whether it wrote P7, and
the observed command/status registers. `nwoas_cpu_pstate_status_name()` gives a
short ASCII label for logs.

## Bounded clock assumptions

There is no RPC/USB transport at the firmware stage, so idle is polled directly
against MMIO: up to `NWOAS_IDLE_ATTEMPTS` (250) reads of the command register,
each still-BUSY read followed by a `delay_us(NWOAS_IDLE_DELAY_US)` (1 µs) call
through the caller's callback. This is an **upper attempt bound**, not a claim
that a real T8103 transition takes 250 µs. The original Python used a 250 ms
*host* budget under RPC; that number does not carry over. `set_pstate()` in
m1n1 uses a 400-unit `poll64` timeout; the caller may raise `NWOAS_IDLE_ATTEMPTS`
if a real ReadyToBoot integration shows it is needed. The delay callback lets the
integrator supply the real firmware clock (e.g. `MicroSecondDelay`); the tests
supply a counting stub, so wall-clock behaviour is the integrator's to validate
on hardware.

## ReadyToBoot integration (later work, not done here)

Intended shape of a future EDK2 caller (kept out of this tree so the helper stays
inert):

1. Register an `EFI_EVENT` on `gEfiEventReadyToBootGuid` (or the platform's
   equivalent late hook), so it runs after the inner payload's cpufreq init.
2. In the callback, build a `NWOAS_CPU_PSTATE_IO`:
   * `read64`  -> `MmioRead64`
   * `write64` -> `MmioWrite64`
   * `delay_us(ctx, us)` -> `MicroSecondDelay(us)`
   * `chip_id` -> the detected SoC id (must be `0x8103`).
3. Call `nwoas_cpu_pstate_enable_p12(&io, &result)` once, log the snapshot, and
   take no automatic retry. A failure should be surfaced, not silently booted
   over — mirroring the Python module's abort-rather-than-boot-unverified rule.

The header is plain C on top of `<Base.h>` only, using `UINT64`/`UINT32`/
`BOOLEAN`/`TRUE`/`FALSE` (the same style as `../memory-s161/Include/Library/
NwoasGuestRam.h`). Firmware modules include the real MdePkg `<Base.h>`; the host
tests compile the identical header against the stub `<Base.h>` in `tests/stub`.
No `<stdint.h>` is pulled in directly and there is no opt-out macro.

## Files

* `Include/Library/NwoasCpuPstate.h` — the header-only helper (the deliverable).
* `tests/NwoasCpuPstateHostStub.h` — host-only fake T8103 P-cluster.
* `tests/test_nwoas_cpu_pstate.c` — host tests that run the real helper.
* `tests/stub/Base.h` — minimal `<Base.h>` subset for the native build.
* `build.sh` — compile + run the tests (plain `-Werror` and, when available,
  ASan/UBSan).

## Building and testing

```sh
sh build.sh
```

Compiles the real header via `-IInclude` (the firmware `<Library/...>` path) and
`-Itests/stub` (the stub `<Base.h>`), under
`-Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion`, then runs the
suite twice — plain `-O2` and, if the toolchain supports it,
`-fsanitize=address,undefined -fno-sanitize-recover=all`.

Cases: success; initially-busy baseline; request-stuck-busy where the rollback
skips the P7 write and reports `RollbackBusy`; rollback P7 written-then-busy
reporting `RollbackUnverified` (distinct from the skipped case); idle
wrong-readback + successful rollback; rollback whose command matches P7 but whose
STATUS never reaches 7/7 reporting `RollbackFailed`; preservation of
unrelated/protected feature bits; failure status reporting (non-P7 baseline and
non-T8103 chip); NULL/incomplete `Io` reporting `InvalidIo`; a STATUS-changes-
between-reads case proving single-capture validation; and a STATUS-mismatch
rollback.

**A green test run means the code compiles and its branches behave as designed.
It is not proof of a correct or stable boot on hardware.** The 1.53x P-core
figure comes from the S164 live post-boot measurement; this helper only requests
the same operating point earlier and more permanently.

## Main review clarification

Header prose saying the setting survives permanently describes intent, not evidence.
Only the earlier postboot Python path is hardware-validated so far. Firmware
access, post-boot persistence, and sustained operation still require the S170
hardware control trial. The build leaves the original S139 RAM layout/cap intact.
