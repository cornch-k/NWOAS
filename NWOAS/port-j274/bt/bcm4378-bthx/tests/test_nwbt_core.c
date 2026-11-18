/* Host unit tests for src/core/nwbt_core.c (run by tests/run-tests.sh with ASan/UBSan).
 * A pass here is a logic check of the core only: no chip, no Windows. */
#include "../src/core/nwbt_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t g32(const uint8_t *p) { return (uint32_t)g16(p) | (uint32_t)g16(p + 2) << 16; }
static uint64_t g64(const uint8_t *p) { return (uint64_t)g32(p) | (uint64_t)g32(p + 4) << 32; }

static void test_layout(void)
{
    nbt_layout L;
    uint32_t i, j;
    struct { uint32_t off, len; } reg[32];
    uint32_t n = 0;
    CHECK(nbt_layout_init(&L) == NBT_OK);
    CHECK(L.x[NBT_XR_CONTROL].payload_size == 0x34);
    CHECK(L.x[NBT_XR_HCI_H2D].payload_size == 264);
    CHECK(L.x[NBT_XR_SCO_H2D].payload_size == 260 && L.x[NBT_XR_SCO_H2D].doorbell == NBT_DB_SCO);
    CHECK(L.x[NBT_XR_ACL_H2D].mapped_size == 1032 && L.x[NBT_XR_ACL_H2D].payload_size == 0);
    CHECK(L.x[NBT_XR_ACL_D2H].flags == NBT_RF_D2H_BUFS);
    CHECK(L.x[NBT_XR_HCI_D2H].flags == NBT_RF_VIRTUAL && L.x[NBT_XR_HCI_D2H].ring_off == 0);
    CHECK(L.c[NBT_CR_CONTROL_ACK].n_entries == 32);
    CHECK(L.c[NBT_CR_HCI_ACL_EVENT].n_entries == 256 && L.c[NBT_CR_HCI_ACL_EVENT].payload_size == 264);
    CHECK(L.c[NBT_CR_HCI_ACL_ACK].xfer_mask == ((1u << 1) | (1u << 5)));
    CHECK(L.size % 4096 == 0 && L.size < 0x80000);
    reg[n].off = L.ctx_off; reg[n++].len = NBT_CONTEXT_SIZE;
    reg[n].off = L.state_off; reg[n++].len = NBT_RS_SIZE;
    reg[n].off = L.periph_off; reg[n++].len = NBT_PERIPH_INFO_SIZE;
    for (i = 0; i < NBT_N_XR_USED; i++) {
        if (!(L.x[i].flags & NBT_RF_VIRTUAL)) {
            reg[n].off = L.x[i].ring_off; reg[n++].len = L.x[i].n_entries * (16u + L.x[i].payload_size);
        }
        if (L.x[i].mapped_size) {
            reg[n].off = L.x[i].payload_off; reg[n++].len = L.x[i].n_entries * (uint32_t)L.x[i].mapped_size;
        }
    }
    for (i = 0; i < NBT_N_CR_USED; i++) {
        reg[n].off = L.c[i].ring_off; reg[n++].len = L.c[i].n_entries * (16u + L.c[i].payload_size);
    }
    for (i = 0; i < n; i++) {
        CHECK(reg[i].off % 4096 == 0);
        CHECK(reg[i].off + reg[i].len <= L.size);
        for (j = i + 1; j < n; j++)
            CHECK(reg[i].off + reg[i].len <= reg[j].off || reg[j].off + reg[j].len <= reg[i].off);
    }
    CHECK(NBT_RS_SIZE == 60 && NBT_RS_XTAIL(0) == 42 && NBT_RS_XHEAD(0) == 24 && NBT_RS_CTAIL(0) == 12);
    printf("layout: %u regions, DMA block %u bytes\n", n, L.size);
}

