/* SPDX-License-Identifier: MIT
 *
 * NwoasRtcSeedCore.c - see NwoasRtcSeedCore.h. Pure C99, no libc calls,
 * no allocation, no I/O, no hardware. Compiled into both the UEFI library
 * and the host test binary.
 */
#include "NwoasRtcSeedCore.h"

/* ---- little-endian readers (no unaligned access, no host-endian assumption) */

static uint32_t
rd_le32 (const uint8_t *p)
{
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

static uint64_t
rd_le64 (const uint8_t *p)
{
  return (uint64_t)rd_le32 (p) | ((uint64_t)rd_le32 (p + 4) << 32);
}

/* ---- seed ------------------------------------------------------------- */

nwoas_rtc_status_t
nwoas_rtc_seed_parse (const uint8_t *buf, size_t len, nwoas_rtc_seed_t *out)
{
  nwoas_rtc_seed_t s;

  if (out == NULL) {
    return NWOAS_RTC_E_ARG;
  }
  /* Zero the output so a caller that ignores the status never sees stale
   * bytes masquerading as a seed. */
  out->magic   = 0;
  out->version = 0;
  out->epoch   = 0;
  out->cntpct  = 0;
  out->cntfrq  = 0;
  out->flags   = 0;

  if (buf == NULL) {
    return NWOAS_RTC_E_ARG;
  }
  if (len != NWOAS_RTC_SEED_SIZE) {
    return NWOAS_RTC_E_SIZE;
  }

  s.magic   = rd_le32 (buf + 0);
  s.version = rd_le32 (buf + 4);
  s.epoch   = rd_le64 (buf + 8);
  s.cntpct  = rd_le64 (buf + 16);
  s.cntfrq  = rd_le32 (buf + 24);
  s.flags   = rd_le32 (buf + 28);

  if (s.magic != NWOAS_RTC_SEED_MAGIC) {
    return NWOAS_RTC_E_MAGIC;
  }
  if (s.version != NWOAS_RTC_SEED_VERSION) {
    return NWOAS_RTC_E_VERSION;
  }
  if ((s.flags & NWOAS_RTC_SEED_FLAG_VALID) == 0 ||
      (s.flags & ~NWOAS_RTC_SEED_FLAGS_KNOWN) != 0) {
    return NWOAS_RTC_E_FLAGS;
  }

  *out = s;
  return NWOAS_RTC_OK;
}

nwoas_rtc_status_t
nwoas_rtc_seed_validate (const nwoas_rtc_seed_t *seed, uint64_t live_cntfrq)
{
  if (seed == NULL) {
    return NWOAS_RTC_E_ARG;
  }
  if (seed->magic != NWOAS_RTC_SEED_MAGIC || seed->version != NWOAS_RTC_SEED_VERSION) {
    return NWOAS_RTC_E_MAGIC;
  }
  if ((seed->flags & NWOAS_RTC_SEED_FLAG_VALID) == 0) {
    return NWOAS_RTC_E_FLAGS;
  }
  if (seed->cntfrq == 0 || live_cntfrq == 0 || live_cntfrq > 0xFFFFFFFFull) {
    return NWOAS_RTC_E_FREQ;
  }
  if ((uint64_t)seed->cntfrq != live_cntfrq) {
    return NWOAS_RTC_E_FREQ_MISMATCH;
  }
  if (seed->epoch < NWOAS_RTC_EPOCH_MIN || seed->epoch > NWOAS_RTC_EPOCH_MAX) {
    return NWOAS_RTC_E_EPOCH_RANGE;
  }
  return NWOAS_RTC_OK;
}

/* ---- advance ---------------------------------------------------------- */

nwoas_rtc_status_t
nwoas_rtc_advance (const nwoas_rtc_seed_t *seed, uint64_t cntpct_now,
                   uint64_t cntfrq, uint64_t *epoch_out, uint32_t *ns_out)
{
  uint64_t delta;
  uint64_t secs;
  uint64_t rem;
  uint64_t epoch;

  if (seed == NULL || epoch_out == NULL || ns_out == NULL) {
    return NWOAS_RTC_E_ARG;
  }
  *epoch_out = 0;
  *ns_out    = 0;

  if (cntfrq == 0 || cntfrq > 0xFFFFFFFFull) {
    return NWOAS_RTC_E_FREQ;
  }
  if (seed->epoch < NWOAS_RTC_EPOCH_MIN || seed->epoch > NWOAS_RTC_EPOCH_MAX) {
    return NWOAS_RTC_E_EPOCH_RANGE;
  }
  if (cntpct_now < seed->cntpct) {
    return NWOAS_RTC_E_UNDERFLOW;
  }

  delta = cntpct_now - seed->cntpct;      /* no wrap: checked above */
  secs  = delta / cntfrq;
  rem   = delta - secs * cntfrq;          /* rem < cntfrq <= 2^32 - 1 */

  /* seed->epoch <= 2^32 here, secs <= 2^64 / cntfrq; the sum cannot wrap
   * uint64 because secs <= 2^64 - 1 and epoch is tiny, but be explicit. */
  if (secs > NWOAS_RTC_EPOCH_MAX - seed->epoch) {
    return NWOAS_RTC_E_OVERFLOW;
  }
  epoch = seed->epoch + secs;

  /* rem < 2^32 and 1e9 < 2^30, so rem * 1e9 < 2^62: no 64-bit overflow. */
  *epoch_out = epoch;
  *ns_out    = (uint32_t)((rem * 1000000000ull) / cntfrq);
  return NWOAS_RTC_OK;
}

/* ---- calendar --------------------------------------------------------- */

int
nwoas_rtc_is_leap_year (uint32_t year)
{
  if ((year % 4u) != 0) {
    return 0;
  }
  if ((year % 100u) != 0) {
    return 1;
  }
  return (year % 400u) == 0;
}

uint8_t
nwoas_rtc_days_in_month (uint32_t year, uint32_t month)
{
  static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  if (month < 1 || month > 12) {
    return 0;
  }
  if (month == 2 && nwoas_rtc_is_leap_year (year)) {
    return 29;
  }
  return dim[month - 1];
}

/* Days since 1970-01-01 -> civil date. Howard Hinnant's algorithm, exact for
 * the proleptic Gregorian calendar; inputs here are always >= 0. */
static void
civil_from_days (uint64_t z, uint32_t *y, uint32_t *m, uint32_t *d)
{
  uint64_t era;
  uint64_t doe;
  uint64_t yoe;
  uint64_t doy;
  uint64_t mp;
  uint64_t yy;

  z  += 719468;                                     /* shift epoch to 0000-03-01 */
  era = z / 146097;
  doe = z - era * 146097;                           /* [0, 146096] */
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
  yy  = yoe + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);   /* [0, 365] */
  mp  = (5 * doy + 2) / 153;                        /* [0, 11] */
  *d  = (uint32_t)(doy - (153 * mp + 2) / 5 + 1);   /* [1, 31] */
  *m  = (uint32_t)(mp < 10 ? mp + 3 : mp - 9);      /* [1, 12] */
  *y  = (uint32_t)(yy + (*m <= 2 ? 1 : 0));
}

