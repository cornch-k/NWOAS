# S213 review of the S215 loader placement and S216 UEFI guard/reservation

Source-only review of the built candidates. Inputs: `loader-s215/`
(`placement.patch`, `handoff_layout.h`, `payload-s215.c`, `test_layout.c`,
`out/build_tag.h`), `firmware-s216/` (`NwoasHandoffRanges.h`,
`adt-handoff.patch`, `memory-handoff.patch`, `memory-inf-handoff.patch`,
`patch_sources.py`, `source-candidate/…/AdtParser.c`,
`…/MemoryInitPeiLib.{c,inf}`, `tests/`, `test_ranges.c`, `manifest.json`),
the pinned guest tree `m1n1-guest-s197` (`bddf7f06`) and the Mu tree.
Nothing was built, booted or edited outside `loader-s213/`. **No hardware
validation is claimed for anything below.** Scope is actionable defects
supported by exact code; design alternatives are out of scope.

Main verification after review: the final `adt-handoff.patch`,
`patch_sources.py`, and retained source snapshot all include
`NwoasHandoffAdtSource`. The review overlapped source generation; its earlier
claim that the patch lacked that guard is superseded by this check.

## Verdict

**No blocking defect found in either candidate.** Two non-blocking
inconsistencies and one cosmetic item are listed in section 4 with minimal
proposed fixes. The specific concerns raised in the request are answered in
sections 2 and 3.

## 1. S215 loader placement (`payload.c` `load_kernel`, `handoff_layout.h`)

Checked against `heapblock.c`, `smp.c`, `memory.c`, `startup.c`,
`m1n1-raw.ld`, `utils.h`.

- **Forced relocation.** The 2 MiB-alignment branch is removed; the copy is
  unconditional (`payload-s215.c:142-156`). `heapblock_alloc_aligned(0, 2 MiB)`
  only rounds `heap_base` up; `heapblock_alloc(dest - heap)` then lands
  exactly on `dest` because `heap` is already 2 MiB aligned and `heapblock_alloc`
  aligns to 64; the following `heapblock_alloc_aligned(image_size, 2 MiB)`
  returns `dest` (2 MiB aligned by the helper), so `assert(new_addr == dest)`
  cannot fire for a valid layout. Correct.
- **Copy safety.** `memcpy` is byte-wise forward (`string.c:9-19`);
  `dest >= heap >= top_of_kernel_data > p + FD length`, so the FD bytes are
  copied intact before any source byte is overwritten. The `image_size`
  over-read (`0x78000` bytes past the inline FD with the 30 MiB header) stays
  in mapped guest RAM. Correct.
- **Live-prefix disjointness.** The helper rejects any overlap of
  `[NWOAS_BOOTARGS_COPY, end)` with `[_base, _end)`. Per `m1n1-raw.ld` that
  range contains `.bss` (`spin_table[]`, `cur_boot_args`) and the stacks
  (`_stack_bot` precedes `_end`), which are the only guest-side structures
  still live at handoff: parked secondaries spin in `smp_secondary_entry`
  (`smp.c:78-94`) with the MMU **off** on the payload path
  (`mmu_init_secondary` is called only from `hv.c:309` and `proxy.c:310`, never
  from `smp.c`/`payload.c`), and the boot CPU runs `mmu_shutdown()` before
  the jump (`main.c:208`). So the early heap `[top_of_kernel_data, heap)`
  (page tables, dlmalloc arena) is dead at handoff and the helper is right to
  allow the window there (second case in `test_layout.c`). The inline
  DTB/FD/SEPFW region is a dead copy source. No missing range.
- **Integer bounds.** All additions are overflow-guarded (`adt_size >
  max-NWOAS_ADT_COPY`, `end > max-0x3fff`, `base > max-(align-1)`,
  `ram_size > max-ram_base`); `image_size > ram_end - base` is checked after
  `base <= ram_end`; `heap` is range-checked; zero `adt_size`/`image_size`
  rejected; `prefix_end < prefix_base` rejected. No defect.
- **Failure behaviour.** A rejected layout hits `assert()`, which panics
  m1n1 with a UART message before any copy. Acceptable for a guest-only
  build; it is loud, not silent.

## 2. S216 UEFI guard and reservation

Checked against `ModuleEntryPoint.S`, `PrePi.inf`, `MemoryInitPeiLib.c`
(S204 snapshot as baseline), `AppleDTLib.h`, `T810XFamilyPkg.dsc.inc`.

- **SavedBootArgs.** The 0x480-byte struct (`AppleDTLib.h:47-75`, rv3
  cmdline 1024) is copied onto the temp stack before any destination write;
  every later field read (`phys_base`, `mem_size`, `virt_base`, `devtree`,
  `devtree_size`, `video.*`) uses the saved copy. The temp stack is inside the
  FD header stub and has 32 KiB minus current depth available, so 0x480 bytes
  is safe. Correct.
- **ADT before BootArgs.** Both `CopyMem` calls are MdePkg `CopyMem`, which
  is specified to handle overlapping ranges, and the ADT source is consumed
  first. The only way the boot_args destination can alias the ADT source is
  if the window overlaps guest memory below `guest_base`, which S215 rejects
  through the prefix check only indirectly; since the ADT is already copied,
  that aliasing would be harmless anyway. Correct.
