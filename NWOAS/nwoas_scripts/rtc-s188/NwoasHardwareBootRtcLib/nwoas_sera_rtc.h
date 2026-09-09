/* SPDX-License-Identifier: MIT
 *
 * nwoas_sera_rtc.h - portable, allocation-free C99 helper that performs a
 * READ-ONLY snapshot of the Apple T8103 "sera" PMU real-time clock through
 * the SoC SPMI controller (S184).
 *
 * Scope
 * -----
 *   * Exactly three SPMI EXT_READL transactions: counter (0xd002), stored
 *     offset (0xd100), counter again (0xd002). Six bytes each.
 *   * The ONLY MMIO writes this code ever performs are the three command
 *     words to the controller CMD FIFO register (base + 4). It never writes a
 *     PMU register, never touches SMC, never resets or powers anything.
 *   * All hardware access goes through caller supplied callbacks, so the same
 *     object file runs in a UEFI DXE library and in host tests with a mock.
 *     No UEFI headers, no libc calls.
 *
 * Protocol facts (source: Linux drivers/spmi/spmi-apple-controller.c and
 * OpenBSD sys/arch/arm64/dev/aplpmu.c, plus the S180 hardware evidence in
 * rtc-s178/hardware-s180-evidence.json):
 *
 *   controller base   0x23d0d9300 (ADT /arm-io/nub-spmi reg0, t8103.dtsi)
 *   STATUS  base+0    bit24 = RX FIFO empty, bits 7..0 = TX FIFO count
 *   CMD     base+4    write one 32-bit command word
 *   RSP     base+8    read: [0] reply status/header, [1] data bytes 0..3,
 *                     [2] data bytes 4..5 in the low half
 *   command word      opc | sid<<8 | addr<<16 | (len-1) | 1<<15
 *                     EXT_READL opc 0x38, len 6 -> low byte 0x3d, sid 15
 *                     counter : 0xd0028f3d     offset : 0xd1008f3d
 *   reply header      hardware returned 0x003f0f3d for all three reads; the
 *                     low 16 bits echo sid<<8 | opc and are verified here.
 *
 * Time model (OpenBSD aplpmu.c, confirmed by S180 evidence to within 2 s):
 *
 *   counter  : 48-bit little endian, 32.16 fixed point seconds since epoch
 *   offset   : 48-bit little endian, 33.15 fixed point
 *   time     : (counter + (offset << 1)) taken modulo 2^49 and interpreted
 *              as a signed 49-bit two's complement 32.16 value
 *   seconds  : floor(time / 65536)     fraction: time mod 65536
 *
 * Why 49 bits: aplpmu_settime stores ((T - C) >> 1) truncated to 48 bits, so
 * counter + (offset << 1) equals T + k*2^49 and must be reduced modulo 2^49
 * (a plain 64-bit sum gives the wrong date for a "negative" offset, i.e.
 * wall time earlier than the raw counter). Bit 48 of the reduced sum is the
 * sign; a set sign bit means the stored clock is not a valid time. The Linux
 * macsmc formula sign_extend64(ctr + off, 47) >> 15 is the same arithmetic
 * expressed in 33.15 units. Because settime discards bit 0 of (T - C), an
 * odd raw counter reproduces the fraction one unit (15 us) low; seconds are
 * unaffected.
 *
 * Integration advice (UEFI)
 * -------------------------
 *   Call nwoas_sera_rtc_read() ONCE from LibRtcInitialize (DXE, before
 *   ExitBootServices) with callbacks that touch the physical MMIO window.
 *   Keep only the returned integers (utc_seconds, nanoseconds, cntpct,
 *   cntfrq) in module globals. LibGetTime must then advance that snapshot
 *   from CNTPCT_EL0/CNTFRQ_EL0 alone. Do not retain an MMIO pointer, do not
 *   call this helper at runtime: the SPMI window is not part of the runtime
 *   memory map and SetVirtualAddressMap will not translate it. The SMC "CLKM"
 *   comparison is a separate measurement and is not performed here.
 */
