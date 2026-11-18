/*
 * Host unit tests for the S256 header v2, the amp write gate and the speaker
 * thermal protection in nwau_core.c.  Included by test_nwau_core.c.
 *
 * The references here are written in double precision directly from the
 * published speakersafetyd J274 parameters (per-sample first order coil and
 * magnet stages, attenuation ramp over the last t_window degrees); they are an
 * independent re-implementation used only to check the integer model.
 */

/* ---------------------------------------------------------------- header v2 */
static void good_words_v2(nwu32 w[NWAU_HDR_WORDS])
{
    good_words(w);
    w[1] = 2;
    w[2] = 0x80001000u;     /* ring IOVA = window IOVA 0x80000000 + 0x1000 */
    w[11] = NWAU_HDR_STATUS_READY | NWAU_HDR_STATUS_SENSE;
    w[10] = 0x0c;
    w[12] = 0x80021000u;    /* sense ring at window + 0x21000 */
    w[13] = 0;
    w[14] = 0x10000;
    w[15] = 3;              /* cluster 0 RXB */
    w[16] = NWAU_SENSE_FORMAT_IV16;
    w[17] = NWAU_PROT_GAIN;
    w[18] = 0x00;
}

static void test_header_v2(void)
{
    nwu32 w[NWAU_HDR_WORDS];
    nwau_header h;

    good_words_v2(w);
    nwau_header_from_words(w, &h);
    CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == NWAU_OK);
    CHECK(nwau_header_has_sense(&h));
    CHECK(nwau_header_sense_offset(&h) == 0x21000u);
    CHECK(h.prot_gain == 0x14 && h.prot_vol_floor == 0 && h.sense_chan == 3);

    /* v1 never has sense, whatever the status bits say */
    good_words(w); w[11] = 3; nwau_header_from_words(w, &h);
    CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == NWAU_OK);
    CHECK(!nwau_header_has_sense(&h));

#define BAD2(idx, val, code) do { good_words_v2(w); w[idx] = (val); \
    nwau_header_from_words(w, &h); CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == (code)); } while (0)
    BAD2(11, NWAU_HDR_STATUS_READY, NWAU_OK);           /* sense not prepared: static only */
    good_words_v2(w); w[11] = 1; nwau_header_from_words(w, &h);
    CHECK(!nwau_header_has_sense(&h));
    BAD2(11, NWAU_HDR_STATUS_SENSE, NWAU_EHDR_NOT_READY);
    BAD2(17, 0, NWAU_OK);                               /* sense measured only */
    BAD2(17, 0x10, NWAU_EHDR_PROT);
    BAD2(17, 0x15, NWAU_EHDR_PROT);
    BAD2(17, 0xff, NWAU_EHDR_PROT);
    BAD2(18, 0xc9, NWAU_OK);
    BAD2(18, 0xca, NWAU_EHDR_PROT);
    BAD2(12, 0x80020000u, NWAU_EHDR_SENSE_RING);        /* overlaps the TX ring (ends 0x21000) */
    BAD2(12, 0x80021800u, NWAU_EHDR_SENSE_RING);        /* not page aligned */
    BAD2(12, 0x80030000u, NWAU_OK);                     /* ends at 0x40000 */
    BAD2(12, 0x7ffff000u, NWAU_EHDR_SENSE_RING);        /* before the window */
    BAD2(14, 0x4000, NWAU_EHDR_SENSE_RING);             /* fewer than 8 chunks */
    BAD2(14, 0x10800, NWAU_EHDR_SENSE_RING);            /* not a chunk multiple */
    BAD2(14, 0x1e000, NWAU_OK);                         /* 0x21000 + 0x1e000 = 0x3f000 */
    BAD2(14, 0x20000, NWAU_EHDR_SENSE_RING);
    BAD2(15, 2, NWAU_EHDR_SENSE_CHAN);                  /* even = TX */
    BAD2(15, 1, NWAU_OK);                               /* RXA of cluster 0 */
    BAD2(15, 5, NWAU_EHDR_SENSE_CHAN);                  /* other cluster */
    BAD2(15, 25, NWAU_EHDR_SENSE_CHAN);
    BAD2(16, 1, NWAU_EHDR_SENSE_FMT);
    /* with the sense bit clear the sense fields are not used and not checked */
    good_words_v2(w); w[11] = 1; w[15] = 2; w[16] = 9; nwau_header_from_words(w, &h);
    CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == NWAU_OK);
    /* 0x31000 + 0x10000 exceeds the window */
    good_words_v2(w); w[12] = 0x80031000u; nwau_header_from_words(w, &h);
    CHECK(nwau_header_validate(&h, NWAU_WINDOW_BYTES) == NWAU_EHDR_SENSE_RING);
}

/* ---------------------------------------------------------- fixed point */
static void test_fixed_point(void)
{
    double maxerr = 0;
    nwu64 x;
    int db;

    for (x = 0; x < (20ull << 32); x += 0x1357913ull) {
        double ref = exp(-(double)x / 4294967296.0) * 1073741824.0;
        double e = fabs((double)nwau_exp_neg_q30(x) - ref);
        if (e > maxerr) maxerr = e;
    }
    CHECK(maxerr <= 3.0);
    CHECK(nwau_exp_neg_q30(0) == (1u << 30));
    CHECK(nwau_exp_neg_q30(200ull << 32) == 0);
    printf("exp_neg_q30 max error %.2f ulp (Q30)\n", maxerr);

    maxerr = 0;
    for (db = -200; db <= 60; db++) {
        double ref = pow(10.0, (double)db / 20.0) * 1073741824.0;  /* db is in 0.5 dB units */
        double got = (double)nwau_pow10_db_x2_q30(db);
        double rel = fabs(got - ref) / (ref * 1e-6 + 2.0);   /* 1 ppm or 2 ulp */
        if (rel > maxerr) maxerr = rel;
    }
    CHECK(maxerr <= 1.0);
    CHECK(nwau_pow10_db_x2_q30(0) == (1u << 30));
    CHECK(nwau_pow10_db_x2_q30(20) > (10ull << 30) - 1000 && nwau_pow10_db_x2_q30(20) < (10ull << 30) + 1000);

    CHECK(nwau_isqrt64(0) == 0 && nwau_isqrt64(1) == 1 && nwau_isqrt64(3) == 1 && nwau_isqrt64(4) == 2);
    CHECK(nwau_isqrt64(0xffffffffffffffffull) == 0xffffffffu);
    {
        int ok = 1;
        nwu64 v;
        for (v = 1; v < (1ull << 50); v = v * 3 + 7) {
            nwu64 r = nwau_isqrt64(v);
            if (r * r > v || (r + 1) * (r + 1) <= v) ok = 0;
        }
        CHECK(ok);
    }
    CHECK(nwau_volume_reg_base(0, 0, 0) == 0);
    CHECK(nwau_volume_reg_base(-6 * 65536, 0, 0) == 12);
    CHECK(nwau_volume_reg_base(0, 1, 0) == 0xc9);
    CHECK(nwau_volume_reg_base(0, 0, 0xd0) == 0xc9);
    CHECK(nwau_volume_reg(0, 0, 0x0c) == 0x0c);
    CHECK(nwau_full_scale_db_x2(0x14, 0) == 42);        /* +21 dB */
    CHECK(nwau_full_scale_db_x2(0x00, 0x0c) == 10);     /* +5 dB: the static cap */

    /* conversion energy */
    {
        nwu8 src[16];
        nwu32 dst[4], used;
        nwu64 e = 0;
        nwau_conv c;
        put16(src + 0, 1000); put16(src + 2, 3000);     /* mono 2000 */
        put16(src + 4, -100); put16(src + 6, -300);     /* -200 */
        put16(src + 8, 32767); put16(src + 10, 32767);
        put16(src + 12, -32768); put16(src + 14, -32768);
        nwau_conv_reset(&c);
        CHECK(nwau_conv_run_e(&c, src, 16, dst, 4, &used, &e) == 4);
        CHECK(e == 2000ull * 2000 + 200ull * 200 + 32767ull * 32767 + 32768ull * 32768);
        e = 0;
        nwau_conv_reset(&c);
        CHECK(nwau_conv_run_e(&c, src, 3, dst, 4, &used, &e) == 0 && e == 0);
        CHECK(nwau_conv_run_e(&c, src + 3, 5, dst, 4, &used, &e) == 2);
        CHECK(e == 2000ull * 2000 + 200ull * 200);
    }
}

