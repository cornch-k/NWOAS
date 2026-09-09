#ifndef NWOAS_REVIEW_SHIM_IOLIB_H
#define NWOAS_REVIEW_SHIM_IOLIB_H
#include <PiDxe.h>
UINT32 MmioRead32 (UINTN Address);
UINT32 MmioWrite32 (UINTN Address, UINT32 Value);
#endif
