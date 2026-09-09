/* SPDX-License-Identifier: MIT
 *
 * S188 review test: copy of the S185 review test, compiling the REAL S188
 * NwoasHardwareBootRtcLib.c (from ../NwoasHardwareBootRtcLib/) against host
 * shim headers and driving LibRtcInitialize / LibGetTime with a mock SPMI
 * controller, mock ADT and a mock physical counter. Nothing touches hardware
 * or any file outside rtc-s188/review-tests/.
 *
 * S188 change under test: an unseeded GetTime returns EFI_DEVICE_ERROR (S185
 * returned EFI_NOT_READY). The unseeded path is additionally required to
 * touch neither the counter nor CNTFRQ, which keeps it distinguishable from
 * the seeded DEVICE_ERROR paths (frequency change, counter backwards, 2099).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <PiDxe.h>
#include <Library/RealTimeClockLib.h>
#include <Library/AppleDTLib.h>
#include "nwoas_sera_rtc.h"
#include "NwoasRtcSeedCore.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; \
  fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
  if (_a == _b) g_pass++; else { g_fail++; \
  fprintf (stderr, "FAIL %s:%d: %s = %llu (0x%llx), want %s = %llu (0x%llx)\n", __FILE__, __LINE__, #a, _a, _a, #b, _b, _b); } } while (0)

/* ---------------- DEBUG capture ---------------- */
static char g_last_debug[512];
static unsigned g_debug_calls;

VOID ShimDebugPrint (UINTN Level, CONST CHAR8 *Fmt, ...)
{
  /* EDK2 %a is an ASCII string; %l is 64-bit. On this host long is 64-bit so
   * only %a needs translation. */
  char fmt[512]; size_t i = 0, o = 0;
  (void)Level;
  while (Fmt[i] && o + 2 < sizeof fmt) {
    if (Fmt[i] == '%' && Fmt[i + 1] == 'a') { fmt[o++] = '%'; fmt[o++] = 's'; i += 2; }
    else fmt[o++] = Fmt[i++];
  }
  fmt[o] = 0;
  va_list ap; va_start (ap, Fmt);
  vsnprintf (g_last_debug, sizeof g_last_debug, fmt, ap);
  va_end (ap);
  g_debug_calls++;
  if (getenv ("S188_REVIEW_VERBOSE")) fputs (g_last_debug, stdout);
}

/* ---------------- BaseLib / BaseMemoryLib ---------------- */
VOID MemoryFence (VOID) {}
VOID *CopyMem (VOID *Dest, CONST VOID *Src, UINTN Len) { return memmove (Dest, Src, Len); }
VOID *ZeroMem (VOID *Buf, UINTN Len) { return memset (Buf, 0, Len); }

/* ---------------- physical counter ---------------- */
static struct {
  uint64_t pct, frq, step;
  unsigned reads;
  unsigned jump_at_read;   /* 1-based index of the counter read that jumps; 0 = never */
  uint64_t jump;
  unsigned freq_change_after; /* after N freq reads, return freq+1 */
  unsigned freq_reads;
} g_clk;

UINT64 ArmGenericTimerGetSystemCount (VOID)
{
  g_clk.reads++;
  if (g_clk.jump_at_read && g_clk.reads == g_clk.jump_at_read) g_clk.pct += g_clk.jump;
  g_clk.pct += g_clk.step;
  return g_clk.pct;
}
UINTN ArmGenericTimerGetTimerFreq (VOID)
{
  g_clk.freq_reads++;
  if (g_clk.freq_change_after && g_clk.freq_reads > g_clk.freq_change_after) return (UINTN)(g_clk.frq + 1);
  return (UINTN)g_clk.frq;
}

/* ---------------- mock SPMI controller ---------------- */
static struct {
  uint32_t fifo[16]; unsigned head, tail;
  uint32_t tx_count;
  uint64_t counter_seq[2]; unsigned counter_idx;
  uint64_t offset;
  uint32_t header;
  unsigned latency;         /* STATUS polls before a reply lands */
  unsigned pending_words[3]; unsigned pending_n, pending_latency; int pending_active;
  int never_reply;
  int extra_word;
  unsigned cmd_writes; uint32_t cmd_log[8];
  unsigned status_reads, rsp_reads, bad_addr;
} g_bus;

