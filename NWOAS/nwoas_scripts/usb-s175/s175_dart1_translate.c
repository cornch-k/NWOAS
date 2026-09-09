/* S175 helper implementation. See header for scope. No libc, no heap. */
#include "s175_dart1_translate.h"

#define D(off) (S175_DART1_BASE + (uint64_t)(off))
#define ENABLED_BIT (1u << S175_SID)

static uint32_t rd(const s175_ops *o, uint64_t a) { return o->read32(o->ctx, a); }
static void wr(const s175_ops *o, uint64_t a, uint32_t v) { o->write32(o->ctx, a, v); }

static bool ops_valid(const s175_ops *o)
{
    return o && o->read32 && o->write32 && o->dsb && o->delay_us && o->table_in_range;
}

static void snap(const s175_ops *o, uint64_t op, s175_snapshot *s)
{
    s->config = rd(o, D(S175_DART_CONFIG));
    s->enabled_streams = rd(o, D(S175_DART_ENABLED_STREAMS));
    s->tcr = rd(o, D(S175_DART_TCR(S175_SID)));
    for (unsigned n = 0; n < 4; n++)
        s->ttbr[n] = rd(o, D(S175_DART_TTBR(S175_SID, n)));
    s->error = rd(o, D(S175_DART_ERROR));
    s->error_lo = rd(o, D(S175_DART_ERROR_ADDR_LO));
    s->error_hi = rd(o, D(S175_DART_ERROR_ADDR_HI));
    s->usbcmd = rd(o, op + 0x00);
    s->usbsts = rd(o, op + 0x04);
}

/* op base of the fixed non-debug xHCI, or 0 if CAPLENGTH is implausible. */
static uint64_t xhci_op_base(const s175_ops *o)
{
    uint32_t caplen = rd(o, S175_XHCI_BASE) & 0xffu;
    if (caplen == 0 || caplen == 0xff)
        return 0;
    return S175_XHCI_BASE + caplen;
}

static bool xhc_halted(const s175_ops *o, uint64_t op)
{
    uint32_t usbcmd = rd(o, op + 0x00);
    uint32_t usbsts = rd(o, op + 0x04);
    return (usbcmd & 1u) == 0 && (usbsts & 1u) != 0;
}

/* Select stream 1, issue invalidate, wait bounded for BUSY to clear. */
static bool flush_sid(const s175_ops *o, uint32_t *wait_us)
{
    uint32_t spent = 0;
    wr(o, D(S175_DART_STREAM_SELECT), ENABLED_BIT);
    wr(o, D(S175_DART_STREAM_COMMAND), S175_DART_STREAM_COMMAND_INVAL);
    o->dsb(o->ctx);
    for (;;) {
        if ((rd(o, D(S175_DART_STREAM_COMMAND)) & S175_DART_STREAM_COMMAND_BUSY) == 0) {
            *wait_us = spent;
            return true;
        }
        if (spent >= S175_FLUSH_TIMEOUT_US) {
            *wait_us = spent;
            return false;
        }
        o->delay_us(o->ctx, 1);
        spent += 1;
    }
}

/* Read back stream-1 TTBR0..3, TCR and ENABLED bit; report first mismatch. */
static bool verify(const s175_ops *o, const uint32_t ttbr[4], uint32_t tcr,
                   bool want_enabled, s175_result *r)
{
    for (unsigned n = 0; n < 4; n++) {
        uint32_t v = rd(o, D(S175_DART_TTBR(S175_SID, n)));
        if (v != ttbr[n]) {
            r->mismatch_reg = (int)n; r->mismatch_expected = ttbr[n]; r->mismatch_observed = v;
            return false;
        }
    }
    uint32_t t = rd(o, D(S175_DART_TCR(S175_SID)));
    if (t != tcr) {
        r->mismatch_reg = 4; r->mismatch_expected = tcr; r->mismatch_observed = t;
        return false;
    }
    uint32_t en = rd(o, D(S175_DART_ENABLED_STREAMS)) & ENABLED_BIT;
    uint32_t want = want_enabled ? ENABLED_BIT : 0u;
    if (en != want) {
        r->mismatch_reg = 5; r->mismatch_expected = want; r->mismatch_observed = en;
        return false;
    }
    return true;
}

/* Write saved stream-1 registers back. Only stream-1 fields are touched;
 * ENABLED_STREAMS bit 1 is returned to its saved state via read-modify-write. */
static s175_restore restore_sid1(const s175_ops *o, uint64_t op, s175_result *r)
{
    const s175_snapshot *b = &r->before;
    if (!xhc_halted(o, op))
        return S175_RESTORE_SKIPPED_XHC_RUNNING;
    for (unsigned n = 0; n < 4; n++)
        wr(o, D(S175_DART_TTBR(S175_SID, n)), b->ttbr[n]);
    wr(o, D(S175_DART_TCR(S175_SID)), b->tcr);
    {
        uint32_t en = rd(o, D(S175_DART_ENABLED_STREAMS));
        uint32_t want = (en & ~ENABLED_BIT) | (b->enabled_streams & ENABLED_BIT);
        if (want != en)
            wr(o, D(S175_DART_ENABLED_STREAMS), want);
    }
    o->dsb(o->ctx);
    if (!flush_sid(o, &r->restore_flush_wait_us))
        return S175_RESTORE_FLUSH_BUSY;
    if (!verify(o, b->ttbr, b->tcr, (b->enabled_streams & ENABLED_BIT) != 0, r))
        return S175_RESTORE_FAILED_READBACK;
    return S175_RESTORE_OK;
}

