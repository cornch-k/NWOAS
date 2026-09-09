/* SPDX-License-Identifier: MIT
 *
 * Host boundary tests for NwoasRtcSeedCore (pure C, no hardware).
 * Build/run via ../run_tests.sh (plain + ASan/UBSan).
 *
 * Optional: define NWOAS_HAVE_TIMEBASELIB to also compare against the
 * EmbeddedPkg TimeBaseLib EpochToEfiTime() that the real firmware links.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "NwoasRtcSeedCore.h"

#ifdef NWOAS_HAVE_TIMEBASELIB
#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiSpec.h>
#include <Library/TimeBaseLib.h>
#endif

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

#define CHECK_ST(expr, want) do { \
    nwoas_rtc_status_t _st = (expr); \
    if (_st == (want)) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s -> %s (want %s)\n", __FILE__, __LINE__, \
                             #expr, nwoas_rtc_status_str(_st), nwoas_rtc_status_str(want)); } \
  } while (0)

/* Reference blob produced by rtc-s178/rtc_math.py pack_snapshot(
 *   epoch=1788998400 (2026-09-10T00:00:00Z), cntpct=1e9, cntfrq=24e6,
 *   flags=VALID|SPMI) */
static const uint8_t k_ref_blob[32] = {
  0x43, 0x54, 0x52, 0x4e, 0x01, 0x00, 0x00, 0x00,
  0x00, 0xf3, 0xa1, 0x6a, 0x00, 0x00, 0x00, 0x00,
  0x00, 0xca, 0x9a, 0x3b, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x36, 0x6e, 0x01, 0x03, 0x00, 0x00, 0x00,
};
#define REF_EPOCH   1788998400ull
#define REF_CNTPCT  1000000000ull
#define REF_FREQ    24000000ull

