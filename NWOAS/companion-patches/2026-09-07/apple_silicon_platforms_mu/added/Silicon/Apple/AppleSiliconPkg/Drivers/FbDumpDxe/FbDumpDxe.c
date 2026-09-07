/**
  FbDumpDxe.c — GOP framebuffer serial dump on ResetSystem.

  At DXE entry this driver locates the EFI_GRAPHICS_OUTPUT_PROTOCOL and caches
  the framebuffer base address and geometry.  It then registers a callback via
  EFI_RESET_NOTIFICATION_PROTOCOL so that when bootmgfw calls
  gRT->ResetSystem(EfiResetShutdown, ...) the callback runs FIRST, 2x-down-
  samples the live framebuffer, RLE-encodes it, and emits the result to the
  Apple UART serial port.

  Output format — each line starts with "HVLOG: " for grep/m1n1-vuart filtering:

    HVLOG: FBDUMP START w=<w> h=<h> bpp=32
    HVLOG: FB <c0><p0><c1><p1><c2><p2><c3><p3><c4><p4><c5><p5><c6><p6><c7><p7>
    ...
    HVLOG: FBDUMP END pairs=<n>

  Each pair is a 16-bit run-count followed by a 32-bit BGRA pixel, printed as
  fixed-width hex with no separators (4 + 8 chars per pair, 8 pairs per line).

  A (0000, 00000000) pair is used as a line-padding sentinel on the final
  partial line; the decoder should stop at the pair count in the END line.

  Copyright (c) 2025, NWOAS contributors.  All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/ResetNotification.h>

#include "GoldenWinload.h"  // NWOAS H2: golden hashes of DECOMPRESSED winload.efi 16KB pages (SET B, tiny)
#include "GoldenHashes.h"   // NWOAS H1: golden hashes of boot.wim 16KB pages (SET A)

//
// Framebuffer parameters captured at driver entry.
// Written once during initialization; read-only during the reset callback.
// No locking is required: reset notifications are not invoked re-entrantly
// on the first call path (MAX_RESET_NOTIFY_DEPTH guards deeper recursion).
//
STATIC EFI_PHYSICAL_ADDRESS  mFbBase              = 0;
STATIC UINT32                 mFbWidth             = 0;
STATIC UINT32                 mFbHeight            = 0;
STATIC UINT32                 mFbPixelsPerScanLine = 0;
STATIC BOOLEAN                mFbValid             = FALSE;

// NWOAS H2: FNV-1a of a 16KB page (matches the host golden generator).
STATIC UINT32 FbFnvPage16k (CONST UINT8 *p) {
  UINT32 h = 2166136261u;
  UINTN  i;
  for (i = 0; i < 0x4000; i++) { h = (h ^ p[i]) * 16777619u; }
  return h;
}

/**
  Write a formatted ASCII string directly to the Apple UART serial port.

  Bypasses the DEBUG() level-filter so that the dump is emitted regardless
  of future changes to PcdDebugPrintErrorLevel.

  @param[in]  Fmt   printf-style ASCII format string.
  @param[in]  ...   Optional arguments.
**/
STATIC VOID
FbSerial (
  IN CONST CHAR8  *Fmt,
  ...
  )
{
  CHAR8    Buf[256];
  VA_LIST  Marker;
  UINTN    Len;

  VA_START (Marker, Fmt);
  Len = AsciiVSPrint (Buf, sizeof (Buf), Fmt, Marker);
  VA_END (Marker);

  SerialPortWrite ((UINT8 *)Buf, Len);
}

/**
  Flush exactly 8 accumulated RLE pairs to the serial port as one line.

  @param[in]  Counts    Array of 8 run-counts (UINT16 widened to UINTN).
  @param[in]  Pixels    Array of 8 pixel values (UINT32 widened to UINTN).
**/
STATIC VOID
FlushPairLine (
  IN UINTN  *Counts,
  IN UINTN  *Pixels
  )
{
  CHAR8  Buf[256];
  UINTN  Len;

  Len = AsciiSPrint (
          Buf, sizeof (Buf),
          "HVLOG: FB "
          "%04x%08x%04x%08x%04x%08x%04x%08x"
          "%04x%08x%04x%08x%04x%08x%04x%08x\n",
          Counts[0], Pixels[0],
          Counts[1], Pixels[1],
          Counts[2], Pixels[2],
          Counts[3], Pixels[3],
          Counts[4], Pixels[4],
          Counts[5], Pixels[5],
          Counts[6], Pixels[6],
          Counts[7], Pixels[7]
          );
  SerialPortWrite ((UINT8 *)Buf, Len);
}