/* ---------------------------------------------------------------- gate */
static nwau_gate gate_base(void)
{
    nwau_gate g;
    memset(&g, 0, sizeof(g));
    g.serdes_on = 1; g.dma_on = 1; g.inflight = 3;
    g.static_floor = 0x0c;
    g.gain_shadow = 0x00;
    g.vol_shadow = 0x50;
    g.prot_ok = 0;
    g.prot_gain = NWAU_PROT_GAIN;
    g.prot_floor = 0;
    return g;
}

static int w1(const nwau_gate *g, nwu32 reg, nwu32 v)
{
    nwu8 b = (nwu8)v;
    return nwau_amp_write_allowed(g, reg, &b, 1);
}

static void test_gate(void)
{
    nwau_gate g = gate_base();
    nwu8 three[3] = { 0x21, 0x42, 0x40 };
    nwu32 v, gs;
    int ok;

    /* page, software reset, book: never */
    CHECK(!w1(&g, 0x00, 0)); CHECK(!w1(&g, 0x01, 1)); CHECK(!w1(&g, 0x7f, 0));
    CHECK(!nwau_amp_write_allowed(&g, 0x7d, three, 3));
    /* multi byte over 0x02..0x05 or 0x01 */
    CHECK(!nwau_amp_write_allowed(&g, 0x01, three, 2));
    CHECK(!nwau_amp_write_allowed(&g, 0x04, three, 2));
    CHECK(nwau_amp_write_allowed(&g, 0x0e, three, 3));        /* sense TX slots */
    CHECK(nwau_amp_write_allowed(&g, 0x0a, three, 3));
    CHECK(!nwau_amp_write_allowed(&g, 0x0a, three, 0));
    CHECK(!nwau_amp_write_allowed(&g, 0x0a, 0, 1));

    /* static mode volume floor */
    CHECK(!w1(&g, 0x05, 0x0b)); CHECK(w1(&g, 0x05, 0x0c)); CHECK(w1(&g, 0x05, 0xc9));
    g.static_floor = 0x30; CHECK(!w1(&g, 0x05, 0x2f)); CHECK(w1(&g, 0x05, 0x30));
    g.static_floor = 0x00; CHECK(!w1(&g, 0x05, 0x0b));       /* hard floor still applies */
    g = gate_base();

    /* power control */
    CHECK(w1(&g, 0x02, 0x0c)); CHECK(w1(&g, 0x02, 0x00));
    CHECK(w1(&g, 0x02, 0x0d)); CHECK(w1(&g, 0x02, 0x0e)); CHECK(w1(&g, 0x02, 0x02));
    CHECK(!w1(&g, 0x02, 0x03)); CHECK(!w1(&g, 0x02, 0x04)); CHECK(!w1(&g, 0x02, 0x08));
    g.serdes_on = 0; CHECK(!w1(&g, 0x02, 0x0c)); CHECK(!w1(&g, 0x02, 0x00)); CHECK(w1(&g, 0x02, 0x0e));
    g = gate_base(); g.dma_on = 0; CHECK(!w1(&g, 0x02, 0x00));
    g = gate_base(); g.inflight = 0; CHECK(!w1(&g, 0x02, 0x0c));
    g = gate_base();

    /* gain: 0x00 always; 0x14 when the header configures the protection and the
       verified volume satisfies the 0x14 floor (not healthy: 0x20 = same +5 dB) */
    CHECK(w1(&g, 0x03, 0x00));
    g.vol_shadow = 0x1f; CHECK(!w1(&g, 0x03, 0x14));          /* would be louder than static */
    g.vol_shadow = 0x20; CHECK(w1(&g, 0x03, 0x14));           /* 0x0c + 0x14: same full scale */
    g.vol_shadow = 0x00; CHECK(!w1(&g, 0x03, 0x14));          /* unknown volume */
    g.prot_ok = 1;
    g.vol_shadow = 0x1f; g.prot_floor = 0x10; CHECK(w1(&g, 0x03, 0x14));   /* healthy: model floor */
    g.prot_floor = 0x30; CHECK(!w1(&g, 0x03, 0x14));          /* below the model floor */
    g.vol_shadow = 0x30; CHECK(w1(&g, 0x03, 0x14));
    g.prot_gain = 0; CHECK(!w1(&g, 0x03, 0x14));              /* header: protection off */
    g = gate_base(); CHECK(w1(&g, 0x02, 0x01));               /* mute with sense on */
    g = gate_base(); g.prot_ok = 1; g.vol_shadow = 0x50;
    g.gain_shadow = NWAU_AMP_GAIN_UNKNOWN; CHECK(!w1(&g, 0x03, 0x14)); CHECK(w1(&g, 0x03, 0x00));
    for (v = 0x01; v <= 0xff; v++) {
        if (v != 0x14) CHECK(!w1(&g, 0x03, v));
    }
    /* volume floor with gain 0x14 */
    g = gate_base(); g.gain_shadow = 0x14;
    CHECK(nwau_gate_vol_floor(&g) == 0x20);                   /* not healthy */
    g.prot_ok = 1; g.prot_floor = 0x00; CHECK(nwau_gate_vol_floor(&g) == 0x00);
    CHECK(w1(&g, 0x05, 0x00));
    g.prot_floor = 0x24; CHECK(!w1(&g, 0x05, 0x23)); CHECK(w1(&g, 0x05, 0x24));
    g.prot_gain = 0; CHECK(nwau_gate_vol_floor(&g) == 0x20);  /* header withdrew the protection */
    g = gate_base(); g.gain_shadow = NWAU_AMP_GAIN_UNKNOWN; g.prot_ok = 1;
    CHECK(nwau_gate_vol_floor(&g) == 0x20);
    g.static_floor = 0xc0; CHECK(nwau_gate_vol_floor(&g) == 0xc9);

    /* fade-out below the floor but quieter than the verified volume: allowed */
    g = gate_base(); g.gain_shadow = 0x14; g.prot_ok = 0; g.vol_shadow = 0x04;
    CHECK(nwau_gate_vol_floor(&g) == 0x20);
    CHECK(w1(&g, 0x05, 0x04) && w1(&g, 0x05, 0x10) && !w1(&g, 0x05, 0x03));
    g.vol_shadow = 0; CHECK(!w1(&g, 0x05, 0x10));      /* unknown shadow: floor only */
    /* matched static mapping */
    CHECK(nwau_volume_reg_matched(0, 0, 0x0c) == 0x0c);
    CHECK(nwau_volume_reg_matched(-16 * 65536, 0, 0x0c) == 0x0c);          /* 32 - 20 = 12 */
    CHECK(nwau_volume_reg_matched(-20 * 65536, 0, 0x0c) == 20);            /* 40 - 20 */
    CHECK(nwau_volume_reg_matched(-20 * 65536, 1, 0x0c) == 0xc9);
    CHECK(nwau_volume_reg_matched(-200 * 65536, 0, 0x0c) == 0xc9 - 20);
    CHECK(nwau_volume_reg_matched(0, 0, 0x00) == NWAU_VOL_HARD_FLOOR);
    {
        nws32 lv;
        int same = 1;
        for (lv = -16 * 65536; lv >= -90 * 65536; lv -= 0x4000) {
            nwu32 st = nwau_volume_reg_matched(lv, 0, 0x0c), pr = nwau_volume_reg_base(lv, 0, 0);
            if (pr <= 0xc9 - 1 && nwau_full_scale_db_x2(0, st) != nwau_full_scale_db_x2(0x14, pr)) same = 0;
            if (st < 0x0c) same = 0;
        }
        CHECK(same);                                    /* same loudness below the static cap */
    }

    /* exhaustive: no allowed (gain, volume) pair is louder than 21 dB, and without a
       healthy protection none is louder than the static +5 dB */
    ok = 1;
    for (gs = 0; gs <= 0xff; gs++) {
        int healthy;
        for (healthy = 0; healthy < 2; healthy++) {
            nwu32 pf;
            for (pf = 0; pf <= 0xc9; pf += 7) {
                g = gate_base(); g.gain_shadow = gs; g.prot_ok = healthy; g.prot_floor = pf;
                nwu32 sh;
                for (sh = 0; sh <= 0xc9; sh += 13) {
                    g.vol_shadow = sh;
                    for (v = 0; v <= 0xff; v++) {
                        nwu32 eff_gain = (gs == 0x00) ? 0 : 0x14;   /* unknown = worst case */
                        nwu32 fl = nwau_gate_vol_floor(&g);
                        if (!w1(&g, 0x05, v)) continue;
                        if (nwau_full_scale_db_x2(eff_gain, v) > 42) ok = 0;
                        /* never louder than both the floor and the current verified volume */
                        if (v < fl && (sh == 0 || v < sh)) ok = 0;
                        if (!healthy && v < fl && nwau_full_scale_db_x2(eff_gain, v) > 10 && v < sh) ok = 0;
                        if (healthy && gs == 0x14 && v < pf && v < sh) ok = 0;
                    }
                }
            }
        }
    }
    CHECK(ok);
    /* gain raise never makes the output louder than the 0x14 floor allows */
    ok = 1;
    for (gs = 0; gs <= 0xff; gs++) {
        int healthy;
        for (healthy = 0; healthy < 2; healthy++) {
            for (v = 0; v <= 0xc9; v++) {
                g = gate_base(); g.gain_shadow = gs; g.prot_ok = healthy; g.prot_floor = 0x18; g.vol_shadow = v;
                if (!w1(&g, 0x03, 0x14)) continue;
                if (!healthy && nwau_full_scale_db_x2(0x14, v) > 10) ok = 0;
                if (healthy && v < 0x18) ok = 0;
                if (gs != 0x00 && gs != 0x14) ok = 0;
            }
        }
    }
    CHECK(ok);
}

