/* SPDX-License-Identifier: MIT
 *
 * Host tests for nwoas_sera_rtc: a mock SPMI controller behind the callback
 * API. Every test also asserts the write discipline: the only MMIO writes are
 * command words to base+4, at most three, with the exact expected values.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "nwoas_sera_rtc.h"

/* ----------------------------------------------------------------------- */
/* mock controller                                                         */
/* ----------------------------------------------------------------------- */

#define MOCK_FIFO 16
#define MOCK_LOG  16

typedef struct {
  uint64_t addr;
  uint32_t val;
} mock_write_t;

typedef struct {
  /* physical counter model */
  uint64_t pct;
  uint64_t frq;
  uint64_t pct_step;          /* ticks added per STATUS read */
  int      freeze_pct;

  /* RTC model. If derive_from_pct: counter = counter_base + elapsed pct
   * scaled to 1/65536 s. Otherwise the explicit sequence is used for the
   * 1st and 3rd read. */
  int      derive_from_pct;
  uint64_t pct_base;
  uint64_t counter_base;
  uint64_t counter_seq[2];
  unsigned counter_idx;
  uint64_t offset;

  /* reply behaviour */
  uint32_t header;
  unsigned reply_latency;     /* STATUS polls before the reply lands */
  int      never_reply;
  int      never_reply_from_txn; /* -1 = n/a; else transaction index that never replies */
  int      extra_word;        /* append one bogus word to each reply */
  int      truncate_reply;    /* deliver only the header word */

  /* controller FIFO state */
  uint32_t rx[MOCK_FIFO];
  unsigned rx_head, rx_count;
  uint32_t tx_count;
  int      pending;
  unsigned pending_polls;
  uint32_t pending_cmd;
  unsigned txn;

  /* accounting */
  mock_write_t writes[MOCK_LOG];
  unsigned nwrites;
  unsigned bad_writes;        /* writes to anything but CMD */
  unsigned status_reads, rsp_reads, rsp_underflow, bad_reads;
  uint64_t pct_at_first_write;
} mock_t;

static void mock_init (mock_t *m)
{
  memset (m, 0, sizeof (*m));
  m->frq = 24000000;
  m->pct = 702686355;
  m->pct_step = 24;           /* 1 us per poll */
  m->header = 0x003f0f3d;     /* observed on hardware */
  m->reply_latency = 3;
  m->never_reply_from_txn = -1;
  m->offset = 0x31289eae70e3ull;
  m->counter_seq[0] = 0x085075720348ull;
  m->counter_seq[1] = 0x0850757203deull;
}

static void mock_push (mock_t *m, uint32_t w)
{
  if (m->rx_count < MOCK_FIFO) {
    m->rx[(m->rx_head + m->rx_count) % MOCK_FIFO] = w;
    m->rx_count++;
  }
}

static uint64_t mock_counter_now (mock_t *m)
{
  if (m->derive_from_pct) {
    uint64_t elapsed = m->pct - m->pct_base;
    return (m->counter_base + (elapsed << 16) / m->frq) & NWOAS_SERA_MASK48;
  }
  {
    uint64_t v = m->counter_seq[m->counter_idx < 2 ? m->counter_idx : 1];
    m->counter_idx++;
    return v;
  }
}

static void mock_deliver (mock_t *m)
{
  uint64_t v;
  uint32_t addr = m->pending_cmd >> 16;
  if (addr == NWOAS_SERA_REG_TIME) {
    v = mock_counter_now (m);
  } else if (addr == NWOAS_SERA_REG_TIME_OFFSET) {
    v = m->offset & NWOAS_SERA_MASK48;
  } else {
    v = 0xdeadbeefcafeull;    /* should never happen: unexpected address */
  }
  mock_push (m, m->header);
  if (m->truncate_reply) {
    m->pending = 0;
    return;
  }
  mock_push (m, (uint32_t)(v & 0xffffffffu));
  /* upper half of the third word is garbage on purpose: bytes 6..7 are
   * outside the 6-byte payload and must be ignored by the decoder. */
  mock_push (m, (uint32_t)((v >> 32) & 0xffffu) | 0xa5a50000u);
  if (m->extra_word) {
    mock_push (m, 0x12345678u);
  }
  m->pending = 0;
}

static uint32_t mock_read32 (void *ctx, uint64_t addr)
{
  mock_t *m = (mock_t *)ctx;
  if (addr == NWOAS_SERA_SPMI_STATUS) {
    m->status_reads++;
    if (!m->freeze_pct) {
      m->pct += m->pct_step;
    }
    if (m->pending) {
      int silent = m->never_reply ||
                   (m->never_reply_from_txn >= 0 && (int)m->txn - 1 >= m->never_reply_from_txn);
      if (!silent) {
        if (m->pending_polls == 0) {
          mock_deliver (m);
        } else {
          m->pending_polls--;
        }
      }
    }
    return (m->rx_count == 0 ? NWOAS_SERA_STATUS_RX_EMPTY : 0u) |
           (m->tx_count & NWOAS_SERA_STATUS_TX_MASK);
  }
  if (addr == NWOAS_SERA_SPMI_RSP) {
    m->rsp_reads++;
    if (m->rx_count == 0) {
      m->rsp_underflow++;
      return 0;
    }
    {
      uint32_t w = m->rx[m->rx_head];
      m->rx_head = (m->rx_head + 1) % MOCK_FIFO;
      m->rx_count--;
      return w;
    }
  }
  m->bad_reads++;
  return 0;
}

