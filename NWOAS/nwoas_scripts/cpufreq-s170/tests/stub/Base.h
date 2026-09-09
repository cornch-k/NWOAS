/*
 * Stub <Base.h> for the native NwoasCpuPstate.h unit test.
 *
 * Provides ONLY the subset of MdePkg Base.h the header relies on, with the same
 * widths EDK2 uses on AARCH64. NwoasCpuPstate.h is pure C on top of these
 * typedefs, so the very same header the firmware compiles is tested on the host.
 */
#ifndef NWOAS_TEST_STUB_BASE_H_
#define NWOAS_TEST_STUB_BASE_H_

#include <stdint.h>
#include <stddef.h>

typedef uint64_t        UINT64;
typedef int64_t         INT64;
typedef uint32_t        UINT32;
typedef int32_t         INT32;
typedef uint16_t        UINT16;
typedef uint8_t         UINT8;
typedef uintptr_t       UINTN;
typedef intptr_t        INTN;
typedef unsigned char   BOOLEAN;
typedef char            CHAR8;
typedef void            VOID;

#ifndef TRUE
#define TRUE   ((BOOLEAN)(1 == 1))
#endif
#ifndef FALSE
#define FALSE  ((BOOLEAN)(0 == 1))
#endif

#define CONST   const
#define STATIC  static
#define IN
#define OUT
#define OPTIONAL

#define MAX_UINT64  ((UINT64)0xFFFFFFFFFFFFFFFFULL)
#define MAX_UINT32  ((UINT32)0xFFFFFFFF)

#endif /* NWOAS_TEST_STUB_BASE_H_ */
