# S213: why the 30 MiB `image_size` FD died before UEFI, from source only

Read-only source investigation. Nothing was built, booted or edited outside
`nwoas_scripts/loader-s213/`. No hardware, USB, secrets, raw logs, ADT or
NVRAM dumps were read. Inputs: `firmware-s204/hardware-result.json` (curated),
the S204 builders and FDF snapshots, `apple_silicon_platforms_mu` PrePi /
MemoryInitPeiLib / DSC sources, and the pinned guest m1n1 tree
`m1n1-guest-s197` (`bddf7f06`). **No hardware fix is verified here.** The
report needs no user approval; it recommends, it does not change active
source.

Legend: **[obs]** = recorded in curated evidence or read literally from
source; **[inf]** = inferred from source by reasoning.

## 1. Answer in one paragraph

The failure is a fixed-address collision, not an image-size effect. PrePi's
entry code copies `boot_args` to the build-time constant `PcdBootArgsPointer`
= `0x840000000` and the ADT to `PcdAdtPointer` = `0x840004000` while it is
executing from the FD and using a temporary stack whose top is `FdBase +
0x8000` **[obs]**. In the S204 run the FD was placed at exactly `0x840000000`
**[obs]**, so the ADT copy starts at FD offset `0x4000`, runs over the live
temp stack, the FV header at offset `0x8000` and the relocated PrePi image
**[inf]**. The FD placement address is computed by m1n1 as
`ALIGN_UP(heap_base, 2 MiB)` and does not depend on `image_size` **[obs]**;
`image_size` only changes how much m1n1 reserves *after* that address. So the
old 256.875 MiB header would have died identically at the same address; the S203
pass implies the FD landed elsewhere in that session **[inf]**. The old
runtime address is not recorded in curated evidence and is not invented here.

## 2. What the builders actually produce

- `firmware-s204/build_candidate.py` swaps the RTC lib in the DSC, then runs
  `build_memory.py`, which applies the S161 memory diff, the S167 hide-high-RAM
  wrapper, the S204 one-line FDF change, and generates `s131_builder.py` from
  `smp-s131/build_candidate.py` (eight GICC entries, NVMe identity, SSD-first,
  NVMe enable) with output paths retargeted to S204 **[obs]**.
- The generated `s131_builder.py` still assembles its inner payload as
  `old[:1376256] + fd` from the opaque S129 payload **[obs]** (line 125-127);
  that inner payload is only a carrier. `assemble_candidate.py` re-slices the
  FD at 1376256 and prepends the S197 compat prefix + DTB, giving the tested
  33,177,600-byte candidate with the FD at payload offset `0x21c000` **[obs]**.
- The only FD difference versus S192 is bytes 18-19 (`0e 10` to `e0 01`)
  **[obs]**, i.e. `image_size` `0x100E0000` to `0x01E00000`. `PcdFdSize` is
  `0x1E00000` and the FD file is `0x8000 + 0x1D80000 = 0x1D88000` bytes in both
  **[obs]** (`MacMini2020.fdf` lines 30-31, 60-75).
- No builder, launcher or guest module reads `image_size`; the only consumer
  is m1n1 `load_kernel()` **[obs]** (grep over `nwoas_scripts/*.py` excluding
  logs, `hv/__init__.py`, `run_guest.py`).

## 3. Placement chain, from source

Host side (`m1n1_windows/proxyclient/m1n1/hv/__init__.py:2186-2296`) **[obs]**:

1. `guest_base = heap_top + 16 MiB + align(devtree_size) + align(tc_size)`;
   `heap_top` is the host proxy's heap end
   (`proxyutils.py:102-112`: host `heapblock_alloc(0)` + 128 MiB + 1 GiB), a
   runtime property of the host m1n1 state at proxy connect.
2. Payload is written at `guest_base`; `top_of_kernel_data = guest_base +
   align(len(payload)) + align(SEPFW) + preoslog + 0x4000`.
3. Guest `phys_base` is `heap_top`; guest RAM is `[heap_top, mem_top)`.

Guest m1n1 (`m1n1-guest-s197/src`) **[obs]**:

4. `heapblock_init()` sets `heap_base = top_of_kernel_data`; `mmu_init`,
   `display_init` (compat variant) and friends malloc from it before payloads.