static void bus_push (uint32_t w) { g_bus.fifo[g_bus.tail++ % 16] = w; }
static int  bus_empty (void) { return g_bus.head == g_bus.tail; }
static uint32_t bus_pop (void) { return bus_empty () ? 0xdeadbeefu : g_bus.fifo[g_bus.head++ % 16]; }

static void bus_deliver (void)
{
  unsigned i;
  for (i = 0; i < g_bus.pending_n; i++) bus_push (g_bus.pending_words[i]);
  if (g_bus.extra_word) bus_push (0x11111111u);
  g_bus.pending_active = 0;
}

UINT32 MmioRead32 (UINTN Address)
{
  if (Address == NWOAS_SERA_SPMI_STATUS) {
    g_bus.status_reads++;
    if (g_bus.pending_active) {
      if (g_bus.pending_latency == 0) bus_deliver (); else g_bus.pending_latency--;
    }
    return (bus_empty () ? NWOAS_SERA_STATUS_RX_EMPTY : 0u) | (g_bus.tx_count & 0xffu);
  }
  if (Address == NWOAS_SERA_SPMI_RSP) { g_bus.rsp_reads++; return bus_pop (); }
  g_bus.bad_addr++;
  return 0;
}

UINT32 MmioWrite32 (UINTN Address, UINT32 Value)
{
  if (Address != NWOAS_SERA_SPMI_CMD) { g_bus.bad_addr++; return Value; }
  if (g_bus.cmd_writes < 8) g_bus.cmd_log[g_bus.cmd_writes] = Value;
  g_bus.cmd_writes++;
  if (g_bus.never_reply) return Value;
  {
    uint64_t v;
    if (Value == NWOAS_SERA_CMD_READ_TIME) {
      v = g_bus.counter_seq[g_bus.counter_idx < 2 ? g_bus.counter_idx : 1]; g_bus.counter_idx++;
    } else if (Value == NWOAS_SERA_CMD_READ_OFFSET) {
      v = g_bus.offset;
    } else {
      return Value; /* unknown command: silence */
    }
    g_bus.pending_words[0] = g_bus.header;
    g_bus.pending_words[1] = (uint32_t)v;
    g_bus.pending_words[2] = (uint32_t)((v >> 32) & 0xffffu) | 0xabcd0000u; /* bytes 6..7 garbage */
    g_bus.pending_n = 3; g_bus.pending_latency = g_bus.latency; g_bus.pending_active = 1;
  }
  return Value;
}

/* ---------------- mock ADT ---------------- */
struct shim_prop { const char *name; const void *val; size_t len; };
struct shim_dt_node { const char *path; struct shim_prop props[6]; };

static uint32_t g_chip_id = 0x8103;
static uint64_t g_ctrl_reg[2] = { 0x3d0d9300ull, 0x100ull };
static uint32_t g_pmu_reg = 15, g_info_rtc = 0xd002, g_info_scr = 0xd100;
static size_t   g_ctrl_reg_len = 16, g_chip_id_len = 4, g_pmu_reg_len = 4;
static int      g_hide_chosen, g_hide_ctrl, g_hide_pmu, g_hide_info_rtc, g_hide_scr;

static struct shim_dt_node g_nodes[3];

static void adt_reset (void)
{
  g_chip_id = 0x8103; g_ctrl_reg[0] = 0x3d0d9300ull; g_ctrl_reg[1] = 0x100ull;
  g_pmu_reg = 15; g_info_rtc = 0xd002; g_info_scr = 0xd100;
  g_ctrl_reg_len = 16; g_chip_id_len = 4; g_pmu_reg_len = 4;
  g_hide_chosen = g_hide_ctrl = g_hide_pmu = g_hide_info_rtc = g_hide_scr = 0;
}