#ifndef NWOAS_SERA_RTC_H
#define NWOAS_SERA_RTC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- controller constants ---------------------------------------------- */

#define NWOAS_SERA_SPMI_BASE        0x23d0d9300ull
#define NWOAS_SERA_SPMI_STATUS      (NWOAS_SERA_SPMI_BASE + 0x0u)
#define NWOAS_SERA_SPMI_CMD         (NWOAS_SERA_SPMI_BASE + 0x4u)
#define NWOAS_SERA_SPMI_RSP         (NWOAS_SERA_SPMI_BASE + 0x8u)

#define NWOAS_SERA_STATUS_RX_EMPTY  (1u << 24)
#define NWOAS_SERA_STATUS_TX_MASK   0xffu

#define NWOAS_SERA_SID              0xfu
#define NWOAS_SERA_OPC_EXT_READL    0x38u
#define NWOAS_SERA_REG_LEN          6u
#define NWOAS_SERA_CMD_OPC_BYTE     (NWOAS_SERA_OPC_EXT_READL | (NWOAS_SERA_REG_LEN - 1u)) /* 0x3d */

#define NWOAS_SERA_REG_TIME         0xd002u
#define NWOAS_SERA_REG_TIME_OFFSET  0xd100u

/* Exact command words; nothing else is ever written. */
#define NWOAS_SERA_CMD_READ_TIME    0xd0028f3du
#define NWOAS_SERA_CMD_READ_OFFSET  0xd1008f3du

/* Expected low 16 bits of the reply header (sid << 8 | opc byte). */
#define NWOAS_SERA_RSP_ECHO_MASK    0xffffu
#define NWOAS_SERA_RSP_ECHO         ((NWOAS_SERA_SID << 8) | NWOAS_SERA_CMD_OPC_BYTE) /* 0x0f3d */

#define NWOAS_SERA_RSP_WORDS        3u
#define NWOAS_SERA_TRANSACTIONS     3u
#define NWOAS_SERA_TOTAL_CMD_WRITES 3u

/* --- policy limits ----------------------------------------------------- */

/* Per transaction: 100 ms of physical counter ticks AND a finite number
 * of status polls, whichever comes first. The poll cap guarantees termination
 * even if the physical counter is frozen or the callback lies. */
#define NWOAS_SERA_DEADLINE_MS      100u
#define NWOAS_SERA_MAX_POLLS        200000u

/* Physical counter frequency must be plausible for an ARM generic timer.
 * T8103 reports 24 MHz. */
#define NWOAS_SERA_CNTFRQ_MIN       1000000ull
#define NWOAS_SERA_CNTFRQ_MAX       4000000000ull

/* counter1 - counter0 (32.16 ticks) must be non-negative and small. Three
 * transactions took 6.8 ms and 150 ticks on hardware; allow half a second. */
#define NWOAS_SERA_MAX_COUNTER_SPAN 32768ull

/* Realistic wall-clock window, 2000-01-01T00:00:00Z .. 2099-12-31T23:59:59Z
 * (also the EFI_TIME representable range). */
#define NWOAS_SERA_UTC_MIN          946684800ll
#define NWOAS_SERA_UTC_MAX          4102444799ll

#define NWOAS_SERA_MASK48           0xffffffffffffull
#define NWOAS_SERA_MASK49           0x1ffffffffffffull
#define NWOAS_SERA_FRAC_BITS        16u
#define NWOAS_SERA_FRAC_MASK        0xffffu

/* --- API --------------------------------------------------------------- */

