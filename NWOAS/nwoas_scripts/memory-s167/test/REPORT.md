# Test report: `Include/Library/NwoasSplitMemoryMap.h`

Date: 2026-09-10
Scope: host-side review and tests of `NwoasSplitMemoryMap()` only. No hardware,
disk, or other source directories were touched. The production header was not
modified (sha256 `71711be7…97e4ca`, mtime unchanged).

## How to run

```
./run.sh
```

Builds `test_split_memory_map.c` plus `stub/BaseMemoryLibStub.c` with clang
(`-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion`, AddressSanitizer,
UndefinedBehaviorSanitizer) against the real header and the stubs in `stub/`
(`Uefi.h`, `Library/BaseMemoryLib.h`). Each map buffer is heap-allocated with
exactly `Capacity` bytes so any write past `Capacity` is caught by ASan.

## Result

```
365 checks, 0 failures
```

Zero compiler warnings from the test build. No sanitizer reports.

## Coverage matrix

| Requirement | Tests | Outcome |
|---|---|---|
| Descriptor extension bytes preserved (DescSize 48 and 64) | `Test_BasicSplit_ExtensionPreserved`, `Test_DescSize64_LargeExtension` | pass: head and tail both carry the original per-descriptor extension pattern; shifted entries byte-identical |
| Overlapping insertion (memmove-style shift) | `Test_OverlappingInsertion_StraddlerFirst` (5 entries shifted), `Test_OverlappingInsertion_StraddlerLast` (zero-length shift), `Test_BasicSplit_ExtensionPreserved` | pass; stub instrumentation confirms exactly one overlapping `CopyMem` in the middle-straddler case and none for the last-entry case |
| Exact boundary | `Test_ExactBoundary_NoSplit` (end == KeepEnd, start == KeepEnd), `Test_ExactBoundary_StartsAtKeepStart_Splits`, `Test_ExactBoundary_OnePageOverBoundary` | pass |
| No split | `Test_NoSplit_NoStraddler`, `Test_NoSplit_NonConventionalStraddlerIgnored`, `Test_NoSplit_NoSpareCapacityIsFine`, `Test_NoSplit_EmptyMap` | pass: SUCCESS, SplitCount 0, map byte-identical, MapBytes unchanged |
| Insufficient capacity | `Test_InsufficientCapacity_ExactlyFull`, `Test_InsufficientCapacity_PartialSlot` (40 spare bytes < 48), `Test_InsufficientCapacity_RetryWithReportedLength` | pass: BUFFER_TOO_SMALL, map byte-identical, `*MapBytes` = old + DescSize, SplitCount 0; retry with reported size succeeds |
| Malformed size / alignment / overflow | `Test_Malformed_Parameters` (NULLs, DescSize 0/32/44, MapBytes not multiple, MapBytes > Capacity, KeepStart >= KeepEnd, unaligned edges), `Test_Malformed_UnalignedPhysicalStartInWindow`, `Test_Malformed_NumberOfPagesOverflow`, `Test_Malformed_NumberOfPagesAtExactLimitIsAccepted`, `Test_Malformed_VirtualStartOverflow`, `Test_Malformed_ZeroPagesDescriptorInWindowIsHarmless` | pass: INVALID_PARAMETER leaves `*SplitCount` and `*MapBytes` untouched; COMPROMISED_DATA leaves map byte-identical; largest legal descriptor (ending at 2^64-4096) splits correctly |
| Multiple straddlers | `Test_MultipleStraddlers_Rejected`, `Test_MultipleStraddlers_ThirdIsAlsoRejected_NoPartialWrite` | pass: COMPROMISED_DATA, no `CopyMem` executed, map byte-identical |
| Virtual address adjustment | `Test_VirtualAddress_NonZeroAdjusted`, `Test_VirtualAddress_ZeroStaysZero`, `Test_VirtualAddress_MaxWithoutOverflowAccepted`, `Test_VirtualAddress_IdentityAtZero_Observation` | pass: tail VirtualStart = VirtualStart + Offset, phys/virt delta identical in both halves, 0 stays 0, Virt + Offset == MAX_UINT64 accepted |
| Page coverage | `Test_PageCoverage_Conserved` (5 KeepEnd values) | pass: head ends at KeepEnd, tail starts at KeepEnd, tail ends at original end, page sum conserved, no empty halves |
| Unchanged map metadata | `Test_Metadata_UnchangedAndIdempotent`, `Test_DescSize40_NoExtension` | pass: Type, Attribute, padding, extension identical; bytes past new MapBytes untouched; order preserved; second call is a no-op |

## Defects

No functional defect was found in the tested paths. Every documented contract
(preflight before mutation, byte-identical map on every error path, required
length reported on BUFFER_TOO_SMALL, overflow-safe arithmetic) held under ASan
and UBSan.

## Observations (behaviour worth a deliberate decision, not failures)

1. **Straddler that starts below `KeepStart` is silently ignored.**
   Line 41 filters on `PhysicalStart < KeepStart`. A Conventional descriptor
   covering `[16MB, 8GB+16MB)` with window `[1GB, 2GB)` returns SUCCESS with
   `SplitCount = 0` and the map unchanged (`Test_Observation_StraddlerBelowKeepStartNotSplit`).
   If the caller later trims everything above `KeepEnd`, this descriptor's
   upper tail would not be separable. If `KeepStart` is meant only as a lower
   bound on which descriptors to consider, this is intended; if it is meant as
   the window whose upper edge must always become a descriptor boundary, the
   condition should be `End <= KeepStart` instead of `PhysicalStart < KeepStart`.
   Evidence: test output line
   `note: descriptor [0x1000000, 0x201000000) spans window [0x40000000, 0x80000000) but SplitCount=0`.

2. **`VirtualStart == 0` is used as the "not mapped" sentinel.**
   Lines 54 and 82. An identity-mapped Conventional descriptor at physical 0
   (only possible when `KeepStart == 0`) is split into a tail with
   `PhysicalStart = KeepEnd` but `VirtualStart = 0`
   (`Test_VirtualAddress_IdentityAtZero_Observation`). Pre-`SetVirtualAddressMap`
   snapshots carry `VirtualStart = 0` everywhere, so this is harmless in the
   stated use case, but it is a latent inconsistency if the routine is ever
   applied to a runtime-converted map.

3. **Only the `KeepEnd` edge is a split point.** Descriptors that straddle
   `KeepStart` are never split. Consistent with the file comment ("split ... at
   an experimental high-RAM limit"), noted for completeness.

4. **Style / portability.** Line 78 declares `Tail` after statements; clang
   reports it under `-Wdeclaration-after-statement`, which EDK2 coding style
   disallows. `STATIC inline` relies on the C99 `inline` keyword, which older
   MSVC C-mode toolchains do not accept (they need `__inline`). Neither affects
   correctness on clang/gcc.

5. **Untestable guard.** The `*MapBytes > MAX_UINTN - DescSize` check on line 65
   cannot be reached in practice: `*MapBytes <= Capacity` is already enforced and
   the preflight loop would have walked the whole buffer first. It is harmless.

## Files

- `stub/Uefi.h`, `stub/Library/BaseMemoryLib.h`, `stub/BaseMemoryLibStub.c` — host stand-ins (CopyMem = memmove, with overlap counters).
- `test_split_memory_map.c` — 31 test functions, 365 checks.
- `run.sh` — build and run.
- `build/` — compiled binary (generated).
