# Review 2026-11-15 (gpu-hv1)

## Report

gpu-hv1 (GPU HV port step 1): DONE as far as building goes. Nothing has run on hardware. Nothing touched the Mac mini, nothing was committed, nothing was installed. A successful build does not mean the GPU or the boot works.

## Result in one line
All 6 review defects are fixed in a new copy tree, /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-gpu-hv1. m1n1-gpu-s240 was left untouched (verified against its pre-edit manifest). The GPU code builds only with `make NWOAS_AGX=1`. One read-only ADT probe is called from hv_init only with `NWOAS_AGX_PROBE=1` (default off). With the probe off, the binary matches S233b byte for byte except the 8 bytes of the version tag.

## Files created
- /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-gpu-hv1/ : rsync copy of m1n1-gpu-s240 plus the edits below
- m1n1-gpu-hv1/src/nwoas_agx.c (new probe), m1n1-gpu-hv1/THIRD_PARTY_NOTICES.md, m1n1-gpu-hv1/3rdparty_licenses/LICENSE.MIT.windows-on-m1
- /Users/angigyeom/Desktop/DEV/NWOAS/port-j274/gpu/hv/README.md (Korean summary on top, then the details and the step-2 list)
- port-j274/gpu/hv/BASE-MANIFEST-s240.sha256 (599 files, taken before any edit), port-j274/gpu/hv/MANIFEST-hv1.sha256
- port-j274/gpu/hv/out/ : the two .bin files, build logs, bindiff/masked/tail diffs, src-s240-vs-hv1.diff, adt-offline-current.txt, adt-carveouts-current.txt
- port-j274/gpu/hv/tools/ : buildvar.sh, maskanon.py, tailcheck.py, adt_probe_offline.py, adt_carveouts.py

## Files changed (all inside m1n1-gpu-hv1)
- Makefile:179-199: NWOAS_AGX block. It adds nwoas_agx.o and 10 pure-logic hv_agx objects, plus `-Isrc/agx_shared/include`. NWOAS_AGX_PROBE=1 without NWOAS_AGX=1 stops with $(error).
- src/hv.c:17-25 and 150-152: guarded include, and a `nwoas_agx_probe()` call at the end of hv_init that exists only when NWOAS_AGX_PROBE=1.
- src/nwoas_agx.h: rewritten. The hard-coded IRQ list, aperture constants and declarations of functions that never existed are gone.
- Defect 1:
  - hv_agx_g2.generated.h:12-17: J313 GPU_BASE/SIZE and HANDOFF constants removed.
  - agx_shared/include/j313_agx_g2.generated.h:86-93: GPU/SHARED/HANDOFF constants removed.
  - hv_agx_config_snapshot.c:60: gpu_region_base must now equal the ADT-derived nwoas_agx_gpu_base, and is refused while that is 0.
- hv_agx_retained_backing.h:3: now includes nwoas_agx_contract.h.
- nwoas_agx_contract.h: adds stdbool and fixes stale references.
- hv_agx_retained_platform.c:
  - an `#error` guard at :12-14; it is not compiled.
  - the J313 constant is replaced by the runtime nwoas_agx_gpu_base/_size.
  - the board 0x26 gate at :469 is left for step 2.
- Not compiled: hv_agx_power_platform.c (the pmgr power calls) and nwoas_agx_mmio.c.orig_ref.

## What the probe does
FACT: src/nwoas_agx.c:234-368.
- It reads only the ADT:
  - /arm-io/sgx: reg, interrupts, perf properties, and gpu-region / gfx-handoff / gfx-shared-region base and size, with alignment and inside-DRAM checks
  - /arm-io/gfx-asc: reg and interrupts
  - /chosen/carveout-memory-map: entries overlapping the three GPU ranges, the largest entry, and the entry count
- Every line it prints starts with `NWOAS-AGX:`.
- It does not power anything, map anything, read MMIO, or access the gpu-region RAM.
- Its gates are T8103, board-id 0x22, and present, aligned, non-overlapping ranges. If any gate fails it logs `REFUSED:` and all its globals stay 0.

## Build results
Same command as nwoas_scripts/usb-s233/README.md (b). Each variant was built in a fresh scratch copy. Tags keep the original length.

