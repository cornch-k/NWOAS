# S237 — UEFI reserves the PCIe bridge window 0xC0000000..0xC00FFFFF (USB-A root fix, firmware form)

Status: **BUILT, NOT BOOTED.** Build success is not boot success. Nothing here has run on the Mac mini.

| Item | Value |
|---|---|
| Payload | `out/m1n1-payload-s237-bridgeresv.bin` |
| Payload sha256 | `a1f7cff837d35c380738302f92c586ae55e91635e95f05c72e6fd693985e8466` |
| Payload bytes | 33669120 (same as S223) |
| FD sha256 (`out/s237/fd-s237.fd`) | `ac78bd9b81e4c0a67b1445d2e0240209bd5ae6ff0e18f45a3af7ebfdcbd635db` |
| Launcher | `../fw-s237-guest-test.sh` (copy of `usb-s233b-guest-test.sh`, S233b HV unchanged) |
| Source change | `s237-bridge-window.patch` (MemoryInitPeiLib.c + .inf only), full files in `src/` |

## 1. What changed (FACT)

The only source difference from the S216 source of record (`firmware-s216/source-candidate/`) is in
`Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c` (MemoryPeim): directly after the
low DMA window `BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY, ..., NWOAS_DMA_WIN_BASE, NWOAS_DMA_WIN_SIZE)`
it calls the existing S216 helper `ReserveMemoryRegion (PcdPciMmio32Base, SIZE_1MB)`, which splits the low-window
system-memory resource HOB and adds an `EFI_RESOURCE_MEMORY_RESERVED` HOB for [0xC0000000, 0xC0100000), and logs
`HVLOG: S237 reserved PCIe bridge window base=c0000000 bytes=100000` (or a `could NOT reserve` line; boot continues
in that case = S223 behaviour). The .inf adds `gArmTokenSpaceGuid.PcdPciMmio32Base` to `[FixedPcd]`
(T810XFamilyPkg.dsc.inc:85 sets it to 0xc0000000).

Placement choice:
- `ReserveMemoryRegion` instead of the proposal's `BuildMemoryAllocationHob (..., EfiReservedMemoryType)`: same
  mechanism S216 already uses for the BootArgs/ADT handoff copies, which on hardware showed up as a reserved region
  in the UEFI map (firmware-s216/README.md). It removes the range at resource level, so no DXE allocation
  (including AllocateAddress) and no OS can use it.
- Not in NwoasHideHighRamDxe: that DXE only relabels the GetMemoryMap snapshot at ReadyToBoot; UEFI and the
  Windows loader could still allocate those pages earlier/later from the live map.
- Not derived from the live root-port window: MemoryPeim runs in PEI, before PciBusDxe programs BARs/bridge
  windows; a DXE-time reservation after enumeration cannot guarantee the pages are still free. The base therefore
  follows the PCI aperture PCD (PciBusDxe assigns from its bottom; S235W measured root port 1 = 0xC0000000..0xC00FFFFF
  with FL1100 BAR0 = 0xC0000000), the size is the 1 MiB PCI-PCI bridge window granularity. The code comment states
  that the size must grow if more non-prefetchable BARs are ever placed behind/above it.

Disassembly of the built S237 PrePi (`out/s237-prepi-disasm-excerpt.txt`): `bl ReserveMemoryRegion` with
w0 = 0xC0000000, w1 = 0x100000 directly after the low-window HOB call (the range checks fold to constants).
The new HVLOG string is present in the S237 FD and absent in the reproduction FD (grep count 1 / 0).

## 2. Reproduction of S216/S223 (FACT, with the precise residual)

`build.py --variant repro` re-executes the S216 edit sequence (build_candidate -> build_memory -> patch_sources ->
s131 edits) on a fresh APFS clone of `apple_silicon_platforms_mu`, asserting every resulting file equals the S216
recorded bytes, builds DEBUG/CLANGPDB, then assembles S215 prefix + S221 DTB + FD + 0x78000 x 0xff (S223).