static void test_context_and_msgs(void)
{
    nbt_layout L;
    uint8_t c[NBT_CONTEXT_SIZE], m[NBT_CONTROL_MSG_SIZE];
    const uint64_t base = 0x80000000ull;
    nbt_layout_init(&L);
    nbt_context_build(c, &L, base);
    CHECK(g16(c) == 1 && g16(c + 2) == 0x68 && g32(c + 4) == 2);
    CHECK(g64(c + 8) == base + L.periph_off);
    CHECK(g64(c + 16) == base + L.state_off + 0);
    CHECK(g64(c + 24) == base + L.state_off + 42);
    CHECK(g64(c + 32) == base + L.state_off + 12);
    CHECK(g64(c + 40) == base + L.state_off + 24);
    CHECK(g16(c + 48) == 6 && g16(c + 50) == 9);
    CHECK(g64(c + 52) == base + L.c[0].ring_off && g64(c + 60) == base + L.x[0].ring_off);
    CHECK(g16(c + 68) == 128 && g16(c + 70) == 32 && g16(c + 72) == 0 && g16(c + 74) == 0xffff);
    CHECK(c[80] == 0 && c[81] == 13 && c[82] == 0 && c[83] == 0 && g64(c + 88) == 0);

    nbt_msg_create_cring(m, &L.c[NBT_CR_HCI_ACL_EVENT], base);
    CHECK(m[0] == 2 && m[2] == 66 && g16(m + 4) == 2 && g16(m + 6) == 2);
    CHECK(g64(m + 8) == base + L.c[2].ring_off && g16(m + 16) == 256);
    CHECK(g32(m + 18) == 0xffffffffu && g16(m + 28) == 0 && g16(m + 30) == 1000 && g32(m + 32) == 0xffffffffu);
    nbt_msg_create_xring(m, &L.x[NBT_XR_SCO_D2H], base);
    CHECK(m[0] == 1 && g16(m + 4) == 4 && g64(m + 8) == 0 && g16(m + 24) == 128);
    CHECK(g16(m + 26) == NBT_CR_SCO_EVENT && g16(m + 28) == NBT_DB_SCO && g16(m + 30) == 0x180);
    nbt_msg_create_xring(m, &L.x[NBT_XR_HCI_H2D], base);
    CHECK(m[2] == 66 && g64(m + 8) == base + L.x[1].ring_off && g16(m + 30) == 0 && g16(m + 26) == 1);
    nbt_msg_destroy_xring(m, 5);
    CHECK(m[0] == 3 && g16(m + 2) == 5 && m[4] == 0);
    nbt_msg_destroy_cring(m, 4);
    CHECK(m[0] == 4 && g16(m + 2) == 4);
    CHECK(nbt_doorbell_value(NBT_DB_ACL_D2H, 0xf) == 0x000f0420u);
}

static void test_xring(void)
{
    nbt_layout L;
    uint8_t *blk;
    uint8_t *st;
    uint8_t data[1100];
    uint16_t db = 0;
    int id, i;
    const uint64_t base = 0x40000000ull;
    nbt_xring *h, *a;
    nbt_layout_init(&L);
    blk = calloc(1, L.size);
    st = blk + L.state_off;
    h = &L.x[NBT_XR_HCI_H2D];
    a = &L.x[NBT_XR_ACL_H2D];
    for (i = 0; i < (int)sizeof(data); i++)
        data[i] = (uint8_t)(i * 3);
    nbt_xring_reset(h, st);
    CHECK(h->generation == 1);
    id = nbt_xring_put(h, blk, base, st, data, 7, &db);
    CHECK(id == 0 && db == 1 && nbt_rs_get(st, NBT_RS_XHEAD(1)) == 1);
    {
        uint8_t *e = blk + h->ring_off;
        CHECK(e[0] == NBT_XF_IN_FOOTER && g16(e + 1) == 7 && g16(e + 12) == 0x0100 && g64(e + 4) == 0);
        CHECK(!memcmp(e + 16, data, 7));
    }
    CHECK(nbt_xring_put(h, blk, base, st, data, 265, &db) == NBT_ERR_TOOBIG);
    /* ACL: mapped slot */
    nbt_xring_reset(a, st);
    id = nbt_xring_put(a, blk, base, st, data, 1032, &db);
    CHECK(id == 0);
    {
        uint8_t *e = blk + a->ring_off;
        CHECK(e[0] == NBT_XF_MAPPED && g16(e + 1) == 1032 && g64(e + 4) == base + a->payload_off);
        CHECK(!memcmp(blk + a->payload_off, data, 1032));
    }
    CHECK(nbt_xring_put(a, blk, base, st, data, 1033, &db) == NBT_ERR_TOOBIG);
    /* acks: generation and id checks */
    CHECK(nbt_xring_ack(a, 0x0000) == NBT_ERR_GEN);
    CHECK(nbt_xring_ack(a, 0x0105) == NBT_ERR_MSGID);
    CHECK(nbt_xring_ack(a, 0x0100) == 0 && nbt_xring_inflight(a) == 0);
    CHECK(nbt_xring_ack(a, 0x0100) == NBT_ERR_MSGID);
    /* ring full: head + 1 == tail */
    nbt_xring_reset(h, st);
    for (i = 0; i < 127; i++)
        CHECK(nbt_xring_put(h, blk, base, st, data, 4, &db) == i);
    CHECK(nbt_xring_put(h, blk, base, st, data, 4, &db) == NBT_ERR_FULL);
    nbt_rs_set(st, NBT_RS_XTAIL(1), 10);                   /* device consumed 10 */
    CHECK(nbt_xring_put(h, blk, base, st, data, 4, &db) == 127 && db == 0);   /* head wraps to 0 */
    CHECK(nbt_xring_put(h, blk, base, st, data, 4, &db) == NBT_ERR_FULL);      /* no msgid left */
    CHECK(nbt_xring_ack(h, (uint16_t)(0x0200 | 3)) == 3);
    CHECK(nbt_xring_put(h, blk, base, st, data, 4, &db) == 3);                 /* lowest free id again */
    CHECK(nbt_xring_put(&L.x[NBT_XR_HCI_D2H], blk, base, st, data, 4, &db) == NBT_ERR_ARG);  /* virtual */
    nbt_rs_set(st, NBT_RS_XHEAD(1), 200);
    CHECK(nbt_xring_put(h, blk, base, st, data, 4, &db) == NBT_ERR_RING);
    free(blk);
}