typedef enum {
  NWOAS_SERA_OK = 0,
  NWOAS_SERA_E_ARG,             /* NULL ops or callback */
  NWOAS_SERA_E_CNTFRQ,          /* physical counter frequency implausible */
  NWOAS_SERA_E_FIFO_NOT_IDLE,   /* RX not empty or TX count != 0 before CMD write; nothing written */
  NWOAS_SERA_E_TIMEOUT,         /* 100 ms physical-counter deadline expired waiting for a reply word */
  NWOAS_SERA_E_POLL_CAP,        /* finite poll cap hit (frozen counter or stuck controller) */
  NWOAS_SERA_E_BAD_HEADER,      /* reply header low 16 bits != 0x0f3d */
  NWOAS_SERA_E_EXTRA_RESPONSE,  /* RX still not empty after 3 words; left in FIFO, not drained */
  NWOAS_SERA_E_COUNTER_BACKWARD,/* counter1 < counter0 */
  NWOAS_SERA_E_COUNTER_SPAN,    /* counter1 - counter0 > NWOAS_SERA_MAX_COUNTER_SPAN */
  NWOAS_SERA_E_NEGATIVE_TIME,   /* 49-bit signed sum is negative */
  NWOAS_SERA_E_RANGE            /* seconds outside 2000..2099 */
} nwoas_sera_status_t;

typedef struct {
  /* 32-bit MMIO accessors. addr is a physical address inside the SPMI window. */
  uint32_t (*read32)(void *ctx, uint64_t addr);
  void     (*write32)(void *ctx, uint64_t addr, uint32_t value);
  /* CNTPCT_EL0 and CNTFRQ_EL0 (physical counter; m1n1 offsets only CNTVCT). */
  uint64_t (*physical_counter)(void *ctx);
  uint64_t (*counter_frequency)(void *ctx);
  void *ctx;
} nwoas_sera_rtc_ops_t;

typedef struct {
  /* Decoded clock. Valid only when nwoas_sera_rtc_read returned OK. */
  int64_t  utc_seconds;     /* signed Unix seconds, within 2000..2099 */
  uint32_t subseconds;      /* 0..65535, units of 1/65536 s */
  uint32_t nanoseconds;     /* subseconds converted, 0..999984741 */
  uint64_t cntpct;          /* CNTPCT_EL0 sampled right after counter1 arrived */
  uint64_t cntfrq;          /* CNTFRQ_EL0 used for the deadline */

  /* Raw material, filled as far as the read progressed (for logging). */
  uint64_t counter0;        /* raw 48-bit 0xd002, first read */
  uint64_t offset;          /* raw 48-bit 0xd100 */
  uint64_t counter1;        /* raw 48-bit 0xd002, second read */
  uint32_t headers[NWOAS_SERA_TRANSACTIONS];
  uint32_t transactions_done;
  uint32_t cmd_writes;      /* number of CMD writes performed (<= 3) */
} nwoas_sera_rtc_result_t;

/* Perform the bounded three-transaction snapshot. On any error the result
 * still carries the raw fields collected so far and the error is final; no
 * retry, no drain, no further writes. */
nwoas_sera_status_t nwoas_sera_rtc_read (const nwoas_sera_rtc_ops_t *ops,
                                         nwoas_sera_rtc_result_t *out);

/* Pure arithmetic: counter/offset are raw 48-bit values. Produces signed
 * seconds and 16-bit fraction using the 49-bit wrap described above, then
 * applies the negative and 2000..2099 checks. */
nwoas_sera_status_t nwoas_sera_rtc_convert (uint64_t counter48, uint64_t offset48,
                                            int64_t *seconds_out,
                                            uint32_t *subseconds_out);

/* fraction (0..65535) -> nanoseconds, floor((frac * 1e9) / 65536). */
uint32_t nwoas_sera_rtc_frac_to_ns (uint32_t subseconds);

/* Decode 6 little-endian data bytes carried in two response words. */
uint64_t nwoas_sera_rtc_words_to_u48 (uint32_t w1, uint32_t w2);

const char *nwoas_sera_status_str (nwoas_sera_status_t st);

#ifdef __cplusplus
}
#endif

#endif /* NWOAS_SERA_RTC_H */