dt_node_t *dt_get (const char *name)
{
  if (!strcmp (name, "/chosen")) {
    if (g_hide_chosen) return NULL;
    g_nodes[0].path = name;
    g_nodes[0].props[0].name = "chip-id"; g_nodes[0].props[0].val = &g_chip_id; g_nodes[0].props[0].len = g_chip_id_len;
    g_nodes[0].props[1].name = NULL;
    return &g_nodes[0];
  }
  if (!strcmp (name, "/arm-io/nub-spmi")) {
    if (g_hide_ctrl) return NULL;
    g_nodes[1].path = name;
    g_nodes[1].props[0].name = "reg"; g_nodes[1].props[0].val = g_ctrl_reg; g_nodes[1].props[0].len = g_ctrl_reg_len;
    g_nodes[1].props[1].name = NULL;
    return &g_nodes[1];
  }
  if (!strcmp (name, "/arm-io/nub-spmi/spmi-pmu")) {
    unsigned i = 0;
    if (g_hide_pmu) return NULL;
    g_nodes[2].path = name;
    g_nodes[2].props[i].name = "reg"; g_nodes[2].props[i].val = &g_pmu_reg; g_nodes[2].props[i].len = g_pmu_reg_len; i++;
    if (!g_hide_info_rtc) { g_nodes[2].props[i].name = "info-rtc"; g_nodes[2].props[i].val = &g_info_rtc; g_nodes[2].props[i].len = 4; i++; }
    if (!g_hide_scr) { g_nodes[2].props[i].name = "info-rtc_scrpad"; g_nodes[2].props[i].val = &g_info_scr; g_nodes[2].props[i].len = 4; i++; }
    g_nodes[2].props[i].name = NULL;
    return &g_nodes[2];
  }
  return NULL;
}

void *dt_node_prop (dt_node_t *node, const char *prop, size_t *size)
{
  unsigned i;
  if (node == NULL) { fprintf (stderr, "dt_node_prop(NULL) would crash in AppleDTLib\n"); abort (); }
  for (i = 0; node->props[i].name; i++) {
    if (!strcmp (node->props[i].name, prop)) { if (size) *size = node->props[i].len; return (void *)node->props[i].val; }
  }
  if (size) *size = 0;
  return NULL;
}

/* ---------------- helpers ---------------- */

/* S183 hardware-result.json values are the raw 6 reply bytes as hex, i.e.
 * little-endian byte order. */
static uint64_t le_hex48 (const char *s)
{
  uint64_t v = 0; unsigned i;
  for (i = 0; i < 6; i++) { unsigned b; sscanf (s + 2 * i, "%2x", &b); v |= (uint64_t)b << (8 * i); }
  return v;
}

#define S183_DIRECT "5e88fc7a5008"
#define S183_CLKM   "fa437e3d2804"
#define S183_OFFSET "e370ae9e2831"
#define S183_EPOCH  1788983385ull
/* hardware-result.json "subticks" 13533 is the CLKM 33.15 fraction (1/32768 s).
 * The direct 32.16 path carries 2*13533 + 106 raw ticks = 27172 (1/65536 s). */
#define S183_SUBT_CLKM 13533u
#define S183_SUBT   (2u * S183_SUBT_CLKM + 106u)

static uint64_t civil (int y, int mo, int d, int h, int mi, int s)
{
  struct tm g; memset (&g, 0, sizeof g);
  g.tm_year = y - 1900; g.tm_mon = mo - 1; g.tm_mday = d; g.tm_hour = h; g.tm_min = mi; g.tm_sec = s;
  return (uint64_t)timegm (&g);
}

static void reset_all (void)
{
  memset (&g_clk, 0, sizeof g_clk);
  g_clk.pct = 345577004ull; g_clk.frq = 24000000ull; g_clk.step = 100;
  memset (&g_bus, 0, sizeof g_bus);
  g_bus.header = 0x003f0f3du;
  g_bus.counter_seq[0] = le_hex48 (S183_DIRECT) - 150; /* S180 showed 150 units over 3 txns */
  g_bus.counter_seq[1] = le_hex48 (S183_DIRECT);
  g_bus.offset = le_hex48 (S183_OFFSET);
  adt_reset ();
  g_last_debug[0] = 0; g_debug_calls = 0;
}

static EFI_STATUS init (void) { return LibRtcInitialize (NULL, NULL); }