static void mock_write32 (void *ctx, uint64_t addr, uint32_t val)
{
  mock_t *m = (mock_t *)ctx;
  if (m->nwrites < MOCK_LOG) {
    m->writes[m->nwrites].addr = addr;
    m->writes[m->nwrites].val = val;
  }
  if (m->nwrites == 0) {
    m->pct_at_first_write = m->pct;
  }
  m->nwrites++;
  if (addr != NWOAS_SERA_SPMI_CMD) {
    m->bad_writes++;
    return;
  }
  m->pending = 1;
  m->pending_polls = m->reply_latency;
  m->pending_cmd = val;
  m->txn++;
}

static uint64_t mock_pct (void *ctx)
{
  mock_t *m = (mock_t *)ctx;
  return m->pct;
}

static uint64_t mock_frq (void *ctx)
{
  mock_t *m = (mock_t *)ctx;
  return m->frq;
}

static nwoas_sera_rtc_ops_t mock_ops (mock_t *m)
{
  nwoas_sera_rtc_ops_t ops;
  ops.read32 = mock_read32;
  ops.write32 = mock_write32;
  ops.physical_counter = mock_pct;
  ops.counter_frequency = mock_frq;
  ops.ctx = m;
  return ops;
}

/* ----------------------------------------------------------------------- */
/* harness                                                                 */
/* ----------------------------------------------------------------------- */

static unsigned g_checks, g_failures;

#define CHECK(cond) do { \
  g_checks++; \
  if (!(cond)) { \
    g_failures++; \
    printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
  } \
} while (0)

#define CHECK_EQ_U64(a, b) do { \
  uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); \
  g_checks++; \
  if (_a != _b) { \
    g_failures++; \
    printf ("  FAIL %s:%d: %s = 0x%" PRIx64 " != %s = 0x%" PRIx64 "\n", \
            __FILE__, __LINE__, #a, _a, #b, _b); \
  } \
} while (0)

#define CHECK_EQ_I64(a, b) do { \
  int64_t _a = (int64_t)(a), _b = (int64_t)(b); \
  g_checks++; \
  if (_a != _b) { \
    g_failures++; \
    printf ("  FAIL %s:%d: %s = %" PRId64 " != %s = %" PRId64 "\n", \
            __FILE__, __LINE__, #a, _a, #b, _b); \
  } \
} while (0)

#define CHECK_ST(st, want) do { \
  nwoas_sera_status_t _s = (st); \
  g_checks++; \
  if (_s != (want)) { \
    g_failures++; \
    printf ("  FAIL %s:%d: status %d (%s), wanted %d (%s)\n", __FILE__, __LINE__, \
            (int)_s, nwoas_sera_status_str (_s), (int)(want), nwoas_sera_status_str (want)); \
  } \
} while (0)

/* Write discipline shared by every test: only CMD, only the known words, in
 * order, and never more than 'expected' of them. */
static void check_writes (const mock_t *m, unsigned expected)
{
  static const uint32_t seq[3] = {
    NWOAS_SERA_CMD_READ_TIME, NWOAS_SERA_CMD_READ_OFFSET, NWOAS_SERA_CMD_READ_TIME
  };
  unsigned i;
  CHECK (m->bad_writes == 0);
  CHECK (m->bad_reads == 0);
  CHECK (m->rsp_underflow == 0);
  CHECK (m->nwrites == expected);
  CHECK (m->nwrites <= NWOAS_SERA_TOTAL_CMD_WRITES);
  for (i = 0; i < m->nwrites && i < 3; i++) {
    CHECK_EQ_U64 (m->writes[i].addr, NWOAS_SERA_SPMI_CMD);
    CHECK_EQ_U64 (m->writes[i].val, seq[i]);
  }
}

/* aplpmu_settime: offset = ((T<<16 | frac) - counter) >> 1, stored in 48 bits. */
static uint64_t settime_offset (int64_t sec, uint32_t frac, uint64_t counter)
{
  uint64_t t = ((uint64_t)sec << 16) | (frac & 0xffffu);
  return ((t - counter) >> 1) & NWOAS_SERA_MASK48;
}

/* ----------------------------------------------------------------------- */
/* tests                                                                   */
/* ----------------------------------------------------------------------- */

