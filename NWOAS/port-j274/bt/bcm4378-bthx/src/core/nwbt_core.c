/* nwbt_core.c - see nwbt_core.h.  Independent implementation, SPDX-License-Identifier: MIT */
#include "nwbt_core.h"

#include <string.h>

/* ---------------------------------------------------------------- little endian helpers */
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16); }

uint32_t nbt_doorbell_value(uint8_t doorbell, uint16_t value)
{
    return ((uint32_t)value << 16) | ((uint32_t)doorbell << 8) | 0x20u;
}

uint16_t nbt_rs_get(const uint8_t *state, uint32_t off) { return get16(state + off); }
void nbt_rs_set(uint8_t *state, uint32_t off, uint16_t v) { put16(state + off, v); }

/* ---------------------------------------------------------------- layout */
static uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

static void xr(nbt_xring *r, uint8_t id, uint8_t db, uint8_t cr, uint8_t flags, uint16_t payload, uint16_t mapped)
{
    memset(r, 0, sizeof(*r));
    r->id = id;
    r->doorbell = db;
    r->completion = cr;
    r->flags = flags;
    r->n_entries = NBT_RING_N;
    r->payload_size = (uint16_t)align_up(payload, 4);
    r->mapped_size = (uint16_t)align_up(mapped, 4);
}

static void cr(nbt_cring *c, uint8_t id, uint16_t n, uint16_t payload, uint16_t delay, uint32_t mask)
{
    memset(c, 0, sizeof(*c));
    c->id = id;
    c->n_entries = n;
    c->payload_size = (uint16_t)align_up(payload, 4);
    c->delay = delay;
    c->xfer_mask = mask;
}

int nbt_layout_init(nbt_layout *L)
{
    uint32_t off = 0, i;
    if (!L)
        return NBT_ERR_ARG;
    memset(L, 0, sizeof(*L));
    xr(&L->x[NBT_XR_CONTROL], NBT_XR_CONTROL, NBT_DB_CONTROL, NBT_CR_CONTROL_ACK, NBT_RF_WAIT, NBT_CONTROL_MSG_SIZE, 0);
    xr(&L->x[NBT_XR_HCI_H2D], NBT_XR_HCI_H2D, NBT_DB_HCI_H2D, NBT_CR_HCI_ACL_ACK, 0, NBT_MAX_EVENT_PAYLOAD, 0);
    xr(&L->x[NBT_XR_HCI_D2H], NBT_XR_HCI_D2H, NBT_DB_HCI_D2H, NBT_CR_HCI_ACL_EVENT, NBT_RF_VIRTUAL, 0, 0);
    xr(&L->x[NBT_XR_SCO_H2D], NBT_XR_SCO_H2D, NBT_DB_SCO, NBT_CR_SCO_ACK, NBT_RF_SYNC, NBT_MAX_SCO_PAYLOAD, 0);
    xr(&L->x[NBT_XR_SCO_D2H], NBT_XR_SCO_D2H, NBT_DB_SCO, NBT_CR_SCO_EVENT, NBT_RF_VIRTUAL | NBT_RF_SYNC, 0, 0);
    xr(&L->x[NBT_XR_ACL_H2D], NBT_XR_ACL_H2D, NBT_DB_ACL_H2D, NBT_CR_HCI_ACL_ACK, 0, 0, NBT_MAX_ACL_PAYLOAD);
    xr(&L->x[NBT_XR_ACL_D2H], NBT_XR_ACL_D2H, NBT_DB_ACL_D2H, NBT_CR_HCI_ACL_EVENT, NBT_RF_D2H_BUFS, 0, NBT_MAX_ACL_PAYLOAD);
    cr(&L->c[NBT_CR_CONTROL_ACK], NBT_CR_CONTROL_ACK, 32, 0, 0, 1u << NBT_XR_CONTROL);
    cr(&L->c[NBT_CR_HCI_ACL_ACK], NBT_CR_HCI_ACL_ACK, 2 * NBT_RING_N, 0, 1000,
       (1u << NBT_XR_HCI_H2D) | (1u << NBT_XR_ACL_H2D));
    cr(&L->c[NBT_CR_HCI_ACL_EVENT], NBT_CR_HCI_ACL_EVENT, 2 * NBT_RING_N, NBT_MAX_EVENT_PAYLOAD, 1000,
       (1u << NBT_XR_HCI_D2H) | (1u << NBT_XR_ACL_D2H));
    cr(&L->c[NBT_CR_SCO_ACK], NBT_CR_SCO_ACK, NBT_RING_N, 0, 0, 1u << NBT_XR_SCO_H2D);
    cr(&L->c[NBT_CR_SCO_EVENT], NBT_CR_SCO_EVENT, NBT_RING_N, NBT_MAX_SCO_PAYLOAD, 0, 1u << NBT_XR_SCO_D2H);

    L->ctx_off = off;    off = align_up(off + NBT_CONTEXT_SIZE, 4096);
    L->state_off = off;  off = align_up(off + NBT_RS_SIZE, 4096);
    L->periph_off = off; off = align_up(off + NBT_PERIPH_INFO_SIZE, 4096);
    for (i = 0; i < NBT_N_XR_USED; i++) {
        nbt_xring *r = &L->x[i];
        if (r->payload_size > NBT_MAX_INPLACE || r->n_entries > NBT_MAX_RING)
            return NBT_ERR_RING;
        if (!(r->flags & NBT_RF_VIRTUAL)) {
            r->ring_off = off;
            off = align_up(off + (uint32_t)r->n_entries * (NBT_ENTRY_SIZE + r->payload_size), 4096);
        }
        if (r->mapped_size) {
            r->payload_off = off;
            off = align_up(off + (uint32_t)r->n_entries * r->mapped_size, 4096);
        }
    }
    for (i = 0; i < NBT_N_CR_USED; i++) {
        nbt_cring *c = &L->c[i];
        if (c->payload_size > NBT_MAX_INPLACE || c->n_entries > NBT_MAX_RING)
            return NBT_ERR_RING;
        c->ring_off = off;
        off = align_up(off + (uint32_t)c->n_entries * (NBT_ENTRY_SIZE + c->payload_size), 4096);
    }
    L->size = off;
    return NBT_OK;
}

