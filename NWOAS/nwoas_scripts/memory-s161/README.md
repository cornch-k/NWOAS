# memory-s161: normalize S102 guest RAM end / low-window backing

Status: OFFLINE BUILT, not deployed. The source tree was restored exactly after
the isolated build. Hardware comparison waits for the NVMe stability result.

## Files

| File | Purpose |
|---|---|
| `Include/Library/NwoasGuestRam.h` | New shared header (header-only, `<Base.h>` only). Destination in tree: `Silicon/Apple/AppleSiliconPkg/Include/Library/NwoasGuestRam.h`. |
| `nwoas-s161-guest-ram-normalize.diff` | Unified diff (`-p1` from the apple_silicon_platforms_mu root) for AdtParser.c, AppleDartIoMmuDxe.c, NwoasHideHighRamDxe.c and T810X MemoryInitPeiLib.c. |
| `test/NwoasGuestRamTest.c` | Native unit test that compiles the real header. |
| `test/stub/Base.h` | Minimal MdePkg Base.h stand-in (AARCH64 widths). |
| `test/Makefile` | Builds and runs the test twice: `-DNWOAS_WINDOW_AFTER_RAM=1` and `=0`. |
| `README.md` | This file. |

## What the patch changes

The header computes one layout from boot_args:

```
GuestRamSize = mem_size            (NWOAS_WINDOW_AFTER_RAM=1, current default)
GuestRamSize = mem_size - 4 GiB    (NWOAS_WINDOW_AFTER_RAM=0, legacy; subtracted once, underflow-checked)
GuestRamEnd  = BackingPa = phys_base + GuestRamSize
BackingEnd   = BackingPa + 4 GiB
Window       = [0, 4 GiB)          (unchanged)
```

Consumers after the patch:

- `PrePi/AdtParser.c` sets PcdSystemMemorySize from `Layout.GuestRamSize`. Mode 1 no longer subtracts 4 GiB. An invalid layout logs, asserts and dead-loops, because ModuleEntryPoint.S ignores the EarlySetup return value.
- `AppleDartIoMmuDxe.c` computes the layout once at DXE init into `mNwoasRam` and `mApcieBackingPa`. SetupApcieDarts uses that value instead of its own unconditional minus-4 GiB recompute, so the WIDE-DART L2 tables and the ExitBootServices identity fill end at the same address. The window macros now alias the header constants with identical values. The PCD fallback for unreadable boot_args is kept.
- `NwoasHideHighRamDxe.c` uses the helper for its log line only. NWOAS_HIDE_BASE, the abort guard and descriptor classification are untouched.
- `T810X MemoryInitPeiLib.c` takes the window constants from the header and prints the backing as `[SystemMemoryTop, +4 GiB)` instead of the stale `[Top-4 GiB, Top)`.

Not changed by the memory diff: Windows RAM cap, PCI leaf and bridge COMMAND
handling at EBS, and the 4 GiB window base and size. The build script also keeps
XHC1 hidden in DSDT, matching the current S139 USB-A-only control payload.

## Expected values (S158 reference, phys_base 0x83CA9C000, mem_size 0x2A4530000)

| Quantity | Mode 1 (default) | Legacy mode 0 |
|---|---|---|
| PcdSystemMemorySize | 0x2A4530000 | 0x1A4530000 |
| Backing (EARLY == DXE == EBS) | 0xAE0FCC000 | 0x9E0FCC000 |
| WIDE-DART L1 range | 1054..1392 | 1054..1264 |

## Running the unit test

```
cd /Volumes/X31/NWOAS/nwoas_scripts/memory-s161/test
make
```

Both binaries must print `PASS`. The test covers the reference vector in both modes, the compile-time entry point, NULL out, phys_base floor, 16 KiB alignment, zero and legacy-underflow mem_size, UINT64 wrap, the 64 GiB ceiling, zeroed output on error, pairwise-disjoint window/guest/backing ranges with boundary probes, window-IPA-to-backing translation, and the exact 4 GiB gap between the two interpretations.