static void test_constants (void)
{
  printf ("constants\n");
  CHECK_EQ_U64 (NWOAS_SERA_CMD_OPC_BYTE, 0x3d);
  CHECK_EQ_U64 (NWOAS_SERA_RSP_ECHO, 0x0f3d);
  /* Linux apple_spmi_pack_cmd(0x38, 15, 0xd002, 6) */
  CHECK_EQ_U64 ((0x38u | (15u << 8) | (0xd002u << 16) | 5u | (1u << 15)), NWOAS_SERA_CMD_READ_TIME);
  CHECK_EQ_U64 ((0x38u | (15u << 8) | (0xd100u << 16) | 5u | (1u << 15)), NWOAS_SERA_CMD_READ_OFFSET);
  CHECK_EQ_U64 (NWOAS_SERA_SPMI_STATUS, 0x23d0d9300ull);
  CHECK_EQ_U64 (NWOAS_SERA_SPMI_CMD, 0x23d0d9304ull);
  CHECK_EQ_U64 (NWOAS_SERA_SPMI_RSP, 0x23d0d9308ull);
  /* S180 header 4132669 decodes to sid 0xf / opc 0x3d echo */
  CHECK_EQ_U64 (4132669u & 0xffffu, NWOAS_SERA_RSP_ECHO);
  /* byte decode matches rtc_math.le48 */
  CHECK_EQ_U64 (nwoas_sera_rtc_words_to_u48 (0x78563412u, 0xffff9abcu), 0x9abc78563412ull);
  CHECK_EQ_U64 (nwoas_sera_rtc_words_to_u48 (0x75720348u, 0xa5a50850u), 0x085075720348ull);
}

static void test_hardware_vector (void)
{
  /* rtc-s178/hardware-s180-evidence.json, raw LE bytes:
   * counter0 480372755008  counter1 de0372755008  offset e370ae9e2831
   * headers 4132669 x3, cntpct0 702686355, cntpct1 702819479, cntfrq 24 MHz,
   * probe started at 1788981964.226 UTC. */
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;
  nwoas_sera_status_t st;

  printf ("hardware vector (S180)\n");
  mock_init (&m);
  ops = mock_ops (&m);
  st = nwoas_sera_rtc_read (&ops, &r);
  CHECK_ST (st, NWOAS_SERA_OK);
  CHECK_EQ_U64 (r.counter0, 0x085075720348ull);
  CHECK_EQ_U64 (r.counter1, 0x0850757203deull);
  CHECK_EQ_U64 (r.offset, 0x31289eae70e3ull);
  CHECK_EQ_U64 (r.headers[0], 4132669u);
  CHECK_EQ_U64 (r.headers[1], 4132669u);
  CHECK_EQ_U64 (r.headers[2], 4132669u);
  /* counter1 + (offset<<1) = 0x6aa1b2cee5a4 -> 1788981966 s, frac 0xe5a4 */
  CHECK_EQ_I64 (r.utc_seconds, 1788981966ll);
  CHECK_EQ_U64 (r.subseconds, 0xe5a4u);
  CHECK (r.utc_seconds >= 1788981964ll && r.utc_seconds <= 1788981966ll); /* within 2 s of host */
  CHECK_EQ_U64 (r.nanoseconds, nwoas_sera_rtc_frac_to_ns (0xe5a4u));
  CHECK (r.nanoseconds < 1000000000u);
  CHECK_EQ_U64 (r.cntfrq, 24000000u);
  CHECK (r.cntpct > 702686355ull);
  CHECK_EQ_U64 (r.transactions_done, 3);
  CHECK_EQ_U64 (r.cmd_writes, 3);
  CHECK_EQ_U64 (m.rsp_reads, 9);
  CHECK_EQ_U64 (m.rx_count, 0);
  check_writes (&m, 3);

  /* The old "raw counter + offset" reading (rtc_math.py, Linux CLKM formula
   * applied to the raw PMU counter) is what produced the bogus 2031 date. */
  {
    uint64_t wrong = (0x085075720348ull + 0x31289eae70e3ull) & NWOAS_SERA_MASK48;
    CHECK_EQ_U64 (wrong >> 15, 1928472640ull);
  }
}