void nbt_context_build(uint8_t ctx[NBT_CONTEXT_SIZE], const nbt_layout *L, uint64_t base)
{
    const nbt_xring *ctl = &L->x[NBT_XR_CONTROL];
    const nbt_cring *ack = &L->c[NBT_CR_CONTROL_ACK];
    uint64_t st = base + L->state_off;
    memset(ctx, 0, NBT_CONTEXT_SIZE);
    put16(ctx + 0, 1);                                  /* version (value the firmware accepts) */
    put16(ctx + 2, NBT_CONTEXT_SIZE);
    put32(ctx + 4, 2);                                  /* enabled capabilities */
    put64(ctx + 8, base + L->periph_off);
    put64(ctx + 16, st + NBT_RS_CHEAD(0));
    put64(ctx + 24, st + NBT_RS_XTAIL(0));
    put64(ctx + 32, st + NBT_RS_CTAIL(0));
    put64(ctx + 40, st + NBT_RS_XHEAD(0));
    put16(ctx + 48, NBT_N_COMPLETION_RINGS);
    put16(ctx + 50, NBT_N_XFER_RINGS);
    put64(ctx + 52, base + ack->ring_off);
    put64(ctx + 60, base + ctl->ring_off);
    put16(ctx + 68, ctl->n_entries);
    put16(ctx + 70, ack->n_entries);
    put16(ctx + 72, ctl->doorbell);
    put16(ctx + 74, 0xffff);                            /* control completion ring has no doorbell */
    ctx[81] = (uint8_t)(ctl->payload_size / 4);         /* control transfer ring footer (32-bit words) */
}

