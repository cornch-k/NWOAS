/*
 * Host unit tests for Source/Main/nwau_core.c (run on the build Mac):
 *     tests/run-tests.sh
 * Covers header validation, resource checks, volume mapping, format
 * conversion, and a simulated ADMAC consumer driving the ring logic.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../Source/Main/nwau_core.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void good_words(nwu32 w[NWAU_HDR_WORDS])
{
    memset(w, 0, sizeof(nwu32) * NWAU_HDR_WORDS);
    w[0] = 0x5541574e;      /* 'NWAU' */
    w[1] = 1;
    w[2] = 0x00801000;      /* ring_iova lo (example) */
    w[3] = 0;
    w[4] = 0x20000;
    w[5] = 48000;
    w[6] = 1;
    w[7] = 0;
    w[8] = 0;
    w[9] = 0x31;
    w[10] = 0x30;
    w[11] = 1;
}

static void test_header(void)
{
    nwu32 w[NWAU_HDR_WORDS];
    nwau_header h;
    const char magic[4] = { 'N', 'W', 'A', 'U' };
    nwu32 m;

    memcpy(&m, magic, 4);
    CHECK(m == NWAU_HDR_MAGIC);             /* little-endian 'NWAU' */

    good_words(w);
    nwau_header_from_words(w, &h);
    CHECK(h.ring_iova == 0x00801000ull);
    CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == NWAU_OK);

    w[3] = 0x2; nwau_header_from_words(w, &h);
    CHECK(h.ring_iova == 0x200801000ull);
    CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == NWAU_OK);

#define BAD(idx, val, code) do { good_words(w); w[idx] = (val); \
    nwau_header_from_words(w, &h); CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == (code)); } while (0)
    BAD(0, 0x12345678, NWAU_EHDR_MAGIC);
    BAD(1, 3, NWAU_EHDR_VERSION);
    BAD(1, 0, NWAU_EHDR_VERSION);
    BAD(11, 0, NWAU_EHDR_NOT_READY);
    BAD(11, 2, NWAU_EHDR_NOT_READY);
    BAD(4, 0, NWAU_EHDR_RING);
    BAD(4, 0x20800, NWAU_EHDR_RING);        /* not a chunk multiple */
    BAD(4, 0x8000, NWAU_EHDR_RING);         /* too small */
    BAD(4, 0x40000, NWAU_EHDR_RING);        /* does not fit after the header page */
    BAD(4, 0x3e000, NWAU_OK);               /* fits (chunk multiple below 0x3f000) */
    BAD(2, 0, NWAU_EHDR_IOVA);
    BAD(2, 0x00801004, NWAU_EHDR_IOVA);     /* unaligned */
    BAD(5, 44100, NWAU_EHDR_RATE);
    BAD(6, 2, NWAU_EHDR_FORMAT);
    BAD(7, 1, NWAU_EHDR_CHANNEL);           /* odd = RX */
    BAD(7, 24, NWAU_EHDR_CHANNEL);
    BAD(7, 4, NWAU_OK);
    BAD(8, 6, NWAU_EHDR_CLUSTER);
    BAD(8, 5, NWAU_OK);
    BAD(9, 0x00, NWAU_EHDR_AMP_ADDR);
    BAD(9, 0x78, NWAU_EHDR_AMP_ADDR);
    BAD(10, 0x00, NWAU_EHDR_VOL_CAP);       /* 0 dB cap refused */
    BAD(10, 0x0b, NWAU_EHDR_VOL_CAP);       /* louder than the -6 dB hard floor */
    BAD(10, 0x0c, NWAU_OK);
    BAD(10, 0x14, NWAU_OK);
    BAD(10, 0x28, NWAU_OK);
    BAD(10, 0xca, NWAU_EHDR_VOL_CAP);
