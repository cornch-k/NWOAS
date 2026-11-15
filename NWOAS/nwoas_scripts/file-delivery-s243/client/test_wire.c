/* Host unit test for the PURE wire.h / sha256.h helpers compiled into
 * NWGET243.EXE, against vectors generated from the real ../s243_channel.py.
 * In-memory only: passing it says nothing about hardware. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wire.h"
#include "sha256.h"
#include "vectors.h"

static int failures;
static void check(int cond, const char *what)
{
    printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) failures++;
}

static void sha_of(const unsigned char *p, size_t n, size_t step, unsigned char out[32])
{
    sha256_ctx c;
    sha256_init(&c);
    while (n) { size_t k = n < step ? n : step; sha256_update(&c, p, k); p += k; n -= k; }
    sha256_final(&c, out);
}

static void test_sha(void)
{
    unsigned char d[32];
    printf("sha256:\n");
    sha_of((const unsigned char *)"", 0, 1, d);
    check(!memcmp(d, V_SHA_EMPTY, 32), "empty");
    sha_of((const unsigned char *)"abc", 3, 1, d);
    check(!memcmp(d, V_SHA_ABC, 32), "abc");
    unsigned char *m = malloc(1000000);
    memset(m, 'a', 1000000);
    size_t steps[] = {1, 55, 56, 63, 64, 65, 4096, 65408, 1000000};
    int all = 1;
    for (size_t i = 0; i < sizeof steps / sizeof *steps; i++) {
        sha_of(m, 1000000, steps[i], d);
        if (memcmp(d, V_SHA_MILLION_A, 32)) all = 0;
    }
    check(all, "1e6 x 'a' with 9 update chunkings");
    free(m);
}

static void test_none(void)
{
    wire_resp r;
    printf("no-request window:\n");
    check(wire_parse_response(V_NONE, sizeof V_NONE, V_TOKEN, &r) == WIRE_OK, "parse");
    check(r.status == WIRE_STATUS_NONE && r.id == 0 && r.count == 2, "status NONE, id 0, count 2");
}

static void test_manifest(void)
{
    wire_resp r;
    printf("manifest:\n");
    check(wire_parse_response(V_MANIFEST, sizeof V_MANIFEST, V_TOKEN, &r) == WIRE_OK, "parse");
    check(wire_check_manifest(&r, 1u, 1u) == WIRE_OK, "manifest id 1 index 1");
    check(r.file_size == V_FILE_SIZE, "file_size");
    check(!memcmp(r.sha256, V_SHA, 32), "sha256");
    check(r.served == 7 && !memcmp(r.payload, "big.sys", 7), "name payload");
    check(wire_check_manifest(&r, 2u, 1u) == WIRE_E_ID, "wrong id rejected");
    check(wire_check_manifest(&r, 1u, 0u) == WIRE_E_INDEX, "wrong index rejected");
}

static void test_data(void)
{
    wire_resp r;
    unsigned char badsha[32];
    printf("data / final:\n");
    check(wire_parse_response(V_DATA, sizeof V_DATA, V_TOKEN, &r) == WIRE_OK, "parse data");
    check(wire_check_data(&r, 2u, 1u, 0, V_DATA_LEN, V_FILE_SIZE, V_SHA) == WIRE_OK, "full-cap chunk");
    check(wire_check_data(&r, 2u, 0u, 0, V_DATA_LEN, V_FILE_SIZE, V_SHA) == WIRE_E_INDEX, "index mismatch");
    check(wire_check_data(&r, 2u, 1u, 1, V_DATA_LEN, V_FILE_SIZE, V_SHA) == WIRE_E_OFFSET, "offset mismatch");
    memcpy(badsha, V_SHA, 32); badsha[5] ^= 1;
    check(wire_check_data(&r, 2u, 1u, 0, V_DATA_LEN, V_FILE_SIZE, badsha) == WIRE_E_SIZE, "digest change caught");
    check(wire_parse_response(V_FINAL, sizeof V_FINAL, V_TOKEN, &r) == WIRE_OK, "parse final");
    check(wire_check_data(&r, 3u, 1u, V_FINAL_OFF, WIRE_PAYLOAD_CAP, V_FILE_SIZE, V_SHA) == WIRE_OK, "final-short");
    check(r.served == V_FINAL_SERVED && r.status == WIRE_STATUS_FINAL, "served == remainder");
    /* streamed digest over the three chunks the channel would serve */
}

