/*
 * Host unit tests for src/core/nwbc_core.c (run on the build Mac):
 *     tests/run-tests.sh
 * Expected bytes come from three places:
 *   - the Python reference that passed on the Mac mini (nwoas_scripts/wifi-s257,
 *     PASS_FW_UP / PASS_SCAN 2026-11-18): message and escan encodings, hex below;
 *   - the hardware run itself (runs/run-20261118-121320-aznboqcr/result.json):
 *     shared-RAM words, host_cap 0x1407, NVRAM/seed addresses, chunk counts;
 *   - optional files given on the command line by run-tests.sh (the macOS NVRAM
 *     file and the Python conversion of it), so no Apple file is stored here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/nwbc_core.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int unhex(const char *h, nb_u8 *out, int cap)
{
    int n = 0;
    while (h[0] && h[1] && n < cap) {
        unsigned v;
        sscanf(h, "%2x", &v);
        out[n++] = (nb_u8)v;
        h += 2;
    }
    return n;
}

static int same_hex(const nb_u8 *b, nb_u32 n, const char *hex)
{
    nb_u8 e[512];
    int m = unhex(hex, e, sizeof(e));
    if ((nb_u32)m != n) {
        fprintf(stderr, "  length %u, expected %d\n", n, m);
        return 0;
    }
    return memcmp(b, e, n) == 0;
}

/* ------------------------------------------------------------ EROM */
static nb_u32 rom[64];
static nb_u32 rom_rd(void *ctx, nb_u32 addr)
{
    (void)ctx;
    return rom[(addr - 0x18010000u) / 4 % 64];
}

static void test_erom(void)
{
    nb_core c[8];
    int n = -1, i = 0;
    /* core 1: chipcommon rev 0x43, nsw 1: slave addr 0x18000000, swrap 0x18100000 */
    rom[i++] = 0x1 | (0x800 << 8);
    rom[i++] = 0x1 | (1u << 19) | (0x43u << 24);
    rom[i++] = 0x18000000u | 0x5;
    rom[i++] = 0x18100000u | 0x5 | (2u << 6);
    /* core 2: pcie2 rev 0x44 with a master port first (mwrap) */
    rom[i++] = 0x1 | (0x83C << 8);
    rom[i++] = 0x1 | (1u << 14) | (1u << 19) | (0x44u << 24);
    rom[i++] = 0x3;                     /* master port descriptor */
    rom[i++] = 0x0;                     /* (skipped word) */
    rom[i++] = 0x18001000u | 0x5;
    rom[i++] = 0x18101000u | 0x5 | (3u << 6);
    rom[i++] = 0xF;                     /* EOT */
    CHECK(nb_erom_scan(rom_rd, 0, 0x18010000u, c, 8, &n) == NB_OK);
    CHECK(n == 2);
    CHECK(c[0].id == 0x800 && c[0].rev == 0x43 && c[0].base == 0x18000000u && c[0].wrap == 0x18100000u);
    CHECK(c[1].id == 0x83C && c[1].rev == 0x44 && c[1].base == 0x18001000u && c[1].wrap == 0x18101000u);
    CHECK(nb_core_find(c, n, 0x83C, 0) == &c[1]);
    CHECK(nb_core_find(c, n, 0x83C, 1) == 0);
    /* a ROM without EOT must stop on the bound, not loop */
    for (i = 0; i < 64; i++) rom[i] = 0x2;
    CHECK(nb_erom_scan(rom_rd, 0, 0x18010000u, c, 8, &n) == NB_E_LOOP);
}

/* ------------------------------------------------------------- OTP */
static void test_otp(void)
{
    nb_u8 otp[64];
    nb_otp o;
    char name[96];
    const char *s = "s=B1 M=RASP V=m m=6.11";
    nb_u32 n = (nb_u32)strlen(s);
    memset(otp, 0, sizeof(otp));
    otp[0] = 0x80; otp[1] = 2; otp[2] = 1; otp[3] = 2;        /* unrelated tuple */
    otp[4] = 0x15; otp[5] = (nb_u8)(4 + n);
    otp[6] = 8;
    memcpy(otp + 10, s, n);
    CHECK(nb_otp_parse(otp, sizeof(otp), &o) == NB_OK);
    CHECK(o.tuples == 2);                                       /* hardware: tuples=0x2 */
    CHECK(strcmp(o.module, "RASP") == 0 && strcmp(o.vendor, "m") == 0);
    CHECK(strcmp(o.modrev, "6.11") == 0 && strcmp(o.chiprev, "B1") == 0);
    CHECK(nb_nvram_name(name, sizeof(name), "atlantisb", &o) == NB_OK);
    CHECK(strcmp(name, "P-atlantisb_M-RASP_V-m.txt") == 0);
    strcpy(o.vendor, "../");
    CHECK(nb_nvram_name(name, sizeof(name), "atlantisb", &o) != NB_OK);
}

/* ----------------------------------------------------------- NVRAM */
static void test_nvram_small(void)
{
    const char *t = "# comment\r\nNVRAMRev=$Rev$\r\n  boardtype = 0x0889\n\nnoequals\nmacaddr=00:90:4c:00:00:01\n";
    nb_u8 out[256];
    nb_u32 n = 0, e = 0;
    const nb_u8 mac[6] = { 1, 2, 3, 4, 5, 6 };
    const char exp[] = "NVRAMRev=$Rev$\0boardtype= 0x0889\0noequals\0macaddr=00:90:4c:00:00:01";
    nb_u32 el = sizeof(exp);           /* includes trailing NUL */
    nb_u32 padded = nb_align(el, 4);
    CHECK(nb_nvram_convert((const nb_u8 *)t, (nb_u32)strlen(t), mac, out, sizeof(out), &n, &e) == NB_OK);
    CHECK(e == 4);                                 /* macaddr already present: not appended */
    CHECK(n == padded + 4);
    CHECK(memcmp(out, exp, el) == 0);
    CHECK(nb_ld32(out + padded) == (((padded / 4) & 0xFFFF) | ((~(padded / 4) & 0xFFFF) << 16)));
    /* append when absent */
    t = "a=1\n";
    CHECK(nb_nvram_convert((const nb_u8 *)t, 4, mac, out, sizeof(out), &n, &e) == NB_OK);
    CHECK(e == 2 && memcmp(out, "a=1\0macaddr=01:02:03:04:05:06", 30) == 0);
    CHECK(nb_nvram_convert((const nb_u8 *)t, 4, mac, out, 10, &n, &e) == NB_E_ARG);
}

static nb_u8 *slurp(const char *p, nb_u32 *len)
{
    FILE *f = fopen(p, "rb");
    nb_u8 *b;
    long n;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (nb_u8 *)malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 0; }
    fclose(f);
    *len = (nb_u32)n;
    return b;
}

/* real macOS NVRAM vs Python convert_nvram (same MAC 02:00:00:00:00:01) */
static void test_nvram_real(const char *txt, const char *py)
{
    nb_u32 tl, pl, n = 0, e = 0;
    nb_u8 *t = slurp(txt, &tl), *p = slurp(py, &pl), *out;
    const nb_u8 mac[6] = { 2, 0, 0, 0, 0, 1 };
    if (!t || !p) { fprintf(stderr, "skip real NVRAM test (%s)\n", txt); free(t); free(p); return; }
    out = (nb_u8 *)malloc(tl + 64);
    CHECK(nb_nvram_convert(t, tl, mac, out, tl + 64, &n, &e) == NB_OK);
    CHECK(n == pl && memcmp(out, p, n) == 0);
    CHECK(n == 0x1E88);           /* hardware: NVRAM at 0x51e178 = top 0x520000 - 0x1e88 (V-m file) */
    free(out); free(t); free(p);
}