static void test_d2h_and_completion(void)
{
    nbt_layout L;
    uint8_t *blk, *st, *e;
    nbt_centry ce;
    uint32_t off = 0, len = 0;
    const uint64_t base = 0x40000000ull;
    nbt_xring *ad;
    nbt_cring *ev;
    nbt_layout_init(&L);
    blk = calloc(1, L.size);
    st = blk + L.state_off;
    ad = &L.x[NBT_XR_ACL_D2H];
    ev = &L.c[NBT_CR_HCI_ACL_EVENT];
    nbt_xring_reset(ad, st);
    nbt_xring_fill_d2h(ad, blk, base);
    e = blk + ad->ring_off + 5 * 16;
    CHECK(e[0] == NBT_XF_MAPPED && g16(e + 1) == 1032 && g16(e + 12) == 0x0105);
    CHECK(g64(e + 4) == base + ad->payload_off + 5 * 1032);
    nbt_rs_set(st, NBT_RS_XHEAD(6), NBT_D2H_POSTED);
    CHECK(nbt_xring_advance(ad, st) == 0x10);
    nbt_rs_set(st, NBT_RS_XHEAD(6), 127);
    CHECK(nbt_xring_advance(ad, st) == 0);
    /* completion entry 3: mapped ACL in slot 9 */
    e = blk + ev->ring_off + 3 * (16 + 264);
    e[0] = NBT_XF_MAPPED; e[2] = 6; e[4] = 9; e[5] = 1; e[6] = 0x40; e[7] = 0x02;   /* len 0x240 */
    CHECK(nbt_cring_entry(ev, blk, 3, &ce) == NBT_OK);
    CHECK(ce.xring == 6 && ce.msgid == 0x0109 && ce.len == 0x240 && ce.data_off == ev->ring_off + 3 * 280 + 16);
    CHECK(nbt_d2h_payload(ad, ev, &ce, &off, &len) == NBT_OK && off == ad->payload_off + 9 * 1032 && len == 0x240);
    ce.msgid = 0x0209;
    CHECK(nbt_d2h_payload(ad, ev, &ce, &off, &len) == NBT_ERR_GEN);
    ce.msgid = 0x0109; ce.len = 1033;
    CHECK(nbt_d2h_payload(ad, ev, &ce, &off, &len) == NBT_ERR_TOOBIG);
    /* in-place HCI event */
    e = blk + ev->ring_off + 4 * 280;
    memset(e, 0, 16); e[2] = 2; e[6] = 6;
    CHECK(nbt_cring_entry(ev, blk, 4, &ce) == NBT_OK);
    CHECK(nbt_d2h_payload(&L.x[NBT_XR_HCI_D2H], ev, &ce, &off, &len) == NBT_OK && off == ce.data_off && len == 6);
    ce.len = 265;
    CHECK(nbt_d2h_payload(&L.x[NBT_XR_HCI_D2H], ev, &ce, &off, &len) == NBT_ERR_TOOBIG);
    /* transfer ring that does not belong to this completion ring */
    e[2] = 1;
    CHECK(nbt_cring_entry(ev, blk, 4, &ce) == NBT_ERR_UNKNOWN);
    CHECK(nbt_cring_entry(ev, blk, 256, &ce) == NBT_ERR_ARG);
    free(blk);
}