static void efi_time_fill_garbage (EFI_TIME *t)
{
  memset (t, 0x5a, sizeof *t);
  t->TimeZone = 540; t->Daylight = 3; /* RealTimeClockRuntimeDxe pre-fills these */
}

static void expect_calendar (const EFI_TIME *t, uint64_t epoch)
{
  time_t tt = (time_t)epoch; struct tm g; gmtime_r (&tt, &g);
  CHECK_EQ (t->Year, g.tm_year + 1900); CHECK_EQ (t->Month, g.tm_mon + 1); CHECK_EQ (t->Day, g.tm_mday);
  CHECK_EQ (t->Hour, g.tm_hour); CHECK_EQ (t->Minute, g.tm_min); CHECK_EQ (t->Second, g.tm_sec);
}

/* ---------------- tests ---------------- */

static void test_s183_vector_arithmetic (void)
{
  uint64_t direct = le_hex48 (S183_DIRECT), clkm = le_hex48 (S183_CLKM), off = le_hex48 (S183_OFFSET);
  int64_t sec; uint32_t frac;
  puts ("S183 hardware vector: CLKM vs direct PMU and decoded UTC");
  CHECK_EQ (direct - 2 * clkm, 106);                       /* hardware-result.json direct_minus_twice_clkm_ticks */
  CHECK (nwoas_sera_rtc_convert (direct, off, &sec, &frac) == NWOAS_SERA_OK);
  CHECK_EQ (sec, S183_EPOCH); CHECK_EQ (frac, S183_SUBT);
  CHECK_EQ (((clkm + off) & NWOAS_SERA_MASK48) & 0x7fffu, S183_SUBT_CLKM); /* probe's subticks came from CLKM */
  /* The 33.15 CLKM word decoded with the Linux macsmc formula gives the same
   * second: sign_extend48(clkm + off) >> 15. */
  {
    uint64_t sum = (clkm + off) & NWOAS_SERA_MASK48;
    int64_t s = (sum & (1ull << 47)) ? -(int64_t)((1ull << 48) - sum) : (int64_t)sum;
    CHECK_EQ ((uint64_t)(s >> 15), S183_EPOCH);
  }
  /* And the earlier probe bug (Linux formula applied to the raw 32.16 counter)
   * reproduces the implausible 2031 epoch logged in rtc-bounded-probe.json. */
  {
    uint64_t c1 = le_hex48 ("1277fc7a5008");
    uint64_t sum = (c1 + off) & NWOAS_SERA_MASK48;
    CHECK_EQ (sum >> 15, 1928475477ull);
  }
}