#undef BAD
    /* a smaller window must be honoured */
    good_words(w); nwau_header_from_words(w, &h);
    CHECK(nwau_header_validate(&h, 0x20000) == NWAU_EHDR_RING);
}

static void test_resources(void)
{
    CHECK(nwau_check_resource(0, 0x235014000ull, 0x4000) == NWAU_OK);
    CHECK(nwau_check_resource(1, 0x238200000ull, 0x34000) == NWAU_OK);
    CHECK(nwau_check_resource(2, 0x238400000ull, 0x18000) == NWAU_OK);
    CHECK(nwau_check_resource(3, 0x238300000ull, 0x30000) == NWAU_OK);
    CHECK(nwau_check_resource(4, 0x301000000ull, 0x40000) == NWAU_OK);
    /* swapped order is caught */
    CHECK(nwau_check_resource(0, 0x238200000ull, 0x34000) == NWAU_ERES_BASE + 0);
    CHECK(nwau_check_resource(1, 0x235014000ull, 0x4000) == NWAU_ERES_BASE + 1);
    CHECK(nwau_check_resource(2, 0x238400000ull, 0x4000) == NWAU_ERES_BASE + 2);
    CHECK(nwau_check_resource(4, 0x301000000ull, 0x20000) == NWAU_ERES_BASE + 4);
    CHECK(nwau_check_resource(4, 0x301001000ull, 0x40000) == NWAU_ERES_BASE + 4);
    CHECK(nwau_check_resource(4, 0, 0x40000) == NWAU_ERES_BASE + 4);
    CHECK(nwau_check_resource(4, 0x238400000ull, 0x40000) == NWAU_ERES_BASE + 4); /* over MCA */
    CHECK(nwau_check_resource(5, 0, 0) == NWAU_ERES_COUNT);
}

static void test_volume(void)
{
    const nwu32 cap = 0x30;
    CHECK(nwau_volume_reg(0, 0, cap) == 0x30);               /* 0 dB Windows -> cap */
    CHECK(nwau_volume_reg(1 << 16, 0, cap) == 0x30);          /* positive clamps to cap */
    CHECK(nwau_volume_reg(-0x8000, 0, cap) == 0x31);          /* -0.5 dB */
    CHECK(nwau_volume_reg(-16 * 65536, 0, cap) == 0x50);      /* -16 dB -> -40 dB amp */
    CHECK(nwau_volume_reg(-96 * 65536, 0, cap) == 0xc9);      /* floor at -100.5 dB */
    CHECK(nwau_volume_reg(-0x7fffffff, 0, cap) == 0xc9);
    CHECK(nwau_volume_reg((nws32)0x80000000u, 0, cap) == 0xc9); /* INT_MIN */
    CHECK(nwau_volume_reg(0, 1, cap) == 0xc9);                /* mute */
    CHECK(nwau_volume_reg(-0x3fff, 0, cap) == 0x30);          /* rounding */
    CHECK(nwau_volume_reg(-0x4000, 0, cap) == 0x31);
    /* never below the hard floor even with a bad cap */
    CHECK(nwau_volume_reg(0, 0, 0x00) == NWAU_VOL_HARD_FLOOR);
    CHECK(nwau_volume_reg(0, 0, 0xff) == 0xc9);
    {
        nws32 lv;
        int ok = 1;
        for (lv = 0; lv >= -100 * 65536; lv -= 0x1000) {
            nwu32 reg = nwau_volume_reg(lv, 0, cap);
            if (reg < cap || reg > 0xc9) ok = 0;
        }
        CHECK(ok);
    }
}

static void put16(nwu8 *b, nws16 v) { b[0] = (nwu8)((nwu16)v & 0xff); b[1] = (nwu8)((nwu16)v >> 8); }

