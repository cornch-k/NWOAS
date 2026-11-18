/*
 * nwbc_core.c - hardware-independent core of the NWOAS BCM4378 Wi-Fi driver.
 * License and provenance: see nwbc_core.h (ISC, from OpenBSD bwfm, checked
 * against Linux brcmfmac; Python reference nwoas_scripts/wifi-s257).
 */
#include "nwbc_core.h"
#include <string.h>

/* ================================================================ EROM */
#define DMP_DESC_MASK     0xF
#define DMP_COMPONENT     0x1
#define DMP_MASTER_PORT   0x3
#define DMP_ADDRESS       0x5
#define DMP_ADDR_GT32     0x8
#define DMP_EOT           0xF
#define DMP_SLAVE_SLAVE   0
#define DMP_SLAVE_SWRAP   2
#define DMP_SLAVE_MWRAP   3
#define DMP_SIZE_4K       0
#define DMP_SIZE_8K       1
#define DMP_SIZE_DESC     3

typedef struct erom_walk {
    nb_rd32_fn rd;
    void *ctx;
    nb_u32 e;
    nb_u32 steps;
    int overflow;
} erom_walk;

static nb_u32 erom_rd(erom_walk *w)
{
    if (++w->steps > NB_EROM_MAX_WORDS) {
        w->overflow = 1;
        return DMP_EOT;
    }
    return w->rd(w->ctx, w->e);
}

/* bwfm_chip_dmp_get_regaddr */
static int erom_regaddr(erom_walk *w, nb_u32 *base, nb_u32 *wrap)
{
    nb_u32 val, typ, wraptype, sztype, stype;
    *base = *wrap = 0;
    val = erom_rd(w);
    typ = val & DMP_DESC_MASK;
    if (typ == DMP_MASTER_PORT) {
        wraptype = DMP_SLAVE_MWRAP;
        w->e += 4;
    } else if ((typ & ~DMP_ADDR_GT32) == DMP_ADDRESS) {
        wraptype = DMP_SLAVE_SWRAP;
    } else {
        return 0;
    }
    while (*base == 0 || *wrap == 0) {
        for (;;) {
            val = erom_rd(w);
            if (w->overflow)
                return 0;
            typ = val & DMP_DESC_MASK;
            if (typ == DMP_COMPONENT)
                return 1;
            if (typ == DMP_EOT)
                return 0;
            w->e += 4;
            if ((typ & ~DMP_ADDR_GT32) == DMP_ADDRESS)
                break;
        }
        if (typ & DMP_ADDR_GT32)
            w->e += 4;
        sztype = (val >> 4) & 0x3;
        if (sztype == DMP_SIZE_DESC) {
            nb_u32 v2 = erom_rd(w);
            w->e += ((v2 & DMP_DESC_MASK) & DMP_ADDR_GT32) ? 8 : 4;
        }
        if (sztype != DMP_SIZE_4K && sztype != DMP_SIZE_8K)
            continue;
        stype = (val >> 6) & 0x3;
        if (*base == 0 && stype == DMP_SLAVE_SLAVE)
            *base = val & 0xFFFFF000u;
        if (*wrap == 0 && stype == wraptype)
            *wrap = val & 0xFFFFF000u;
    }
    return 1;
}

int nb_erom_scan(nb_rd32_fn rd, void *ctx, nb_u32 erom, nb_core *out, int max, int *n)
{
    erom_walk w;
    nb_u32 val, typ = 0, cid, nmw, nsw, rev, base, wrap;
    int cnt = 0;

    if (!rd || !out || !n || max <= 0)
        return NB_E_ARG;
    w.rd = rd; w.ctx = ctx; w.e = erom; w.steps = 0; w.overflow = 0;
    while (typ != DMP_EOT) {
        val = erom_rd(&w); w.e += 4;
        if (w.overflow)
            return NB_E_LOOP;
        typ = val & DMP_DESC_MASK;
        if (typ != DMP_COMPONENT)
            continue;
        cid = (val >> 8) & 0xFFF;
        val = erom_rd(&w); w.e += 4;
        if (w.overflow)
            return NB_E_LOOP;
        if ((val & DMP_DESC_MASK) != DMP_COMPONENT)
            return NB_E_FORMAT;
        nmw = (val >> 14) & 0x1F;
        nsw = (val >> 19) & 0x1F;
        rev = (val >> 24) & 0xFF;
        if (nmw + nsw == 0 && cid != NB_CORE_PMU && cid != NB_CORE_GCI)
            continue;
        if (!erom_regaddr(&w, &base, &wrap)) {
            if (w.overflow)
                return NB_E_LOOP;
            continue;
        }
        if (cnt < max) {
            out[cnt].id = (nb_u16)cid;
            out[cnt].rev = (nb_u8)rev;
            out[cnt].base = base;
            out[cnt].wrap = wrap;
            cnt++;
        }
    }
    *n = cnt;
    return NB_OK;
}

const nb_core *nb_core_find(const nb_core *c, int n, nb_u16 id, int idx)
{
    int i;
    for (i = 0; i < n; i++) {
        if (c[i].id == id) {
            if (idx == 0)
                return &c[i];
            idx--;
        }
    }
    return 0;
}

/* ================================================================= OTP */
static void otp_set(char *dst, const nb_u8 *v, nb_u32 n)
{
    nb_u32 i;
    if (dst[0])
        return;                       /* first occurrence wins (as setdefault) */
    if (n > 17)
        n = 17;
    for (i = 0; i < n; i++)
        dst[i] = (char)v[i];
    dst[n] = 0;
}

int nb_otp_parse(const nb_u8 *otp, nb_u32 len, nb_otp *o)
{
    nb_u32 i = 0;
    if (!otp || !o)
        return NB_E_ARG;
    memset(o, 0, sizeof(*o));
    while (i + 1 < len) {
        nb_u32 t = otp[i], n = otp[i + 1];
        const nb_u8 *d = otp + i + 2;
        if (t == 0 || i + 2 + n > len)
            break;
        o->tuples++;
        if (t == 0x15 && n >= 6 && d[0] == 8 && d[1] == 0 && d[2] == 0 && d[3] == 0) {
            nb_u32 p = 4;
            while (p < n) {
                nb_u32 s = p;
                while (p < n && d[p] != ' ' && d[p] != 0 && d[p] != 0xFF)
                    p++;
                if (p - s >= 3 && p - s <= 17 && d[s + 1] == '=') {
                    switch (d[s]) {
                    case 'M': otp_set(o->module, d + s + 2, p - s - 2); break;
                    case 'V': otp_set(o->vendor, d + s + 2, p - s - 2); break;
                    case 'm': otp_set(o->modrev, d + s + 2, p - s - 2); break;
                    case 's': otp_set(o->chiprev, d + s + 2, p - s - 2); break;
                    default: break;
                    }
                }
                p++;
            }
        }
        i += 2 + n;
    }
    return NB_OK;
}