- **ADT source conversion.** `NwoasHandoffAdtSource` computes
  `phys_base + (devtree - virt_base)` and rejects `devtree < virt_base`,
  `delta > ram_size` and `adt_size > ram_size - delta`. This matches how the
  host sets `tba.devtree = adt_base - phys_base + virt_base`
  (`hv/__init__.py:2275`), so a valid handoff always passes, and the previous
  unchecked pointer arithmetic on `VOID*` is gone. **Validation is present
  and sufficient**; no further check is needed for correctness. (It does not
  test the source against the destination window, but see the previous
  bullet: that overlap cannot corrupt the copy.)
- **FD guard.** `NwoasHandoffRange` rejects `ba_base < fd_base + fd_size &&
  end > fd_base` using `PcdGet64(PcdFdBaseAddress)`, which
  `ModuleEntryPoint.S:176` has already patched to the runtime base before
  `EarlySetup` is called (line 209). `PcdFdSize` is listed in `PrePi.inf:75`.
  The temp stack lies inside the FD range, so it is covered. Reproduces the
  S204/S211 geometry as a clean halt (`test_ranges.c` case 2). Correct.
- **HOB split reservation.** `ReserveMemoryRegion` now returns TRUE only when
  a single `EFI_RESOURCE_SYSTEM_MEMORY` HOB fully contains the window. At the
  call site (`MemoryInitPeiLib.c:348-364`) the only HOB boundaries that exist
  are those created by the FD carve (`FdBase`, `FdTop`), and the window is
  already known to be disjoint from `[FdBase, FdTop)`, so it lies entirely in
  one of the at most three pieces. The window is also inside
  `[PcdSystemMemoryBase, +PcdSystemMemorySize)` by the same helper. **A
  legitimate split HOB layout therefore cannot make the reservation fail.**
  The low DMA-window HOB and the cpm-impl-reg carveouts are built after this
  block and are unaffected. The tests in `tests/reserve_hob.c` exercise
  start/end/middle/whole and the not-contained cases against the real
  function body (`reserve_actual.inc`). Correct.
- **Permanent reserved type.** `EFI_RESOURCE_MEMORY_RESERVED` with attribute
  0 is the same convention the existing carveout path uses, and it is the
  right lifetime: `AppleDartIoMmuDxe.c:633` reads the boot_args copy from its
  ExitBootServices handler and Windows never reclaims Reserved memory. The
  page tables built by `BuildVirtualMemoryMap` (line 534-537) map the whole
  system-memory PCD range, so the reserved window stays readable in DXE.
  `NwoasHideHighRamDxe` only splits `EfiConventionalMemory` descriptors
  (`NwoasSplitMemoryMap.h:40`), so it does not touch the reservation.
  Intentional and consistent.
- **Platform PCD declarations.** `MemoryInitPeiLib.inf` gains
  `[FixedPcd] PcdBootArgsPointer, PcdAdtPointer`; both are
  `PcdsFixedAtBuild.common` in `T810XFamilyPkg.dsc.inc:19-20`, so `PcdGet64`
  resolves to the build constants and the values match the ones
  `ModuleEntryPoint.S:206-207` passes to `EarlySetup`. `PcdFdBaseAddress` is
  PatchableInModule and the library is linked into the PrePi module, so it
  sees the runtime-patched value. Correct.
- **Ordering with the MMU.** Both the `EarlySetup` copies and the
  `MemoryPeim` read of `devtree_size` at `0x840000000` happen before
  `InitMmu` (line 415), i.e. with the MMU off; no cache-coherence step is
  needed between the write and the read.

## 3. Cross-candidate consistency

S215 places the FD at `ALIGN_UP(max(heap, window_end), 2 MiB)`, and S216
accepts any FD base outside the window, so the two agree by construction.
`test_layout.c` case 1 (`dest = 0x840200000`, window end `0x840060000` for
`adt_size = 0x5c000`) satisfies `test_ranges.c` case 1. Each side still
halts loudly if the other is absent.

## 4. Non-blocking findings and minimal fixes

1. **ADT size bound differs.** `NwoasHandoffRange`
   rejects `adt_size > 0x100000`; `nwoas_handoff_layout` accepts any size
   that fits RAM. For an ADT above 1 MiB the loader would place, then UEFI
   would halt. Observed size is `0x5c000`, so this cannot trigger today.
   Fix, one line in `handoff_layout.h` after the null checks:
   `if(adt_size>0x100000ULL)return 0;` and one assert in `test_layout.c`.
2. **Two guards, two RAM sizes.** `EarlySetup` checks against raw
   `BootArgs->mem_size`; `MemoryPeim` checks against
   `PcdSystemMemorySize = Layout.GuestRamSize`, which in legacy mode
   (`NWOAS_WINDOW_AFTER_RAM=0`) is 4 GiB smaller. A window inside the last
   4 GiB would pass the first guard and halt at the second. Cannot happen
   with `0x840000000`; fix only if the constants move: pass
   `Layout.GuestRamSize` to the first guard by moving the guard after the
   `NwoasComputeGuestRamLayout` block.
3. **Unreachable `return` after `CpuDeadLoop()`** in both S216 halts. Harmless;
   keep or drop.

Cosmetic: the S215 `printf` uses `%llx`; m1n1 `vsprintf.c:346-348` supports
`ll`, so the diagnostic line prints correctly.

## 5. What this review does not establish

Correct placement on hardware, the actual session `heap_top`, and the S214
soak result. The S215 build tag `v1.0.2-1474-gbddf7f06-s215handoff` and the
S216 payload `0ae1cb75…ad3197` are the artefacts this review applies to.
