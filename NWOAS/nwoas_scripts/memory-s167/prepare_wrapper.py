#!/usr/bin/env python3
"""Generate an isolated S167 wrapper candidate. No source-tree/hardware mutation."""
from pathlib import Path
import difflib
R=Path('/Volumes/X31/NWOAS'); O=R/'nwoas_scripts/memory-s167'
rel='Silicon/Apple/AppleSiliconPkg/Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c'
s=(R/'apple_silicon_platforms_mu'/rel).read_text()
# Apply only the S161 change belonging to this file; other S161 files are staged by builder.
s=s.replace('#include <Library/AppleDTLib.h>', '#include <Library/AppleDTLib.h>\n#include <Library/NwoasGuestRam.h>')
a=s.index('  // S102: the hv excludes'); b=s.index('\n\n',a)
s=s[:a]+'  Backing = NwoasGuestRamBackingPa (PhysBase, MemSize);'+s[b:]
base=s
s=s.replace('#include <Library/NwoasGuestRam.h>', '#include <Library/NwoasGuestRam.h>\n#include <Library/NwoasSplitMemoryMap.h>')
s=s.replace('STATIC EFI_EVENT  mReadyToBootEvent', '''// S167: default zero retains the S161 cap. Candidate enables the first 4 GiB
// of validated high RAM; this is separate from the indispensable low DMA pool.
#ifndef NWOAS_HIDE_KEEP_HIGH_BYTES
#define NWOAS_HIDE_KEEP_HIGH_BYTES 0ULL
#endif
#if NWOAS_HIDE_KEEP_HIGH_BYTES && (!NWOAS_HIDE_USE_WRAP || NWOAS_HIDE_FULL)
#error S167 requires conservative wrapper mode
#endif
#if NWOAS_HIDE_KEEP_HIGH_BYTES
STATIC UINT64 mKeepHighStart = 0;
STATIC UINT64 mKeepHighEnd = 0;
#endif

STATIC EFI_EVENT  mReadyToBootEvent''')
s=s.replace('  BOOLEAN                AbortGuard;', '''  BOOLEAN                AbortGuard;
#if NWOAS_HIDE_KEEP_HIGH_BYTES
  UINTN                  Capacity = (MemoryMapSize != NULL) ? *MemoryMapSize : 0;
  UINTN                  SplitCount = 0;
  UINT64                 KeptPages = 0;
#endif''',1)
needle='''  //
  // Only doctor a genuine FILL.'''
s=s.replace(needle,'''#if NWOAS_HIDE_KEEP_HIGH_BYTES
  // Reserve room for one boundary split, without allocating or changing MapKey.
  if ((Status == EFI_BUFFER_TOO_SMALL) && (MemoryMapSize != NULL) &&
      (DescriptorSize != NULL) && (*DescriptorSize >= sizeof (EFI_MEMORY_DESCRIPTOR)) &&
      (mKeepHighEnd > mKeepHighStart)) {
    if (*MemoryMapSize > MAX_UINTN - *DescriptorSize) {
      return EFI_OUT_OF_RESOURCES;
    }
    *MemoryMapSize += *DescriptorSize;
    return Status;
  }
#endif

'''+needle,1)
needle='''  if (!AbortGuard) {
    Desc = MemoryMap;'''
s=s.replace(needle,'''  if (!AbortGuard) {
#if NWOAS_HIDE_KEEP_HIGH_BYTES
    if (mKeepHighEnd > mKeepHighStart) {
      Status = NwoasSplitMemoryMap (MemoryMap, MemoryMapSize, Capacity, DescSize,
                                   mKeepHighStart, mKeepHighEnd, &SplitCount);
      if (EFI_ERROR (Status)) {
        return Status; // No descriptor is hidden or discarded on retry/error.
      }
      Count = *MemoryMapSize / DescSize;
    }
#endif
    Desc = MemoryMap;''',1)
needle='''        Desc->Type  = EfiReservedMemoryType;   // snapshot-only edit'''
s=s.replace(needle,'''#if NWOAS_HIDE_KEEP_HIGH_BYTES
        if ((Desc->Type == EfiConventionalMemory) &&
            (Desc->PhysicalStart >= mKeepHighStart) &&
            (Desc->PhysicalStart < mKeepHighEnd) &&
            (Desc->NumberOfPages <= (mKeepHighEnd - Desc->PhysicalStart) / EFI_PAGE_SIZE)) {
          KeptPages += Desc->NumberOfPages;
          Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescSize);
          continue;
        }
#endif
'''+needle,1)
s=s.replace('''    mWrapLogged = TRUE;
    DEBUG''','''    mWrapLogged = TRUE;
#if NWOAS_HIDE_KEEP_HIGH_BYTES
    DEBUG ((DEBUG_ERROR,
      "HVLOG: S167 high_keep=[0x%lx,0x%lx) kept_pages=0x%lx split=%lu hidden_pages=0x%lx guard=%a\\n",
      mKeepHighStart, mKeepHighEnd, KeptPages, (UINT64)SplitCount,
      AbortGuard ? 0 : HideablePages - KeptPages, AbortGuard ? "fired" : "clear"));
#endif
    DEBUG''',1)
needle='  Backing = NwoasGuestRamBackingPa (PhysBase, MemSize);'
s=s.replace(needle,needle+'''
#if NWOAS_HIDE_KEEP_HIGH_BYTES
  // S161 validates alignment, overflow, guest end and disjoint backing.
  if ((Backing > PhysBase) &&
      ((NWOAS_HIDE_KEEP_HIGH_BYTES & (EFI_PAGE_SIZE - 1)) == 0) &&
      (NWOAS_HIDE_KEEP_HIGH_BYTES <= Backing - PhysBase)) {
    mKeepHighStart = PhysBase;
    mKeepHighEnd = PhysBase + NWOAS_HIDE_KEEP_HIGH_BYTES;
  } else {
    DEBUG ((DEBUG_ERROR, "HVLOG: S167 invalid keep extent; retain S161 cap\\n"));
  }
#endif''',1)
s=s.replace('      (HideablePages * EFI_PAGE_SIZE) / (1024 * 1024),', '''#if NWOAS_HIDE_KEEP_HIGH_BYTES
      ((AbortGuard ? 0 : HideablePages - KeptPages) * EFI_PAGE_SIZE) / (1024 * 1024),
#else
      (HideablePages * EFI_PAGE_SIZE) / (1024 * 1024),
#endif''',1)
(O/'NwoasHideHighRamDxe-s161.c').write_text(base)
(O/'NwoasHideHighRamDxe-s167.c').write_text(s)
(O/'wrapper-from-s161.patch').write_text(''.join(difflib.unified_diff(base.splitlines(True),s.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel)))
print('Prepared isolated S167 wrapper; not deployed')