5. `payload_run()` finds the DTB at `_payload_start` and the FD at
   `_payload_start + 0x10000`. `load_kernel()` (`payload.c:134-159`): if the FD
   is not 2 MiB aligned, `K = heapblock_alloc_aligned(image_size, 2 MiB)`
   returns `ALIGN_UP(heap_base, 2 MiB)` and advances `heap_base` by
   `image_size`; then a byte-wise forward `memcpy` (`string.c:9-19`) copies
   `image_size` bytes. `K` is independent of `image_size` (heapblock.c:40).
6. `kboot_prepare_dt()` then `memalign`s the DT buffer (`fdt_totalsize + 96
   KiB`) and the EL2 ADT copy (`devtree_size` rounded + 16 KiB) via dlmalloc,
   whose `sbrk` is `heapblock_alloc` (`malloc_config.h`), i.e. at or after
   `K + image_size`.
7. `kboot_boot()` sets `entry = K`, `x0 = dt`, `x4 = &cur_boot_args`
   (`kboot.c:2828-2833`); `m1n1_main` shuts down MMU and jumps.

UEFI PrePi (`PrePi/AArch64/ModuleEntryPoint.S`) **[obs]**:

8. FDF stub at `K`: `adr x1, .` then `b 0x8000`. Entry saves `x0`(dt),
   `x1`(K), `x4`(boot_args). `DiscoverDramFromDt` checks `ARM\x64` at
   `K+0x38`, writes `K` into PrePi's own `PcdFdBaseAddress` and `K+0x8000`
   into `PcdFvBaseAddress` (lines 170-177), sets `sp = K + 0x8000` (line 186,
   temp stack grows down into the 32 KiB header stub) and relocates the PE
   image in place.
9. `EarlySetup(x25, ..., 0x840000000, 0x840004000)` (`AdtParser.c:29-30`):
   `CopyMem(0x840000000, boot_args, sizeof(struct boot_args))` (0x480 bytes,
   from `xnuboot.h`) then `CopyMem(0x840004000, ADT source, devtree_size)`.
   These two addresses are `PcdsFixedAtBuild` in
   `T810XFamilyPkg.dsc.inc:19-20`, and every later consumer uses
   `FixedPcdGet64` (`AppleDTLib.c:315,321`, `SimpleFbDxe.c:176`,
   `AppleDartIoMmuDxe.c:633,1247`, `NwoasHideHighRamDxe.c:372`).
10. Back in `_ModuleEntrySetupStack` the real UEFI region (128 MiB) is placed
    at the top of system memory, or directly below `FdBase` if the gap is
    smaller; `MemoryInitPeiLib.c:260-323` reserves exactly `[FdBase, FdBase +
    PcdFdSize)` as BootServicesData. Nothing reserves the `0x840000000`
    window (lines 346-359 reserve only the cpm-impl-reg carveouts).

## 4. Does the ADT copy overwrite executing relocated firmware?

With `K = 0x840000000` **[obs]**, yes **[inf]**:

| Range written by EarlySetup | FD offset | What lives there at that moment |
|---|---|---|
| `[0x840000000, 0x840000480)` boot_args | `0x0..0x480` | Linux header stub incl. magic at `+0x38` (already consumed; harmless by itself) |
| `[0x840004000, 0x840004000 + devtree_size)` ADT | `0x4000..` | remaining stub, the **live temp stack** just below `K+0x8000`, the FV header at `+0x8000`, then the first FFS file, which is the relocated PrePi SEC image (`FVMAIN_COMPACT` line 311) |

`devtree_size` is not in `hardware-result.json`. Main reports the observed
value as `0x5c000` (376 KiB), read from the exception-copy registers; it is
cited here as main-provided evidence, not added to the curated file. With
that size the ADT copy spans `[0x840004000, 0x840060000)`: the stack in use,
the FV header at `+0x8000` and the first ~352 KiB of `FVMAIN_COMPACT`, which
starts with the relocated PrePi image. Any value above roughly `0x3800`
already reaches the stack. The failure signature `PC = 0xffffffffffffffff`,
`ESR = 0x8a000000` **[obs]** decodes as EC `0x22` = PC alignment fault (IL=1,
ISS 0), the exception taken when a branch or return targets a non-4-byte
aligned PC. It is consistent with returning through a corrupted stack frame
into all-ones **[inf]** and not with a DEBUG `ASSERT` dead loop, which would
leave a valid PC. (Earlier text here wrongly called it an instruction abort.)