| Build | Workspace path seen by tools | FD sha256 | Payload sha256 | Leaves equal to S216 after RSDS mask |
|---|---|---|---|---|
| S216 (Sep 10, reference) | /Volumes/X31/NWOAS/apple_silicon_platforms_mu (real) | b008dcfc...37cc | 2343f7fc... (S223) | 241 / 241 |
| repro, logical X31 path | /Volumes/X31/NWOAS/apple_silicon_platforms_mu (symlink) | de92c0ab...e8ef | 326ca1cf...2507 | 241 / 241 |
| repro, native path | /Users/.../fw-s237-mu-native3 | e523dcc1...2b89 | 68a8ffc9...a502 | 162 / 241 (79 differ: embedded paths) |
| S237 | logical X31 path | ac78bd9b...35db | a1f7cff8...8466 | 240 / 241 (PrePi changed) |
| S237 repeat, same tree | logical X31 path | ac78bd9b...35db | a1f7cff8...8466 | byte-identical to S237 |

- Not byte-identical to S223. FD/payload length and layout are identical. `maskcmp.py` walks every FV/FFS/section
  (LZMA sections decompressed, nested FVs included, 241 leaf sections): **after zeroing the 8-byte LLD PDB hash in
  each image's CodeView RSDS record ("RSDS" + 8 hash bytes + "LLD PDB."), all 241 leaves and the decompressed DXE
  volume are identical to S216.** 79 images differ only in those 8 bytes; every other differing FD byte is the
  consequence (recompressed LZMA stream of FFS 9e21fd93..., its FFS size/checksum).
- Why the hash differs: the PDB records the linker's real working directory and the lld-link executable path
  (FACT: Sec.pdb contains `/Users/angigyeom/Desktop/DEV/NWOAS/fw-s237-mu-s237/Build/...` and `toolbin/lld-link`).
  S216 linked with `/opt/homebrew/opt/llvm/bin/lld-link` (S216 build.log) in a tree that really lived on X31. Today
  `/opt/homebrew/opt/llvm` is LLVM 23.1.5 without lld-link, so byte identity would require changing the user's
  Homebrew llvm link and copying the 4.2 GB tree physically onto the X31 volume; neither was done. Same real tree +
  same logical path repeats bit-exactly (S237 repeat row).
- Native-path builds embed the different path in the RSDS record itself (79 leaves still differ after mask), so the
  logical `/Volumes/X31/NWOAS` path is required to keep the image contents identical.

## 3. Diff report S237 vs S223 (`out/diff-report.json`)

| Payload region | Range | Identical | Differing bytes |
|---|---|---|---|
| guest m1n1 prefix (S215) | 0x0..0x20c000 | yes | 0 |
| padded J274 DTB (S221) | 0x20c000..0x21c000 | yes | 0 |
| Project Mu FD | 0x21c000..0x1fa4000 | no | 1,094,460 (first 0x225178, last 0x34a3f2) |
| 0xff tail (S223) | 0x1fa4000..0x201c000 | yes | 0 |

Module level (repro -> S237, and S216 -> S237): only `FV@0x8000/{fa8e7979-a845-4378-b7f8-ceede175b1e9}/TE`
= PrePi (Sec, FILE_GUID in PrePi.inf) changes after the RSDS mask: 57000 bytes both, 106 differing ranges (new
code/string, shifted code). Every other image differs only in the RSDS hash; the large FD byte count is the
recompressed DXE FV (offsets 0x287ef..0x12e3f3 within the FD), not DXE code changes.

## 4. Build steps (exact)

Tools: LLVM 20.1.8 (`/opt/homebrew/opt/llvm@20`), LLD 20.1.8 (`/opt/homebrew/opt/lld@20`) via `toolbin/`
symlinks (CLANG_BIN), iasl 20260408 (Homebrew acpica; S216 log shows the same version), rustc/cargo 1.88.0
(reported only), Python 3.11.18 venv `fw-s237-mu/venv` (`venv-freeze.txt`; edk2 pins from pip-requirements.txt,
other pins matched to the S216 build.log).

```
cd /Users/angigyeom/Desktop/DEV/NWOAS
# fresh APFS clone of the source of record (venv/Build excluded)
mkdir fw-s237-mu-s237; (cd fw-s237-mu && for e in .git* *; do [ "$e" = venv ] && continue; cp -Rc "$e" ../fw-s237-mu-s237/; done)
ln -sfn $PWD/fw-s237-mu-s237 nwoas_scripts/fw-s237/x31root/apple_silicon_platforms_mu
ln -s $PWD/nwoas_scripts/fw-s237/x31root /Volumes/X31/NWOAS          # temporary; removed after the build
python3 nwoas_scripts/fw-s237/build.py --tree fw-s237-mu-s237 --variant s237 \
    --logical-root /Volumes/X31/NWOAS --out nwoas_scripts/fw-s237/out/s237
rm /Volumes/X31/NWOAS
python3 -I nwoas_scripts/fw-s237/diff_report.py
```
`--variant repro` gives the reproduction; `--rebuild-only` repeats a build in an already-edited tree whose
`Build/` was moved away. The `/Volumes/X31/NWOAS` symlink existed only during builds and has been removed.