| Variant | Size (bytes) | sha256 |
|---|---|---|
| S233b rebuild | 2162688 | d2eb62b53310dbd6c5b481a37bdbbf39aea5826f2bfb8d4a9c740cba3c529316 (= shipped) |
| g240N (no NWOAS_AGX) | 2162688 | 8393b26ec81540ecf7644f88b589a5001c9bcfcacc9d1a0c89608443df569913 (not kept) |
| g240O (NWOAS_AGX=1, probe off) out/m1n1-g240O-agx-probeoff.bin | 2162688 | 54c109da6dcc71c22255bfd4607011f5a493a47411d611d670b5c91050c5a78b |
| g240P (probe on) out/m1n1-g240P-agx-probe.bin | 2179072 | 337ed2ed5d1690acfc2ff59e6a44581168269e3e817092197f171f7383f50325 (built twice, identical) |

- Warnings: 43 compiler warnings, the same set as S233b, plus 2 Makefile warnings whose line numbers moved. The new code adds no warnings.
- O vs S233b: 8 bytes differ, all in the version tag. 0 functions differ, .data and relocations are identical. With --gc-sections the linker drops all the AGX code, so it is dead.
- P vs S233b:
  - Only one existing function really changes: hv_init gains one `bl nwoas_agx_probe` (0x190 -> 0x194).
  - The other 224 functions that differ do so only because data moved.
  - 6 new functions: the probe, 2 of its helpers, and 3 Rust ADT exports that are now linked (adt_first_property_offset, adt_get_property_count, adt_getprop_by_offset).
  - New probe strings, and 12 new relocations.
  - The image is 16 KiB larger.
- Guard checks: the bad flag combinations are refused at both make and compile time, and hv_agx_retained_platform.c fails on its `#error` as intended.

## Offline ADT check
FACT for the saved dump experiments/tahoe-afk-20260906/current.adt (J274, board 0x22, system firmware mBoot-18000.121.3):

| Field | Value |
|---|---|
| sgx interrupts | 563, 564, 565, 566, 579 |
| gfx-asc interrupts | 576, 575, 578, 577 (so the J313 9-IRQ set = sgx 5 + gfx-asc 4) |
| gpu-region | 0xbfffb8000 + 0x4000 (carveout region-id-8) |
| gfx-handoff | 0xbfff70000 + 0x4000 (region-id-56) |
| gfx-shared-region | 0xbfff78000 + 0x40000 (region-id-52) |
| Largest carveout | region-id-24 = 0x801504000 + 0x3dfafc000 |
| sgx reg[0] | 0x204000000 + 0x4000000 (equals the J313 aperture) |
| gpu-perf-base-pstate | MISSING, so upstream's config snapshot would refuse (fail closed) |

HYPOTHESIS: the 13.5 stub boot gives the same values. Only gpu-region and handoff are confirmed from the 13.5 log.

## Hardware note for the main session
- Every existing launcher pins the HV size 2162688 and its hash (e.g. usb-s233b-guest-test.sh:30-31). Running P needs a new launcher copy.
- HYPOTHESIS to check: the +16 KiB image size is harmless for payload and heap placement.
- Expected output and the pass criteria are in README section 6.

## Installs
None. The bindiff tools ran with the existing m1n1_windows/.venv-hv python -I. Scratch build copies (about 1.2 GB) were deleted.

## Open questions and step 2
README section 7 is the full step-2 list. The open questions are:
- Does the 13.5 stub ADT match the saved one?
- Is the 16 KiB larger image safe on hardware?
- J313-only parts still to port: board gate 0x26, perf constants, launch contract, power code.
- A source for the guest RAM description, and a J274 IPA for the power broker.
- The driver side's IRQ 889 and HVC 0x4d31/0x4d32 are not in the copied HV files.
- The upstream revisions 882985fb / 6c656732 were not re-verified with git, because the clone is treated as untrusted.

## Independent review

gpu-hv1 critical review. VERDICT: the output is safe and inert as delivered. The build claims reproduce exactly. I found no rule violations and no way this can harm the Mac mini or macOS partitions. There are 4 minor defects (documentation, gating and tooling), and none of them blocks step 1. Build success is not boot success: nothing here was run on hardware.

VERIFIED (FACT, re-run by me, offline only)
- m1n1-gpu-s240 matches BASE-MANIFEST-s240.sha256 (599 files, all OK), and m1n1-gpu-hv1 matches MANIFEST-hv1.sha256. `diff -rq` s240 vs hv1 shows only the files the report lists.
- I rebuilt each variant with tools/buildvar.sh in a fresh scratch copy:

