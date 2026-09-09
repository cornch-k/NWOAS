/* Host-side unit test for the PURE wire.h helpers. This is the same code the
 * no-CRT client compiles, exercised natively against golden vectors generated
 * from the real ../channel.py (vectors.h). It performs NO I/O to any device --
 * it only validates in-memory buffers -- so passing it says nothing about
 * hardware; it only proves the response-validation logic matches channel.py. */
#include <stdio.h>
#include <string.h>
#include "wire.h"
#include "vectors.h"

static int failures;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

/* A known CRC-32 anchor: zlib.crc32(b"123456789") == 0xCBF43926. */
static void test_crc(void)
{
    printf("crc32:\n");
    check(wire_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u,
          "crc32(\"123456789\") == 0xCBF43926");
}

static void test_manifest(void)
{
    wire_resp r;
    printf("manifest:\n");
    check(wire_parse_response(V_MANIFEST, sizeof V_MANIFEST, V_TOKEN, &r) == WIRE_OK,
          "parse manifest window");
    check(wire_check_manifest(&r, 1u) == WIRE_OK, "manifest is for id 1");
    check(r.file_size == V_FILE_SIZE, "manifest file_size matches channel.py");
    check(memcmp(r.sha256, V_SHA, WIRE_SHA_LEN) == 0, "manifest sha256 matches channel.py");
    check(wire_check_manifest(&r, 2u) == WIRE_E_ID, "wrong id rejected");
}

static void test_data(void)
{
    wire_resp r;
    printf("data chunk:\n");
    check(wire_parse_response(V_DATA, sizeof V_DATA, V_TOKEN, &r) == WIRE_OK,
          "parse data window");
    check(wire_check_data(&r, 2u, V_DATA_OFF, V_DATA_LEN, V_FILE_SIZE) == WIRE_OK,
          "full-cap chunk validates");
    check(r.status == WIRE_STATUS_DATA, "status == DATA");
    check(r.served == V_DATA_LEN, "served == requested (full cap)");
    check(wire_check_data(&r, 2u, V_DATA_OFF + 1, V_DATA_LEN, V_FILE_SIZE) == WIRE_E_OFFSET,
          "offset mismatch rejected");
    check(wire_check_data(&r, 99u, V_DATA_OFF, V_DATA_LEN, V_FILE_SIZE) == WIRE_E_ID,
          "id mismatch rejected");
}

static void test_final(void)
{
    wire_resp r;
    printf("final-short chunk:\n");
    check(wire_parse_response(V_FINAL, sizeof V_FINAL, V_TOKEN, &r) == WIRE_OK,
          "parse final window");
    check(wire_check_data(&r, 3u, V_FINAL_OFF, V_FINAL_LEN, V_FILE_SIZE) == WIRE_OK,
          "final-short chunk validates");
    check(r.status == WIRE_STATUS_FINAL, "status == FINAL");
    check(r.served == V_FINAL_SERVED, "served == remainder");
    check(V_FINAL_OFF + (unsigned long long)r.served == V_FILE_SIZE, "final lands on EOF");
}

static void test_tamper(void)
{
    unsigned char buf[WIRE_WINDOW_BYTES];
    wire_resp r;
    unsigned char badtok[WIRE_TOKEN_LEN];
    printf("tamper detection:\n");

    memcpy(buf, V_DATA, sizeof buf);
    buf[0] ^= 0xFF;  /* corrupt magic */
    check(wire_parse_response(buf, sizeof buf, V_TOKEN, &r) == WIRE_E_MAGIC, "bad magic caught");

    memcpy(buf, V_DATA, sizeof buf);
    buf[70] ^= 0x01; /* flip a header byte -> header CRC must fail */
    check(wire_parse_response(buf, sizeof buf, V_TOKEN, &r) == WIRE_E_HDRCRC, "header CRC caught");

    memcpy(buf, V_DATA, sizeof buf);
    buf[200] ^= 0x01; /* flip a payload byte -> payload CRC must fail */
    check(wire_parse_response(buf, sizeof buf, V_TOKEN, &r) == WIRE_E_PAYCRC, "payload CRC caught");

    memcpy(badtok, V_TOKEN, sizeof badtok);
    badtok[0] ^= 0xFF;
    check(wire_parse_response(V_DATA, sizeof V_DATA, badtok, &r) == WIRE_E_TOKEN, "token caught");
}

static void test_build_request(void)
{
    unsigned char b[WIRE_BLOCK];
    printf("request builder:\n");
    wire_build_request(b, V_TOKEN, 7u, 65408ULL, WIRE_PAYLOAD_CAP);
    check(wire_crc32(b, 48) == wire_rd32(b + 48), "request self CRC over [0:48]");
    check(wire_rd32(b + 32) == 7u, "id encoded");
    check(wire_rd64(b + 36) == 65408ULL, "offset encoded");
    check(wire_rd32(b + 44) == WIRE_PAYLOAD_CAP, "length encoded");
    int tail_zero = 1;
    for (unsigned i = 52; i < WIRE_BLOCK; i++) if (b[i]) tail_zero = 0;
    check(tail_zero, "tail [52:] is zero");
}

int main(void)
{
    test_crc();
    test_manifest();
    test_data();
    test_final();
    test_tamper();
    test_build_request();
    if (failures) {
        printf("\nHOST WIRE TESTS FAILED: %d\n", failures);
        return 1;
    }
    printf("\nHOST WIRE TESTS PASSED (pure logic only; no hardware exercised)\n");
    return 0;
}