/**
  EFI_RESET_SYSTEM callback registered with EFI_RESET_NOTIFICATION_PROTOCOL.

  Called by ResetSystemRuntimeDxe before performing the actual hardware reset,
  while still in pre-ExitBootServices DXE context.  Reads the cached framebuffer,
  2x-downsamples it in both axes (reducing 640×1136 to 320×568), RLE-encodes the
  result as 32-bit BGRA pixels, and emits every pair to the Apple UART serial
  port in the HVLOG: format.

  This function is deliberately NOT called on EfiResetWarm to avoid spamming
  the serial log during the MemoryTypeInformation warm-reset loop; add
  EfiResetWarm to the filter below if needed.

  @param[in]  ResetType    Type of reset being performed.
  @param[in]  ResetStatus  Caller-supplied status for the reset.
  @param[in]  DataSize     Size in bytes of ResetData.
  @param[in]  ResetData    Optional Null-terminated reset reason string.
**/
STATIC VOID
EFIAPI
FbDumpResetNotify (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData  OPTIONAL
  )
{
  UINT32  DstW;
  UINT32  DstH;
  UINT32  SrcByteStride;  // bytes from start of scanline N to start of scanline N+1
  UINT32  x;
  UINT32  y;
  UINT32  *Row;
  UINT32  Pixel;
  UINT32  LastPixel;
  UINT16  RunLen;
  UINTN   nPairs;
  UINTN   PairIdx;

  // Staging arrays: widened to UINTN so they match AsciiSPrint's %x consumption
  UINTN   PairCount[8];
  UINTN   PairPixel[8];

  if (!mFbValid) {
    return;
  }

  //
  // Dump on Shutdown (bootmgfw error path) and Cold reset.
  // Skip Warm — those happen during the MemoryTypeInformation correction loop
  // and the screen at that point is the UEFI setup UI, not the Windows error.
  //
  if ((ResetType != EfiResetShutdown) && (ResetType != EfiResetCold)) {
    return;
  }

  //
  // NWOAS H1: find the WIM (MSWIM magic) in guest RAM, re-hash it vs the boot.wim golden (SET A).
  //  found ~= GOLDEN_N => RAMDisk boot.wim INTACT at failure => corruption downstream (decompress/read)
  //  found <  GOLDEN_N => boot.wim pages corrupt/clobbered post-DMA
  //  NO-MSWIM => boot.wim not present as a contiguous WIM in RAM at shutdown
  //
  {
    STATIC UINT8 mH1Cov[(GOLDEN_N + 7) / 8];
    UINT64 pa, wimBase = 0;
    UINTN  nWim = 0, found1 = 0, zi, ii;
    for (pa = 0x83C038000ULL; (pa + 8ULL) <= 0xBE0FC0000ULL; pa += 0x200ULL) {
      CONST UINT8 *m = (CONST UINT8 *)(UINTN)pa;
      if ((m[0] == 'M') && (m[1] == 'S') && (m[2] == 'W') && (m[3] == 'I') && (m[4] == 'M')) {
        nWim++;
        FbSerial ("HVLOG: WIMFOUND #%u at 0x%lx\n", (UINTN)nWim, (UINT64)pa);
        if (wimBase == 0) { wimBase = pa; }
        if (nWim >= 12) { break; }
      }
    }
    if (wimBase != 0) {
      for (zi = 0; zi < sizeof (mH1Cov); zi++) { mH1Cov[zi] = 0; }
      for (ii = 0; ii < GOLDEN_N; ii++) {
        UINT32 h = FbFnvPage16k ((CONST UINT8 *)(UINTN)(wimBase + (UINT64)ii * 0x4000ULL));
        INTN lo = 0, hi = (INTN)GOLDEN_N - 1, f = -1;
        while (lo <= hi) {
          INTN mid = (lo + hi) / 2;
          if (mGolden[mid] == h) { f = mid; break; }
          if (mGolden[mid] < h) { lo = mid + 1; } else { hi = mid - 1; }
        }
        if ((f >= 0) && ((mH1Cov[f >> 3] & (1u << (f & 7))) == 0)) {
          mH1Cov[f >> 3] |= (UINT8)(1u << (f & 7));
          found1++;
        }
      }
      FbSerial ("HVLOG: RESETCOV wimbase=0x%lx found=%u/%u nwim=%u\n", (UINT64)wimBase, (UINTN)found1, (UINTN)GOLDEN_N, (UINTN)nWim);
    } else {
      FbSerial ("HVLOG: RESETCOV NO-MSWIM (boot.wim not a contiguous WIM in RAM)\n");
    }
  }

  //
  // NWOAS H2: scan guest UEFI RAM (4KB steps) for pages of the DECOMPRESSED winload.efi (SET B).
  //  found ~= GOLDEN_WL_N => the decompressed winload is present + byte-correct in memory
  //                          => corruption is bootmgfw mis-reading/mis-computing, not decompression
  //  found much < GOLDEN_WL_N (but >0) => the decompressed winload buffer is CORRUPT (wrong bytes)
  //  found == 0 => winload buffer freed/absent at shutdown (H2 inconclusive; rely on other signals)
  //
  {
    UINT64 pa, firstAddr = 0;
    UINTN  found = 0, zi;
    UINT8  wlcov[(GOLDEN_WL_N + 7) / 8];
    for (zi = 0; zi < sizeof (wlcov); zi++) { wlcov[zi] = 0; }
    for (pa = 0x83C038000ULL; (pa + 0x4000ULL) <= 0xBE0FC0000ULL; pa += 0x1000ULL) {
      UINT32 h = FbFnvPage16k ((CONST UINT8 *)(UINTN)pa);
      INTN lo = 0, hi = (INTN)GOLDEN_WL_N - 1, f = -1;
      while (lo <= hi) {
        INTN mid = (lo + hi) / 2;
        if (mGoldenWl[mid] == h) { f = mid; break; }
        if (mGoldenWl[mid] < h) { lo = mid + 1; } else { hi = mid - 1; }
      }
      if ((f >= 0) && ((wlcov[f >> 3] & (1u << (f & 7))) == 0)) {
        wlcov[f >> 3] |= (UINT8)(1u << (f & 7));
        found++;
        if (firstAddr == 0) { firstAddr = pa; }
      }
    }
    FbSerial ("HVLOG: WLCOV found=%u/%u firstAddr=0x%lx\n", (UINTN)found, (UINTN)GOLDEN_WL_N, (UINT64)firstAddr);
  }

  DstW          = mFbWidth  / 2;
  DstH          = mFbHeight / 2;
  SrcByteStride = mFbPixelsPerScanLine * 4;

  FbSerial ("HVLOG: FBDUMP START w=%u h=%u bpp=32\n", (UINTN)DstW, (UINTN)DstH);

  nPairs    = 0;
  PairIdx   = 0;
  LastPixel = 0xDEADBEEF;  // impossible initial sentinel; forces first pixel to open a run
  RunLen    = 0;

  for (y = 0; y < DstH; y++) {
    //
    // Source row: pick every second scanline (rows 0, 2, 4, ...) to downsample Y.
    // Cast via UINT8 * to do byte arithmetic without aliasing issues, then
    // reinterpret as UINT32 * for 32-bit pixel access.
    //
    Row = (UINT32 *)((UINT8 *)(UINTN)mFbBase + (UINTN)(y * 2) * SrcByteStride);

    for (x = 0; x < DstW; x++) {
      //
      // Pick every second pixel in the row (columns 0, 2, 4, ...) to downsample X.
      //
      Pixel = Row[x * 2];

      if ((Pixel == LastPixel) && (RunLen < 0xFFFF)) {
        RunLen++;
      } else {
        //
        // Current run ended.  Store the completed run in the staging arrays.
        //
        if (RunLen > 0) {
          PairCount[PairIdx] = (UINTN)RunLen;
          PairPixel[PairIdx] = (UINTN)LastPixel;
          PairIdx++;
          nPairs++;

          if (PairIdx == 8) {
            FlushPairLine (PairCount, PairPixel);
            PairIdx = 0;
          }
        }

        //
        // Start a new run with the current pixel.
        //
        LastPixel = Pixel;
        RunLen    = 1;
      }
    }
  }

  //
  // Commit the final open run.
  //
  if (RunLen > 0) {
    PairCount[PairIdx] = (UINTN)RunLen;
    PairPixel[PairIdx] = (UINTN)LastPixel;
    PairIdx++;
    nPairs++;
  }

  //
  // Flush the partial final line.  Pad unused slots with (0000, 00000000).
  // The decoder must stop at the pair count in the FBDUMP END line, not at
  // the padding zeros.
  //
  if (PairIdx > 0) {
    while (PairIdx < 8) {
      PairCount[PairIdx] = 0;
      PairPixel[PairIdx] = 0;
      PairIdx++;
    }
    FlushPairLine (PairCount, PairPixel);
  }

  FbSerial ("HVLOG: FBDUMP END pairs=%u\n", nPairs);
}