/* ------------------------------------------------------- sense synthesis */
#define SR 48000.0

/* fill one block of IV16 words for an in-phase sine: v(t) = V*sqrt2*sin, i = v / z */
static void synth_block(nwu32 *w, nwu32 frames, double vrms, double z_ohm, nwu64 *phase,
                        double vs_gain, double is_gain, int inv_i)
{
    nwu32 k;
    for (k = 0; k < frames; k++) {
        double s = sin(2.0 * M_PI * 1000.0 * (double)(*phase + k) / SR);
        double v = vrms * 1.41421356237 * s * vs_gain;
        double i = (vrms / z_ohm) * 1.41421356237 * s * is_gain * (inv_i ? -1.0 : 1.0);
        double vc = v / 14.0 * 32768.0, ic = i / 3.75 * 32768.0;
        long vi, ii;
        if (vc > 32767) vc = 32767;
        if (vc < -32768) vc = -32768;
        if (ic > 32767) ic = 32767;
        if (ic < -32768) ic = -32768;
        vi = lrint(vc); ii = lrint(ic);
        w[k] = ((nwu32)(nwu16)(nws16)vi << 16) | (nwu32)(nwu16)(nws16)ii;
    }
    *phase += frames;
}

/* TX energy of a full scale (s16 amplitude a) sine over n frames */
static nwu64 tx_energy_sine(double a, nwu32 n)
{
    return (nwu64)(a * a * 0.5 * n);
}

