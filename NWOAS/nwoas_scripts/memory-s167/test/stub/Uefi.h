/** @file
  Minimal host-side stand-in for EDK2 <Uefi.h>.
  Only the definitions consumed by NwoasSplitMemoryMap.h are provided.
  Layout of EFI_MEMORY_DESCRIPTOR matches the UEFI spec (40 bytes on 64-bit).
**/
#ifndef UEFI_STUB_H_
#define UEFI_STUB_H_

#include <stdint.h>
#include <stddef.h>

typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef uintptr_t UINTN;
typedef intptr_t  INTN;
typedef UINTN     EFI_STATUS;
typedef UINT64    EFI_PHYSICAL_ADDRESS;
typedef UINT64    EFI_VIRTUAL_ADDRESS;
typedef UINT8     BOOLEAN;
typedef void      VOID;

#define IN
#define OUT
#define OPTIONAL
#define CONST   const
#define STATIC  static
#define EFIAPI
#define TRUE    ((BOOLEAN)1)
#define FALSE   ((BOOLEAN)0)

#define MAX_UINT64  ((UINT64)0xFFFFFFFFFFFFFFFFULL)
#define MAX_UINTN   ((UINTN)~(UINTN)0)
#define MAX_ADDRESS MAX_UINTN

#define EFI_PAGE_SIZE   0x1000
#define EFI_PAGE_SHIFT  12
#define EFI_PAGE_MASK   0xFFF

#define ENCODE_ERROR(a)       ((EFI_STATUS)(MAX_UINTN - MAX_UINTN / 2 + 1 - 1 + (a)))
#define EFI_ERROR(s)          (((INTN)(s)) < 0)
#define EFI_SUCCESS           ((EFI_STATUS)0)
#define EFI_LOAD_ERROR        ENCODE_ERROR (1)
#define EFI_INVALID_PARAMETER ENCODE_ERROR (2)
#define EFI_UNSUPPORTED       ENCODE_ERROR (3)
#define EFI_BAD_BUFFER_SIZE   ENCODE_ERROR (4)
#define EFI_BUFFER_TOO_SMALL  ENCODE_ERROR (5)
#define EFI_NOT_READY         ENCODE_ERROR (6)
#define EFI_DEVICE_ERROR      ENCODE_ERROR (7)
#define EFI_WRITE_PROTECTED   ENCODE_ERROR (8)
#define EFI_OUT_OF_RESOURCES  ENCODE_ERROR (9)
#define EFI_NOT_FOUND         ENCODE_ERROR (14)
#define EFI_COMPROMISED_DATA  ENCODE_ERROR (33)

typedef enum {
  EfiReservedMemoryType,
  EfiLoaderCode,
  EfiLoaderData,
  EfiBootServicesCode,
  EfiBootServicesData,
  EfiRuntimeServicesCode,
  EfiRuntimeServicesData,
  EfiConventionalMemory,
  EfiUnusableMemory,
  EfiACPIReclaimMemory,
  EfiACPIMemoryNVS,
  EfiMemoryMappedIO,
  EfiMemoryMappedIOPortSpace,
  EfiPalCode,
  EfiPersistentMemory,
  EfiUnacceptedMemoryType,
  EfiMaxMemoryType
} EFI_MEMORY_TYPE;

#define EFI_MEMORY_UC       0x0000000000000001ULL
#define EFI_MEMORY_WC       0x0000000000000002ULL
#define EFI_MEMORY_WT       0x0000000000000004ULL
#define EFI_MEMORY_WB       0x0000000000000008ULL
#define EFI_MEMORY_XP       0x0000000000004000ULL
#define EFI_MEMORY_RUNTIME  0x8000000000000000ULL

typedef struct {
  UINT32                Type;
  // 4 bytes of natural padding here, as in the real ABI
  EFI_PHYSICAL_ADDRESS  PhysicalStart;
  EFI_VIRTUAL_ADDRESS   VirtualStart;
  UINT64                NumberOfPages;
  UINT64                Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EFI_MEMORY_DESCRIPTOR_VERSION 1

#endif
