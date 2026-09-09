/* Review-only host shim for the UEFI headers that
 * NwoasHardwareBootRtcLib.c includes. Provides just enough of PiDxe.h so the
 * REAL library source compiles unchanged on the host and can be driven by a
 * mock SPMI controller, mock ADT and mock physical counter.
 * Nothing here is used by the firmware build. */
#ifndef NWOAS_REVIEW_SHIM_PIDXE_H
#define NWOAS_REVIEW_SHIM_PIDXE_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef uintptr_t UINTN;
typedef intptr_t  INTN;
typedef char      CHAR8;
typedef uint8_t   BOOLEAN;
#define VOID      void
#define CONST     const
#define STATIC    static
#define EFIAPI
#define IN
#define OUT
#define OPTIONAL
#define TRUE      ((BOOLEAN)1)
#define FALSE     ((BOOLEAN)0)

typedef UINTN EFI_STATUS;
#define MAX_BIT               (1ULL << 63)
#define EFI_SUCCESS           ((EFI_STATUS)0)
#define EFI_INVALID_PARAMETER ((EFI_STATUS)(MAX_BIT | 2))
#define EFI_UNSUPPORTED       ((EFI_STATUS)(MAX_BIT | 3))
#define EFI_NOT_READY         ((EFI_STATUS)(MAX_BIT | 6))
#define EFI_DEVICE_ERROR      ((EFI_STATUS)(MAX_BIT | 7))
#define EFI_ERROR(s)          (((INTN)(s)) < 0)

typedef VOID *EFI_HANDLE;
typedef VOID *EFI_EVENT;
typedef struct { UINT32 Unused; } EFI_SYSTEM_TABLE;

typedef struct {
  UINT16 Year;
  UINT8  Month;
  UINT8  Day;
  UINT8  Hour;
  UINT8  Minute;
  UINT8  Second;
  UINT8  Pad1;
  UINT32 Nanosecond;
  INT16  TimeZone;
  UINT8  Daylight;
  UINT8  Pad2;
} EFI_TIME;

typedef struct {
  UINT32  Resolution;
  UINT32  Accuracy;
  BOOLEAN SetsToZero;
} EFI_TIME_CAPABILITIES;

#define EFI_UNSPECIFIED_TIMEZONE 0x07FF
#define EFI_TIME_ADJUST_DAYLIGHT 0x01
#define EFI_TIME_IN_DAYLIGHT     0x02

#endif