int nb_nvram_name(char *out, nb_u32 cap, const char *board, const nb_otp *o)
{
    nb_u32 n = 0, i;
    const char *parts[6];
    if (!out || !board || !o || !o->module[0] || !o->vendor[0])
        return NB_E_ARG;
    for (i = 0; o->module[i]; i++)
        if (!((o->module[i] >= 'A' && o->module[i] <= 'Z') || (o->module[i] >= '0' && o->module[i] <= '9')))
            return NB_E_FORMAT;
    for (i = 0; o->vendor[i]; i++)
        if (!(o->vendor[i] >= 'a' && o->vendor[i] <= 'z'))
            return NB_E_FORMAT;
    parts[0] = "P-"; parts[1] = board; parts[2] = "_M-"; parts[3] = o->module;
    parts[4] = "_V-"; parts[5] = o->vendor;
    for (i = 0; i < 6; i++) {
        const char *p = parts[i];
        while (*p) {
            if (n + 1 >= cap)
                return NB_E_ARG;
            out[n++] = *p++;
        }
    }
    if (n + 5 >= cap)
        return NB_E_ARG;
    memcpy(out + n, ".txt", 5);
    return NB_OK;
}

/* =============================================================== NVRAM */
int nb_nvram_convert(const nb_u8 *txt, nb_u32 len, const nb_u8 *mac,
                     nb_u8 *out, nb_u32 cap, nb_u32 *outlen, nb_u32 *entries)
{
    nb_u32 i = 0, o = 0, n = 0, tok;
    int have_mac = 0;
    if (!txt || !out || !outlen)
        return NB_E_ARG;
    while (i < len) {
        nb_u32 s = i, e, k, eq;
        while (i < len && txt[i] != '\n')
            i++;
        e = i;
        i++;                                        /* skip '\n' */
        /* drop CR anywhere in the line: copy into out char by char later */
        while (e > s && txt[e - 1] == '\r')
            e--;
        if (e == s || txt[s] == '#')
            continue;
        /* skip lines made only of CR */
        eq = s;
        while (eq < e && txt[eq] != '=')
            eq++;
        if (n && o + 1 > cap)
            return NB_E_ARG;
        if (n)
            out[o++] = 0;
        if (eq < e) {
            nb_u32 ks = s, ke = eq;
            while (ks < ke && (txt[ks] == ' ' || txt[ks] == '\t'))
                ks++;
            while (ke > ks && (txt[ke - 1] == ' ' || txt[ke - 1] == '\t'))
                ke--;
            if (ke - ks == 7 && memcmp(txt + ks, "macaddr", 7) == 0)
                have_mac = 1;
            for (k = ks; k < ke; k++) {
                if (txt[k] == '\r') continue;
                if (o >= cap) return NB_E_ARG;
                out[o++] = txt[k];
            }
            for (k = eq; k < e; k++) {
                if (txt[k] == '\r') continue;
                if (o >= cap) return NB_E_ARG;
                out[o++] = txt[k];
            }
        } else {
            for (k = s; k < e; k++) {
                if (txt[k] == '\r') continue;
                if (o >= cap) return NB_E_ARG;
                out[o++] = txt[k];
            }
        }
        n++;
    }
    if (mac && !have_mac) {
        static const char hx[] = "0123456789abcdef";
        nb_u32 k;
        if (o + 1 + 8 + 17 > cap)
            return NB_E_ARG;
        if (n)
            out[o++] = 0;
        memcpy(out + o, "macaddr=", 8);
        o += 8;
        for (k = 0; k < 6; k++) {
            out[o++] = (nb_u8)hx[mac[k] >> 4];
            out[o++] = (nb_u8)hx[mac[k] & 15];
            if (k < 5)
                out[o++] = ':';
        }
        n++;
    }
    /* NUL terminate and pad to a multiple of 4 (align_up(len + 1, 4)) */
    {
        nb_u32 padded = nb_align(o + 1, 4);
        if (padded + 4 > cap)
            return NB_E_ARG;
        while (o < padded)
            out[o++] = 0;
    }
    tok = (o / 4) & 0xFFFF;
    tok |= (~tok & 0xFFFF) << 16;
    nb_st32(out + o, tok);
    o += 4;
    *outlen = o;
    if (entries)
        *entries = n;
    return NB_OK;
}

int nb_ram_layout(nb_u32 rambase, nb_u32 ramsize, nb_u32 fwlen, nb_u32 nvlen, nb_ramlayout *l)
{
    if (!l || !ramsize || nvlen > ramsize || (nvlen & 3))
        return NB_E_ARG;
    l->rambase = rambase;
    l->ramsize = ramsize;
    l->top = rambase + ramsize;
    l->fw_end = rambase + nb_align(fwlen, 4);
    l->nvram = l->top - nvlen;
    l->footer = l->nvram - 8;
    l->seed = l->footer - NB_SEED_LEN;
    l->shared_ptr = l->top - 4;
    if (l->fw_end > l->seed)
        return NB_E_RANGE;
    return NB_OK;
}

nb_u32 nb_fw_ramsize_override(const nb_u8 *fw, nb_u32 len)
{
    if (!fw || len < NB_RAMSIZE_OFF + 8)
        return 0;
    if (nb_ld32(fw + NB_RAMSIZE_OFF) != NB_RAMSIZE_MAGIC)
        return 0;
    return nb_ld32(fw + NB_RAMSIZE_OFF + 4);
}

