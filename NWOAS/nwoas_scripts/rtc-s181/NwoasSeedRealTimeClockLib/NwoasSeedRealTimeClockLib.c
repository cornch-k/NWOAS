/** @file
 *
 *  NWOAS boot-seeded EFI RealTimeClock library (S181 CANDIDATE, OFFLINE).
 *
 *  What this is:
 *    A RealTimeClockLib that takes a one-shot UTC snapshot placed in the Apple
 *    Device Tree by the host-side pre-boot module (ADT /chosen
 *    "nwoas,rtc-snapshot", 32 bytes) and advances it with the ARM physical
 *    counter (CNTPCT_EL0). GetTime therefore reports real UTC for the
 *    duration of the boot session.
 *
 *  What this is NOT:
 *    - Not a native RTC driver. Nothing here talks to the PMU or SMC.
 *    - Not persistent. SetTime is EFI_UNSUPPORTED; Windows cannot write the
 *      hardware clock through this path.
 *    - Not proof that PMU register 0xd002 is the RTC counter. That evidence
 *      (S178 bounded probe) was not available when this file was written.
 *
 *  Runtime rules honoured:
 *    - The ADT lives in boot-services memory. It is read exactly once in
 *      LibRtcInitialize (DXE, before ExitBootServices) and every value is
 *      copied into module globals. No ADT pointer is kept.
 *    - LibGetTime reads only system registers (CNTPCT_EL0 / CNTFRQ_EL0) and
 *      module globals, both reachable after SetVirtualAddressMap. No DEBUG
 *      output, no boot-services calls, no MMIO.
 *    - Missing/invalid seed => LibRtcInitialize still returns EFI_SUCCESS
 *      (otherwise RealTimeClockRuntimeDxe would fail to load and the RTC
 *      architectural protocol would never be installed) and LibGetTime
 *      returns EFI_NOT_READY. No calendar date is invented.
 *
 *  Copyright (c) 2026, NWOAS project. SPDX-License-Identifier: MIT
 *
 **/

#include <PiDxe.h>

#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/AppleDTLib.h>

#include "NwoasRtcSeedCore.h"

#define NWOAS_RTC_ADT_NODE      "/chosen"
#define NWOAS_RTC_ADT_PROPERTY  "nwoas,rtc-snapshot"

//
// Honest capability numbers.
//   Resolution: the seed carries integer UTC seconds, so 1 Hz is the truthful
//   granularity even though Nanosecond is filled from the 24 MHz counter.
//   Accuracy:   UEFI spec unit is 1e-6 ppm (50 ppm => 50,000,000). The
//   counter crystal plus an unqualified seed source are bounded here at
//   100 ppm. This is an upper bound, not a measurement.
//
#define NWOAS_RTC_CAP_RESOLUTION_HZ   1u
#define NWOAS_RTC_CAP_ACCURACY        100000000u

//
// Module globals. These are the ONLY state used at runtime. They are plain
// values (no pointers), so no SetVirtualAddressMap conversion is required.
//
STATIC BOOLEAN           mSeedValid = FALSE;
STATIC nwoas_rtc_seed_t  mSeed;
STATIC UINT64            mFreqHz    = 0;

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

