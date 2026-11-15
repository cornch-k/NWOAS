/* S243 native ARM64 Windows no-CRT delivery client.
 *
 *   NWGET243.EXE <index> <sha256-hex> <C:\target\path>
 *
 * Fetches host artifact <index> over the S243 channel in the pre-FAT gap of the
 * transport-s123 NS2 host-RAM image and stores it at <target>, refusing to
 * overwrite. Generalized from file-delivery-s168/client/nwoas_client.c.
 *
 * Raw-disk discipline (unchanged from S168):
 *   - only the disk identified as NS2 (exact length 8448*4096, MBR signature
 *     0x53313233 + 55AA, "NWOAS-S123-LINK!" at LBA 128) is opened for writing;
 *   - the ONLY raw block ever written is LBA 127 (one 4096-B request);
 *   - LBA 128 (worker mailbox) is read for the token, never written;
 *   - responses are read from LBA 129 as one aligned 64 KiB read.
 * The artifact itself is written only through the Windows filesystem on C:.
 *
 * File discipline: <target> must not exist; data goes to <target>.part opened
 * CREATE_NEW. The streamed SHA-256 must equal the job-supplied digest (which
 * must also equal the host manifest digest before any byte is written); the
 * .part is then re-read from disk and hashed again; only then is it renamed
 * with MoveFileExW without MOVEFILE_REPLACE_EXISTING (no overwrite). On any
 * failure after creating the .part, the client deletes its own .part.
 */

typedef unsigned char      U8;
typedef unsigned short     W;
typedef unsigned int       U32;
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
IMP W  *GetCommandLineW(void);
IMP U32 GetFileAttributesW(const W *);
IMP int MoveFileExW(const W *, const W *, U32);
IMP int DeleteFileW(const W *);
IMP U64 GetTickCount64(void);

#include "wire.h"
#include "sha256.h"

#define STD_OUT            ((U32)-11)
#define GENERIC_READ       0x80000000u
#define GENERIC_WRITE      0x40000000u
#define GENERIC_RW         0xC0000000u
#define SHARE_RW           0x00000003u
#define SHARE_READ         0x00000001u
#define OPEN_EXISTING      3u
#define CREATE_NEW         1u
#define FLAG_NO_BUFFERING  0x20000000u
#define FLAG_SEQ_SCAN      0x08000000u
#define MEM_COMMIT_RESERVE 0x00003000u
#define PAGE_RW            0x00000004u
#define IOCTL_DISK_LENGTH  0x0007405Cu
#define INVALID_H          ((H)-1)
#define INVALID_ATTR       0xFFFFFFFFu
#define MOVEFILE_WRITE_THROUGH 0x8u

#define NS2_LENGTH   (8448ULL * 4096ULL)
#define MBR_SIG      0x53313233u
static const U8 PORT_MAGIC[16] = {'N','W','O','A','S','-','S','1','2','3','-','L','I','N','K','!'};

#define FREE_MARGIN   (64ULL * 1024ULL * 1024ULL)
#define PROGRESS_STEP (32ULL * 1024ULL * 1024ULL)
#define MAX_TRIES     3

static U8 *g_win;    /* 65536 response window / readback buffer */
static U8 *g_req;    /* 4096 request block */
static U8 *g_idbuf;  /* 4096 identify scratch */
static U8  g_token[WIRE_TOKEN_LEN];
static H   g_disk = INVALID_H;
static W   g_target[ARG_MAX_PATH + 1];
static W   g_part[ARG_MAX_PATH + 6];
static int g_part_created;

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

static void emit_hex(const U8 *p, U32 n)
{
    static const char D[] = "0123456789abcdef";
    U8 t[2];
    U32 w;
    for (U32 i = 0; i < n; i++) {
        t[0] = (U8)D[p[i] >> 4]; t[1] = (U8)D[p[i] & 15];
        WriteFile(GetStdHandle(STD_OUT), t, 2, &w, 0);
    }
}

static void emit_w(const W *s)
{
    U8 c;
    U32 w;
    for (; *s; s++) { c = (U8)(*s < 0x80 ? *s : '?'); WriteFile(GetStdHandle(STD_OUT), &c, 1, &w, 0); }
}

static void stop(const char *why, U32 code)
{
    U32 err = GetLastError();
    emit("S243 STOP: ");
    emit(why);
    emit(" (code "); emit_u64(code); emit(", lasterror "); emit_u64(err); emit(")\r\n");
    if (g_disk != INVALID_H) CloseHandle(g_disk);
    if (g_part_created) {
        if (DeleteFileW(g_part)) emit("S243 removed own partial file\r\n");
        else emit("S243 WARNING: could not remove own partial file\r\n");
    }
    ExitProcess(code);
}

static int rd_block(H h, U64 at, U8 *dst)
{
    U32 got = 0;
    return SetFilePointerEx(h, (long long)at, 0, 0) &&
           ReadFile(h, dst, WIRE_BLOCK, &got, 0) && got == WIRE_BLOCK;
}