/* ========================================================== shared RAM */
int nb_shared_parse(const nb_u32 *w, const nb_u32 *ri, nb_u32 lo, nb_u32 hi, nb_shared *s)
{
    nb_u32 i;
    if (!w || !ri || !s)
        return NB_E_ARG;
    memset(s, 0, sizeof(*s));
    s->flags = w[NB_SH_FLAGS / 4];
    s->version = s->flags & 0xFF;
    if (s->version < 5 || s->version > 7)
        return NB_E_RANGE;
    s->console = w[NB_SH_CONSOLE / 4];
    s->max_rxbufpost = w[NB_SH_MAX_RXBUFPOST / 4 /* 0x20 */] >> 16;  /* u16 at 0x22 */
    if (!s->max_rxbufpost)
        s->max_rxbufpost = 255;
    s->rx_dataoffset = w[NB_SH_RX_DATAOFFSET / 4];
    s->htod_mb = w[NB_SH_HTOD_MB / 4];
    s->dtoh_mb = w[NB_SH_DTOH_MB / 4];
    s->ring_info = w[NB_SH_RING_INFO / 4];
    s->ringmem = ri[0];
    for (i = 0; i < 4; i++)
        s->tcm_idx[i] = ri[1 + i];
    if (s->version >= 6) {
        s->max_flowrings = ri[13] & 0xFFFF;
        s->max_sub = ri[13] >> 16;
        s->max_compl = ri[14] & 0xFFFF;
    } else {
        s->max_sub = ri[13] & 0xFFFF;
        s->max_flowrings = s->max_sub >= 2 ? s->max_sub - 2 : 0;
        s->max_compl = 3;
    }
    if (s->flags & NB_FLAG_DMA_INDEX)
        s->idx_sz = (s->flags & NB_FLAG_DMA_2B_IDX) ? 2 : 4;
    s->mb_via_ctl = s->version >= 6 && !(s->flags & NB_FLAG_USE_MAILBOX);
    s->hostrdy_db1 = (s->flags & NB_FLAG_HOSTRDY_DB1) != 0;
    s->shared_dar = (s->flags & NB_FLAG_SHARED_DAR) != 0;

    if (!(lo <= s->ring_info && s->ring_info + NB_RI_WORDS * 4 <= hi && !(s->ring_info & 3)))
        return NB_E_RANGE;
    if (!(s->max_sub >= 2 && s->max_sub <= 1024 && s->max_compl >= 3 && s->max_compl <= 1024 &&
          s->max_flowrings <= 512))
        return NB_E_RANGE;
    if (!(lo <= s->ringmem && s->ringmem + NB_RING_MEM_SZ * NB_NUM_COMMON_RINGS <= hi))
        return NB_E_RANGE;
    if (s->rx_dataoffset > 256)
        return NB_E_RANGE;
    if (s->htod_mb && !(lo <= s->htod_mb && s->htod_mb < hi))
        return NB_E_RANGE;
    if (s->dtoh_mb && !(lo <= s->dtoh_mb && s->dtoh_mb < hi))
        return NB_E_RANGE;
    if (!s->idx_sz)
        for (i = 0; i < 4; i++)
            if (!(lo <= s->tcm_idx[i] && s->tcm_idx[i] < hi))
                return NB_E_RANGE;
    return NB_OK;
}

nb_u32 nb_host_cap(const nb_shared *s)
{
    nb_u32 c;
    if (s->version < 6)
        return 0;
    c = s->version | NB_HOSTCAP_DS_NO_OOB_DW;
    if (s->hostrdy_db1)
        c |= NB_HOSTCAP_H2D_ENABLE_HOSTRDY;
    if (s->shared_dar)
        c |= NB_HOSTCAP_H2D_DAR;
    return c;
}

/* ============================================================ DMA layout */
int nb_dma_layout(const nb_shared *s, nb_u32 align, nb_dmalayout *l)
{
    /* ring order and sizes as bwfm_pci_preinit (v7 item sizes) */
    static const nb_u16 nitem[NB_NUM_COMMON_RINGS] = { 64, 1024, 64, 1024, 1024 };
    static const nb_u16 isz7[NB_NUM_COMMON_RINGS] = { 40, 32, 24, 24, 40 };
    static const nb_u16 isz6[NB_NUM_COMMON_RINGS] = { 40, 32, 24, 16, 32 };
    static const nb_u16 h2d[NB_NUM_COMMON_RINGS] = { 1, 1, 0, 0, 0 };
    static const nb_u16 slot[NB_NUM_COMMON_RINGS] = { 0, 1, 0, 1, 2 };
    nb_u32 off = 0, i;
    if (!s || !l || !align || (align & (align - 1)))
        return NB_E_ARG;
    memset(l, 0, sizeof(*l));
    if (s->idx_sz) {
        nb_u32 m = s->max_sub > s->max_compl ? s->max_sub : s->max_compl;
        l->idx_stride = nb_align(m * s->idx_sz, 256);
        l->idx_off = off;
        off = nb_align(off + 4 * l->idx_stride, align);
    }
    for (i = 0; i < NB_NUM_COMMON_RINGS; i++) {
        l->ring[i].off = off;
        l->ring[i].nitem = nitem[i];
        l->ring[i].itemsz = s->version >= 7 ? isz7[i] : isz6[i];
        l->ring[i].is_h2d = h2d[i];
        l->ring[i].idx = slot[i];
        off = nb_align(off + (nb_u32)nitem[i] * l->ring[i].itemsz, align);
    }
    for (i = 0; i < NB_MAX_FLOWRINGS; i++) {
        l->flow[i].off = off;
        l->flow[i].nitem = NB_FLOWRING_ITEMS;
        l->flow[i].itemsz = NB_FLOWRING_ITEMSZ;
        l->flow[i].is_h2d = 1;
        l->flow[i].idx = (nb_u16)(2 + i);
        off = nb_align(off + NB_FLOWRING_ITEMS * NB_FLOWRING_ITEMSZ, align);
    }
    if (s->idx_sz && 2 + NB_MAX_FLOWRINGS > s->max_sub)
        ; /* flowring slots beyond max_sub are never used: nb driver caps flowrings */
    l->scratch_off = off;
    l->ringupd_off = off + 256;
    off = nb_align(off + 256 + NB_RINGUPD_LEN, align);
    l->ioctl_req_off = off;
    off = nb_align(off + NB_IOCTL_MAX_LEN, align);
    l->ioctl_resp_off = off;
    off = nb_align(off + NB_N_IOCTLRESP * NB_CTL_PKT_SIZE, align);
    l->event_off = off;
    off = nb_align(off + NB_N_EVENT * NB_CTL_PKT_SIZE, align);
    l->size = off;
    return NB_OK;
}

nb_u32 nb_ring_write_space(nb_u32 r, nb_u32 w, nb_u32 n)
{
    /* one slot always stays empty (bwfm_pci_ring_write_reserve) */
    if (r >= n || w >= n)
        return 0;
    if (r > w)
        return r - w - 1;
    return r + (n - w) - 1;
}

nb_u32 nb_ring_read_avail(nb_u32 r, nb_u32 w, nb_u32 n, nb_u32 *contig)
{
    nb_u32 total;
    if (r >= n || w >= n) {
        if (contig) *contig = 0;
        return 0;
    }
    if (w >= r) {
        total = w - r;
        if (contig) *contig = total;
    } else {
        total = n - r + w;
        if (contig) *contig = n - r;
    }
    return total;
}

/* ============================================================== msgbuf */
static void hdr(nb_u8 *it, nb_u8 type, nb_u8 ifidx, nb_u32 req)
{
    it[0] = type; it[1] = ifidx; it[2] = 0; it[3] = 0;
    nb_st32(it + 4, req);
}
static void addr64(nb_u8 *p, nb_u64 a)
{
    nb_st32(p, (nb_u32)a);
    nb_st32(p + 4, (nb_u32)(a >> 32));
}