/* expected amp output for TX amplitude a (s16) at gain/vol (hypothesis used by the core) */
static double out_vrms(double a, nwu32 gain, nwu32 vol)
{
    double fs = pow(10.0, nwau_full_scale_db_x2(gain, vol) / 40.0);  /* Vrms of a full scale sine */
    return fs * a / 32768.0;
}

/* -------------------------------------------- double precision reference */
typedef struct ref_model { double coil, mag; } ref_model;

static void ref_step(ref_model *r, double p, double n_samples)
{
    /* exact first order steps over n samples with constant input (same physics as the
       speakersafetyd per-sample filter, which is its small-step approximation) */
    double dt = n_samples / SR;
    double ec = exp(-dt / 3.7), em = exp(-dt / 250.0);
    double tc = r->mag + p * 40.0, tm = 46.0 + p * 60.0;
    r->coil = tc + (r->coil - tc) * ec;
    r->mag = tm + (r->mag - tm) * em;
}

static void ref_sample_model(ref_model *r, double p)
{
    /* the per-sample filter form: alpha = step / (tau + step) */
    double step = 1.0 / SR;
    double ac = step / (3.7 + step), am = step / (250.0 + step);
    double tc = r->mag + p * 40.0, tm = 46.0 + p * 60.0;
    r->coil = tc * ac + r->coil * (1 - ac);
    r->mag = tm * am + r->mag * (1 - am);
}

static double ts_coil(const nwau_ts *t) { return (double)t->t_coil_uc / 1e6; }
static double ts_mag(const nwau_ts *t) { return (double)t->t_mag_uc / 1e6; }

static void test_ts_constants(void)
{
    double max_p = (140.0 - 46.0) / (40.0 + 60.0);
    double peak = pow(10.0, 21.0 / 10.0) / 4.6 * 2.0;
    double att = -10.0 * log10(max_p / peak);
    nwau_ts t;

    CHECK(fabs(max_p - 0.94) < 1e-9);
    CHECK(NWAU_TS_MIN_ATT_MDB >= (int)(att * 1000.0) && NWAU_TS_MIN_ATT_MDB <= (int)(att * 1000.0) + 2);
    printf("speakersafetyd j274 worst case attenuation at gain 21 dB: %.4f dB (const %d mdB)\n",
           att, NWAU_TS_MIN_ATT_MDB);
    nwau_ts_init(&t);
    CHECK(t.state == NWAU_TS_OFF);
    CHECK(t.t_coil_uc == 119000000ll);
    CHECK(fabs(ts_mag(&t) - (46.0 + (119.0 - 46.0) * 0.6)) < 1e-6);
    CHECK(nwau_ts_floor(&t) == 0);              /* 119 degC: below the 120 degC ramp start */
}

/* step response: constant power, compare with the double reference */
static void test_ts_step(void)
{
    nwau_ts t;
    ref_model r, rs;
    nwu32 w[NWAU_SENSE_CHUNK_FRAMES];
    nwu64 ph = 0;
    nwau_ts_drive d;
    double vrms = 1.5, z = 4.6, p = vrms * vrms / z;       /* 0.489 W */
    double maxdev = 0, maxdev_s = 0;
    nwu32 b, blocks = (nwu32)(1500.0 * SR / NWAU_SENSE_CHUNK_FRAMES);   /* 25 minutes */

    nwau_ts_init(&t);
    nwau_ts_restore(&t, 46000000, 46000000, 0);             /* then raised to the cold assumption */
    CHECK(ts_coil(&t) == 119.0);
    t.t_coil_uc = 46000000; t.t_mag_uc = 46000000;           /* test from ambient */
    t.hyst_coil_uc = t.t_coil_uc; t.hyst_mag_uc = t.t_mag_uc;
    nwau_ts_stream_start(&t, 0);
    CHECK(t.state == NWAU_TS_ARMING);
    r.coil = r.mag = rs.coil = rs.mag = 46.0;
    memset(&d, 0, sizeof(d));
    d.gain_reg = 0x14; d.vol_reg = 0;
    for (b = 0; b < blocks; b++) {
        double a;
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, vrms, z, &ph, 1, 1, 0);
        /* tx amplitude that gives vrms at gain 0x14 vol 0 */
        a = vrms / out_vrms(32768.0, 0x14, 0) * 32768.0;
        d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) != NWAU_TS_EV_TRIP);
        ref_step(&r, p, NWAU_SENSE_CHUNK_FRAMES);
        {
            nwu32 k;
            for (k = 0; k < NWAU_SENSE_CHUNK_FRAMES; k++) ref_sample_model(&rs, p);
        }
        if (fabs(ts_coil(&t) - r.coil) > maxdev) maxdev = fabs(ts_coil(&t) - r.coil);
        if (fabs(ts_coil(&t) - rs.coil) > maxdev_s) maxdev_s = fabs(ts_coil(&t) - rs.coil);
        if (b == (nwu32)(3.7 * SR / NWAU_SENSE_CHUNK_FRAMES)) {
            printf("step 0.49 W after 3.7 s: coil %.3f (ref %.3f) magnet %.3f\n", ts_coil(&t), r.coil, ts_mag(&t));
        }
    }
    /* measured power from the synthesized sense data */
    CHECK(abs(t.p_uw - (nws32)(p * 1e6)) < 3000);
    CHECK(abs((int)t.z_mohm - 4600) < 30);
    CHECK(abs((int)t.r_mohm - 4600) < 30);
    CHECK(abs((int)t.vrms_mv - 1500) < 3);
    CHECK(t.ratio_permille > 980 && t.ratio_permille < 1020);
    CHECK(t.state == NWAU_TS_HEALTHY);
    /* steady state: ambient + P (tr_coil + tr_magnet) after 6 magnet time constants */
    CHECK(fabs(ts_coil(&t) - (46.0 + p * 100.0)) < 0.2);
    CHECK(fabs(ts_mag(&t) - (46.0 + p * 60.0)) < 0.2);
    CHECK(maxdev < 0.05);       /* power from sense data vs exact power */
    CHECK(maxdev_s < 0.2);      /* against the per-sample filter form */
    printf("step response 25 min: coil %.3f degC (steady %.3f), max dev vs exact %.4f, vs per-sample %.4f\n",
           ts_coil(&t), 46.0 + p * 100.0, maxdev, maxdev_s);
    /* rtemp at 4.6 ohm = Tref */
    CHECK(abs(t.rtemp_mc - NWAU_TS_TREF_MC) < 2000);
}