Main-agent validation: 179 member-comparison checks per mode, 358 total, pass.
The original test used struct memcmp, which depended on padding; it was corrected.
The UEFI build passed and all 96 PE/COFF images were validated.
Payload SHA256: c7b725851cdf3bbb1e192fcbcc8edac9642fb5ef1c0ffbe862ff39bd06d29271.

## Caveats for the reviewer

1. **Advertised RAM grows by 4 GiB in mode 1.** PrePi now reports the full mem_size, so UEFI's top-down allocator places its stack, heap and DXE data in the previously unadvertised [0x9E0FCC000, 0xAE0FCC000) region directly below the backing. That region is real guest RAM per the S102 hv contract, and the wide DART now identity-maps it, but it is a behavioural change UEFI has not run with yet.
2. **WIDE-DART table growth.** The reserved L2 block grows from 211 to 339 tables (about 5.3 MiB of runtime-reserved pages, up from 3.3 MiB), and the EBS identity fill writes about 262k more PTEs. Check EBS timing on hardware.
3. **Extra UEFI DMA coverage confirms, does not create, correctness.** The S159 review noted the printed identity end exceeded the mapped end. After this patch they agree. Whether Windows-side high buffers work is still gated on the FL1100 AC64 and HAL behaviour noted in that review.
4. **EARLY log value changes.** The `HVLOG: NWOAS EARLY DART IOVA ... backing PA` line keeps its format but now prints 0xAE0FCC000 in mode 1. No script under nwoas_scripts parses it today, but the old pcie_emul-exp5 breaker did; confirm the active hv script does not re-map its alias from this line, or that it wants the new value.
5. **New fatal path in SEC.** A layout failure now dead-loops with a serial message instead of booting with a wrapped size. Failure conditions: phys_base below 32 GiB, phys_base or mem_size not 16 KiB aligned, mem_size zero, legacy mem_size at or below 4 GiB, UINT64 wrap, backing end above 64 GiB. The alignment check is new strictness; the S158 values pass it.
6. **HideHighRam log semantics.** The old code printed backing 0 when mem_size was at or below 4 GiB even in mode 1; the helper accepts any non-zero aligned mem_size in mode 1. Log-only.
7. **The header is not embedded in the diff.** Copy `Include/Library/NwoasGuestRam.h` into `Silicon/Apple/AppleSiliconPkg/Include/Library/` before applying; the diff alone does not compile.
8. **Line offsets** were computed against the files as read on 2026-09-10 and hunk counts were hand-verified, and the isolated build successfully ran `git apply --check` and applied the patch in this session. If the tree moved, use `git apply --3way` or `patch -p1 --fuzz=2`.
9. **Blank context lines are empty, not a single space.** The authoring tool strips trailing whitespace. `git apply` and GNU `patch` both accept an empty line as blank context; if a stricter tool rejects the file, run `sed -i '' 's/^$/ /'` on the hunk bodies (not the preamble) first.
10. No INF changes are needed: PrePi.inf already links BaseLib (CpuDeadLoop), and all four modules already list AppleSiliconPkg.dec, whose Includes root is `Include`.

## Built candidate review

Opus/Fable review artifacts are under ../claude-s156. The post-build Fable
review confirms that host stage2 mapping, NVMe bounds, and DART L1 capacity
cover the additional region. Before accepting a hardware run, compare host
S102 backing, UEFI early backing, and final DART backing: all must equal
0xAE0FCC000 for the observed boot arguments. WIDE-DART first L1 index is `(phys_base >> 25)`, last index is
`((BackingPa - 1) >> 25)`. The reference boot used L1[1054..1392]
(339 tables); the later phys_base 0x83B7E0000 uses L1[1053..1392]
(340 tables). Derive the range from each boot, never hard-code the count. Preserve the old payload for rollback.
