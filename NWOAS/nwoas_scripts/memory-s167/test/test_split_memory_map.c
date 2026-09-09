/** @file
  Host tests for ../Include/Library/NwoasSplitMemoryMap.h.

  Build/run: ./run.sh   (clang, -fsanitize=address,undefined)

  Each map buffer is heap-allocated with exactly Capacity bytes so ASan flags
  any write past Capacity. Descriptors use DescSize 48 by default (40-byte
  EFI_MEMORY_DESCRIPTOR + 8 extension bytes, as real firmware reports) and the
  extension bytes are filled with a per-descriptor pattern.
**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/NwoasSplitMemoryMap.h>

#define KB(x)  ((UINT64)(x) * 1024ULL)
#define MB(x)  ((UINT64)(x) * 1024ULL * 1024ULL)
#define GB(x)  ((UINT64)(x) * 1024ULL * 1024ULL * 1024ULL)
#define PAGES(bytes)  ((UINT64)(bytes) / EFI_PAGE_SIZE)

static int  gFailures = 0;
static int  gChecks   = 0;
static const char *gCurrentTest = "";

#define CHECK(cond, ...) do {                                            \
  gChecks++;                                                             \
  if (!(cond)) {                                                         \
    gFailures++;                                                         \
    printf ("  FAIL [%s] line %d: %s\n    ", gCurrentTest, __LINE__, #cond); \
    printf (__VA_ARGS__);                                                \
    printf ("\n");                                                       \
  }                                                                      \
} while (0)

#define CHECK_EQ_U64(a, b) CHECK ((UINT64)(a) == (UINT64)(b), "%s=0x%" PRIx64 " %s=0x%" PRIx64, #a, (UINT64)(a), #b, (UINT64)(b))
#define CHECK_STATUS(s, e)  CHECK ((s) == (e), "status=0x%" PRIxPTR " expected %s (0x%" PRIxPTR ")", (UINTN)(s), #e, (UINTN)(e))

#define TEST(name) static void name (void); \
  static void name##_run (void) { gCurrentTest = #name; printf ("- %s\n", #name); name (); } \
  static void name (void)

// ---------------------------------------------------------------------------
// Map builder helpers
// ---------------------------------------------------------------------------

typedef struct {
  UINT32  Type;
  UINT64  PhysicalStart;
  UINT64  VirtualStart;
  UINT64  NumberOfPages;
  UINT64  Attribute;
} DESC_SPEC;

typedef struct {
  UINT8   *Buf;        // exactly Capacity bytes
  UINTN   Capacity;
  UINTN   MapBytes;
  UINTN   DescSize;
  UINTN   Count;
  UINT8   *Snapshot;   // copy of Buf[0..Capacity) taken at build time
} TEST_MAP;

static EFI_MEMORY_DESCRIPTOR *
DescAt (const TEST_MAP *M, UINTN Index)
{
  return (EFI_MEMORY_DESCRIPTOR *)(M->Buf + Index * M->DescSize);
}

static UINT8 *
ExtAt (const TEST_MAP *M, UINTN Index)
{
  return M->Buf + Index * M->DescSize + sizeof (EFI_MEMORY_DESCRIPTOR);
}

// Extension pattern for a descriptor: derived from its original index.
static UINT8
ExtPattern (UINTN OriginalIndex, UINTN ByteIndex)
{
  return (UINT8)(0xA0 + OriginalIndex * 0x11 + ByteIndex);
}

// Build a map with Count descriptors and SpareSlots free slots after them.
static void
BuildMap (TEST_MAP *M, const DESC_SPEC *Specs, UINTN Count, UINTN DescSize, UINTN SpareSlots)
{
  UINTN Index, B;
  M->DescSize = DescSize;
  M->Count    = Count;
  M->MapBytes = Count * DescSize;
  M->Capacity = (Count + SpareSlots) * DescSize;
  M->Buf      = (UINT8 *)malloc (M->Capacity ? M->Capacity : 1);
  M->Snapshot = (UINT8 *)malloc (M->Capacity ? M->Capacity : 1);
  memset (M->Buf, 0xEE, M->Capacity);   // canary in unused slots
  for (Index = 0; Index < Count; Index++) {
    EFI_MEMORY_DESCRIPTOR *D = DescAt (M, Index);
    memset (D, 0, sizeof (*D));          // deterministic padding after Type
    D->Type          = Specs[Index].Type;
    D->PhysicalStart = Specs[Index].PhysicalStart;
    D->VirtualStart  = Specs[Index].VirtualStart;
    D->NumberOfPages = Specs[Index].NumberOfPages;
    D->Attribute     = Specs[Index].Attribute;
    for (B = 0; B < DescSize - sizeof (*D); B++) {
      ExtAt (M, Index)[B] = ExtPattern (Index, B);
    }
  }
  memcpy (M->Snapshot, M->Buf, M->Capacity);
}

static void
FreeMap (TEST_MAP *M)
{
  free (M->Buf);
  free (M->Snapshot);
  M->Buf = M->Snapshot = NULL;
}

static int
MapUnchanged (const TEST_MAP *M)
{
  return memcmp (M->Buf, M->Snapshot, M->Capacity) == 0;
}

// Compare descriptor at CurrentIndex against the original spec/ext pattern at OriginalIndex.
static void
CheckDescMatchesOriginal (const TEST_MAP *M, UINTN CurrentIndex, UINTN OriginalIndex)
{
  const UINT8 *Orig = M->Snapshot + OriginalIndex * M->DescSize;
  const UINT8 *Cur  = M->Buf + CurrentIndex * M->DescSize;
  CHECK (memcmp (Orig, Cur, M->DescSize) == 0,
         "descriptor now at %" PRIuPTR " should be byte-identical to original %" PRIuPTR,
         CurrentIndex, OriginalIndex);
}

static void
CheckExtPattern (const TEST_MAP *M, UINTN CurrentIndex, UINTN OriginalIndex)
{
  UINTN B;
  for (B = 0; B < M->DescSize - sizeof (EFI_MEMORY_DESCRIPTOR); B++) {
    CHECK (ExtAt (M, CurrentIndex)[B] == ExtPattern (OriginalIndex, B),
           "ext byte %" PRIuPTR " of slot %" PRIuPTR " = 0x%02x, expected 0x%02x",
           B, CurrentIndex, ExtAt (M, CurrentIndex)[B], ExtPattern (OriginalIndex, B));
  }
}

static EFI_STATUS
Split (TEST_MAP *M, UINT64 KeepStart, UINT64 KeepEnd, UINTN *SplitCount)
{
  return NwoasSplitMemoryMap ((EFI_MEMORY_DESCRIPTOR *)M->Buf, &M->MapBytes,
                              M->Capacity, M->DescSize, KeepStart, KeepEnd, SplitCount);
}

// A realistic-looking 7-entry map. Entry 4 (16MB..4GB conventional) is the
// straddler for KeepEnd = 2GB.
static const DESC_SPEC gBaseMap[] = {
  { EfiBootServicesCode,   0x00000000, 0, PAGES (KB (640)),       EFI_MEMORY_WB },
  { EfiReservedMemoryType, 0x000A0000, 0, PAGES (KB (384)),       EFI_MEMORY_UC },
  { EfiConventionalMemory, 0x00100000, 0, PAGES (MB (15)),        EFI_MEMORY_WB },
  { EfiRuntimeServicesData,0x01000000, 0, PAGES (MB (0)) + 0,     EFI_MEMORY_WB | EFI_MEMORY_RUNTIME }, // zero pages, valid
  { EfiConventionalMemory, 0x01000000, 0, PAGES (GB (4) - MB (16)), EFI_MEMORY_WB },
  { EfiMemoryMappedIO,     0xFEC00000, 0, PAGES (MB (4)),         EFI_MEMORY_UC | EFI_MEMORY_RUNTIME },
  { EfiConventionalMemory, 0x100000000ULL, 0, PAGES (GB (4)),      EFI_MEMORY_WB },
};
#define BASE_COUNT  (sizeof (gBaseMap) / sizeof (gBaseMap[0]))
#define STRADDLER   4

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST (Test_BasicSplit_ExtensionPreserved)
{
  TEST_MAP M;
  UINTN SplitCount = 99;
  EFI_STATUS Status;
  UINTN I;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 1);
  gStubCopyMemCalls = gStubCopyMemOverlapCalls = 0;

  Status = Split (&M, 0, GB (2), &SplitCount);
  CHECK_STATUS (Status, EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (M.MapBytes, (BASE_COUNT + 1) * 48);

  // Entries before the straddler untouched.
  for (I = 0; I < STRADDLER; I++) {
    CheckDescMatchesOriginal (&M, I, I);
  }
  // Head half.
  EFI_MEMORY_DESCRIPTOR *Head = DescAt (&M, STRADDLER);
  EFI_MEMORY_DESCRIPTOR *Tail = DescAt (&M, STRADDLER + 1);
  CHECK_EQ_U64 (Head->Type, EfiConventionalMemory);
  CHECK_EQ_U64 (Head->PhysicalStart, 0x01000000);
  CHECK_EQ_U64 (Head->NumberOfPages, PAGES (GB (2) - MB (16)));
  CHECK_EQ_U64 (Head->Attribute, EFI_MEMORY_WB);
  CHECK_EQ_U64 (Head->VirtualStart, 0);
  CheckExtPattern (&M, STRADDLER, STRADDLER);
  // Tail half.
  CHECK_EQ_U64 (Tail->Type, EfiConventionalMemory);
  CHECK_EQ_U64 (Tail->PhysicalStart, GB (2));
  CHECK_EQ_U64 (Tail->NumberOfPages, PAGES (GB (2)));
  CHECK_EQ_U64 (Tail->Attribute, EFI_MEMORY_WB);
  CHECK_EQ_U64 (Tail->VirtualStart, 0);
  CheckExtPattern (&M, STRADDLER + 1, STRADDLER);
  // Entries after the straddler shifted by one slot, byte-identical.
  for (I = STRADDLER + 1; I < BASE_COUNT; I++) {
    CheckDescMatchesOriginal (&M, I + 1, I);
  }
  // Shift used an overlapping CopyMem (the tail of 2 entries moved by 1 slot).
  CHECK (gStubCopyMemOverlapCalls == 1, "overlap calls=%" PRIuPTR, gStubCopyMemOverlapCalls);
  FreeMap (&M);
}

TEST (Test_OverlappingInsertion_StraddlerFirst)
{
  // Straddler at index 0 with many entries after it: the shift moves N-1
  // descriptors by one slot through an overlapping region.
  DESC_SPEC Specs[6] = {
    { EfiConventionalMemory, MB (1),  0, PAGES (MB (63)), EFI_MEMORY_WB },
    { EfiLoaderData,         MB (64), 0, PAGES (MB (1)),  EFI_MEMORY_WB },
    { EfiLoaderCode,         MB (65), 0, PAGES (MB (1)),  EFI_MEMORY_WB },
    { EfiBootServicesData,   MB (66), 0, PAGES (MB (1)),  EFI_MEMORY_WB },
    { EfiACPIMemoryNVS,      MB (67), 0, PAGES (MB (1)),  EFI_MEMORY_WB | EFI_MEMORY_RUNTIME },
    { EfiReservedMemoryType, MB (68), 0, PAGES (MB (1)),  EFI_MEMORY_UC },
  };
  TEST_MAP M;
  UINTN SplitCount, I;
  BuildMap (&M, Specs, 6, 48, 1);
  CHECK_STATUS (Split (&M, 0, MB (32), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (M.MapBytes, 7 * 48);
  CHECK_EQ_U64 (DescAt (&M, 0)->PhysicalStart, MB (1));
  CHECK_EQ_U64 (DescAt (&M, 0)->NumberOfPages, PAGES (MB (31)));
  CHECK_EQ_U64 (DescAt (&M, 1)->PhysicalStart, MB (32));
  CHECK_EQ_U64 (DescAt (&M, 1)->NumberOfPages, PAGES (MB (32)));
  CheckExtPattern (&M, 0, 0);
  CheckExtPattern (&M, 1, 0);
  for (I = 1; I < 6; I++) {
    CheckDescMatchesOriginal (&M, I + 1, I);
  }
  FreeMap (&M);
}

TEST (Test_OverlappingInsertion_StraddlerLast)
{
  // Straddler is the last entry: shift length is zero; new tail lands in the spare slot.
  DESC_SPEC Specs[3] = {
    { EfiLoaderData,         MB (1),  0, PAGES (MB (1)),   EFI_MEMORY_WB },
    { EfiReservedMemoryType, MB (2),  0, PAGES (MB (1)),   EFI_MEMORY_UC },
    { EfiConventionalMemory, MB (3),  0, PAGES (MB (125)), EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 3, 48, 1);
  gStubCopyMemCalls = gStubCopyMemOverlapCalls = 0;
  CHECK_STATUS (Split (&M, 0, MB (64), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (M.MapBytes, 4 * 48);
  CheckDescMatchesOriginal (&M, 0, 0);
  CheckDescMatchesOriginal (&M, 1, 1);
  CHECK_EQ_U64 (DescAt (&M, 2)->PhysicalStart, MB (3));
  CHECK_EQ_U64 (DescAt (&M, 2)->NumberOfPages, PAGES (MB (61)));
  CHECK_EQ_U64 (DescAt (&M, 3)->PhysicalStart, MB (64));
  CHECK_EQ_U64 (DescAt (&M, 3)->NumberOfPages, PAGES (MB (64)));
  CheckExtPattern (&M, 3, 2);
  CHECK (gStubCopyMemOverlapCalls == 0, "no overlapping copy expected for last-entry split");
  FreeMap (&M);
}

TEST (Test_ExactBoundary_NoSplit)
{
  // Descriptor ends exactly at KeepEnd; next one starts exactly at KeepEnd.
  DESC_SPEC Specs[2] = {
    { EfiConventionalMemory, MB (16), 0, PAGES (GB (2) - MB (16)), EFI_MEMORY_WB },
    { EfiConventionalMemory, GB (2),  0, PAGES (GB (2)),           EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount = 5;
  BuildMap (&M, Specs, 2, 48, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, 2 * 48);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_ExactBoundary_StartsAtKeepStart_Splits)
{
  // PhysicalStart == KeepStart is inside the window and must be split.
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, GB (1), 0, PAGES (GB (2)), EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, GB (1), GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (DescAt (&M, 0)->NumberOfPages, PAGES (GB (1)));
  CHECK_EQ_U64 (DescAt (&M, 1)->PhysicalStart, GB (2));
  CHECK_EQ_U64 (DescAt (&M, 1)->NumberOfPages, PAGES (GB (1)));
  FreeMap (&M);
}

TEST (Test_ExactBoundary_OnePageOverBoundary)
{
  // Smallest possible tail: exactly one page past KeepEnd.
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, MB (16), 0, PAGES (GB (2) - MB (16)) + 1, EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (DescAt (&M, 0)->NumberOfPages, PAGES (GB (2) - MB (16)));
  CHECK_EQ_U64 (DescAt (&M, 1)->PhysicalStart, GB (2));
  CHECK_EQ_U64 (DescAt (&M, 1)->NumberOfPages, 1);
  FreeMap (&M);
}

TEST (Test_NoSplit_NoStraddler)
{
  TEST_MAP M;
  UINTN SplitCount = 7;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 1);
  // KeepEnd = 8GB: entry 6 ends exactly at 8GB, so nothing straddles.
  CHECK_STATUS (Split (&M, 0, GB (8), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, BASE_COUNT * 48);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_NoSplit_NonConventionalStraddlerIgnored)
{
  // MMIO entry straddles 0xFEE00000 but only Conventional memory is eligible.
  TEST_MAP M;
  UINTN SplitCount = 7;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 1);
  CHECK_STATUS (Split (&M, 0xFEC00000, 0xFEE00000, &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_NoSplit_NoSpareCapacityIsFine)
{
  // With no straddler, a full buffer is not an error.
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 0);
  CHECK_STATUS (Split (&M, 0, GB (8), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_NoSplit_EmptyMap)
{
  TEST_MAP M;
  UINTN SplitCount = 3;
  BuildMap (&M, gBaseMap, 0, 48, 0);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, 0);
  FreeMap (&M);
}

TEST (Test_InsufficientCapacity_ExactlyFull)
{
  TEST_MAP M;
  UINTN SplitCount = 9;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 0);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_BUFFER_TOO_SMALL);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, (BASE_COUNT + 1) * 48);   // required length reported
  CHECK (MapUnchanged (&M), "map must be byte-identical on BUFFER_TOO_SMALL");
  FreeMap (&M);
}

TEST (Test_InsufficientCapacity_PartialSlot)
{
  // Capacity has 40 spare bytes: less than one 48-byte descriptor.
  TEST_MAP M;
  UINTN SplitCount = 9;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 0);
  {
    UINT8 *Bigger = (UINT8 *)malloc (M.Capacity + 40);
    UINT8 *BiggerSnap = (UINT8 *)malloc (M.Capacity + 40);
    memcpy (Bigger, M.Buf, M.Capacity);
    memset (Bigger + M.Capacity, 0xEE, 40);
    memcpy (BiggerSnap, Bigger, M.Capacity + 40);
    free (M.Buf); free (M.Snapshot);
    M.Buf = Bigger; M.Snapshot = BiggerSnap; M.Capacity += 40;
  }
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_BUFFER_TOO_SMALL);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, (BASE_COUNT + 1) * 48);
  CHECK (MapUnchanged (&M), "map must be byte-identical on BUFFER_TOO_SMALL");
  FreeMap (&M);
}

TEST (Test_InsufficientCapacity_RetryWithReportedLength)
{
  // Caller pattern: call, get BUFFER_TOO_SMALL with required *MapBytes,
  // reallocate to that size, restore *MapBytes to the real length, retry.
  TEST_MAP M;
  UINTN SplitCount;
  UINTN Required;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 0);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_BUFFER_TOO_SMALL);
  Required = M.MapBytes;
  {
    UINT8 *Bigger = (UINT8 *)malloc (Required);
    memcpy (Bigger, M.Buf, M.Capacity);
    free (M.Buf);
    M.Buf = Bigger;
    M.Capacity = Required;
    M.MapBytes = BASE_COUNT * 48;
  }
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (M.MapBytes, Required);
  CHECK_EQ_U64 (DescAt (&M, STRADDLER + 1)->PhysicalStart, GB (2));
  FreeMap (&M);
}

TEST (Test_Malformed_Parameters)
{
  TEST_MAP M;
  UINTN SplitCount = 42;
  UINTN Bytes;
  EFI_MEMORY_DESCRIPTOR *Map;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 1);
  Map = (EFI_MEMORY_DESCRIPTOR *)M.Buf;

  Bytes = M.MapBytes;
  CHECK_STATUS (NwoasSplitMemoryMap (NULL, &Bytes, M.Capacity, 48, 0, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  CHECK_STATUS (NwoasSplitMemoryMap (Map, NULL, M.Capacity, 48, 0, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 48, 0, GB (2), NULL), EFI_INVALID_PARAMETER);
  // DescSize smaller than the base struct.
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 32, 0, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 0, 0, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  // DescSize not a multiple of 8.
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 44, 0, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  // MapBytes not a multiple of DescSize.
  Bytes = M.MapBytes + 8;
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 48, 0, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  CHECK_EQ_U64 (Bytes, M.MapBytes + 8);
  // MapBytes larger than Capacity.
  Bytes = M.MapBytes;
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.MapBytes - 48, 48, 0, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  // KeepStart >= KeepEnd.
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 48, GB (2), GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 48, GB (3), GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  // Unaligned window edges.
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 48, 0x800, GB (2), &SplitCount), EFI_INVALID_PARAMETER);
  CHECK_STATUS (NwoasSplitMemoryMap (Map, &Bytes, M.Capacity, 48, 0, GB (2) + 1, &SplitCount), EFI_INVALID_PARAMETER);

  CHECK_EQ_U64 (SplitCount, 42);           // untouched on INVALID_PARAMETER
  CHECK_EQ_U64 (Bytes, M.MapBytes);
  CHECK (MapUnchanged (&M), "map must be byte-identical after rejected calls");
  FreeMap (&M);
}

TEST (Test_Malformed_UnalignedPhysicalStartInWindow)
{
  DESC_SPEC Specs[2] = {
    { EfiConventionalMemory, MB (1),           0, PAGES (MB (1)),   EFI_MEMORY_WB },
    { EfiConventionalMemory, MB (2) + 0x200,   0, PAGES (MB (100)), EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount = 4;
  BuildMap (&M, Specs, 2, 48, 1);
  CHECK_STATUS (Split (&M, 0, MB (64), &SplitCount), EFI_COMPROMISED_DATA);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, 2 * 48);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_Malformed_UnalignedPhysicalStartOutsideWindowIgnored)
{
  // Malformed descriptor lies below KeepStart; the function does not inspect it.
  DESC_SPEC Specs[2] = {
    { EfiConventionalMemory, MB (1) + 0x200,   0, PAGES (MB (1)),   EFI_MEMORY_WB },
    { EfiConventionalMemory, GB (1),           0, PAGES (GB (2)),   EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 2, 48, 1);
  CHECK_STATUS (Split (&M, GB (1), GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CheckDescMatchesOriginal (&M, 0, 0);
  FreeMap (&M);
}

TEST (Test_Malformed_NumberOfPagesOverflow)
{
  // PhysicalStart + NumberOfPages*4096 would wrap past 2^64.
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, 0xFFFFFFFF00000000ULL, 0, PAGES (GB (4)) + 1, EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount = 4;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, 0xFFFFFFFF00000000ULL, 0xFFFFFFFF80000000ULL, &SplitCount), EFI_COMPROMISED_DATA);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_Malformed_NumberOfPagesAtExactLimitIsAccepted)
{
  // Largest legal descriptor: ends exactly at 2^64 - 4096 + 4096 == 2^64? No:
  // (MAX_UINT64 - Start) / 4096 pages ends at 2^64 - 4096. That is legal and
  // straddles KeepEnd, so it must split with correct page counts.
  UINT64 Start = 0xFFFFFFFF00000000ULL;
  UINT64 MaxPages = (MAX_UINT64 - Start) / EFI_PAGE_SIZE;   // 0xFFFFF
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, Start, 0, MaxPages, EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, Start, 0xFFFFFFFF80000000ULL, &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (DescAt (&M, 0)->NumberOfPages, PAGES (GB (2)));
  CHECK_EQ_U64 (DescAt (&M, 1)->PhysicalStart, 0xFFFFFFFF80000000ULL);
  CHECK_EQ_U64 (DescAt (&M, 1)->NumberOfPages, MaxPages - PAGES (GB (2)));
  CHECK_EQ_U64 (DescAt (&M, 1)->PhysicalStart + DescAt (&M, 1)->NumberOfPages * EFI_PAGE_SIZE,
                MAX_UINT64 - 0xFFF);
  FreeMap (&M);
}

TEST (Test_Malformed_VirtualStartOverflow)
{
  // VirtualStart + Offset would wrap. Offset = 2GB - 16MB.
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, MB (16), 0xFFFFFFFFC0000000ULL, PAGES (GB (4)), EFI_MEMORY_WB | EFI_MEMORY_RUNTIME },
  };
  TEST_MAP M;
  UINTN SplitCount = 4;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_COMPROMISED_DATA);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_Malformed_ZeroPagesDescriptorInWindowIsHarmless)
{
  DESC_SPEC Specs[2] = {
    { EfiConventionalMemory, MB (1),  0, 0,               EFI_MEMORY_WB },
    { EfiConventionalMemory, MB (2),  0, PAGES (MB (100)), EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 2, 48, 1);
  CHECK_STATUS (Split (&M, 0, MB (64), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CheckDescMatchesOriginal (&M, 0, 0);
  CHECK_EQ_U64 (DescAt (&M, 2)->PhysicalStart, MB (64));
  FreeMap (&M);
}

TEST (Test_MultipleStraddlers_Rejected)
{
  // Two overlapping conventional descriptors both cross KeepEnd.
  DESC_SPEC Specs[3] = {
    { EfiLoaderData,         MB (1),  0, PAGES (MB (1)),   EFI_MEMORY_WB },
    { EfiConventionalMemory, MB (2),  0, PAGES (MB (100)), EFI_MEMORY_WB },
    { EfiConventionalMemory, MB (50), 0, PAGES (MB (100)), EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount = 4;
  BuildMap (&M, Specs, 3, 48, 2);
  CHECK_STATUS (Split (&M, 0, MB (64), &SplitCount), EFI_COMPROMISED_DATA);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, 3 * 48);
  CHECK (MapUnchanged (&M), "map must be byte-identical when two straddlers are found");
  FreeMap (&M);
}

TEST (Test_MultipleStraddlers_ThirdIsAlsoRejected_NoPartialWrite)
{
  // Straddlers at indexes 0 and 2, non-straddler between: preflight must
  // detect the second one before any modification.
  DESC_SPEC Specs[3] = {
    { EfiConventionalMemory, MB (2),  0, PAGES (MB (100)), EFI_MEMORY_WB },
    { EfiLoaderData,         MB (1),  0, PAGES (MB (1)),   EFI_MEMORY_WB },
    { EfiConventionalMemory, MB (60), 0, PAGES (MB (8)),   EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount = 4;
  BuildMap (&M, Specs, 3, 48, 2);
  gStubCopyMemCalls = 0;
  CHECK_STATUS (Split (&M, 0, MB (64), &SplitCount), EFI_COMPROMISED_DATA);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK (gStubCopyMemCalls == 0, "no CopyMem may run before preflight completes");
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  FreeMap (&M);
}

TEST (Test_VirtualAddress_NonZeroAdjusted)
{
  // Runtime-mapped conventional range with an offset virtual mapping.
  UINT64 Virt = 0xFFFF800000000000ULL;
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, MB (16), Virt, PAGES (GB (4) - MB (16)), EFI_MEMORY_WB | EFI_MEMORY_RUNTIME },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (DescAt (&M, 0)->VirtualStart, Virt);
  CHECK_EQ_U64 (DescAt (&M, 1)->VirtualStart, Virt + (GB (2) - MB (16)));
  // Physical-to-virtual delta identical for both halves.
  CHECK_EQ_U64 (DescAt (&M, 0)->VirtualStart - DescAt (&M, 0)->PhysicalStart,
                DescAt (&M, 1)->VirtualStart - DescAt (&M, 1)->PhysicalStart);
  FreeMap (&M);
}

TEST (Test_VirtualAddress_ZeroStaysZero)
{
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, MB (16), 0, PAGES (GB (4)), EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (DescAt (&M, 0)->VirtualStart, 0);
  CHECK_EQ_U64 (DescAt (&M, 1)->VirtualStart, 0);
  FreeMap (&M);
}

TEST (Test_VirtualAddress_MaxWithoutOverflowAccepted)
{
  // VirtualStart + Offset == MAX_UINT64 exactly is allowed.
  UINT64 Offset = GB (2) - MB (16);
  UINT64 Virt = MAX_UINT64 - Offset;
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, MB (16), Virt, PAGES (GB (4)), EFI_MEMORY_WB | EFI_MEMORY_RUNTIME },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (DescAt (&M, 1)->VirtualStart, MAX_UINT64);
  FreeMap (&M);
}

TEST (Test_VirtualAddress_IdentityAtZero_Observation)
{
  // Documented behaviour: VirtualStart == 0 is the "not mapped" sentinel, so
  // an identity-mapped descriptor at physical 0 gets a tail with
  // VirtualStart 0 rather than KeepEnd. Recorded as an observation.
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, 0, 0, PAGES (GB (4)), EFI_MEMORY_WB | EFI_MEMORY_RUNTIME },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (DescAt (&M, 1)->PhysicalStart, GB (2));
  CHECK_EQ_U64 (DescAt (&M, 1)->VirtualStart, 0);
  printf ("    note: identity map at 0 -> tail VirtualStart=0x%" PRIx64 " (PhysicalStart=0x%" PRIx64 ")\n",
          DescAt (&M, 1)->VirtualStart, DescAt (&M, 1)->PhysicalStart);
  FreeMap (&M);
}

TEST (Test_PageCoverage_Conserved)
{
  UINT64 KeepEnds[] = { MB (32), GB (1), GB (2), GB (3), GB (4) - MB (4) };
  UINTN K;
  for (K = 0; K < sizeof (KeepEnds) / sizeof (KeepEnds[0]); K++) {
    TEST_MAP M;
    UINTN SplitCount;
    UINT64 OrigStart = gBaseMap[STRADDLER].PhysicalStart;
    UINT64 OrigPages = gBaseMap[STRADDLER].NumberOfPages;
    UINT64 OrigEnd   = OrigStart + OrigPages * EFI_PAGE_SIZE;
    EFI_MEMORY_DESCRIPTOR *Head, *Tail;
    BuildMap (&M, gBaseMap, BASE_COUNT, 48, 1);
    CHECK_STATUS (Split (&M, 0, KeepEnds[K], &SplitCount), EFI_SUCCESS);
    CHECK_EQ_U64 (SplitCount, 1);
    Head = DescAt (&M, STRADDLER);
    Tail = DescAt (&M, STRADDLER + 1);
    CHECK_EQ_U64 (Head->PhysicalStart, OrigStart);
    CHECK_EQ_U64 (Head->PhysicalStart + Head->NumberOfPages * EFI_PAGE_SIZE, KeepEnds[K]);
    CHECK_EQ_U64 (Tail->PhysicalStart, KeepEnds[K]);
    CHECK_EQ_U64 (Tail->PhysicalStart + Tail->NumberOfPages * EFI_PAGE_SIZE, OrigEnd);
    CHECK_EQ_U64 (Head->NumberOfPages + Tail->NumberOfPages, OrigPages);
    CHECK (Head->NumberOfPages > 0 && Tail->NumberOfPages > 0, "no empty halves");
    FreeMap (&M);
  }
}

TEST (Test_Metadata_UnchangedAndIdempotent)
{
  TEST_MAP M;
  UINTN SplitCount, I;
  UINT8 *AfterFirst;
  UINTN BytesAfterFirst;
  BuildMap (&M, gBaseMap, BASE_COUNT, 48, 2);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);

  // Type/Attribute/extension of both halves equal the original.
  for (I = STRADDLER; I <= STRADDLER + 1; I++) {
    CHECK_EQ_U64 (DescAt (&M, I)->Type, gBaseMap[STRADDLER].Type);
    CHECK_EQ_U64 (DescAt (&M, I)->Attribute, gBaseMap[STRADDLER].Attribute);
    CheckExtPattern (&M, I, STRADDLER);
    // Padding bytes between Type and PhysicalStart are copied verbatim (zero here).
    CHECK (memcmp ((UINT8 *)DescAt (&M, I) + 4, (UINT8 *)M.Snapshot + STRADDLER * 48 + 4, 4) == 0,
           "padding after Type must be preserved");
  }
  // Every non-split descriptor is byte-identical to its original.
  for (I = 0; I < STRADDLER; I++) CheckDescMatchesOriginal (&M, I, I);
  for (I = STRADDLER + 1; I < BASE_COUNT; I++) CheckDescMatchesOriginal (&M, I + 1, I);
  // Bytes beyond the new MapBytes are untouched canary.
  for (I = M.MapBytes; I < M.Capacity; I++) {
    CHECK (M.Buf[I] == 0xEE, "byte %" PRIuPTR " past MapBytes was written (0x%02x)", I, M.Buf[I]);
    if (M.Buf[I] != 0xEE) break;
  }
  // Map is sorted by PhysicalStart at and around the split.
  CHECK (DescAt (&M, STRADDLER)->PhysicalStart < DescAt (&M, STRADDLER + 1)->PhysicalStart, "sorted");
  CHECK (DescAt (&M, STRADDLER + 1)->PhysicalStart < DescAt (&M, STRADDLER + 2)->PhysicalStart, "sorted");

  // Second call with the same window finds nothing to split and changes nothing.
  AfterFirst = (UINT8 *)malloc (M.Capacity);
  memcpy (AfterFirst, M.Buf, M.Capacity);
  BytesAfterFirst = M.MapBytes;
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK_EQ_U64 (M.MapBytes, BytesAfterFirst);
  CHECK (memcmp (AfterFirst, M.Buf, M.Capacity) == 0, "idempotent");
  free (AfterFirst);
  FreeMap (&M);
}

TEST (Test_DescSize40_NoExtension)
{
  TEST_MAP M;
  UINTN SplitCount, I;
  BuildMap (&M, gBaseMap, BASE_COUNT, 40, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CHECK_EQ_U64 (M.MapBytes, (BASE_COUNT + 1) * 40);
  CHECK_EQ_U64 (DescAt (&M, STRADDLER + 1)->PhysicalStart, GB (2));
  for (I = STRADDLER + 1; I < BASE_COUNT; I++) CheckDescMatchesOriginal (&M, I + 1, I);
  FreeMap (&M);
}

TEST (Test_DescSize64_LargeExtension)
{
  TEST_MAP M;
  UINTN SplitCount, I;
  BuildMap (&M, gBaseMap, BASE_COUNT, 64, 1);
  CHECK_STATUS (Split (&M, 0, GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 1);
  CheckExtPattern (&M, STRADDLER, STRADDLER);
  CheckExtPattern (&M, STRADDLER + 1, STRADDLER);
  for (I = STRADDLER + 1; I < BASE_COUNT; I++) CheckDescMatchesOriginal (&M, I + 1, I);
  FreeMap (&M);
}

TEST (Test_Observation_StraddlerBelowKeepStartNotSplit)
{
  // A conventional descriptor that starts below KeepStart and runs past
  // KeepEnd is not considered (PhysicalStart < KeepStart filter). Recorded
  // as an observation for the reviewer; the function reports SUCCESS with 0.
  DESC_SPEC Specs[1] = {
    { EfiConventionalMemory, MB (16), 0, PAGES (GB (8)), EFI_MEMORY_WB },
  };
  TEST_MAP M;
  UINTN SplitCount;
  BuildMap (&M, Specs, 1, 48, 1);
  CHECK_STATUS (Split (&M, GB (1), GB (2), &SplitCount), EFI_SUCCESS);
  CHECK_EQ_U64 (SplitCount, 0);
  CHECK (MapUnchanged (&M), "map must be byte-identical");
  printf ("    note: descriptor [0x%" PRIx64 ", 0x%" PRIx64 ") spans window [0x%" PRIx64 ", 0x%" PRIx64 ") but SplitCount=%" PRIuPTR "\n",
          MB (16), MB (16) + GB (8), GB (1), GB (2), SplitCount);
  FreeMap (&M);
}

// ---------------------------------------------------------------------------

int
main (void)
{
  printf ("NwoasSplitMemoryMap host tests (sizeof EFI_MEMORY_DESCRIPTOR=%zu)\n", sizeof (EFI_MEMORY_DESCRIPTOR));
  if (sizeof (EFI_MEMORY_DESCRIPTOR) != 40) {
    printf ("stub layout mismatch; aborting\n");
    return 2;
  }
  Test_BasicSplit_ExtensionPreserved_run ();
  Test_OverlappingInsertion_StraddlerFirst_run ();
  Test_OverlappingInsertion_StraddlerLast_run ();
  Test_ExactBoundary_NoSplit_run ();
  Test_ExactBoundary_StartsAtKeepStart_Splits_run ();
  Test_ExactBoundary_OnePageOverBoundary_run ();
  Test_NoSplit_NoStraddler_run ();
  Test_NoSplit_NonConventionalStraddlerIgnored_run ();
  Test_NoSplit_NoSpareCapacityIsFine_run ();
  Test_NoSplit_EmptyMap_run ();
  Test_InsufficientCapacity_ExactlyFull_run ();
  Test_InsufficientCapacity_PartialSlot_run ();
  Test_InsufficientCapacity_RetryWithReportedLength_run ();
  Test_Malformed_Parameters_run ();
  Test_Malformed_UnalignedPhysicalStartInWindow_run ();
  Test_Malformed_UnalignedPhysicalStartOutsideWindowIgnored_run ();
  Test_Malformed_NumberOfPagesOverflow_run ();
  Test_Malformed_NumberOfPagesAtExactLimitIsAccepted_run ();
  Test_Malformed_VirtualStartOverflow_run ();
  Test_Malformed_ZeroPagesDescriptorInWindowIsHarmless_run ();
  Test_MultipleStraddlers_Rejected_run ();
  Test_MultipleStraddlers_ThirdIsAlsoRejected_NoPartialWrite_run ();
  Test_VirtualAddress_NonZeroAdjusted_run ();
  Test_VirtualAddress_ZeroStaysZero_run ();
  Test_VirtualAddress_MaxWithoutOverflowAccepted_run ();
  Test_VirtualAddress_IdentityAtZero_Observation_run ();
  Test_PageCoverage_Conserved_run ();
  Test_Metadata_UnchangedAndIdempotent_run ();
  Test_DescSize40_NoExtension_run ();
  Test_DescSize64_LargeExtension_run ();
  Test_Observation_StraddlerBelowKeepStartNotSplit_run ();

  printf ("\n%d checks, %d failures\n", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
