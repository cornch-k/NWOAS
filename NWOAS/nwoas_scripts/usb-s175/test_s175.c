/* Host fake-MMIO tests for the S175 helper. The helper's real code runs;
 * only read32/write32/dsb/delay_us/table_in_range are faked. */
#include "s175_dart1_translate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DART1 S175_DART1_BASE
#define DART0 0x502f00000ULL
#define XHCI  S175_XHCI_BASE
#define CAPLEN 0x20u

#define MAX_LOG 512
typedef struct { bool is_write; uint64_t addr; uint32_t val; } access;

typedef struct {
    uint32_t dart1[0x400 / 4];      /* DART1 register window we model */
    uint32_t xhci[0x100 / 4];       /* CAPLENGTH + op regs */
    access log[MAX_LOG];
    unsigned nlog;
    unsigned delayed_us;
    /* fault injection */
    int busy_reads_remaining;       /* BUSY reads after each invalidate */
    bool busy_forever;
    uint32_t ignore_write_mask_ttbr; /* bit n: writes to SID1 TTBRn ignored */
    unsigned tcr_writes_allowed;    /* 0 = unlimited */
    unsigned tcr_writes_seen;
    bool run_xhc_after_first_flush; /* set RS=1 once first invalidate issued */
    bool out_of_range_tables;
} fake;

static fake F;

static void logit(bool w, uint64_t a, uint32_t v)
{
    if (F.nlog < MAX_LOG) { F.log[F.nlog].is_write = w; F.log[F.nlog].addr = a; F.log[F.nlog].val = v; }
    F.nlog++;
}

static uint32_t f_read32(void *ctx, uint64_t addr)
{
    (void)ctx;
    uint32_t v = 0xdeadbeef;
    if (addr >= DART1 && addr < DART1 + 0x400) {
        uint32_t off = (uint32_t)(addr - DART1);
        if (off == S175_DART_STREAM_COMMAND) {
            if (F.busy_forever || F.busy_reads_remaining > 0) {
                if (F.busy_reads_remaining > 0) F.busy_reads_remaining--;
                v = F.dart1[off / 4] | S175_DART_STREAM_COMMAND_BUSY;
            } else
                v = F.dart1[off / 4] & ~S175_DART_STREAM_COMMAND_BUSY;
        } else
            v = F.dart1[off / 4];
    } else if (addr >= XHCI && addr < XHCI + 0x100) {
        v = F.xhci[(addr - XHCI) / 4];
    } else {
        fprintf(stderr, "FAKE: read outside modelled ranges: 0x%llx\n", (unsigned long long)addr);
        abort();
    }
    logit(false, addr, v);
    return v;
}

static void f_write32(void *ctx, uint64_t addr, uint32_t val)
{
    (void)ctx;
    logit(true, addr, val);
    if (addr >= DART1 && addr < DART1 + 0x400) {
        uint32_t off = (uint32_t)(addr - DART1);
        for (unsigned n = 0; n < 4; n++)
            if (off == S175_DART_TTBR(S175_SID, n) && (F.ignore_write_mask_ttbr & (1u << n)))
                return;
        if (off == S175_DART_TCR(S175_SID)) {
            F.tcr_writes_seen++;
            if (F.tcr_writes_allowed && F.tcr_writes_seen > F.tcr_writes_allowed)
                return;
        }
        if (off == S175_DART_STREAM_COMMAND && (val & S175_DART_STREAM_COMMAND_INVAL)) {
            if (F.run_xhc_after_first_flush) {
                F.xhci[CAPLEN / 4] |= 1u;   /* USBCMD.RS = 1 */
                F.xhci[CAPLEN / 4 + 1] &= ~1u; /* HCH = 0 */
            }
        }
        F.dart1[off / 4] = val;
        return;
    }
    fprintf(stderr, "FAKE: write outside DART1 window: 0x%llx=0x%x\n",
            (unsigned long long)addr, val);
    abort();
}

static void f_dsb(void *ctx) { (void)ctx; }
static void f_delay(void *ctx, uint32_t us) { (void)ctx; F.delayed_us += us; }
static bool f_range(void *ctx, uint64_t pa, uint64_t len)
{
    (void)ctx;
    if (F.out_of_range_tables) return false;
    return pa >= 0x800000000ULL && pa + len <= 0xbe1000000ULL;
}

static const s175_ops OPS = { NULL, f_read32, f_write32, f_dsb, f_delay, f_range };

