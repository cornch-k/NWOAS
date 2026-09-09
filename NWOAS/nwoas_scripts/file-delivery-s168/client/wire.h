/* S168 delivery-channel wire format: pure, platform-independent logic.
 *
 * This header deliberately depends ONLY on the freestanding standard headers
 * <stdint.h>/<stddef.h> and contains NO Win32 typedefs, NO I/O, and NO global
 * state. Every function is a pure `static inline` transform over caller-owned
 * buffers, so the exact same code that runs inside the no-CRT ARM64 Windows
 * client (nwoas_client.c) is also compiled and exercised natively on the build
 * host (test_wire.c). It mirrors ../channel.py byte-for-byte.
 *
 * Request  (4096 B, written by the guest to LBA 127):
 *   0  16  magic  "NWOAS-S16x-REQ !"
 *  16  16  token  boot session token
 *  32   4  id     u32 LE, >=1, strictly increasing
 *  36   8  offset u64 LE  (0xFFFF_FFFF_FFFF_FFFF => manifest request)
 *  44   4  length u32 LE  requested payload bytes
 *  48   4  crc32  over bytes[0:48]
 *  52 ..   MUST be zero
 *
 * Response window (LBA 129, up to 16 blocks); 128-byte header then payload:
 *   0  16  magic  "NWOAS-S16x-FILE!"
 *  16  16  token
 *  32   4  id           echo of request id
 *  36   4  status       0=data 1=final-short 2=no-request 3=manifest
 *  40   8  file_size    total artifact size
 *  48   8  offset       echo of request offset
 *  56   4  length       valid payload bytes in this chunk (served)
 *  60   4  payload_crc32 over payload[0:served]
 *  64  32  sha256       full-artifact digest (constant)
 *  96  28  reserved     zero
 * 124   4  header_crc32 over header[0:124]
 * 128 ..   payload
 */
#ifndef NWOAS_WIRE_H
#define NWOAS_WIRE_H

#include <stdint.h>
#include <stddef.h>

#define WIRE_BLOCK          4096u
#define WIRE_REQ_LBA        127u   /* guest WRITE, exactly one block */
#define WIRE_PORT_LBA       128u   /* existing worker mailbox -- read only, never written */
#define WIRE_RESP_LBA       129u   /* guest READ */
#define WIRE_WINDOW_BLOCKS  16u
#define WIRE_WINDOW_BYTES   (WIRE_WINDOW_BLOCKS * WIRE_BLOCK) /* 65536 */
#define WIRE_HEADER         128u
#define WIRE_PAYLOAD_CAP    (WIRE_WINDOW_BYTES - WIRE_HEADER) /* 65408 */
#define WIRE_TOKEN_LEN      16u
#define WIRE_SHA_LEN        32u
#define WIRE_SENTINEL       0xFFFFFFFFFFFFFFFFULL

/* response status codes (must match channel.py) */
#define WIRE_STATUS_DATA      0u
#define WIRE_STATUS_FINAL     1u
#define WIRE_STATUS_NONE      2u
#define WIRE_STATUS_MANIFEST  3u

/* validation results (0 == accepted) */
enum {
    WIRE_OK = 0,
    WIRE_E_SHORT,   /* window shorter than a header */
    WIRE_E_MAGIC,   /* wrong response magic */
    WIRE_E_TOKEN,   /* token mismatch */
    WIRE_E_HDRCRC,  /* header crc over [0:124] mismatch */
    WIRE_E_RSVD,    /* reserved [96:124] not zero */
    WIRE_E_SERVED,  /* served out of range / wrong for status */
    WIRE_E_PAYCRC,  /* payload crc mismatch */
    WIRE_E_ID,      /* id echo mismatch */
    WIRE_E_STATUS,  /* unexpected status */
    WIRE_E_OFFSET,  /* offset echo mismatch */
    WIRE_E_SIZE     /* file_size mismatch / final-short arithmetic */
};

/* CRC-32 (reflected, poly 0xEDB88320, init/xorout 0xFFFFFFFF) == zlib.crc32. */
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