static void test_conversion(void)
{
    nwu8 src[64];
    nwu32 dst[16];
    nwu32 used, n;
    nwau_conv c;
    int i;

    CHECK(nwau_mono_word(0, 0) == 0);
    CHECK(nwau_mono_word(32767, 32767) == 0x7fff0000u);
    CHECK(nwau_mono_word(-32768, -32768) == 0x80000000u);
    CHECK(nwau_mono_word(32767, -32768) == 0x00000000u);  /* (32767-32768)/2 = 0 (trunc) */
    CHECK(nwau_mono_word(100, 300) == ((nwu32)200 << 16));
    CHECK(nwau_mono_word(-1, -1) == 0xffff0000u);
    /* 24-bit value = sample << 8, msb aligned => low byte always zero */
    CHECK((nwau_mono_word(1234, -999) & 0xffu) == 0);
    CHECK((nws32)nwau_mono_word(1000, 1000) >> 8 == 1000 << 8);

    for (i = 0; i < 8; i++) {           /* 8 frames L=i*10, R=i*10+2 */
        put16(src + i * 4, (nws16)(i * 10));
        put16(src + i * 4 + 2, (nws16)(i * 10 + 2));
    }
    nwau_conv_reset(&c);
    n = nwau_conv_run(&c, src, 32, dst, 16, &used);
    CHECK(n == 8 && used == 32 && c.ncarry == 0);
    for (i = 0; i < 8; i++) CHECK(dst[i] == ((nwu32)(i * 10 + 1) << 16));

    /* split at odd byte boundaries must give identical output */
    {
        nwu32 dst2[16];
        nwu32 total = 0, off = 0;
        nwu32 cuts[] = { 3, 1, 6, 5, 2, 15 };
        nwau_conv_reset(&c);
        for (i = 0; i < 6; i++) {
            total += nwau_conv_run(&c, src + off, cuts[i], dst2 + total, 16 - total, &used);
            CHECK(used == cuts[i]);
            off += cuts[i];
        }
        CHECK(off == 32 && total == 8 && c.ncarry == 0);
        CHECK(memcmp(dst, dst2, sizeof(nwu32) * 8) == 0);
    }

    /* destination full: stop exactly, no partial consumption beyond frames written */
    nwau_conv_reset(&c);
    n = nwau_conv_run(&c, src, 32, dst, 3, &used);
    CHECK(n == 3 && used == 12 && c.ncarry == 0);
    n = nwau_conv_run(&c, src, 2, dst, 0, &used);        /* no room: partial not taken */
    CHECK(n == 0 && used == 0 && c.ncarry == 0);
    n = nwau_conv_run(&c, src, 3, dst, 1, &used);        /* room: partial carried */
    CHECK(n == 0 && used == 3 && c.ncarry == 3);
    n = nwau_conv_run(&c, src + 3, 1, dst, 0, &used);    /* completing needs room */
    CHECK(n == 0 && used == 0 && c.ncarry == 3);
    n = nwau_conv_run(&c, src + 3, 1, dst, 1, &used);
    CHECK(n == 1 && used == 1 && c.ncarry == 0 && dst[0] == ((nwu32)1 << 16));

    /* skip keeps alignment */
    nwau_conv_reset(&c);
    nwau_conv_skip(&c, 6);           /* 1.5 frames */
    CHECK(c.ncarry == 2);
    n = nwau_conv_run(&c, src + 6, 26, dst, 16, &used);
    /* first word = zeroed left half + frame 1 right (12): (0 + 12) / 2 = 6 */
    CHECK(n == 7 && used == 26 && c.ncarry == 0);
    CHECK(dst[0] == ((nwu32)6 << 16));
    CHECK(dst[1] == ((nwu32)(2 * 10 + 1) << 16));
}

/* Simulated ADMAC: completes the oldest in-flight descriptor every `period`
 * ticks; the producer writes `prod` bytes every tick. */
typedef struct sim {
    nwau_ring r;
    nwu8 *ring;
    nwu64 played;
    nwu32 pads, pad_bytes, dropped, stray;
    nwu32 descs;
    int bad;
} sim;

