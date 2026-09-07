/**
 * Copyright (c) 2026, AppleWOA authors. All rights reserved.
 *
 * Module Name:
 *     NwoasHideHighRamDxe.c
 *
 * Abstract:
 *     NWOAS (Native Windows on Apple Silicon) workaround driver, §3.2.
 *
 *     Wall: Windows 11 ARM's usbxhci.sys cannot allocate a DMA common
 *     buffer reachable by the 32-bit-addressing-only FL1100 xHCI. The
 *     "deferred" build advertises high RAM (>=4GB PA) as
 *     EfiConventionalMemory so UEFI's own USB reads work during boot
 *     services, but Windows then also picks its xHCI DMA buffer above 4GB
 *     out of that same pool (kernel-stage dumps: DCBAAP=0).
 *
 *     "Real deferred map" (this driver, NWOAS_HIDE_USE_WRAP=1):
 *     UEFI keeps full high-RAM visibility for its OWN execution (reads
 *     work, Setup renders). At ReadyToBoot -- the last firmware-owned
 *     instant, still before ExitBootServices -- we WRAP gBS->GetMemoryMap.
 *     The wrapper calls the real CoreGetMemoryMap, then rewrites Type to
 *     EfiReservedMemoryType for descriptors at/above NWOAS_HIDE_BASE in
 *     the *caller's returned snapshot only*. The live DXE-core map is
 *     never mutated, so the returned MapKey stays valid and winload's
 *     ExitBootServices(MapKey) still succeeds. Result the OS loader sees:
 *     the [0,4GB) DART-reachable window is the sole Conventional pool.
 *
 *     Safety net (NWOAS §3.2 pm review, abort guard): if hiding the high
 *     RAM would leave the OS with less than NWOAS_MIN_LOW_FREE_PAGES of
 *     usable window RAM, the wrapper ABORTS the hide (returns the map
 *     untouched) rather than ship a map that OOMs winload before the
 *     kernel stage. A graceful fall-back to the known-deferred null
 *     (DCBAAP=0) beats a wasted hardware cycle with zero data.
 *
 *     This supersedes the legacy AllocatePages(AllocateAddress) path
 *     (NWOAS_HIDE_USE_WRAP=0), which mutated the live map, failed on
 *     ReadyToBoot fragmentation (reserved_pages=0), and would have bumped
 *     the live MapKey.
 *
 *     Build success here does NOT imply boot success; only real hardware
 *     (§3.2 pass signal: kernel-stage DCBAAP!=0 at a <4GB address AND
 *     USBCMD.RS=1) can confirm the FL1100 DMA buffer lands in-window.
 *
 * Environment:
 *     UEFI DXE (Driver Execution Environment).
 *
 * License:
 *     SPDX-License-Identifier: (BSD-2-Clause-Patent OR MIT)
 */

#include <PiDxe.h>
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/AppleDTLib.h>

#include <Guid/EventGroup.h>

//
// ---- NWOAS §3.2 gates (compile-time; A/B revert = edit these) ----------
//
// NWOAS_HIDE_HIGH_RAM : master enable.
//    1 = engage the hide-high-RAM workaround at ReadyToBoot.
//    0 = INERT. Driver registers the hook but does nothing; wrapper is
//        never installed and gBS CRC is never touched -> byte-for-byte the
//        pure "deferred" behavior. THIS IS THE ONE-LINE A/B REVERT KNOB.
//
#ifndef NWOAS_HIDE_HIGH_RAM
#define NWOAS_HIDE_HIGH_RAM 1   // NWOAS §3.2 COMBINE experiment (2026-07-11 night): wrapper ON to force
                                // Windows' DMA common buffer into the sole <4GB window. Pairs with the
                                // RPi4-match ACPI (IORT + leaf _DMA removed) that made DCBAAP!=0 possible
                                // but FLAKY (1/2 boots) -- hypothesis: real-deferred-map makes it RELIABLE
                                // by removing high RAM so the DCBAA MUST land in the reachable window.
#endif