static void test_convert_known_answers (void)
{
  int64_t sec;
  uint32_t frac;
  const int64_t t2026 = 1788981966ll; /* 2026-09-09T19:26:06Z */
  const uint64_t ctrs[] = { 0ull, 1ull, 0x123456789abcull, NWOAS_SERA_MASK48,
                            (1ull << 47) + 5ull, 0x085075720348ull };
  size_t i;

  printf ("convert: settime round trip, positive and negative offsets\n");
  for (i = 0; i < sizeof (ctrs) / sizeof (ctrs[0]); i++) {
    uint64_t off = settime_offset (t2026, 0x1234, ctrs[i]);
    CHECK_ST (nwoas_sera_rtc_convert (ctrs[i], off, &sec, &frac), NWOAS_SERA_OK);
    CHECK_EQ_I64 (sec, t2026);
    /* settime shifts (T - C) right by one, so an odd counter loses 1/65536 s */
    CHECK_EQ_U64 (frac, 0x1234u - (uint32_t)(ctrs[i] & 1u));
  }

  /* Negative offset: raw counter already past the wanted wall time. The sum
   * counter + (offset << 1) then exceeds 2^49 and must be reduced modulo
   * 2^49 (the faithful inverse of aplpmu_settime). A plain 64-bit add gives
   * a wrong date. A 48-bit mask happens to give the same seconds for every
   * VALID clock (2^49 is a multiple of 2^48) and only differs in how an
   * invalid clock is reported: bit 48 of the 49-bit sum is the sign. */
  {
    uint64_t ctr = (uint64_t)(t2026 + 100000) << 16;   /* counter 100000 s ahead */
    uint64_t off = settime_offset (t2026, 0, ctr);
    uint64_t raw = ctr + (off << 1);
    CHECK (off & (1ull << 47));                          /* looks "negative" in 48 bits */
    CHECK_ST (nwoas_sera_rtc_convert (ctr, off, &sec, &frac), NWOAS_SERA_OK);
    CHECK_EQ_I64 (sec, t2026);
    CHECK_EQ_U64 (frac, 0);
    /* the unreduced 64-bit sum is wrong, and it is exactly T + 2^49 */
    CHECK ((raw >> 16) != (uint64_t)t2026);
    CHECK_EQ_U64 (raw, ((uint64_t)t2026 << 16) + (1ull << 49));
    CHECK_EQ_U64 (raw & NWOAS_SERA_MASK49, (uint64_t)t2026 << 16);
    CHECK ((raw & (1ull << 48)) == 0);                   /* sign bit clear */
  }

  /* Wanted time before the counter by more than 2^48 units is not
   * representable: settime would produce an offset whose sum has bit 48 set,
   * which is what the negative check catches. */
  {
    uint64_t ctr = NWOAS_SERA_MASK48;                    /* counter at max, ~136 years */
    uint64_t off = settime_offset (t2026, 0x8000, ctr);
    uint64_t sum49 = (ctr + (off << 1)) & NWOAS_SERA_MASK49;
    /* T - C is negative and |T - C| < 2^48, so this one still decodes
     * (odd counter: fraction loses one unit, seconds intact) */
    CHECK_ST (nwoas_sera_rtc_convert (ctr, off, &sec, &frac), NWOAS_SERA_OK);
    CHECK_EQ_I64 (sec, t2026);
    CHECK_EQ_U64 (frac, 0x7fff);
    CHECK ((sum49 & (1ull << 48)) == 0);
  }

  /* Zero offset with counter exactly encoding the time */
  CHECK_ST (nwoas_sera_rtc_convert ((uint64_t)t2026 << 16, 0, &sec, &frac), NWOAS_SERA_OK);
  CHECK_EQ_I64 (sec, t2026);
  CHECK_EQ_U64 (frac, 0);

  /* Offset LSB weight is 2 units of 1/65536 s (33.15 vs 32.16) */
  CHECK_ST (nwoas_sera_rtc_convert ((uint64_t)t2026 << 16, 1, &sec, &frac), NWOAS_SERA_OK);
  CHECK_EQ_U64 (frac, 2);

  /* Inputs above 48 bits are masked, not trusted */
  CHECK_ST (nwoas_sera_rtc_convert (((uint64_t)t2026 << 16) | (0xffull << 48), 0, &sec, &frac), NWOAS_SERA_OK);
  CHECK_EQ_I64 (sec, t2026);

  /* NULL outputs */
  CHECK_ST (nwoas_sera_rtc_convert (0, 0, NULL, &frac), NWOAS_SERA_E_ARG);
  CHECK_ST (nwoas_sera_rtc_convert (0, 0, &sec, NULL), NWOAS_SERA_E_ARG);
}

static void test_fraction (void)
{
  int64_t sec;
  uint32_t frac;
  printf ("fixed-point fraction\n");
  CHECK_EQ_U64 (nwoas_sera_rtc_frac_to_ns (0), 0);
  CHECK_EQ_U64 (nwoas_sera_rtc_frac_to_ns (0x8000), 500000000u);
  CHECK_EQ_U64 (nwoas_sera_rtc_frac_to_ns (0x4000), 250000000u);
  CHECK_EQ_U64 (nwoas_sera_rtc_frac_to_ns (1), 15258u);
  CHECK_EQ_U64 (nwoas_sera_rtc_frac_to_ns (0xffff), 999984741u);
  CHECK_EQ_U64 (nwoas_sera_rtc_frac_to_ns (0x1ffff), 999984741u); /* masked */
  /* OpenBSD tv_usec = ((frac * 1000000) >> 16); ours agrees at us level */
  CHECK_EQ_U64 (nwoas_sera_rtc_frac_to_ns (0xe50e) / 1000u, ((0xe50eull * 1000000ull) >> 16));
  /* fraction survives the conversion for a mid-second time */
  {
    uint64_t ctr = (1788981966ull << 16) | 0xe50eull;
    CHECK_ST (nwoas_sera_rtc_convert (ctr, 0, &sec, &frac), NWOAS_SERA_OK);
    CHECK_EQ_U64 (frac, 0xe50e);
    CHECK_EQ_I64 (sec, 1788981966ll);
  }
  /* fraction carries correctly across the 49-bit wrap with a negative offset */
  {
    uint64_t ctr = ((1788981966ull + 7ull) << 16) | 0x0001ull;
    uint64_t off = settime_offset (1788981966ll, 0xfffe, ctr);
    CHECK_ST (nwoas_sera_rtc_convert (ctr, off, &sec, &frac), NWOAS_SERA_OK);
    CHECK_EQ_I64 (sec, 1788981966ll);
    /* settime drops bit 0 of the difference: (0xfffe - 1) is odd -> loses 1 unit */
    CHECK (frac == 0xfffe || frac == 0xfffd);
  }
}