static void test_ts_idle(void)
{
    nwau_ts t, t2;
    nwu32 w[NWAU_SENSE_CHUNK_FRAMES];
    nwau_ts_drive d;
    double times[] = { 0.5, 3.7, 10, 60, 250, 1000, 5000 };
    int i;

    memset(w, 0, sizeof(w));
    memset(&d, 0, sizeof(d));
    for (i = 0; i < 7; i++) {
        double c0 = 135.0, m0 = 100.0, A = 46.0, T = times[i];
        double eta = 250.0 / (250.0 - 3.7);
        double m = A + exp(-T / 250.0) * (m0 - A);
        double c = A + exp(-T / 3.7) * ((c0 - A) - eta * (m0 - A)) + eta * exp(-T / 250.0) * (m0 - A);
        nwu32 b, nb = (nwu32)(T * SR / NWAU_SENSE_CHUNK_FRAMES + 0.5);
        nwau_ts_init(&t);
        t.t_coil_uc = 135000000; t.t_mag_uc = 100000000;
        t.hyst_coil_uc = t.t_coil_uc; t.hyst_mag_uc = t.t_mag_uc;
        t2 = t;
        nwau_ts_idle(&t, (nwu64)(T * 1e6));
        CHECK(fabs(ts_coil(&t) - c) < 0.01);
        CHECK(fabs(ts_mag(&t) - m) < 0.01);
        /* the same with silent sense blocks (model running, zero power) */
        t2.state = NWAU_TS_ARMING;
        for (b = 0; b < nb; b++) nwau_ts_block(&t2, w, NWAU_SENSE_CHUNK_FRAMES, &d);
        {
            double tb = nb * NWAU_SENSE_CHUNK_FRAMES / SR;
            double cb = A + exp(-tb / 3.7) * ((c0 - A) - eta * (m0 - A)) + eta * exp(-tb / 250.0) * (m0 - A);
            /* block stepping is the decoupled form: small difference only */
            CHECK(fabs(ts_coil(&t2) - cb) < 0.5);
        }
    }
    /* hysteresis: the floor follows the falling coil only 5 degC late */
    nwau_ts_init(&t);
    t.t_coil_uc = t.hyst_coil_uc = 140000000; t.t_mag_uc = t.hyst_mag_uc = 100000000;
    CHECK(nwau_ts_floor(&t) == 36);
    nwau_ts_idle(&t, 2000000);
    CHECK(ts_coil(&t) < 135.0);
    CHECK(t.hyst_coil_uc == t.t_coil_uc + 5000000);
    /* restore */
    nwau_ts_init(&t);
    nwau_ts_restore(&t, 139000000, 120000000, 1000000);     /* 1 s old, hot: kept hot */
    CHECK(ts_coil(&t) > 125.0 && ts_coil(&t) < 139.0 && ts_mag(&t) > 119.0);
    nwau_ts_restore(&t, 139000000, 120000000, 3600000000ull); /* 1 h old: cold assumption */
    CHECK(t.t_coil_uc == 119000000ll);
    nwau_ts_restore(&t, 900000000, 120000000, 0);             /* absurd: worst case */
    CHECK(t.t_coil_uc == 140000000ll);
    nwau_ts_restore(&t, 10000000, 20000000, 0);               /* below ambient: worst case */
    CHECK(t.t_coil_uc == 140000000ll);
}

/* ------------------------------------------------------------ fail-safe */
static void arm(nwau_ts *t, nwu64 *ph, nwau_ts_drive *d, nwu32 *w)
{
    int b;
    double a = 32768.0 * 0.5;
    d->gain_reg = 0x00; d->vol_reg = 0x0c;
    for (b = 0; b < 16 && t->state != NWAU_TS_HEALTHY; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, d->gain_reg, d->vol_reg), 4.6, ph, 1, 1, 0);
        d->tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d->tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        nwau_ts_block(t, w, NWAU_SENSE_CHUNK_FRAMES, d);
    }
}