static s175_status finish(const s175_ops *o, uint64_t op, s175_result *r, s175_status st)
{
    r->status = st;
    if (r->wrote_anything)
        snap(o, op, &r->after);
    return st;
}

s175_status s175_dart1_sid1_translate(const s175_ops *o, const uint32_t src_ttbr[4],
                                      s175_result *r)
{
    if (!r)
        return S175_ERR_BAD_ARGS;
    {
        /* zero the result without memset (freestanding-friendly) */
        s175_result z = {0};
        *r = z;
        r->mismatch_reg = -1;
        r->restore = S175_RESTORE_NOT_ATTEMPTED;
    }
    if (!ops_valid(o) || !src_ttbr)
        return r->status = S175_ERR_BAD_ARGS;

    uint64_t op = xhci_op_base(o);
    if (!op)
        return r->status = S175_ERR_XHC_CAPLEN;
    if (!xhc_halted(o, op))
        return r->status = S175_ERR_XHC_NOT_HALTED;

    /* Source TTBR validation: TTBR0 must be valid; others 0 or valid.
     * Every valid entry must reference a 16 KB L1 table the caller accepts. */
    for (unsigned n = 0; n < 4; n++) {
        uint32_t t = src_ttbr[n];
        if (t == 0) {
            if (n == 0)
                return r->status = S175_ERR_SOURCE_INVALID;
            continue;
        }
        if ((t & S175_DART_TTBR_VALID) == 0)
            return r->status = S175_ERR_SOURCE_INVALID;
        uint64_t pa = (uint64_t)(t & S175_DART_TTBR_ADDR_MASK) << S175_DART_TTBR_SHIFT;
        if (!o->table_in_range(o->ctx, pa, S175_DART_L1_TABLE_BYTES))
            return r->status = S175_ERR_SOURCE_INVALID;
    }

    snap(o, op, &r->before);
    if (r->before.config & S175_DART_CONFIG_LOCK)
        return r->status = S175_ERR_LOCKED;

    uint32_t tcr_new = s175_translate_tcr(r->before.tcr);
    r->tcr_programmed = tcr_new;

    /* ---- writes begin: stream 1 of DART1 only ---- */
    r->wrote_anything = true;
    for (unsigned n = 0; n < 4; n++)
        wr(o, D(S175_DART_TTBR(S175_SID, n)), src_ttbr[n]);
    if ((r->before.enabled_streams & ENABLED_BIT) == 0)
        wr(o, D(S175_DART_ENABLED_STREAMS), r->before.enabled_streams | ENABLED_BIT);
    o->dsb(o->ctx);
    wr(o, D(S175_DART_TCR(S175_SID)), tcr_new);
    o->dsb(o->ctx);

    if (!flush_sid(o, &r->flush_wait_us)) {
        r->restore = restore_sid1(o, op, r);
        return finish(o, op, r, S175_ERR_FLUSH_BUSY);
    }
    if (!verify(o, src_ttbr, tcr_new, true, r)) {
        r->restore = restore_sid1(o, op, r);
        return finish(o, op, r, S175_ERR_READBACK);
    }
    return finish(o, op, r, S175_OK);
}

const char *s175_status_str(s175_status s)
{
    switch (s) {
    case S175_OK: return "OK";
    case S175_ERR_BAD_ARGS: return "ERR_BAD_ARGS";
    case S175_ERR_XHC_CAPLEN: return "ERR_XHC_CAPLEN";
    case S175_ERR_XHC_NOT_HALTED: return "ERR_XHC_NOT_HALTED";
    case S175_ERR_SOURCE_INVALID: return "ERR_SOURCE_INVALID";
    case S175_ERR_LOCKED: return "ERR_LOCKED";
    case S175_ERR_FLUSH_BUSY: return "ERR_FLUSH_BUSY";
    case S175_ERR_READBACK: return "ERR_READBACK";
    }
    return "?";
}

const char *s175_restore_str(s175_restore r)
{
    switch (r) {
    case S175_RESTORE_NOT_ATTEMPTED: return "RESTORE_NOT_ATTEMPTED";
    case S175_RESTORE_OK: return "RESTORE_OK";
    case S175_RESTORE_SKIPPED_XHC_RUNNING: return "RESTORE_SKIPPED_XHC_RUNNING";
    case S175_RESTORE_FAILED_READBACK: return "RESTORE_FAILED_READBACK";
    case S175_RESTORE_FLUSH_BUSY: return "RESTORE_FLUSH_BUSY";
    }
    return "?";
}
