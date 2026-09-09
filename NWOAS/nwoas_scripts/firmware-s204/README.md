# S204: isolated UEFI Image-header (`image_size`) correction candidate

> Later hardware result: the S204 payload failed before Windows when its FD
> overlapped the fixed BootArgs/ADT copies. S211 reproduced the collision with
> the old header, so header size alone is not the cause. See `hardware-result.json`
> and the S215/S216 placement and reservation corrections. The original build
> report below is retained as history.

Executes the "concrete future candidate plan" recorded in
`nwoas_scripts/loader-s199/README.md` section 2: correct the FD Linux-Image
header `image_size` from the mis-encoded `0x100E0000` (256.875 MiB) to the intended
`0x01E00000` (30 MiB == `PcdFdSize`), by rebuilding the FD **from source**, not
by patching a binary.

Offline source work only. Nothing was booted; no hardware, launcher, USB,
guest/m1n1 main source, secrets or UI was touched, and no ADT/NVRAM dump was
read. `apple_silicon_platforms_mu` edits were TEMPORARY and are restored to
their exact original tracked bytes (proof below). S192 and loader-s197 artifacts
keep their hashes. This is **build-only**; it makes no boot, native-driver, or
standalone-boot completion claim.

## What changed vs S192

One source line, in `Platform/MacMini2020Pkg/MacMini2020.fdf` only
(`image_size.patch`):

```
-  0x00, 0x00, 0x0e, 0x10, 0x00, 0x00, 0x00, 0x00, # image_size: 30 MB
+  0x00, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x00, 0x00, # image_size: 30 MB (0x01E00000 == PcdFdSize)
```

Every other input is byte-identical to the S192 build (same RTC lib, same six
S192 source edits, same S161 memory diff, same headers, same s131 eight-core /
NVMe / SSD-first set). No other platform file was changed.

## Reproducible recipe

1. `python3 nwoas_scripts/firmware-s204/build_candidate.py`
   - Derived from `firmware-s192/build_candidate.py` and `build_memory.py`.
   - Swaps the RTC lib in the DSC, applies the full S192 source set, applies the
     one FDF `image_size` correction, then runs the generated `s131_builder.py`
     (`nice -n 19`, `stuart_build ... MAX_CONCURRENT_THREAD_NUMBER=2`, existing
     Mu `venv`/toolchain).
   - All generated/output paths retarget to `firmware-s204/`; S192 outputs are
     never written.
   - FDF and every temporarily edited tracked file are restored in `finally`,
     even on build failure; a concurrent writer is refused, not clobbered.
   - Writes `manifest.json`, `restore-proof.json`, the payload
     `m1n1-payload-s204-native-rtc-p12-30mib-hdr.bin`.
2. `python3 nwoas_scripts/firmware-s204/compare_fd.py`
   - Slices the FD at offset 1376256 from the S204 payload and from the pinned
     S192 payload (`b67313c4…c7b9`) and diffs them.
3. `python3 nwoas_scripts/firmware-s204/assemble_candidate.py`
   - loader-s197 compat recipe with the S204 FD substituted for the S192 FD.

## Results

| Item | Value |
|---|---|
| S204 payload (FD carrier) | 32,342,016 B, `97808b29…25c4a` |
| S204 FD (payload[1376256:]) | 30,965,760 B, `e3407c36…40cb57` |
| S192 FD (reference) | 30,965,760 B, `ae917fef…c3382b` |
| FD diff vs S192 | exactly 2 bytes, 0-based offsets **18 and 19** (`0e 10` -> `e0 01`) |
| `image_size` after | `0x01E00000` == `PcdFdSize`, and >= FD length (`0x1D88000`) |
| Candidate | `m1n1-payload-s204-imghdr-compat.bin`, 33,177,600 B, `548fd6b0…1bd783` |

The FD comparison is exact: the only difference from the S192 FD is the two
`image_size` header bytes. GenFds copies the FDF `DATA = {}` block verbatim, so
this is the expected result; nothing else moved.

Candidate layout (loader-s197 compat prefix, unchanged and read-only):

| Component | Offset | Size | sha256 |
|---|---|---|---|
| m1n1 (s197 compat) | 0 | 2,146,304 | `1c255a7f…162078` |
| DTB (apple-j274-padded) | 0x20C000 | 65,536 | `ecc93b24…190f76` |
| S204 FD | 0x21C000 | 30,965,760 | `e3407c36…40cb57` |

## Restore proof

`restore-proof.json`: `restored: true`, `changed_after_restore: []`, and the
`git status --porcelain` of `apple_silicon_platforms_mu` is byte-identical
before and after. The only entries in that status are the pre-existing
submodule modifications (`MU_BASECORE`, `Silicon/ARM/TIANO`) and untracked files
(`DSDT.hex`, `MADT_Static.aslc.bak-8cpu`, `venv/`); all pre-existing edits and
submodules are preserved. `MacMini2020.fdf` is back to `7cebb2a3…eabba`, its
original tracked hash. S192 payload stays `b67313c4…c7b9`; loader-s197
`out/compat/m1n1.bin` stays `1c255a7f…162078` and its assembled payload stays
`9a0b96af…35020a`.

## Heap-position consequence (why this is build-only)

m1n1 `load_kernel()` (`src/payload.c`) reserves and `memcpy()`s `image_size`
bytes for a non-2 MiB-aligned inline FD, and `heapblock.c` advances `heap_base`
by that amount. S192/S197 used `0x100E0000` (256.875 MiB reserved); this candidate
uses `0x01E00000` (30 MiB). So every later m1n1 allocation
(`kboot_prepare_dt` buffers, malloc arena) sits `0xE2E0000` (~226.875 MiB) lower.

On the UEFI side this is safe by construction: the image is position-independent
and relocated in place, its scratch memory (stacks/HOBs) is placed at or below
`FdBase`, and the only real footprint rule is `image_size >= PcdFdSize`, which
`0x01E00000` satisfies exactly (and it also stays >= the FD file length). But
the m1n1 heap shift is a real layout change with no offline proof of neutrality.

It must be confirmed by a **main-run** hardware A/B (S197-compat vs this
candidate): equal boot-stage parity on UART, the m1n1 `Heap base:` line and the
PrePi `FD Base Address` line showing the FD at the same place, and the DT buffer
`0xE2E0000` lower. Only after that passes should any launcher be pointed at this
FD. No byte-patching of an assembled payload is used; if ever cross-checked it
must reproduce this source-built FD bit-for-bit.

## Files

| File | Purpose |
|---|---|
| `build_candidate.py` | Top orchestrator; RTC DSC swap; captures before/after tracked hashes + git status. |
| `build_memory.py` | Source set + the FDF `image_size` fix; retargets outputs to S204; exact FDF/source restore in `finally`. |
| `s131_builder.py` | Generated from `smp-s131/build_candidate.py`, retargeted to S204 (`-j2`). |
| `image_size.patch` | The one-line source patch. |
| `compare_fd.py` / `fd-compare.json` | Step-2 FD diff (exactly offsets 18/19). |
| `assemble_candidate.py` / `candidate-manifest.json` | Step-3 assembly + validation manifest. |
| `manifest.json` | Inner build manifest (payload + FDF + source hashes). |
| `restore-proof.json` | Before/after tracked hashes and git status. |
| `MacMini2020.fdf.before` / `.candidate` | FDF before/after bytes. |
| `NwoasHardwareBootRtcLib/`, `Include/Library/*.h`, `NwoasHideHighRamDxe-s167.c` | Source-only inputs copied into S204. |