| Variant | rc | warning: lines | sha256 |
|---|---|---|---|
| P (AGX=1, PROBE=1) | 0 | 45 | 337ed2ed...0f325 (= out/) |
| O (AGX=1) | 0 | 45 | 54c109da...0a78b (= out/) |
| N (no flags) | 0 | 45 | 8393b26e...69913 (= report) |

- `cmp` of O vs the shipped S233b finds 8 differing bytes. `make NWOAS_AGX_PROBE=1` without AGX stops at Makefile:197 as claimed.
- In P's ELF, nwoas_agx_probe, agx_print_regs and agx_read_region are linked, plus the BSS globals. probe_ok and refusal are dropped by the linker.
- Probe code (src/nwoas_agx.c:234-368) only calls adt_* functions, memcpy and printf. There is no pmgr call, MMIO, hook, map, or read64/write64. It is called only under NWOAS_AGX_PROBE (src/hv.c:150-152), and hv_init is reached only via proxy P_HV_INIT (src/proxy.c:476).
- I reproduced out/adt-offline-current.txt and out/adt-carveouts-current.txt byte-for-byte using the trusted m1n1-gpu-hv1/proxyclient. A raw grep of current.adt confirms "gpu-perf-base-pstate" is absent (0 hits).
- Existing launchers pin size and hash, so P cannot be run by accident. No new launcher was created.
- The hv_agx_retained_platform.c code that writes the gpu-region RAM (:117-127, :356, :432) is behind an `#error` (:12-14) and is not in AGX_OBJECTS.

DEFECTS
1. m1n1-gpu-hv1/src/nwoas_agx.c:158-163 and :326-353: the "inside DRAM" result is only printed, never gated. The carveout containment check (:173-213) is also info-only. The report says "alignment and inside-DRAM checks", but a gpu-region outside DRAM, or outside its carveout, still sets probe_ok and nwoas_agx_gpu_base. Step 2 will hook and write that address. Fix: add `in_dram` to the regions_ok gate, and require each range to be fully contained in one region-id-* carveout before the globals are set.
2. port-j274/gpu/hv/README.md section 4 (build command) has a reproducibility hazard. The Makefile does not track CFLAGS (Makefile:246 only has -MMD header deps). Switching NWOAS_AGX or NWOAS_AGX_PROBE in a tree that already has build/ leaves a stale hv.o. An "O" built that way would still call the probe. The hash pins would catch it, but the README does not warn. Fix: state "fresh copy or empty build/ only (as tools/buildvar.sh does)".
3. port-j274/gpu/hv/tools/buildvar.sh:3,13-14: because of `set -e`, a failing make exits before the `rc=` line, so `rc=$?` can only ever print 0. Fix: drop `set -e` around make, or use `make ... || rc=$?`.
4. README section 5 and tools/adt_*.py do not record the exact command or the proxyclient path (argv[1]). The tools sys.path-insert whatever directory is given, so a reader cannot tell whether trusted code was imported. `python -I` also does not stop __pycache__ from being written into that tree: my own re-run created m1n1-gpu-hv1/proxyclient/m1n1/__pycache__, which I deleted. Fix: record the command with `m1n1-gpu-hv1/proxyclient` and add `-B`.

NOTES (no fix needed in step 1)
- HYPOTHESIS, labelled correctly in the report: the +16 KiB image size is harmless for payload and heap placement. Not verifiable offline.
- README section 6 says a boot difference "would mean the size change matters, not the probe". A Rust unwrap panic in adt_first_property_offset (rust/src/adt.rs:593-602) would also change the boot, and section 6 mentions this possibility only later. In the worst case m1n1 hangs before the guest starts. The probe writes nothing to disk or NVMe, so macOS partitions are not at risk.
- For step 2: hv_agx_retained_platform.c:554-559 underflows `nwoas_agx_gpu_size - bytes` when the size is 0. Gate every use on nwoas_agx_probe_ok(), as its header comment says.

Rule compliance: no /dev/cu.* access, no launcher, proxy or chainload run, no git, no installs, no rm -rf, and the untrusted clone is not executed. The m1n1_windows/.venv-hv interpreter is the same one the S233 workflow already uses. My scratch builds (271 MB) were deleted with find -delete.

Files: /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-gpu-hv1/src/nwoas_agx.c, /Users/angigyeom/Desktop/DEV/NWOAS/port-j274/gpu/hv/README.md, /Users/angigyeom/Desktop/DEV/NWOAS/port-j274/gpu/hv/tools/buildvar.sh
