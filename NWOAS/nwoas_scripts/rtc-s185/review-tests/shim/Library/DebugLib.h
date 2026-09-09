#ifndef NWOAS_REVIEW_SHIM_DEBUGLIB_H
#define NWOAS_REVIEW_SHIM_DEBUGLIB_H
#include <PiDxe.h>
#define DEBUG_INFO  0x40
#define DEBUG_WARN  0x02
#define DEBUG_ERROR 0x80000000
VOID ShimDebugPrint (UINTN Level, CONST CHAR8 *Fmt, ...);
#define DEBUG(Expression) ShimDebugPrint Expression
#endif
