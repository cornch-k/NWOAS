/* S168 native ARM64 Windows no-CRT delivery client.
 *
 * Downloads ONE known, immutable host artifact -- the official Cinebench zip --
 * over the S168 delivery channel that lives in the pre-FAT gap of the existing
 * transport-s123 NS2 host-RAM image. Raw writes are limited to the registered host-RAM virtual disk.
 * The downloaded file is created through the Windows filesystem on C:.
 * It reuses the NWOS worker session token without changing its sequence.
 *
 * Strict block discipline:
 *   - The ONLY block ever written is LBA 127 (request port), exactly one 4096-B
 *     block per request. LBA 128 (the worker's job/stdout mailbox and its ACK
 *     sequence) is READ for the session token but NEVER written.
 *   - Responses are READ from LBA 129 as a single aligned 64 KiB request.
 *   - The raw disk is opened with FILE_FLAG_NO_BUFFERING and page-aligned
 *     VirtualAlloc buffers; the session token is re-verified after the
 *     read/write reopen.
 *
 * Integrity: every response is validated for magic, token, header CRC over
 * [0:124], payload CRC, and echoed id/offset/status/file_size (see wire.h).
 * The manifest's advertised size and SHA-256 are matched against the known
 * constants below before a single byte is written. NOTE: per-chunk CRCs prove
 * transport integrity ONLY -- they are NOT a full-file hash. The authoritative
 * SHA-256 of the completed file and the rename to the final .ZIP are performed
 * by a separate PowerShell step after this program exits with code 0.
 *
 * Destination: C:\NWOAS-BENCH\CINEBEN.ZIP.part, opened CREATE_NEW (never
 * overwrites, never resumes). The folder is pre-created by the wrapper.
 */

typedef unsigned char      U8;
typedef unsigned short     W;   /* UTF-16 code unit */
typedef unsigned int       U32; /* 32-bit on Windows LLP64 */
typedef unsigned long long U64;
typedef void              *H;

#define IMP __declspec(dllimport)
IMP H   GetStdHandle(U32);
IMP int WriteFile(H, const void *, U32, U32 *, void *);
IMP int ReadFile(H, void *, U32, U32 *, void *);
IMP H   CreateFileW(const W *, U32, U32, void *, U32, U32, H);
IMP int SetFilePointerEx(H, long long, long long *, U32);
IMP int DeviceIoControl(H, U32, void *, U32, void *, U32, U32 *, void *);
IMP int CloseHandle(H);
IMP H   VirtualAlloc(H, U64, U32, U32);
IMP void ExitProcess(U32);
IMP void Sleep(U32);
IMP U32 GetLastError(void);
IMP int FlushFileBuffers(H);
IMP int GetDiskFreeSpaceExW(const W *, U64 *, U64 *, U64 *);

#include "wire.h"

/* Win32 constants (only what we use). */
#define STD_OUT            ((U32)-11)
#define GENERIC_READ       0x80000000u
#define GENERIC_WRITE      0x40000000u
#define GENERIC_RW         0xC0000000u
#define SHARE_RW           0x00000003u
#define OPEN_EXISTING      3u
#define CREATE_NEW         1u
#define FLAG_NO_BUFFERING  0x20000000u
#define FLAG_SEQ_SCAN      0x08000000u
#define MEM_COMMIT_RESERVE 0x00003000u
#define PAGE_RW            0x00000004u
#define IOCTL_DISK_LENGTH  0x0007405Cu
#define INVALID_H          ((H)-1)

/* NS2 identity, mirroring native-link-s125/nwos.c. */
#define NS2_LENGTH   (8448ULL * 4096ULL)
#define MBR_SIG      0x53313233u                     /* bytes at MBR[440:444] */
static const U8 PORT_MAGIC[16] = "NWOAS-S123-LINK!"; /* LBA 128 session frame */

/* The one artifact we will accept: the official Cinebench zip. */
#define EXPECTED_SIZE 783373346ULL
static const U8 EXPECTED_SHA[WIRE_SHA_LEN] = {
    0xcb,0x6c,0x76,0x5f,0x80,0xd5,0x3e,0x1f, 0xe7,0x02,0xde,0x14,0x5b,0x6d,0xa1,0xc6,
    0x7a,0xf5,0xb3,0x7d,0x5a,0x39,0x9c,0x8f, 0x33,0x96,0xa7,0xec,0xfe,0xd7,0x81,0x59
};

#define FREE_MARGIN   (512ULL * 1024ULL * 1024ULL)   /* require zip + 512 MiB */
#define PROGRESS_STEP (32ULL * 1024ULL * 1024ULL)    /* stdout note per 32 MiB */
#define MAX_TRIES     3                              /* identical-request retries */

/* Page-aligned raw-I/O buffers (VirtualAlloc is page aligned -> valid for
 * FILE_FLAG_NO_BUFFERING). */
static U8 *g_win;    /* 65536: response window read from LBA 129 */
static U8 *g_req;    /* 4096: request block written to LBA 127   */
static U8 *g_idbuf;  /* 4096: identify scratch (LBA 0 and LBA 128) */
static U8  g_token[WIRE_TOKEN_LEN];
static H   g_disk = INVALID_H;