static void
put_le32 (uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void
put_le64 (uint8_t *p, uint64_t v)
{
  put_le32 (p, (uint32_t)v);
  put_le32 (p + 4, (uint32_t)(v >> 32));
}

static void
make_blob (uint8_t *b, uint32_t magic, uint32_t ver, uint64_t epoch,
           uint64_t cntpct, uint32_t freq, uint32_t flags)
{
  put_le32 (b + 0, magic);
  put_le32 (b + 4, ver);
  put_le64 (b + 8, epoch);
  put_le64 (b + 16, cntpct);
  put_le32 (b + 24, freq);
  put_le32 (b + 28, flags);
}

static nwoas_rtc_seed_t
good_seed (void)
{
  nwoas_rtc_seed_t s;
  s.magic = NWOAS_RTC_SEED_MAGIC; s.version = NWOAS_RTC_SEED_VERSION;
  s.epoch = REF_EPOCH; s.cntpct = REF_CNTPCT; s.cntfrq = (uint32_t)REF_FREQ;
  s.flags = NWOAS_RTC_SEED_FLAG_VALID | NWOAS_RTC_SEED_FLAG_SPMI;
  return s;
}

/* ---------------------------------------------------------------- parse */

static void
test_parse (void)
{
  nwoas_rtc_seed_t s;
  uint8_t b[64];

  /* reference vector round-trips */
  CHECK_ST (nwoas_rtc_seed_parse (k_ref_blob, 32, &s), NWOAS_RTC_OK);
  CHECK (s.magic == NWOAS_RTC_SEED_MAGIC);
  CHECK (s.version == 1);
  CHECK (s.epoch == REF_EPOCH);
  CHECK (s.cntpct == REF_CNTPCT);
  CHECK (s.cntfrq == REF_FREQ);
  CHECK (s.flags == (NWOAS_RTC_SEED_FLAG_VALID | NWOAS_RTC_SEED_FLAG_SPMI));

  /* exact-size requirement: 31, 33, 0, 64 all rejected; output zeroed */
  memcpy (b, k_ref_blob, 32);
  memset (b + 32, 0xAA, 32);
  CHECK_ST (nwoas_rtc_seed_parse (b, 31, &s), NWOAS_RTC_E_SIZE);
  CHECK (s.magic == 0 && s.epoch == 0 && s.cntpct == 0 && s.cntfrq == 0 && s.flags == 0);
  CHECK_ST (nwoas_rtc_seed_parse (b, 33, &s), NWOAS_RTC_E_SIZE);
  CHECK_ST (nwoas_rtc_seed_parse (b, 0, &s), NWOAS_RTC_E_SIZE);
  CHECK_ST (nwoas_rtc_seed_parse (b, 64, &s), NWOAS_RTC_E_SIZE);

  /* NULL handling */
  CHECK_ST (nwoas_rtc_seed_parse (NULL, 32, &s), NWOAS_RTC_E_ARG);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, NULL), NWOAS_RTC_E_ARG);

  /* magic: byte-swapped, off by one, zero */
  make_blob (b, 0x4354524Eu, 1, REF_EPOCH, REF_CNTPCT, 24000000u, 1);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_MAGIC);
  make_blob (b, NWOAS_RTC_SEED_MAGIC + 1, 1, REF_EPOCH, REF_CNTPCT, 24000000u, 1);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_MAGIC);
  make_blob (b, 0, 1, REF_EPOCH, REF_CNTPCT, 24000000u, 1);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_MAGIC);

  /* version 0 and 2 rejected */
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 0, REF_EPOCH, REF_CNTPCT, 24000000u, 1);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_VERSION);
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 2, REF_EPOCH, REF_CNTPCT, 24000000u, 1);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_VERSION);

  /* flags: valid bit clear; unknown bit set; SMC source accepted */
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 1, REF_EPOCH, REF_CNTPCT, 24000000u, 0);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_FLAGS);
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 1, REF_EPOCH, REF_CNTPCT, 24000000u, NWOAS_RTC_SEED_FLAG_SPMI);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_FLAGS);
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 1, REF_EPOCH, REF_CNTPCT, 24000000u, 1u | 0x8u);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_FLAGS);
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 1, REF_EPOCH, REF_CNTPCT, 24000000u, 1u | 0x80000000u);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_E_FLAGS);
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 1, REF_EPOCH, REF_CNTPCT, 24000000u, 1u | NWOAS_RTC_SEED_FLAG_SMC);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_OK);

  /* parse does not judge epoch/freq (that is validate's job) */
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 1, 0, 0, 0, 1);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_OK);
  CHECK (s.epoch == 0 && s.cntfrq == 0);

  /* 64-bit fields decode all bytes (high halves included) */
  make_blob (b, NWOAS_RTC_SEED_MAGIC, 1, 0x0123456789ABCDEFull, 0xFEDCBA9876543210ull, 0xFFFFFFFFu, 1);
  CHECK_ST (nwoas_rtc_seed_parse (b, 32, &s), NWOAS_RTC_OK);
  CHECK (s.epoch == 0x0123456789ABCDEFull);
  CHECK (s.cntpct == 0xFEDCBA9876543210ull);
  CHECK (s.cntfrq == 0xFFFFFFFFu);
}

/* ------------------------------------------------------------- validate */

static void
test_validate (void)
{
  nwoas_rtc_seed_t s = good_seed ();

  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_OK);
  CHECK_ST (nwoas_rtc_seed_validate (NULL, REF_FREQ), NWOAS_RTC_E_ARG);

  /* frequency: zero seed, zero live, live too wide, mismatch by 1 Hz, both ways */
  s = good_seed (); s.cntfrq = 0;
  CHECK_ST (nwoas_rtc_seed_validate (&s, 0), NWOAS_RTC_E_FREQ);
  s = good_seed ();
  CHECK_ST (nwoas_rtc_seed_validate (&s, 0), NWOAS_RTC_E_FREQ);
  CHECK_ST (nwoas_rtc_seed_validate (&s, 0x100000000ull), NWOAS_RTC_E_FREQ);
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ + 1), NWOAS_RTC_E_FREQ_MISMATCH);
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ - 1), NWOAS_RTC_E_FREQ_MISMATCH);
  CHECK_ST (nwoas_rtc_seed_validate (&s, 19200000ull), NWOAS_RTC_E_FREQ_MISMATCH); /* Qualcomm-style */
  s.cntfrq = 0xFFFFFFFFu;
  CHECK_ST (nwoas_rtc_seed_validate (&s, 0xFFFFFFFFull), NWOAS_RTC_OK);

  /* epoch window: exact bounds accepted, one second outside rejected */
  s = good_seed (); s.epoch = NWOAS_RTC_EPOCH_MIN;
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_OK);
  s.epoch = NWOAS_RTC_EPOCH_MIN - 1;          /* 1999-12-31T23:59:59Z */
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_E_EPOCH_RANGE);
  s.epoch = NWOAS_RTC_EPOCH_MAX;
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_OK);
  s.epoch = NWOAS_RTC_EPOCH_MAX + 1;          /* 2100-01-01T00:00:00Z */
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_E_EPOCH_RANGE);
  s.epoch = 0;
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_E_EPOCH_RANGE);
  s.epoch = 0xFFFFFFFFFFFFFFFFull;
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_E_EPOCH_RANGE);
  s.epoch = 1651881600ull;                    /* 2022-05-07: Windows' bogus default is in-window */
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_OK);

  /* validate re-checks header so a hand-built struct cannot bypass parse */
  s = good_seed (); s.magic = 0;
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_E_MAGIC);
  s = good_seed (); s.flags = 0;
  CHECK_ST (nwoas_rtc_seed_validate (&s, REF_FREQ), NWOAS_RTC_E_FLAGS);
}