static void sim_service(sim *s)
{
    for (;;) {
        nwu32 coff, poff, plen;
        int force = nwau_ring_inflight(&s->r) < s->r.min_inflight;
        int k = nwau_ring_next(&s->r, force, &coff, &poff, &plen);
        if (k == NWAU_NEXT_NONE) break;
        if (coff % s->r.chunk_bytes) s->bad |= 1;
        if (k == NWAU_NEXT_PADDED) {
            if (poff < coff || poff + plen != coff + s->r.chunk_bytes) s->bad |= 2;
            memset(s->ring + poff, 0, plen);
            s->pads++; s->pad_bytes += plen;
        }
        s->descs++;
        if (nwau_ring_inflight(&s->r) > s->r.max_inflight) s->bad |= 4;
    }
}

/* start of playback: fill the whole descriptor ring with silence (driver does the same) */
static void sim_prime(sim *s)
{
    nwu32 coff, poff, plen;
    while (nwau_ring_next(&s->r, 1, &coff, &poff, &plen) != NWAU_NEXT_NONE) {
        memset(s->ring + poff, 0, plen);
    }
}

static void sim_produce(sim *s, nwu32 bytes, nwu8 fill)
{
    while (bytes) {
        nwu32 off, span = nwau_ring_write_span(&s->r, &off);
        if (span == 0) { s->dropped += bytes; return; }
        if (span > bytes) span = bytes;
        /* must never touch an in-flight chunk */
        {
            nwu64 pos = s->r.wr;
            if (pos < s->r.sub) s->bad |= 8;
            if (pos + span - s->r.done > s->r.ring_bytes) s->bad |= 16;
        }
        memset(s->ring + off, fill, span);
        nwau_ring_commit(&s->r, span);
        bytes -= span;
    }
}