static void emit(const char *s)
{
    U32 n = 0, w;
    while (s[n]) n++;
    WriteFile(GetStdHandle(STD_OUT), s, n, &w, 0);
}

static void emit_u64(U64 v)
{
    U8 t[24];
    int i = 24;
    if (v == 0) t[--i] = '0';
    while (v) { t[--i] = (U8)('0' + (v % 10)); v /= 10; }
    U32 w;
    WriteFile(GetStdHandle(STD_OUT), t + i, (U32)(24 - i), &w, 0);
}

static void stop(const char *why, U32 code)
{
    emit("STOP: ");
    emit(why);
    emit("\r\n");
    if (g_disk != INVALID_H) CloseHandle(g_disk);
    ExitProcess(code);
}

/* Read exactly one 4096-byte block at byte offset `at` into `dst`. */
static int rd_block(H h, U64 at, U8 *dst)
{
    U32 got = 0;
    return SetFilePointerEx(h, (long long)at, 0, 0) &&
           ReadFile(h, dst, WIRE_BLOCK, &got, 0) && got == WIRE_BLOCK;
}

/* Confirm a handle points at the S168/S123 NS2 host-RAM image. */
static int identify(H h)
{
    U64 length = 0;
    U32 got = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_LENGTH, 0, 0, &length, 8, &got, 0) ||
        got != 8 || length != NS2_LENGTH)
        return 0;
    if (!rd_block(h, 0, g_idbuf) ||
        wire_rd32(g_idbuf + 440) != MBR_SIG ||
        g_idbuf[510] != 0x55 || g_idbuf[511] != 0xAA)
        return 0;
    if (!rd_block(h, (U64)WIRE_PORT_LBA * WIRE_BLOCK, g_idbuf))
        return 0;
    for (int i = 0; i < 16; i++)
        if (g_idbuf[i] != PORT_MAGIC[i]) return 0;
    return 1;
}

/* Write one request block to LBA 127 -- the ONLY block this program writes. */
static int put_request(U32 id, U64 offset, U32 length)
{
    U32 wr = 0;
    wire_build_request(g_req, g_token, id, offset, length);
    return SetFilePointerEx(g_disk, (long long)WIRE_REQ_LBA * WIRE_BLOCK, 0, 0) &&
           WriteFile(g_disk, g_req, WIRE_BLOCK, &wr, 0) && wr == WIRE_BLOCK;
}

/* Read the full 64 KiB response window from LBA 129 in one aligned request. */
static int get_window(void)
{
    U32 got = 0;
    return SetFilePointerEx(g_disk, (long long)WIRE_RESP_LBA * WIRE_BLOCK, 0, 0) &&
           ReadFile(g_disk, g_win, WIRE_WINDOW_BYTES, &got, 0) &&
           got == WIRE_WINDOW_BYTES;
}

/* Exchange one request/response, retrying the IDENTICAL request up to MAX_TRIES
 * (idempotent per channel.py). Returns 1 and fills `r` on the first response
 * that both parses and passes `data_mode` expectation, else 0. */
static int exchange(wire_resp *r, U32 id, U64 offset, U32 length, int data_mode)
{
    for (int attempt = 0; attempt < MAX_TRIES; attempt++) {
        if (attempt) Sleep(100);
        if (!put_request(id, offset, length)) continue;
        if (!get_window())                    continue;
        if (wire_parse_response(g_win, WIRE_WINDOW_BYTES, g_token, r) != WIRE_OK)
            continue;
        int rc = data_mode
               ? wire_check_data(r, id, offset, length, EXPECTED_SIZE)
               : wire_check_manifest(r, id);
        if (rc == WIRE_OK) {
            int same_sha = 1;
            for (int j = 0; j < 32; j++)
                if (r->sha256[j] != EXPECTED_SHA[j]) same_sha = 0;
            if (same_sha) return 1;
        }
    }
    return 0;
}

static int write_all(H f, const U8 *p, U32 n)
{
    U32 done = 0, w;
    while (done < n) {
        if (!WriteFile(f, p + done, n - done, &w, 0) || w == 0) return 0;
        done += w;
    }
    return 1;
}