static void test_init_success_and_gettime (void)
{
  EFI_TIME t; EFI_TIME_CAPABILITIES cap; EFI_STATUS st; uint64_t seed_pct;
  puts ("init: S183 vector over mock SPMI, then GetTime at the seed instant");
  reset_all ();
  st = init ();
  CHECK_EQ (st, EFI_SUCCESS);
  CHECK (strstr (g_last_debug, "DIRECT SERA") != NULL);
  CHECK (strstr (g_last_debug, "2026-09-09 19:49:45 UTC") != NULL);
  CHECK_EQ (g_bus.cmd_writes, 3); CHECK_EQ (g_bus.bad_addr, 0); CHECK_EQ (g_bus.rsp_reads, 9);
  CHECK_EQ (g_bus.cmd_log[0], NWOAS_SERA_CMD_READ_TIME); CHECK_EQ (g_bus.cmd_log[1], NWOAS_SERA_CMD_READ_OFFSET); CHECK_EQ (g_bus.cmd_log[2], NWOAS_SERA_CMD_READ_TIME);
  CHECK (bus_empty ());
  seed_pct = g_clk.pct; /* 'after' read; seed cntpct is one step earlier */

  /* Freeze the counter at the seed value: GetTime must report the seed second. */
  g_clk.step = 0; g_clk.pct = seed_pct - 100;
  efi_time_fill_garbage (&t); memset (&cap, 0, sizeof cap);
  st = LibGetTime (&t, &cap);
  CHECK_EQ (st, EFI_SUCCESS);
  expect_calendar (&t, S183_EPOCH);
  CHECK_EQ (t.Nanosecond, (uint32_t)(((uint64_t)S183_SUBT * 1000000000ull) >> 16));
  CHECK_EQ (t.TimeZone, EFI_UNSPECIFIED_TIMEZONE); CHECK_EQ (t.Daylight, 0); CHECK_EQ (t.Pad1, 0); CHECK_EQ (t.Pad2, 0);
  CHECK_EQ (cap.Resolution, 1); CHECK_EQ (cap.Accuracy, 100000000); CHECK_EQ (cap.SetsToZero, 0);

  /* Capabilities NULL is allowed. */
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  CHECK_EQ (LibGetTime (NULL, &cap), EFI_INVALID_PARAMETER);

  /* One hour later, no MMIO at runtime. */
  {
    unsigned s_before = g_bus.status_reads, r_before = g_bus.rsp_reads, w_before = g_bus.cmd_writes;
    g_clk.pct = seed_pct - 100 + 3600ull * 24000000ull;
    CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
    expect_calendar (&t, S183_EPOCH + 3600);
    CHECK_EQ (g_bus.status_reads, s_before); CHECK_EQ (g_bus.rsp_reads, r_before); CHECK_EQ (g_bus.cmd_writes, w_before);
  }
  /* Counter went backwards => EFI_DEVICE_ERROR, not a stale time. */
  g_clk.pct = seed_pct - 101;
  efi_time_fill_garbage (&t);
  CHECK_EQ (LibGetTime (&t, NULL), EFI_DEVICE_ERROR);
  /* Frequency changed => EFI_DEVICE_ERROR. */
  g_clk.pct = seed_pct; g_clk.freq_change_after = g_clk.freq_reads;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_DEVICE_ERROR);
  g_clk.freq_change_after = 0;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);

  CHECK_EQ (LibSetTime (&t), EFI_UNSUPPORTED);
  { BOOLEAN e, p; CHECK_EQ (LibGetWakeupTime (&e, &p, &t), EFI_UNSUPPORTED); CHECK_EQ (LibSetWakeupTime (TRUE, &t), EFI_UNSUPPORTED); }
  LibRtcVirtualNotifyEvent (NULL, NULL);
}

/* Seed the mock with an exact wall time (seconds + 1/65536 fraction) using a
 * zero-offset clock, then read back with GetTime. */
static uint64_t seed_at (uint64_t epoch, uint32_t frac)
{
  uint64_t counter = (epoch << 16) | frac;
  reset_all ();
  g_bus.offset = 0; g_bus.counter_seq[0] = counter; g_bus.counter_seq[1] = counter;
  CHECK_EQ (init (), EFI_SUCCESS);
  g_clk.step = 0;
  return g_clk.pct - 100; /* seed cntpct (init reads 'after' one step past it) */
}

