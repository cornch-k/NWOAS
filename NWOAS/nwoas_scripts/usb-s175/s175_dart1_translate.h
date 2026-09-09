/* S175: bounded helper that puts ONLY the non-debug USB-C DART instance 1
 * (0x502f80000) stream 1 into translate mode using caller-supplied TTBRs.
 *
 * HARDWARE-UNVERIFIED. Host-tested against a fake MMIO model only.
 *
 * Scope and guarantees (enforced by code, checked by tests):
 *  - Touches exactly one DART block, S175_DART1_BASE, and only: stream 1
 *    TTBR0..3, stream 1 TCR, ENABLED_STREAMS (bit 1 only), STREAM_SELECT,
 *    STREAM_COMMAND. Reads CONFIG and ERROR regs. Never writes DART 0
 *    (0x502f00000), the debug USB0 DARTs, or any xHCI register.
 *  - Reads the non-debug xHCI (S175_XHCI_BASE) only to confirm the host
 *    controller is halted (USBSTS.HCH=1) and not running (USBCMD.RS=0).
 *  - TCR: preserves every baseline bit except: clear BYPASS_DART (bit 8),
 *    set TRANSLATE (bit 7). DAPF bypass (bit 12) is preserved as found.
 *    (D78 wrote 0x80 outright, which also cleared bit 12.)
 *  - No heap. No arbitrary runtime addresses: the two bases are constants,
 *    the only caller-provided addresses are the 4 source TTBR register
 *    values, and each referenced table must pass the caller's range
 *    predicate.
 *  - On failure after the first write, a bounded restore of the saved
 *    stream-1 registers is attempted, only while the xHC is still halted.
 *    Restore is reported OK only if readback matches AND the flush
 *    completed; a busy flush is never reported as restored.
 */
#ifndef S175_DART1_TRANSLATE_H
#define S175_DART1_TRANSLATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define S175_XHCI_BASE   0x502280000ULL /* j274 usb@502280000 = dwc3_1 (non-debug) */
#define S175_DART1_BASE  0x502f80000ULL /* dwc3_1 DART instance 1 */
#define S175_SID         1u             /* dwc3_1 uses dart_1 stream 1 (t8103 dts) */

/* T8020 DART register layout (matches m1n1 dart.c). */
#define S175_DART_STREAM_COMMAND        0x20u
#define S175_DART_STREAM_COMMAND_BUSY   (1u << 2)
#define S175_DART_STREAM_COMMAND_INVAL  (1u << 20)
#define S175_DART_STREAM_SELECT         0x34u
#define S175_DART_ERROR                 0x40u
#define S175_DART_ERROR_ADDR_LO         0x50u
#define S175_DART_ERROR_ADDR_HI         0x54u
#define S175_DART_CONFIG                0x60u
#define S175_DART_CONFIG_LOCK           (1u << 15)
#define S175_DART_ENABLED_STREAMS       0xfcu
#define S175_DART_TCR(sid)              (0x100u + 4u * (sid))
#define S175_DART_TCR_TRANSLATE         (1u << 7)
#define S175_DART_TCR_BYPASS_DART       (1u << 8)
#define S175_DART_TCR_BYPASS_DAPF       (1u << 12)
#define S175_DART_TTBR(sid, n)          (0x200u + 16u * (sid) + 4u * (n))
#define S175_DART_TTBR_VALID            (1u << 31)
#define S175_DART_TTBR_ADDR_MASK        0x7fffffffu
#define S175_DART_TTBR_SHIFT            12
#define S175_DART_L1_TABLE_BYTES        0x4000u

#define S175_FLUSH_TIMEOUT_US           100u

typedef struct s175_ops {
    void *ctx;
    uint32_t (*read32)(void *ctx, uint64_t addr);
    void (*write32)(void *ctx, uint64_t addr, uint32_t val);
    void (*dsb)(void *ctx);
    void (*delay_us)(void *ctx, uint32_t us);
    /* true if [pa, pa+len) is DRAM the DART may walk (caller policy). */
    bool (*table_in_range)(void *ctx, uint64_t pa, uint64_t len);
} s175_ops;

typedef enum s175_status {
    S175_OK = 0,
    S175_ERR_BAD_ARGS,             /* NULL ops/callbacks/result */
    S175_ERR_XHC_CAPLEN,           /* CAPLENGTH 0 or 0xff: controller not up */
    S175_ERR_XHC_NOT_HALTED,       /* USBCMD.RS=1 or USBSTS.HCH=0 */
    S175_ERR_SOURCE_INVALID,       /* TTBR0 invalid, or a TTBR fails range check */
    S175_ERR_LOCKED,               /* CONFIG lock bit 15 set */
    S175_ERR_FLUSH_BUSY,           /* invalidate never finished: state unknown */
    S175_ERR_READBACK              /* a programmed register read back differently */
} s175_status;

typedef enum s175_restore {
    S175_RESTORE_NOT_ATTEMPTED = 0, /* no writes happened, or success */
    S175_RESTORE_OK,                /* saved regs read back AND flush completed */
    S175_RESTORE_SKIPPED_XHC_RUNNING,
    S175_RESTORE_FAILED_READBACK,   /* saved regs written but read back wrong */
    S175_RESTORE_FLUSH_BUSY         /* regs written, flush busy: NOT restored */
} s175_restore;

typedef struct s175_snapshot {
    uint32_t config;
    uint32_t enabled_streams;
    uint32_t tcr;
    uint32_t ttbr[4];
    uint32_t error;
    uint32_t error_lo;
    uint32_t error_hi;
    uint32_t usbcmd;
    uint32_t usbsts;
} s175_snapshot;

typedef struct s175_result {
    s175_status  status;
    s175_restore restore;
    bool         wrote_anything;   /* any write32 issued to the DART */
    s175_snapshot before;
    s175_snapshot after;           /* valid whenever wrote_anything */
    uint32_t     tcr_programmed;   /* value written to TCR (0 if none) */
    uint32_t     flush_wait_us;    /* setup flush wait actually spent */
    uint32_t     restore_flush_wait_us;
    int          mismatch_reg;     /* -1 none; 0..3 TTBRn; 4 TCR; 5 ENABLED */
    uint32_t     mismatch_expected;
    uint32_t     mismatch_observed;
} s175_result;

/* Compute the TCR the helper would program from a baseline value. */
static inline uint32_t s175_translate_tcr(uint32_t baseline)
{
    return (baseline & ~S175_DART_TCR_BYPASS_DART) | S175_DART_TCR_TRANSLATE;
}

/* Program DART1 stream 1 into translate mode with src_ttbr[0..3].
 * src_ttbr[0] must have the VALID bit; src_ttbr[1..3] must each be either 0
 * or VALID with an in-range table. Returns out->status (also stored). */
s175_status s175_dart1_sid1_translate(const s175_ops *ops,
                                      const uint32_t src_ttbr[4],
                                      s175_result *out);

const char *s175_status_str(s175_status s);
const char *s175_restore_str(s175_restore r);

#endif