/* -------------------------------------------------------------- advance */

static void
test_advance (void)
{
  nwoas_rtc_seed_t s = good_seed ();
  uint64_t e;
  uint32_t ns;

  /* zero delta */
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH && ns == 0);

  /* matches rtc_math.uefi_now(): +1h and 12e6 ticks = 500 ms */
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ * 3600 + 12000000, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH + 3600 && ns == 500000000u);

  /* one tick short of a second: ns = floor((freq-1)*1e9/freq) */
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ - 1, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH && ns == 999999958u);
  CHECK (ns <= NWOAS_RTC_NS_MAX);

  /* exactly one second */
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH + 1 && ns == 0);

  /* counter underflow (went backwards by 1 and by a lot) */
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT - 1, REF_FREQ, &e, &ns), NWOAS_RTC_E_UNDERFLOW);
  CHECK (e == 0 && ns == 0);
  CHECK_ST (nwoas_rtc_advance (&s, 0, REF_FREQ, &e, &ns), NWOAS_RTC_E_UNDERFLOW);

  /* seed cntpct at UINT64_MAX: only equal is representable */
  s.cntpct = 0xFFFFFFFFFFFFFFFFull;
  CHECK_ST (nwoas_rtc_advance (&s, 0xFFFFFFFFFFFFFFFFull, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH);
  CHECK_ST (nwoas_rtc_advance (&s, 0xFFFFFFFFFFFFFFFEull, REF_FREQ, &e, &ns), NWOAS_RTC_E_UNDERFLOW);

  /* huge delta: UINT64_MAX ticks at 24 MHz is ~24k years -> overflow, no UB */
  s = good_seed (); s.cntpct = 0;
  CHECK_ST (nwoas_rtc_advance (&s, 0xFFFFFFFFFFFFFFFFull, REF_FREQ, &e, &ns), NWOAS_RTC_E_OVERFLOW);
  CHECK_ST (nwoas_rtc_advance (&s, 0xFFFFFFFFFFFFFFFFull, 1, &e, &ns), NWOAS_RTC_E_OVERFLOW);

  /* overflow boundary at 2099-12-31T23:59:59 */
  s = good_seed (); s.epoch = NWOAS_RTC_EPOCH_MAX;
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ - 1, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == NWOAS_RTC_EPOCH_MAX);
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ, REF_FREQ, &e, &ns), NWOAS_RTC_E_OVERFLOW);
  s.epoch = NWOAS_RTC_EPOCH_MAX - 1;
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == NWOAS_RTC_EPOCH_MAX);
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + 2 * REF_FREQ, REF_FREQ, &e, &ns), NWOAS_RTC_E_OVERFLOW);

  /* seed epoch outside window is refused even if advance would land inside */
  s = good_seed (); s.epoch = NWOAS_RTC_EPOCH_MIN - 1;
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ, REF_FREQ, &e, &ns), NWOAS_RTC_E_EPOCH_RANGE);

  /* frequency arguments */
  s = good_seed ();
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT, 0, &e, &ns), NWOAS_RTC_E_FREQ);
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT, 0x100000000ull, &e, &ns), NWOAS_RTC_E_FREQ);

  /* max representable frequency: remainder * 1e9 must not overflow */
  s.cntfrq = 0xFFFFFFFFu;
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + 0xFFFFFFFEull, 0xFFFFFFFFull, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH && ns == 999999999u);
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + 0xFFFFFFFFull, 0xFFFFFFFFull, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH + 1 && ns == 0);

  /* 1 Hz counter */
  s = good_seed (); s.cntfrq = 1;
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + 86400, 1, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH + 86400 && ns == 0);

  /* NULLs */
  CHECK_ST (nwoas_rtc_advance (NULL, 0, REF_FREQ, &e, &ns), NWOAS_RTC_E_ARG);
  CHECK_ST (nwoas_rtc_advance (&s, 0, REF_FREQ, NULL, &ns), NWOAS_RTC_E_ARG);
  CHECK_ST (nwoas_rtc_advance (&s, 0, REF_FREQ, &e, NULL), NWOAS_RTC_E_ARG);

  /* realistic session: 30-day uptime at 24 MHz stays exact */
  s = good_seed ();
  CHECK_ST (nwoas_rtc_advance (&s, REF_CNTPCT + REF_FREQ * 86400ull * 30ull + 1, REF_FREQ, &e, &ns), NWOAS_RTC_OK);
  CHECK (e == REF_EPOCH + 86400ull * 30ull && ns == 41u);
}