static void test_overflow_and_bounds (void)
{
  int64_t sec;
  uint32_t frac;
  printf ("overflow, wrap and bounds\n");

  /* counter all ones + offset all ones: sum mod 2^49 = 2^48 - 3 (positive,
   * bit 48 clear) -> seconds 2^32 - 1 -> out of window */
  CHECK_ST (nwoas_sera_rtc_convert (NWOAS_SERA_MASK48, NWOAS_SERA_MASK48, &sec, &frac), NWOAS_SERA_E_RANGE);
  CHECK_EQ_I64 (sec, 0xffffffffll);
  CHECK_EQ_U64 (frac, 0xfffd);

  /* counter 0 + offset 2^48-1: sum = 2^49 - 2 -> signed -2 -> floor -> -1 s, frac 0xfffe */
  CHECK_ST (nwoas_sera_rtc_convert (0, NWOAS_SERA_MASK48, &sec, &frac), NWOAS_SERA_E_NEGATIVE_TIME);
  CHECK_EQ_I64 (sec, -1);
  CHECK_EQ_U64 (frac, 0xfffe);

  /* counter 2^48-1 + offset 1: sum = 2^48 + 1, bit 48 set -> negative */
  CHECK_ST (nwoas_sera_rtc_convert (NWOAS_SERA_MASK48, 1, &sec, &frac), NWOAS_SERA_E_NEGATIVE_TIME);
  CHECK (sec < 0);

  /* largest positive 49-bit value: 2^48 - 1 -> seconds 2^32 - 1 -> range */
  CHECK_ST (nwoas_sera_rtc_convert (NWOAS_SERA_MASK48, 0, &sec, &frac), NWOAS_SERA_E_RANGE);
  CHECK_EQ_I64 (sec, 0xffffffffll);

  /* most negative: sum = 2^48 exactly -> -2^48 -> seconds -2^32 */
  CHECK_ST (nwoas_sera_rtc_convert (0, 1ull << 47, &sec, &frac), NWOAS_SERA_E_NEGATIVE_TIME);
  CHECK_EQ_I64 (sec, -(1ll << 32));
  CHECK_EQ_U64 (frac, 0);

  /* zero clock (fresh PMU, no offset) is not a valid clock */
  CHECK_ST (nwoas_sera_rtc_convert (0, 0, &sec, &frac), NWOAS_SERA_E_RANGE);
  CHECK_EQ_I64 (sec, 0);

  /* window edges via offset arithmetic, both signs */
  CHECK_ST (nwoas_sera_rtc_convert (0x123456789abcull, settime_offset (946684799ll, 0xffff, 0x123456789abcull), &sec, &frac), NWOAS_SERA_E_RANGE);
  CHECK_EQ_I64 (sec, 946684799ll);
  CHECK_ST (nwoas_sera_rtc_convert (0x123456789abcull, settime_offset (946684800ll, 0, 0x123456789abcull), &sec, &frac), NWOAS_SERA_OK);
  CHECK_EQ_I64 (sec, 946684800ll);
  CHECK_ST (nwoas_sera_rtc_convert (0xf23456789abcull, settime_offset (4102444799ll, 0xffff, 0xf23456789abcull), &sec, &frac), NWOAS_SERA_OK);
  CHECK_EQ_I64 (sec, 4102444799ll);
  CHECK_EQ_U64 (frac, 0xfffe); /* settime drops bit 0 */
  CHECK_ST (nwoas_sera_rtc_convert (0xf23456789abcull, settime_offset (4102444800ll, 0, 0xf23456789abcull), &sec, &frac), NWOAS_SERA_E_RANGE);
  CHECK_EQ_I64 (sec, 4102444800ll);

  /* Exhaustive-ish: for many counters and wall times, settime then gettime
   * round-trips, including counters above the target (negative offsets). */
  {
    uint64_t c;
    int64_t t;
    unsigned ok = 0, total = 0;
    for (c = 0; c < NWOAS_SERA_MASK48; c += 0x0123456789abull) {
      for (t = NWOAS_SERA_UTC_MIN; t <= NWOAS_SERA_UTC_MAX; t += 123456789ll) {
        uint64_t off = settime_offset (t, 0x5a5a, c);
        uint32_t want_frac = 0x5a5au - (uint32_t)(c & 1u);
        total++;
        if (nwoas_sera_rtc_convert (c, off, &sec, &frac) == NWOAS_SERA_OK && sec == t && frac == want_frac) {
          ok++;
        }
      }
    }
    CHECK (total > 3000);
    CHECK (ok == total);
  }
}

