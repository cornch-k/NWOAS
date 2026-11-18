/*
 * NWOAS Mac mini (J274) internal speaker - hardware independent core logic.
 * See nwau_core.h.  Plain C, no OS calls, no floating point.
 * The thermal model is written from the published speakersafetyd J274
 * parameters and the physics of a two stage RC network; no speakersafetyd
 * (GPL) code is copied.
 */
#include "nwau_core.h"

/* PLAN-S256.md "ACPI resources (order fixed)" */
const nwau_res_expect nwau_expected_resources[NWAU_NUM_RESOURCES] = {
    { 0x235014000ull, 0x4000u  },   /* 0: I2C1 */
    { 0x238200000ull, 0x34000u },   /* 1: ADMAC (admac-sio) */
    { 0x238400000ull, 0x18000u },   /* 2: MCA clusters (mca-switch reg 0) */
    { 0x238300000ull, 0x30000u },   /* 3: MCA switch1 (mca-switch reg 1) */
    { 0,              NWAU_WINDOW_BYTES } /* 4: sound window, IPA chosen by the launcher */
};

static int ranges_overlap(nwu64 a, nwu64 alen, nwu64 b, nwu64 blen)
{
    return (a < b + blen) && (b < a + alen);
}

int nwau_check_resource(nwu32 index, nwu64 start, nwu64 length)
{
    nwu32 i;

    if (index >= NWAU_NUM_RESOURCES) {
        return NWAU_ERES_COUNT;
    }
    if (length != nwau_expected_resources[index].length) {
        return (int)(NWAU_ERES_BASE + index);
    }
    if (index != NWAU_RES_WINDOW) {
        if (start != nwau_expected_resources[index].start) {
            return (int)(NWAU_ERES_BASE + index);
        }
        return NWAU_OK;
    }
    /* window: 16 KiB aligned (hypervisor page), non zero, not over a register block */
    if (start == 0 || (start & 0x3fffu) != 0 || start + length < start) {
        return (int)(NWAU_ERES_BASE + index);
    }
    for (i = 0; i < NWAU_RES_WINDOW; i++) {
        if (ranges_overlap(start, length, nwau_expected_resources[i].start,
                           nwau_expected_resources[i].length)) {
            return (int)(NWAU_ERES_BASE + index);
        }
    }
    return NWAU_OK;
}

void nwau_header_from_words(const nwu32 w[NWAU_HDR_WORDS], nwau_header *h)
{
    h->magic       = w[0];
    h->version     = w[1];
    h->ring_iova   = (nwu64)w[2] | ((nwu64)w[3] << 32);
    h->ring_bytes  = w[4];
    h->sample_rate = w[5];
    h->format      = w[6];
    h->admac_chan  = w[7];
    h->mca_cluster = w[8];
    h->amp_addr    = w[9];
    h->vol_cap     = w[10];
    h->status      = w[11];
    h->sense_iova  = (nwu64)w[12] | ((nwu64)w[13] << 32);
    h->sense_bytes = w[14];
    h->sense_chan  = w[15];
    h->sense_format = w[16];
    h->prot_gain   = w[17];
    h->prot_vol_floor = w[18];
}

int nwau_header_has_sense(const nwau_header *h)
{
    return h->version == NWAU_HDR_VERSION_SENSE && (h->status & NWAU_HDR_STATUS_SENSE) != 0;
}

nwu32 nwau_header_sense_offset(const nwau_header *h)
{
    /* window IOVA = ring_iova - NWAU_RING_OFFSET (one DART-sio mapping of the whole window) */
    return (nwu32)(h->sense_iova - (h->ring_iova - NWAU_RING_OFFSET));
}

