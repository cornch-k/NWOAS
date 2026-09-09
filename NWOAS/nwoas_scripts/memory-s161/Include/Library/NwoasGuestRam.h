/** @file
  NwoasGuestRam.h

  NWOAS S161: the ONE place that turns boot_args (phys_base, mem_size) into the guest RAM
  extent and the low DMA-window backing.  Every consumer must use these helpers; none may
  carry its own "+/- 4 GiB" arithmetic:

    PrePi/AdtParser.c            PcdSystemMemorySize            = Layout.GuestRamSize
    T810X MemoryInitPeiLib.c     SystemMemoryTop (== backing)   = PcdBase + PcdSize
    AppleDartIoMmuDxe.c          mApcieBackingPa / WIDE-DART end = Layout.BackingPa
    NwoasHideHighRamDxe.c        cross-check log                = NwoasGuestRamBackingPa()

  Layout (both modes; the three ranges are pairwise disjoint by construction because the
  window ends at 4 GiB and guest RAM starts at >= NWOAS_GUEST_PHYS_BASE_MIN = 32 GiB):

    [NWOAS_DMA_WIN_BASE, NWOAS_DMA_WIN_END)   low DMA window (IPA alias, sole sub-4GB pool)
    [PhysBase, GuestRamEnd)                   advertised guest RAM (UEFI / Windows-visible)
    [BackingPa, BackingEnd)                   real RAM the hv aliases the window onto
    GuestRamEnd == BackingPa                  (always -- the backing directly follows RAM)

  NWOAS_WINDOW_AFTER_RAM = 1 (S102, default): the m1n1 hv has ALREADY excluded the 4 GiB
    window backing from boot_args.mem_size.  GuestRamSize = mem_size, nothing subtracted.
    Example (S158 log): phys_base 0x83CA9C000, mem_size 0x2A4530000 -> backing 0xAE0FCC000.
  NWOAS_WINDOW_AFTER_RAM = 0 (legacy): the backing is the TOP 4 GiB of mem_size.
    GuestRamSize = mem_size - 4 GiB, subtracted exactly once -> backing 0x9E0FCC000.

  The header is plain C on top of <Base.h> only (no BaseLib calls) so it compiles both in
  SEC/PEI/DXE modules and in the native unit test (memory-s161/test) against a stub Base.h.

  Copyright (c) 2026, AppleWOA authors. All rights reserved.
  SPDX-License-Identifier: (BSD-2-Clause-Patent OR MIT)
**/

#ifndef NWOAS_GUEST_RAM_H_
#define NWOAS_GUEST_RAM_H_

#include <Base.h>

//
// S102 mode switch.  1 = guest RAM ends where the low-window backing begins (hv shrinks
// boot_args.mem_size); 0 = legacy: backing is the top NWOAS_DMA_WIN_SIZE of mem_size.
// Override with -DNWOAS_WINDOW_AFTER_RAM=0 on the build line; do NOT redefine per module.
//
#ifndef NWOAS_WINDOW_AFTER_RAM
#define NWOAS_WINDOW_AFTER_RAM  1
#endif

//
// Low DMA window: base 0, size 4 GiB, so base + size == 0x100000000 and a 32-bit-IOVA device
// can reach all of it (DSDT _DMA _TRA = 0, IOVA == window IPA).  UNCHANGED from the values
// previously duplicated in AppleDartIoMmuDxe.c (APCIE_WIN_*) and MemoryInitPeiLib.c.
//
#define NWOAS_DMA_WIN_BASE          0x0000000000000000ULL
#define NWOAS_DMA_WIN_SIZE          0x0000000100000000ULL
#define NWOAS_DMA_WIN_END           (NWOAS_DMA_WIN_BASE + NWOAS_DMA_WIN_SIZE)

//
// Sanity bounds.  Guest RAM on every supported m1n1 hv layout starts at or above 32 GiB
// (the same 0x800000000 check the three drivers used before).  The layout must end below
// 64 GiB: that is the T8020 apcie DART TTBR0 span (2048 L1 entries x 32 MiB) and comfortably
// above any Mac mini 2020 DRAM option, so it only catches wrapped/garbage boot_args.
//
#define NWOAS_GUEST_PHYS_BASE_MIN   0x0000000800000000ULL
#define NWOAS_GUEST_PA_LIMIT        0x0000001000000000ULL

//
// DART / hv page granule.  phys_base, mem_size and therefore the backing must be 16 KiB
// aligned or the L2 PTE address masking (APCIE_L2_PAGE) would silently shift the alias.
//
#define NWOAS_DART_PAGE_SIZE        0x4000ULL
#define NWOAS_DART_PAGE_MASK        (NWOAS_DART_PAGE_SIZE - 1ULL)

