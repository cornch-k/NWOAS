# 2026-09-10 S216 source companion

S215 passed its isolated original-FD trial. S216 passed two Windows boots,
8-core/P12 integration, 256MiB full integrity, a 10GiB three-pass memory test,
five-minute active reads and a thirty-minute mixed soak. See the individual
result files. Cinebench is a separate result, and these bounded checks are
not a production stability or standalone-driver claim.

The manifest identifies exact base commits and each patch hash. Apply each
patch to its named repository at that base commit. `m1n1_guest.patch` belongs
to a separate guest build checkout; do not combine it with the host/HV patch.

- `m1n1_windows.patch`: S163 behavior plus S208 diagnostic correction and
  strict-init launcher support. Explicit build flags are mandatory:
  `EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50'`.
  Gap25 is a distinct candidate/configuration. Guest prefix patch is separate.
- `m1n1_guest.patch`: S197 compatibility behavior and S215 fixed-handoff
  placement. Build tag `v1.0.2-1474-gbddf7f06-s215handoff`. The old HPM-index
  behavior is retained for compatibility; this does not claim native USB-C
  bring-up. The S215 recipe validates vendored sources and toolchain versions.
- Project Mu and its submodule patches: S216 handoff guard/reservation,
  source Image-size correction, native RTC/P12 and the existing 8-core /
  expanded-RAM / storage platform changes.

`audit_companion.py` reconstructs patched sources from clean base blobs and
compares them with the actual build snapshots. The export preserves the
executable bit on `run_guest.py`. The older S198 patch is retained unchanged
because the S205 exact rebuild recipe pins its hash; this fixes export mode
metadata without breaking historical reproduction.

This source bundle still requires host EL2 orchestration/runtime services.
It is not a complete standalone firmware or a Windows hardware-driver bundle.

## Reproduction scope and required inputs

This directory is a patch export, not a self-contained build workspace. The
same public repository ships the recipes under `NWOAS/nwoas_scripts/loader-s215/`
and `firmware-s216/`, and the exact runtime invocation in
`firmware-s216-handoff-guest-test.sh`. Scripts currently use the documented
`/Volumes/X31/NWOAS` workspace layout. Tool versions, component hashes and
separate gap25/gap50 flags are now in the manifest. `NwoasGuestRam.h` is
included by `apple_silicon_platforms_mu.patch`; S161 is its historical source.

The unchanged static64KiB J274 FDT now has recovered DTS and a DTC1.8.1
build recipe in `NWOAS/nwoas_scripts/dtb-s221/`. It reproduces every byte of
the existing input; assembly reproduces the same S216 payload. The original
upstream source revision remains unknown, and this is explicitly recorded
with the recovered third-party source. See S221 NOTICE and comparison results.
No machine-generated ADT or NVRAM is published. These recipes still assume
the separate pinned source checkouts/submodules and the documented local
workspace layout; the companion directory alone is not a full workspace.

## S223 packaging candidate

S223 appends480KiB of explicit0xff FD tail without changing any S216 byte,
so the inline loader's declared copy span stays within the supplied payload.
Its recipe and separate status are in `NWOAS/nwoas_scripts/loader-s223/`.
The exported C/UEFI patches above describe S216; S223 adds packaging only.
Cinebench1888.454 belongs to S216, not to S223. Integrity-soak timings are not
isolated performance measurements; the S216 mixed test overlapped a short
low-priority host build. See the result files for each qualification.