static void test_ts_failsafe(void)
{
    nwau_ts t;
    nwu32 w[NWAU_SENSE_CHUNK_FRAMES];
    nwu64 ph = 0;
    nwau_ts_drive d;
    double a = 32768.0 * 0.5;           /* -6 dBFS sine */
    int b, ev;

    memset(&d, 0, sizeof(d));

    /* OFF / TRIPPED ignore data */
    nwau_ts_init(&t);
    memset(w, 0, sizeof(w));
    CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) == NWAU_TS_EV_NONE && t.blocks == 0);

    /* arming needs loud verified blocks; silence never arms */
    nwau_ts_stream_start(&t, 0);
    for (b = 0; b < 200; b++) CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) == NWAU_TS_EV_NONE);
    CHECK(t.state == NWAU_TS_ARMING);
    arm(&t, &ph, &d, w);
    CHECK(t.state == NWAU_TS_HEALTHY);
    CHECK(t.verified == NWAU_TS_ARM_BLOCKS);

    /* stale (model hot enough that no bridge applies) */
    t.hyst_coil_uc = 115000000;
    CHECK(nwau_ts_stale(&t, 140000) == NWAU_TS_EV_NONE);
    CHECK(nwau_ts_stale(&t, 150001) == NWAU_TS_EV_TRIP);
    CHECK(t.state == NWAU_TS_TRIPPED && t.trip_reason == NWAU_TS_R_STALE);
    CHECK(nwau_ts_stale(&t, 900000) == NWAU_TS_EV_NONE);     /* already tripped */
    CHECK((t.trips & 0xffff) == 1);
    /* next stream: raised to at least the cold assumption, arming again */
    t.t_coil_uc = 60000000; t.t_mag_uc = 50000000;
    nwau_ts_stream_start(&t, 0);
    CHECK(t.state == NWAU_TS_ARMING && t.t_coil_uc >= 119000000ll);

    /* unwritten chunk (marker) trips at once */
    arm(&t, &ph, &d, w);
    CHECK(t.state == NWAU_TS_HEALTHY);
    for (b = 0; b < (int)NWAU_SENSE_CHUNK_FRAMES; b++) w[b] = NWAU_SENSE_MARKER;
    CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) == NWAU_TS_EV_TRIP);
    CHECK(t.trip_reason == NWAU_TS_R_UNWRITTEN);
    /* a few marker words (< 25 %) are tolerated */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c), 4.6, &ph, 1, 1, 0);
    for (b = 0; b < 200; b++) w[b] = NWAU_SENSE_MARKER;
    CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) == NWAU_TS_EV_NONE);

    /* V sense dead (zeros) while loud TX: trips after NWAU_TS_DEAD_BLOCKS..+smoothing,
       and those blocks heat the model with the expected power */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    {
        nws64 c0 = 46000000;
        t.t_coil_uc = t.t_mag_uc = t.hyst_coil_uc = t.hyst_mag_uc = c0;   /* to see the heating */
        memset(w, 0, sizeof(w));
        ev = 0;
        for (b = 0; b < 20 && ev != NWAU_TS_EV_TRIP; b++) ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
        CHECK(ev == NWAU_TS_EV_TRIP && t.trip_reason == NWAU_TS_R_DEAD);
        CHECK(b <= 8);
        CHECK(t.t_coil_uc > c0);
        printf("dead V sense: tripped after %d blocks (%.0f ms)\n", b, b * NWAU_SENSE_CHUNK_FRAMES / 48.0);
    }

    /* I dead (V fine): impedance out of range */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    for (b = 0, ev = 0; b < 10 && ev != NWAU_TS_EV_TRIP; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c), 4.6, &ph, 1, 0.0, 0);
        ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_TRIP && t.trip_reason == NWAU_TS_R_IMPEDANCE && b == NWAU_TS_BAD_BLOCKS);

    /* I scaled wrong by 10x (Z = 46 ohm) */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    for (b = 0, ev = 0; b < 10 && ev != NWAU_TS_EV_TRIP; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c), 4.6, &ph, 1, 0.1, 0);
        ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_TRIP && t.trip_reason == NWAU_TS_R_IMPEDANCE);

    /* inverted current: negative power */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    for (b = 0, ev = 0; b < 10 && ev != NWAU_TS_EV_TRIP; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c), 4.6, &ph, 1, 1, 1);
        ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_TRIP && t.trip_reason == NWAU_TS_R_NEG_POWER);

    /* saturated V sense while the sent signal is quiet (-6 dBFS at the static cap = 0.9 V) */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    for (b = 0, ev = 0; b < 10 && ev != NWAU_TS_EV_TRIP; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, 20.0, 4.6, &ph, 1, 1, 0);
        ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_TRIP && t.trip_reason == NWAU_TS_R_SATURATED);
    /* clipping of a full scale sine at gain 0x14 / volume 0 (11.2 V rms, 15.8 V peak) is
       expected: no trip, and the model uses the sent power (27 W at 4.6 ohm) */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    t.t_coil_uc = t.t_mag_uc = t.hyst_coil_uc = t.hyst_mag_uc = 46000000;
    d.gain_reg = 0x14; d.vol_reg = 0;
    for (b = 0, ev = 0; b < 10; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(32767.0, 0x14, 0), 4.6, &ph, 1, 1, 0);
        d.tx_energy = tx_energy_sine(32767.0, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        ev |= nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_NONE && t.state == NWAU_TS_HEALTHY && t.sat > 0);
    {
        ref_model r = { 46.0, 46.0 };
        double pf = pow(10.0, 21.0 / 10.0) / 4.6 * (32767.0 / 32768.0) * (32767.0 / 32768.0);
        ref_step(&r, pf, 10 * NWAU_SENSE_CHUNK_FRAMES);
        CHECK(ts_coil(&t) >= r.coil - 0.3);
        printf("clipped full scale 10 blocks: model coil %.2f degC, exact at %.1f W %.2f degC\n", ts_coil(&t), pf, r.coil);
    }
    d.gain_reg = 0x00; d.vol_reg = 0x0c;

    /* V sense 10 dB too low compared with what was sent: never arms, then trips as dead */
    nwau_ts_stream_start(&t, 0);
    d.gain_reg = 0; d.vol_reg = 0x0c;
    for (b = 0, ev = 0; b < 40 && ev != NWAU_TS_EV_TRIP; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c) * 0.316, 4.6, &ph, 1, 1, 0);
        d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
        CHECK(t.state != NWAU_TS_HEALTHY);
    }
    CHECK(ev == NWAU_TS_EV_TRIP && t.trip_reason == NWAU_TS_R_DEAD && t.verified == 0);

    /* V and I both at half scale (-6 dB V^2, 4x less power): no trip, but never armed */
    nwau_ts_stream_start(&t, 0);
    for (b = 0, ev = 0; b < 60; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c) * 0.5, 4.6, &ph, 1, 1, 0);
        d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        ev |= nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_NONE && t.state == NWAU_TS_ARMING && t.verified == 0);
    CHECK(t.ratio_permille > 240 && t.ratio_permille < 260);
    /* 2.5 dB low still arms (within -3 dB) */
    nwau_ts_stream_start(&t, 0);
    for (b = 0, ev = 0; b < 20; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c) * 0.75, 4.6, &ph, 1, 1, 0);
        d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        ev |= nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(t.state == NWAU_TS_HEALTHY);

    /* V sense 9.5 dB too high: no trip, never verified (stays in static mode) */
    nwau_ts_stream_start(&t, 0);
    for (b = 0, ev = 0; b < 40; b++) {
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c) * 3.0, 4.6, &ph, 1, 1, 0);
        d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        ev |= nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_NONE && t.state == NWAU_TS_ARMING && t.ratio_permille > NWAU_TS_RATIO_HI_PERMILLE);

    /* over temperature: 19.6 W measured with no control trips with OVERTEMP */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    d.gain_reg = 0x14; d.vol_reg = 0;
    for (b = 0, ev = 0; b < 2000 && ev == NWAU_TS_EV_NONE; b++) {
        double v = 9.5, aa = v / out_vrms(32768.0, 0x14, 0) * 32768.0;
        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, v, 4.6, &ph, 1, 1, 0);
        d.tx_energy = tx_energy_sine(aa, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
    }
    CHECK(ev == NWAU_TS_EV_OVERTEMP && t.trip_reason == NWAU_TS_R_OVERTEMP);
    CHECK(ts_coil(&t) > 180.0 && ts_coil(&t) < 192.0);
    printf("uncontrolled 19.6 W: overtemp fault after %.2f s\n", b * 1024 / SR);

    /* warm-up: garbage in the first block of a stream is ignored (no trip, no
       verification), the model takes the sent power; the next garbage block counts */
    nwau_ts_stream_start(&t, 0);
    {
        nwu32 k, seed = 12345;
        nws64 c0;
        t.t_coil_uc = t.t_mag_uc = t.hyst_coil_uc = t.hyst_mag_uc = 46000000;
        c0 = t.t_coil_uc;
        d.gain_reg = 0x00; d.vol_reg = 0x0c;
        d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        for (b = 0; b < (int)NWAU_TS_WARMUP_BLOCKS; b++) {
            for (k = 0; k < NWAU_SENSE_CHUNK_FRAMES; k++) { seed = seed * 1103515245u + 12345u; w[k] = seed; }
            w[0] = NWAU_SENSE_MARKER;
            for (k = 1; k < NWAU_SENSE_CHUNK_FRAMES / 2; k++) w[k] = NWAU_SENSE_MARKER;   /* half unwritten */
            CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) == NWAU_TS_EV_NONE);
            CHECK(t.state == NWAU_TS_ARMING && t.bad_run == 0 && t.verified == 0);
        }
        CHECK(t.t_coil_uc > c0);                        /* heated with the sent power */
        CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) == NWAU_TS_EV_TRIP);
        CHECK(t.trip_reason == NWAU_TS_R_UNWRITTEN);
    }

    /* bridge: healthy and cool -> a sense gap up to 0.5 s does not trip; blind model runs */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    t.t_coil_uc = t.t_mag_uc = t.hyst_coil_uc = t.hyst_mag_uc = 80000000;
    CHECK(nwau_ts_stale(&t, 140000) == NWAU_TS_EV_NONE && !t.bridging);
    CHECK(nwau_ts_stale(&t, 160000) == NWAU_TS_EV_BRIDGE && t.bridging && t.state == NWAU_TS_HEALTHY);
    CHECK(nwau_ts_stale(&t, 300000) == NWAU_TS_EV_NONE && t.state == NWAU_TS_HEALTHY);
    {
        nws64 c0 = t.t_coil_uc;
        CHECK(nwau_ts_blind(&t, tx_energy_sine(32767.0, NWAU_SENSE_CHUNK_FRAMES), NWAU_SENSE_CHUNK_FRAMES, 0x14, 0) == NWAU_TS_EV_NONE);
        CHECK(t.t_coil_uc > c0);                        /* blind model heats while bridging */
    }
    nwau_ts_rx_restarted(&t);
    CHECK(t.stream_blocks == 0);
    synth_block(w, NWAU_SENSE_CHUNK_FRAMES, out_vrms(a, 0, 0x0c), 4.6, &ph, 1, 1, 0);
    d.gain_reg = 0; d.vol_reg = 0x0c;
    d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
    CHECK(nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d) == NWAU_TS_EV_NONE);
    CHECK(!t.bridging && t.state == NWAU_TS_HEALTHY && t.bridges >= 1);
    /* gap longer than the bridge: trip */
    CHECK(nwau_ts_stale(&t, 200000) == NWAU_TS_EV_BRIDGE);
    CHECK(nwau_ts_stale(&t, 500001) == NWAU_TS_EV_TRIP && t.trip_reason == NWAU_TS_R_STALE && !t.bridging);
    /* hot model: no bridge, trip at 150 ms */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    t.t_coil_uc = t.hyst_coil_uc = 112000000;
    CHECK(nwau_ts_stale(&t, 160000) == NWAU_TS_EV_TRIP);
    /* arming (not verified yet): no bridge */
    nwau_ts_stream_start(&t, 0);
    t.t_coil_uc = t.t_mag_uc = t.hyst_coil_uc = t.hyst_mag_uc = 80000000;
    CHECK(t.state == NWAU_TS_ARMING && nwau_ts_stale(&t, 160000) == NWAU_TS_EV_TRIP);
    /* blind is refused while healthy and not bridging */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    {
        nws64 c0 = t.t_coil_uc;
        nwau_ts_blind(&t, tx_energy_sine(32767.0, NWAU_SENSE_CHUNK_FRAMES), NWAU_SENSE_CHUNK_FRAMES, 0x14, 0);
        CHECK(t.t_coil_uc == c0);
    }

    /* blind model while tripped: tracks the sent power at 4.0 ohm; rearm keeps the temperature */
    nwau_ts_stream_start(&t, 0);
    arm(&t, &ph, &d, w);
    t.t_coil_uc = t.t_mag_uc = t.hyst_coil_uc = t.hyst_mag_uc = 46000000;
    CHECK(nwau_ts_blind(&t, tx_energy_sine(32767.0, NWAU_SENSE_CHUNK_FRAMES), NWAU_SENSE_CHUNK_FRAMES, 0x14, 0) == NWAU_TS_EV_NONE);
    CHECK(t.t_coil_uc == 46000000);                      /* only while tripped */
    nwau_ts_trip(&t, NWAU_TS_R_STALE);
    {
        ref_model r = { 46.0, 46.0 };
        double pb = pow(10.0, 21.0 / 10.0) / 4.0 * (32767.0 / 32768.0) * (32767.0 / 32768.0);
        for (b = 0; b < 5; b++) {
            ev = nwau_ts_blind(&t, tx_energy_sine(32767.0, NWAU_SENSE_CHUNK_FRAMES), NWAU_SENSE_CHUNK_FRAMES, 0x14, 0);
            ref_step(&r, pb, NWAU_SENSE_CHUNK_FRAMES);
        }
        CHECK(ev == NWAU_TS_EV_NONE && fabs(ts_coil(&t) - r.coil) < 0.3);
        printf("blind model 5 blocks at 31.5 W: coil %.2f (exact %.2f)\n", ts_coil(&t), r.coil);
        CHECK((t.trips & 0x80000000u) == 0);            /* tracked: no cold raise at the next start */
    }
    {
        nws64 c = t.t_coil_uc;
        nwau_ts_rearm(&t);
        CHECK(t.state == NWAU_TS_ARMING && t.t_coil_uc == c && t.verified == 0 && t.stream_blocks == 0);
        CHECK(nwau_ts_blind(&t, 1, 1, 0, 0x0c) == NWAU_TS_EV_NONE && t.t_coil_uc == c);
        arm(&t, &ph, &d, w);
        CHECK(t.state == NWAU_TS_HEALTHY);
    }
    /* blind overtemp */
    nwau_ts_trip(&t, NWAU_TS_R_STALE);
    for (b = 0, ev = 0; b < 4000 && ev == NWAU_TS_EV_NONE; b++) {
        ev = nwau_ts_blind(&t, tx_energy_sine(32767.0, NWAU_SENSE_CHUNK_FRAMES), NWAU_SENSE_CHUNK_FRAMES, 0x14, 0);
    }
    CHECK(ev == NWAU_TS_EV_OVERTEMP && t.trip_reason == NWAU_TS_R_OVERTEMP);

    /* explicit trip (driver: RX setup failed) from OFF */
    nwau_ts_init(&t);
    nwau_ts_trip(&t, NWAU_TS_R_SETUP);
    CHECK(t.state == NWAU_TS_TRIPPED && t.trip_reason == NWAU_TS_R_SETUP);
    nwau_ts_stream_stop(&t);
    CHECK(t.state == NWAU_TS_OFF);
}