int nwau_header_validate(const nwau_header *h, nwu32 window_bytes)
{
    if (h->magic != NWAU_HDR_MAGIC) {
        return NWAU_EHDR_MAGIC;
    }
    if (h->version != NWAU_HDR_VERSION && h->version != NWAU_HDR_VERSION_SENSE) {
        return NWAU_EHDR_VERSION;
    }
    if ((h->status & NWAU_HDR_STATUS_READY) == 0) {
        return NWAU_EHDR_NOT_READY;
    }
    if (h->ring_bytes < NWAU_MIN_RING_BYTES ||
        (h->ring_bytes % NWAU_CHUNK_BYTES) != 0 ||
        window_bytes < NWAU_RING_OFFSET ||
        h->ring_bytes > window_bytes - NWAU_RING_OFFSET) {
        return NWAU_EHDR_RING;
    }
    if (h->ring_iova == 0 || (h->ring_iova & 0xfffu) != 0 ||
        h->ring_iova + h->ring_bytes < h->ring_iova ||
        (h->ring_iova >> 48) != 0) {
        return NWAU_EHDR_IOVA;
    }
    if (h->sample_rate != NWAU_SAMPLE_RATE) {
        return NWAU_EHDR_RATE;
    }
    if (h->format != NWAU_FORMAT_MONO24IN32) {
        return NWAU_EHDR_FORMAT;
    }
    /* even = TX (memory -> device) */
    if (h->admac_chan >= NWAU_ADMAC_CHANNELS || (h->admac_chan & 1u) != 0) {
        return NWAU_EHDR_CHANNEL;
    }
    if (h->mca_cluster >= NWAU_MCA_CLUSTERS) {
        return NWAU_EHDR_CLUSTER;
    }
    if (h->amp_addr < 0x08u || h->amp_addr > 0x77u) {
        return NWAU_EHDR_AMP_ADDR;
    }
    if (h->vol_cap < NWAU_VOL_HARD_FLOOR || h->vol_cap > NWAU_VOL_REG_MAX) {
        return NWAU_EHDR_VOL_CAP;
    }
    if (h->version == NWAU_HDR_VERSION) {
        return NWAU_OK;
    }
    /* version 2 */
    if ((h->prot_gain != 0 && h->prot_gain != NWAU_PROT_GAIN) || h->prot_vol_floor > NWAU_VOL_REG_MAX) {
        return NWAU_EHDR_PROT;
    }
    if ((h->status & NWAU_HDR_STATUS_SENSE) == 0) {
        return NWAU_OK;                         /* sense not prepared: static mode only */
    }
    {
        nwu64 win_iova = h->ring_iova - NWAU_RING_OFFSET;
        nwu64 off;
        if (h->sense_iova < win_iova || (h->sense_iova & 0xfffu) != 0) {
            return NWAU_EHDR_SENSE_RING;
        }
        off = h->sense_iova - win_iova;
        if (h->sense_bytes < NWAU_SENSE_MIN_BYTES || (h->sense_bytes % NWAU_SENSE_CHUNK_BYTES) != 0 ||
            off < (nwu64)NWAU_RING_OFFSET + h->ring_bytes ||
            off + h->sense_bytes > window_bytes) {
            return NWAU_EHDR_SENSE_RING;
        }
    }
    /* odd = RX, same group of four channels (one MCA cluster) as the TX channel */
    if (h->sense_chan >= NWAU_ADMAC_CHANNELS || (h->sense_chan & 1u) == 0 ||
        (h->sense_chan / 4u) != (h->admac_chan / 4u)) {
        return NWAU_EHDR_SENSE_CHAN;
    }
    if (h->sense_format != NWAU_SENSE_FORMAT_IV16) {
        return NWAU_EHDR_SENSE_FMT;
    }
    return NWAU_OK;
}

nwu32 nwau_volume_reg(nws32 level_16_16, int muted, nwu32 cap)
{
    return nwau_volume_reg_base(level_16_16, muted, cap < NWAU_VOL_HARD_FLOOR ? NWAU_VOL_HARD_FLOOR : cap);
}

nwu32 nwau_volume_reg_matched(nws32 level_16_16, int muted, nwu32 cap)
{
    nwu32 floor_reg = cap < NWAU_VOL_HARD_FLOOR ? NWAU_VOL_HARD_FLOOR : cap;
    nwu32 reg = nwau_volume_reg_base(level_16_16, muted, 0);

    if (muted) {
        return NWAU_VOL_REG_MAX;
    }
    reg = reg > NWAU_PROT_GAIN ? reg - NWAU_PROT_GAIN : 0;
    if (reg < floor_reg) {
        reg = floor_reg;
    }
    return reg > NWAU_VOL_REG_MAX ? NWAU_VOL_REG_MAX : reg;
}

nwu32 nwau_volume_reg_base(nws32 level_16_16, int muted, nwu32 base)
{
    nwu32 floor_reg = base;
    nwu32 steps;
    nwu32 reg;

    if (floor_reg > NWAU_VOL_REG_MAX) {
        floor_reg = NWAU_VOL_REG_MAX;
    }
    if (muted) {
        return NWAU_VOL_REG_MAX;
    }
    if (level_16_16 >= 0) {
        steps = 0;
    } else {
        /* round to the nearest 0.5 dB step; -level fits in 32 bits unsigned */
        nwu32 att = (nwu32)0 - (nwu32)level_16_16;
        steps = (att + (NWAU_VOL_STEP_16_16 / 2)) / NWAU_VOL_STEP_16_16;
    }
    if (steps > NWAU_VOL_REG_MAX) {
        steps = NWAU_VOL_REG_MAX;
    }
    reg = floor_reg + steps;
    if (reg > NWAU_VOL_REG_MAX) {
        reg = NWAU_VOL_REG_MAX;
    }
    return reg;
}

void nwau_conv_reset(nwau_conv *c)
{
    c->carry[0] = c->carry[1] = c->carry[2] = c->carry[3] = 0;
    c->ncarry = 0;
}

static nws32 mono_value(nws16 left, nws16 right)
{
    return ((nws32)left + (nws32)right) / 2;     /* average: no clipping possible */
}

nwu32 nwau_mono_word(nws16 left, nws16 right)
{
    /* s16 -> s24 (<< 8) -> msb aligned (<< 8) */
    return (nwu32)mono_value(left, right) << 16;
}

static nwu32 frame_from_bytes(const nwu8 *b, nwu64 *energy)
{
    nws16 l = (nws16)(nwu16)((nwu16)b[0] | ((nwu16)b[1] << 8));
    nws16 r = (nws16)(nwu16)((nwu16)b[2] | ((nwu16)b[3] << 8));
    nws32 m = mono_value(l, r);
    if (energy) {
        *energy += (nwu64)((nws64)m * m);
    }
    return (nwu32)m << 16;
}

nwu32 nwau_conv_run(nwau_conv *c, const nwu8 *src, nwu32 len,
                    volatile nwu32 *dst, nwu32 dst_frames, nwu32 *consumed)
{
    return nwau_conv_run_e(c, src, len, dst, dst_frames, consumed, 0);
}