static void test_layout(void)
{
    nb_ramlayout l;
    /* hardware 2026-11-18: rambase 0x352000 ramsize 0x1ce000, fw 0x16d97e, NVRAM 0x1e88 */
    CHECK(nb_ram_layout(0x352000, 0x1ce000, 0x16d97e, 0x1e88, &l) == NB_OK);
    CHECK(l.nvram == 0x51e178 && l.seed == 0x51e070 && l.shared_ptr == 0x51fffc);
    CHECK(l.fw_end == 0x352000 + 0x16d980);
    CHECK(nb_ram_layout(0x352000, 0x100000, 0x100000, 0x1e88, &l) == NB_E_RANGE);
    {
        nb_u8 fw[0x80];
        memset(fw, 0, sizeof(fw));
        CHECK(nb_fw_ramsize_override(fw, sizeof(fw)) == 0);
        nb_st32(fw + 0x6C, NB_RAMSIZE_MAGIC); nb_st32(fw + 0x70, 0x1c0000);
        CHECK(nb_fw_ramsize_override(fw, sizeof(fw)) == 0x1c0000);
    }
    CHECK(nb_cr4_bank_bytes(0x20F) == 16 * 1024 && nb_cr4_bank_bytes(0x0F) == 16 * 8192);
}

/* -------------------------------------------------- shared + layout */
static const nb_u32 hw_sh[30] = {   /* scan.shared_words, hardware 2026-11-18 12:13 */
    0x78810007, 0, 0, 0, 0, 0x4ea118, 0, 0x2606388a, 0x23f0109, 0, 0x508e9c, 0x508ea0, 0x5089b8,
    0, 0, 0, 0, 0, 0, 0, 0x4c15a083, 0, 0, 0, 0, 0, 0, 0x3, 0, 0 };

static void test_shared(void)
{
    nb_u32 ri[15];
    nb_shared s;
    nb_dmalayout l;
    memset(ri, 0, sizeof(ri));
    ri[0] = 0x508a04;
    ri[1] = 0x508d14; ri[2] = 0x508dc0; ri[3] = 0x508e6c; ri[4] = 0x508e84;
    ri[13] = 0x28 | (0x2bu << 16);
    ri[14] = 6;
    CHECK(nb_shared_parse(hw_sh, ri, 0x352000, 0x520000, &s) == NB_OK);
    CHECK(s.version == 7 && s.idx_sz == 4 && s.max_rxbufpost == 0x23f && s.rx_dataoffset == 0);
    CHECK(s.max_sub == 0x2b && s.max_compl == 6 && s.max_flowrings == 0x28);
    CHECK(s.mb_via_ctl && s.hostrdy_db1 && !s.shared_dar);
    CHECK(s.console == 0x4ea118 && s.htod_mb == 0x508e9c && s.dtoh_mb == 0x508ea0);
    CHECK(nb_host_cap(&s) == 0x1407);                          /* hardware host_cap */
    CHECK(nb_dma_layout(&s, 4096, &l) == NB_OK);
    CHECK(l.idx_stride == 256);
    CHECK(l.ring[NB_RING_CTRL_SUBMIT].itemsz == 40 && l.ring[NB_RING_TX_COMPLETE].itemsz == 24);
    CHECK(l.ring[NB_RING_RX_COMPLETE].nitem == 1024 && l.ring[NB_RING_RX_COMPLETE].idx == 2);
    CHECK(l.flow[0].idx == 2 && l.flow[3].idx == 5);
    CHECK((l.ring[1].off & 4095) == 0 && l.size > l.event_off);
    CHECK(nb_idx_off(&l, &s, 2, 2) == l.idx_off + 2 * 256 + 8);
    /* range checks */
    ri[0] = 0x100;
    CHECK(nb_shared_parse(hw_sh, ri, 0x352000, 0x520000, &s) == NB_E_RANGE);
}

static void test_ring(void)
{
    nb_u32 c;
    CHECK(nb_ring_write_space(0, 0, 64) == 63);
    CHECK(nb_ring_write_space(5, 4, 64) == 0);
    CHECK(nb_ring_write_space(4, 60, 64) == 7);
    CHECK(nb_ring_read_avail(10, 10, 64, &c) == 0 && c == 0);
    CHECK(nb_ring_read_avail(60, 3, 64, &c) == 7 && c == 4);
    CHECK(nb_ring_read_avail(3, 60, 64, &c) == 57 && c == 57);
    CHECK(nb_ring_read_avail(70, 3, 64, &c) == 0);
}

/* --------------------------------------------- messages vs Python */
static void test_msgs(void)
{
    nb_u8 it[64];
    nb_msg m;
    CHECK(nb_enc_ioctl_req(it, 40, 262, 7, 100, 0x10048000u) == 40);
    CHECK(same_hex(it, 40, "09000000feff00000601000007006400640000000000000000800410000000000000000000000000"));
    CHECK(nb_enc_buf_post(it, 40, NB_MSG_EVENT_BUF_POST, 0x2001, 8192, 0x10064000u + 8192) == 40);
    CHECK(same_hex(it, 40, "0d000000012000000020000000000000006006100000000000000000000000000000000000000000"));
    CHECK(nb_enc_rxbuf_post(it, 32, 0x3005, 2048, 0x123456789aull) == 32);
    CHECK(same_hex(it, 32, "1100000005300000000000080000000000000000000000009a78563412000000"));
    CHECK(nb_enc_h2d_mailbox(it, 40, 2) == 40);
    CHECK(same_hex(it, 40, "23ff0000000000000200000000000000000000000000000000000000000000000000000000000000"));
    {
        const nb_u8 eh[14] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0x88, 0x8E };
        CHECK(nb_enc_tx_post(it, 48, 0, 0x101, eh, 0, 0x2000000040ull, 99) == 48);
        CHECK(it[0] == NB_MSG_TX_POST && nb_ld32(it + 4) == 0x101 && memcmp(it + 8, eh, 14) == 0);
        CHECK(it[22] == 1 && it[23] == 1 && nb_ld32(it + 32) == 0x40 && nb_ld32(it + 36) == 0x20 && nb_ld16(it + 42) == 99);
        CHECK(nb_enc_flowring_create(it, 40, 0, 2, eh, eh + 6, 0, 512, 48, 0x10200000u) == 40);
        CHECK(it[0] == 3 && memcmp(it + 8, eh, 6) == 0 && memcmp(it + 14, eh + 6, 6) == 0 && nb_ld16(it + 22) == 2);
        CHECK(nb_ld16(it + 28) == 512 && nb_ld16(it + 30) == 48 && nb_ld32(it + 32) == 0x10200000u);
    }
    /* decode IOCTL_CMPLT: status 0, resp_len 4, trans 7, cmd 1 */
    memset(it, 0, sizeof(it));
    it[0] = NB_MSG_IOCTL_CMPLT; nb_st32(it + 4, 0x1003); nb_st16(it + 12, 4); nb_st16(it + 14, 7); nb_st32(it + 16, 1);
    CHECK(nb_dec_msg(it, 24, &m) == NB_OK && m.type == 0xC && m.req_id == 0x1003 && m.resp_len == 4 && m.trans_id == 7 && m.cmd == 1);
    memset(it, 0, sizeof(it));
    it[0] = NB_MSG_RX_CMPLT; nb_st32(it + 4, 0x3001); nb_st16(it + 14, 1500); nb_st16(it + 16, 8);
    CHECK(nb_dec_msg(it, 40, &m) == NB_OK && m.data_len == 1500 && m.data_offset == 8);
    memset(it, 0, sizeof(it));
    it[0] = NB_MSG_FLOW_RING_CREATE_CMPLT; nb_st16(it + 8, 0xFFFF); nb_st16(it + 10, 2);
    CHECK(nb_dec_msg(it, 24, &m) == NB_OK && m.status == -1 && m.flow_ring_id == 2);
    memset(it, 0, sizeof(it));
    it[0] = NB_MSG_D2H_MAILBOX_DATA; nb_st32(it + 12, NB_D2H_DEV_DS_ENTER_REQ);
    CHECK(nb_dec_msg(it, 24, &m) == NB_OK && m.mb_data == 2);
}