static void test_full_read_negative_offset (void)
{
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;
  const int64_t want = 1800000000ll; /* 2027-01-15T08:00:00Z */

  printf ("full read with negative offset\n");
  mock_init (&m);
  m.counter_seq[0] = ((uint64_t)want + 5000000ull) << 16;      /* counter ~58 days ahead */
  m.counter_seq[1] = m.counter_seq[0] + 120;
  m.offset = settime_offset (want, 0, m.counter_seq[0]);
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  CHECK_EQ_I64 (r.utc_seconds, want);
  CHECK_EQ_U64 (r.subseconds, 120);   /* counter1 is 120 units later */
  check_writes (&m, 3);
}

static void test_derived_model (void)
{
  /* Counter derived from the physical counter: cntpct and utc must agree. */
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;
  const int64_t want = 1788981966ll;

  printf ("counter derived from CNTPCT (consistency of cntpct pairing)\n");
  mock_init (&m);
  m.derive_from_pct = 1;
  m.pct = 0xfffffffffff00000ull;   /* also exercises unsigned wrap of the deadline math */
  m.pct_base = m.pct;
  m.pct_step = 24000;              /* 1 ms per poll: whole read takes a few ms */
  m.counter_base = 0x085075720348ull;
  m.offset = settime_offset (want, 0, m.counter_base);
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  CHECK_EQ_I64 (r.utc_seconds, want);
  CHECK (r.counter1 >= r.counter0);
  CHECK (r.counter1 - r.counter0 <= NWOAS_SERA_MAX_COUNTER_SPAN);
  CHECK (r.cntpct - m.pct_base <= 24000000ull / 10ull * 3ull);
  /* sub-second must reflect elapsed pct: ~12 polls * 1 ms -> ~0.01 s */
  CHECK (r.subseconds < 0x1000);
  check_writes (&m, 3);
}

static void test_timeout (void)
{
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;

  printf ("timeout: no reply, counter advancing\n");
  mock_init (&m);
  m.never_reply = 1;
  m.pct_step = 2400;     /* 100 us per poll -> deadline after 1000 polls */
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_TIMEOUT);
  CHECK_EQ_U64 (r.transactions_done, 0);
  CHECK_EQ_U64 (r.cmd_writes, 1);
  CHECK (m.status_reads < NWOAS_SERA_MAX_POLLS);
  /* elapsed physical time is the deadline (2.4M ticks), within one step */
  CHECK (m.pct - m.pct_at_first_write >= 2400000ull);
  CHECK (m.pct - m.pct_at_first_write < 2400000ull + 2u * 2400ull);
  CHECK_EQ_U64 (m.rsp_reads, 0);
  check_writes (&m, 1);

  printf ("timeout: frozen physical counter hits the poll cap\n");
  mock_init (&m);
  m.never_reply = 1;
  m.freeze_pct = 1;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_POLL_CAP);
  CHECK_EQ_U64 (m.status_reads, NWOAS_SERA_MAX_POLLS + 1u); /* + the idle check */
  CHECK_EQ_U64 (m.rsp_reads, 0);
  CHECK_EQ_U64 (r.cmd_writes, 1);
  check_writes (&m, 1);

  printf ("timeout: counter frozen, but a slow reply still arrives\n");
  mock_init (&m);
  m.freeze_pct = 1;
  m.reply_latency = NWOAS_SERA_MAX_POLLS - 10u;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  check_writes (&m, 3);

  printf ("timeout: reply stops after the first transaction\n");
  mock_init (&m);
  m.never_reply_from_txn = 1;
  m.pct_step = 2400;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_TIMEOUT);
  CHECK_EQ_U64 (r.transactions_done, 1);
  CHECK_EQ_U64 (r.counter0, 0x085075720348ull);
  CHECK_EQ_U64 (r.offset, 0);
  check_writes (&m, 2);

  printf ("timeout: reply stops before the third transaction\n");
  mock_init (&m);
  m.never_reply_from_txn = 2;
  m.pct_step = 2400;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_TIMEOUT);
  CHECK_EQ_U64 (r.transactions_done, 2);
  CHECK_EQ_U64 (r.offset, 0x31289eae70e3ull);
  CHECK_EQ_I64 (r.utc_seconds, 0);
  check_writes (&m, 3);

  printf ("timeout: partial reply (header only) then silence\n");
  mock_init (&m);
  m.truncate_reply = 1;
  m.pct_step = 2400;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_TIMEOUT);
  CHECK_EQ_U64 (m.rsp_reads, 1);          /* consumed the header, then waited */
  CHECK_EQ_U64 (m.rx_count, 0);
  CHECK_EQ_U64 (r.transactions_done, 0);
  CHECK_EQ_U64 (r.headers[0], 0);         /* header only recorded once the reply is complete */
  check_writes (&m, 1);
}

static void test_shared_deadline (void)
{
  mock_t m; nwoas_sera_rtc_ops_t ops; nwoas_sera_rtc_result_t r;
  printf ("main: one deadline for all response words, including ready data\n");
  mock_init (&m); m.reply_latency=0; m.pct_step=m.frq/20;
  ops=mock_ops (&m);
  CHECK_ST(nwoas_sera_rtc_read(&ops,&r),NWOAS_SERA_E_TIMEOUT);
  CHECK_EQ_U64(m.rsp_reads,1); CHECK_EQ_U64(m.rx_count,2);
  check_writes(&m,1);
  mock_init (&m); m.reply_latency=0; m.pct_step=m.frq/5;
  ops=mock_ops (&m);
  CHECK_ST(nwoas_sera_rtc_read(&ops,&r),NWOAS_SERA_E_TIMEOUT);
  CHECK_EQ_U64(m.rsp_reads,0); CHECK_EQ_U64(m.rx_count,3);
  check_writes(&m,1);
}