static uint64_t
days_from_civil (uint32_t y, uint32_t m, uint32_t d)
{
  uint64_t era;
  uint64_t yoe;
  uint64_t doy;
  uint64_t doe;

  y  -= (m <= 2) ? 1 : 0;
  era = y / 400;
  yoe = y - era * 400;                                            /* [0, 399] */
  doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;          /* [0, 365] */
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                    /* [0, 146096] */
  return era * 146097 + doe - 719468;
}

nwoas_rtc_status_t
nwoas_rtc_epoch_to_calendar (uint64_t epoch, nwoas_rtc_calendar_t *cal)
{
  uint64_t days;
  uint32_t sod;
  uint32_t y;
  uint32_t m;
  uint32_t d;

  if (cal == NULL) {
    return NWOAS_RTC_E_ARG;
  }
  cal->year = 0; cal->month = 0; cal->day = 0;
  cal->hour = 0; cal->minute = 0; cal->second = 0;
  cal->nanosecond = 0;
  cal->timezone = NWOAS_RTC_TZ_UNSPECIFIED;
  cal->daylight = 0;

  if (epoch < NWOAS_RTC_EPOCH_MIN || epoch > NWOAS_RTC_EPOCH_MAX) {
    return NWOAS_RTC_E_EPOCH_RANGE;
  }

  days = epoch / 86400u;
  sod  = (uint32_t)(epoch - days * 86400u);
  civil_from_days (days, &y, &m, &d);

  cal->year   = (uint16_t)y;
  cal->month  = (uint8_t)m;
  cal->day    = (uint8_t)d;
  cal->hour   = (uint8_t)(sod / 3600u);
  cal->minute = (uint8_t)((sod / 60u) % 60u);
  cal->second = (uint8_t)(sod % 60u);
  return NWOAS_RTC_OK;
}