static void reset(void)
{
    memset(&F, 0, sizeof F);
    F.xhci[0] = CAPLEN;                 /* CAPLENGTH */
    F.xhci[CAPLEN / 4] = 0x0;           /* USBCMD RS=0 */
    F.xhci[CAPLEN / 4 + 1] = 0x1;       /* USBSTS HCH=1 */
    F.dart1[S175_DART_CONFIG / 4] = 0x0;
    F.dart1[S175_DART_ENABLED_STREAMS / 4] = 0x0;
    for (unsigned s = 0; s < 2; s++)
        F.dart1[S175_DART_TCR(s) / 4] = 0x1100;   /* observed baseline: bypass DART+DAPF */
    F.dart1[S175_DART_ERROR / 4] = 0x0e0e0400;    /* stale value observed in logs */
    F.dart1[S175_DART_ERROR_ADDR_LO / 4] = 0x978c7698;
}

static const uint32_t GOOD_TTBR[4] = { 0x80ae0180u, 0, 0, 0 };   /* D78 value */

static unsigned fails = 0;
#define CHECK(c) do { if (!(c)) { fails++; printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* Allowed write offsets inside DART1 for a translate attempt or restore. */
static bool write_allowed(uint64_t addr)
{
    if (addr < DART1 || addr >= DART1 + 0x400) return false;
    uint32_t off = (uint32_t)(addr - DART1);
    if (off == S175_DART_STREAM_SELECT || off == S175_DART_STREAM_COMMAND) return true;
    if (off == S175_DART_ENABLED_STREAMS || off == S175_DART_TCR(S175_SID)) return true;
    for (unsigned n = 0; n < 4; n++) if (off == S175_DART_TTBR(S175_SID, n)) return true;
    return false;
}

static void check_write_scope(void)
{
    unsigned n = F.nlog < MAX_LOG ? F.nlog : MAX_LOG;
    for (unsigned i = 0; i < n; i++) {
        if (!F.log[i].is_write) continue;
        CHECK(write_allowed(F.log[i].addr));
        /* never STREAM_SELECT another stream */
        if (F.log[i].addr == DART1 + S175_DART_STREAM_SELECT)
            CHECK(F.log[i].val == (1u << S175_SID));
        /* ENABLED_STREAMS: only bit 1 may differ from baseline 0 */
        if (F.log[i].addr == DART1 + S175_DART_ENABLED_STREAMS)
            CHECK((F.log[i].val & ~(1u << S175_SID)) == 0);
    }
}

static unsigned count_writes(void)
{
    unsigned c = 0, n = F.nlog < MAX_LOG ? F.nlog : MAX_LOG;
    for (unsigned i = 0; i < n; i++) if (F.log[i].is_write) c++;
    return c;
}

static void t_success(void)
{
    printf("test: valid success\n");
    reset();
    s175_result r;
    s175_status st = s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r);
    CHECK(st == S175_OK && r.status == S175_OK);
    CHECK(r.restore == S175_RESTORE_NOT_ATTEMPTED);
    CHECK(r.wrote_anything);
    CHECK(r.before.tcr == 0x1100);
    CHECK(r.tcr_programmed == 0x1080);
    CHECK(r.after.tcr == 0x1080);
    CHECK(r.after.ttbr[0] == 0x80ae0180u && r.after.ttbr[1] == 0 && r.after.ttbr[2] == 0 && r.after.ttbr[3] == 0);
    CHECK((r.after.enabled_streams & 2u) != 0);
    CHECK(r.flush_wait_us == 0);
    CHECK(r.mismatch_reg == -1);
    CHECK(F.dart1[S175_DART_TCR(0) / 4] == 0x1100);   /* SID0 untouched */
    CHECK(F.dart1[S175_DART_TTBR(0, 0) / 4] == 0);
    check_write_scope();
}

static void t_busy_then_clear(void)
{
    printf("test: busy clears within bound\n");
    reset();
    F.busy_reads_remaining = 5;
    s175_result r;
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_OK);
    CHECK(r.flush_wait_us == 5);
    CHECK(F.delayed_us == 5);
    check_write_scope();
}

