/** @file
  Minimal host-side stand-in for EDK2 <Library/BaseMemoryLib.h>.
  CopyMem follows the EDK2 contract: overlapping buffers are permitted and
  Length == 0 is a no-op. Implemented in BaseMemoryLibStub.c.
**/
#ifndef BASE_MEMORY_LIB_STUB_H_
#define BASE_MEMORY_LIB_STUB_H_

#include <Uefi.h>

VOID *
EFIAPI
CopyMem (
  OUT VOID       *DestinationBuffer,
  IN CONST VOID  *SourceBuffer,
  IN UINTN       Length
  );

VOID *
EFIAPI
SetMem (
  OUT VOID  *Buffer,
  IN UINTN  Length,
  IN UINT8  Value
  );

VOID *
EFIAPI
ZeroMem (
  OUT VOID  *Buffer,
  IN UINTN  Length
  );

INTN
EFIAPI
CompareMem (
  IN CONST VOID  *DestinationBuffer,
  IN CONST VOID  *SourceBuffer,
  IN UINTN       Length
  );

// Test instrumentation: number of CopyMem calls and of those that overlapped.
extern UINTN  gStubCopyMemCalls;
extern UINTN  gStubCopyMemOverlapCalls;

#endif