static void test_nanosecond_carry_and_gregorian (void)
{
  EFI_TIME t; uint64_t seed_pct;
  puts ("GetTime: nanosecond carry from seed fraction + counter remainder, Gregorian rollovers");

  /* 2027-12-31T23:59:59Z + 65535/65536 s, then 0.99999995 s of counter: must
   * carry through second, minute, hour, day, month and year. */
  CHECK_EQ (civil (2027, 12, 31, 23, 59, 59), 1830297599ull);
  seed_pct = seed_at (1830297599ull, 65535);
  g_clk.pct = seed_pct + 23999999ull; /* rem*1e9/24e6 = 999999958 ns */
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  expect_calendar (&t, 1830297600ull);                 /* 2028-01-01T00:00:00Z */
  CHECK_EQ (t.Year, 2028); CHECK_EQ (t.Month, 1); CHECK_EQ (t.Day, 1);
  CHECK_EQ (t.Nanosecond, 999984741u + 999999958u - 1000000000u);
  CHECK (t.Nanosecond < 1000000000u);

  /* Same seed, no carry: remainder 0 => stays at 23:59:59.999984741. */
  g_clk.pct = seed_pct;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  expect_calendar (&t, 1830297599ull);
  CHECK_EQ (t.Nanosecond, 999984741u);

  /* Leap day: 2028-02-28T23:59:59Z + 1 s => 2028-02-29, and +1 day => 03-01. */
  CHECK_EQ (civil (2028, 1, 1, 0, 0, 0), 1830297600ull);
  seed_pct = seed_at (civil (2028, 2, 28, 23, 59, 59), 0);
  g_clk.pct = seed_pct + 24000000ull;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  CHECK_EQ (t.Year, 2028); CHECK_EQ (t.Month, 2); CHECK_EQ (t.Day, 29); CHECK_EQ (t.Hour, 0);
  expect_calendar (&t, civil (2028, 2, 29, 0, 0, 0));
  g_clk.pct = seed_pct + 24000000ull * 86401ull;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  CHECK_EQ (t.Month, 3); CHECK_EQ (t.Day, 1); CHECK_EQ (t.Second, 0);
  /* 2096-02-29 exists (2100 does not, but 2100 is outside the window anyway). */
  seed_pct = seed_at (civil (2096, 2, 28, 23, 59, 59), 0);
  g_clk.pct = seed_pct + 24000000ull;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  CHECK_EQ (t.Year, 2096); CHECK_EQ (t.Month, 2); CHECK_EQ (t.Day, 29); expect_calendar (&t, civil (2096, 2, 29, 0, 0, 0));
  /* Non-leap century-adjacent year: 2029-02-28 + 1 day => 03-01. */
  seed_pct = seed_at (civil (2029, 2, 28, 12, 0, 0), 0);
  g_clk.pct = seed_pct + 24000000ull * 86400ull;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  CHECK_EQ (t.Month, 3); CHECK_EQ (t.Day, 1); CHECK_EQ (t.Hour, 12);

  /* Window end: 2099-12-31T23:59:59Z is representable; a nanosecond carry
   * beyond it is an explicit EFI_DEVICE_ERROR, never a wrapped year. */
  seed_pct = seed_at (NWOAS_RTC_EPOCH_MAX, 65535);
  g_clk.pct = seed_pct;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  CHECK_EQ (t.Year, 2099); CHECK_EQ (t.Month, 12); CHECK_EQ (t.Day, 31); CHECK_EQ (t.Second, 59);
  g_clk.pct = seed_pct + 23999999ull;                  /* fraction carry only */
  CHECK_EQ (LibGetTime (&t, NULL), EFI_DEVICE_ERROR);
  g_clk.pct = seed_pct + 24000000ull;                  /* whole second past the window */
  CHECK_EQ (LibGetTime (&t, NULL), EFI_DEVICE_ERROR);

  /* Sweep: hourly steps across 2026..2030 agree with the host gmtime. */
  {
    uint64_t e0 = 1767225600ull; /* 2026-01-01T00:00:00Z */
    uint64_t k; int bad = 0;
    seed_pct = seed_at (e0, 0);
    for (k = 0; k < 5ull * 366 * 24; k += 7) {
      time_t tt; struct tm g;
      g_clk.pct = seed_pct + k * 3600ull * 24000000ull;
      if (LibGetTime (&t, NULL) != EFI_SUCCESS) { bad++; continue; }
      tt = (time_t)(e0 + k * 3600ull); gmtime_r (&tt, &g);
      if (t.Year != g.tm_year + 1900 || t.Month != g.tm_mon + 1 || t.Day != g.tm_mday ||
          t.Hour != g.tm_hour || t.Minute != g.tm_min || t.Second != g.tm_sec || t.Nanosecond != 0) bad++;
    }
    CHECK_EQ (bad, 0);
  }
}

