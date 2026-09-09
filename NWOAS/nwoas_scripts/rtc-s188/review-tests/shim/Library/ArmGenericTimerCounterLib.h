#ifndef NWOAS_REVIEW_SHIM_ARMTIMER_H
#define NWOAS_REVIEW_SHIM_ARMTIMER_H
#include <PiDxe.h>
UINTN  ArmGenericTimerGetTimerFreq (VOID);
UINT64 ArmGenericTimerGetSystemCount (VOID);
#endif