/**
  Load the boot seed from the ADT (boot-services memory) into module globals.

  Always returns EFI_SUCCESS: RealTimeClockRuntimeDxe propagates any error
  from here as its own entry-point failure, which would leave the platform
  without a RealTimeClock architectural protocol. A missing seed is reported
  through LibGetTime (EFI_NOT_READY) instead.

  @retval EFI_SUCCESS           Always (seed state is recorded in globals).
**/
EFI_STATUS
EFIAPI
LibRtcInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  dt_node_t           *Chosen;
  VOID                *Prop;
  size_t               PropLen;
  UINT8                Blob[NWOAS_RTC_SEED_SIZE];
  nwoas_rtc_seed_t     Seed;
  nwoas_rtc_status_t   St;
  UINT64               LiveFreq;
  UINT64               LiveCount;
  nwoas_rtc_calendar_t Cal;

  mSeedValid = FALSE;
  mFreqHz    = 0;
  ZeroMem (&mSeed, sizeof (mSeed));

  //
  // AppleDTLib dt_get_prop() dereferences a NULL node, so look the node up
  // first and bail out ourselves if /chosen is absent.
  //
  Chosen = dt_get (NWOAS_RTC_ADT_NODE);
  if (Chosen == NULL) {
    DEBUG ((DEBUG_WARN, "NwoasSeedRtc: ADT node %a missing; GetTime => NOT_READY\n", NWOAS_RTC_ADT_NODE));
    return EFI_SUCCESS;
  }

  PropLen = 0;
  Prop    = dt_node_prop (Chosen, NWOAS_RTC_ADT_PROPERTY, &PropLen);
  if (Prop == NULL) {
    DEBUG ((DEBUG_WARN, "NwoasSeedRtc: no %a seed; GetTime => NOT_READY\n", NWOAS_RTC_ADT_PROPERTY));
    return EFI_SUCCESS;
  }

  if (PropLen != NWOAS_RTC_SEED_SIZE) {
    DEBUG ((DEBUG_ERROR, "NwoasSeedRtc: seed length %u != %u; GetTime => NOT_READY\n",
            (UINT32)PropLen, (UINT32)NWOAS_RTC_SEED_SIZE));
    return EFI_SUCCESS;
  }

  //
  // Copy out of the ADT immediately; nothing below touches Prop again.
  //
  CopyMem (Blob, Prop, NWOAS_RTC_SEED_SIZE);
  Prop = NULL;

  St = nwoas_rtc_seed_parse (Blob, NWOAS_RTC_SEED_SIZE, &Seed);
  if (St != NWOAS_RTC_OK) {
    DEBUG ((DEBUG_ERROR, "NwoasSeedRtc: seed rejected (%a); GetTime => NOT_READY\n",
            nwoas_rtc_status_str (St)));
    return EFI_SUCCESS;
  }

  LiveFreq  = (UINT64)ArmGenericTimerGetTimerFreq ();
  LiveCount = ArmGenericTimerGetSystemCount ();

  St = nwoas_rtc_seed_validate (&Seed, LiveFreq);
  if (St != NWOAS_RTC_OK) {
    DEBUG ((DEBUG_ERROR, "NwoasSeedRtc: seed invalid (%a) seed.cntfrq=%u live=%lu epoch=%lu; GetTime => NOT_READY\n",
            nwoas_rtc_status_str (St), Seed.cntfrq, LiveFreq, Seed.epoch));
    return EFI_SUCCESS;
  }

  //
  // Prove the seed is usable from this counter domain right now: the current
  // CNTPCT must not be below the snapshot and the advanced time must convert.
  //
  {
    UINT64  Epoch;
    UINT32  Ns;

    St = nwoas_rtc_advance (&Seed, LiveCount, LiveFreq, &Epoch, &Ns);
    if (St == NWOAS_RTC_OK) {
      St = nwoas_rtc_epoch_to_calendar (Epoch, &Cal);
    }
    if (St != NWOAS_RTC_OK) {
      DEBUG ((DEBUG_ERROR, "NwoasSeedRtc: seed unusable (%a) seed.cntpct=%lx now=%lx; GetTime => NOT_READY\n",
              nwoas_rtc_status_str (St), Seed.cntpct, LiveCount));
      return EFI_SUCCESS;
    }
  }

  CopyMem (&mSeed, &Seed, sizeof (mSeed));
  mFreqHz    = LiveFreq;
  mSeedValid = TRUE;

  DEBUG ((DEBUG_INFO,
          "NwoasSeedRtc: BOOT SEED (not native RTC) flags=%x epoch=%lu cntpct=%lx cntfrq=%lu now=%lx => %04u-%02u-%02u %02u:%02u:%02u UTC\n",
          Seed.flags, Seed.epoch, Seed.cntpct, LiveFreq, LiveCount,
          Cal.year, Cal.month, Cal.day, Cal.hour, Cal.minute, Cal.second));

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