/* Build a fully-formed 4096-byte request block. */
static inline void wire_build_request(uint8_t b[WIRE_BLOCK], const uint8_t token[WIRE_TOKEN_LEN],
                                      uint32_t id, uint64_t offset, uint32_t length)
{
    static const uint8_t M[16] =
        { 'N','W','O','A','S','-','S','1','6','x','-','R','E','Q',' ','!' };
    for (size_t i = 0; i < WIRE_BLOCK; i++) b[i] = 0;
    for (int i = 0; i < 16; i++)            b[i] = M[i];
    for (int i = 0; i < 16; i++)            b[16 + i] = token[i];
    wire_wr32(b + 32, id);
    wire_wr64(b + 36, offset);
    wire_wr32(b + 44, length);
    wire_wr32(b + 48, wire_crc32(b, 48));
}

typedef struct {
    uint32_t       id;
    uint32_t       status;
    uint64_t       file_size;
    uint64_t       offset;
    uint32_t       served;
    uint32_t       payload_crc;
    uint32_t       header_crc;
    const uint8_t *sha256;   /* -> window[64:96]  */
    const uint8_t *payload;  /* -> window[128:128+served] */
} wire_resp;

/* Structural + integrity validation of a response window. Verifies magic,
 * token, header CRC over [0:124], reserved-zero, served range, and payload CRC.
 * Does NOT check id/offset/status expectations -- see wire_check_*.  */
static inline int wire_parse_response(const uint8_t *win, size_t winlen,
                                      const uint8_t token[WIRE_TOKEN_LEN], wire_resp *r)
{
    static const uint8_t M[16] =
        { 'N','W','O','A','S','-','S','1','6','x','-','F','I','L','E','!' };
    if (winlen < WIRE_HEADER)                    return WIRE_E_SHORT;
    for (int i = 0; i < 16; i++) if (win[i] != M[i])       return WIRE_E_MAGIC;
    for (int i = 0; i < 16; i++) if (win[16 + i] != token[i]) return WIRE_E_TOKEN;

    uint32_t hc = wire_rd32(win + 124);
    if (wire_crc32(win, 124) != hc)              return WIRE_E_HDRCRC;
    for (int i = 96; i < 124; i++) if (win[i] != 0) return WIRE_E_RSVD;

    r->id          = wire_rd32(win + 32);
    r->status      = wire_rd32(win + 36);
    r->file_size   = wire_rd64(win + 40);
    r->offset      = wire_rd64(win + 48);
    r->served      = wire_rd32(win + 56);
    r->payload_crc = wire_rd32(win + 60);
    r->header_crc  = hc;
    r->sha256      = win + 64;

    if (r->served > WIRE_PAYLOAD_CAP)            return WIRE_E_SERVED;
    if ((size_t)WIRE_HEADER + r->served > winlen) return WIRE_E_SERVED;
    r->payload = win + WIRE_HEADER;
    if (wire_crc32(r->payload, r->served) != r->payload_crc) return WIRE_E_PAYCRC;
    return WIRE_OK;
}

/* Confirm an already-parsed response is the manifest for request `id`. */
static inline int wire_check_manifest(const wire_resp *r, uint32_t id)
{
    if (r->id != id)                        return WIRE_E_ID;
    if (r->status != WIRE_STATUS_MANIFEST)  return WIRE_E_STATUS;
    if (r->offset != WIRE_SENTINEL)         return WIRE_E_OFFSET;
    if (r->served != 0)                     return WIRE_E_SERVED;
    return WIRE_OK;
}

/* Confirm an already-parsed response is the data chunk we asked for. A DATA
 * status must serve exactly `requested`; a FINAL status must serve fewer bytes
 * AND land exactly on end-of-file. */
static inline int wire_check_data(const wire_resp *r, uint32_t id, uint64_t offset,
                                  uint32_t requested, uint64_t file_size)
{
    if (r->id != id)               return WIRE_E_ID;
    if (r->offset != offset)       return WIRE_E_OFFSET;
    if (r->file_size != file_size) return WIRE_E_SIZE;
    if (offset > file_size || r->served > file_size - offset) return WIRE_E_SIZE;
    if (r->status == WIRE_STATUS_DATA) {
        if (r->served != requested) return WIRE_E_SERVED;
    } else if (r->status == WIRE_STATUS_FINAL) {
        if (r->served >= requested)              return WIRE_E_SERVED;
        if (offset + (uint64_t)r->served != file_size) return WIRE_E_SIZE;
    } else {
        return WIRE_E_STATUS;
    }
    return WIRE_OK;
}

#endif /* NWOAS_WIRE_H */