nb_u32 nb_enc_ioctl_req(nb_u8 *it, nb_u32 itemsz, nb_u32 cmd, nb_u16 trans_id, nb_u16 len, nb_u64 iova)
{
    if (!it || itemsz < 40)
        return 0;
    memset(it, 0, itemsz);
    hdr(it, NB_MSG_IOCTLPTR_REQ, 0, NB_IOCTL_REQ_PKTID);
    nb_st32(it + 8, cmd);
    nb_st16(it + 12, trans_id);
    nb_st16(it + 14, len);       /* input_buf_len */
    nb_st16(it + 16, len);       /* output_buf_len */
    addr64(it + 24, iova);
    return itemsz;
}

nb_u32 nb_enc_buf_post(nb_u8 *it, nb_u32 itemsz, nb_u8 type, nb_u32 pktid, nb_u16 len, nb_u64 iova)
{
    if (!it || itemsz < 40)
        return 0;
    memset(it, 0, itemsz);
    hdr(it, type, 0, pktid);
    nb_st16(it + 8, len);
    addr64(it + 16, iova);
    return itemsz;
}

nb_u32 nb_enc_rxbuf_post(nb_u8 *it, nb_u32 itemsz, nb_u32 pktid, nb_u16 len, nb_u64 iova)
{
    if (!it || itemsz < 32)
        return 0;
    memset(it, 0, itemsz);
    hdr(it, NB_MSG_RXBUF_POST, 0, pktid);
    nb_st16(it + 8, 0);          /* metadata_buf_len */
    nb_st16(it + 10, len);       /* data_buf_len */
    addr64(it + 24, iova);       /* data_buf_addr (metadata addr at 16 stays 0) */
    return itemsz;
}

nb_u32 nb_enc_tx_post(nb_u8 *it, nb_u32 itemsz, nb_u8 ifidx, nb_u32 req_id, const nb_u8 *ethhdr,
                      nb_u8 prio, nb_u64 data_iova, nb_u16 data_len)
{
    if (!it || !ethhdr || itemsz < 48)
        return 0;
    memset(it, 0, itemsz);
    hdr(it, NB_MSG_TX_POST, ifidx, req_id);
    memcpy(it + 8, ethhdr, 14);
    it[22] = (nb_u8)(NB_TXF_802_3 | ((prio & 7) << NB_TXF_PRIO_SHIFT));
    it[23] = 1;                  /* seg_cnt */
    addr64(it + 24, 0);          /* metadata_buf_addr */
    addr64(it + 32, data_iova);
    nb_st16(it + 40, 0);         /* metadata_buf_len */
    nb_st16(it + 42, data_len);
    return itemsz;
}

nb_u32 nb_enc_flowring_create(nb_u8 *it, nb_u32 itemsz, nb_u8 ifidx, nb_u16 flow_ring_id, const nb_u8 *da,
                              const nb_u8 *sa, nb_u8 tid, nb_u16 max_items, nb_u16 len_item, nb_u64 iova)
{
    if (!it || !da || !sa || itemsz < 40)
        return 0;
    memset(it, 0, itemsz);
    hdr(it, NB_MSG_FLOW_RING_CREATE, ifidx, 0);
    memcpy(it + 8, da, 6);
    memcpy(it + 14, sa, 6);
    it[20] = tid;
    it[21] = 0;                  /* if_flags */
    nb_st16(it + 22, flow_ring_id);
    it[24] = 0;                  /* tc */
    it[25] = 0;                  /* priority */
    nb_st16(it + 26, 0);         /* int_vector */
    nb_st16(it + 28, max_items);
    nb_st16(it + 30, len_item);
    addr64(it + 32, iova);
    return itemsz;
}

nb_u32 nb_enc_flowring_delete(nb_u8 *it, nb_u32 itemsz, nb_u8 ifidx, nb_u16 flow_ring_id)
{
    if (!it || itemsz < 40)
        return 0;
    memset(it, 0, itemsz);
    hdr(it, NB_MSG_FLOW_RING_DELETE, ifidx, 0);
    nb_st16(it + 8, flow_ring_id);
    nb_st16(it + 10, 0);
    return itemsz;
}

nb_u32 nb_enc_h2d_mailbox(nb_u8 *it, nb_u32 itemsz, nb_u32 data)
{
    if (!it || itemsz < 40)
        return 0;
    memset(it, 0, itemsz);
    hdr(it, NB_MSG_H2D_MAILBOX_DATA, 0xFF, 0);
    nb_st32(it + 8, data);
    return itemsz;
}

int nb_dec_msg(const nb_u8 *it, nb_u32 len, nb_msg *m)
{
    if (!it || !m || len < 16)
        return NB_E_ARG;
    memset(m, 0, sizeof(*m));
    m->type = it[0];
    m->ifidx = it[1];
    m->flags = it[2];
    m->req_id = nb_ld32(it + 4);
    m->status = (nb_s16)nb_ld16(it + 8);
    m->flow_ring_id = nb_ld16(it + 10);
    switch (m->type) {
    case NB_MSG_IOCTL_CMPLT:
        if (len < 24) return NB_E_FORMAT;
        m->resp_len = nb_ld16(it + 12);
        m->trans_id = nb_ld16(it + 14);
        m->cmd = nb_ld32(it + 16);
        break;
    case NB_MSG_WL_EVENT:
        m->event_len = nb_ld16(it + 12);
        break;
    case NB_MSG_RX_CMPLT:
        if (len < 20) return NB_E_FORMAT;
        m->data_len = nb_ld16(it + 14);
        m->data_offset = nb_ld16(it + 16);
        m->rx_flags = nb_ld16(it + 18);
        break;
    case NB_MSG_D2H_MAILBOX_DATA:
        m->mb_data = nb_ld32(it + 12);
        break;
    default:
        break;
    }
    return NB_OK;
}

/* ============================================================== iovars */
nb_u32 nb_iovar(nb_u8 *out, nb_u32 cap, const char *name, const void *payload, nb_u32 plen)
{
    nb_u32 n;
    if (!out || !name)
        return 0;
    n = (nb_u32)strlen(name) + 1;
    if (n + plen > cap)
        return 0;
    memcpy(out, name, n);
    if (plen) {
        if (payload)
            memcpy(out + n, payload, plen);
        else
            memset(out + n, 0, plen);
    }
    return n + plen;
}

nb_u32 nb_dload_count(nb_u32 len)
{
    return (len + NB_DLOAD_MAX_LEN - 1) / NB_DLOAD_MAX_LEN;
}

