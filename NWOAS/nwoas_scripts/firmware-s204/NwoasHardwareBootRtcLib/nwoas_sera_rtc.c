/* SPDX-License-Identifier: MIT
 *
 * nwoas_sera_rtc.c - read-only T8103 sera PMU RTC snapshot over SPMI.
 * See nwoas_sera_rtc.h for the protocol facts and the integration contract.
 *
 * Portable C99: no libc, no UEFI, no allocation, no globals.
 */
#include "nwoas_sera_rtc.h"

/* ----------------------------------------------------------------------- */
/* pure helpers                                                            */
/* ----------------------------------------------------------------------- */

uint64_t
nwoas_sera_rtc_words_to_u48 (uint32_t w1, uint32_t w2)
{
  /* Linux spmi_read_cmd copies reply words byte-wise, little endian, into
   * buf[0..5]; bytes 6..7 (upper half of w2) are beyond len and dropped. */
  return ((uint64_t)w1) | (((uint64_t)(w2 & 0xffffu)) << 32);
}

uint32_t
nwoas_sera_rtc_frac_to_ns (uint32_t subseconds)
{
  uint64_t f = (uint64_t)(subseconds & NWOAS_SERA_FRAC_MASK);
  return (uint32_t)((f * 1000000000ull) >> NWOAS_SERA_FRAC_BITS);
}

nwoas_sera_status_t
nwoas_sera_rtc_convert (uint64_t counter48, uint64_t offset48,
                        int64_t *seconds_out, uint32_t *subseconds_out)
{
  uint64_t sum;
  int64_t  signed_time;
  int64_t  q, r;

  if (seconds_out == NULL || subseconds_out == NULL) {
    return NWOAS_SERA_E_ARG;
  }
  *seconds_out    = 0;
  *subseconds_out = 0;

  counter48 &= NWOAS_SERA_MASK48;
  offset48  &= NWOAS_SERA_MASK48;

  /* 48-bit + 49-bit fits in 50 bits; reduce modulo 2^49 (aplpmu_settime
   * stores ((T - C) >> 1) truncated to 48 bits, so T == (C + (O << 1)) mod 2^49). */
  sum = (counter48 + (offset48 << 1)) & NWOAS_SERA_MASK49;

  /* Sign-extend 49 bits without relying on implementation-defined shifts. */
  if (sum & (1ull << 48)) {
    signed_time = -(int64_t)((1ull << 49) - sum);
  } else {
    signed_time = (int64_t)sum;
  }

  /* Floor division by 65536 (C99 '/' truncates toward zero; fix remainder). */
  q = signed_time / 65536;
  r = signed_time % 65536;
  if (r < 0) {
    q -= 1;
    r += 65536;
  }

  *seconds_out    = q;
  *subseconds_out = (uint32_t)r;

  if (signed_time < 0) {
    return NWOAS_SERA_E_NEGATIVE_TIME;
  }
  if (q < NWOAS_SERA_UTC_MIN || q > NWOAS_SERA_UTC_MAX) {
    return NWOAS_SERA_E_RANGE;
  }
  return NWOAS_SERA_OK;
}

const char *
nwoas_sera_status_str (nwoas_sera_status_t st)
{
  switch (st) {
    case NWOAS_SERA_OK:                 return "ok";
    case NWOAS_SERA_E_ARG:              return "bad argument";
    case NWOAS_SERA_E_CNTFRQ:           return "implausible physical counter frequency";
    case NWOAS_SERA_E_FIFO_NOT_IDLE:    return "SPMI FIFO not idle; queued transaction preserved, nothing written";
    case NWOAS_SERA_E_TIMEOUT:          return "100 ms deadline expired waiting for SPMI reply";
    case NWOAS_SERA_E_POLL_CAP:         return "poll cap reached (frozen counter or stuck controller)";
    case NWOAS_SERA_E_BAD_HEADER:       return "SPMI reply header does not echo sid/opcode";
    case NWOAS_SERA_E_EXTRA_RESPONSE:   return "unexpected extra SPMI reply left in FIFO";
    case NWOAS_SERA_E_COUNTER_BACKWARD: return "RTC counter went backwards between reads";
    case NWOAS_SERA_E_COUNTER_SPAN:     return "RTC counter advanced too far between reads";
    case NWOAS_SERA_E_NEGATIVE_TIME:    return "counter + offset is negative; no valid clock";
    case NWOAS_SERA_E_RANGE:            return "decoded time outside 2000..2099; no valid clock";
    default:                            return "unknown status";
  }
}