void nbt_msg_create_cring(uint8_t m[NBT_CONTROL_MSG_SIZE], const nbt_cring *c, uint64_t base)
{
    memset(m, 0, NBT_CONTROL_MSG_SIZE);
    m[0] = 2;
    m[2] = (uint8_t)(c->payload_size / 4);
    put16(m + 4, c->id);
    put16(m + 6, c->id);
    put64(m + 8, base + c->ring_off);
    put16(m + 16, c->n_entries);
    put32(m + 18, 0xffffffffu);
    put16(m + 30, c->delay);
    put32(m + 32, 0xffffffffu);
}

void nbt_msg_create_xring(uint8_t m[NBT_CONTROL_MSG_SIZE], const nbt_xring *r, uint64_t base)
{
    uint16_t flags = 0;
    memset(m, 0, NBT_CONTROL_MSG_SIZE);
    if (r->flags & NBT_RF_VIRTUAL)
        flags |= 0x80;
    if (r->flags & NBT_RF_SYNC)
        flags |= 0x100;
    m[0] = 1;
    m[2] = (uint8_t)(r->payload_size / 4);
    put16(m + 4, r->id);
    put16(m + 6, r->id);
    put64(m + 8, (r->flags & NBT_RF_VIRTUAL) ? 0 : base + r->ring_off);
    put16(m + 24, r->n_entries);
    put16(m + 26, r->completion);
    put16(m + 28, r->doorbell);
    put16(m + 30, flags);
}

void nbt_msg_destroy_cring(uint8_t m[NBT_CONTROL_MSG_SIZE], uint8_t id)
{
    memset(m, 0, NBT_CONTROL_MSG_SIZE);
    m[0] = 4;
    put16(m + 2, id);
}

void nbt_msg_destroy_xring(uint8_t m[NBT_CONTROL_MSG_SIZE], uint8_t id)
{
    memset(m, 0, NBT_CONTROL_MSG_SIZE);
    m[0] = 3;
    put16(m + 2, id);
}

/* ---------------------------------------------------------------- transfer rings */
static int bit_get(const uint32_t *b, int i) { return (b[i >> 5] >> (i & 31)) & 1; }
static void bit_set(uint32_t *b, int i) { b[i >> 5] |= 1u << (i & 31); }
static void bit_clr(uint32_t *b, int i) { b[i >> 5] &= ~(1u << (i & 31)); }

int nbt_xring_busy(const nbt_xring *r, int msgid)
{
    return msgid >= 0 && msgid < NBT_MAX_RING && bit_get(r->msgids, msgid);
}

uint32_t nbt_xring_inflight(const nbt_xring *r)
{
    uint32_t n = 0;
    int i;
    for (i = 0; i < r->n_entries; i++)
        n += (uint32_t)bit_get(r->msgids, i);
    return n;
}

int nbt_xring_put(nbt_xring *r, uint8_t *blk, uint64_t base, uint8_t *state,
                  const uint8_t *data, uint32_t len, uint16_t *db)
{
    uint16_t head, tail, nh;
    uint8_t *e;
    int id;
    if (!r || !blk || !state || (!data && len) || (r->flags & NBT_RF_VIRTUAL) || !r->n_entries)
        return NBT_ERR_ARG;
    if (len > r->payload_size && len > r->mapped_size)
        return NBT_ERR_TOOBIG;
    head = nbt_rs_get(state, NBT_RS_XHEAD(r->id));
    tail = nbt_rs_get(state, NBT_RS_XTAIL(r->id));
    if (head >= r->n_entries || tail >= r->n_entries)
        return NBT_ERR_RING;
    nh = (uint16_t)((head + 1) % r->n_entries);
    if (nh == tail)
        return NBT_ERR_FULL;
    for (id = 0; id < r->n_entries && bit_get(r->msgids, id); id++)
        ;
    if (id >= r->n_entries)
        return NBT_ERR_FULL;
    e = blk + r->ring_off + (uint32_t)head * (NBT_ENTRY_SIZE + r->payload_size);
    memset(e, 0, NBT_ENTRY_SIZE);
    put16(e + 1, (uint16_t)len);
    put16(e + 12, (uint16_t)(((uint16_t)r->generation << 8) | (uint16_t)id));
    if (len <= r->payload_size) {
        e[0] = NBT_XF_IN_FOOTER;
        if (len)
            memcpy(e + NBT_ENTRY_SIZE, data, len);
    } else {
        uint32_t slot = r->payload_off + (uint32_t)id * r->mapped_size;
        e[0] = NBT_XF_MAPPED;
        put64(e + 4, base + slot);
        memcpy(blk + slot, data, len);
    }
    bit_set(r->msgids, id);
    nbt_rs_set(state, NBT_RS_XHEAD(r->id), nh);
    if (db)
        *db = nh;
    return id;
}