static void test_ring(void)
{
    sim s;
    nwu32 t, coff, poff, plen;
    int k;

    memset(&s, 0, sizeof(s));
    CHECK(nwau_ring_init(&s.r, 0x20000, NWAU_CHUNK_BYTES, 4, 2, NWAU_MAX_LEAD_BYTES) == NWAU_OK);
    CHECK(nwau_ring_init(&s.r, 0x20000, 0x1001, 4, 2, NWAU_MAX_LEAD_BYTES) == NWAU_ERING_PARAM);
    CHECK(nwau_ring_init(&s.r, 0x8000, NWAU_CHUNK_BYTES, 4, 2, NWAU_MAX_LEAD_BYTES) == NWAU_ERING_PARAM);
    CHECK(nwau_ring_init(&s.r, 0x20000, NWAU_CHUNK_BYTES, 4, 5, NWAU_MAX_LEAD_BYTES) == NWAU_ERING_PARAM);
    CHECK(nwau_ring_init(&s.r, 0x20000, NWAU_CHUNK_BYTES, 4, 2, NWAU_MAX_LEAD_BYTES) == NWAU_OK);
    s.ring = (nwu8 *)malloc(0x20000);
    memset(s.ring, 0xee, 0x20000);

    /* prime: 4 silence chunks */
    for (t = 0; t < 4; t++) {
        k = nwau_ring_next(&s.r, 1, &coff, &poff, &plen);
        CHECK(k == NWAU_NEXT_PADDED && coff == t * NWAU_CHUNK_BYTES && plen == NWAU_CHUNK_BYTES);
        memset(s.ring + poff, 0, plen);
    }
    CHECK(nwau_ring_inflight(&s.r) == 4);
    CHECK(nwau_ring_next(&s.r, 1, &coff, &poff, &plen) == NWAU_NEXT_NONE);  /* ring depth */
    CHECK(nwau_ring_complete(&s.r, 9) == 4);   /* stray reports are not accepted */
    CHECK(nwau_ring_inflight(&s.r) == 0);
    CHECK(nwau_ring_complete(&s.r, 1) == 0);

    /* steady state: producer 192 bytes/ms (48 kHz mono 32-bit), DMA completes a
     * 4 KiB chunk every 21.333 ms; run 60 s of 1 ms ticks with a little drift. */
    nwau_ring_reset(&s.r);
    s.pads = s.pad_bytes = s.dropped = 0;
    sim_prime(&s);
    CHECK(nwau_ring_inflight(&s.r) == 4);
    {
        nwu64 dma_acc = 0;                       /* in 1/1000 bytes */
        nwu32 pads_after_start;
        for (t = 0; t < 60000; t++) {
            sim_produce(&s, 192, (nwu8)(t & 0x7f));
            dma_acc += 192000 + 5;               /* DMA ~26 ppm faster than producer */
            while (dma_acc >= (nwu64)NWAU_CHUNK_BYTES * 1000 && nwau_ring_inflight(&s.r) > 0) {
                dma_acc -= (nwu64)NWAU_CHUNK_BYTES * 1000;
                if (nwau_ring_complete(&s.r, 1) != 1) s.stray++;
                s.played += NWAU_CHUNK_BYTES;
            }
            sim_service(&s);
            CHECK(nwau_ring_inflight(&s.r) >= 1);   /* never starved */
            if (t == 100) pads_after_start = s.pads;
        }
        (void)pads_after_start;
        CHECK(s.bad == 0);
        CHECK(s.dropped == 0);
        CHECK(s.stray == 0);
        /* only the 4 priming chunks plus rare drift corrections are padded */
        CHECK(s.pads <= 4 + 3);
        printf("ring sim 60 s: descs=%u pads=%u pad_bytes=%u dropped=%u lead=%llu inflight=%u\n",
               s.descs, s.pads, s.pad_bytes, s.dropped,
               (unsigned long long)(s.r.wr - s.r.sub), nwau_ring_inflight(&s.r));
    }

    /* producer stops: DMA keeps running on padded silence, nothing breaks */
    {
        nwu32 before = s.pads;
        for (t = 0; t < 1000; t++) {
            if (t % 21 == 0 && nwau_ring_inflight(&s.r) > 0) nwau_ring_complete(&s.r, 1);
            sim_service(&s);
        }
        CHECK(s.pads > before);
        CHECK(s.bad == 0);
    }

    /* DMA stalls: producer is limited by max_lead and drops, never overwrites */
    nwau_ring_reset(&s.r);
    s.dropped = 0;
    sim_prime(&s);
    for (t = 0; t < 500; t++) {
        sim_produce(&s, 192, 0x11);
        sim_service(&s);
    }
    CHECK(s.r.wr - s.r.sub <= NWAU_MAX_LEAD_BYTES);
    CHECK(s.dropped == 500 * 192 - NWAU_MAX_LEAD_BYTES);
    CHECK(s.bad == 0);

    /* wrap-around: the write span never crosses the ring end */
    nwau_ring_reset(&s.r);
    {
        int ok = 1;
        for (t = 0; t < 5000; t++) {
            nwu32 off, span = nwau_ring_write_span(&s.r, &off);
            if (off + span > s.r.ring_bytes) ok = 0;
            nwau_ring_commit(&s.r, span > 1000 ? 1000 : span);
            sim_service(&s);
            if (nwau_ring_inflight(&s.r) == 4) nwau_ring_complete(&s.r, 2);
        }
        CHECK(ok);
        CHECK(s.r.wr > 4 * (nwu64)s.r.ring_bytes);
    }
    free(s.ring);
}

#include "test_thermal.inc"

int main(void)
{
    test_header();
    test_resources();
    test_volume();
    test_conversion();
    test_ring();
    test_header_v2();
    test_fixed_point();
    test_gate();
    test_ts_constants();
    test_ts_step();
    test_ts_idle();
    test_ts_failsafe();
    test_ts_limits();
    test_ts_fullscale_loop();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