void mainCRTStartup(void)
{
    static const W disk_path[] = L"\\\\.\\PhysicalDrive0";
    static const W dest_path[] = L"C:\\NWOAS-BENCH\\CINEBEN.ZIP.part";
    static const W root_path[] = L"C:\\";
    W path[20] = {0};

    emit("NWOAS S168 delivery client: single artifact over NS2 gap.\r\n");

    /* Free-space guard before any device work: need zip + 512 MiB free. */
    U64 avail = 0;
    if (!GetDiskFreeSpaceExW(root_path, &avail, 0, 0))
        stop("GetDiskFreeSpaceExW failed", 4);
    if (avail < EXPECTED_SIZE + FREE_MARGIN) {
        emit("free bytes = "); emit_u64(avail);
        emit(", need = ");     emit_u64(EXPECTED_SIZE + FREE_MARGIN); emit("\r\n");
        stop("insufficient free space on C:", 4);
    }

    g_win   = VirtualAlloc(0, WIRE_WINDOW_BYTES, MEM_COMMIT_RESERVE, PAGE_RW);
    g_req   = VirtualAlloc(0, WIRE_BLOCK,        MEM_COMMIT_RESERVE, PAGE_RW);
    g_idbuf = VirtualAlloc(0, WIRE_BLOCK,        MEM_COMMIT_RESERVE, PAGE_RW);
    if (!g_win || !g_req || !g_idbuf)
        stop("VirtualAlloc failed", 5);

    for (int i = 0; i < 20 && disk_path[i]; i++) path[i] = disk_path[i];

    /* Locate the NS2 image: identify read-only, capture the token, then reopen
     * read/write with NO_BUFFERING and re-verify the token. */
    for (int i = 0; i < 10; i++) {
        path[17] = (W)('0' + i);
        H h = CreateFileW(path, GENERIC_READ, SHARE_RW, 0, OPEN_EXISTING, FLAG_NO_BUFFERING, 0);
        if (h == INVALID_H) continue;
        int ok = identify(h);
        if (ok) for (int j = 0; j < 16; j++) g_token[j] = g_idbuf[16 + j];
        CloseHandle(h);
        if (!ok) continue;

        h = CreateFileW(path, GENERIC_RW, SHARE_RW, 0, OPEN_EXISTING, FLAG_NO_BUFFERING, 0);
        if (h == INVALID_H) continue;
        if (!identify(h)) { CloseHandle(h); continue; }
        int same = 1;
        for (int j = 0; j < 16; j++) if (g_idbuf[16 + j] != g_token[j]) same = 0;
        if (!same) { CloseHandle(h); continue; }
        g_disk = h;
        break;
    }
    if (g_disk == INVALID_H)
        stop("NS2 host virtual disk not found; nothing written", 6);

    /* Manifest (id 1, SENTINEL offset, length 0): learn size + digest, then
     * confirm they are the known official Cinebench zip. */
    wire_resp r;
    if (!exchange(&r, 1u, WIRE_SENTINEL, 0u, 0))
        stop("manifest request failed validation", 7);
    if (r.file_size != EXPECTED_SIZE)
        stop("manifest size does not match known Cinebench zip", 7);
    for (int j = 0; j < (int)WIRE_SHA_LEN; j++)
        if (r.sha256[j] != EXPECTED_SHA[j])
            stop("manifest SHA-256 does not match known Cinebench zip", 7);
    emit("manifest OK: size "); emit_u64(EXPECTED_SIZE);
    emit(" bytes, known digest matched.\r\n");

    /* Open the destination with CREATE_NEW: refuse to overwrite or resume. */
    H f = CreateFileW(dest_path, GENERIC_WRITE, 0, 0, CREATE_NEW, FLAG_SEQ_SCAN, 0);
    if (f == INVALID_H) {
        emit("CreateFileW(CINEBEN.ZIP.part) failed, GetLastError = ");
        emit_u64(GetLastError()); emit("\r\n");
        stop("destination exists or cannot be created", 8);
    }

    /* Monotonic full-cap requests; the channel returns a final-short chunk. */
    U64 offset = 0, next_mark = PROGRESS_STEP;
    U32 id = 2u;
    while (offset < EXPECTED_SIZE) {
        if (!exchange(&r, id, offset, WIRE_PAYLOAD_CAP, 1)) {
            CloseHandle(f);
            stop("data chunk failed validation after retries", 9);
        }
        if (r.served && !write_all(f, r.payload, r.served)) {
            CloseHandle(f);
            stop("WriteFile to destination failed", 10);
        }
        offset += r.served;
        id++;
        if (offset >= next_mark || offset == EXPECTED_SIZE) {
            emit("S168 progress: "); emit_u64(offset);
            emit(" / ");             emit_u64(EXPECTED_SIZE); emit(" bytes\r\n");
            while (offset >= next_mark) next_mark += PROGRESS_STEP;
        }
        if (r.status == WIRE_STATUS_FINAL) break;
    }

    if (offset != EXPECTED_SIZE) {
        CloseHandle(f);
        stop("short download: bytes written != known size", 11);
    }
    if (!FlushFileBuffers(f)) {
        CloseHandle(f);
        stop("FlushFileBuffers failed", 12);
    }
    CloseHandle(f);
    CloseHandle(g_disk);
    g_disk = INVALID_H;

    emit("S168 done: "); emit_u64(EXPECTED_SIZE);
    emit(" bytes written to C:\\NWOAS-BENCH\\CINEBEN.ZIP.part\r\n");
    emit("NOTE: per-chunk header/payload CRCs verify transport integrity only; "
         "they are NOT a full-file hash.\r\n");
    emit("NOTE: full-file SHA-256 verification and rename to the final .ZIP are "
         "done by the separate PowerShell step after exit 0.\r\n");
    ExitProcess(0);
}
