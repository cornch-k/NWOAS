/* SPDX-License-Identifier: MIT
 *
 * NwoasRtcSeedCore.h - portable, allocation-free, pure C99 core for the
 * NWOAS boot-seeded UEFI real-time clock candidate (S181).
 *
 * This file has NO UEFI dependencies so the same code is compiled into the
 * UEFI RealTimeClockLib and into the host test binary (plain + sanitizers).
 *
 * Input seed: ADT /chosen property "nwoas,rtc-snapshot", 32 bytes, little
 * endian, Python struct format "<IIQQII":
 *
 *   offset  size  field    meaning
 *   0       4     magic    0x4E525443
 *   4       4     version  1
 *   8       8     epoch    Unix UTC seconds at the moment cntpct was sampled
 *   16      8     cntpct   CNTPCT_EL0 (physical counter) at that moment
 *   24      4     cntfrq   CNTFRQ_EL0 in Hz at that moment
 *   28      4     flags    bit0 = valid, bit1 = counter read over SPMI,
 *                          bit2 = counter read from SMC key CLKM
 *
 * Time model: utc(now) = seed.epoch + (CNTPCT_now - seed.cntpct) / cntfrq.
 * The physical counter is used deliberately; the m1n1 hypervisor only offsets
 * the *virtual* counter (CNTVOFF_EL2 = stolen time), never CNTPCT.
 *
 * Nothing here writes hardware. Nothing here reads hardware. Callers pass
 * counter values in.
 */
#ifndef NWOAS_RTC_SEED_CORE_H
#define NWOAS_RTC_SEED_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NWOAS_RTC_SEED_MAGIC        0x4E525443u
#define NWOAS_RTC_SEED_VERSION      1u
#define NWOAS_RTC_SEED_SIZE         32u

#define NWOAS_RTC_SEED_FLAG_VALID   0x1u
#define NWOAS_RTC_SEED_FLAG_SPMI    0x2u
#define NWOAS_RTC_SEED_FLAG_SMC     0x4u
#define NWOAS_RTC_SEED_FLAGS_KNOWN  (NWOAS_RTC_SEED_FLAG_VALID | \
                                     NWOAS_RTC_SEED_FLAG_SPMI  | \
                                     NWOAS_RTC_SEED_FLAG_SMC)

/* EFI_TIME representable window used by EmbeddedPkg TimeBaseLib IsTimeValid:
 * Year 2000..2099. Expressed as Unix seconds. */
#define NWOAS_RTC_EPOCH_MIN         946684800ull   /* 2000-01-01T00:00:00Z */
#define NWOAS_RTC_EPOCH_MAX         4102444799ull  /* 2099-12-31T23:59:59Z */
#define NWOAS_RTC_YEAR_MIN          2000u
#define NWOAS_RTC_YEAR_MAX          2099u

/* EFI_TIME conventions mirrored so the core can validate without UEFI headers. */
#define NWOAS_RTC_TZ_UNSPECIFIED    0x7FF   /* EFI_UNSPECIFIED_TIMEZONE = 2047 */
#define NWOAS_RTC_TZ_MIN            (-1440)
#define NWOAS_RTC_TZ_MAX            1440
#define NWOAS_RTC_DAYLIGHT_ADJUST   0x01    /* EFI_TIME_ADJUST_DAYLIGHT */
#define NWOAS_RTC_DAYLIGHT_IN       0x02    /* EFI_TIME_IN_DAYLIGHT */
#define NWOAS_RTC_NS_MAX            999999999u