/* --------------------------------------------------------------- limits */
static void test_ts_limits(void)
{
    nwau_ts t;
    nws64 temp;
    nwu32 prev = 0;
    int mono = 1;

    nwau_ts_init(&t);
    for (temp = 20000000; temp <= 300000000; temp += 100000) {
        nwu32 f;
        t.hyst_coil_uc = temp; t.hyst_mag_uc = 46000000;
        f = nwau_ts_floor(&t);
        if (f < prev) mono = 0;
        prev = f;
        if (temp <= 120000000 && f != 0) mono = 0;
        if (f > 0xc9) mono = 0;
        if (temp == 140000000) {
            /* at t_limit the full scale continuous power is the speakersafetyd allowance */
            double fs_db = nwau_full_scale_db_x2(0x14, f) / 2.0;
            double pw = pow(10.0, fs_db / 10.0) / 4.6 * 2.0;
            CHECK(f == 36);
            CHECK(pw <= 0.94 + 1e-9);
        }
    }
    CHECK(mono);
    /* magnet temperature counts too */
    t.hyst_coil_uc = 46000000; t.hyst_mag_uc = 130000000;
    CHECK(nwau_ts_floor(&t) == 18);
}

/* ------------------------------------------- long full scale closed loop */
/* Driver-like loop: static gain until healthy, then raise (volume first), then
 * slew louder 1 step per 10 ms, quieter at once.  The plant is the double
 * precision reference model driven by the real output power of a full scale
 * 1 kHz sine at the volume/gain in effect, with a coil resistance that rises
 * with temperature.  The sense data the core sees is synthesized from the
 * plant (with clipping at the sense full scale). */
