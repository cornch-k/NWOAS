# S216 — source-built payload with protected BootArgs/ADT handoff

First hardware trial passed 8-core Windows boot, native P12, 256MiB full
write/read integrity, and 10GiB all-word memory verification over three passes.
UEFI reported a 384KiB reserved handoff region. A second boot passed integration, 2,309 continuous reads over 300.061 s,
and 31 mixed CPU/read samples over 1,807.61 s, all with exit 0.
The Cinebench repeat is recorded separately; these bounded checks do not
establish production stability.
Build with `RUSTUP_TOOLCHAIN=1.88.0 NWOAS_GUEST_VARIANT=compat python3
nwoas_scripts/firmware-s216/build_candidate.py` from the workspace root.
The outer/inner builders preserve pre-existing source changes and restore
all temporary modifications; `restore-proof.json` records the verification.

Components are pinned in `components-compat.json`: S215 source-built guest
prefix, padded J274 DTB, and source-built Project Mu FD. The assembly consumes
no opaque guest prefix. S221 rebuilds the unchanged DTB from recovered DTS
with DTC 1.8.1; its bytes and the assembled payload match the hardware-tested
S216 artifact. The original DT source revision remains unconfirmed. The baseline remains S192 native RTC/P12, 8 cores,
10GiB high-memory allowance plus the low4GiB alias and visible XHC1.

Changes:

- S215 prevents kernel/FDT heap allocations from intersecting the fixed T810X
  handoff addresses. Source review found those addresses could overwrite the
  running FD in S204/S211 before Windows.
- The ARM64 Image header advertises30MiB, matching PcdFdSize. The former256.875MiB
  value copied/reserved226.875MiB extra; reducing it alone was not sufficient.
- EarlySetup saves the input BootArgs locally, validates the fixed destinations
  against guest RAM and the actual relocated FD, and validates the converted
  ADT source span before reading it, then copies ADT before
  BootArgs. This protects subsequent source reads from destination overlap.
- MemoryPeim explicitly removes the complete aligned handoff interval from
  system-memory HOBs and reserves it before DXE allocation/OS handoff. Failure
  to find a containing system-memory HOB stops instead of pretending success.

Tests compile actual arithmetic and the extracted reservation helper under
ASan+UBSan:1,048,576 ADT sizes, invalid FD overlap, overflow, reservation bounds,
start/end/middle/whole HOB splits and absent/duplicate reservation failures.
No claim that these copies caused an earlier Windows watchdog is made: the
observed failure was a firmware self-overwrite; reserving their lifetime is a
separate correctness requirement. Runtime memory totals may decrease by the
reserved copy size (observed ADT implies384KiB).

This remains EL2-mediated Windows with host runtime services. It is neither
an installed standalone boot path nor a new Windows hardware driver.