nb_u32 nb_dload_chunk(const nb_u8 *blob, nb_u32 len, nb_u32 i, nb_u8 *out, nb_u32 cap)
{
    nb_u32 off = i * NB_DLOAD_MAX_LEN, n, flag;
    if (!blob || !out || off >= len)
        return 0;
    n = len - off < NB_DLOAD_MAX_LEN ? len - off : NB_DLOAD_MAX_LEN;
    if (NB_DLOAD_HDR_LEN + n > cap)
        return 0;
    flag = NB_DLOAD_HANDLER_VER;
    if (off == 0)
        flag |= NB_DLOAD_FLAG_BEGIN;
    if (off + n >= len)
        flag |= NB_DLOAD_FLAG_END;
    nb_st16(out, flag);
    nb_st16(out + 2, NB_DLOAD_TYPE_CLM);
    nb_st32(out + 4, n);
    nb_st32(out + 8, 0);         /* crc */
    memcpy(out + NB_DLOAD_HDR_LEN, blob + off, n);
    return NB_DLOAD_HDR_LEN + n;
}

/* ============================================================== events */
int nb_event_parse(const nb_u8 *f, nb_u32 len, nb_event *e)
{
    static const nb_u8 oui[3] = { 0x00, 0x10, 0x18 };
    if (!f || !e)
        return NB_E_ARG;
    if (len < 14 || nb_be16(f + 12) != NB_ETHERTYPE_LINK_CTL)
        return 0;
    if (len < NB_EVENT_HDR_LEN || memcmp(f + 19, oui, 3) != 0 || nb_be16(f + 22) != 1)
        return 0;
    memset(e, 0, sizeof(*e));
    e->flags = nb_be16(f + 26);
    e->type = nb_be32(f + 28);
    e->status = nb_be32(f + 32);
    e->reason = nb_be32(f + 36);
    e->auth = nb_be32(f + 40);
    e->datalen = nb_be32(f + 44);
    memcpy(e->addr, f + 48, 6);
    e->ifidx = f[NB_EVENT_HDR_LEN - 2];
    if (e->datalen > len - NB_EVENT_HDR_LEN)
        return NB_E_FORMAT;
    e->data = f + NB_EVENT_HDR_LEN;
    return 1;
}

void nb_event_mask(nb_u8 *mask, nb_u32 len, const nb_u32 *ev, nb_u32 n)
{
    nb_u32 i;
    memset(mask, 0, len);
    for (i = 0; i < n; i++)
        if (ev[i] / 8 < len)
            mask[ev[i] / 8] |= (nb_u8)(1u << (ev[i] % 8));
}

/* ================================================================ scan */
nb_u32 nb_escan_params(nb_u8 *out, nb_u32 cap, int scan_ver, int scan_type, nb_u16 sync_id,
                       const nb_u8 *ssid, nb_u32 ssid_len)
{
    nb_u8 *p;
    if (!out || ssid_len > 32)
        return 0;
    if (scan_ver == 2 || scan_ver == 3) {
        /* bwfm_escan_params_v2: version u32, action u16, sync u16,
         * then v2 params: version u16, length u16, ssid, bssid, bss_type, pad,
         * scan_type, nprobes, active, passive, home, channel_num (u32 each) */
        if (cap < 8 + 72)
            return 0;
        memset(out, 0, 8 + 72);
        nb_st32(out, 2);
        nb_st16(out + 4, 1);
        nb_st16(out + 6, sync_id);
        p = out + 8;
        nb_st16(p, (nb_u32)scan_ver);
        nb_st16(p + 2, 80);
        nb_st32(p + 4, ssid_len);
        if (ssid_len)
            memcpy(p + 8, ssid, ssid_len);
        memset(p + 40, 0xFF, 6);
        p[46] = 2;               /* DOT11_BSSTYPE_ANY */
        nb_st32(p + 48, (nb_u32)scan_type);
        nb_st32(p + 52, 0xFFFFFFFFu);
        nb_st32(p + 56, 0xFFFFFFFFu);
        nb_st32(p + 60, 0xFFFFFFFFu);
        nb_st32(p + 64, 0xFFFFFFFFu);
        nb_st32(p + 68, 0);
        return 8 + 72;
    }
    /* bwfm_escan_params_v0 (version 1): ssid(36) bssid(6) bss_type scan_type
     * nprobes active passive home channel_num */
    if (cap < 8 + 64)
        return 0;
    memset(out, 0, 8 + 64);
    nb_st32(out, 1);
    nb_st16(out + 4, 1);
    nb_st16(out + 6, sync_id);
    p = out + 8;
    nb_st32(p, ssid_len);
    if (ssid_len)
        memcpy(p + 4, ssid, ssid_len);
    memset(p + 36, 0xFF, 6);
    p[42] = 2;
    p[43] = (nb_u8)scan_type;
    nb_st32(p + 44, 0xFFFFFFFFu);
    nb_st32(p + 48, 0xFFFFFFFFu);
    nb_st32(p + 52, 0xFFFFFFFFu);
    nb_st32(p + 56, 0xFFFFFFFFu);
    nb_st32(p + 60, 0);
    return 8 + 64;
}

int nb_escan_hdr_parse(const nb_u8 *body, nb_u32 len, nb_escan_hdr *h)
{
    if (!body || !h || len < 12)
        return NB_E_FORMAT;
    h->buflen = nb_ld32(body);
    h->version = nb_ld32(body + 4);
    h->sync_id = nb_ld16(body + 8);
    h->count = nb_ld16(body + 10);
    return NB_OK;
}

void nb_chanspec_decode(nb_u16 cs, int io_type, nb_u8 *chan, nb_u8 *band, nb_u16 *bw)
{
    nb_u8 c = (nb_u8)(cs & 0xFF), b = 0;
    nb_u16 w = 20;
    if (io_type == NB_IO_TYPE_D11N) {
        nb_u32 bnd = (cs >> 12) & 3, bwf = (cs >> 10) & 3;
        b = bnd == 2 ? 2 : bnd == 1 ? 5 : 0;
        w = bwf == 3 ? 40 : bwf == 1 ? 10 : 20;
        if (bwf == 3) {          /* 40 MHz: chan field is the center; control from sideband */
            nb_u32 sb = (cs >> 8) & 3;
            c = (nb_u8)(sb == 1 ? c - 2 : sb == 2 ? c + 2 : c);
        }
    } else {
        nb_u32 bnd = (cs >> 14) & 3, bwf = (cs >> 11) & 7, sb = (cs >> 8) & 7;
        b = bnd == 0 ? 2 : bnd == 3 ? 5 : 0;   /* 6 GHz encoding not verified: reported as unknown */
        switch (bwf) {
        case 3: w = 40;  c = (nb_u8)(c - 2 + 4 * (sb & 1)); break;
        case 4: w = 80;  c = (nb_u8)(c - 6 + 4 * (sb & 3)); break;
        case 5: w = 160; c = (nb_u8)(c - 14 + 4 * (sb & 7)); break;
        default: w = 20; break;
        }
    }
    if (chan) *chan = c;
    if (band) *band = b;
    if (bw) *bw = w;
}