static void t_busy_forever(void)
{
    printf("test: flush busy forever -> not restored claim\n");
    reset();
    F.busy_forever = true;
    s175_result r;
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_FLUSH_BUSY);
    CHECK(r.flush_wait_us == S175_FLUSH_TIMEOUT_US);
    CHECK(r.restore == S175_RESTORE_FLUSH_BUSY);
    CHECK(r.restore_flush_wait_us == S175_FLUSH_TIMEOUT_US);
    /* saved regs were written back even though restore is not claimed */
    CHECK(F.dart1[S175_DART_TCR(1) / 4] == 0x1100);
    CHECK(F.dart1[S175_DART_TTBR(1, 0) / 4] == 0);
    CHECK((F.dart1[S175_DART_ENABLED_STREAMS / 4] & 2u) == 0);
    CHECK(r.after.tcr == 0x1100);
    check_write_scope();
}

static void t_locked(void)
{
    printf("test: CONFIG locked -> no writes\n");
    reset();
    F.dart1[S175_DART_CONFIG / 4] = S175_DART_CONFIG_LOCK;
    s175_result r;
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_LOCKED);
    CHECK(!r.wrote_anything && count_writes() == 0);
    CHECK(r.before.config == S175_DART_CONFIG_LOCK);
}

static void t_source_invalid(void)
{
    printf("test: source TTBR invalid variants -> no writes\n");
    s175_result r;
    reset();
    uint32_t t1[4] = { 0x00ae0180u, 0, 0, 0 };          /* valid bit clear */
    CHECK(s175_dart1_sid1_translate(&OPS, t1, &r) == S175_ERR_SOURCE_INVALID);
    CHECK(count_writes() == 0);
    reset();
    uint32_t t2[4] = { 0, 0, 0, 0 };                    /* TTBR0 zero */
    CHECK(s175_dart1_sid1_translate(&OPS, t2, &r) == S175_ERR_SOURCE_INVALID);
    CHECK(count_writes() == 0);
    reset();
    uint32_t t3[4] = { 0x80ae0180u, 0x00000001u, 0, 0 }; /* nonzero, invalid */
    CHECK(s175_dart1_sid1_translate(&OPS, t3, &r) == S175_ERR_SOURCE_INVALID);
    CHECK(count_writes() == 0);
    reset();
    F.out_of_range_tables = true;                       /* caller predicate rejects */
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_SOURCE_INVALID);
    CHECK(count_writes() == 0);
    reset();
    uint32_t t4[4] = { 0x80ae0180u, 0x80ae0190u, 0, 0 }; /* two valid in range */
    CHECK(s175_dart1_sid1_translate(&OPS, t4, &r) == S175_OK);
    CHECK(r.after.ttbr[1] == 0x80ae0190u);
    check_write_scope();
}

static void t_not_halted(void)
{
    printf("test: xHC not halted / caplen invalid -> no writes\n");
    s175_result r;
    reset();
    F.xhci[CAPLEN / 4] = 1;                              /* RS=1 */
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_XHC_NOT_HALTED);
    CHECK(count_writes() == 0);
    reset();
    F.xhci[CAPLEN / 4 + 1] = 0;                          /* HCH=0 */
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_XHC_NOT_HALTED);
    CHECK(count_writes() == 0);
    reset();
    F.xhci[0] = 0xff;
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_XHC_CAPLEN);
    CHECK(count_writes() == 0);
}

static void t_readback_mismatch_rollback_ok(void)
{
    printf("test: readback mismatch -> rollback OK\n");
    reset();
    F.ignore_write_mask_ttbr = 1u << 0;                  /* TTBR0 write lost */
    s175_result r;
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_READBACK);
    CHECK(r.mismatch_reg == 0 && r.mismatch_expected == 0x80ae0180u && r.mismatch_observed == 0);
    CHECK(r.restore == S175_RESTORE_OK);
    CHECK(r.after.tcr == 0x1100 && (r.after.enabled_streams & 2u) == 0);
    check_write_scope();
}