static void test_fifo_not_idle (void)
{
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;

  printf ("non-idle FIFO: RX has a queued word -> no CMD write, no drain\n");
  mock_init (&m);
  mock_push (&m, 0xabcdef01u);
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_FIFO_NOT_IDLE);
  CHECK_EQ_U64 (m.nwrites, 0);
  CHECK_EQ_U64 (m.rsp_reads, 0);
  CHECK_EQ_U64 (m.rx_count, 1);           /* preserved */
  CHECK_EQ_U64 (m.rx[m.rx_head], 0xabcdef01u);
  CHECK_EQ_U64 (m.status_reads, 1);
  CHECK_EQ_U64 (r.cmd_writes, 0);
  check_writes (&m, 0);

  printf ("non-idle FIFO: TX count nonzero -> no CMD write\n");
  mock_init (&m);
  m.tx_count = 1;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_FIFO_NOT_IDLE);
  CHECK_EQ_U64 (m.nwrites, 0);
  CHECK_EQ_U64 (m.rsp_reads, 0);
  check_writes (&m, 0);

  printf ("non-idle FIFO: TX count in bit 7 only\n");
  mock_init (&m);
  m.tx_count = 0x80;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_FIFO_NOT_IDLE);
  check_writes (&m, 0);
}

static void test_malformed_response (void)
{
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;

  printf ("malformed: header echo wrong (opcode byte)\n");
  mock_init (&m);
  m.header = 0x003f0f38u;   /* bare opcode without length bits */
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_BAD_HEADER);
  CHECK_EQ_U64 (r.headers[0], 0x003f0f38u);
  CHECK_EQ_U64 (r.transactions_done, 0);
  CHECK_EQ_U64 (m.rsp_reads, 3);          /* the whole reply was consumed, nothing more */
  CHECK_EQ_U64 (m.rx_count, 0);
  check_writes (&m, 1);

  printf ("malformed: header echo wrong (sid byte)\n");
  mock_init (&m);
  m.header = 0x003f0e3du;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_BAD_HEADER);
  check_writes (&m, 1);

  printf ("malformed: header with byte order swapped\n");
  mock_init (&m);
  m.header = 0x3d0f3f00u;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_BAD_HEADER);
  check_writes (&m, 1);

  printf ("malformed: header upper bits differ from hardware are tolerated\n");
  mock_init (&m);
  m.header = 0x00000f3du;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  check_writes (&m, 3);

  printf ("malformed: extra reply word -> rejected, left in FIFO, not drained\n");
  mock_init (&m);
  m.extra_word = 1;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_EXTRA_RESPONSE);
  CHECK_EQ_U64 (m.rsp_reads, 3);
  CHECK_EQ_U64 (m.rx_count, 1);
  CHECK_EQ_U64 (m.rx[m.rx_head], 0x12345678u);
  CHECK_EQ_U64 (r.transactions_done, 0);
  CHECK_EQ_U64 (r.counter0, 0);           /* data not accepted */
  check_writes (&m, 1);

  printf ("malformed: garbage bytes 6..7 in third word are ignored\n");
  mock_init (&m);                         /* mock always sets 0xa5a5 there */
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  CHECK_EQ_U64 (r.counter0 >> 48, 0);
  CHECK_EQ_U64 (r.offset >> 48, 0);
}

static void test_counter_checks (void)
{
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;

  printf ("counter went backwards\n");
  mock_init (&m);
  m.counter_seq[1] = m.counter_seq[0] - 1;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_COUNTER_BACKWARD);
  CHECK_EQ_U64 (r.transactions_done, 3);
  CHECK_EQ_I64 (r.utc_seconds, 0);
  check_writes (&m, 3);

  printf ("counter span too large\n");
  mock_init (&m);
  m.counter_seq[1] = m.counter_seq[0] + NWOAS_SERA_MAX_COUNTER_SPAN + 1;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_COUNTER_SPAN);
  check_writes (&m, 3);

  printf ("counter span at the limit is accepted\n");
  mock_init (&m);
  m.counter_seq[1] = m.counter_seq[0] + NWOAS_SERA_MAX_COUNTER_SPAN;
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  check_writes (&m, 3);

  printf ("counter identical between reads is accepted\n");
  mock_init (&m);
  m.counter_seq[1] = m.counter_seq[0];
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  CHECK_EQ_I64 (r.utc_seconds, 1788981966ll);
  CHECK_EQ_U64 (r.subseconds, 0xe50e);

  printf ("full read: clock outside 2000..2099 is an explicit error\n");
  mock_init (&m);
  m.offset = settime_offset (915148800ll, 0, m.counter_seq[0]); /* 1999-01-01 */
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_RANGE);
  CHECK_EQ_I64 (r.utc_seconds, 0);
  check_writes (&m, 3);

  mock_init (&m);
  m.offset = 0;   /* fresh PMU: raw counter only -> 1970 + 35 days */
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_RANGE);

  mock_init (&m);
  m.counter_seq[0] = 0;
  m.counter_seq[1] = 0;
  m.offset = NWOAS_SERA_MASK48; /* negative */
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_NEGATIVE_TIME);
  check_writes (&m, 3);
}