nwoas_rtc_status_t
nwoas_rtc_calendar_to_epoch (const nwoas_rtc_calendar_t *cal, uint64_t *epoch_out)
{
  nwoas_rtc_status_t st;
  uint64_t days;

  if (epoch_out == NULL) {
    return NWOAS_RTC_E_ARG;
  }
  *epoch_out = 0;
  st = nwoas_rtc_calendar_validate (cal);
  if (st != NWOAS_RTC_OK) {
    return st;
  }
  days = days_from_civil (cal->year, cal->month, cal->day);
  *epoch_out = days * 86400u
             + (uint64_t)cal->hour * 3600u
             + (uint64_t)cal->minute * 60u
             + (uint64_t)cal->second;
  return NWOAS_RTC_OK;
}

nwoas_rtc_status_t
nwoas_rtc_calendar_validate (const nwoas_rtc_calendar_t *cal)
{
  if (cal == NULL) {
    return NWOAS_RTC_E_ARG;
  }
  if (cal->year < NWOAS_RTC_YEAR_MIN || cal->year > NWOAS_RTC_YEAR_MAX) {
    return NWOAS_RTC_E_CALENDAR;
  }
  if (cal->month < 1 || cal->month > 12) {
    return NWOAS_RTC_E_CALENDAR;
  }
  if (cal->day < 1 || cal->day > nwoas_rtc_days_in_month (cal->year, cal->month)) {
    return NWOAS_RTC_E_CALENDAR;
  }
  if (cal->hour > 23 || cal->minute > 59 || cal->second > 59) {
    return NWOAS_RTC_E_CALENDAR;
  }
  if (cal->nanosecond > NWOAS_RTC_NS_MAX) {
    return NWOAS_RTC_E_CALENDAR;
  }
  if (cal->timezone != NWOAS_RTC_TZ_UNSPECIFIED &&
      (cal->timezone < NWOAS_RTC_TZ_MIN || cal->timezone > NWOAS_RTC_TZ_MAX)) {
    return NWOAS_RTC_E_CALENDAR;
  }
  if (cal->daylight != 0 &&
      cal->daylight != NWOAS_RTC_DAYLIGHT_ADJUST &&
      cal->daylight != (NWOAS_RTC_DAYLIGHT_ADJUST | NWOAS_RTC_DAYLIGHT_IN)) {
    return NWOAS_RTC_E_CALENDAR;
  }
  return NWOAS_RTC_OK;
}

/* ---- misc ------------------------------------------------------------- */

const char *
nwoas_rtc_status_str (nwoas_rtc_status_t st)
{
  switch (st) {
    case NWOAS_RTC_OK:              return "ok";
    case NWOAS_RTC_E_ARG:           return "null-argument";
    case NWOAS_RTC_E_SIZE:          return "seed-size";
    case NWOAS_RTC_E_MAGIC:         return "seed-magic";
    case NWOAS_RTC_E_VERSION:       return "seed-version";
    case NWOAS_RTC_E_FLAGS:         return "seed-flags";
    case NWOAS_RTC_E_FREQ:          return "frequency";
    case NWOAS_RTC_E_FREQ_MISMATCH: return "frequency-mismatch";
    case NWOAS_RTC_E_EPOCH_RANGE:   return "epoch-range";
    case NWOAS_RTC_E_UNDERFLOW:     return "counter-underflow";
    case NWOAS_RTC_E_OVERFLOW:      return "time-overflow";
    case NWOAS_RTC_E_CALENDAR:      return "calendar-field";
    case NWOAS_RTC_E_NOT_READY:     return "not-ready";
    default:                        return "unknown";
  }
}