static int identify(H h)
{
    U64 length = 0;
    U32 got = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_LENGTH, 0, 0, &length, 8, &got, 0) ||
        got != 8 || length != NS2_LENGTH)
        return 0;
    if (!rd_block(h, 0, g_idbuf) || wire_rd32(g_idbuf + 440) != MBR_SIG ||
        g_idbuf[510] != 0x55 || g_idbuf[511] != 0xAA)
        return 0;
    if (!rd_block(h, (U64)WIRE_PORT_LBA * WIRE_BLOCK, g_idbuf))
        return 0;
    for (int i = 0; i < 16; i++)
        if (g_idbuf[i] != PORT_MAGIC[i]) return 0;
    return 1;
}

static int put_request(U32 id, U32 index, U64 offset, U32 length)
{
    U32 wr = 0;
    wire_build_request(g_req, g_token, id, index, offset, length);
    return SetFilePointerEx(g_disk, (long long)WIRE_REQ_LBA * WIRE_BLOCK, 0, 0) &&
           WriteFile(g_disk, g_req, WIRE_BLOCK, &wr, 0) && wr == WIRE_BLOCK;
}

static int get_window(void)
{
    U32 got = 0;
    return SetFilePointerEx(g_disk, (long long)WIRE_RESP_LBA * WIRE_BLOCK, 0, 0) &&
           ReadFile(g_disk, g_win, WIRE_WINDOW_BYTES, &got, 0) && got == WIRE_WINDOW_BYTES;
}