static void test_arguments (void)
{
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;

  printf ("argument and frequency validation (no writes)\n");
  mock_init (&m);
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (NULL, &r), NWOAS_SERA_E_ARG);
  CHECK_ST (nwoas_sera_rtc_read (&ops, NULL), NWOAS_SERA_E_ARG);

  ops = mock_ops (&m); ops.read32 = NULL;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_ARG);
  ops = mock_ops (&m); ops.write32 = NULL;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_ARG);
  ops = mock_ops (&m); ops.physical_counter = NULL;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_ARG);
  ops = mock_ops (&m); ops.counter_frequency = NULL;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_ARG);

  ops = mock_ops (&m);
  m.frq = 0;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_CNTFRQ);
  m.frq = NWOAS_SERA_CNTFRQ_MIN - 1;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_CNTFRQ);
  m.frq = NWOAS_SERA_CNTFRQ_MAX + 1;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_CNTFRQ);
  m.frq = ~0ull;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_CNTFRQ);
  CHECK_EQ_U64 (m.nwrites, 0);
  CHECK_EQ_U64 (m.status_reads, 0);
  check_writes (&m, 0);

  m.frq = NWOAS_SERA_CNTFRQ_MAX;   /* deadline math must not overflow */
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  CHECK_EQ_U64 (r.cntfrq, NWOAS_SERA_CNTFRQ_MAX);

  /* result is cleared on entry even when a stale result is passed in */
  memset (&r, 0xff, sizeof (r));
  m.frq = 0;
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_E_CNTFRQ);
  CHECK_EQ_U64 (r.cmd_writes, 0);
  CHECK_EQ_I64 (r.utc_seconds, 0);

  printf ("status strings exist for all codes\n");
  {
    int i;
    for (i = 0; i <= (int)NWOAS_SERA_E_RANGE; i++) {
      const char *s = nwoas_sera_status_str ((nwoas_sera_status_t)i);
      CHECK (s != NULL && strcmp (s, "unknown status") != 0);
    }
    CHECK (strcmp (nwoas_sera_status_str ((nwoas_sera_status_t)99), "unknown status") == 0);
  }
}

static void test_exact_writes (void)
{
  /* The whole point: across a successful read the complete MMIO write log is
   * exactly three words to base+4, and nothing else. Also verified for every
   * error path above by check_writes(). */
  mock_t m;
  nwoas_sera_rtc_ops_t ops;
  nwoas_sera_rtc_result_t r;

  printf ("exactly the allowed writes\n");
  mock_init (&m);
  ops = mock_ops (&m);
  CHECK_ST (nwoas_sera_rtc_read (&ops, &r), NWOAS_SERA_OK);
  CHECK_EQ_U64 (m.nwrites, 3);
  CHECK_EQ_U64 (m.writes[0].addr, 0x23d0d9304ull); CHECK_EQ_U64 (m.writes[0].val, 0xd0028f3du);
  CHECK_EQ_U64 (m.writes[1].addr, 0x23d0d9304ull); CHECK_EQ_U64 (m.writes[1].val, 0xd1008f3du);
  CHECK_EQ_U64 (m.writes[2].addr, 0x23d0d9304ull); CHECK_EQ_U64 (m.writes[2].val, 0xd0028f3du);
  CHECK_EQ_U64 (m.bad_writes, 0);
  /* no PMU register address can ever appear in a write: all writes are to
   * the controller window, and the command words only carry EXT_READL */
  {
    unsigned i;
    for (i = 0; i < m.nwrites; i++) {
      CHECK ((m.writes[i].val & 0xffu) == 0x3du);            /* EXT_READL len 6 */
      CHECK ((m.writes[i].val & 0xf8u) != 0x78u);            /* never EXT_WRITEL (0x78) */
      CHECK (m.writes[i].addr >= NWOAS_SERA_SPMI_BASE && m.writes[i].addr < NWOAS_SERA_SPMI_BASE + 0x100u);
    }
  }
  /* reads: 3 idle checks + 3 x (3 polls-with-latency ... ) status reads, 9 RSP reads */
  CHECK_EQ_U64 (m.rsp_reads, 9);
}

int main (void)
{
  test_constants ();
  test_hardware_vector ();
  test_convert_known_answers ();
  test_fraction ();
  test_overflow_and_bounds ();
  test_full_read_negative_offset ();
  test_derived_model ();
  test_timeout ();
  test_shared_deadline ();
  test_fifo_not_idle ();
  test_malformed_response ();
  test_counter_checks ();
  test_arguments ();
  test_exact_writes ();

  printf ("%u checks, %u failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