nb_u32 nb_chan_freq(nb_u8 band, nb_u8 chan)
{
    if (band == 2) {
        if (chan == 14) return 2484;
        if (chan >= 1 && chan <= 13) return 2407u + 5u * chan;
        return 0;
    }
    if (band == 5) {
        if (chan >= 182) return 4000u + 5u * chan;
        return chan ? 5000u + 5u * chan : 0;
    }
    if (band == 6)
        return chan ? 5950u + 5u * chan : 0;
    return 0;
}

int nb_bss_parse(const nb_u8 *b, nb_u32 len, int io_type, nb_bss *o)
{
    nb_u32 reclen, ieo, iel;
    if (!b || !o || len < NB_BSS_INFO_FIXED)
        return NB_E_FORMAT;
    reclen = nb_ld32(b + 4);
    if (reclen < NB_BSS_INFO_FIXED || reclen > len)
        return NB_E_FORMAT;
    memset(o, 0, sizeof(*o));
    memcpy(o->bssid, b + 8, 6);
    o->beacon_period = nb_ld16(b + 14);
    o->capability = nb_ld16(b + 16);
    o->ssid_len = b[18] > 32 ? 32 : b[18];
    memcpy(o->ssid, b + 19, o->ssid_len);
    o->chanspec = nb_ld16(b + 72);
    o->rssi = (nb_s16)nb_ld16(b + 78);
    nb_chanspec_decode(o->chanspec, io_type, &o->channel, &o->band, &o->bw_mhz);
    if (b[88])                    /* ctl_ch */
        o->channel = b[88];
    o->freq_mhz = nb_chan_freq(o->band, o->channel);
    ieo = nb_ld16(b + 116);
    iel = nb_ld32(b + 120);
    if (ieo && ieo <= reclen && iel <= reclen - ieo) {
        o->ies = b + ieo;
        o->ies_len = iel;
    }
    return NB_OK;
}

int nb_escan_next(const nb_u8 *body, nb_u32 len, nb_u32 *off, int io_type, nb_bss *b)
{
    nb_u32 o, reclen;
    if (!body || !off || !b)
        return NB_E_ARG;
    o = *off;
    if (o + NB_BSS_INFO_FIXED > len)
        return 0;
    reclen = nb_ld32(body + o + 4);
    if (reclen < NB_BSS_INFO_FIXED || o + reclen > len)
        return NB_E_FORMAT;
    if (nb_bss_parse(body + o, reclen, io_type, b) != NB_OK)
        return NB_E_FORMAT;
    *off = o + reclen;
    return 1;
}

nb_u32 nb_bss_probe_body(const nb_bss *b, nb_u8 *out, nb_u32 cap)
{
    nb_u32 n;
    if (!b || !out)
        return 0;
    n = 12 + b->ies_len;
    if (n > cap)
        return 0;
    memset(out, 0, 8);
    nb_st16(out + 8, b->beacon_period);
    nb_st16(out + 10, b->capability);
    if (b->ies_len)
        memcpy(out + 12, b->ies, b->ies_len);
    return n;
}

const nb_u8 *nb_ie_find(const nb_u8 *ies, nb_u32 len, nb_u8 id, const nb_u8 *oui4)
{
    nb_u32 i = 0;
    if (!ies)
        return 0;
    while (i + 2 <= len) {
        nb_u32 l = ies[i + 1];
        if (i + 2 + l > len)
            return 0;
        if (ies[i] == id && (!oui4 || (l >= 4 && memcmp(ies + i + 2, oui4, 4) == 0)))
            return ies + i;
        i += 2 + l;
    }
    return 0;
}

/* ============================================================= connect */
static void put_ssid(nb_u8 *p, const nb_u8 *ssid, nb_u32 n)
{
    memset(p, 0, 36);
    nb_st32(p, n);
    if (n)
        memcpy(p + 4, ssid, n);
}

nb_u32 nb_ext_join_params(nb_u8 *out, nb_u32 cap, const nb_u8 *ssid, nb_u32 ssid_len,
                          const nb_u8 *bssid, nb_u16 chanspec)
{
    nb_u32 n = NB_EXT_JOIN_LEN + (chanspec ? 2 : 0);
    if (!out || !ssid || !ssid_len || ssid_len > 32 || cap < n)
        return 0;
    memset(out, 0, n);
    put_ssid(out, ssid, ssid_len);
    out[36] = 0xFF;                              /* scan_type default */
    nb_st32(out + 40, 0xFFFFFFFFu);              /* nprobes */
    nb_st32(out + 44, 0xFFFFFFFFu);              /* active_time */
    nb_st32(out + 48, 0xFFFFFFFFu);              /* passive_time */
    nb_st32(out + 52, 0xFFFFFFFFu);              /* home_time */
    if (bssid)
        memcpy(out + 56, bssid, 6);
    else
        memset(out + 56, 0xFF, 6);
    if (chanspec) {
        nb_st32(out + 64, 1);
        nb_st16(out + 68, chanspec);
    }
    return n;
}

nb_u32 nb_join_params(nb_u8 *out, nb_u32 cap, const nb_u8 *ssid, nb_u32 ssid_len,
                      const nb_u8 *bssid, nb_u16 chanspec)
{
    nb_u32 n = NB_JOIN_LEN + (chanspec ? 2 : 0);
    if (!out || ssid_len > 32 || cap < n)
        return 0;
    memset(out, 0, n);
    put_ssid(out, ssid, ssid_len);
    if (bssid)
        memcpy(out + 36, bssid, 6);
    else
        memset(out + 36, 0xFF, 6);
    if (chanspec) {
        nb_st32(out + 44, 1);
        nb_st16(out + 48, chanspec);
    }
    return n;
}

nb_u32 nb_wsec_for_cipher(int c)
{
    switch (c) {
    case NB_CIPHER_WEP40: case NB_CIPHER_WEP104: return NB_WSEC_WEP;
    case NB_CIPHER_TKIP: return NB_WSEC_TKIP;
    case NB_CIPHER_CCMP: return NB_WSEC_AES;
    default: return NB_WSEC_NONE;
    }
}

nb_u32 nb_wpa_auth_for(int rsn, int akm)
{
    if (akm == NB_AKM_PSK)
        return rsn ? NB_WPA_AUTH_WPA2_PSK : NB_WPA_AUTH_WPA_PSK;
    if (akm == NB_AKM_PSK256)
        return NB_WPA_AUTH_WPA2_PSK_SHA256;
    return NB_WPA_AUTH_DISABLED;
}