nwu32 nwau_conv_run_e(nwau_conv *c, const nwu8 *src, nwu32 len,
                      volatile nwu32 *dst, nwu32 dst_frames, nwu32 *consumed,
                      nwu64 *energy)
{
    nwu32 used = 0;
    nwu32 out = 0;

    /* finish a carried partial frame first */
    while (c->ncarry > 0 && used < len) {
        if (c->ncarry == 3 && out == dst_frames) {
            break;
        }
        c->carry[c->ncarry++] = src[used++];
        if (c->ncarry == 4) {
            dst[out++] = frame_from_bytes(c->carry, energy);
            c->ncarry = 0;
        }
    }
    if (c->ncarry == 0) {
        while (len - used >= 4 && out < dst_frames) {
            dst[out++] = frame_from_bytes(src + used, energy);
            used += 4;
        }
        /* keep a trailing partial frame only if the whole input fits */
        if (len - used < 4 && len - used > 0 && out < dst_frames) {
            while (used < len) {
                c->carry[c->ncarry++] = src[used++];
            }
        }
    }
    if (consumed) {
        *consumed = used;
    }
    return out;
}

void nwau_conv_skip(nwau_conv *c, nwu32 len)
{
    nwu32 total = c->ncarry + (len & 3u);
    c->carry[0] = c->carry[1] = c->carry[2] = c->carry[3] = 0;
    c->ncarry = total & 3u;
}

int nwau_ring_init(nwau_ring *r, nwu32 ring_bytes, nwu32 chunk_bytes,
                   nwu32 max_inflight, nwu32 min_inflight, nwu32 max_lead)
{
    if (chunk_bytes == 0 || (chunk_bytes & 3u) != 0 || ring_bytes == 0 ||
        (ring_bytes % chunk_bytes) != 0 || max_inflight == 0 ||
        min_inflight > max_inflight || max_lead < chunk_bytes || (max_lead & 3u) != 0 ||
        (nwu64)max_lead + (nwu64)max_inflight * chunk_bytes > ring_bytes) {
        return NWAU_ERING_PARAM;
    }
    r->ring_bytes = ring_bytes;
    r->chunk_bytes = chunk_bytes;
    r->max_inflight = max_inflight;
    r->min_inflight = min_inflight;
    r->max_lead = max_lead;
    nwau_ring_reset(r);
    return NWAU_OK;
}

void nwau_ring_reset(nwau_ring *r)
{
    r->wr = 0;
    r->sub = 0;
    r->done = 0;
}

nwu32 nwau_ring_inflight(const nwau_ring *r)
{
    return (nwu32)((r->sub - r->done) / r->chunk_bytes);
}

nwu32 nwau_ring_write_space(const nwau_ring *r)
{
    nwu64 lead = r->wr - r->sub;
    nwu64 used = r->wr - r->done;
    nwu64 a, b;

    if (lead >= r->max_lead || used >= r->ring_bytes) {
        return 0;
    }
    a = r->max_lead - lead;
    b = r->ring_bytes - used;
    return (nwu32)(a < b ? a : b);
}

nwu32 nwau_ring_write_span(const nwau_ring *r, nwu32 *off)
{
    nwu32 o = (nwu32)(r->wr % r->ring_bytes);
    nwu32 space = nwau_ring_write_space(r);
    nwu32 to_end = r->ring_bytes - o;
    nwu32 span = space < to_end ? space : to_end;

    if (off) {
        *off = o;
    }
    return span & ~3u;
}

void nwau_ring_commit(nwau_ring *r, nwu32 bytes)
{
    nwu32 space = nwau_ring_write_space(r);
    if (bytes > space) {
        bytes = space;          /* defensive: never pass the limits */
    }
    r->wr += bytes & ~3u;
}

int nwau_ring_next(nwau_ring *r, int force_pad,
                   nwu32 *chunk_off, nwu32 *pad_off, nwu32 *pad_len)
{
    nwu64 end = r->sub + r->chunk_bytes;

    *pad_off = 0;
    *pad_len = 0;
    if (nwau_ring_inflight(r) >= r->max_inflight) {
        return NWAU_NEXT_NONE;
    }
    *chunk_off = (nwu32)(r->sub % r->ring_bytes);
    if (r->wr >= end) {
        r->sub = end;
        return NWAU_NEXT_DATA;
    }
    if (!force_pad) {
        return NWAU_NEXT_NONE;
    }
    /* [wr, end) lies inside the chunk being submitted, which is never in flight */
    *pad_off = (nwu32)(r->wr % r->ring_bytes);
    *pad_len = (nwu32)(end - r->wr);
    r->wr = end;
    r->sub = end;
    return NWAU_NEXT_PADDED;
}

nwu32 nwau_ring_complete(nwau_ring *r, nwu32 n)
{
    nwu32 inflight = nwau_ring_inflight(r);
    nwu32 acc = n < inflight ? n : inflight;
    r->done += (nwu64)acc * r->chunk_bytes;
    return acc;
}

/* ======================================================================== */
/* amplifier write gate                                                      */
/* ======================================================================== */

static nwu32 clamp_vol(nwu32 v)
{
    return v > NWAU_VOL_REG_MAX ? NWAU_VOL_REG_MAX : v;
}

static nwu32 gate_static_floor(const nwau_gate *g)
{
    return clamp_vol(g->static_floor < NWAU_VOL_HARD_FLOOR ? NWAU_VOL_HARD_FLOOR : g->static_floor);
}