static void test_hci(void)
{
    uint8_t b[300], p[NBT_CAL_PARAM_LEN], q[NBT_PTB_PARAM_LEN], blob[5664];
    uint16_t op = 0;
    uint8_t st = 0xff;
    uint32_t ro = 0, rl = 0, i;
    CHECK(nbt_hci_cmd(b, sizeof(b), NBT_OP_RESET, NULL, 0) == 3 && b[0] == 0x03 && b[1] == 0x0c && b[2] == 0);
    CHECK(nbt_hci_cmd(b, 4, NBT_OP_RESET, b, 2) == 0);
    {
        const uint8_t cc[] = { 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
        const uint8_t cs[] = { 0x0f, 0x04, 0x0c, 0x01, 0x01, 0x10 };
        const uint8_t bad[] = { 0x0e, 0x09, 0x01 };
        const uint8_t other[] = { 0x05, 0x04, 0, 0, 0, 0 };
        CHECK(nbt_hci_cmd_done(cc, 6, &op, &st, &ro, &rl) == 1 && op == 0x0c03 && st == 0 && ro == 5 && rl == 1);
        CHECK(nbt_hci_cmd_done(cs, 6, &op, &st, &ro, &rl) == 2 && op == 0x1001 && st == 0x0c);
        CHECK(nbt_hci_cmd_done(bad, 3, &op, &st, &ro, &rl) < 0);
        CHECK(nbt_hci_cmd_done(other, 6, &op, &st, &ro, &rl) == 0);
    }
    for (i = 0; i < sizeof(blob); i++)
        blob[i] = (uint8_t)(i ^ 0x5a);
    CHECK(nbt_chunks(5664, NBT_CAL_CHUNK) == 25 && NBT_CAL_PARAM_LEN == 0xe9 && NBT_PTB_PARAM_LEN == 0xd1);
    CHECK(nbt_cal_chunk(blob, 5664, 0, p) == NBT_OK && p[0] == 3 && g16(p + 1) == 24 && !memcmp(p + 3, blob, 0xe6));
    CHECK(nbt_cal_chunk(blob, 5664, 24, p) == NBT_OK && g16(p + 1) == 0);
    CHECK(!memcmp(p + 3, blob + 24 * 0xe6, 5664 - 24 * 0xe6) && p[3 + 5664 - 24 * 0xe6] == 0 && p[0xe8] == 0);
    CHECK(nbt_cal_chunk(blob, 5664, 25, p) == NBT_ERR_ARG);
    CHECK(nbt_chunks(786, NBT_PTB_CHUNK) == 4);
    CHECK(nbt_ptb_chunk(blob, 786, 3, q) == NBT_OK && g16(q) == 0 && !memcmp(q + 2, blob + 3 * 0xcf, 786 - 3 * 0xcf));
    CHECK(nbt_ptb_chunk(blob, 786, 0, q) == NBT_OK && g16(q) == 3);
    {
        const uint8_t def[6] = { 0x93, 0x76, 0x00, 0x12, 0x78, 0x43 }, real[6] = { 1, 2, 3, 4, 5, 6 };
        uint8_t le[6];
        CHECK(nbt_bdaddr_is_default(def, 0x4378) && !nbt_bdaddr_is_default(real, 0x4378));
        CHECK(!nbt_bdaddr_is_default(def, 0x4377));
        nbt_bdaddr_from_adt(real, le);
        CHECK(le[0] == 6 && le[5] == 1);
    }
}

static void test_fix(void)
{
    uint8_t ev[80];
    /* Read Local Supported Commands complete: status + 64 octets */
    memset(ev, 0xff, sizeof(ev));
    ev[0] = 0x0e; ev[1] = 68; ev[2] = 1; ev[3] = 0x02; ev[4] = 0x10; ev[5] = 0;
    CHECK(nbt_fix_event(ev, 70, NBT_FIX_MWS) == NBT_FIX_MWS && ev[36] == 0xf7 && ev[35] == 0xff && ev[37] == 0xff);
    CHECK(nbt_fix_event(ev, 70, NBT_FIX_MWS) == 0);
    memset(ev + 6, 0xff, 64);
    CHECK(nbt_fix_event(ev, 70, 0) == 0 && ev[36] == 0xff);
    /* LE Read Local Supported Features */
    memset(ev, 0xff, sizeof(ev));
    ev[0] = 0x0e; ev[1] = 12; ev[2] = 1; ev[3] = 0x03; ev[4] = 0x20; ev[5] = 0;
    CHECK(nbt_fix_event(ev, 14, NBT_FIX_LE_CODED) == NBT_FIX_LE_CODED && ev[7] == 0xf7 && ev[6] == 0xff);
    ev[5] = 1;                                              /* failed command: untouched */
    ev[7] = 0xff;
    CHECK(nbt_fix_event(ev, 14, NBT_FIX_LE_CODED) == 0 && ev[7] == 0xff);
    /* LE Extended Advertising Report with two reports (data 3 and 0 bytes) */
    memset(ev, 0, sizeof(ev));
    ev[0] = 0x3e; ev[2] = 0x0d; ev[3] = 2;
    ev[4 + 9] = 0x81; ev[4 + 10] = 0x43; ev[4 + 23] = 3;
    ev[4 + 27 + 9] = 0x01; ev[4 + 27 + 10] = 0xe2; ev[4 + 27 + 23] = 0;
    ev[1] = (uint8_t)(2 + 27 + 24);
    CHECK(nbt_fix_event(ev, 2u + ev[1], NBT_FIX_EXTADV_PHY) == NBT_FIX_EXTADV_PHY);
    CHECK(ev[13] == 0x01 && ev[14] == 0x03 && ev[40] == 0x01 && ev[41] == 0x02);
    ev[1] = 20;                                             /* truncated: nothing past the end */
    ev[13] = 0x81;
    CHECK(nbt_fix_event(ev, 22, NBT_FIX_EXTADV_PHY) == 0 && ev[13] == 0x81);
}

static void test_otp_and_names(void)
{
    uint8_t otp[NBT_OTP_SIZE];
    char s[16], v[16], name[64];
    const char chip[] = "s=B1 M=RASP", board[] = "V=m m=6.11";
    uint32_t n = 0, l;
    memset(otp, 0, sizeof(otp));
    otp[n++] = 0x80; otp[n++] = 4; n += 4;                 /* CIS block first: skipped */
    l = 4 + sizeof(chip) + sizeof(board);
    otp[n++] = 0x15; otp[n++] = (uint8_t)l;
    otp[n] = 8; n += 4;
    memcpy(otp + n, chip, sizeof(chip)); n += sizeof(chip);
    memcpy(otp + n, board, sizeof(board)); n += sizeof(board);
    CHECK(nbt_otp_parse(otp, sizeof(otp), s, v) == NBT_OK && !strcmp(s, "b1") && !strcmp(v, "m"));
    otp[8] = 9;                                             /* header != 8 */
    CHECK(nbt_otp_parse(otp, sizeof(otp), s, v) == NBT_ERR_FORMAT);
    otp[8] = 8;
    otp[7] = (uint8_t)(l - 1);                              /* board string not terminated inside */
    CHECK(nbt_otp_parse(otp, sizeof(otp), s, v) != NBT_OK);
    otp[7] = (uint8_t)l;
    otp[12] = 'x'; otp[13] = 'x';                           /* "sxB1": no '=' */
    CHECK(nbt_otp_parse(otp, sizeof(otp), s, v) == NBT_ERR_FORMAT);
    memset(otp, 0, sizeof(otp));
    CHECK(nbt_otp_parse(otp, sizeof(otp), s, v) == NBT_ERR_FORMAT);
    CHECK(nbt_fw_name(name, sizeof(name), "b1", "m", "bin") == NBT_OK && !strcmp(name, "bt4378b1-atlantisb-m.bin"));
    CHECK(nbt_fw_name(name, sizeof(name), "b1", "u", "ptb") == NBT_OK && !strcmp(name, "bt4378b1-atlantisb-u.ptb"));
    CHECK(nbt_fw_name(name, sizeof(name), "b3", "m", "bin") == NBT_ERR_UNKNOWN);
    CHECK(nbt_fw_name(name, sizeof(name), "b1", "../", "bin") == NBT_ERR_ARG);
    CHECK(nbt_fw_name(name, 10, "b1", "m", "bin") == NBT_ERR_TOOBIG);
}

static void test_fifo(void)
{
    uint8_t mem[64], out[64], t = 0;
    uint8_t d[40];
    uint32_t l = 0, i, k;
    nbt_fifo f;
    for (i = 0; i < sizeof(d); i++)
        d[i] = (uint8_t)(i + 1);
    nbt_fifo_init(&f, mem, sizeof(mem));
    CHECK(nbt_fifo_pop(&f, out, sizeof(out), &t, &l) == NBT_ERR_RING);
    for (k = 0; k < 50; k++) {                              /* wrap around many times */
        uint32_t n1 = 5 + k % 17, n2 = 20 - k % 11;
        CHECK(nbt_fifo_push(&f, 4, d, n1) == NBT_OK);
        CHECK(nbt_fifo_push(&f, 2, d + 3, n2) == NBT_OK);
        CHECK(nbt_fifo_peek(&f, &t, &l) == NBT_OK && t == 4 && l == n1);
        CHECK(nbt_fifo_pop(&f, out, sizeof(out), &t, &l) == NBT_OK && t == 4 && l == n1 && !memcmp(out, d, n1));
        CHECK(nbt_fifo_pop(&f, out, sizeof(out), &t, &l) == NBT_OK && t == 2 && l == n2 && !memcmp(out, d + 3, n2));
        CHECK(f.used == 0 && f.count == 0);
    }
    CHECK(nbt_fifo_push(&f, 4, d, 40) == NBT_OK);
    CHECK(nbt_fifo_push(&f, 4, d, 30) == NBT_ERR_FULL && f.drops == 1);
    CHECK(nbt_fifo_pop(&f, out, 10, &t, &l) == NBT_ERR_TOOBIG && f.count == 1);
    CHECK(nbt_fifo_push(&f, 4, NULL, 0) == NBT_OK);
    CHECK(nbt_fifo_pop(&f, out, sizeof(out), &t, &l) == NBT_OK && l == 40);
    CHECK(nbt_fifo_pop(&f, out, sizeof(out), &t, &l) == NBT_OK && l == 0 && f.used == 0);
    CHECK(nbt_fifo_push(&f, 4, d, 33) == NBT_OK && nbt_fifo_push(&f, 2, d, 5) == NBT_OK);
    CHECK(nbt_fifo_drop(&f) == NBT_OK && f.count == 1 && f.drops == 2);
    CHECK(nbt_fifo_pop(&f, out, sizeof(out), &t, &l) == NBT_OK && t == 2 && l == 5);
    CHECK(nbt_fifo_drop(&f) == NBT_ERR_RING);
}

/* optional: the macOS firmware files the build script packages must be what the core names */
static void test_real_files(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "rb");
        long sz;
        CHECK(fp != NULL);
        if (!fp)
            continue;
        fseek(fp, 0, SEEK_END);
        sz = ftell(fp);
        fclose(fp);
        CHECK(sz > 0);
        printf("file %s: %ld bytes (%u calibration-sized chunks if ptb)\n", strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i],
               sz, nbt_chunks((uint32_t)sz, NBT_PTB_CHUNK));
    }
}

int main(int argc, char **argv)
{
    test_layout();
    test_context_and_msgs();
    test_xring();
    test_d2h_and_completion();
    test_hci();
    test_fix();
    test_otp_and_names();
    test_fifo();
    test_real_files(argc, argv);
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