/* One request/response; identical request retried up to MAX_TRIES (idempotent). */
static int exchange(wire_resp *r, U32 id, U32 index, U64 offset, U32 length,
                    U64 size, const U8 *sha)
{
    for (int attempt = 0; attempt < MAX_TRIES; attempt++) {
        if (attempt) Sleep(100);
        if (!put_request(id, index, offset, length)) continue;
        if (!get_window()) continue;
        if (wire_parse_response(g_win, WIRE_WINDOW_BYTES, g_token, r) != WIRE_OK) continue;
        int rc = offset == WIRE_SENTINEL ? wire_check_manifest(r, id, index)
                                         : wire_check_data(r, id, index, offset, length, size, sha);
        if (rc == WIRE_OK) return 1;
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

static int same(const U8 *a, const U8 *b, U32 n)
{
    for (U32 i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

void mainCRTStartup(void)
{
    static const W disk_path[] = L"\\\\.\\PhysicalDrive0";
    static const W root_path[] = L"C:\\";
    static const W part_ext[] = L".part";
    W path[20] = {0};
    size_t st[4], ln[4];
    U32 index = 0;
    U8 want[WIRE_SHA_LEN], got_sha[WIRE_SHA_LEN];

    emit("NWOAS S243 delivery client\r\n");
    const W *cl = GetCommandLineW();
    if (arg_split((const uint16_t *)cl, st, ln, 4) != 4 ||
        !arg_index((const uint16_t *)cl + st[1], ln[1], &index) ||
        !arg_sha((const uint16_t *)cl + st[2], ln[2], want) ||
        !arg_target((const uint16_t *)cl + st[3], ln[3]))
        stop("usage: NWGET243.EXE <index 0-63> <sha256 64 hex> <C:\\absolute\\target>", 2);
    for (size_t i = 0; i < ln[3]; i++) g_target[i] = cl[st[3] + i];
    g_target[ln[3]] = 0;
    for (size_t i = 0; i < ln[3]; i++) g_part[i] = g_target[i];
    for (size_t i = 0; i < 6; i++) g_part[ln[3] + i] = part_ext[i];

    emit("S243 target "); emit_w(g_target); emit("\r\n");
    if (GetFileAttributesW(g_target) != INVALID_ATTR)
        stop("target already exists; refusing to overwrite", 3);
    if (GetFileAttributesW(g_part) != INVALID_ATTR)
        stop("target.part already exists; refusing to reuse", 3);

    g_win   = VirtualAlloc(0, WIRE_WINDOW_BYTES, MEM_COMMIT_RESERVE, PAGE_RW);
    g_req   = VirtualAlloc(0, WIRE_BLOCK, MEM_COMMIT_RESERVE, PAGE_RW);
    g_idbuf = VirtualAlloc(0, WIRE_BLOCK, MEM_COMMIT_RESERVE, PAGE_RW);
    if (!g_win || !g_req || !g_idbuf) stop("VirtualAlloc failed", 5);

    for (int i = 0; i < 20 && disk_path[i]; i++) path[i] = disk_path[i];
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
        if (!identify(h) || !same(g_idbuf + 16, g_token, 16)) { CloseHandle(h); continue; }
        g_disk = h;
        break;
    }
    if (g_disk == INVALID_H) stop("NS2 host virtual disk not found; nothing written", 6);

    /* Learn the channel's last accepted id (ids are global and monotonic). */
    wire_resp r;
    if (!get_window() || wire_parse_response(g_win, WIRE_WINDOW_BYTES, g_token, &r) != WIRE_OK)
        stop("S243 channel not present on NS2 (launcher without delivery module?)", 7);
    U32 id = r.id + 1;
    if (index >= r.count) stop("artifact index out of range", 7);

    if (!exchange(&r, id, index, WIRE_SENTINEL, 0, 0, 0)) stop("manifest request failed validation", 7);
    id++;
    U64 size = r.file_size;
    U8 sha[WIRE_SHA_LEN];
    for (U32 j = 0; j < WIRE_SHA_LEN; j++) sha[j] = r.sha256[j];
    emit("S243 manifest index "); emit_u64(index); emit(" of "); emit_u64(r.count);
    emit(" name "); {
        U8 nm[WIRE_MAX_NAME + 1];
        U32 k;
        for (k = 0; k < r.served; k++) nm[k] = (r.payload[k] >= 0x20 && r.payload[k] < 0x7f) ? r.payload[k] : '?';
        nm[k] = 0;
        emit((const char *)nm);
    }
    emit(" bytes "); emit_u64(size); emit(" sha256 "); emit_hex(sha, WIRE_SHA_LEN); emit("\r\n");
    if (!same(sha, want, WIRE_SHA_LEN)) stop("host manifest SHA-256 differs from job SHA-256", 7);

    U64 avail = 0;
    if (!GetDiskFreeSpaceExW(root_path, &avail, 0, 0)) stop("GetDiskFreeSpaceExW failed", 4);
    if (avail < size + FREE_MARGIN) stop("insufficient free space on C:", 4);

    H f = CreateFileW(g_part, GENERIC_WRITE, 0, 0, CREATE_NEW, FLAG_SEQ_SCAN, 0);
    if (f == INVALID_H) stop("cannot create target.part (exists, or parent directory missing)", 8);
    g_part_created = 1;

    U64 t0 = GetTickCount64();
    sha256_ctx ctx;
    sha256_init(&ctx);
    U64 offset = 0, next_mark = PROGRESS_STEP;
    while (offset < size) {
        if (!exchange(&r, id, index, offset, WIRE_PAYLOAD_CAP, size, sha)) {
            CloseHandle(f);
            stop("data chunk failed validation after retries", 9);
        }
        if (r.served) {
            sha256_update(&ctx, r.payload, r.served);
            if (!write_all(f, r.payload, r.served)) { CloseHandle(f); stop("WriteFile failed", 10); }
        }
        offset += r.served;
        id++;
        if (offset >= next_mark) {
            emit("S243 progress "); emit_u64(offset); emit(" / "); emit_u64(size); emit("\r\n");
            while (offset >= next_mark) next_mark += PROGRESS_STEP;
        }
        if (r.status == WIRE_STATUS_FINAL) break;
    }
    U64 t1 = GetTickCount64();
    if (offset != size) { CloseHandle(f); stop("short transfer", 11); }
    if (!FlushFileBuffers(f)) { CloseHandle(f); stop("FlushFileBuffers failed", 12); }
    CloseHandle(f);
    CloseHandle(g_disk);
    g_disk = INVALID_H;
    sha256_final(&ctx, got_sha);
    if (!same(got_sha, want, WIRE_SHA_LEN)) stop("streamed SHA-256 mismatch", 13);

    /* Re-read the written file through the filesystem and hash it again. */
    f = CreateFileW(g_part, GENERIC_READ, SHARE_READ, 0, OPEN_EXISTING, FLAG_SEQ_SCAN, 0);
    if (f == INVALID_H) stop("cannot reopen target.part for readback", 14);
    sha256_init(&ctx);
    U64 back = 0;
    for (;;) {
        U32 n = 0;
        if (!ReadFile(f, g_win, WIRE_WINDOW_BYTES, &n, 0)) { CloseHandle(f); stop("readback ReadFile failed", 14); }
        if (!n) break;
        sha256_update(&ctx, g_win, n);
        back += n;
    }
    CloseHandle(f);
    sha256_final(&ctx, got_sha);
    if (back != size || !same(got_sha, want, WIRE_SHA_LEN)) stop("readback SHA-256/size mismatch", 14);
    U64 t2 = GetTickCount64();

    if (!MoveFileExW(g_part, g_target, MOVEFILE_WRITE_THROUGH))
        stop("rename target.part -> target failed (target appeared?)", 15);
    g_part_created = 0;

    emit("S243 OK index "); emit_u64(index); emit(" bytes "); emit_u64(size);
    emit(" sha256 "); emit_hex(got_sha, WIRE_SHA_LEN);
    emit(" transfer_ms "); emit_u64(t1 - t0); emit(" readback_ms "); emit_u64(t2 - t1);
    emit(" target "); emit_w(g_target); emit("\r\n");
    ExitProcess(0);
}