/* ----------------------------------------------------------------------- */
/* bus access                                                              */
/* ----------------------------------------------------------------------- */

typedef struct {
  const nwoas_sera_rtc_ops_t *ops;
  uint64_t deadline_ticks;   /* cntfrq / 10 */
} nwoas_sera_bus_t;

static int
nwoas_sera_fifo_idle (uint32_t status)
{
  return (status & NWOAS_SERA_STATUS_RX_EMPTY) != 0u &&
         (status & NWOAS_SERA_STATUS_TX_MASK) == 0u;
}

/* Wait until RX FIFO is not empty. Bounded by BOTH the physical counter
 * deadline and a finite poll count. The status read that observes data is the
 * last one; no RSP read happens here. */
static nwoas_sera_status_t
nwoas_sera_wait_rx (const nwoas_sera_bus_t *bus, uint64_t start)
{
  const nwoas_sera_rtc_ops_t *ops = bus->ops;
  uint32_t polls;

  for (polls = 0; polls < NWOAS_SERA_MAX_POLLS; polls++) {
    uint32_t status = ops->read32 (ops->ctx, NWOAS_SERA_SPMI_STATUS);
    uint64_t now;

    now = ops->physical_counter (ops->ctx);
    /* Unsigned wrap-safe elapsed; a counter that goes backwards yields a
     * huge value and trips the deadline, which is the safe outcome. */
    if ((now - start) >= bus->deadline_ticks) {
      return NWOAS_SERA_E_TIMEOUT;
    }
    if ((status & NWOAS_SERA_STATUS_RX_EMPTY) == 0u) {
      return NWOAS_SERA_OK;
    }
  }
  return NWOAS_SERA_E_POLL_CAP;
}

/* One EXT_READL transaction: idle check, exactly one CMD write, three RSP
 * words, then verify the FIFO is empty again. Never drains. */
static nwoas_sera_status_t
nwoas_sera_read_reg (const nwoas_sera_bus_t *bus, uint32_t cmd,
                     nwoas_sera_rtc_result_t *res, uint32_t index,
                     uint64_t *value48_out)
{
  const nwoas_sera_rtc_ops_t *ops = bus->ops;
  uint32_t words[NWOAS_SERA_RSP_WORDS];
  uint64_t start;
  uint32_t status;
  uint32_t i;
  nwoas_sera_status_t st;

  *value48_out = 0;

  status = ops->read32 (ops->ctx, NWOAS_SERA_SPMI_STATUS);
  if (!nwoas_sera_fifo_idle (status)) {
    return NWOAS_SERA_E_FIFO_NOT_IDLE;
  }

  start = ops->physical_counter (ops->ctx);
  ops->write32 (ops->ctx, NWOAS_SERA_SPMI_CMD, cmd);
  res->cmd_writes++;

  for (i = 0; i < NWOAS_SERA_RSP_WORDS; i++) {
    st = nwoas_sera_wait_rx (bus, start);
    if (st != NWOAS_SERA_OK) {
      return st;
    }
    words[i] = ops->read32 (ops->ctx, NWOAS_SERA_SPMI_RSP);
  }

  res->headers[index] = words[0];
  if ((words[0] & NWOAS_SERA_RSP_ECHO_MASK) != NWOAS_SERA_RSP_ECHO) {
    return NWOAS_SERA_E_BAD_HEADER;
  }

  /* The FIFO must be empty again. If not, something else replied; leave it
   * exactly where it is so the owner can pick it up. */
  status = ops->read32 (ops->ctx, NWOAS_SERA_SPMI_STATUS);
  if ((status & NWOAS_SERA_STATUS_RX_EMPTY) == 0u) {
    return NWOAS_SERA_E_EXTRA_RESPONSE;
  }

  if ((ops->physical_counter (ops->ctx) - start) >= bus->deadline_ticks) {
    return NWOAS_SERA_E_TIMEOUT;
  }
  *value48_out = nwoas_sera_rtc_words_to_u48 (words[1], words[2]);
  res->transactions_done = index + 1u;
  return NWOAS_SERA_OK;
}