static void test_blobs(void)
{
    nb_u8 blob[3000], out[1500], iov[64];
    nb_u32 i, n;
    for (i = 0; i < sizeof(blob); i++) blob[i] = (nb_u8)i;
    CHECK(nb_dload_count(34061) == 25 && nb_dload_count(2096) == 2 && nb_dload_count(304) == 1); /* hardware */
    CHECK(nb_dload_count(3000) == 3);
    n = nb_dload_chunk(blob, 3000, 0, out, sizeof(out));
    CHECK(n == 12 + 1400 && same_hex(out, 16, "02100200780500000000000000010203"));
    n = nb_dload_chunk(blob, 3000, 1, out, sizeof(out));
    CHECK(n == 1412 && nb_ld16(out) == 0x1000);
    n = nb_dload_chunk(blob, 3000, 2, out, sizeof(out));
    CHECK(n == 12 + 200 && nb_ld16(out) == 0x1004 && out[12] == (nb_u8)2800);
    CHECK(nb_dload_chunk(blob, 3000, 3, out, sizeof(out)) == 0);
    n = nb_iovar(iov, sizeof(iov), "mpc", "\0\0\0\0", 4);
    CHECK(n == 8 && memcmp(iov, "mpc\0\0\0\0\0", 8) == 0);
    CHECK(nb_iovar(iov, 6, "mpc", "\0\0\0\0", 4) == 0);
}

/* ----------------------------------------------------- events/scan */
static void test_escan(void)
{
    nb_u8 p[128], f[600], mask[18];
    nb_u32 n, off;
    nb_event e;
    nb_escan_hdr h;
    nb_bss b;
    const nb_u32 ev[3] = { 69, 0, 16 };
    n = nb_escan_params(p, sizeof(p), 0, NB_SCANTYPE_PASSIVE, NB_ESCAN_SYNC_ID, 0, 0);
    CHECK(same_hex(p, n, "0100000001005752000000000000000000000000000000000000000000000000000000000000000000000000ffffffffffff0201ffffffffffffffffffffffffffffffff00000000"));
    n = nb_escan_params(p, sizeof(p), 2, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, 0, 0);
    CHECK(same_hex(p, n, "020000000100575202005000000000000000000000000000000000000000000000000000000000000000000000000000ffffffffffff020000000000ffffffffffffffffffffffffffffffff00000000"));
    n = nb_escan_params(p, sizeof(p), 1, NB_SCANTYPE_ACTIVE, 1, (const nb_u8 *)"home", 4);
    CHECK(nb_ld32(p + 8) == 4 && memcmp(p + 12, "home", 4) == 0 && p[51] == 0);
    nb_event_mask(mask, 18, ev, 3);
    CHECK(same_hex(mask, 18, "010001000000000020000000000000000000"));

    /* event frame carrying one escan result with one BSS */
    memset(f, 0, sizeof(f));
    f[12] = 0x88; f[13] = 0x6C; f[19] = 0x00; f[20] = 0x10; f[21] = 0x18; f[23] = 1;
    f[28 + 3] = 69;                          /* event type (big endian) */
    f[32 + 3] = NB_E_STATUS_PARTIAL;
    {
        nb_u8 *body = f + NB_EVENT_HDR_LEN, *r = body + 12;
        const nb_u8 ie[] = { 0, 4, 'h', 'o', 'm', 'e', 48, 20, 1, 0, 0, 0x0F, 0xAC, 4, 1, 0, 0, 0x0F, 0xAC, 4,
                             1, 0, 0, 0x0F, 0xAC, 2, 0x0C, 0 };
        nb_u32 rl = 128 + sizeof(ie);
        nb_st32(body, 12 + rl); nb_st32(body + 4, 109); nb_st16(body + 8, NB_ESCAN_SYNC_ID); nb_st16(body + 10, 1);
        nb_st32(r, 109); nb_st32(r + 4, rl);
        memcpy(r + 8, "\xe8\x9f\x80\xeb\xba\x65", 6);
        nb_st16(r + 14, 100); nb_st16(r + 16, 0x0411);
        r[18] = 4; memcpy(r + 19, "home", 4);
        nb_st16(r + 72, 0xD024);             /* 5 GHz, 20 MHz, channel 36 (d11ac) */
        nb_st16(r + 78, (nb_u16)-23);
        r[88] = 36;
        nb_st16(r + 116, 128); nb_st32(r + 120, sizeof(ie));
        memcpy(r + 128, ie, sizeof(ie));
        nb_st32(f + 44, 12 + rl);            /* datalen big endian */
        { nb_u32 dl = 12 + rl; f[44] = (nb_u8)(dl >> 24); f[45] = (nb_u8)(dl >> 16); f[46] = (nb_u8)(dl >> 8); f[47] = (nb_u8)dl; }
        CHECK(nb_event_parse(f, NB_EVENT_HDR_LEN + 12 + rl, &e) == 1);
        CHECK(e.type == 69 && e.status == NB_E_STATUS_PARTIAL && e.datalen == 12 + rl);
        CHECK(nb_escan_hdr_parse(e.data, e.datalen, &h) == NB_OK && h.count == 1 && h.sync_id == NB_ESCAN_SYNC_ID);
        off = 12;
        CHECK(nb_escan_next(e.data, e.datalen, &off, NB_IO_TYPE_D11AC, &b) == 1);
        CHECK(b.ssid_len == 4 && memcmp(b.ssid, "home", 4) == 0 && b.rssi == -23 && b.channel == 36);
        CHECK(b.band == 5 && b.freq_mhz == 5180 && b.bw_mhz == 20 && b.ies_len == sizeof(ie));
        CHECK(nb_escan_next(e.data, e.datalen, &off, NB_IO_TYPE_D11AC, &b) == 0);
        {
            nb_u8 pb[256];
            nb_u32 g = 0, pw = 0, akm = 0; nb_u16 caps = 0;
            const nb_u8 *rsn;
            n = nb_bss_probe_body(&b, pb, sizeof(pb));
            CHECK(n == 12 + sizeof(ie) && nb_ld16(pb + 8) == 100 && nb_ld16(pb + 10) == 0x0411 && pb[12] == 0);
            rsn = nb_ie_find(b.ies, b.ies_len, 48, 0);
            CHECK(rsn && nb_rsn_parse(rsn, &g, &pw, &akm, &caps) == NB_OK);
            CHECK(g == NB_CIPHER_CCMP && pw == NB_CIPHER_CCMP && akm == NB_AKM_PSK && caps == 0x0C);
            n = nb_rsn_build(pb, sizeof(pb), g, NB_CIPHER_CCMP, NB_AKM_PSK, 0);
            CHECK(n == 22 && memcmp(pb, rsn, 20) == 0);
            {
                nb_u32 m = 0;
                static const nb_u8 two[] = { 48, 24, 1, 0, 0x00, 0x0F, 0xAC, 4, 2, 0, 0x00, 0x0F, 0xAC, 8,
                                             0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 2, 0x0C, 0 };
                CHECK(nb_rsn_pairwise_mask(rsn, &m) == NB_OK && m == (1u << NB_CIPHER_CCMP));
                CHECK(nb_rsn_pairwise_mask(two, &m) == NB_OK && m == ((1u << 8) | (1u << 4)));
                CHECK(nb_rsn_pairwise_mask(pb + 1, &m) == NB_E_FORMAT);
            }
        }
    }
    /* not an event: plain IPv4 */
    f[12] = 0x08; f[13] = 0x00;
    CHECK(nb_event_parse(f, 100, &e) == 0);
}

