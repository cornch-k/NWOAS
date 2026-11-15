/* S243 delivery-channel wire format + argument/path helpers: pure logic only.
 *
 * Depends only on <stdint.h>/<stddef.h>; no Win32 types, no I/O, no globals.
 * The same code is compiled into the no-CRT ARM64 client (nwget243.c) and the
 * host unit test (test_wire.c), which checks it against vectors generated from
 * the real ../s243_channel.py.
 *
 * Request  (4096 B, LBA 127): magic "NWOAS-S243-REQ!!", token, id u32 @32,
 *   offset u64 @36 (SENTINEL = manifest), length u32 @44, index u32 @48,
 *   crc32[0:52] @52, zero tail from 56.
 * Response (LBA 129, 64 KiB): magic "NWOAS-S243-FILE!", token, id @32,
 *   status @36, file_size @40, offset @48, served @56, payload_crc @60,
 *   sha256 @64, index @96, count @100, reserved zero [104:124],
 *   header crc32[0:124] @124, payload @128.
 */
#ifndef NWOAS_WIRE243_H
#define NWOAS_WIRE243_H

#include <stdint.h>
#include <stddef.h>

#define WIRE_BLOCK          4096u
#define WIRE_REQ_LBA        127u
#define WIRE_PORT_LBA       128u
#define WIRE_RESP_LBA       129u
#define WIRE_WINDOW_BLOCKS  16u
#define WIRE_WINDOW_BYTES   (WIRE_WINDOW_BLOCKS * WIRE_BLOCK)
#define WIRE_HEADER         128u
#define WIRE_PAYLOAD_CAP    (WIRE_WINDOW_BYTES - WIRE_HEADER)
#define WIRE_TOKEN_LEN      16u
#define WIRE_SHA_LEN        32u
#define WIRE_SENTINEL       0xFFFFFFFFFFFFFFFFULL
#define WIRE_MAX_ARTIFACTS  64u
#define WIRE_MAX_NAME       255u

#define WIRE_STATUS_DATA      0u
#define WIRE_STATUS_FINAL     1u
#define WIRE_STATUS_NONE      2u
#define WIRE_STATUS_MANIFEST  3u

enum {
    WIRE_OK = 0,
    WIRE_E_SHORT, WIRE_E_MAGIC, WIRE_E_TOKEN, WIRE_E_HDRCRC, WIRE_E_RSVD,
    WIRE_E_SERVED, WIRE_E_PAYCRC, WIRE_E_ID, WIRE_E_STATUS, WIRE_E_OFFSET,
    WIRE_E_SIZE, WIRE_E_INDEX
};

static inline uint32_t wire_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & ((c & 1u) ? 0xFFFFFFFFu : 0u));
    }
    return ~c;
}

static inline uint32_t wire_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t wire_rd64(const uint8_t *p)
{
    return (uint64_t)wire_rd32(p) | ((uint64_t)wire_rd32(p + 4) << 32);
}

static inline void wire_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline void wire_wr64(uint8_t *p, uint64_t v)
{
    wire_wr32(p, (uint32_t)v);
    wire_wr32(p + 4, (uint32_t)(v >> 32));
}

static inline void wire_build_request(uint8_t b[WIRE_BLOCK], const uint8_t token[WIRE_TOKEN_LEN],
                                      uint32_t id, uint32_t index, uint64_t offset, uint32_t length)
{
    static const uint8_t M[16] =
        { 'N','W','O','A','S','-','S','2','4','3','-','R','E','Q','!','!' };
    for (size_t i = 0; i < WIRE_BLOCK; i++) b[i] = 0;
    for (int i = 0; i < 16; i++) b[i] = M[i];
    for (int i = 0; i < 16; i++) b[16 + i] = token[i];
    wire_wr32(b + 32, id);
    wire_wr64(b + 36, offset);
    wire_wr32(b + 44, length);
    wire_wr32(b + 48, index);
    wire_wr32(b + 52, wire_crc32(b, 52));
}

typedef struct {
    uint32_t       id, status, served, payload_crc, index, count;
    uint64_t       file_size, offset;
    const uint8_t *sha256;   /* -> window[64:96] */
    const uint8_t *payload;  /* -> window[128:128+served] */
} wire_resp;

/* Structural + integrity validation (magic, token, header CRC, reserved zero,
 * served range, payload CRC). Expectations are checked by wire_check_*. */
static inline int wire_parse_response(const uint8_t *win, size_t winlen,
                                      const uint8_t token[WIRE_TOKEN_LEN], wire_resp *r)
{
    static const uint8_t M[16] =
        { 'N','W','O','A','S','-','S','2','4','3','-','F','I','L','E','!' };
    if (winlen < WIRE_HEADER) return WIRE_E_SHORT;
    for (int i = 0; i < 16; i++) if (win[i] != M[i]) return WIRE_E_MAGIC;
    for (int i = 0; i < 16; i++) if (win[16 + i] != token[i]) return WIRE_E_TOKEN;
    if (wire_crc32(win, 124) != wire_rd32(win + 124)) return WIRE_E_HDRCRC;
    for (int i = 104; i < 124; i++) if (win[i] != 0) return WIRE_E_RSVD;
    r->id          = wire_rd32(win + 32);
    r->status      = wire_rd32(win + 36);
    r->file_size   = wire_rd64(win + 40);
    r->offset      = wire_rd64(win + 48);
    r->served      = wire_rd32(win + 56);
    r->payload_crc = wire_rd32(win + 60);
    r->sha256      = win + 64;
    r->index       = wire_rd32(win + 96);
    r->count       = wire_rd32(win + 100);
    if (r->served > WIRE_PAYLOAD_CAP) return WIRE_E_SERVED;
    if ((size_t)WIRE_HEADER + r->served > winlen) return WIRE_E_SERVED;
    if (r->count > WIRE_MAX_ARTIFACTS) return WIRE_E_INDEX;
    r->payload = win + WIRE_HEADER;
    if (wire_crc32(r->payload, r->served) != r->payload_crc) return WIRE_E_PAYCRC;
    return WIRE_OK;
}