/* ------------------------------------------------------------- calendar */

static int
cal_is (const nwoas_rtc_calendar_t *c, unsigned y, unsigned mo, unsigned d,
        unsigned h, unsigned mi, unsigned s)
{
  return c->year == y && c->month == mo && c->day == d &&
         c->hour == h && c->minute == mi && c->second == s;
}

static void
test_calendar_known (void)
{
  nwoas_rtc_calendar_t c;

  CHECK (nwoas_rtc_is_leap_year (2000) == 1);   /* divisible by 400 */
  CHECK (nwoas_rtc_is_leap_year (2100) == 0);   /* divisible by 100 only */
  CHECK (nwoas_rtc_is_leap_year (2024) == 1);
  CHECK (nwoas_rtc_is_leap_year (2023) == 0);
  CHECK (nwoas_rtc_is_leap_year (1900) == 0);
  CHECK (nwoas_rtc_is_leap_year (2400) == 1);
  CHECK (nwoas_rtc_days_in_month (2024, 2) == 29);
  CHECK (nwoas_rtc_days_in_month (2023, 2) == 28);
  CHECK (nwoas_rtc_days_in_month (2000, 2) == 29);
  CHECK (nwoas_rtc_days_in_month (2099, 2) == 28);
  CHECK (nwoas_rtc_days_in_month (2024, 0) == 0);
  CHECK (nwoas_rtc_days_in_month (2024, 13) == 0);
  CHECK (nwoas_rtc_days_in_month (2024, 4) == 30);
  CHECK (nwoas_rtc_days_in_month (2024, 12) == 31);

  CHECK_ST (nwoas_rtc_epoch_to_calendar (946684800ull, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2000, 1, 1, 0, 0, 0));
  CHECK (c.timezone == NWOAS_RTC_TZ_UNSPECIFIED && c.daylight == 0 && c.nanosecond == 0);

  CHECK_ST (nwoas_rtc_epoch_to_calendar (946684799ull, &c), NWOAS_RTC_E_EPOCH_RANGE);
  CHECK (c.year == 0 && c.day == 0);
  CHECK_ST (nwoas_rtc_epoch_to_calendar (0, &c), NWOAS_RTC_E_EPOCH_RANGE);
  CHECK_ST (nwoas_rtc_epoch_to_calendar (4102444800ull, &c), NWOAS_RTC_E_EPOCH_RANGE);
  CHECK_ST (nwoas_rtc_epoch_to_calendar (0xFFFFFFFFFFFFFFFFull, &c), NWOAS_RTC_E_EPOCH_RANGE);
  CHECK_ST (nwoas_rtc_epoch_to_calendar (1, NULL), NWOAS_RTC_E_ARG);

  CHECK_ST (nwoas_rtc_epoch_to_calendar (4102444799ull, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2099, 12, 31, 23, 59, 59));

  CHECK_ST (nwoas_rtc_epoch_to_calendar (951782400ull, &c), NWOAS_RTC_OK);      /* 2000-02-29 */
  CHECK (cal_is (&c, 2000, 2, 29, 0, 0, 0));
  CHECK_ST (nwoas_rtc_epoch_to_calendar (951782400ull + 86400, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2000, 3, 1, 0, 0, 0));

  CHECK_ST (nwoas_rtc_epoch_to_calendar (1709164800ull, &c), NWOAS_RTC_OK);     /* 2024-02-29 */
  CHECK (cal_is (&c, 2024, 2, 29, 0, 0, 0));
  CHECK_ST (nwoas_rtc_epoch_to_calendar (1709164800ull - 1, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2024, 2, 28, 23, 59, 59));
  CHECK_ST (nwoas_rtc_epoch_to_calendar (1709164800ull + 86400, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2024, 3, 1, 0, 0, 0));

  /* 2023 is not leap: Feb 28 23:59:59 + 1 = Mar 1 */
  CHECK_ST (nwoas_rtc_epoch_to_calendar (1677628800ull - 1, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2023, 2, 28, 23, 59, 59));
  CHECK_ST (nwoas_rtc_epoch_to_calendar (1677628800ull, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2023, 3, 1, 0, 0, 0));

  /* year rollover */
  CHECK_ST (nwoas_rtc_epoch_to_calendar (1704067199ull, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2023, 12, 31, 23, 59, 59));
  CHECK_ST (nwoas_rtc_epoch_to_calendar (1704067200ull, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2024, 1, 1, 0, 0, 0));

  /* reference seed date and the 2036 upper plausibility bound used by rtc_math */
  CHECK_ST (nwoas_rtc_epoch_to_calendar (REF_EPOCH, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2026, 9, 10, 0, 0, 0));
  CHECK_ST (nwoas_rtc_epoch_to_calendar (2082758400ull, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2036, 1, 1, 0, 0, 0));

  /* a mid-day value */
  CHECK_ST (nwoas_rtc_epoch_to_calendar (REF_EPOCH + 13 * 3600 + 7 * 60 + 42, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2026, 9, 10, 13, 7, 42));
}