static void expect_unseeded_device_error (const char *why, unsigned expected_cmd_writes)
{
  EFI_TIME t; EFI_TIME_CAPABILITIES cap;
  unsigned clk_reads, frq_reads;
  EFI_STATUS st = init ();
  CHECK_EQ (st, EFI_SUCCESS); /* init never fails the driver; DEVICE_ERROR is reported by GetTime */
  CHECK_EQ (g_bus.cmd_writes, expected_cmd_writes);
  CHECK_EQ (g_bus.bad_addr, 0);
  efi_time_fill_garbage (&t); memset (&cap, 0xff, sizeof cap);
  clk_reads = g_clk.reads; frq_reads = g_clk.freq_reads;
  st = LibGetTime (&t, &cap);
  if (st != EFI_DEVICE_ERROR) fprintf (stderr, "  case: %s\n", why);
  CHECK_EQ (st, EFI_DEVICE_ERROR);          /* S188: spec-listed status (S185: EFI_NOT_READY) */
  CHECK (st != EFI_NOT_READY);
  CHECK_EQ (g_clk.reads, clk_reads);        /* unseeded path never samples the counter... */
  CHECK_EQ (g_clk.freq_reads, frq_reads);   /* ...nor CNTFRQ */
  CHECK_EQ (cap.Resolution, 1); CHECK_EQ (cap.Accuracy, 100000000); CHECK_EQ (cap.SetsToZero, 0);
  CHECK (strstr (g_last_debug, "GetTime DEVICE_ERROR") != NULL);
  CHECK (strstr (g_last_debug, "NOT_READY") == NULL);
}

static void test_topology_guard (void)
{
  puts ("topology guard: any ADT mismatch => no CMD write, GetTime DEVICE_ERROR (S188)");
  reset_all (); g_chip_id = 0x8112;             expect_unseeded_device_error ("chip-id T8112", 0);
  reset_all (); g_chip_id_len = 2;              expect_unseeded_device_error ("chip-id short", 0);
  reset_all (); g_hide_chosen = 1;              expect_unseeded_device_error ("no /chosen", 0);
  reset_all (); g_hide_ctrl = 1;                expect_unseeded_device_error ("no nub-spmi", 0);
  reset_all (); g_hide_pmu = 1;                 expect_unseeded_device_error ("no spmi-pmu", 0);
  reset_all (); g_pmu_reg = 14;                 expect_unseeded_device_error ("sid 14", 0);
  reset_all (); g_hide_info_rtc = 1;            expect_unseeded_device_error ("no info-rtc", 0);
  reset_all (); g_info_rtc = 0xd000;            expect_unseeded_device_error ("info-rtc alarm ctrl", 0);
  reset_all (); g_hide_scr = 1;                 expect_unseeded_device_error ("no info-rtc_scrpad", 0);
  reset_all (); g_info_scr = 0xd008;            expect_unseeded_device_error ("scrpad wrong", 0);
  reset_all (); g_ctrl_reg[0] = 0x23d0d9300ull; expect_unseeded_device_error ("absolute instead of arm-io relative reg", 0);
  reset_all (); g_ctrl_reg[0] = 0x3d0d9400ull;  expect_unseeded_device_error ("wrong controller addr", 0);
  reset_all (); g_ctrl_reg[1] = 0x4000ull;      expect_unseeded_device_error ("wrong controller size", 0);
  reset_all (); g_ctrl_reg_len = 8;             expect_unseeded_device_error ("reg too short", 0);
  /* Robustness: an 8-byte sid cell and a longer reg (extra ranges) still pass. */
  reset_all (); g_pmu_reg_len = 8;              CHECK_EQ (init (), EFI_SUCCESS); CHECK (strstr (g_last_debug, "DIRECT SERA") != NULL);
  {
    static uint64_t regs4[4] = { 0x3d0d9300ull, 0x100ull, 0x3d2b0000ull, 0x4000ull };
    reset_all (); memcpy (g_ctrl_reg, regs4, 16); g_ctrl_reg_len = 32;
    /* dt_node_prop returns g_ctrl_reg (16 bytes) but reports 32; the code only copies 16. */
    CHECK_EQ (init (), EFI_SUCCESS); CHECK (strstr (g_last_debug, "DIRECT SERA") != NULL);
  }
}

