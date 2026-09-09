/* SPDX-License-Identifier: MIT
 * S185: read the T8103 SERA RTC directly once during DXE, then advance the
 * copied UTC snapshot with CNTPCT at runtime. No host date/ADT seed, no PMU
 * writes. SetTime/wakeup remain unsupported; suspend counter continuity and
 * long-term drift are not qualified. This is a hardware boot RTC reader,
 * not a complete persistent read/write Windows RTC driver.
 */
#include <PiDxe.h>

#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/AppleDTLib.h>
#include <Library/IoLib.h>
#include "nwoas_sera_rtc.h"

#include "NwoasRtcSeedCore.h"

/* Capability accuracy is a nominal 100ppm placeholder, not a measured
 * hardware bound. One-second advertised resolution is conservative. */
#define NWOAS_RTC_CAP_RESOLUTION_HZ   1u
#define NWOAS_RTC_CAP_ACCURACY        100000000u

//
// Module globals. These are the ONLY state used at runtime. They are plain
// values (no pointers), so no SetVirtualAddressMap conversion is required.
//
STATIC BOOLEAN           mSeedValid = FALSE;
STATIC nwoas_rtc_seed_t  mSeed;
STATIC UINT64            mFreqHz    = 0;
STATIC UINT32            mSeedNs    = 0;

/**
  Returns the current time and date information, and the time-keeping
  capabilities of the seeded clock.

  @retval EFI_SUCCESS           Time filled from seed + CNTPCT delta.
  @retval EFI_INVALID_PARAMETER Time is NULL.
  @retval EFI_NOT_READY         No valid boot seed was found at DXE init.
  @retval EFI_DEVICE_ERROR      Counter frequency changed, counter went
                                backwards, or the advanced time left the
                                representable window.
**/
EFI_STATUS
EFIAPI
LibGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities
  )
{
  UINT64                Now;
  UINT64                Freq;
  UINT64                Epoch;
  UINT32                Nanosecond;
  nwoas_rtc_calendar_t  Cal;
  nwoas_rtc_status_t    St;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Capabilities != NULL) {
    Capabilities->Resolution = NWOAS_RTC_CAP_RESOLUTION_HZ;
    Capabilities->Accuracy   = NWOAS_RTC_CAP_ACCURACY;
    Capabilities->SetsToZero = FALSE;
  }

  if (!mSeedValid) {
    return EFI_NOT_READY;
  }

  //
  // Physical counter. AppleArmGenericTimerPhyCounterLib maps this to
  // CNTPCT_EL0; the m1n1 HV grants EL1 access (CNTHCTL_EL1PCTEN) and only
  // offsets the virtual counter, so this is wall-clock monotonic.
  //
  Freq = (UINT64)ArmGenericTimerGetTimerFreq ();
  if (Freq != mFreqHz) {
    return EFI_DEVICE_ERROR;
  }
  Now = ArmGenericTimerGetSystemCount ();

  St = nwoas_rtc_advance (&mSeed, Now, Freq, &Epoch, &Nanosecond);
  if (St != NWOAS_RTC_OK) {
    return EFI_DEVICE_ERROR;
  }

  Nanosecond += mSeedNs;
  if (Nanosecond >= 1000000000u) {
    if (Epoch >= NWOAS_RTC_EPOCH_MAX) return EFI_DEVICE_ERROR;
    Epoch++; Nanosecond -= 1000000000u;
  }
  St = nwoas_rtc_epoch_to_calendar (Epoch, &Cal);
  if (St != NWOAS_RTC_OK) {
    return EFI_DEVICE_ERROR;
  }

  //
  // RealTimeClockRuntimeDxe pre-fills TimeZone/Daylight from its own
  // in-memory settings before calling us, and it updates those settings even
  // when LibSetTime fails. We override them unconditionally so the reported
  // value is always "UTC, zone unspecified".
  //
  Time->Year       = Cal.year;
  Time->Month      = Cal.month;
  Time->Day        = Cal.day;
  Time->Hour       = Cal.hour;
  Time->Minute     = Cal.minute;
  Time->Second     = Cal.second;
  Time->Pad1       = 0;
  Time->Nanosecond = Nanosecond;
  Time->TimeZone   = EFI_UNSPECIFIED_TIMEZONE;
  Time->Daylight   = 0;
  Time->Pad2       = 0;

  return EFI_SUCCESS;
}