//
// NWOAS_HIDE_USE_WRAP : which mechanism when the master gate is on.
//    1 = ROBUST GetMemoryMap wrapper (this spec). Does not mutate the live
//        map -> cannot fail on fragmentation, keeps ExitBootServices MapKey
//        valid.
//    0 = LEGACY AllocatePages(AllocateAddress) walk (retired Candidate A;
//        documented FAILED with reserved_pages=0). Kept only for A/B.
//
#ifndef NWOAS_HIDE_USE_WRAP
#define NWOAS_HIDE_USE_WRAP 1
#endif

//
// NWOAS_HIDE_FULL : which source memory types get hidden (wrapper mode).
//    0 = CONSERVATIVE. Only EfiConventionalMemory (free RAM) >= base.
//        Lowest brick risk: never relabels winload code/data/pagetables or
//        the boot.wim RAMDisk. Residual gap: high-RAM BootServices/Loader
//        regions Windows reclaims post-EBS can re-enter the free pool.
//    1 = FULL. Also hides EfiBootServicesCode/Data + EfiLoaderCode/Data
//        >= base, so post-EBS reclamation cannot re-add high RAM. Reserved
//        stays CPU-readable/executable (RAMDisk + loader image still work),
//        just not reclaimed. Higher risk (relabels winload's own regions);
//        revert to 0 if winload/kernel MM chokes.
//
#ifndef NWOAS_HIDE_FULL
#define NWOAS_HIDE_FULL 0
#endif

//
// NWOAS_HIDE_BASE : PAs at/above this are hidden from the OS. The
// [0, NWOAS_HIDE_BASE) window (PhysicalStart==0 in the OS-facing map, the
// DART-reachable sub-4GB backing window) is left as the sole Conventional
// pool. OOM fallback: raise this to keep more high RAM visible (at the cost
// of reopening out-of-window DMA), and rebuild.
//
#define NWOAS_HIDE_BASE 0x0000000100000000ULL

//
// NWOAS_MIN_LOW_FREE_PAGES : abort guard floor. If, at the OS-loader's
// GetMemoryMap, the free (EfiConventionalMemory) pool BELOW NWOAS_HIDE_BASE
// (the [0,4GB) DART window) totals fewer than this many 4KB pages, the
// wrapper does NOT hide high RAM (returns the map untouched). 0x80000 pages
// = 2 GiB. Rationale: EDK2 allocates top-down so the low window should stay
// nearly-empty (~4GB free); if it does not, hiding high RAM would leave the
// OS below a bootable floor -> abort to known-deferred instead of bricking.
//
#define NWOAS_MIN_LOW_FREE_PAGES 0x0000000000080000ULL

STATIC EFI_EVENT  mReadyToBootEvent = NULL;
STATIC BOOLEAN    mHideDone         = FALSE;

#if NWOAS_HIDE_HIGH_RAM && NWOAS_HIDE_USE_WRAP
//
// Saved original gBS->GetMemoryMap. The wrapper calls THIS directly (never
// gBS->GetMemoryMap) so it does not recurse.
//
STATIC EFI_GET_MEMORY_MAP  mOrigGetMemoryMap = NULL;
STATIC BOOLEAN             mWrapLogged       = FALSE;

/**
  Whether a descriptor of the given EFI memory type is eligible to be
  hidden (relabeled Reserved) when it lives at/above NWOAS_HIDE_BASE.

  CONSERVATIVE (NWOAS_HIDE_FULL=0): free RAM only.
  FULL         (NWOAS_HIDE_FULL=1): also reclaimable BootServices/Loader.

  Never returns TRUE for runtime, ACPI, MMIO, PersistentMemory, PalCode,
  or already-Reserved types.
**/
STATIC
BOOLEAN
NwoasTypeIsHideable (
  IN UINT32  Type
  )
{
  if (Type == EfiConventionalMemory) {
    return TRUE;
  }
#if NWOAS_HIDE_FULL
  if ((Type == EfiBootServicesCode) ||
      (Type == EfiBootServicesData) ||
      (Type == EfiLoaderCode)       ||
      (Type == EfiLoaderData)) {
    return TRUE;
  }
#endif
  return FALSE;
}