typedef enum {
  NWOAS_RTC_OK = 0,
  NWOAS_RTC_E_ARG,            /* NULL pointer argument */
  NWOAS_RTC_E_SIZE,           /* seed blob is not exactly 32 bytes */
  NWOAS_RTC_E_MAGIC,          /* magic mismatch */
  NWOAS_RTC_E_VERSION,        /* version != 1 */
  NWOAS_RTC_E_FLAGS,          /* valid bit clear or unknown bits set */
  NWOAS_RTC_E_FREQ,           /* cntfrq == 0 or not representable */
  NWOAS_RTC_E_FREQ_MISMATCH,  /* seed cntfrq != live CNTFRQ_EL0 */
  NWOAS_RTC_E_EPOCH_RANGE,    /* seed epoch outside 2000..2099 */
  NWOAS_RTC_E_UNDERFLOW,      /* CNTPCT now < seed cntpct (counter went backwards) */
  NWOAS_RTC_E_OVERFLOW,       /* advanced time would leave the 2000..2099 window or wrap */
  NWOAS_RTC_E_CALENDAR,       /* calendar field out of range */
  NWOAS_RTC_E_NOT_READY       /* no valid seed loaded */
} nwoas_rtc_status_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint64_t epoch;
  uint64_t cntpct;
  uint32_t cntfrq;
  uint32_t flags;
} nwoas_rtc_seed_t;

/* Field layout and ranges follow EFI_TIME; TimeZone/Daylight are carried so
 * the validator can check them, the clock itself always emits
 * TimeZone = unspecified and Daylight = 0. */
typedef struct {
  uint16_t year;        /* 2000..2099 */
  uint8_t  month;       /* 1..12 */
  uint8_t  day;         /* 1..31 */
  uint8_t  hour;        /* 0..23 */
  uint8_t  minute;      /* 0..59 */
  uint8_t  second;      /* 0..59 */
  uint32_t nanosecond;  /* 0..999999999 */
  int16_t  timezone;    /* -1440..1440 or 2047 */
  uint8_t  daylight;    /* 0, 1, 3 */
} nwoas_rtc_calendar_t;

/* --- seed --------------------------------------------------------------- */

/* Decode a little-endian seed blob. Rejects len != 32 before touching buf
 * beyond bounds. Checks magic, version and flags. Does NOT check epoch or
 * frequency; see nwoas_rtc_seed_validate. */
nwoas_rtc_status_t nwoas_rtc_seed_parse (const uint8_t *buf, size_t len,
                                         nwoas_rtc_seed_t *out);

/* Semantic checks: cntfrq non-zero, live frequency non-zero and <= UINT32_MAX,
 * seed cntfrq == live frequency, epoch within 2000..2099. A frequency mismatch
 * is a hard error: the seed's cntpct would be in a different tick domain. */
nwoas_rtc_status_t nwoas_rtc_seed_validate (const nwoas_rtc_seed_t *seed,
                                            uint64_t live_cntfrq);

/* --- advance ------------------------------------------------------------ */

/* utc = seed.epoch + (cntpct_now - seed.cntpct) / cntfrq, plus nanoseconds
 * from the remainder. Errors: underflow if cntpct_now < seed.cntpct
 * (the counter is monotonic; a smaller value means a bad seed or a reset and
 * we refuse to guess), overflow if the result would exceed 2099-12-31. */
nwoas_rtc_status_t nwoas_rtc_advance (const nwoas_rtc_seed_t *seed,
                                      uint64_t cntpct_now, uint64_t cntfrq,
                                      uint64_t *epoch_out, uint32_t *ns_out);

/* --- calendar ----------------------------------------------------------- */

int      nwoas_rtc_is_leap_year (uint32_t year);
uint8_t  nwoas_rtc_days_in_month (uint32_t year, uint32_t month); /* 0 if month invalid */

/* Proleptic Gregorian, UTC. Fails with E_EPOCH_RANGE outside 2000..2099.
 * Sets nanosecond = 0, timezone = unspecified, daylight = 0. */
nwoas_rtc_status_t nwoas_rtc_epoch_to_calendar (uint64_t epoch,
                                                nwoas_rtc_calendar_t *cal);

/* Inverse (ignores nanosecond/timezone/daylight). Validates fields first. */
nwoas_rtc_status_t nwoas_rtc_calendar_to_epoch (const nwoas_rtc_calendar_t *cal,
                                                uint64_t *epoch_out);

/* Same rules as EmbeddedPkg TimeBaseLib IsTimeValid. */
nwoas_rtc_status_t nwoas_rtc_calendar_validate (const nwoas_rtc_calendar_t *cal);

/* --- misc --------------------------------------------------------------- */

const char *nwoas_rtc_status_str (nwoas_rtc_status_t st);

#ifdef __cplusplus
}
#endif

#endif /* NWOAS_RTC_SEED_CORE_H */