int nbt_xring_ack(nbt_xring *r, uint16_t raw)
{
    int id = raw & 0xff;
    if ((raw >> 8) != r->generation)
        return NBT_ERR_GEN;
    if (id >= r->n_entries || !bit_get(r->msgids, id))
        return NBT_ERR_MSGID;
    bit_clr(r->msgids, id);
    return id;
}

void nbt_xring_reset(nbt_xring *r, uint8_t *state)
{
    nbt_rs_set(state, NBT_RS_XHEAD(r->id), 0);
    nbt_rs_set(state, NBT_RS_XTAIL(r->id), 0);
    r->generation++;
    memset(r->msgids, 0, sizeof(r->msgids));
}

void nbt_xring_fill_d2h(nbt_xring *r, uint8_t *blk, uint64_t base)
{
    uint16_t i;
    for (i = 0; i < r->n_entries; i++) {
        uint8_t *e = blk + r->ring_off + (uint32_t)i * NBT_ENTRY_SIZE;
        memset(e, 0, NBT_ENTRY_SIZE);
        e[0] = NBT_XF_MAPPED;
        put16(e + 1, r->mapped_size);
        put64(e + 4, base + r->payload_off + (uint32_t)i * r->mapped_size);
        put16(e + 12, (uint16_t)(((uint16_t)r->generation << 8) | i));
    }
}

uint16_t nbt_xring_advance(const nbt_xring *r, uint8_t *state)
{
    uint16_t h = (uint16_t)((nbt_rs_get(state, NBT_RS_XHEAD(r->id)) + 1) % r->n_entries);
    nbt_rs_set(state, NBT_RS_XHEAD(r->id), h);
    return h;
}

/* ---------------------------------------------------------------- completion rings */
int nbt_cring_entry(const nbt_cring *c, const uint8_t *blk, uint16_t pos, nbt_centry *out)
{
    const uint8_t *e;
    if (!c || !blk || !out || pos >= c->n_entries)
        return NBT_ERR_ARG;
    e = blk + c->ring_off + (uint32_t)pos * (NBT_ENTRY_SIZE + c->payload_size);
    out->flags = e[0];
    out->xring = get16(e + 2);
    out->msgid = get16(e + 4);
    out->len = get32(e + 6);
    out->data_off = c->ring_off + (uint32_t)pos * (NBT_ENTRY_SIZE + c->payload_size) + NBT_ENTRY_SIZE;
    if (out->xring >= 32 || !(c->xfer_mask & (1u << out->xring)))
        return NBT_ERR_UNKNOWN;
    return NBT_OK;
}

int nbt_d2h_payload(const nbt_xring *r, const nbt_cring *c, const nbt_centry *e, uint32_t *off, uint32_t *len)
{
    if ((r->flags & NBT_RF_D2H_BUFS) && (e->flags & NBT_XF_MAPPED)) {
        int id = e->msgid & 0xff;
        if ((e->msgid >> 8) != r->generation)
            return NBT_ERR_GEN;
        if (id >= r->n_entries)
            return NBT_ERR_MSGID;
        if (e->len > r->mapped_size)
            return NBT_ERR_TOOBIG;
        *off = r->payload_off + (uint32_t)id * r->mapped_size;
        *len = e->len;
        return NBT_OK;
    }
    if (e->len > c->payload_size)
        return NBT_ERR_TOOBIG;
    *off = e->data_off;
    *len = e->len;
    return NBT_OK;
}