/**
  Wrapper installed over gBS->GetMemoryMap at ReadyToBoot.

  Defers to the real CoreGetMemoryMap, then -- on a genuine FILL result
  only -- rewrites Type to EfiReservedMemoryType for every hideable
  descriptor at/above NWOAS_HIDE_BASE, IN THE CALLER'S RETURNED SNAPSHOT.

  Two read-only passes + one conditional mutation pass, all allocation-free:
    pass 1  : tally low-window free pages (Conventional < base) and the
              high hideable pages (>= base); decide the abort guard.
    pass 2  : only if the guard is clear, relabel hideable >= base -> Reserved.

  Invariants that keep ExitBootServices(MapKey) valid:
    * calls mOrigGetMemoryMap (not gBS->GetMemoryMap) -> no recursion;
    * performs NO memory allocation of any kind -> live map key unchanged;
    * returns the original MapKey verbatim.

  @param[in,out] MemoryMapSize      As EFI_GET_MEMORY_MAP.
  @param[in,out] MemoryMap          As EFI_GET_MEMORY_MAP.
  @param[out]    MapKey             As EFI_GET_MEMORY_MAP (passed through).
  @param[out]    DescriptorSize     As EFI_GET_MEMORY_MAP.
  @param[out]    DescriptorVersion  As EFI_GET_MEMORY_MAP.
**/
EFI_STATUS
EFIAPI
NwoasWrappedGetMemoryMap (
  IN OUT UINTN                  *MemoryMapSize,
  IN OUT EFI_MEMORY_DESCRIPTOR  *MemoryMap,
  OUT    UINTN                  *MapKey,
  OUT    UINTN                  *DescriptorSize,
  OUT    UINT32                 *DescriptorVersion
  )
{
  EFI_STATUS             Status;
  UINTN                  DescSize;
  UINTN                  Count;
  UINTN                  i;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINT64                 LowFreePages;
  UINT64                 HideablePages;
  UINT64                 HideableCount;
  UINT64                 FirstHiddenPa;
  UINT64                 Rewritten;
  BOOLEAN                AbortGuard;

  //
  // Real map first. Caller's pointers pass straight through so the returned
  // MapKey reflects true live-map state.
  //
  Status = mOrigGetMemoryMap (
             MemoryMapSize,
             MemoryMap,
             MapKey,
             DescriptorSize,
             DescriptorVersion
             );

  //
  // Only doctor a genuine FILL. A NULL/too-small probe returns
  // EFI_BUFFER_TOO_SMALL with MemoryMap untouched -> pass through.
  //
  if (EFI_ERROR (Status)   ||
      (MemoryMap == NULL)  ||
      (DescriptorSize == NULL) ||
      (*DescriptorSize == 0)) {
    return Status;
  }

  DescSize      = *DescriptorSize;
  Count         = *MemoryMapSize / DescSize;
  LowFreePages  = 0;
  HideablePages = 0;
  HideableCount = 0;
  FirstHiddenPa = 0;
  Rewritten     = 0;

  //
  // Pass 1 (read-only): tally the low-window free pool and the hideable high
  // pages. This decides the abort guard BEFORE any relabel, so the decision
  // is all-or-nothing (never a partial hide that leaves stray high RAM).
  //
  Desc = MemoryMap;
  for (i = 0; i < Count; i++) {
    if (Desc->PhysicalStart >= NWOAS_HIDE_BASE) {
      if (NwoasTypeIsHideable (Desc->Type)) {
        if (HideableCount == 0) {
          FirstHiddenPa = Desc->PhysicalStart;
        }
        HideablePages += Desc->NumberOfPages;
        HideableCount += 1;
      }
    } else {
      //
      // Below the hide line == the DART-reachable [0,4GB) window. Count its
      // free (Conventional) pages: this is what the OS is left with.
      //
      if (Desc->Type == EfiConventionalMemory) {
        LowFreePages += Desc->NumberOfPages;
      }
    }
    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescSize);
  }

  AbortGuard = (BOOLEAN)(LowFreePages < NWOAS_MIN_LOW_FREE_PAGES);

  //
  // Pass 2 (mutation, snapshot-only): relabel high hideable descriptors to
  // Reserved, UNLESS the guard fired. Never touches PhysicalStart < base
  // (the window stays the sole Conventional pool).
  //
  if (!AbortGuard) {
    Desc = MemoryMap;
    for (i = 0; i < Count; i++) {
      if ((Desc->PhysicalStart >= NWOAS_HIDE_BASE) &&
          NwoasTypeIsHideable (Desc->Type)) {
        Desc->Type  = EfiReservedMemoryType;   // snapshot-only edit
        Rewritten  += 1;
      }
      Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescSize);
    }
  }

  //
  // Log once (first fill). NO allocation on this path (SerialPortLib
  // DebugLib does not allocate on this platform; serial is already warmed
  // by the entry-point/ReadyToBoot DEBUGs above).
  //
  if (!mWrapLogged) {
    mWrapLogged = TRUE;
    DEBUG ((DEBUG_ERROR,
      "HVLOG: NWOAS HIDE(wrap) fill#1 desc_total=%lu low_free_pages=0x%lx "
      "would_survive_MB=%lu high_hideable_pages=0x%lx hideable_desc=%lu "
      "abort_guard=%a rewritten=%lu hidden_MB=%lu first_hidden_PA=0x%lx "
      "window_kept=[0,0x%lx) full=%d\n",
      (UINT64)Count,
      LowFreePages,
      (LowFreePages * EFI_PAGE_SIZE) / (1024 * 1024),
      HideablePages,
      HideableCount,
      AbortGuard ? "fired" : "clear",
      Rewritten,
      (HideablePages * EFI_PAGE_SIZE) / (1024 * 1024),
      FirstHiddenPa,
      (UINT64)NWOAS_HIDE_BASE,
      NWOAS_HIDE_FULL));
  }

  return Status;
}
#endif // NWOAS_HIDE_HIGH_RAM && NWOAS_HIDE_USE_WRAP