nwu32 nwau_gate_vol_floor(const nwau_gate *g)
{
    nwu32 sf = gate_static_floor(g);

    if (g->gain_shadow == NWAU_AMP_GAIN_STATIC) {
        return sf;
    }
    if (g->gain_shadow == NWAU_PROT_GAIN && g->prot_gain == NWAU_PROT_GAIN && g->prot_ok) {
        return clamp_vol(g->prot_floor);
    }
    /* gain 0x14 without a healthy protection, or unknown gain (worst case 0x14):
       keep the static full scale (the gain step is 0.5 dB like the volume step) */
    return clamp_vol(sf + NWAU_PROT_GAIN);
}

int nwau_amp_write_allowed(const nwau_gate *g, nwu32 reg, const nwu8 *data, nwu32 count)
{
    nwu32 last;
    nwu32 v;

    if (count == 0 || count > 4 || data == 0) {
        return 0;
    }
    last = reg + count - 1;
    /* 0x00 page select, 0x01 software reset (back to 0 dB / 19 dB defaults), 0x7f book select */
    if (reg <= 0x01 || last >= 0x7f) {
        return 0;
    }
    /* a multi-byte (auto increment) write must not reach 0x02, 0x03 or 0x05 */
    if (count > 1 && reg <= NWAU_AMP_REG_VOLUME && last >= NWAU_AMP_REG_PWR_CTRL) {
        return 0;
    }
    v = data[0];
    if (reg == NWAU_AMP_REG_VOLUME) {
        /* quieter than (or equal to) the verified current value is always safe */
        return v >= nwau_gate_vol_floor(g) || (g->vol_shadow != 0 && v >= g->vol_shadow);
    }
    if (reg == NWAU_AMP_REG_GAIN) {
        if (v == NWAU_AMP_GAIN_STATIC) {
            return 1;                               /* lowering the gain is always allowed */
        }
        if (v == NWAU_PROT_GAIN && g->prot_gain == NWAU_PROT_GAIN &&
            (g->gain_shadow == NWAU_AMP_GAIN_STATIC || g->gain_shadow == NWAU_PROT_GAIN)) {
            /* protection configured: the gain may be 0x14 whenever the verified volume
               already satisfies the 0x14 floor (static: cap + 0x14 = the same +5 dB) */
            nwau_gate g14 = *g;
            g14.gain_shadow = NWAU_PROT_GAIN;
            return g->vol_shadow != 0 && g->vol_shadow >= nwau_gate_vol_floor(&g14);
        }
        return 0;
    }
    if (reg == NWAU_AMP_REG_PWR_CTRL) {
        if (v == NWAU_AMP_PWR_ACTIVE || v == NWAU_AMP_PWR_ACTIVE_SNS) {
            return g->serdes_on && g->dma_on && g->inflight > 0;
        }
        return v == NWAU_AMP_PWR_MUTE || v == NWAU_AMP_PWR_SHUTDOWN || v == NWAU_AMP_PWR_SHDN_SNS ||
               v == NWAU_AMP_PWR_MUTE_SNS;
    }
    return 1;
}

/* ======================================================================== */
/* fixed point helpers                                                       */
/* ======================================================================== */

#define NWAU_LN2_Q32            2977044472ull   /* ln 2 * 2^32 */
#define NWAU_LN10_20_Q32        494476384ull    /* ln 10 / 20 * 2^32: 0.5 dB of power ratio */
#define NWAU_Q30                (1ll << 30)

nwu32 nwau_exp_neg_q30(nwu64 x_q32)
{
    nwu64 k = x_q32 / NWAU_LN2_Q32;
    nwu64 r = x_q32 - k * NWAU_LN2_Q32;           /* [0, ln 2) in Q32 */
    nwu64 rq = (r + 2u) >> 2;                     /* Q30 */
    nwu64 term = (nwu64)NWAU_Q30;
    nws64 sum = NWAU_Q30;
    nwu32 n;

    if (k >= 32) {
        return 0;
    }
    /* exp(-r) = sum (-r)^n / n!, r < 0.7: 14 terms leave < 1e-13 */
    for (n = 1; n <= 14; n++) {
        term = (term * rq + ((nwu64)n << 29)) / ((nwu64)n << 30);
        if (n & 1u) {
            sum -= (nws64)term;
        } else {
            sum += (nws64)term;
        }
    }
    if (k == 0) {
        return (nwu32)sum;
    }
    return (nwu32)(((nwu64)sum + ((nwu64)1 << (k - 1))) >> k);
}

/* exp(-num / den), den < 2^31 */
static nwu32 exp_ratio_q30(nwu64 num, nwu64 den)
{
    nwu64 q;

    while (den >= (1ull << 31)) {
        den >>= 1;
        num >>= 1;
    }
    if (den == 0) {
        return 0;
    }
    q = num / den;
    if (q >= 64) {
        return 0;
    }
    return nwau_exp_neg_q30((q << 32) + (((num % den) << 32) / den));
}

nwu64 nwau_pow10_db_x2_q30(nws32 db_x2)
{
    nwu32 m;
    nwu32 e;

    if (db_x2 > 120) {
        db_x2 = 120;
    }
    if (db_x2 < -240) {
        return 0;
    }
    m = db_x2 < 0 ? (nwu32)(-db_x2) : (nwu32)db_x2;
    e = nwau_exp_neg_q30((nwu64)m * NWAU_LN10_20_Q32);
    if (db_x2 <= 0) {
        return e;
    }
    if (e == 0) {
        return 0;
    }
    return (((nwu64)1 << 60) + e / 2u) / e;
}

nwu32 nwau_isqrt64(nwu64 v)
{
    nwu64 res = 0;
    nwu64 bit = 1ull << 62;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (nwu32)res;
}