nb_u32 nb_wsec_key(nb_u8 *out, nb_u32 cap, nb_u32 index, int cipher, int pairwise,
                   const nb_u8 *key, nb_u32 keylen, const nb_u8 *peer, const nb_u8 *rsc)
{
    nb_u32 algo;
    if (!out || cap < NB_WSEC_KEY_LEN || keylen > 32 || (keylen && !key))
        return 0;
    switch (cipher) {
    case NB_CIPHER_WEP40: algo = NB_CRYPTO_ALGO_WEP1; break;
    case NB_CIPHER_WEP104: algo = NB_CRYPTO_ALGO_WEP128; break;
    case NB_CIPHER_TKIP: algo = NB_CRYPTO_ALGO_TKIP; break;
    case NB_CIPHER_CCMP: algo = NB_CRYPTO_ALGO_AES_CCM; break;
    case NB_CIPHER_NONE: algo = NB_CRYPTO_ALGO_OFF; break;
    default: return 0;
    }
    memset(out, 0, NB_WSEC_KEY_LEN);
    nb_st32(out + 0, index);
    nb_st32(out + 4, keylen);
    if (keylen)
        memcpy(out + 8, key, keylen);
    /* pad_1[18] at 40..111 */
    nb_st32(out + 112, algo);
    /* bwfm_set_key_cb: group (and WEP) keys are primary; pairwise keys carry the peer address */
    nb_st32(out + 116, pairwise ? 0 : NB_WSEC_PRIMARY_KEY);
    /* pad_2[3] 120..131, iv_initialized 132, pad_3 136, rxiv {hi 140, lo 144, pad 146}, pad_5[2] 148..155, ea 156 */
    if (rsc) {
        nb_st32(out + 140, ((nb_u32)rsc[5] << 24) | ((nb_u32)rsc[4] << 16) | ((nb_u32)rsc[3] << 8) | rsc[2]);
        nb_st16(out + 144, ((nb_u32)rsc[1] << 8) | rsc[0]);
        nb_st32(out + 132, 1);
    }
    if (pairwise && peer)
        memcpy(out + 156, peer, 6);
    return NB_WSEC_KEY_LEN;
}

int nb_rsn_parse(const nb_u8 *ie, nb_u32 *group, nb_u32 *pairwise, nb_u32 *akm, nb_u16 *caps)
{
    static const nb_u8 oui[3] = { 0x00, 0x0F, 0xAC };
    nb_u32 l, p = 2, n;
    if (!ie || ie[0] != 48)
        return NB_E_FORMAT;
    l = ie[1] + 2u;
    if (l < 8 || nb_ld16(ie + 2) != 1)
        return NB_E_FORMAT;
    p = 4;
    if (memcmp(ie + p, oui, 3) != 0)
        return NB_E_FORMAT;
    if (group) *group = ie[p + 3];
    p += 4;
    if (pairwise) *pairwise = 0;
    if (akm) *akm = 0;
    if (caps) *caps = 0;
    if (p + 2 > l) return NB_OK;
    n = nb_ld16(ie + p); p += 2;
    if (p + 4 * n > l) return NB_E_FORMAT;
    if (n && pairwise && memcmp(ie + p, oui, 3) == 0) *pairwise = ie[p + 3];
    p += 4 * n;
    if (p + 2 > l) return NB_OK;
    n = nb_ld16(ie + p); p += 2;
    if (p + 4 * n > l) return NB_E_FORMAT;
    if (n && akm && memcmp(ie + p, oui, 3) == 0) {
        nb_u32 k;
        *akm = ie[p + 3];
        for (k = 0; k < n; k++)               /* prefer plain PSK when offered */
            if (memcmp(ie + p + 4 * k, oui, 3) == 0 && ie[p + 4 * k + 3] == NB_AKM_PSK)
                *akm = NB_AKM_PSK;
    }
    p += 4 * n;
    if (p + 2 <= l && caps) *caps = nb_ld16(ie + p);
    return NB_OK;
}

int nb_rsn_pairwise_mask(const nb_u8 *ie, nb_u32 *mask)
{
    static const nb_u8 oui[3] = { 0x00, 0x0F, 0xAC };
    nb_u32 l, p = 8, n, k;
    if (!ie || !mask || ie[0] != 48)
        return NB_E_FORMAT;
    *mask = 0;
    l = ie[1] + 2u;
    if (l < 8 || nb_ld16(ie + 2) != 1)
        return NB_E_FORMAT;
    if (p + 2 > l)
        return NB_OK;
    n = nb_ld16(ie + p); p += 2;
    if (p + 4 * n > l)
        return NB_E_FORMAT;
    for (k = 0; k < n; k++)
        if (memcmp(ie + p + 4 * k, oui, 3) == 0 && ie[p + 4 * k + 3] < 32)
            *mask |= 1u << ie[p + 4 * k + 3];
    return NB_OK;
}

nb_u32 nb_rsn_build(nb_u8 *out, nb_u32 cap, nb_u32 group, nb_u32 pairwise, nb_u32 akm, nb_u16 caps)
{
    nb_u8 *p = out;
    if (!out || cap < 22)
        return 0;
    p[0] = 48; p[1] = 20;
    nb_st16(p + 2, 1);
    p[4] = 0x00; p[5] = 0x0F; p[6] = 0xAC; p[7] = (nb_u8)group;
    nb_st16(p + 8, 1);
    p[10] = 0x00; p[11] = 0x0F; p[12] = 0xAC; p[13] = (nb_u8)pairwise;
    nb_st16(p + 14, 1);
    p[16] = 0x00; p[17] = 0x0F; p[18] = 0xAC; p[19] = (nb_u8)akm;
    nb_st16(p + 20, caps);
    return 22;
}

int nb_assoc_info_parse(const nb_u8 *b, nb_u32 len, nb_u32 *req_len, nb_u32 *resp_len)
{
    if (!b || len < 8)
        return NB_E_FORMAT;
    if (req_len) *req_len = nb_ld32(b);
    if (resp_len) *resp_len = nb_ld32(b + 4);
    return NB_OK;
}

/* =========================================================== data path */
static const nb_u8 llc_snap[6] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00 };

static nb_u32 hdr80211_len(const nb_u8 *f)
{
    nb_u32 n = 24;
    if ((f[1] & 0x03) == 0x03)
        n += 6;                                   /* 4-address */
    if ((f[0] & 0xF0) & 0x80)                     /* QoS data subtype bit */
        n += 2;
    if (f[1] & 0x80)                              /* +HTC/order */
        n += 4;
    return n;
}

int nb_frame_kind(const nb_u8 *f, nb_u32 len)
{
    nb_u32 h;
    if (!f || len < 14)
        return 0;
    /* 802.11 data frame (type 2), protocol version 0, to-DS from a station,
     * followed by LLC/SNAP: very unlikely to be an Ethernet header. */
    if ((f[0] & 0x0F) == 0x08 && (f[1] & 0x03) == 0x01 && len >= 32) {
        h = hdr80211_len(f);
        if (len >= h + 8 && f[h] == 0xAA && f[h + 1] == 0xAA && f[h + 2] == 0x03)
            return NB_FRAME_80211;
    }
    return NB_FRAME_8023;
}