static void t_rollback_failure(void)
{
    printf("test: rollback failure (TCR stuck) -> RESTORE_FAILED_READBACK\n");
    reset();
    F.ignore_write_mask_ttbr = 1u << 2;                  /* force setup mismatch */
    F.tcr_writes_allowed = 1;                            /* restore TCR write lost */
    uint32_t t[4] = { 0x80ae0180u, 0, 0x80ae01a0u, 0 };
    s175_result r;
    CHECK(s175_dart1_sid1_translate(&OPS, t, &r) == S175_ERR_READBACK);
    CHECK(r.mismatch_reg == 4 || r.mismatch_reg == 2);
    CHECK(r.restore == S175_RESTORE_FAILED_READBACK);
    CHECK(r.after.tcr == 0x1080);                        /* honest after-state */
    check_write_scope();

    printf("test: xHC starts before rollback -> RESTORE_SKIPPED_XHC_RUNNING\n");
    reset();
    F.ignore_write_mask_ttbr = 1u << 0;
    F.run_xhc_after_first_flush = true;
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_ERR_READBACK);
    CHECK(r.restore == S175_RESTORE_SKIPPED_XHC_RUNNING);
    CHECK(r.after.usbcmd == 1 && r.after.tcr == 0x1080); /* nothing written back */
    /* setup only: 4 TTBR + ENABLED + TCR + SELECT + COMMAND; no restore writes */
    CHECK(count_writes() == 8);
    check_write_scope();
}

static void t_dapf_preserved(void)
{
    printf("test: DAPF bit12 and unrelated bits preserved\n");
    static const uint32_t cases[][2] = {
        { 0x1100, 0x1080 }, { 0x0100, 0x0080 }, { 0x1104, 0x1084 }, { 0x0000, 0x0080 }, { 0x1180, 0x1080 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        reset();
        F.dart1[S175_DART_TCR(1) / 4] = cases[i][0];
        s175_result r;
        CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_OK);
        CHECK(r.tcr_programmed == cases[i][1]);
        CHECK(r.after.tcr == cases[i][1]);
        CHECK(s175_translate_tcr(cases[i][0]) == cases[i][1]);
    }
    /* D78 comparison: writing 0x80 would have dropped DAPF bypass */
    CHECK(s175_translate_tcr(0x1100) != 0x80);
}

static void t_other_sid_no_access(void)
{
    printf("test: no access to SID0, DART0, debug DARTs, xHCI writes\n");
    reset();
    F.dart1[S175_DART_TTBR(0, 0) / 4] = 0x80123456u;     /* pre-existing SID0 state */
    F.dart1[S175_DART_ENABLED_STREAMS / 4] = 0x1;        /* SID0 enabled */
    s175_result r;
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, &r) == S175_OK);
    CHECK(F.dart1[S175_DART_TTBR(0, 0) / 4] == 0x80123456u);
    CHECK(F.dart1[S175_DART_TCR(0) / 4] == 0x1100);
    CHECK(F.dart1[S175_DART_ENABLED_STREAMS / 4] == 0x3);
    unsigned n = F.nlog < MAX_LOG ? F.nlog : MAX_LOG;
    for (unsigned i = 0; i < n; i++) {
        uint64_t a = F.log[i].addr;
        CHECK(!(a >= DART0 && a < DART0 + 0x80000));      /* never DART0 */
        CHECK(!(a >= 0x382f00000ULL && a < 0x383000000ULL)); /* never debug USB0 DARTs */
        if (F.log[i].is_write) CHECK(a >= DART1 && a < DART1 + 0x400);
        if (a >= XHCI && a < XHCI + 0x100) CHECK(!F.log[i].is_write);
        if (F.log[i].is_write) {
            uint32_t off = (uint32_t)(a - DART1);
            CHECK(off != S175_DART_TCR(0));
            for (unsigned k = 0; k < 4; k++) CHECK(off != S175_DART_TTBR(0, k));
        }
    }
    CHECK(F.nlog < MAX_LOG);
}

static void t_bad_args(void)
{
    printf("test: bad args\n");
    s175_result r;
    reset();
    CHECK(s175_dart1_sid1_translate(NULL, GOOD_TTBR, &r) == S175_ERR_BAD_ARGS);
    CHECK(s175_dart1_sid1_translate(&OPS, NULL, &r) == S175_ERR_BAD_ARGS);
    CHECK(s175_dart1_sid1_translate(&OPS, GOOD_TTBR, NULL) == S175_ERR_BAD_ARGS);
    s175_ops bad = OPS; bad.dsb = NULL;
    CHECK(s175_dart1_sid1_translate(&bad, GOOD_TTBR, &r) == S175_ERR_BAD_ARGS);
    CHECK(count_writes() == 0);
}

int main(void)
{
    t_success();
    t_busy_then_clear();
    t_busy_forever();
    t_locked();
    t_source_invalid();
    t_not_halted();
    t_readback_mismatch_rollback_ok();
    t_rollback_failure();
    t_dapf_preserved();
    t_other_sid_no_access();
    t_bad_args();
    printf("%s: %u failure(s)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