/* round(a * e / 2^30) for signed a, e in Q30 */
static nws64 mul_q30(nws64 a, nwu32 e)
{
    if (a < 0) {
        return -(nws64)((((nwu64)(-a)) * e + (1ull << 29)) >> 30);
    }
    return (nws64)((((nwu64)a) * e + (1ull << 29)) >> 30);
}

/* ======================================================================== */
/* thermal model                                                             */
/* ======================================================================== */

#define TS_UC(mc)               ((nws64)(mc) * 1000)
#define TS_AMBIENT_UC           TS_UC(NWAU_TS_T_AMBIENT_MC)
#define TS_COLD_COIL_UC         TS_UC(NWAU_TS_T_LIMIT_MC - NWAU_TS_T_WINDOW_MC - 1000)
#define TS_P_MAX_UW             60000000      /* clamp: above the 52.5 W sense full scale */

static nws64 ts_cold_magnet_uc(void)
{
    /* static split of the coil rise over the two thermal resistances */
    return TS_AMBIENT_UC + (TS_COLD_COIL_UC - TS_AMBIENT_UC) * NWAU_TS_TR_MAGNET /
                           (NWAU_TS_TR_MAGNET + NWAU_TS_TR_COIL);
}

static nws64 max64(nws64 a, nws64 b) { return a > b ? a : b; }
static nws64 min64(nws64 a, nws64 b) { return a < b ? a : b; }

static void ts_hyst(nwau_ts *t)
{
    t->hyst_coil_uc = min64(max64(t->hyst_coil_uc, t->t_coil_uc), t->t_coil_uc + TS_UC(NWAU_TS_T_HYST_MC));
    t->hyst_mag_uc  = min64(max64(t->hyst_mag_uc,  t->t_mag_uc),  t->t_mag_uc  + TS_UC(NWAU_TS_T_HYST_MC));
    if (t->t_coil_uc > t->max_coil_uc) {
        t->max_coil_uc = t->t_coil_uc;
    }
}

static void ts_raise_to_cold(nwau_ts *t)
{
    nws64 cm = ts_cold_magnet_uc();
    if (t->t_coil_uc < TS_COLD_COIL_UC) {
        t->t_coil_uc = TS_COLD_COIL_UC;
    }
    if (t->t_mag_uc < cm) {
        t->t_mag_uc = cm;
    }
    t->hyst_coil_uc = max64(t->hyst_coil_uc, t->t_coil_uc);
    t->hyst_mag_uc = max64(t->hyst_mag_uc, t->t_mag_uc);
    ts_hyst(t);
}

void nwau_ts_init(nwau_ts *t)
{
    nwu32 i;
    nwu8 *b = (nwu8 *)t;
    for (i = 0; i < sizeof(*t); i++) {
        b[i] = 0;
    }
    t->t_coil_uc = TS_COLD_COIL_UC;
    t->t_mag_uc = ts_cold_magnet_uc();
    t->hyst_coil_uc = t->t_coil_uc;
    t->hyst_mag_uc = t->t_mag_uc;
    t->max_coil_uc = t->t_coil_uc;
    t->state = NWAU_TS_OFF;
}

void nwau_ts_idle(nwau_ts *t, nwu64 idle_us)
{
    /* zero input response of  dm/dt = (A - m) / tau_m,  dc/dt = (m - c) / tau_c :
       m(t) - A = em (m0 - A)
       c(t) - A = ec ((c0 - A) - eta (m0 - A)) + eta em (m0 - A),  eta = tau_m / (tau_m - tau_c) */
    nws64 c0 = t->t_coil_uc - TS_AMBIENT_UC;
    nws64 m0 = t->t_mag_uc - TS_AMBIENT_UC;
    nwu32 ec = exp_ratio_q30(idle_us, NWAU_TS_TAU_COIL_US);
    nwu32 em = exp_ratio_q30(idle_us, NWAU_TS_TAU_MAGNET_US);
    nwu32 eta = (nwu32)(((nwu64)NWAU_TS_TAU_MAGNET_US << 30) /
                        (NWAU_TS_TAU_MAGNET_US - NWAU_TS_TAU_COIL_US));
    nws64 b = mul_q30(m0, em);
    nws64 a = mul_q30(c0 - mul_q30(m0, eta), ec);

    if (idle_us == 0) {
        return;
    }
    t->t_coil_uc = TS_AMBIENT_UC + a + mul_q30(b, eta);
    t->t_mag_uc = TS_AMBIENT_UC + b;
    ts_hyst(t);
}

void nwau_ts_restore(nwau_ts *t, nws64 coil_uc, nws64 mag_uc, nwu64 idle_us)
{
    /* never trust a saved state below ambient or absurdly high */
    if (coil_uc < TS_AMBIENT_UC || coil_uc > TS_UC(NWAU_TS_T_LIMIT_MC + NWAU_TS_T_HEADROOM_MC) ||
        mag_uc < TS_AMBIENT_UC || mag_uc > TS_UC(NWAU_TS_T_LIMIT_MC + NWAU_TS_T_HEADROOM_MC)) {
        coil_uc = TS_UC(NWAU_TS_T_LIMIT_MC);            /* worst case */
        mag_uc = TS_UC(NWAU_TS_T_LIMIT_MC);
        idle_us = 0;
    }
    t->t_coil_uc = coil_uc;
    t->t_mag_uc = mag_uc;
    t->hyst_coil_uc = coil_uc;
    t->hyst_mag_uc = mag_uc;
    nwau_ts_idle(t, idle_us);
    ts_raise_to_cold(t);
}