Other fixed addresses: `PcdBootArgsPointer` overlaps the same FD by
construction (it equals `K` here). `PcdSecPhaseStackBase` (`0x900000000`) is
declared but the entry code hardcodes a `0x20000` stack instead (line 89), so
it is unused **[obs]**. `PcdFdBaseAddress` link value `0x830000000` is
overwritten at runtime and only affects the pre-relocation UART line
`FD Base Address` **[obs]**. No other fixed RAM address is written in SEC.

## 5. Why the 256.875 MiB header "worked": what the allocation code does and does not say

- What `image_size` changes **[obs]**: the length of the m1n1 reservation and
  copy after `K`. Old: `[K, K+0x100E0000)`. New: `[K, K+0x01E00000)`. Every
  later m1n1 allocation (DT buffer, EL2 ADT copy, malloc arena) moves down by
  `0xE2E0000` (226.875 MiB; `0x100E0000` is 269,352,960 bytes = 256.875 MiB, not
  269 MiB as earlier documents said).
- What it does not change **[obs]**: `K` itself, hence `FdBase`, hence the
  position of the window relative to the FD.
- Therefore at `K = 0x840000000` the old header collides exactly the same
  way (test `test_observed_entry_collides_for_both_headers`). Curated
  `nvme-s211/failed-layout.json` confirms this independently on hardware: the
  S197 compat payload with the original header, entry `0x840000000`, same PC
  and ESR, "FAIL before Windows" **[obs]**. The old header
  cannot have "avoided" this geometry; S203 passing means the S203 session's
  `K` was not in the fatal band `(0x840004000 + devtree_size - 0x1D88000,
  0x840004000 + devtree_size)` **[inf]**. `K` is quantised to 2 MiB from a
  heap base that is a host runtime property (section 3, step 1), so a
  sub-2 MiB shift in host heap state flips `K` by 2 MiB. The S189 recorded
  guest base `0x83e7dc000` (`loader-s197/assemble_validate.py:36-39`) gives
  `K >= 0x840800000` **[inf]**, i.e. the window below the FD inside dead
  loader memory. The observed S204 `K = 0x840000000` requires a guest base at
  least ~8 MiB lower **[inf]**, which shows the placement drifts across
  sessions. The exact S203 address is unrecorded and is not claimed.
