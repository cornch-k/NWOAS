/** @file
  Host implementation of the BaseMemoryLib subset used by the tests.
  Mirrors EDK2 MdePkg/Library/BaseMemoryLib semantics: CopyMem handles
  overlapping regions (memmove) and Length == 0 returns immediately.
**/
#include <string.h>
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

UINTN  gStubCopyMemCalls        = 0;
UINTN  gStubCopyMemOverlapCalls = 0;

VOID *
EFIAPI
CopyMem (
  OUT VOID       *DestinationBuffer,
  IN CONST VOID  *SourceBuffer,
  IN UINTN       Length
  )
{
  gStubCopyMemCalls++;
  if (Length == 0) {
    return DestinationBuffer;
  }
  {
    const UINT8 *S = (const UINT8 *)SourceBuffer;
    const UINT8 *D = (const UINT8 *)DestinationBuffer;
    if ((S < D && S + Length > D) || (D < S && D + Length > S)) {
      gStubCopyMemOverlapCalls++;
    }
  }
  return memmove (DestinationBuffer, SourceBuffer, Length);
}

VOID *
EFIAPI
SetMem (
  OUT VOID  *Buffer,
  IN UINTN  Length,
  IN UINT8  Value
  )
{
  return memset (Buffer, Value, Length);
}

VOID *
EFIAPI
ZeroMem (
  OUT VOID  *Buffer,
  IN UINTN  Length
  )
{
  return memset (Buffer, 0, Length);
}

INTN
EFIAPI
CompareMem (
  IN CONST VOID  *DestinationBuffer,
  IN CONST VOID  *SourceBuffer,
  IN UINTN       Length
  )
{
  return memcmp (DestinationBuffer, SourceBuffer, Length);
}