static void test_read_failures (void)
{
  puts ("bus/decode failures: explicit DEVICE_ERROR (S188), bounded writes, no drain");
  reset_all (); g_bus.tx_count = 1;               expect_unseeded_device_error ("FIFO TX busy", 0);
  reset_all (); bus_push (0x12345678u);           expect_unseeded_device_error ("FIFO RX queued", 0); CHECK (!bus_empty ());
  reset_all (); g_bus.never_reply = 1;            expect_unseeded_device_error ("no reply", 1);
  CHECK (strstr (g_last_debug, "100 ms deadline") != NULL);
  CHECK (g_clk.pct - 345577004ull >= g_clk.frq / 10); /* deadline really elapsed on the mock counter */
  reset_all (); g_bus.header = 0x003f0f3cu;       expect_unseeded_device_error ("header echo", 1);
  reset_all (); g_bus.extra_word = 1;             expect_unseeded_device_error ("extra reply word", 1); CHECK (!bus_empty ());
  reset_all (); g_bus.counter_seq[1] = g_bus.counter_seq[0] - 1; expect_unseeded_device_error ("counter backwards", 3);
  reset_all (); g_bus.counter_seq[1] = g_bus.counter_seq[0] + NWOAS_SERA_MAX_COUNTER_SPAN + 1; expect_unseeded_device_error ("counter span", 3);
  reset_all (); g_bus.offset = 0; g_bus.counter_seq[0] = g_bus.counter_seq[1] = (946684799ull << 16); expect_unseeded_device_error ("1999-12-31", 3);
  reset_all (); g_bus.offset = 0; g_bus.counter_seq[0] = g_bus.counter_seq[1] = (4102444800ull << 16); expect_unseeded_device_error ("2100-01-01", 3);
  reset_all (); g_bus.offset = (1ull << 47); g_bus.counter_seq[0] = g_bus.counter_seq[1] = 0; expect_unseeded_device_error ("negative 49-bit sum", 3);
  /* Slow but in-time reply (latency polls) still succeeds. */
  reset_all (); g_bus.latency = 50; CHECK_EQ (init (), EFI_SUCCESS); CHECK (strstr (g_last_debug, "DIRECT SERA") != NULL);
  /* Implausible CNTFRQ: rejected before any MMIO. */
  reset_all (); g_clk.frq = 999999; expect_unseeded_device_error ("cntfrq too low", 0); CHECK_EQ (g_bus.status_reads, 0);
  reset_all (); g_clk.frq = 0;      expect_unseeded_device_error ("cntfrq zero", 0);
}

static void test_integration_window_guard (void)
{
  /* The helper's deadline does not cover the instant between the last
   * deadline check and its final CNTPCT sample. A jump there passes the
   * helper but must trip the integration's own 1 s window check. */
  puts ("integration guard: whole-read span > 1 s of CNTPCT is rejected even when the helper says OK");
  reset_all ();
  g_clk.jump_at_read = 17; g_clk.jump = 24000000ull * 2; /* read #17 = helper's out->cntpct sample */
  expect_unseeded_device_error ("span > 1 s", 3);
  CHECK (strstr (g_last_debug, "read failed ok") != NULL);
  /* Sanity: same jump one read later lands on 'after' and is also rejected;
   * a jump after 'after' (read #19+) is not observed by init at all. */
  reset_all (); g_clk.jump_at_read = 18; g_clk.jump = 24000000ull * 2; expect_unseeded_device_error ("span > 1 s (after)", 3);
  reset_all (); g_clk.jump_at_read = 19; g_clk.jump = 24000000ull * 2; CHECK_EQ (init (), EFI_SUCCESS); CHECK (strstr (g_last_debug, "DIRECT SERA") != NULL);
  CHECK_EQ (g_clk.reads, 18);
}

static void test_reinit_resets_state (void)
{
  EFI_TIME t;
  puts ("re-init: a later failed init clears an earlier valid seed");
  reset_all (); CHECK_EQ (init (), EFI_SUCCESS); g_clk.step = 0;
  CHECK_EQ (LibGetTime (&t, NULL), EFI_SUCCESS);
  g_bus.never_reply = 1; g_bus.cmd_writes = 0; g_bus.counter_idx = 0;
  CHECK_EQ (init (), EFI_SUCCESS);
  CHECK_EQ (LibGetTime (&t, NULL), EFI_DEVICE_ERROR); /* S188: was EFI_NOT_READY */
}

int main (void)
{
  test_s183_vector_arithmetic ();
  test_init_success_and_gettime ();
  test_nanosecond_carry_and_gregorian ();
  test_topology_guard ();
  test_read_failures ();
  test_integration_window_guard ();
  test_reinit_resets_state ();
  printf ("%d checks, %d failures\n", g_pass + g_fail, g_fail);
  return g_fail ? 1 : 0;
}
