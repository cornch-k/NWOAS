#ifndef NWOAS_REVIEW_SHIM_RTCLIB_H
#define NWOAS_REVIEW_SHIM_RTCLIB_H
#include <PiDxe.h>
/* Same prototypes as MdeModulePkg/Include/Library/RealTimeClockLib.h. */
EFI_STATUS EFIAPI LibGetTime (OUT EFI_TIME *Time, OUT EFI_TIME_CAPABILITIES *Capabilities);
EFI_STATUS EFIAPI LibSetTime (IN EFI_TIME *Time);
EFI_STATUS EFIAPI LibGetWakeupTime (OUT BOOLEAN *Enabled, OUT BOOLEAN *Pending, OUT EFI_TIME *Time);
EFI_STATUS EFIAPI LibSetWakeupTime (IN BOOLEAN Enabled, OUT EFI_TIME *Time);
EFI_STATUS EFIAPI LibRtcInitialize (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable);
VOID EFIAPI LibRtcVirtualNotifyEvent (IN EFI_EVENT Event, IN VOID *Context);
#endif