/* ---------------------------------------------------------------- HCI */
uint32_t nbt_hci_cmd(uint8_t *out, uint32_t cap, uint16_t opcode, const uint8_t *params, uint32_t plen)
{
    if (!out || plen > 255 || cap < 3 + plen || (plen && !params))
        return 0;
    put16(out, opcode);
    out[2] = (uint8_t)plen;
    if (plen)
        memcpy(out + 3, params, plen);
    return 3 + plen;
}

int nbt_hci_cmd_done(const uint8_t *evt, uint32_t len, uint16_t *opcode, uint8_t *status,
                     uint32_t *ret_off, uint32_t *ret_len)
{
    uint32_t plen;
    if (!evt || len < 2)
        return NBT_ERR_FORMAT;
    plen = evt[1];
    if (plen + 2 > len)
        return NBT_ERR_FORMAT;
    if (evt[0] == NBT_HCI_EVT_CMD_COMPLETE) {
        if (plen < 3)
            return NBT_ERR_FORMAT;
        *opcode = get16(evt + 3);
        *status = plen >= 4 ? evt[5] : 0;
        *ret_off = 5;
        *ret_len = plen - 3;
        return 1;
    }
    if (evt[0] == NBT_HCI_EVT_CMD_STATUS) {
        if (plen < 4)
            return NBT_ERR_FORMAT;
        *status = evt[2];
        *opcode = get16(evt + 4);
        *ret_off = 0;
        *ret_len = 0;
        return 2;
    }
    return 0;
}

uint32_t nbt_chunks(uint32_t len, uint32_t chunk) { return chunk ? (len + chunk - 1) / chunk : 0; }

int nbt_cal_chunk(const uint8_t *blob, uint32_t len, uint32_t idx, uint8_t params[NBT_CAL_PARAM_LEN])
{
    uint32_t n = nbt_chunks(len, NBT_CAL_CHUNK), take;
    if (!blob || !len || idx >= n)
        return NBT_ERR_ARG;
    take = len - idx * NBT_CAL_CHUNK;
    if (take > NBT_CAL_CHUNK)
        take = NBT_CAL_CHUNK;
    memset(params, 0, NBT_CAL_PARAM_LEN);
    params[0] = 0x03;
    put16(params + 1, (uint16_t)(n - idx - 1));
    memcpy(params + 3, blob + idx * NBT_CAL_CHUNK, take);
    return NBT_OK;
}

int nbt_ptb_chunk(const uint8_t *blob, uint32_t len, uint32_t idx, uint8_t params[NBT_PTB_PARAM_LEN])
{
    uint32_t n = nbt_chunks(len, NBT_PTB_CHUNK), take;
    if (!blob || !len || idx >= n)
        return NBT_ERR_ARG;
    take = len - idx * NBT_PTB_CHUNK;
    if (take > NBT_PTB_CHUNK)
        take = NBT_PTB_CHUNK;
    memset(params, 0, NBT_PTB_PARAM_LEN);
    put16(params, (uint16_t)(n - idx - 1));
    memcpy(params + 2, blob + idx * NBT_PTB_CHUNK, take);
    return NBT_OK;
}

int nbt_bdaddr_is_default(const uint8_t le[6], uint16_t chip)
{
    return le[0] == 0x93 && le[1] == 0x76 && le[2] == 0x00 && le[4] == (uint8_t)chip && le[5] == (uint8_t)(chip >> 8);
}

void nbt_bdaddr_from_adt(const uint8_t adt[6], uint8_t le[6])
{
    int i;
    for (i = 0; i < 6; i++)
        le[i] = adt[5 - i];
}