/**
  Driver entry point.

  Locates the EFI Graphics Output Protocol, caches the framebuffer parameters,
  then registers FbDumpResetNotify via EFI_RESET_NOTIFICATION_PROTOCOL.

  @param[in]  ImageHandle   Firmware-allocated handle for this image.
  @param[in]  SystemTable   Pointer to the EFI System Table.

  @retval EFI_SUCCESS           Driver initialized; reset callback registered.
  @retval EFI_NOT_FOUND         GOP not available; cannot capture framebuffer.
  @retval other                 Unexpected error from LocateProtocol.
**/
EFI_STATUS
EFIAPI
FbDumpDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                        Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL      *Gop;
  EFI_RESET_NOTIFICATION_PROTOCOL   *ResetNotify;

  //
  // 1. Locate the Graphics Output Protocol.
  //    SimpleFbDxe installs it; our DEPEX on gEfiGraphicsOutputProtocolGuid
  //    guarantees it is present before we run.
  //
  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&Gop
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "FbDumpDxe: LocateProtocol(GOP) failed: %r -- cannot cache FB\n",
      Status
      ));
    return Status;
  }

  mFbBase              = Gop->Mode->FrameBufferBase;
  mFbWidth             = Gop->Mode->Info->HorizontalResolution;
  mFbHeight            = Gop->Mode->Info->VerticalResolution;
  mFbPixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;
  mFbValid             = TRUE;

  DEBUG ((
    DEBUG_ERROR,
    "FbDumpDxe: cached FB base=0x%016llx w=%u h=%u ppl=%u fmt=%u\n",
    (UINT64)mFbBase,
    mFbWidth,
    mFbHeight,
    mFbPixelsPerScanLine,
    (UINT32)Gop->Mode->Info->PixelFormat
    ));

  //
  // 2. Register the reset notification callback.
  //    MdeModulePkg/Universal/ResetSystemRuntimeDxe installs
  //    EFI_RESET_NOTIFICATION_PROTOCOL; our DEPEX on
  //    gEfiResetNotificationProtocolGuid ensures it is present.
  //
  Status = gBS->LocateProtocol (
                  &gEfiResetNotificationProtocolGuid,
                  NULL,
                  (VOID **)&ResetNotify
                  );
  if (EFI_ERROR (Status)) {
    //
    // Non-fatal: framebuffer parameters are cached, but the automatic
    // on-reset dump will not happen.  Manual investigation still possible.
    //
    DEBUG ((
      DEBUG_ERROR,
      "FbDumpDxe: LocateProtocol(ResetNotification) failed: %r -- no auto-dump\n",
      Status
      ));
    return EFI_SUCCESS;
  }

  Status = ResetNotify->RegisterResetNotify (ResetNotify, FbDumpResetNotify);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "FbDumpDxe: RegisterResetNotify failed: %r\n",
      Status
      ));
  } else {
    DEBUG ((
      DEBUG_ERROR,
      "FbDumpDxe: reset notification registered; "
      "FB will be dumped on next ResetSystem(Shutdown/Cold)\n"
      ));
  }

  return EFI_SUCCESS;
}