static void ts_reset_health(nwau_ts *t)
{
    t->verified = 0;
    t->bad_run = 0;
    t->dead_run = 0;
    t->exp_inst_mv2 = 0;
    t->exp_s_mv2 = 0;
    t->v2_s_mv2 = 0;
    t->r_min_mohm = 0;
    t->ratio_permille = 0;
    t->stream_blocks = 0;
    t->bridging = 0;
    t->trip_reason = NWAU_TS_R_NONE;
}

/* set by a trip: the rest of that stream runs in static mode without the model */
#define TS_UNMODELLED_FLAG      0x80000000u

void nwau_ts_stream_start(nwau_ts *t, nwu64 idle_us)
{
    nwau_ts_idle(t, idle_us);
    if (t->trips & TS_UNMODELLED_FLAG) {
        /* static mode heats the coil to at most 46 + 100 * 0.69 W = 115 degC
           (continuous full scale sine at gain 0x00, volume 0x0c), below the cold assumption */
        ts_raise_to_cold(t);
        t->trips &= ~TS_UNMODELLED_FLAG;
    }
    ts_reset_health(t);
    t->state = NWAU_TS_ARMING;
}

void nwau_ts_stream_stop(nwau_ts *t)
{
    t->state = NWAU_TS_OFF;
}

void nwau_ts_trip(nwau_ts *t, nwu32 reason)
{
    if (t->state == NWAU_TS_TRIPPED) {
        return;
    }
    t->state = NWAU_TS_TRIPPED;
    t->bridging = 0;
    t->trip_reason = reason;
    t->trips = ((t->trips & ~TS_UNMODELLED_FLAG) + 1u) | TS_UNMODELLED_FLAG;
}

/* advance both stages over frames samples with a constant power (uW) */
static void ts_step(nwau_ts *t, nws64 p_model, nwu32 frames)
{
    nwu32 ec, em;
    nws64 tc_target, tm_target;

    if (p_model < 0) {
        p_model = 0;
    }
    if (p_model > TS_P_MAX_UW) {
        p_model = TS_P_MAX_UW;
    }
    ec = exp_ratio_q30((nwu64)frames * 125u, (nwu64)NWAU_TS_TAU_COIL_US * 6u);    /* dt = frames / 48000 s */
    em = exp_ratio_q30((nwu64)frames * 125u, (nwu64)NWAU_TS_TAU_MAGNET_US * 6u);
    tc_target = t->t_mag_uc + p_model * NWAU_TS_TR_COIL;                        /* uW * degC/W = udegC */
    tm_target = TS_AMBIENT_UC + p_model * NWAU_TS_TR_MAGNET;
    t->t_coil_uc = tc_target + mul_q30(t->t_coil_uc - tc_target, ec);
    t->t_mag_uc = tm_target + mul_q30(t->t_mag_uc - tm_target, em);
    ts_hyst(t);
}

static int ts_overtemp(nwau_ts *t)
{
    if (t->t_coil_uc > TS_UC(NWAU_TS_T_LIMIT_MC + NWAU_TS_T_HEADROOM_MC) ||
        t->t_mag_uc > TS_UC(NWAU_TS_T_LIMIT_MC + NWAU_TS_T_HEADROOM_MC)) {
        t->bad[NWAU_TS_R_OVERTEMP]++;
        if (t->state != NWAU_TS_TRIPPED) {
            nwau_ts_trip(t, NWAU_TS_R_OVERTEMP);
        }
        t->trip_reason = NWAU_TS_R_OVERTEMP;
        return 1;
    }
    return 0;
}

/* expected V^2 (mV^2) of TX data with this energy at gain/vol */
static nws64 ts_expected_mv2(nwu64 tx_energy, nwu32 tx_frames, nwu32 gain_reg, nwu32 vol_reg)
{
    nwu64 ms_q30 = tx_energy / tx_frames;                        /* <= 2^30 */
    nwu64 base = (2000000ull * ms_q30 + (1ull << 29)) >> 30;     /* mV^2 for a 1 Vrms full scale */
    nwu64 pw = nwau_pow10_db_x2_q30(nwau_full_scale_db_x2(gain_reg, vol_reg));
    return (nws64)((base * pw + (1ull << 29)) >> 30);
}

int nwau_ts_blind(nwau_ts *t, nwu64 tx_energy, nwu32 tx_frames, nwu32 gain_reg, nwu32 vol_reg)
{
    nws64 p;

    if ((t->state != NWAU_TS_TRIPPED && !(t->state == NWAU_TS_HEALTHY && t->bridging)) || tx_frames == 0) {
        return NWAU_TS_EV_NONE;
    }
    p = ts_expected_mv2(tx_energy, tx_frames, gain_reg, vol_reg) * 1000 / NWAU_TS_BLIND_Z_MOHM;  /* uW */
    ts_step(t, p, tx_frames);
    t->trips &= ~TS_UNMODELLED_FLAG;            /* the temperature is tracked (conservatively) */
    t->blind_frames += tx_frames;
    return ts_overtemp(t) ? NWAU_TS_EV_OVERTEMP : NWAU_TS_EV_NONE;
}

void nwau_ts_rearm(nwau_ts *t)
{
    if (t->state != NWAU_TS_TRIPPED) {
        return;
    }
    ts_reset_health(t);
    t->state = NWAU_TS_ARMING;
}