/* ----------------------------------------------------------------------- */
/* top level                                                               */
/* ----------------------------------------------------------------------- */

static void
nwoas_sera_result_clear (nwoas_sera_rtc_result_t *r)
{
  uint32_t i;
  r->utc_seconds = 0;
  r->subseconds  = 0;
  r->nanoseconds = 0;
  r->cntpct      = 0;
  r->cntfrq      = 0;
  r->counter0    = 0;
  r->offset      = 0;
  r->counter1    = 0;
  for (i = 0; i < NWOAS_SERA_TRANSACTIONS; i++) {
    r->headers[i] = 0;
  }
  r->transactions_done = 0;
  r->cmd_writes        = 0;
}

nwoas_sera_status_t
nwoas_sera_rtc_read (const nwoas_sera_rtc_ops_t *ops, nwoas_sera_rtc_result_t *out)
{
  nwoas_sera_bus_t bus;
  nwoas_sera_status_t st;
  uint64_t freq;
  int64_t  seconds;
  uint32_t frac;

  if (out == NULL) {
    return NWOAS_SERA_E_ARG;
  }
  nwoas_sera_result_clear (out);

  if (ops == NULL || ops->read32 == NULL || ops->write32 == NULL ||
      ops->physical_counter == NULL || ops->counter_frequency == NULL) {
    return NWOAS_SERA_E_ARG;
  }

  freq = ops->counter_frequency (ops->ctx);
  if (freq < NWOAS_SERA_CNTFRQ_MIN || freq > NWOAS_SERA_CNTFRQ_MAX) {
    return NWOAS_SERA_E_CNTFRQ;
  }
  out->cntfrq = freq;

  bus.ops            = ops;
  bus.deadline_ticks = (freq * NWOAS_SERA_DEADLINE_MS) / 1000u;

  st = nwoas_sera_read_reg (&bus, NWOAS_SERA_CMD_READ_TIME, out, 0, &out->counter0);
  if (st != NWOAS_SERA_OK) {
    return st;
  }
  st = nwoas_sera_read_reg (&bus, NWOAS_SERA_CMD_READ_OFFSET, out, 1, &out->offset);
  if (st != NWOAS_SERA_OK) {
    return st;
  }
  st = nwoas_sera_read_reg (&bus, NWOAS_SERA_CMD_READ_TIME, out, 2, &out->counter1);
  if (st != NWOAS_SERA_OK) {
    return st;
  }
  /* Pair the second counter sample with the physical counter as tightly as
   * the bus allows: the SPMI reply is at most one transaction old here. */
  out->cntpct = ops->physical_counter (ops->ctx);

  if (out->counter1 < out->counter0) {
    return NWOAS_SERA_E_COUNTER_BACKWARD;
  }
  if ((out->counter1 - out->counter0) > NWOAS_SERA_MAX_COUNTER_SPAN) {
    return NWOAS_SERA_E_COUNTER_SPAN;
  }

  st = nwoas_sera_rtc_convert (out->counter1, out->offset, &seconds, &frac);
  if (st != NWOAS_SERA_OK) {
    return st;
  }

  out->utc_seconds = seconds;
  out->subseconds  = frac;
  out->nanoseconds = nwoas_sera_rtc_frac_to_ns (frac);
  return NWOAS_SERA_OK;
}