/**
  ReadyToBoot notification. Runs once, at the last firmware-owned instant
  before the OS loader takes over (still before ExitBootServices).

  NWOAS_HIDE_USE_WRAP=1: install NwoasWrappedGetMemoryMap over
  gBS->GetMemoryMap and recompute the boot-services table CRC. Firmware's
  own boot-time GetMemoryMap calls (all before this event) already used the
  original pointer, so UEFI kept full high-RAM visibility for its own reads.

  NWOAS_HIDE_USE_WRAP=0: legacy AllocatePages(AllocateAddress) walk
  (retired Candidate A; kept for A/B).
**/
VOID
EFIAPI
NwoasHideHighRamOnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
#if NWOAS_HIDE_HIGH_RAM && !NWOAS_HIDE_USE_WRAP
  //
  // Legacy-path locals only (scoped to avoid -Werror=unused-variable when
  // the wrapper path is selected or the gate is off).
  //
  EFI_STATUS             Status;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;
  EFI_MEMORY_DESCRIPTOR  *MemoryMap;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  UINTN                  Count;
  UINTN                  i;
  UINT64                 TotalReservedPages;
#endif
  struct boot_args  *BA;
  UINT64            PhysBase;
  UINT64            MemSize;
  UINT64            Backing;

  if (mHideDone) {
    return;
  }
  mHideDone = TRUE;

  //
  // Cross-check log from boot_args (same idiom as AppleDartIoMmuDxe.c).
  //
  BA       = (struct boot_args *)FixedPcdGet64 (PcdBootArgsPointer);
  PhysBase = BA->phys_base;
  MemSize  = BA->mem_size;
  Backing  = ((PhysBase >= 0x800000000ULL) && (MemSize > 0x100000000ULL))
               ? (PhysBase + MemSize - 0x100000000ULL) : 0;

  DEBUG ((DEBUG_ERROR,
    "HVLOG: NWOAS HIDE begin phys_base=0x%lx mem_size=0x%lx backing=0x%lx "
    "hide_base=0x%lx min_low_free_pages=0x%lx gate=%d wrap=%d full=%d\n",
    PhysBase, MemSize, Backing, (UINT64)NWOAS_HIDE_BASE,
    (UINT64)NWOAS_MIN_LOW_FREE_PAGES,
    NWOAS_HIDE_HIGH_RAM, NWOAS_HIDE_USE_WRAP, NWOAS_HIDE_FULL));