static void test_chanspec(void)
{
    nb_u8 c, band; nb_u16 bw;
    nb_chanspec_decode(0x1006, NB_IO_TYPE_D11AC, &c, &band, &bw);   /* 2.4 GHz ch 6, 20 MHz */
    CHECK(c == 6 && band == 2 && bw == 20);
    nb_chanspec_decode(0xE02A, NB_IO_TYPE_D11AC, &c, &band, &bw);   /* 80 MHz center 42, sb LLL */
    CHECK(c == 36 && band == 5 && bw == 80);
    nb_chanspec_decode(0x2B06, NB_IO_TYPE_D11N, &c, &band, &bw);    /* d11n 2G 20 MHz ch 6 */
    CHECK(c == 6 && band == 2 && bw == 20);
    CHECK(nb_chan_freq(2, 1) == 2412 && nb_chan_freq(2, 14) == 2484 && nb_chan_freq(5, 149) == 5745);
}

/* --------------------------------------------------------- connect */
static void test_connect(void)
{
    nb_u8 o[200];
    const nb_u8 bss[6] = { 0xe8, 0x9f, 0x80, 0xeb, 0xba, 0x64 };
    const nb_u8 key[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    const nb_u8 rsc[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    CHECK(nb_ext_join_params(o, sizeof(o), (const nb_u8 *)"home-ap", 7, bss, 0) == 68);
    CHECK(nb_ld32(o) == 7 && memcmp(o + 4, "home-ap", 7) == 0 && o[36] == 0xFF && nb_ld32(o + 40) == 0xFFFFFFFFu);
    CHECK(memcmp(o + 56, bss, 6) == 0 && nb_ld32(o + 64) == 0);
    CHECK(nb_ext_join_params(o, sizeof(o), (const nb_u8 *)"x", 1, bss, 0xD024) == 70 && nb_ld32(o + 64) == 1 && nb_ld16(o + 68) == 0xD024);
    CHECK(nb_join_params(o, sizeof(o), (const nb_u8 *)"home-ap", 7, bss, 0) == 48 && memcmp(o + 36, bss, 6) == 0);
    CHECK(nb_ext_join_params(o, sizeof(o), (const nb_u8 *)"x", 33, bss, 0) == 0);
    CHECK(nb_wsec_key(o, sizeof(o), 0, NB_CIPHER_CCMP, 1, key, 16, bss, 0) == 164);
    CHECK(nb_ld32(o + 4) == 16 && memcmp(o + 8, key, 16) == 0 && nb_ld32(o + 112) == NB_CRYPTO_ALGO_AES_CCM);
    CHECK(nb_ld32(o + 116) == 0 && memcmp(o + 156, bss, 6) == 0 && nb_ld32(o + 132) == 0);
    CHECK(nb_wsec_key(o, sizeof(o), 1, NB_CIPHER_CCMP, 0, key, 16, bss, rsc) == 164);
    CHECK(nb_ld32(o) == 1 && nb_ld32(o + 116) == NB_WSEC_PRIMARY_KEY && o[156] == 0);
    CHECK(nb_ld32(o + 132) == 1 && nb_ld32(o + 140) == 0x66554433u && nb_ld16(o + 144) == 0x2211);
    CHECK(nb_wpa_auth_for(1, NB_AKM_PSK) == 0x80 && nb_wsec_for_cipher(NB_CIPHER_CCMP) == 4);
    CHECK(nb_wpa_auth_for(0, NB_AKM_PSK) == 0x4 && nb_wpa_auth_for(1, NB_AKM_NONE) == 0);
    {
        nb_u8 ai[8]; nb_u32 a, b;
        nb_st32(ai, 120); nb_st32(ai + 4, 80);
        CHECK(nb_assoc_info_parse(ai, 8, &a, &b) == NB_OK && a == 120 && b == 80);
    }
}

/* -------------------------------------------------------- data path */
static void test_frames(void)
{
    nb_u8 eth[64], w[128], back[128];
    nb_u32 n, m;
    const nb_u8 bss[6] = { 0xe8, 0x9f, 0x80, 0xeb, 0xba, 0x64 };
    memset(eth, 0, sizeof(eth));
    memcpy(eth, "\x01\x02\x03\x04\x05\x06", 6);          /* DA */
    memcpy(eth + 6, "\x0a\x0b\x0c\x0d\x0e\x0f", 6);      /* SA */
    eth[12] = 0x88; eth[13] = 0x8E;                       /* EAPOL */
    eth[14] = 0x02;
    CHECK(nb_frame_kind(eth, 60) == NB_FRAME_8023);
    n = nb_8023_to_80211(eth, 60, bss, w, sizeof(w));
    CHECK(n == 60 + 18 && w[0] == 0x08 && w[1] == 0x02 && memcmp(w + 4, eth, 6) == 0);
    CHECK(memcmp(w + 10, bss, 6) == 0 && memcmp(w + 16, eth + 6, 6) == 0 && w[24] == 0xAA && w[30] == 0x88);
    /* outbound 802.11 (to-DS: addr1 BSSID, addr2 SA, addr3 DA) */
    w[1] = 0x01;
    memcpy(w + 4, bss, 6); memcpy(w + 10, eth + 6, 6); memcpy(w + 16, eth, 6);
    CHECK(nb_frame_kind(w, n) == NB_FRAME_80211);
    m = nb_80211_to_8023(w, n, back, sizeof(back));
    CHECK(m == 60 && memcmp(back, eth, 60) == 0);
    /* in place */
    m = nb_80211_to_8023(w, n, w, sizeof(w));
    CHECK(m == 60 && memcmp(w, eth, 60) == 0);
    memcpy(back, eth, 60);
    n = nb_8023_to_80211(back, 60, bss, back, sizeof(back));
    CHECK(n == 78 && memcmp(back + 4, eth, 6) == 0 && memcmp(back + 16, eth + 6, 6) == 0 && back[31] == 0x8E);
}

static void test_eapol_event(void)
{
    const nb_u8 own[6] = { 2, 0, 0, 0, 0, 1 }, ap[6] = { 0xe8, 0x9f, 0x80, 0xeb, 0xba, 0x64 };
    nb_u8 raw[99], eth[120], out[200];
    nb_u32 n;
    int fmt;
    memset(raw, 0x5A, sizeof(raw));
    raw[0] = 2; raw[1] = 3; raw[2] = 0; raw[3] = 95;          /* EAPOL-Key, body 95 */
    n = nb_eapol_from_event(raw, sizeof(raw), own, ap, out, sizeof(out), &fmt);
    CHECK(n == 14 + 99 && fmt == 2 && memcmp(out, own, 6) == 0 && memcmp(out + 6, ap, 6) == 0);
    CHECK(out[12] == 0x88 && out[13] == 0x8E && memcmp(out + 14, raw, 99) == 0);
    memcpy(eth, own, 6); memcpy(eth + 6, ap, 6); eth[12] = 0x88; eth[13] = 0x8E;
    memcpy(eth + 14, raw, 99);
    n = nb_eapol_from_event(eth, 113, own, ap, out, sizeof(out), &fmt);
    CHECK(n == 113 && fmt == 1 && memcmp(out, eth, 113) == 0);
    CHECK(nb_eapol_from_event(raw, 50, own, ap, out, sizeof(out), &fmt) == 0);   /* truncated */
    raw[0] = 9;
    CHECK(nb_eapol_from_event(raw, sizeof(raw), own, ap, out, sizeof(out), &fmt) == 0 && fmt == 0);
}

static void test_eapol_kinds(void)
{
    nb_u8 f[120], w[200];
    const nb_u8 bss[6] = { 0xe8, 0x9f, 0x80, 0xeb, 0xba, 0x64 };
    static const nb_u16 ki[] = { 0x008a, 0x010a, 0x13ca, 0x030a, 0x1382, 0x0302 };
    static const int want[] = { 1, 2, 3, 4, NB_EAPOL_G1, NB_EAPOL_G2 };
    memset(f, 0, sizeof(f));
    f[12] = 0x88; f[13] = 0x8E; f[14] = 2; f[15] = 3; f[16] = 0; f[17] = 95; f[18] = 2;
    for (int i = 0; i < 6; i++) {
        f[19] = (nb_u8)(ki[i] >> 8); f[20] = (nb_u8)ki[i];
        CHECK(nb_eapol_key_msg(f, sizeof(f)) == want[i]);
    }
    f[15] = 0;
    CHECK(nb_eapol_key_msg(f, sizeof(f)) == NB_EAPOL_OTHER);
    f[13] = 0x00;
    CHECK(nb_eapol_key_msg(f, sizeof(f)) == -1);
    f[13] = 0x8E;
    CHECK(nb_8023_to_80211_p(f, 60, bss, 1, w, sizeof(w)) == 78 && w[1] == 0x42);
    CHECK(nb_8023_to_80211_p(f, 60, bss, 0, w, sizeof(w)) == 78 && w[1] == 0x02);
}

static void test_board(void)
{
    nb_u8 cal[2096], f[2200];
    nb_board b;
    nb_u32 i, n;
    const nb_u8 mac[6] = { 2, 0, 0, 0, 0, 1 };
    for (i = 0; i < sizeof(cal); i++) cal[i] = (nb_u8)(i * 7);
    CHECK(nb_crc32((const nb_u8 *)"123456789", 9) == 0xCBF43926u);
    n = nb_board_build(f, sizeof(f), mac, cal, sizeof(cal));
    CHECK(n == 28 + 2096);
    CHECK(nb_board_parse(f, n, &b) == NB_OK && b.have_mac && memcmp(b.mac, mac, 6) == 0 && b.cal_len == 2096);
    CHECK(memcmp(b.cal, cal, 2096) == 0);
    f[100] ^= 1;
    CHECK(nb_board_parse(f, n, &b) == NB_E_FORMAT);
    CHECK(nb_board_parse(f, n - 1, &b) == NB_E_FORMAT);
}

static void test_s258o(void)
{
    nb_flowwatch f;
    /* s258n scan0 numbers: 11 items posted during the scan, firmware r frozen at 320 */
    nb_flowwatch_start(&f, 1000, 331, 320);
    CHECK(f.pend == 1 && f.adv_ms == NB_FW_NONE);
    CHECK(nb_flowwatch_step(&f, 1010, 332, 320, 300, 1000) == NB_FW_WAIT);
    CHECK(nb_flowwatch_step(&f, 1299, 335, 320, 300, 1000) == NB_FW_WAIT);
    CHECK(nb_flowwatch_step(&f, 1300, 335, 320, 300, 1000) == NB_FW_STUCK);
    /* healthy: the firmware takes them 30 ms after the scan */
    nb_flowwatch_start(&f, 1000, 331, 320);
    CHECK(nb_flowwatch_step(&f, 1030, 331, 331, 300, 1000) == NB_FW_ALIVE && f.adv_ms == 30);
    /* partial take counts as alive */
    nb_flowwatch_start(&f, 0, 10, 4);
    CHECK(nb_flowwatch_step(&f, 20, 12, 5, 300, 1000) == NB_FW_ALIVE && f.adv_ms == 20);
    /* nothing pending at the end; first post at 500 ms, never taken -> stuck 300 ms after that post */
    nb_flowwatch_start(&f, 0, 7, 7);
    CHECK(f.pend == 0);
    CHECK(nb_flowwatch_step(&f, 400, 7, 7, 300, 1000) == NB_FW_WAIT);
    CHECK(nb_flowwatch_step(&f, 500, 8, 7, 300, 1000) == NB_FW_WAIT && f.pend == 1 && f.tpend == 500);
    CHECK(nb_flowwatch_step(&f, 799, 9, 7, 300, 1000) == NB_FW_WAIT);
    CHECK(nb_flowwatch_step(&f, 1200, 9, 7, 300, 1000) == NB_FW_STUCK);       /* idle limit does not apply once pending */
    /* posted and taken between two samples (w == r again, r moved): alive */
    nb_flowwatch_start(&f, 0, 7, 7);
    CHECK(nb_flowwatch_step(&f, 10, 9, 9, 300, 1000) == NB_FW_ALIVE);
    /* nothing at all for idle_ms */
    nb_flowwatch_start(&f, 0, 7, 7);
    CHECK(nb_flowwatch_step(&f, 999, 7, 7, 300, 1000) == NB_FW_WAIT);
    CHECK(nb_flowwatch_step(&f, 1000, 7, 7, 300, 1000) == NB_FW_IDLE);
    /* ring wrap (512 items): 510 -> 3 is a move */
    nb_flowwatch_start(&f, 0, 2, 510);
    CHECK(nb_flowwatch_step(&f, 5, 2, 3, 300, 1000) == NB_FW_ALIVE);
    /* 2 s tick use (deferred watch, probe 1500): pending seen at tick 1, unchanged at tick 2 */
    nb_flowwatch_start(&f, 0, 50, 50);
    CHECK(nb_flowwatch_step(&f, 2000, 52, 50, 1500, 30000) == NB_FW_WAIT);
    CHECK(nb_flowwatch_step(&f, 4000, 53, 50, 1500, 30000) == NB_FW_STUCK);
    nb_flowwatch_start(&f, 0, 50, 50);
    CHECK(nb_flowwatch_step(&f, 2000, 52, 50, 1500, 30000) == NB_FW_WAIT);
    CHECK(nb_flowwatch_step(&f, 4000, 53, 53, 1500, 30000) == NB_FW_ALIVE && f.adv_ms == 4000);
    CHECK(nb_flowwatch_step(NULL, 0, 0, 0, 1, 1) == NB_FW_WAIT);
}

static void test_s258p(void)
{
    nb_u8 p[256], q[256];
    nb_u8 it[48];
    nb_u16 cl[NB_SCAN_CHLIST_MAX];
    const nb_u32 valid[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 100, 149, 165 };
    nb_u32 n, m, k;
    /* chanspecs (brcmu_d11.h): d11ac 2G ch1 0x1001, 5G ch36 0xd024; d11n 2G ch1 0x2b01, 5G ch36 0x1b24 */
    CHECK(nb_chanspec20(1, NB_IO_TYPE_D11AC) == 0x1001);
    CHECK(nb_chanspec20(36, NB_IO_TYPE_D11AC) == 0xD024);
    CHECK(nb_chanspec20(165, NB_IO_TYPE_D11AC) == 0xD0A5);
    CHECK(nb_chanspec20(1, NB_IO_TYPE_D11N) == 0x2B01);
    CHECK(nb_chanspec20(36, NB_IO_TYPE_D11N) == 0x1B24);
    {   /* decode round trip */
        nb_u8 c = 0, b = 0; nb_u16 w = 0;
        nb_chanspec_decode(nb_chanspec20(149, NB_IO_TYPE_D11AC), NB_IO_TYPE_D11AC, &c, &b, &w);
        CHECK(c == 149 && b == 5 && w == 20);
        nb_chanspec_decode(nb_chanspec20(6, NB_IO_TYPE_D11AC), NB_IO_TYPE_D11AC, &c, &b, &w);
        CHECK(c == 6 && b == 2 && w == 20);
    }
    /* channel lists */
    CHECK(nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, 0, 36, valid, 21, NB_IO_TYPE_D11AC) == 0);
    CHECK(nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, 1, 0, valid, 21, NB_IO_TYPE_D11AC) == 0);
    CHECK(nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, 2, 36, valid, 21, NB_IO_TYPE_D11AC) == 1 && cl[0] == 0xD024);
    n = nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, 1, 36, valid, 21, NB_IO_TYPE_D11AC);
    CHECK(n == 8 && cl[0] == 0xD024 && cl[4] == 0xD034 && cl[7] == 0xD0A5);
    n = nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, 1, 6, valid, 21, NB_IO_TYPE_D11AC);
    CHECK(n == 13 && cl[0] == 0x1001 && cl[12] == 0x100D);
    n = nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, 1, 36, NULL, 0, NB_IO_TYPE_D11AC);   /* fixed 5 GHz list */
    CHECK(n == 9 && cl[0] == 0xD024 && cl[8] == 0xD0A5);
    n = nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, 1, 11, NULL, 0, NB_IO_TYPE_D11AC);
    CHECK(n == 11 && cl[10] == 0x100B);
    n = nb_scan_chanlist(cl, 3, 1, 6, valid, 21, NB_IO_TYPE_D11AC);                  /* cap respected */
    CHECK(n == 3);
    /* escan with no list == the old encoding (byte for byte, v1 and v2) */
    n = nb_escan_params(p, sizeof(p), 0, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, (const nb_u8 *)"home", 4);
    m = nb_escan_params_ch(q, sizeof(q), 0, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, (const nb_u8 *)"home", 4, NULL, 0);
    CHECK(n == 72 && m == 72 && memcmp(p, q, n) == 0);
    n = nb_escan_params(p, sizeof(p), 2, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, NULL, 0);
    m = nb_escan_params_ch(q, sizeof(q), 2, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, NULL, 0, NULL, 0);
    CHECK(n == 80 && m == 80 && memcmp(p, q, n) == 0 && nb_ld16(q + 10) == 80);
    /* v1 with 3 channels: channel_num 3 @ 8+60, list @ 8+64, rounded to 4 bytes */
    cl[0] = 0xD024; cl[1] = 0xD028; cl[2] = 0xD02C;
    m = nb_escan_params_ch(q, sizeof(q), 1, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, (const nb_u8 *)"home", 4, cl, 3);
    CHECK(m == 8 + 64 + 8 && nb_ld32(q + 8 + 60) == 3 && nb_ld16(q + 8 + 64) == 0xD024 &&
          nb_ld16(q + 8 + 68) == 0xD02C && nb_ld16(q + 8 + 70) == 0);
    CHECK(nb_ld32(q + 8) == 4 && memcmp(q + 12, "home", 4) == 0);
    n = nb_escan_params(p, sizeof(p), 1, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, (const nb_u8 *)"home", 4);
    CHECK(n == 72 && memcmp(p, q, 8 + 60) == 0);   /* fixed part identical to the no-list request */
    /* v2 with 1 channel: length 72 + 4 @ p+2, channel_num @ 8+68, list @ 8+72 */
    m = nb_escan_params_ch(q, sizeof(q), 2, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, NULL, 0, cl, 1);
    CHECK(m == 8 + 72 + 4 && nb_ld16(q + 10) == 76 && nb_ld32(q + 8 + 68) == 1 && nb_ld16(q + 8 + 72) == 0xD024);
    CHECK(nb_escan_params_ch(q, 8 + 64 + 4, 1, 0, 1, NULL, 0, cl, 3) == 0);   /* does not fit */
    CHECK(nb_escan_params_ch(q, sizeof(q), 1, 0, 1, NULL, 0, NULL, 3) == 0);
    CHECK(nb_escan_params_ch(q, sizeof(q), 1, 0, 1, NULL, 0, cl, NB_SCAN_CHLIST_MAX + 1) == 0);
    /* flowring resume request: type 0x15, ifidx 0, request id 0, ring id 2 @ 8, reason 0 */
    CHECK(nb_enc_flowring_resume(it, 40, 0, 2) == 40 && it[0] == NB_MSG_FLOW_RING_RESUME && it[1] == 0 &&
          nb_ld32(it + 4) == 0 && nb_ld16(it + 8) == 2 && nb_ld16(it + 10) == 0 && it[12] == 0 && it[39] == 0);
    CHECK(nb_enc_flowring_resume(it, 32, 0, 2) == 0);
    /* candidate table */
    CHECK(nb_cand_bits(0) == 0 && nb_cand_bits(1) == NB_CAND_PM2 && nb_cand_bits(2) == NB_CAND_CH2 &&
          nb_cand_bits(3) == NB_CAND_CH1 && nb_cand_bits(4) == NB_CAND_RSM &&
          nb_cand_bits(5) == (NB_CAND_PM2 | NB_CAND_CH1) && nb_cand_bits(6) == NB_CAND_IOV &&
          nb_cand_bits(7) == (NB_CAND_PM2 | NB_CAND_CH1 | NB_CAND_IOV) && nb_cand_bits(8) == 0);
    CHECK(strcmp(nb_cand_name(0), "base") == 0 && strcmp(nb_cand_name(7), "pm2+ch1+iov") == 0 &&
          strcmp(nb_cand_name(9), "pm2") == 0);
    for (k = 0; k < NB_CAND_N; k++)                 /* CH1 and CH2 never together */
        CHECK((nb_cand_bits(k) & (NB_CAND_CH1 | NB_CAND_CH2)) != (NB_CAND_CH1 | NB_CAND_CH2));
}