static void test_tamper(void)
{
    static unsigned char buf[WIRE_WINDOW_BYTES];
    wire_resp r;
    unsigned char badtok[16];
    printf("tamper:\n");
    memcpy(buf, V_DATA, sizeof buf); buf[0] ^= 0xFF;
    check(wire_parse_response(buf, sizeof buf, V_TOKEN, &r) == WIRE_E_MAGIC, "magic");
    memcpy(buf, V_DATA, sizeof buf); buf[70] ^= 1;
    check(wire_parse_response(buf, sizeof buf, V_TOKEN, &r) == WIRE_E_HDRCRC, "header crc");
    memcpy(buf, V_DATA, sizeof buf); buf[300] ^= 1;
    check(wire_parse_response(buf, sizeof buf, V_TOKEN, &r) == WIRE_E_PAYCRC, "payload crc");
    memcpy(badtok, V_TOKEN, 16); badtok[3] ^= 1;
    check(wire_parse_response(V_DATA, sizeof V_DATA, badtok, &r) == WIRE_E_TOKEN, "token");
}

static void test_request(void)
{
    unsigned char b[WIRE_BLOCK];
    printf("request builder:\n");
    wire_build_request(b, V_TOKEN, 7u, 5u, 65408ULL, WIRE_PAYLOAD_CAP);
    check(!memcmp(b, V_REQ, WIRE_BLOCK), "byte-identical to s243_channel request");
}

static size_t wlen(const uint16_t *s) { size_t n = 0; while (s[n]) n++; return n; }
static void w(uint16_t *dst, const char *s) { size_t i = 0; for (; s[i]; i++) dst[i] = (uint8_t)s[i]; dst[i] = 0; }

static int target_ok(const char *s) { uint16_t b[400]; w(b, s); return arg_target(b, wlen(b)); }

static void test_args(void)
{
    uint16_t cl[600];
    size_t st[4], ln[4];
    uint32_t ix = 99;
    uint8_t sha[32];
    printf("arguments:\n");
    w(cl, "\"D:\\NWGET243.EXE\"  3 ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789 \"C:\\NWOAS\\drop\\a b.sys\"");
    check(arg_split(cl, st, ln, 4) == 4, "split 4 tokens with quotes");
    check(arg_index(cl + st[1], ln[1], &ix) && ix == 3, "index 3");
    check(arg_sha(cl + st[2], ln[2], sha) && sha[0] == 0xab && sha[31] == 0x89, "sha hex");
    check(arg_target(cl + st[3], ln[3]) && ln[3] == 21, "quoted target with space");
    w(cl, "x 1 2 3 4");
    check(arg_split(cl, st, ln, 4) == -1, "too many tokens");
    w(cl, "x \"unterminated");
    check(arg_split(cl, st, ln, 4) == -1, "unterminated quote");
    w(cl, "x a\"b");
    check(arg_split(cl, st, ln, 4) == -1, "embedded quote");
    w(cl, "64");
    check(!arg_index(cl, 2, &ix), "index 64 rejected");
    w(cl, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg");
    check(!arg_sha(cl, 64, sha), "non-hex rejected");
    check(!arg_sha(cl, 63, sha), "short sha rejected");
    check(target_ok("C:\\NWOAS\\drop\\gpu-j274\\AppleAgxRenderAdmission.sys"), "normal target");
    check(target_ok("c:\\x"), "lower-case drive");
    const char *bad[] = {"D:\\x", "C:x", "C:\\", "C:\\a\\", "C:\\a\\\\b", "C:\\a\\..\\b", "C:\\.\\b",
                         "C:\\a\\b.", "C:\\a\\b ", "C:\\a:b", "C:\\a*b", "C:\\a?b", "C:\\a/b",
                         "C:\\a<b", "C:\\a|b", "\\\\?\\C:\\a", "C:\\a\\..", 0};
    int all = 1;
    for (int i = 0; bad[i]; i++) if (target_ok(bad[i])) { printf("    accepted %s\n", bad[i]); all = 0; }
    check(all, "17 bad targets rejected");
    char longp[260] = "C:\\";
    memset(longp + 3, 'a', 238); longp[241] = 0;
    check(!target_ok(longp), "241-char target rejected");
    longp[240] = 0;
    check(target_ok(longp), "240-char target accepted");
    uint16_t nonascii[] = {'C', ':', '\\', 0xAC00, 0};
    check(!arg_target(nonascii, 4), "non-ASCII rejected");
}

int main(void)
{
    test_sha();
    test_none();
    test_manifest();
    test_data();
    test_tamper();
    test_request();
    test_args();
    if (failures) { printf("\nHOST WIRE TESTS FAILED: %d\n", failures); return 1; }
    printf("\nHOST WIRE TESTS PASSED (pure logic only; no hardware exercised)\n");
    return 0;
}