void nwau_ts_rx_restarted(nwau_ts *t)
{
    t->stream_blocks = 0;
    t->bad_run = 0;
    t->dead_run = 0;
}

int nwau_ts_stale(nwau_ts *t, nwu64 us_since_last)
{
    if ((t->state == NWAU_TS_ARMING || t->state == NWAU_TS_HEALTHY) && us_since_last > NWAU_TS_STALE_US) {
        if (t->state == NWAU_TS_HEALTHY && us_since_last <= NWAU_TS_BRIDGE_US &&
            max64(t->hyst_coil_uc, t->hyst_mag_uc) < TS_UC(NWAU_TS_BRIDGE_MAX_MC)) {
            if (!t->bridging) {
                t->bridging = 1;
                t->bridges++;
                return NWAU_TS_EV_BRIDGE;
            }
            return NWAU_TS_EV_NONE;
        }
        t->bridging = 0;
        t->bad[NWAU_TS_R_STALE]++;
        nwau_ts_trip(t, NWAU_TS_R_STALE);
        return NWAU_TS_EV_TRIP;
    }
    return NWAU_TS_EV_NONE;
}

nws32 nwau_full_scale_db_x2(nwu32 gain_reg, nwu32 vol_reg)
{
    /* [hypothesis] amp gain 11 dB + 0.5 * reg is the full scale sine level in dBV */
    return 22 + (nws32)gain_reg - (nws32)vol_reg;
}

nwu32 nwau_ts_floor(const nwau_ts *t)
{
    nws64 temp = max64(t->hyst_coil_uc, t->hyst_mag_uc);
    nws64 start = TS_UC(NWAU_TS_T_LIMIT_MC - NWAU_TS_T_WINDOW_MC);
    nws64 att_mdb;
    nws64 steps;

    if (temp <= start) {
        return 0;
    }
    /* attenuation ramps linearly from 0 at t_limit - t_window to the worst case
       continuous-power attenuation at t_limit, and keeps growing above it */
    att_mdb = (NWAU_TS_MIN_ATT_MDB * (temp - start) + TS_UC(NWAU_TS_T_WINDOW_MC) - 1) / TS_UC(NWAU_TS_T_WINDOW_MC);
    steps = (att_mdb + 499) / 500;
    return steps > NWAU_VOL_REG_MAX ? NWAU_VOL_REG_MAX : (nwu32)steps;
}

static void ts_bad(nwu32 *reason, nwu32 r)
{
    if (*reason == NWAU_TS_R_NONE) {
        *reason = r;
    }
}