static void test_s258n(void)
{
    char o[256];
    nb_stall st = { 0, 0, 0 };
    const nb_u8 ss[] = { 'h', 'o', 'm', 'e', 'n', 'e', 't' };
    /* stall decision */
    CHECK(!nb_tx_stalled(2, 20000, 20000, 8000));             /* fewer than 3 outstanding */
    CHECK(!nb_tx_stalled(10, 20000, 5000, 8000));              /* a status came 5 s ago */
    CHECK(!nb_tx_stalled(10, 7000, 20000, 8000));              /* oldest is young */
    CHECK(nb_tx_stalled(10, 8000, 8000, 8000));
    CHECK(!nb_tx_stalled(10, 20000, 20000, 0));                /* detector off */
    /* state machine: detect -> PM kick works */
    CHECK(nb_stall_step(&st, 1000, 0, 0, 5, 2) == NB_SA_NONE && st.state == 0);
    CHECK(nb_stall_step(&st, 3000, 1, 9, 5, 2) == NB_SA_DETECT && st.state == 1);
    nb_stall_mark(&st, 5, 3000);
    CHECK(nb_stall_step(&st, 5000, 1, 9, 5, 2) == NB_SA_NONE);
    CHECK(nb_stall_step(&st, 6000, 0, 2, 12, 2) == NB_SA_OK_PM && st.state == 0);
    /* detect -> no progress -> flowring -> progress */
    CHECK(nb_stall_step(&st, 10000, 1, 9, 12, 2) == NB_SA_DETECT);
    nb_stall_mark(&st, 12, 10000);
    CHECK(nb_stall_step(&st, 12000, 1, 11, 12, 2) == NB_SA_NONE);
    CHECK(nb_stall_step(&st, 14000, 1, 13, 12, 2) == NB_SA_FLOW && st.state == 2);
    nb_stall_mark(&st, 25, 14500);                             /* flush statuses counted before the mark */
    CHECK(nb_stall_step(&st, 16000, 0, 1, 25, 2) == NB_SA_NONE);
    CHECK(nb_stall_step(&st, 18000, 0, 0, 26, 2) == NB_SA_OK_FLOW && st.state == 0);
    /* detect -> flowring -> still nothing -> link loss (recover 2) / give up (recover 1) */
    CHECK(nb_stall_step(&st, 30000, 1, 9, 26, 2) == NB_SA_DETECT); nb_stall_mark(&st, 26, 30000);
    CHECK(nb_stall_step(&st, 34000, 1, 9, 26, 2) == NB_SA_FLOW);   nb_stall_mark(&st, 40, 34000);
    CHECK(nb_stall_step(&st, 38000, 0, 4, 40, 2) == NB_SA_NONE);
    CHECK(nb_stall_step(&st, 40000, 0, 4, 40, 2) == NB_SA_LINKLOSS && st.state == 0);
    CHECK(nb_stall_step(&st, 50000, 1, 9, 40, 1) == NB_SA_DETECT); nb_stall_mark(&st, 40, 50000);
    CHECK(nb_stall_step(&st, 54000, 1, 9, 40, 1) == NB_SA_FLOW);   nb_stall_mark(&st, 41, 54000);
    CHECK(nb_stall_step(&st, 60000, 0, 4, 41, 1) == NB_SA_GIVEUP);
    CHECK(nb_stall_step(&st, 70000, 1, 9, 41, 0) == NB_SA_DETECT); nb_stall_mark(&st, 41, 70000);
    CHECK(nb_stall_step(&st, 74000, 1, 9, 41, 0) == NB_SA_GIVEUP && st.state == 0);
    /* nothing outstanding after the re-create: wait, then idle */
    CHECK(nb_stall_step(&st, 80000, 1, 9, 41, 2) == NB_SA_DETECT); nb_stall_mark(&st, 41, 80000);
    CHECK(nb_stall_step(&st, 84000, 1, 9, 41, 2) == NB_SA_FLOW);   nb_stall_mark(&st, 50, 84000);
    CHECK(nb_stall_step(&st, 100000, 0, 0, 50, 2) == NB_SA_NONE);
    CHECK(nb_stall_step(&st, 114000, 0, 0, 50, 2) == NB_SA_IDLE);
    /* masking */
    nb_mask_text("peer aa:BB:cc:01:02:03 ok", 25, NULL, 0, o, sizeof(o));
    CHECK(strcmp(o, "peer <m> ok") == 0);
    nb_mask_text("Peer 0xA1B2C3D4E5F6 self 0x0011223344aa", 39, NULL, 0, o, sizeof(o));
    CHECK(strcmp(o, "Peer <m> self <m>") == 0);
    nb_mask_text("bss a1b2c3d4e5f6, 11-22-33-44-55-66", 35, NULL, 0, o, sizeof(o));
    CHECK(strcmp(o, "bss <m>, <m>") == 0);
    nb_mask_text("t 0x1234 x123456789abc 1234567890abcd", 37, NULL, 0, o, sizeof(o));
    CHECK(strcmp(o, "t 0x1234 x123456789abc 1234567890abcd") == 0);   /* 4 hex / glued / 14 hex: kept */
    nb_mask_text("join homenet ch 36\r\n\x01", 21, ss, sizeof(ss), o, sizeof(o));
    CHECK(strcmp(o, "join <s> ch 36 \n?") == 0);
    CHECK(nb_mask_text("abcdef", 6, NULL, 0, o, 4) == 3 && strcmp(o, "abc") == 0);
}