static void fullscale_loop(double idle_s, double minutes)
{
    nwau_ts t;
    ref_model plant;
    nwu32 w[NWAU_SENSE_CHUNK_FRAMES];
    nwu64 ph = 0;
    nwau_ts_drive d;
    nwu32 gain = 0x14, vol = 0x20, b;                 /* fixed 21 dB gain, static ceiling 0x0c + 0x14 */
    nwu32 blocks = (nwu32)(minutes * 60.0 * SR / NWAU_SENSE_CHUNK_FRAMES);
    double maxc = 0, maxdev = 0, maxover = 0, maxfs = -99;
    nwu32 trips = 0, armed_at = 0, min_vol = 0xc9, last_vol = 0;
    const double a = 32767.0;
    nwau_gate g;

    nwau_ts_init(&t);                       /* cold assumption 119 degC */
    nwau_ts_stream_start(&t, (nwu64)(idle_s * 1e6));
    plant.coil = ts_coil(&t); plant.mag = ts_mag(&t);
    memset(&d, 0, sizeof(d));
    memset(&g, 0, sizeof(g));
    g.static_floor = 0x0c; g.prot_gain = NWAU_PROT_GAIN;
    g.serdes_on = g.dma_on = 1; g.inflight = 3;
    for (b = 0; b < blocks; b++) {
        double vr = out_vrms(a, gain, vol);
        double z = 4.6 * (1.0 + 0.0037 * (plant.coil - 25.0)) / (1.0 + 0.0037 * (25.0 - 25.0));
        double p = vr * vr / z;
        int ev;
        nwu32 floor, target, k;
        double fs = nwau_full_scale_db_x2(gain, vol) / 2.0;
        if (fs > maxfs) maxfs = fs;

        synth_block(w, NWAU_SENSE_CHUNK_FRAMES, vr, z, &ph, 1, 1, 0);
        d.tx_energy = tx_energy_sine(a, NWAU_SENSE_CHUNK_FRAMES); d.tx_frames = NWAU_SENSE_CHUNK_FRAMES;
        d.gain_reg = gain; d.vol_reg = vol;
        ev = nwau_ts_block(&t, w, NWAU_SENSE_CHUNK_FRAMES, &d);
        ref_step(&plant, p, NWAU_SENSE_CHUNK_FRAMES);
        if (plant.coil > maxc) maxc = plant.coil;
        if (plant.coil - ts_coil(&t) > maxdev) maxdev = plant.coil - ts_coil(&t);   /* under-estimate */
        if (ts_coil(&t) - plant.coil > maxover) maxover = ts_coil(&t) - plant.coil;
        if (ev == NWAU_TS_EV_TRIP || ev == NWAU_TS_EV_OVERTEMP) trips++;
        if (ev == NWAU_TS_EV_ARMED) armed_at = b;

        /* driver policy (two 10 ms service ticks per block) */
        g.prot_ok = t.state == NWAU_TS_HEALTHY;
        floor = nwau_ts_floor(&t);
        g.prot_floor = floor;
        g.gain_shadow = gain; g.vol_shadow = vol;
        if (b == 0) {
            nwu8 v = 0x14;
            CHECK(nwau_amp_write_allowed(&g, 0x03, &v, 1));      /* gain 0x14 at the static ceiling */
        }
        target = g.prot_ok ? floor : 0x20;              /* healthy: model floor; else the static ceiling */
        for (k = 0; k < 2; k++) {
            if (target > vol) vol = target;              /* quieter at once */
            else if (target < vol) vol--;                /* louder: 0.5 dB per 10 ms */
        }
        {
            nwu8 v = (nwu8)vol;
            CHECK(nwau_amp_write_allowed(&g, 0x05, &v, 1));
        }
        if (gain == 0x14 && vol < min_vol) min_vol = vol;
        last_vol = vol;
    }
    printf("full scale sine %.0f min after %.0f s idle: armed after %u blocks, plant coil max %.2f degC, model under/over max %.2f/%.2f, "
           "loudest full scale %+.1f dBV, final vol 0x%02x (-%.1f dB) coil %.2f magnet %.2f, trips %u\n",
           minutes, idle_s, armed_at, maxc, maxdev, maxover, maxfs, last_vol, last_vol / 2.0, plant.coil, plant.mag, trips);
    CHECK(armed_at > 0 && armed_at < 20);
    CHECK(trips == 0);
    CHECK(maxc < 140.0);                    /* the coil never passes t_limit */
    CHECK(maxdev < 1.0);                    /* the model never runs more than 1 degC cooler than the plant */
    CHECK(maxfs <= 21.0);                   /* never above gain 21 dB at volume 0 dB */
    CHECK(gain == 0x14 && last_vol > 0x0c); /* the limit is active at the end */
    CHECK(min_vol < 0x20);                  /* and it did get louder than the static ceiling */
    if (idle_s >= 3600) {
        CHECK(min_vol == 0 && maxfs == 21.0);   /* a cool speaker gets the full 21 dB for a while */
    }
}

static void test_ts_fullscale_loop(void)
{
    fullscale_loop(0, 20);                  /* from the cold-boot assumption (119 degC) */
    fullscale_loop(7200, 20);               /* after two hours with the amp off */
}