int nwau_ts_block(nwau_ts *t, const volatile nwu32 *w, nwu32 frames, const nwau_ts_drive *d)
{
    nws64 svv = 0, sii = 0, svi = 0;
    nwu32 marker = 0, sat = 0, k;
    nws64 mvv, mii, mvi, v2, i2, p_uw, p_model;
    nwu32 reason = NWAU_TS_R_NONE;
    int sat_explained = 0;
    int verified_block = 0;

    if ((t->state != NWAU_TS_ARMING && t->state != NWAU_TS_HEALTHY) || frames == 0) {
        return NWAU_TS_EV_NONE;
    }
    t->blocks++;
    t->stream_blocks++;
    t->bridging = 0;                            /* data is back */
    for (k = 0; k < frames; k++) {
        nwu32 x = w[k];
        nws32 iv, vv;
        if (x == NWAU_SENSE_MARKER) {
            marker++;
            continue;
        }
        iv = (nws32)(nws16)(nwu16)(x & 0xffffu);
        vv = (nws32)(nws16)(nwu16)(x >> 16);
        if (vv >= 32767 || vv <= -32767 || iv >= 32767 || iv <= -32767) {
            sat++;
        }
        svv += (nws64)vv * vv;
        sii += (nws64)iv * iv;
        svi += (nws64)vv * iv;
    }
    t->sat = sat;
    if (t->stream_blocks <= NWAU_TS_WARMUP_BLOCKS) {
        sat = 0;
        marker = 0;
    }
    if (marker * 4u > frames) {
        t->bad[NWAU_TS_R_UNWRITTEN]++;
        nwau_ts_trip(t, NWAU_TS_R_UNWRITTEN);
        return NWAU_TS_EV_TRIP;
    }
    mvv = svv / frames;
    mii = sii / frames;
    mvi = svi / frames;
    v2 = (mvv * ((nws64)NWAU_TS_VS_SCALE_MV * NWAU_TS_VS_SCALE_MV)) >> 30;     /* mV^2 */
    i2 = (mii * ((nws64)NWAU_TS_IS_SCALE_MA * NWAU_TS_IS_SCALE_MA)) >> 30;     /* mA^2 */
    p_uw = (mvi * ((nws64)NWAU_TS_VS_SCALE_MV * NWAU_TS_IS_SCALE_MA)) / NWAU_Q30; /* mV * mA = uW */
    t->p_uw = (nws32)(p_uw > 0x7fffffff ? 0x7fffffff : (p_uw < -0x7fffffff ? -0x7fffffff : p_uw));
    t->vrms_mv = nwau_isqrt64((nwu64)v2);
    t->irms_ma = nwau_isqrt64((nwu64)i2);

    /* what was sent: expected V^2 at the amp output for the TX data completed meanwhile */
    if (d != 0 && d->tx_frames != 0) {
        t->exp_inst_mv2 = ts_expected_mv2(d->tx_energy, d->tx_frames, d->gain_reg, d->vol_reg);
    }
    /* the same one-pole smoothing (about 2 blocks) on both, so a level change
       moves both together and only a real mismatch shows in the ratio */
    t->exp_s_mv2 += (t->exp_inst_mv2 - t->exp_s_mv2) / 2;
    t->v2_s_mv2 += (v2 - t->v2_s_mv2) / 2;
    if (t->exp_s_mv2 > 0) {
        nws64 r = t->v2_s_mv2 * 1000 / t->exp_s_mv2;
        t->ratio_permille = r > 0xffffffffll ? 0xffffffffu : (nwu32)r;
    } else {
        t->ratio_permille = 0;
    }

    /* plausibility of this block */
    t->z_mohm = 0;
    t->r_mohm = 0;
    if (sat * NWAU_TS_SAT_DIV > frames) {
        /* clipping is expected when what was sent comes within 12 dB of the
           sense full scale: then the model takes the larger expected power
           below; clipping of a quieter signal means the data is wrong */
        if (t->exp_s_mv2 < NWAU_TS_SAT_EXPLAINED_MV2) {
            ts_bad(&reason, NWAU_TS_R_SATURATED);
        } else {
            sat_explained = 1;
        }
    }
    if (p_uw < NWAU_TS_NEG_UW) {
        ts_bad(&reason, NWAU_TS_R_NEG_POWER);
    }
    if (v2 >= NWAU_TS_SIG_MV2) {
        nws64 z = i2 > 0 ? (nws64)nwau_isqrt64((nwu64)(v2 * 1000000 / i2)) : 0x7fffffff;
        nws64 r = mii > 0 ? mvi * ((nws64)NWAU_TS_VS_SCALE_MV * 1000) / (mii * NWAU_TS_IS_SCALE_MA) : 0;
        t->z_mohm = (nwu32)(z > 0x7fffffff ? 0x7fffffff : z);
        t->r_mohm = r > 0 ? (nwu32)(r > 0x7fffffff ? 0x7fffffff : r) : 0;
        if (z < NWAU_TS_Z_MIN_MOHM || z > NWAU_TS_Z_MAX_MOHM || r < NWAU_TS_R_MIN_MOHM) {
            ts_bad(&reason, NWAU_TS_R_IMPEDANCE);
        } else if (reason == NWAU_TS_R_NONE) {
            if (t->r_min_mohm == 0 || t->r_mohm < t->r_min_mohm) {
                t->r_min_mohm = t->r_mohm;
            }
            t->rtemp_mc = (nws32)(NWAU_TS_TREF_MC +
                ((nws64)t->r_min_mohm - NWAU_TS_RREF_MOHM) * 1000000000ll /
                ((nws64)NWAU_TS_RREF_MOHM * NWAU_TS_A_T_PPM));
        }
    }
    if (t->stream_blocks <= NWAU_TS_WARMUP_BLOCKS) {
        reason = NWAU_TS_R_NONE;                /* no verdict yet */
        sat_explained = 1;                      /* model: the sent power (conservative) */
        t->z_mohm = 0;
    }
    if (reason != NWAU_TS_R_NONE) {
        t->bad[reason]++;
        t->bad_run++;
    } else {
        t->bad_run = 0;
    }
    /* liveness: signal was sent, the V sense must see it */
    if (t->stream_blocks > NWAU_TS_WARMUP_BLOCKS &&
        t->exp_s_mv2 >= NWAU_TS_SIG_MV2 && t->ratio_permille < NWAU_TS_RATIO_LO_PERMILLE) {
        t->dead_run++;
        t->bad[NWAU_TS_R_DEAD]++;
    } else {
        t->dead_run = 0;
    }

    /* model input: measured power; for a suspicious block the larger of the
       measured power and the power the sent signal makes in the nominal impedance */
    p_model = p_uw < 0 ? 0 : p_uw;
    if (reason != NWAU_TS_R_NONE || t->dead_run != 0 || sat_explained) {
        nws64 p_exp = t->exp_inst_mv2 * 1000 / NWAU_TS_Z_NOMINAL_MOHM;    /* mV^2 / mOhm = mW; x1000 = uW */
        if (p_exp > p_model) {
            p_model = p_exp;
        }
    }
    /* one block step of each first order stage (exact for a constant input) */
    ts_step(t, p_model, frames);
    if (ts_overtemp(t)) {
        return NWAU_TS_EV_OVERTEMP;
    }
    if (t->bad_run >= NWAU_TS_BAD_BLOCKS) {
        nwau_ts_trip(t, reason);
        return NWAU_TS_EV_TRIP;
    }
    if (t->dead_run >= NWAU_TS_DEAD_BLOCKS) {
        nwau_ts_trip(t, NWAU_TS_R_DEAD);
        return NWAU_TS_EV_TRIP;
    }
    verified_block = reason == NWAU_TS_R_NONE && !sat_explained && t->z_mohm != 0 &&
                     t->exp_s_mv2 >= NWAU_TS_SIG_MV2 &&
                     t->ratio_permille >= NWAU_TS_VERIFY_LO_PERMILLE &&
                     t->ratio_permille <= NWAU_TS_VERIFY_HI_PERMILLE;
    if (t->state == NWAU_TS_ARMING && verified_block) {
        t->verified++;
        if (t->verified >= NWAU_TS_ARM_BLOCKS) {
            t->state = NWAU_TS_HEALTHY;
            return NWAU_TS_EV_ARMED;
        }
    }
    return NWAU_TS_EV_NONE;
}