typedef enum {
  NwoasGuestRamOk = 0,
  NwoasGuestRamErrNullOut,          // Layout pointer is NULL
  NwoasGuestRamErrPhysBaseLow,      // phys_base < NWOAS_GUEST_PHYS_BASE_MIN
  NwoasGuestRamErrUnaligned,        // phys_base or mem_size not 16 KiB aligned
  NwoasGuestRamErrMemSizeZero,      // mem_size == 0
  NwoasGuestRamErrMemSizeTooSmall,  // legacy mode: mem_size <= window (would underflow)
  NwoasGuestRamErrOverflow,         // phys_base + guest size (+ window) wraps UINT64
  NwoasGuestRamErrAboveLimit,       // backing end > NWOAS_GUEST_PA_LIMIT
  NwoasGuestRamStatusMax
} NWOAS_GUEST_RAM_STATUS;

typedef struct {
  UINT64   PhysBase;          // boot_args phys_base: first byte of advertised guest RAM
  UINT64   BootArgsMemSize;   // boot_args mem_size, verbatim (for logs)
  UINT64   GuestRamSize;      // bytes of advertised guest RAM  -> PcdSystemMemorySize
  UINT64   GuestRamEnd;       // PhysBase + GuestRamSize        -> MemoryInitPeiLib SystemMemoryTop
  UINT64   BackingPa;         // == GuestRamEnd                 -> mApcieBackingPa, hv alias target
  UINT64   BackingEnd;        // BackingPa + NWOAS_DMA_WIN_SIZE
  UINT64   WindowBase;        // NWOAS_DMA_WIN_BASE
  UINT64   WindowSize;        // NWOAS_DMA_WIN_SIZE
  BOOLEAN  WindowAfterRam;    // mode the layout was computed with
} NWOAS_GUEST_RAM_LAYOUT;

/**
  Compute the guest RAM / window backing layout for an explicit mode.

  Validation order (first failure wins): NULL out, phys_base floor, 16 KiB alignment,
  mem_size zero, legacy underflow, UINT64 overflow of phys_base+size and of backing+window,
  PA limit.  On any error *Layout is left zeroed.

  @param PhysBase        boot_args.phys_base
  @param MemSize         boot_args.mem_size
  @param WindowAfterRam  TRUE = S102 (backing follows mem_size); FALSE = legacy top-of-RAM
  @param Layout          Filled on NwoasGuestRamOk

  @retval NwoasGuestRamOk on success, else the specific error.
**/
static inline
NWOAS_GUEST_RAM_STATUS
NwoasComputeGuestRamLayoutEx (
  UINT64                  PhysBase,
  UINT64                  MemSize,
  BOOLEAN                 WindowAfterRam,
  NWOAS_GUEST_RAM_LAYOUT  *Layout
  )
{
  UINT64  GuestRamSize;
  UINT64  GuestRamEnd;
  UINT64  BackingEnd;

  if (Layout == NULL) {
    return NwoasGuestRamErrNullOut;
  }

  Layout->PhysBase        = 0;
  Layout->BootArgsMemSize = 0;
  Layout->GuestRamSize    = 0;
  Layout->GuestRamEnd     = 0;
  Layout->BackingPa       = 0;
  Layout->BackingEnd      = 0;
  Layout->WindowBase      = 0;
  Layout->WindowSize      = 0;
  Layout->WindowAfterRam  = FALSE;

  if (PhysBase < NWOAS_GUEST_PHYS_BASE_MIN) {
    return NwoasGuestRamErrPhysBaseLow;
  }
  if (((PhysBase & NWOAS_DART_PAGE_MASK) != 0) || ((MemSize & NWOAS_DART_PAGE_MASK) != 0)) {
    return NwoasGuestRamErrUnaligned;
  }
  if (MemSize == 0) {
    return NwoasGuestRamErrMemSizeZero;
  }

  if (WindowAfterRam) {
    //
    // S102: the hv already removed the window backing from mem_size.  Subtract NOTHING.
    //
    GuestRamSize = MemSize;
  } else {
    //
    // Legacy: the backing is the top NWOAS_DMA_WIN_SIZE of mem_size.  Subtract exactly once,
    // and only when it cannot underflow (strictly greater, so guest RAM stays non-empty).
    //
    if (MemSize <= NWOAS_DMA_WIN_SIZE) {
      return NwoasGuestRamErrMemSizeTooSmall;
    }
    GuestRamSize = MemSize - NWOAS_DMA_WIN_SIZE;
  }

  if (PhysBase > (MAX_UINT64 - GuestRamSize)) {
    return NwoasGuestRamErrOverflow;
  }
  GuestRamEnd = PhysBase + GuestRamSize;

  if (GuestRamEnd > (MAX_UINT64 - NWOAS_DMA_WIN_SIZE)) {
    return NwoasGuestRamErrOverflow;
  }
  BackingEnd = GuestRamEnd + NWOAS_DMA_WIN_SIZE;

  if (BackingEnd > NWOAS_GUEST_PA_LIMIT) {
    return NwoasGuestRamErrAboveLimit;
  }

  Layout->PhysBase        = PhysBase;
  Layout->BootArgsMemSize = MemSize;
  Layout->GuestRamSize    = GuestRamSize;
  Layout->GuestRamEnd     = GuestRamEnd;
  Layout->BackingPa       = GuestRamEnd;
  Layout->BackingEnd      = BackingEnd;
  Layout->WindowBase      = NWOAS_DMA_WIN_BASE;
  Layout->WindowSize      = NWOAS_DMA_WIN_SIZE;
  Layout->WindowAfterRam  = WindowAfterRam;
  return NwoasGuestRamOk;
}

