
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <Uefi.h>
#include <Library/NwoasSplitMemoryMap.h>
#define DEBUG(x) ((void)0)
#define NWOAS_HIDE_BASE 0x100000000ULL
#define NWOAS_MIN_LOW_FREE_PAGES 0x80000ULL
#define NWOAS_HIDE_FULL 0
static BOOLEAN mWrapLogged;
#if NWOAS_HIDE_KEEP_HIGH_BYTES
static UINT64 mKeepHighStart=0x83B7E0000ULL,mKeepHighEnd=0x93B7E0000ULL;
#endif
static BOOLEAN NwoasTypeIsHideable(UINT32 t) { return t==EfiConventionalMemory; }
typedef struct { EFI_MEMORY_DESCRIPTOR d; UINT64 extension; } Ext;
static Ext original[3];
static EFI_STATUS mOrigGetMemoryMap(UINTN *n,EFI_MEMORY_DESCRIPTOR *m,UINTN *key,UINTN *ds,UINT32 *ver) {
  *ds=sizeof(Ext); *ver=1; *key=0x1234;
  if (!m || *n<sizeof(original)) {*n=sizeof(original);return EFI_BUFFER_TOO_SMALL;}
  memcpy(m,original,sizeof(original));*n=sizeof(original);return EFI_SUCCESS;
}
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
#if NWOAS_HIDE_KEEP_HIGH_BYTES
  UINTN                  Capacity = (MemoryMapSize != NULL) ? *MemoryMapSize : 0;
  UINTN                  SplitCount = 0;
  UINT64                 KeptPages = 0;
#endif

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

#if NWOAS_HIDE_KEEP_HIGH_BYTES
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
    Desc = MemoryMap;
    for (i = 0; i < Count; i++) {
      if ((Desc->PhysicalStart >= NWOAS_HIDE_BASE) &&
          NwoasTypeIsHideable (Desc->Type)) {
#if NWOAS_HIDE_KEEP_HIGH_BYTES
        if ((Desc->Type == EfiConventionalMemory) &&
            (Desc->PhysicalStart >= mKeepHighStart) &&
            (Desc->PhysicalStart < mKeepHighEnd) &&
            (Desc->NumberOfPages <= (mKeepHighEnd - Desc->PhysicalStart) / EFI_PAGE_SIZE)) {
          KeptPages += Desc->NumberOfPages;
          Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescSize);
          continue;
        }
#endif
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
#if NWOAS_HIDE_KEEP_HIGH_BYTES
    DEBUG ((DEBUG_ERROR,
      "HVLOG: S167 high_keep=[0x%lx,0x%lx) kept_pages=0x%lx split=%lu hidden_pages=0x%lx guard=%a\n",
      mKeepHighStart, mKeepHighEnd, KeptPages, (UINT64)SplitCount,
      AbortGuard ? 0 : HideablePages - KeptPages, AbortGuard ? "fired" : "clear"));
#endif
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
#if NWOAS_HIDE_KEEP_HIGH_BYTES
      ((AbortGuard ? 0 : HideablePages - KeptPages) * EFI_PAGE_SIZE) / (1024 * 1024),
#else
      (HideablePages * EFI_PAGE_SIZE) / (1024 * 1024),
#endif
      FirstHiddenPa,
      (UINT64)NWOAS_HIDE_BASE,
      NWOAS_HIDE_FULL));
  }

  return Status;
}
int main(void) {
 Ext out[5], snapshot[5]; UINTN n,key,ds; UINT32 v; EFI_STATUS st;
 original[0]=(Ext){{EfiConventionalMemory,0,0,0x100000,EFI_MEMORY_WB},0xaabb};
 original[1]=(Ext){{EfiConventionalMemory,0x83B7E0000ULL,0,0x180000,EFI_MEMORY_WB},0xccdd};
 original[2]=(Ext){{EfiACPIReclaimMemory,0xA00000000ULL,0,4,EFI_MEMORY_WB},0xeeff};
 n=0;st=NwoasWrappedGetMemoryMap(&n,NULL,&key,&ds,&v);
 assert(st==EFI_BUFFER_TOO_SMALL && key==0x1234 && ds==sizeof(Ext));
 assert(n==sizeof(original)+(NWOAS_HIDE_KEEP_HIGH_BYTES?sizeof(Ext):0));
 memset(out,0xa5,sizeof(out));n=sizeof(original);
 st=NwoasWrappedGetMemoryMap(&n,(void*)out,&key,&ds,&v);
 if(NWOAS_HIDE_KEEP_HIGH_BYTES) {
  assert(st==EFI_BUFFER_TOO_SMALL && n==4*sizeof(Ext));
  assert(memcmp(out,original,sizeof(original))==0);
 } else { assert(st==EFI_SUCCESS && out[1].d.Type==EfiReservedMemoryType); }
 memset(out,0xa5,sizeof(out));n=sizeof(out);
 st=NwoasWrappedGetMemoryMap(&n,(void*)out,&key,&ds,&v);
 assert(st==EFI_SUCCESS && key==0x1234 && v==1);
 assert(memcmp(&out[0],&original[0],sizeof(Ext))==0);
 if(NWOAS_HIDE_KEEP_HIGH_BYTES) {
  assert(n==4*sizeof(Ext));
  assert(out[1].d.Type==EfiConventionalMemory && out[1].d.NumberOfPages==0x100000);
  assert(out[2].d.Type==EfiReservedMemoryType && out[2].d.PhysicalStart==0x93B7E0000ULL && out[2].d.NumberOfPages==0x80000);
  assert(out[1].extension==0xccdd && out[2].extension==0xccdd);
  assert(memcmp(&out[3],&original[2],sizeof(Ext))==0);
 } else {
  assert(n==sizeof(original)); assert(out[1].d.NumberOfPages==0x180000);
  assert(memcmp(&out[2],&original[2],sizeof(Ext))==0);
 }
 // Insufficient low DMA pool: preserve full original map, including high RAM.
 original[0].d.NumberOfPages=0x7ffff;
 memset(out,0xa5,sizeof(out));memcpy(snapshot,out,sizeof(out));memcpy(snapshot,original,sizeof(original));n=sizeof(out);
 st=NwoasWrappedGetMemoryMap(&n,(void*)out,&key,&ds,&v);
 assert(st==EFI_SUCCESS && n==sizeof(original) && memcmp(out,snapshot,sizeof(out))==0);
 puts("PASS: actual wrapper, probe/retry/split/MapKey/low-DMA guard");
}