static void
test_calendar_validate (void)
{
  nwoas_rtc_calendar_t c;

  memset (&c, 0, sizeof c);
  c.year = 2026; c.month = 9; c.day = 10; c.hour = 23; c.minute = 59; c.second = 59;
  c.nanosecond = NWOAS_RTC_NS_MAX; c.timezone = NWOAS_RTC_TZ_UNSPECIFIED; c.daylight = 0;
  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  CHECK_ST (nwoas_rtc_calendar_validate (NULL), NWOAS_RTC_E_ARG);

  /* Day = 0: what the active VirtualRealTimeClockLib emits right after boot */
  c.day = 0;  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.day = 31; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR); /* Sep has 30 */
  c.day = 30; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);

  /* February rules */
  c.month = 2; c.year = 2023; c.day = 29; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.year = 2024;                         CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.day = 30;                            CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.year = 2000; c.day = 29;             CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.day = 28; c.year = 2099;             CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.day = 29;                            CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);

  /* month / year bounds */
  c.year = 2026; c.day = 1;
  c.month = 0;  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.month = 13; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.month = 12; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.year = 1999; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.year = 2100; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.year = 2019; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);   /* current firmware's fixed year */
  c.year = 2000; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.year = 2099; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);

  /* time-of-day */
  c.hour = 24;   CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR); c.hour = 23;
  c.minute = 60; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR); c.minute = 59;
  c.second = 60; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR); c.second = 59;
  c.nanosecond = 1000000000u; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.nanosecond = 0;

  /* time zone */
  c.timezone = 1440;  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.timezone = -1440; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.timezone = 1441;  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.timezone = -1441; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.timezone = 2046;  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.timezone = 2047;  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.timezone = 0;     CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);

  /* daylight */
  c.daylight = 1; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.daylight = 3; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);
  c.daylight = 2; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.daylight = 4; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
  c.daylight = 0xFF; CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_E_CALENDAR);
}

/* Exhaustive day sweep 2000-01-01 .. 2099-12-31: core vs libc gmtime_r and
 * round trip through calendar_to_epoch. Also samples odd seconds of day. */