uint32_t nbt_fix_event(uint8_t *evt, uint32_t len, uint32_t enabled)
{
    uint16_t op;
    uint8_t st;
    uint32_t ro, rl, done = 0;
    int k = nbt_hci_cmd_done(evt, len, &op, &st, &ro, &rl);
    if (k == 1 && (enabled & NBT_FIX_MWS) && op == NBT_OP_READ_LOCAL_CMDS && rl >= 1 + 64 && st == 0) {
        uint8_t *b = evt + ro + 1 + 30;                 /* Supported_Commands octet 30 */
        if (*b & 0x08) { *b &= (uint8_t)~0x08; done |= NBT_FIX_MWS; }
    }
    if (k == 1 && (enabled & NBT_FIX_LE_CODED) && op == NBT_OP_LE_READ_FEATURES && rl >= 1 + 8 && st == 0) {
        uint8_t *b = evt + ro + 1 + 1;                  /* LE_Features octet 1, bit 3 = LE Coded PHY */
        if (*b & 0x08) { *b &= (uint8_t)~0x08; done |= NBT_FIX_LE_CODED; }
    }
    if ((enabled & NBT_FIX_EXTADV_PHY) && len >= 4 && evt[0] == NBT_HCI_EVT_LE_META &&
        evt[2] == NBT_LE_EXT_ADV_REPORT && (uint32_t)evt[1] + 2 <= len) {
        uint32_t end = (uint32_t)evt[1] + 2, p = 4, n = evt[3], i;
        for (i = 0; i < n && p + 24 <= end; i++) {
            uint8_t a = evt[p + 9] & 0x1f, b = evt[p + 10] & 0x1f;
            if (a != evt[p + 9] || b != evt[p + 10]) { evt[p + 9] = a; evt[p + 10] = b; done |= NBT_FIX_EXTADV_PHY; }
            p += 24 + evt[p + 23];
        }
    }
    return done;
}

