/** @file
  Split a Conventional-memory descriptor at an experimental high-RAM limit.
  Only the caller's snapshot changes; no allocation or live map mutation.
  SPDX-License-Identifier: (BSD-2-Clause-Patent OR MIT)
**/
#ifndef NWOAS_SPLIT_MEMORY_MAP_H_
#define NWOAS_SPLIT_MEMORY_MAP_H_

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

STATIC inline EFI_STATUS
NwoasSplitMemoryMap (
  IN OUT EFI_MEMORY_DESCRIPTOR *Map,
  IN OUT UINTN                 *MapBytes,
  IN UINTN                     Capacity,
  IN UINTN                     DescSize,
  IN UINT64                    KeepStart,
  IN UINT64                    KeepEnd,
  OUT UINTN                    *SplitCount
  )
{
  UINTN Count, Index, Found = 0, SplitIndex = 0;
  EFI_MEMORY_DESCRIPTOR *Desc;
  UINT64 End, Bytes, Offset;

  if ((Map == NULL) || (MapBytes == NULL) || (SplitCount == NULL) ||
      (DescSize < sizeof (EFI_MEMORY_DESCRIPTOR)) || ((DescSize & 7) != 0) ||
      ((*MapBytes % DescSize) != 0) || (*MapBytes > Capacity) ||
      (KeepStart >= KeepEnd) || ((KeepStart | KeepEnd) & (EFI_PAGE_SIZE - 1))) {
    return EFI_INVALID_PARAMETER;
  }
  *SplitCount = 0;
  Count = *MapBytes / DescSize;

  // Preflight the whole snapshot before modifying a byte. At most one
  // non-overlapping physical descriptor may straddle this single boundary.
  for (Index = 0; Index < Count; Index++) {
    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    if ((Desc->Type != EfiConventionalMemory) ||
        (Desc->PhysicalStart < KeepStart) || (Desc->PhysicalStart >= KeepEnd)) {
      continue;
    }
    if ((Desc->PhysicalStart & (EFI_PAGE_SIZE - 1)) ||
        (Desc->NumberOfPages > (MAX_UINT64 - Desc->PhysicalStart) / EFI_PAGE_SIZE)) {
      return EFI_COMPROMISED_DATA;
    }
    Bytes = Desc->NumberOfPages * EFI_PAGE_SIZE;
    End = Desc->PhysicalStart + Bytes;
    if (End <= KeepEnd) {
      continue;
    }
    Offset = KeepEnd - Desc->PhysicalStart;
    if ((Desc->VirtualStart != 0) && (Desc->VirtualStart > MAX_UINT64 - Offset)) {
      return EFI_COMPROMISED_DATA;
    }
    SplitIndex = Index;
    if (++Found > 1) {
      return EFI_COMPROMISED_DATA;
    }
  }
  if (Found == 0) {
    return EFI_SUCCESS;
  }
  if (*MapBytes > MAX_UINTN - DescSize) {
    return EFI_OUT_OF_RESOURCES;
  }
  if (Capacity - *MapBytes < DescSize) {
    *MapBytes += DescSize;
    return EFI_BUFFER_TOO_SMALL; // Caller retries; never drop the upper tail.
  }

  Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + SplitIndex * DescSize);
  Offset = KeepEnd - Desc->PhysicalStart;
  // CopyMem explicitly supports overlap. Preserve descriptor extension bytes.
  CopyMem ((UINT8 *)Desc + 2 * DescSize, (UINT8 *)Desc + DescSize,
           (Count - SplitIndex - 1) * DescSize);
  EFI_MEMORY_DESCRIPTOR *Tail = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescSize);
  CopyMem (Tail, Desc, DescSize);
  Tail->PhysicalStart = KeepEnd;
  Tail->NumberOfPages -= Offset / EFI_PAGE_SIZE;
  if (Tail->VirtualStart != 0) {
    Tail->VirtualStart += Offset;
  }
  Desc->NumberOfPages = Offset / EFI_PAGE_SIZE;
  *MapBytes += DescSize;
  *SplitCount = 1;
  return EFI_SUCCESS;
}

#endif