static void test_s258q(void)
{
    nb_u8 p[512], q[512];
    nb_u16 cl[NB_SCAN_CHLIST_MAX];
    const nb_u32 valid[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 100, 149, 165 };
    const nb_u32 v36[] = { 1, 6, 11, 36, 40 };
    nb_escan_req r;
    nb_cca_ev ev;
    nb_u32 n, m;
    /* ex with defaults == _ch (v1, v2, with and without a list) */
    memset(&r, 0, sizeof(r));
    r.scan_ver = 1; r.scan_type = NB_SCANTYPE_ACTIVE; r.sync_id = NB_ESCAN_SYNC_ID;
    r.ssid = (const nb_u8 *)"home"; r.ssid_len = 4;
    r.nprobes = r.active_ms = r.passive_ms = r.home_ms = -1;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    m = nb_escan_params_ch(q, sizeof(q), 1, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, (const nb_u8 *)"home", 4, NULL, 0);
    CHECK(n == 72 && m == 72 && memcmp(p, q, n) == 0);
    cl[0] = 0xD095;
    r.chs = cl; r.nch = 1;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    m = nb_escan_params_ch(q, sizeof(q), 1, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, (const nb_u8 *)"home", 4, cl, 1);
    CHECK(n == 76 && m == 76 && memcmp(p, q, n) == 0);
    r.scan_ver = 2;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    m = nb_escan_params_ch(q, sizeof(q), 2, NB_SCANTYPE_ACTIVE, NB_ESCAN_SYNC_ID, (const nb_u8 *)"home", 4, cl, 1);
    CHECK(n == 84 && m == 84 && memcmp(p, q, n) == 0);
    /* shape 2: home channel, active dwell 3000 ms @ v1 p+48, others -1 */
    r.scan_ver = 1; r.active_ms = 3000;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    CHECK(n == 76 && nb_ld32(p + 8 + 44) == 0xFFFFFFFFu && nb_ld32(p + 8 + 48) == 3000 &&
          nb_ld32(p + 8 + 52) == 0xFFFFFFFFu && nb_ld32(p + 8 + 56) == 0xFFFFFFFFu && nb_ld32(p + 8 + 60) == 1);
    r.scan_ver = 2;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    CHECK(n == 84 && nb_ld32(p + 8 + 56) == 3000 && nb_ld32(p + 8 + 52) == 0xFFFFFFFFu);
    /* shape 3 (brcmf wildcard form): ssid_le empty, n_ssids 1 in channel_num, one empty brcmf_ssid_le after
     * the (empty) list: v1 total 8 + 64 + 36, v2 length 72 + 36 */
    r.scan_ver = 1; r.active_ms = -1; r.ssid = NULL; r.ssid_len = 0; r.chs = NULL; r.nch = 0;
    r.n_list_ssids = 1; r.list_ssid = NULL; r.list_ssid_len = 0;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    CHECK(n == 8 + 64 + 36 && nb_ld32(p + 8) == 0 && nb_ld32(p + 8 + 60) == 0x10000 && nb_ld32(p + 8 + 64) == 0 &&
          p[8 + 43] == NB_SCANTYPE_ACTIVE);
    r.scan_ver = 2;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    CHECK(n == 8 + 72 + 36 && nb_ld16(p + 10) == 72 + 36 && nb_ld32(p + 8 + 68) == 0x10000);
    /* list SSID after 3 channels: rounded list 8 bytes, entry len 4 + "home" */
    r.scan_ver = 1; cl[0] = 0xD024; cl[1] = 0xD028; cl[2] = 0xD02C; r.chs = cl; r.nch = 3;
    r.list_ssid = (const nb_u8 *)"home"; r.list_ssid_len = 4;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    CHECK(n == 8 + 64 + 8 + 36 && nb_ld32(p + 8 + 60) == 0x10003 && nb_ld16(p + 8 + 68) == 0xD02C &&
          nb_ld32(p + 8 + 72) == 4 && memcmp(p + 8 + 76, "home", 4) == 0);
    CHECK(nb_escan_params_ex(p, 8 + 64 + 8 + 35, &r) == 0);          /* does not fit */
    r.n_list_ssids = 2;
    CHECK(nb_escan_params_ex(p, sizeof(p), &r) == 0);
    r.n_list_ssids = 1; r.list_ssid_len = 33;
    CHECK(nb_escan_params_ex(p, sizeof(p), &r) == 0);
    /* shape 4: passive type byte */
    memset(&r, 0, sizeof(r));
    r.scan_ver = 1; r.scan_type = NB_SCANTYPE_PASSIVE; r.sync_id = NB_ESCAN_SYNC_ID;
    r.nprobes = r.active_ms = r.passive_ms = r.home_ms = -1;
    n = nb_escan_params_ex(p, sizeof(p), &r);
    CHECK(n == 72 && p[8 + 43] == NB_SCANTYPE_PASSIVE && nb_ld32(p + 8) == 0 && nb_ld32(p + 8 + 60) == 0);
    /* foreign channel */
    CHECK(nb_shape_foreign_ch(0, valid, 21) == 0);
    CHECK(nb_shape_foreign_ch(36, valid, 21) == 149);
    CHECK(nb_shape_foreign_ch(149, valid, 21) == 36);
    CHECK(nb_shape_foreign_ch(44, NULL, 0) == 149);
    CHECK(nb_shape_foreign_ch(36, v36, 5) == 40);                     /* 149.. not valid: same band, not home */
    CHECK(nb_shape_foreign_ch(6, valid, 21) == 11);
    CHECK(nb_shape_foreign_ch(11, valid, 21) == 1);
    CHECK(nb_shape_foreign_ch(1, valid, 21) == 11);
    /* all channels */
    n = nb_shape_all_channels(cl, NB_SCAN_CHLIST_MAX, valid, 21, NB_IO_TYPE_D11AC);
    CHECK(n == 21 && cl[0] == 0x1001 && cl[13] == 0xD024 && cl[20] == 0xD0A5);
    n = nb_shape_all_channels(cl, NB_SCAN_CHLIST_MAX, NULL, 0, NB_IO_TYPE_D11AC);
    CHECK(n == 20 && cl[0] == 0x1001 && cl[11] == 0xD024 && cl[19] == 0xD0A5);
    CHECK(nb_shape_all_channels(cl, 4, valid, 21, NB_IO_TYPE_D11AC) == 4);
    /* names */
    CHECK(strcmp(nb_shape_name(0), "base") == 0 && strcmp(nb_shape_name(5), "chunks") == 0 &&
          strcmp(nb_shape_name(6), "win") == 0 && strcmp(nb_shape_name(200), "win") == 0);
    /* event 124 (cca_chan_qual_event_t): CCA on 5 GHz ch 36 */
    {
        nb_u8 d[20];
        nb_st16(d, 0); nb_st16(d + 2, 0); nb_st16(d + 4, 0xD024); nb_st16(d + 6, 12);
        nb_st32(d + 8, 100); nb_st32(d + 12, 37); nb_st32(d + 16, 5000);
        CHECK(nb_cca_ev_parse(d, 20, &ev) == 1 && ev.id == 0 && ev.chanspec == 0xD024 && ev.len == 12 &&
              ev.has_cca && ev.duration == 100 && ev.congest == 37 && ev.timestamp == 5000 && !ev.has_noise);
        nb_st16(d + 2, 1); nb_st32(d + 8, (nb_u32)-92);
        CHECK(nb_cca_ev_parse(d, 12, &ev) == 1 && ev.has_noise && ev.noise == -92 && !ev.has_cca);
        CHECK(nb_cca_ev_parse(d, 8, &ev) == 1 && !ev.has_noise && !ev.has_cca && ev.chanspec == 0xD024);
        CHECK(nb_cca_ev_parse(d, 7, &ev) < 0);
    }
}

int main(int argc, char **argv)
{
    test_erom();
    test_otp();
    test_nvram_small();
    if (argc >= 3)
        test_nvram_real(argv[1], argv[2]);
    else
        fprintf(stderr, "skip real NVRAM test (no files given)\n");
    test_layout();
    test_shared();
    test_ring();
    test_msgs();
    test_blobs();
    test_escan();
    test_chanspec();
    test_connect();
    test_frames();
    test_board();
    test_eapol_event();
    test_eapol_kinds();
    test_s258n();
    test_s258o();
    test_s258p();
    test_s258q();
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