- What the 256.875 MiB slack does protect, structurally **[obs, modelled in
  `test_what_269mib_actually_protects`]**: when the live FD ends below
  `0x840000000`, the window falls inside the reservation slack, where m1n1
  never places its DT buffer or malloc arena. With 30 MiB the window would
  instead land in m1n1's post-kernel heap. UEFI does not consume that heap
  (the FDT pointer in `x0` is only printed, `PrePi.c:48-51`; `x4` points into
  m1n1's `.bss`, not the heap), so this difference is benign for UEFI. It is
  therefore not the discriminator either; it only mattered as a side effect of
  the accidental 256.875 MiB value.

Conclusion for the size-only plan: `0x01E00000` is the correct header value
(`== PcdFdSize`, `>= FD length`) but adopting it alone changes nothing about
the real hazard, and the hazard also exists for the 256.875 MiB header at any
session where `K` lands in the fatal band. Do not adopt or reject the header
change on the basis of S203/S204 alone.

## 6. Smallest source correction, and the invariants it must hold

The window is fixed at build; the FD address is decided at runtime by m1n1's
bump allocator; nothing on either side checks the two against each other.
The smallest correction that makes the failure impossible rather than
unlikely is on the loader side, because only the loader knows `K` before the
copy happens, and the UEFI side has no free choice (every consumer is
`FixedPcdGet64`):

1. **Loader placement rule (m1n1 `load_kernel`, ~10 lines):** before
   `heapblock_alloc_aligned(image_size, 2 MiB)`, bump `heap_base` past
   `ALIGN_UP(PcdAdtPointer + devtree_size, 16 KiB)` when the candidate block
   would intersect `[PcdBootArgsPointer, PcdAdtPointer + devtree_size)`; also
   force the copy branch so an accidentally 2 MiB-aligned inline FD cannot
   bypass the rule. This is exactly what `loader-s215/placement.patch` drafts
   (build-only, unverified on hardware).
2. **UEFI guard (PrePi `EarlySetup`, ~8 lines):** refuse and halt with a UART
   line if the copy targets intersect `[FdBase, FdBase + PcdFdSize)` or
   `[FdBase + 0x8000 - stack, FdBase + 0x8000)`, computed before the first
   `CopyMem`. Turns a silent stack smash into a diagnosable halt.
   `firmware-s216/patch_sources.py` drafts this plus a HOB reservation
   (build-only, unverified on hardware).
3. **Header:** keep `image_size = PcdFdSize` (`0x01E00000`) so the m1n1
   reservation equals UEFI's claim; the old value only masked (3) below.

Reservation and lifetime invariants (all must hold; today none is checked):

- **I1 disjointness at handoff:** `W = [PcdBootArgsPointer, ALIGN_UP(PcdAdtPointer + devtree_size, 16 KiB))`
  must not intersect `[K, K + PcdFdSize)`, the temp stack
  `[K + 0x8000 - depth, K + 0x8000)`, the guest m1n1 image
  `[guest_base, guest_base + 0x20c000)` (parked secondaries spin in it and
  `cur_boot_args`, the copy source, lives in its `.bss`), nor the ADT source
  `[adt_base, adt_base + devtree_size)`.
- **I2 loader reservation:** `image_size >= PcdFdSize`, so m1n1's bump
  allocator never hands out `[K + FD length, K + PcdFdSize)` which UEFI
  treats as its own. Both header values satisfy this.
- **I3 UEFI lifetime:** `W` is read in SEC (`MemoryInitPeiLib` carveout loop
  via `dt_get`), throughout DXE (`AppleDTLib` users, `SimpleFbDxe`,
  `NwoasHideHighRamDxe`) and inside the ExitBootServices handler
  (`AppleDartIoMmuDxe.c:633`). So `W` must be excluded from the DXE free pool
  for the whole boot-services lifetime: a `BuildMemoryAllocationHob(W,
  EfiBootServicesData)` next to the FD carve in `MemoryInitPeiLib`, sized
  from `devtree_size`, not a constant.
- **I4 placement side:** `W` must lie inside guest RAM `[phys_base, phys_base
  + mem_size)` and above `top_of_kernel_data`; the loader rule in (1) is the
  only place this can be enforced because `guest_base` is chosen by the host
  per session.
- **I5 copy order:** copy the ADT before `boot_args` if the destinations can
  ever alias their sources (S216 already orders it this way).

## 7. Test evidence (host only)

`test_handoff_overlap.py` reads the real constants from tracked source and
the curated evidence (no build products, no logs) and models only the
heapblock bump rule and the stub stack. Six checks, all pass; output in
`test-evidence.txt`:

| Test | What it proves from source |
|---|---|
| `test_source_constants` | PCD values, FDF geometry, `KERNEL_ALIGN`, `sizeof(boot_args) = 0x480`, `sp = FvBase`, curated entry `== PcdBootArgsPointer` |
| `test_image_size_constraints` | both headers `>= PcdFdSize` and `>= FD length`; new `== PcdFdSize` |
| `test_kernel_block_address_is_header_independent` | `K` identical for both headers over a full 2 MiB sweep of heap bases; only the post-`K` reservation differs by `0xE2E0000` |
| `test_observed_entry_collides_for_both_headers` | at `K = 0x840000000` both copies hit the live FD and any ADT `>= 0x4000` crosses the stack top and FV header, for both headers |
| `test_what_269mib_actually_protects` | with the live FD below the window, only the old header keeps the window inside loader-reserved slack |
| `test_reservation_invariant_for_a_fix` | the I1 disjointness predicate rejects the observed geometry and accepts the S215 placement rule |

## 8. Status

- Hardware fix: **none verified**. S215 (loader placement) and S216 (UEFI
  guard + HOB reservation) exist as build-only drafts; neither has a curated
  hardware result in this tree.
- Not determined from curated evidence: the S203 `K` and the host `heap_top`
  of either session. `devtree_size = 0x5c000` is main-provided (section 4).
  None is needed for the conclusion.
- Files: `README.md` (this), `test_handoff_overlap.py`, `test-evidence.txt`,
  `review-s216.md` (source review of the S215 loader placement and S216 UEFI
  guard/reservation candidates).