static inline int wire_check_manifest(const wire_resp *r, uint32_t id, uint32_t index)
{
    if (r->id != id)                       return WIRE_E_ID;
    if (r->status != WIRE_STATUS_MANIFEST) return WIRE_E_STATUS;
    if (r->index != index || index >= r->count) return WIRE_E_INDEX;
    if (r->offset != WIRE_SENTINEL)        return WIRE_E_OFFSET;
    if (r->served < 1 || r->served > WIRE_MAX_NAME) return WIRE_E_SERVED;
    return WIRE_OK;
}

/* DATA must serve exactly `requested`; FINAL must serve fewer and end on EOF.
 * `sha` (32 bytes) must equal the manifest digest so the artifact cannot change. */
static inline int wire_check_data(const wire_resp *r, uint32_t id, uint32_t index, uint64_t offset,
                                  uint32_t requested, uint64_t file_size, const uint8_t *sha)
{
    if (r->id != id)               return WIRE_E_ID;
    if (r->index != index)         return WIRE_E_INDEX;
    if (r->offset != offset)       return WIRE_E_OFFSET;
    if (r->file_size != file_size) return WIRE_E_SIZE;
    if (offset > file_size || r->served > file_size - offset) return WIRE_E_SIZE;
    for (unsigned i = 0; i < WIRE_SHA_LEN; i++) if (r->sha256[i] != sha[i]) return WIRE_E_SIZE;
    if (r->status == WIRE_STATUS_DATA) {
        if (r->served != requested) return WIRE_E_SERVED;
    } else if (r->status == WIRE_STATUS_FINAL) {
        if (r->served >= requested) return WIRE_E_SERVED;
        if (offset + (uint64_t)r->served != file_size) return WIRE_E_SIZE;
    } else {
        return WIRE_E_STATUS;
    }
    return WIRE_OK;
}

/* ---- command line: NWGET243.EXE <index> <sha256-hex> <target-path> ------- */

#define ARG_MAX_PATH 240u   /* leaves room for ".part" under MAX_PATH */

/* Split a UTF-16 command line in place-free fashion: returns token count (up
 * to `max`), each token as [start,len) into `cl`. Tokens are whitespace
 * separated; a token may be wrapped in double quotes (no escapes, no embedded
 * quotes). Returns -1 on an unterminated or embedded quote. argv0 included. */
static inline int arg_split(const uint16_t *cl, size_t starts[], size_t lens[], int max)
{
    size_t i = 0;
    int n = 0;
    for (;;) {
        while (cl[i] == ' ' || cl[i] == '\t') i++;
        if (!cl[i]) return n;
        if (n == max) return -1;
        if (cl[i] == '"') {
            size_t s = ++i;
            while (cl[i] && cl[i] != '"') i++;
            if (cl[i] != '"') return -1;
            starts[n] = s; lens[n] = i - s; n++;
            i++;
            if (cl[i] && cl[i] != ' ' && cl[i] != '\t') return -1;
        } else {
            size_t s = i;
            while (cl[i] && cl[i] != ' ' && cl[i] != '\t') {
                if (cl[i] == '"') return -1;
                i++;
            }
            starts[n] = s; lens[n] = i - s; n++;
        }
    }
}

static inline int arg_index(const uint16_t *p, size_t n, uint32_t *out)
{
    uint32_t v = 0;
    if (n < 1 || n > 2) return 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return 0;
        v = v * 10 + (uint32_t)(p[i] - '0');
    }
    if (v >= WIRE_MAX_ARTIFACTS) return 0;
    *out = v;
    return 1;
}

static inline int arg_sha(const uint16_t *p, size_t n, uint8_t out[WIRE_SHA_LEN])
{
    if (n != 2 * WIRE_SHA_LEN) return 0;
    for (size_t i = 0; i < n; i++) {
        uint16_t c = p[i];
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return 0;
        if (i & 1) out[i / 2] = (uint8_t)(out[i / 2] | v);
        else out[i / 2] = (uint8_t)(v << 4);
    }
    return 1;
}

/* Target path policy: an absolute path on C: ("C:\..."), printable ASCII only,
 * none of * ? " < > | : (after "C:"), no empty / "." / ".." components, no
 * component ending in '.' or ' ', no trailing backslash, length <= 240. */
static inline int arg_target(const uint16_t *p, size_t n)
{
    if (n < 4 || n > ARG_MAX_PATH) return 0;
    if (!(p[0] == 'C' || p[0] == 'c') || p[1] != ':' || p[2] != '\\') return 0;
    size_t comp = 3;
    for (size_t i = 3; i <= n; i++) {
        uint16_t c = i < n ? p[i] : '\\';
        if (i < n) {
            if (c < 0x20 || c > 0x7e) return 0;
            if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
                c == ':' || c == '/') return 0;
        }
        if (c == '\\') {
            size_t len = i - comp;
            if (len == 0) return 0;
            if (p[i - 1] == '.' || p[i - 1] == ' ') return 0;
            if (len == 1 && p[comp] == '.') return 0;
            if (len == 2 && p[comp] == '.' && p[comp + 1] == '.') return 0;
            comp = i + 1;
        }
    }
    return 1;
}

#endif