#if NWOAS_HIDE_HIGH_RAM
# if NWOAS_HIDE_USE_WRAP
  //
  // ROBUST: patch the GetMemoryMap vector + recompute gBS CRC. No live-map
  // mutation, so ExitBootServices(MapKey) issued later by winload stays
  // valid.
  //
  mOrigGetMemoryMap = gBS->GetMemoryMap;
  gBS->GetMemoryMap = NwoasWrappedGetMemoryMap;

  gBS->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gBS, gBS->Hdr.HeaderSize, &gBS->Hdr.CRC32);

  DEBUG ((DEBUG_ERROR,
    "HVLOG: NWOAS HIDE(wrap) installed orig=0x%lx wrap=0x%lx new_crc=0x%x "
    "hdrsize=%lu hide_base=0x%lx full=%d\n",
    (UINT64)(UINTN)mOrigGetMemoryMap,
    (UINT64)(UINTN)NwoasWrappedGetMemoryMap,
    gBS->Hdr.CRC32,
    (UINT64)gBS->Hdr.HeaderSize,
    (UINT64)NWOAS_HIDE_BASE,
    NWOAS_HIDE_FULL));
# else
  //
  // LEGACY (retired Candidate A): AllocatePages walk. Mutates the live map,
  // fails on ReadyToBoot fragmentation (reserved_pages=0). Kept for A/B.
  //
  MapSize            = 0;
  MapKey             = 0;
  DescriptorSize     = 0;
  DescriptorVersion  = 0;
  MemoryMap          = NULL;
  TotalReservedPages = 0;

  Status = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);
  if (DescriptorSize == 0) {
    DEBUG ((DEBUG_ERROR, "HVLOG: NWOAS HIDE(legacy): size probe failed, DescriptorSize=0\n"));
    return;
  }

  MapSize  += 32 * DescriptorSize;
  MemoryMap = AllocatePool (MapSize);
  if (MemoryMap == NULL) {
    DEBUG ((DEBUG_ERROR, "HVLOG: NWOAS HIDE(legacy): AllocatePool(0x%lx) failed\n", (UINT64)MapSize));
    return;
  }

  Status = gBS->GetMemoryMap (&MapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HVLOG: NWOAS HIDE(legacy): GetMemoryMap fill failed: %r\n", Status));
    FreePool (MemoryMap);
    return;
  }

  Count = MapSize / DescriptorSize;
  Desc  = MemoryMap;
  for (i = 0; i < Count; i++) {
    if ((Desc->Type == EfiConventionalMemory) && (Desc->PhysicalStart >= NWOAS_HIDE_BASE)) {
      EFI_PHYSICAL_ADDRESS Addr = Desc->PhysicalStart;
      Status = gBS->AllocatePages (AllocateAddress, EfiReservedMemoryType, (UINTN)Desc->NumberOfPages, &Addr);
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "HVLOG: NWOAS HIDE(legacy) skip 0x%lx pages=0x%lx %r\n",
                (UINT64)Desc->PhysicalStart, (UINT64)Desc->NumberOfPages, Status));
      } else {
        TotalReservedPages += Desc->NumberOfPages;
      }
    }
    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescriptorSize);
  }

  DEBUG ((DEBUG_ERROR,
    "HVLOG: NWOAS HIDE(legacy) done reserved_pages=0x%lx (~%lu MB)\n",
    TotalReservedPages, (UINT64)(TotalReservedPages * EFI_PAGE_SIZE / (1024 * 1024))));

  FreePool (MemoryMap);
# endif // NWOAS_HIDE_USE_WRAP
#else
  DEBUG ((DEBUG_ERROR, "HVLOG: NWOAS HIDE inert (gate=0, pure deferred)\n"));
#endif // NWOAS_HIDE_HIGH_RAM
}

/**
  Entry point. Registers the ReadyToBoot notification. Never fails the
  driver load; this is a best-effort workaround over an otherwise-functional
  deferred build.
**/
EFI_STATUS
EFIAPI
NwoasHideHighRamDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  NwoasHideHighRamOnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );

  DEBUG ((DEBUG_ERROR,
    "HVLOG: NWOAS HideHighRam ReadyToBoot hook registered %r "
    "(gate=%d wrap=%d full=%d base=0x%lx)\n",
    Status, NWOAS_HIDE_HIGH_RAM, NWOAS_HIDE_USE_WRAP, NWOAS_HIDE_FULL,
    (UINT64)NWOAS_HIDE_BASE));

  return EFI_SUCCESS;
}