nb_u32 nb_80211_to_8023(const nb_u8 *in, nb_u32 len, nb_u8 *out, nb_u32 cap)
{
    nb_u32 h, n;
    nb_u8 da[6], sa[6];
    if (!in || !out || len < 32)
        return 0;
    h = hdr80211_len(in);
    if (len < h + 8 || memcmp(in + h, llc_snap, 3) != 0)
        return 0;
    /* to-DS: addr1 = BSSID, addr2 = SA, addr3 = DA */
    memcpy(da, in + 16, 6);
    memcpy(sa, in + 10, 6);
    n = 12 + (len - h - 6);
    if (n > cap)
        return 0;
    memmove(out + 12, in + h + 6, len - h - 6);   /* ethertype + payload (may alias in) */
    memcpy(out, da, 6);
    memcpy(out + 6, sa, 6);
    return n;
}

nb_u32 nb_8023_to_80211(const nb_u8 *eth, nb_u32 len, const nb_u8 *bssid, nb_u8 *out, nb_u32 cap)
{
    return nb_8023_to_80211_p(eth, len, bssid, 0, out, cap);
}

int nb_eapol_key_msg(const nb_u8 *f, nb_u32 len)
{
    nb_u32 ki;
    if (!f || len < 14 || nb_be16(f + 12) != NB_ETHERTYPE_EAPOL)
        return -1;
    if (len < 14 + 7 || f[15] != 3)                 /* packet type 3 = EAPOL-Key */
        return NB_EAPOL_OTHER;
    ki = nb_be16(f + 19);                           /* after version, type, length(2), descriptor */
    if (ki & 0x0008) {                              /* pairwise */
        int ack = (ki & 0x0080) != 0, mic = (ki & 0x0100) != 0, sec = (ki & 0x0200) != 0;
        if (ack && !mic) return 1;
        if (!ack && mic && !sec) return 2;
        if (ack && mic) return 3;
        if (!ack && mic && sec) return 4;
        return NB_EAPOL_OTHER;
    }
    if (ki & 0x0080) return NB_EAPOL_G1;
    if (ki & 0x0100) return NB_EAPOL_G2;
    return NB_EAPOL_OTHER;
}

nb_u32 nb_8023_to_80211_p(const nb_u8 *eth, nb_u32 len, const nb_u8 *bssid, int prot, nb_u8 *out, nb_u32 cap)
{
    nb_u32 n;
    nb_u8 da[6], sa[6], bs[6];
    if (!eth || !out || !bssid || len < 14)
        return 0;
    n = 24 + 6 + (len - 12);
    if (n > cap)
        return 0;
    memcpy(da, eth, 6);
    memcpy(sa, eth + 6, 6);
    memcpy(bs, bssid, 6);
    memmove(out + 30, eth + 12, len - 12);        /* ethertype + payload (may alias eth) */
    memcpy(out + 24, llc_snap, 6);
    out[0] = 0x08; out[1] = (nb_u8)(0x02 | (prot ? 0x40 : 0));   /* data, from-DS (+Protected) */
    out[2] = 0; out[3] = 0;
    memcpy(out + 4, da, 6);                       /* addr1 = DA */
    memcpy(out + 10, bs, 6);                      /* addr2 = BSSID */
    memcpy(out + 16, sa, 6);                      /* addr3 = SA */
    out[22] = 0; out[23] = 0;
    return n;
}

nb_u32 nb_eapol_from_event(const nb_u8 *data, nb_u32 len, const nb_u8 *own, const nb_u8 *peer,
                           nb_u8 *out, nb_u32 cap, int *fmt)
{
    nb_u32 body;
    if (fmt)
        *fmt = 0;
    if (!data || !out || !own || !peer)
        return 0;
    if (len >= 18 && nb_be16(data + 12) == NB_ETHERTYPE_EAPOL) {
        body = 18 + nb_be16(data + 16);
        if (body > len)
            body = len;
        if (body > cap)
            return 0;
        memmove(out, data, body);
        if (fmt) *fmt = 1;
        return body;
    }
    /* bare 802.1X: version 1..3, packet type 0..4 (3 = EAPOL-Key) */
    if (len >= 4 && data[0] >= 1 && data[0] <= 3 && data[1] <= 4) {
        body = 4 + nb_be16(data + 2);
        if (body > len)
            return 0;
        if (14 + body > cap)
            return 0;
        memmove(out + 14, data, body);
        memcpy(out, own, 6);
        memcpy(out + 6, peer, 6);
        out[12] = 0x88; out[13] = 0x8E;
        if (fmt) *fmt = 2;
        return 14 + body;
    }
    return 0;
}

/* ========================================================== board file */
nb_u32 nb_crc32(const nb_u8 *p, nb_u32 n)
{
    nb_u32 c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

int nb_board_parse(const nb_u8 *f, nb_u32 len, nb_board *b)
{
    nb_u32 flags, cl;
    if (!f || !b || len < NB_BOARD_HDR)
        return NB_E_FORMAT;
    if (nb_ld32(f) != NB_BOARD_MAGIC || nb_ld32(f + 4) != 1)
        return NB_E_FORMAT;
    flags = nb_ld32(f + 8);
    cl = nb_ld32(f + 20);
    if (cl > 65536 || NB_BOARD_HDR + cl != len)
        return NB_E_FORMAT;
    if (nb_crc32(f + NB_BOARD_HDR, cl) != nb_ld32(f + 24))
        return NB_E_FORMAT;
    memset(b, 0, sizeof(*b));
    b->have_mac = (flags & 1) != 0;
    memcpy(b->mac, f + 12, 6);
    b->cal = cl ? f + NB_BOARD_HDR : 0;
    b->cal_len = cl;
    return NB_OK;
}

nb_u32 nb_board_build(nb_u8 *out, nb_u32 cap, const nb_u8 *mac, const nb_u8 *cal, nb_u32 cal_len)
{
    if (!out || cap < NB_BOARD_HDR + cal_len || (cal_len && !cal))
        return 0;
    memset(out, 0, NB_BOARD_HDR);
    nb_st32(out, NB_BOARD_MAGIC);
    nb_st32(out + 4, 1);
    nb_st32(out + 8, mac ? 1 : 0);
    if (mac)
        memcpy(out + 12, mac, 6);
    nb_st32(out + 20, cal_len);
    if (cal_len)
        memcpy(out + NB_BOARD_HDR, cal, cal_len);
    nb_st32(out + 24, nb_crc32(out + NB_BOARD_HDR, cal_len));
    return NB_BOARD_HDR + cal_len;
}