## 5. Hardware validation plan (main session; not run)

1. Boot the current setup (S233b + S223, badmemorylist active), queue `../usb-s236/B2-revert-badmemory.cmd`
   (removes `badmemorylist`, effective next boot). Shut down / let the launcher exit.
2. Boot S237: `fw-s237-guest-test.sh`. In the HV/VUART log expect
   `HVLOG: S237 reserved PCIe bridge window base=c0000000 bytes=100000` and the usual
   `NWOAS: low DMA window ...` / `HVLOG: S216 reserved handoff ...` lines. A `could NOT reserve` line = stop.
3. Memory map check: `M1-memmap.cmd` (job; `M1-memmap.ps1` is the readable source, `make_m1.py` regenerates
   the .cmd) prints every physical-memory run Windows uses (registry `.Translated`) and
   `S237 BRIDGE-WINDOW 0xC0000000-0xC00FFFFF EXCLUDED` or `INCLUDED`. It also prints `bcdedit /enum {badmemory}`,
   which must show no badmemorylist. The parser has NOT been run on Windows (no PowerShell here).
4. `validate-s237.sh fw-s237-guest-test.sh s237` (copy of usb-s236/validate.sh; results in
   `fw-s237/results-s237.txt`; runs M1 before every U3): 3 normal U3 cycles + 3 under truncatememory 4 GiB,
   then revert truncatememory, final B0 + M1. Pass = all files good in 6/6 cycles, M1 EXCLUDED every boot,
   no badmemorylist present. Before the fix the same test failed 9/9 (normal) and 3/3 (truncatememory).
5. Decide on belt-and-braces: if 6/6 pass without the BCD entry, the firmware reservation alone carries the fix
   (it also covers WinPE / reinstall). Re-adding `B1-set-badmemory.cmd` afterwards is optional; HYPOTHESIS: marking
   already-reserved pages bad is a no-op, but it would again hide whether the firmware path works.

## 6. Open points

- HYPOTHESIS: Windows treats the EFI_RESOURCE_MEMORY_RESERVED range exactly like the BCD badmemory pages for this
  bug (it never uses EfiReservedMemoryType). Only the hardware U3 cycles can confirm it.
- If Wi-Fi/Ethernet drivers ever get BARs in the 32-bit aperture, their bridge windows land above 0xC0100000
  and are NOT covered (handoff section 10). The constant comment in the source says so.
- Byte identity with S223 is not achieved (section 2); code/data identity is.

## 7. Hardware validation result (2026-11-15, Mac mini M1, installed Windows 11 25H2)

Run with `validate-s237b.sh fw-s237-guest-test.sh s237` (= validate-s237.sh without the M1 memory-map job:
`M1-memmap.cmd` is 4771 bytes, above the 4032-byte mailbox limit, so it was never queued). BCD badmemorylist
was removed before the run, so only the firmware reservation was active.

| Cycle | Time | Files with correct SHA256 | Result |
|---|---|---|---|
| normal 1 | 22:13:03 | 3/3 | FAIL=0 |
| normal 2 | 22:15:05 | 3/3 | FAIL=0 |
| normal 3 | 22:17:07 | 3/3 | FAIL=0 |
| truncatememory 4 GiB 1 | 22:19:16 | 3/3 | FAIL=0 |
| truncatememory 4 GiB 2 | 22:21:18 | 3/3 | FAIL=0 |
| truncatememory 4 GiB 3 | 22:23:22 | 3/3 | FAIL=0 |

Before the fix the same test failed 9/9 (normal) and 3/3 (truncatememory). truncatememory was reverted
(T2) and the final B0 state job completed. Full log: `results-s237.txt`, `validate-s237b.log`.

Not checked: the M1 memory-map job (does Windows list 0xC0000000-0xC00FFFFF as excluded?) - needs to be split
into jobs of at most 4032 bytes. Belt-and-braces: `../usb-s236/B1-set-badmemory.cmd` was re-applied afterwards
(SETRC=0) so an older payload (S223) stays safe too.