/* ---------------------------------------------------------------- OTP / files */
static int param_str(const char *s, uint32_t n, char want, int lower, char out[16])
{
    uint32_t i = 0;
    while (i < n) {
        char tag;
        uint32_t v, l = 0;
        while (i < n && s[i] == ' ')
            i++;
        if (i >= n || !s[i])
            break;
        tag = s[i++];
        if (i >= n || s[i++] != '=')
            return NBT_ERR_FORMAT;
        v = i;
        while (i < n && s[i] && s[i] != ' ')
            i++, l++;
        if (l > 15)
            return NBT_ERR_FORMAT;
        if (tag == want) {
            uint32_t k;
            for (k = 0; k < l; k++) {
                char c = s[v + k];
                out[k] = (lower && c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            }
            out[l] = 0;
        }
    }
    return NBT_OK;
}

int nbt_otp_parse(const uint8_t *otp, uint32_t n, char stepping[16], char vendor[16])
{
    uint32_t i = 0;
    int found = 0;
    stepping[0] = vendor[0] = 0;
    if (!otp)
        return NBT_ERR_ARG;
    while (i + 1 < n) {
        uint8_t type = otp[i], len = otp[i + 1];
        if (type == 0 || i + 2u + len > n)
            break;
        if (type == 0x15) {                              /* SYS_VENDOR: u32 8, chip params\0, board params\0 */
            const uint8_t *p = otp + i + 2;
            uint32_t a = 4, a_end, b, b_end;
            if (len < 6 || get32(p) != 8)
                return NBT_ERR_FORMAT;
            for (a_end = a; a_end < len && p[a_end]; a_end++)
                ;
            b = a_end + 1;
            if (b >= len)
                return NBT_ERR_FORMAT;
            for (b_end = b; b_end < len && p[b_end]; b_end++)
                ;
            if (b_end >= len)
                return NBT_ERR_FORMAT;
            if (param_str((const char *)p + a, a_end - a, 's', 1, stepping) ||
                param_str((const char *)p + b, b_end - b, 'V', 0, vendor))
                return NBT_ERR_FORMAT;
            found = 1;
        }
        i += 2u + len;
    }
    return (found && stepping[0] && vendor[0]) ? NBT_OK : NBT_ERR_FORMAT;
}

static int plain(const char *s)
{
    uint32_t i;
    for (i = 0; s[i]; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9')) || i > 8)
            return 0;
    return i > 0;
}

int nbt_fw_name(char *out, uint32_t cap, const char *stepping, const char *vendor, const char *ext)
{
    static const char pre[] = "bt4378", mid[] = "-atlantisb-";
    uint32_t need;
    if (!out || !stepping || !vendor || !ext || !plain(stepping) || !plain(vendor) || !plain(ext))
        return NBT_ERR_ARG;
    if (strcmp(stepping, "b1") || (strcmp(vendor, "m") && strcmp(vendor, "u")))
        return NBT_ERR_UNKNOWN;                          /* only these are in the package */
    need = (uint32_t)(sizeof(pre) - 1 + strlen(stepping) + sizeof(mid) - 1 + strlen(vendor) + 1 + strlen(ext) + 1);
    if (need > cap)
        return NBT_ERR_TOOBIG;
    strcpy(out, pre);
    strcat(out, stepping);
    strcat(out, mid);
    strcat(out, vendor);
    strcat(out, ".");
    strcat(out, ext);
    return NBT_OK;
}

/* ---------------------------------------------------------------- packet FIFO */
void nbt_fifo_init(nbt_fifo *f, uint8_t *mem, uint32_t cap)
{
    memset(f, 0, sizeof(*f));
    f->buf = mem;
    f->cap = cap;
}

static void fput(nbt_fifo *f, const uint8_t *s, uint32_t n)
{
    uint32_t a = f->cap - f->wr;
    if (a > n)
        a = n;
    memcpy(f->buf + f->wr, s, a);
    memcpy(f->buf, s + a, n - a);
    f->wr = (f->wr + n) % f->cap;
}

static void fget(const nbt_fifo *f, uint32_t at, uint8_t *d, uint32_t n)
{
    uint32_t a = f->cap - at;
    if (a > n)
        a = n;
    memcpy(d, f->buf + at, a);
    memcpy(d + a, f->buf, n - a);
}

int nbt_fifo_push(nbt_fifo *f, uint8_t type, const uint8_t *data, uint32_t len)
{
    uint8_t h[3];
    if (len > 0xffff || (len && !data))
        return NBT_ERR_ARG;
    if (3u + len > f->cap - f->used) {
        f->drops++;
        return NBT_ERR_FULL;
    }
    h[0] = type;
    put16(h + 1, (uint16_t)len);
    fput(f, h, 3);
    if (len)
        fput(f, data, len);
    f->used += 3u + len;
    f->count++;
    return NBT_OK;
}

int nbt_fifo_peek(const nbt_fifo *f, uint8_t *type, uint32_t *len)
{
    uint8_t h[3];
    if (!f->count)
        return NBT_ERR_RING;
    fget(f, f->rd, h, 3);
    *type = h[0];
    *len = get16(h + 1);
    return NBT_OK;
}

int nbt_fifo_pop(nbt_fifo *f, uint8_t *out, uint32_t cap, uint8_t *type, uint32_t *len)
{
    uint32_t n;
    uint8_t t;
    if (nbt_fifo_peek(f, &t, &n))
        return NBT_ERR_RING;
    if (n > cap)
        return NBT_ERR_TOOBIG;
    if (n)
        fget(f, (f->rd + 3) % f->cap, out, n);
    f->rd = (f->rd + 3 + n) % f->cap;
    f->used -= 3 + n;
    f->count--;
    *type = t;
    *len = n;
    return NBT_OK;
}

int nbt_fifo_drop(nbt_fifo *f)
{
    uint8_t t;
    uint32_t n;
    if (nbt_fifo_peek(f, &t, &n))
        return NBT_ERR_RING;
    f->rd = (f->rd + 3 + n) % f->cap;
    f->used -= 3 + n;
    f->count--;
    f->drops++;
    return NBT_OK;
}