/**
  Compute the layout in the build's configured mode (NWOAS_WINDOW_AFTER_RAM).
  This is the entry point every firmware consumer must use.
**/
static inline
NWOAS_GUEST_RAM_STATUS
NwoasComputeGuestRamLayout (
  UINT64                  PhysBase,
  UINT64                  MemSize,
  NWOAS_GUEST_RAM_LAYOUT  *Layout
  )
{
  return NwoasComputeGuestRamLayoutEx (
           PhysBase,
           MemSize,
           (BOOLEAN)(NWOAS_WINDOW_AFTER_RAM != 0),
           Layout
           );
}

/**
  Convenience: the window backing PA in the configured mode, or 0 if boot_args is invalid.
  0 matches the pre-existing "backing unknown -> feature disabled" idiom in the DXE drivers.
**/
static inline
UINT64
NwoasGuestRamBackingPa (
  UINT64  PhysBase,
  UINT64  MemSize
  )
{
  NWOAS_GUEST_RAM_LAYOUT  Layout;

  if (NwoasComputeGuestRamLayout (PhysBase, MemSize, &Layout) != NwoasGuestRamOk) {
    return 0;
  }
  return Layout.BackingPa;
}

/**
  Short ASCII name for a status, for DEBUG lines.  Never returns NULL.
**/
static inline
CONST CHAR8 *
NwoasGuestRamStatusName (
  NWOAS_GUEST_RAM_STATUS  Status
  )
{
  switch (Status) {
    case NwoasGuestRamOk:                 return "ok";
    case NwoasGuestRamErrNullOut:         return "null-out";
    case NwoasGuestRamErrPhysBaseLow:     return "phys_base-below-32GiB";
    case NwoasGuestRamErrUnaligned:       return "not-16KiB-aligned";
    case NwoasGuestRamErrMemSizeZero:     return "mem_size-zero";
    case NwoasGuestRamErrMemSizeTooSmall: return "mem_size<=window(legacy-underflow)";
    case NwoasGuestRamErrOverflow:        return "uint64-overflow";
    case NwoasGuestRamErrAboveLimit:      return "backing-end-above-64GiB";
    default:                              return "unknown";
  }
}

//
// Range predicates.  Half-open intervals throughout.
//

static inline
BOOLEAN
NwoasPaInLowWindow (
  UINT64  Pa
  )
{
  return (BOOLEAN)((Pa >= NWOAS_DMA_WIN_BASE) && (Pa < NWOAS_DMA_WIN_END));
}

static inline
BOOLEAN
NwoasPaInGuestRam (
  CONST NWOAS_GUEST_RAM_LAYOUT  *Layout,
  UINT64                        Pa
  )
{
  if ((Layout == NULL) || (Layout->GuestRamSize == 0)) {
    return FALSE;
  }
  return (BOOLEAN)((Pa >= Layout->PhysBase) && (Pa < Layout->GuestRamEnd));
}

static inline
BOOLEAN
NwoasPaInBacking (
  CONST NWOAS_GUEST_RAM_LAYOUT  *Layout,
  UINT64                        Pa
  )
{
  if ((Layout == NULL) || (Layout->BackingPa == 0)) {
    return FALSE;
  }
  return (BOOLEAN)((Pa >= Layout->BackingPa) && (Pa < Layout->BackingEnd));
}

/**
  Translate a window IPA to the real backing PA (the DART's own page tables and the
  per-Map path need this).  Addresses outside the window pass through unchanged, which is
  the identity behaviour the previous NWOAS_WIN2BACK macro had.  If the layout is invalid
  (BackingPa == 0) everything passes through, again matching the old NwoasBacking==0 path.
**/
static inline
UINT64
NwoasWindowIpaToBackingPa (
  CONST NWOAS_GUEST_RAM_LAYOUT  *Layout,
  UINT64                        Ipa
  )
{
  if ((Layout == NULL) || (Layout->BackingPa == 0) || !NwoasPaInLowWindow (Ipa)) {
    return Ipa;
  }
  return Layout->BackingPa + (Ipa - NWOAS_DMA_WIN_BASE);
}

#endif // NWOAS_GUEST_RAM_H_