/**
  Sets the current local time and date information.

  Deliberately unsupported: the only persistent store is the PMU offset cell,
  and writing it is outside this candidate's scope (Phase C in PLAN.md).

  @retval EFI_UNSUPPORTED       Always.
**/
EFI_STATUS
EFIAPI
LibSetTime (
  IN EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Returns the current wakeup alarm clock setting.

  @retval EFI_UNSUPPORTED       Always.
**/
EFI_STATUS
EFIAPI
LibGetWakeupTime (
  OUT BOOLEAN   *Enabled,
  OUT BOOLEAN   *Pending,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Sets the system wakeup alarm clock time.

  @retval EFI_UNSUPPORTED       Always.
**/
EFI_STATUS
EFIAPI
LibSetWakeupTime (
  IN  BOOLEAN   Enabled,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

static uint32_t RtcRead32(void *ctx, uint64_t address)
{ (void)ctx; return MmioRead32((UINTN)address); }
static void RtcWrite32(void *ctx, uint64_t address, uint32_t value)
{ (void)ctx; MmioWrite32((UINTN)address,value); MemoryFence(); }
static uint64_t RtcCount(void *ctx)
{ (void)ctx; return ArmGenericTimerGetSystemCount(); }
static uint64_t RtcFreq(void *ctx)
{ (void)ctx; return ArmGenericTimerGetTimerFreq(); }
static BOOLEAN RtcProp32(dt_node_t *node, const CHAR8 *name, UINT32 expected)
{
  size_t len=0; UINT32 value=0; VOID *p;
  if(!node) return FALSE;
  p=dt_node_prop(node,name,&len);
  if(!p || len<sizeof(value))return FALSE;
  CopyMem(&value,p,sizeof(value));return value==expected;
}
static BOOLEAN RtcTopology(void)
{
  dt_node_t *chosen=dt_get("/chosen");
  dt_node_t *ctrl=dt_get("/arm-io/nub-spmi");
  dt_node_t *pmu=dt_get("/arm-io/nub-spmi/spmi-pmu");
  size_t len=0; UINT64 reg[2]; VOID *p;
  if(!RtcProp32(chosen,"chip-id",0x8103) || !ctrl || !pmu ||
     !RtcProp32(pmu,"reg",15) || !RtcProp32(pmu,"info-rtc",0xd002) ||
     !RtcProp32(pmu,"info-rtc_scrpad",0xd100))return FALSE;
  p=dt_node_prop(ctrl,"reg",&len);
  if(!p || len<sizeof(reg))return FALSE;
  CopyMem(reg,p,sizeof(reg));
  /* ADT arm-io relative address; T8103 parent range starts at 0x200000000. */
  return reg[0]==0x3d0d9300ULL && reg[1]==0x100;
}
EFI_STATUS EFIAPI LibRtcInitialize(IN EFI_HANDLE ImageHandle,
                                  IN EFI_SYSTEM_TABLE *SystemTable)
{
  nwoas_sera_rtc_ops_t ops={.read32=RtcRead32,.write32=RtcWrite32,
      .physical_counter=RtcCount,.counter_frequency=RtcFreq,.ctx=NULL};
  nwoas_sera_rtc_result_t result;
  nwoas_sera_status_t status;
  nwoas_rtc_calendar_t cal;
  UINT64 before,after,freq;
  (void)ImageHandle; (void)SystemTable;
  mSeedValid=FALSE;mSeedNs=0;mFreqHz=0;ZeroMem(&mSeed,sizeof(mSeed));
  if(!RtcTopology()) {
    DEBUG((DEBUG_ERROR,"NwoasHwRtc: T8103 topology rejected; GetTime NOT_READY\n"));
    return EFI_SUCCESS;
  }
  before=ArmGenericTimerGetSystemCount();freq=ArmGenericTimerGetTimerFreq();
  status=nwoas_sera_rtc_read(&ops,&result);
  after=ArmGenericTimerGetSystemCount();
  if(status!=NWOAS_SERA_OK || freq==0 || after<before || after-before>freq ||
     result.cntfrq!=freq || result.cntpct>after) {
    DEBUG((DEBUG_ERROR,"NwoasHwRtc: read failed %a writes=%u reads=%u span=%lu; GetTime NOT_READY\n",
       nwoas_sera_status_str(status),result.cmd_writes,result.transactions_done,after-before));
    return EFI_SUCCESS;
  }
  mSeed.magic=NWOAS_RTC_SEED_MAGIC;mSeed.version=NWOAS_RTC_SEED_VERSION;
  mSeed.epoch=(UINT64)result.utc_seconds;mSeed.cntpct=result.cntpct;
  mSeed.cntfrq=(UINT32)freq;mSeed.flags=NWOAS_RTC_SEED_FLAG_VALID|NWOAS_RTC_SEED_FLAG_SPMI;
  if(nwoas_rtc_seed_validate(&mSeed,freq)!=NWOAS_RTC_OK ||
     nwoas_rtc_epoch_to_calendar(mSeed.epoch,&cal)!=NWOAS_RTC_OK) {
    DEBUG((DEBUG_ERROR,"NwoasHwRtc: decoded snapshot invalid; GetTime NOT_READY\n"));
    return EFI_SUCCESS;
  }
  mSeedNs=result.nanoseconds;mFreqHz=freq;mSeedValid=TRUE;
  DEBUG((DEBUG_INFO,"NwoasHwRtc: DIRECT SERA epoch=%lu ns=%u counter=%lx offset=%lx cntpct=%lx freq=%lu => %04u-%02u-%02u %02u:%02u:%02u UTC\n",
      mSeed.epoch,mSeedNs,result.counter1,result.offset,mSeed.cntpct,freq,
      cal.year,cal.month,cal.day,cal.hour,cal.minute,cal.second));
  return EFI_SUCCESS;
}

/**
  Virtual-address-change hook kept for parity with the existing
  AppleSiliconPkg VirtualRealTimeClockLib. RealTimeClockRuntimeDxe in this
  tree does not call it, and this library holds no pointers, so it is a no-op.
**/
VOID
EFIAPI
LibRtcVirtualNotifyEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  return;
}