static void
test_calendar_sweep (void)
{
  uint64_t epoch;
  uint64_t back;
  nwoas_rtc_calendar_t c;
  struct tm tmv;
  time_t t;
  int mism = 0;
  int rt = 0;
  int n = 0;
  static const uint32_t sod[] = { 0, 1, 3599, 3600, 43199, 43200, 82800, 86399 };
  size_t i;

  for (epoch = NWOAS_RTC_EPOCH_MIN; epoch <= NWOAS_RTC_EPOCH_MAX; epoch += 86400) {
    for (i = 0; i < sizeof sod / sizeof sod[0]; i++) {
      uint64_t e = epoch + sod[i];
      if (e > NWOAS_RTC_EPOCH_MAX) {
        continue;
      }
      n++;
      if (nwoas_rtc_epoch_to_calendar (e, &c) != NWOAS_RTC_OK) { mism++; continue; }
      t = (time_t)e;
      gmtime_r (&t, &tmv);
      if (!cal_is (&c, (unsigned)(tmv.tm_year + 1900), (unsigned)(tmv.tm_mon + 1), (unsigned)tmv.tm_mday,
                   (unsigned)tmv.tm_hour, (unsigned)tmv.tm_min, (unsigned)tmv.tm_sec)) {
        if (mism < 5) {
          fprintf (stderr, "mismatch at %llu: core %u-%02u-%02u %02u:%02u:%02u libc %d-%02d-%02d %02d:%02d:%02d\n",
                   (unsigned long long)e, c.year, c.month, c.day, c.hour, c.minute, c.second,
                   tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        }
        mism++;
      }
      if (nwoas_rtc_calendar_to_epoch (&c, &back) != NWOAS_RTC_OK || back != e) {
        rt++;
      }
    }
  }
  CHECK (mism == 0);
  CHECK (rt == 0);
  CHECK (n == 36525 * 8 - 0);   /* 100 years incl. 25 leap days, 8 samples/day */
  printf ("sweep: %d samples, %d libc mismatches, %d round-trip failures\n", n, mism, rt);
}

/* Every valid (year, month, day) round-trips; every invalid day is refused. */
static void
test_calendar_days_roundtrip (void)
{
  nwoas_rtc_calendar_t c;
  uint64_t e;
  unsigned y, m, d;
  int bad = 0;

  memset (&c, 0, sizeof c);
  c.timezone = NWOAS_RTC_TZ_UNSPECIFIED;
  for (y = 2000; y <= 2099; y++) {
    for (m = 1; m <= 12; m++) {
      for (d = 1; d <= 31; d++) {
        nwoas_rtc_calendar_t back;
        c.year = (uint16_t)y; c.month = (uint8_t)m; c.day = (uint8_t)d;
        if (d > nwoas_rtc_days_in_month (y, m)) {
          if (nwoas_rtc_calendar_to_epoch (&c, &e) != NWOAS_RTC_E_CALENDAR) bad++;
          continue;
        }
        if (nwoas_rtc_calendar_to_epoch (&c, &e) != NWOAS_RTC_OK) { bad++; continue; }
        if (nwoas_rtc_epoch_to_calendar (e, &back) != NWOAS_RTC_OK) { bad++; continue; }
        if (!cal_is (&back, y, m, d, 0, 0, 0)) bad++;
      }
    }
  }
  CHECK (bad == 0);
}

/* Pseudo-random epochs across the window vs libc (LCG, deterministic). */
static void
test_calendar_random (void)
{
  uint64_t x = 0x9E3779B97F4A7C15ull;
  int i;
  int bad = 0;

  for (i = 0; i < 200000; i++) {
    nwoas_rtc_calendar_t c;
    struct tm tmv;
    time_t t;
    uint64_t e;

    x = x * 6364136223846793005ull + 1442695040888963407ull;
    e = NWOAS_RTC_EPOCH_MIN + (x >> 11) % (NWOAS_RTC_EPOCH_MAX - NWOAS_RTC_EPOCH_MIN + 1);
    if (nwoas_rtc_epoch_to_calendar (e, &c) != NWOAS_RTC_OK) { bad++; continue; }
    t = (time_t)e;
    gmtime_r (&t, &tmv);
    if (!cal_is (&c, (unsigned)(tmv.tm_year + 1900), (unsigned)(tmv.tm_mon + 1), (unsigned)tmv.tm_mday,
                 (unsigned)tmv.tm_hour, (unsigned)tmv.tm_min, (unsigned)tmv.tm_sec)) bad++;
  }
  CHECK (bad == 0);
}

#ifdef NWOAS_HAVE_TIMEBASELIB
/* Compare against the exact EpochToEfiTime() the firmware links (read-only
 * compile of Common/TIANO/EmbeddedPkg/Library/TimeBaseLib/TimeBaseLib.c). */
static void
test_against_timebaselib (void)
{
  uint64_t epoch;
  int bad = 0;
  int n = 0;
  static const uint32_t sod[] = { 0, 86399, 43200 };
  size_t i;

  for (epoch = NWOAS_RTC_EPOCH_MIN; epoch <= NWOAS_RTC_EPOCH_MAX; epoch += 86400) {
    for (i = 0; i < sizeof sod / sizeof sod[0]; i++) {
      EFI_TIME et;
      nwoas_rtc_calendar_t c;
      uint64_t e = epoch + sod[i];
      if (e > NWOAS_RTC_EPOCH_MAX) continue;
      n++;
      EpochToEfiTime ((UINTN)e, &et);
      if (nwoas_rtc_epoch_to_calendar (e, &c) != NWOAS_RTC_OK) { bad++; continue; }
      if (!cal_is (&c, et.Year, et.Month, et.Day, et.Hour, et.Minute, et.Second)) {
        if (bad < 5) {
          fprintf (stderr, "TimeBaseLib mismatch at %llu: core %u-%02u-%02u %02u:%02u:%02u tbl %u-%02u-%02u %02u:%02u:%02u\n",
                   (unsigned long long)e, c.year, c.month, c.day, c.hour, c.minute, c.second,
                   et.Year, et.Month, et.Day, et.Hour, et.Minute, et.Second);
        }
        bad++;
      }
      /* the firmware's IsTimeValid must accept everything we emit */
      et.TimeZone = EFI_UNSPECIFIED_TIMEZONE; et.Daylight = 0; et.Nanosecond = 0;
      if (!IsTimeValid (&et)) bad++;
    }
  }
  CHECK (bad == 0);
  printf ("TimeBaseLib cross-check: %d samples, %d mismatches\n", n, bad);
}
#endif

/* -------------------------------------------------------------- driver */

/* End-to-end model of LibGetTime with a fake counter (no UEFI). */
static void
test_gettime_model (void)
{
  nwoas_rtc_seed_t s;
  nwoas_rtc_calendar_t c;
  uint64_t e;
  uint32_t ns;
  uint64_t live_freq = 24000000ull;

  /* seed absent -> NOT_READY is a policy of the UEFI wrapper; model the
   * parse->validate->advance->calendar chain that the wrapper runs. */
  CHECK_ST (nwoas_rtc_seed_parse (k_ref_blob, sizeof k_ref_blob, &s), NWOAS_RTC_OK);
  CHECK_ST (nwoas_rtc_seed_validate (&s, live_freq), NWOAS_RTC_OK);
  CHECK_ST (nwoas_rtc_advance (&s, s.cntpct + live_freq * 90 + 6000000, live_freq, &e, &ns), NWOAS_RTC_OK);
  CHECK_ST (nwoas_rtc_epoch_to_calendar (e, &c), NWOAS_RTC_OK);
  CHECK (cal_is (&c, 2026, 9, 10, 0, 1, 30));
  c.nanosecond = ns;
  CHECK (ns == 250000000u);
  CHECK_ST (nwoas_rtc_calendar_validate (&c), NWOAS_RTC_OK);

  /* frequency mismatch at runtime must be caught before advancing */
  CHECK_ST (nwoas_rtc_seed_validate (&s, 25000000ull), NWOAS_RTC_E_FREQ_MISMATCH);
}

int
main (void)
{
  test_parse ();
  test_validate ();
  test_advance ();
  test_calendar_known ();
  test_calendar_validate ();
  test_calendar_sweep ();
  test_calendar_days_roundtrip ();
  test_calendar_random ();
#ifdef NWOAS_HAVE_TIMEBASELIB
  test_against_timebaselib ();
#endif
  test_gettime_model ();

  printf ("%s: %d checks passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
