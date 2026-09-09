/* S195 owned-file storage integrity benchmark for native Windows ARM64.
 * Writes exactly 256 MiB to a fixed NEW file with NO_BUFFERING|WRITE_THROUGH,
 * flushes, reopens read-only unbuffered and verifies every 64-bit word.
 * No raw disk access, no deletion, no temp path selection. No CRT. */
#include "iocore.h"
typedef void *H;
/* IMP is __declspec(dllimport) for the real ARM64 build. The host mock in
 * wintest.c compiles this same file with -DIMP= and supplies its own bodies. */
#ifndef IMP
#define IMP __declspec(dllimport)
#endif
#define INVALID_HANDLE ((H)(intptr_t)-1)
IMP uint32_t GetFileAttributesW(const uint16_t *);
IMP int GetDiskFreeSpaceExW(const uint16_t *, uint64_t *, uint64_t *, uint64_t *);
IMP H CreateFileW(const uint16_t *, uint32_t, uint32_t, void *, uint32_t, uint32_t, H);
IMP int WriteFile(H, const void *, uint32_t, uint32_t *, void *);
IMP int ReadFile(H, void *, uint32_t, uint32_t *, void *);
IMP int FlushFileBuffers(H);
IMP int GetFileSizeEx(H, int64_t *);
IMP int CloseHandle(H);
IMP uint32_t GetLastError(void);
IMP H VirtualAlloc(H, uint64_t, uint32_t, uint32_t);
IMP int VirtualFree(H, uint64_t, uint32_t);
IMP int QueryPerformanceCounter(int64_t *);
IMP int QueryPerformanceFrequency(int64_t *);
IMP H GetStdHandle(uint32_t);
IMP void ExitProcess(uint32_t);

static const uint16_t PARENT[] = u"C:\\ProgramData\\NWOAS";
static const uint16_t PARENT_SLASH[] = u"C:\\ProgramData\\NWOAS\\";
static const uint16_t PATH[] = u"C:\\ProgramData\\NWOAS\\IO195.DAT";
static const char PATH_A[] = "C:\\ProgramData\\NWOAS\\IO195.DAT";

static H file = INVALID_HANDLE;
static void *mem = 0;
static int64_t hz = 0;

static char *txt(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *num(char *p, uint64_t n) { char b[24]; unsigned i = 0; do { b[i++] = (char)('0' + n % 10); n /= 10; } while (n); while (i) *p++ = b[--i]; return p; }
static void output(char *b, char *p) { uint32_t n = 0; if (!WriteFile(GetStdHandle((uint32_t)-11), b, (uint32_t)(p - b), &n, 0) || n != (uint32_t)(p - b)) ExitProcess(9); }
static char *kv(char *p, const char *key, uint64_t v) { p = txt(p, key); return num(p, v); }
static void line1(const char *tag, const char *key, uint64_t v) { char s[256], *p = txt(s, tag); p = kv(p, key, v); p = txt(p, "\r\n"); output(s, p); }

/* Release everything still held, then exit. Output file is never deleted. */
static void finish(uint32_t code) {
 if (file != INVALID_HANDLE) { if (!CloseHandle(file) && !code) code = 13; file = INVALID_HANDLE; }
 if (mem) { if (!VirtualFree(mem, 0, 0x8000) && !code) code = 14; mem = 0; }
 line1(code ? "S195 FAIL" : "S195 PASS", " exit=", code);
 ExitProcess(code);
}
static void fail(const char *what, uint32_t code) { line1("S195 ERROR ", what, GetLastError()); finish(code); }

static int64_t clock_cb(void *ctx) { (void)ctx; int64_t n = 0; if (!QueryPerformanceCounter(&n)) fail("qpc err=", 8); return n; }
static int write_cb(void *ctx, void *b, uint32_t len, uint32_t *done, uint32_t *err) {
 (void)ctx; int ok = WriteFile(file, b, len, done, 0); if (!ok) *err = GetLastError(); return ok;
}
static int read_cb(void *ctx, void *b, uint32_t len, uint32_t *done, uint32_t *err) {
 (void)ctx; int ok = ReadFile(file, b, len, done, 0); if (!ok) *err = GetLastError(); return ok;
}
static void report(const char *tag, const s195_result *r) {
 char s[320], *p = txt(s, tag);
 p = kv(p, " code=", (uint64_t)r->code); p = kv(p, " bytes=", r->bytes);
 p = kv(p, " offset=", r->offset); p = kv(p, " done=", r->done); p = kv(p, " err=", r->error);
 p = kv(p, " expected=", r->expected); p = kv(p, " actual=", r->actual);
 p = kv(p, " words=", r->words_verified);
 p = kv(p, " elapsed_us=", s195_ticks_to_us(r->elapsed, hz));
 p = txt(p, "\r\n"); output(s, p);
}
/* True if the single shared budget from 'start' has been exceeded now. */
static int expired(int64_t start, int64_t budget) { return s195_elapsed(clock_cb(0), start) > (uint64_t)budget; }

void mainCRTStartup(void) {
 uint64_t avail = 0, total = 0, freed = 0;
 if (!QueryPerformanceFrequency(&hz) || hz <= 0) fail("qpf err=", 2);
 { char s[320], *p = txt(s, "S195 START path="); p = txt(p, PATH_A);
   p = kv(p, " size=", S195_TOTAL_BYTES); p = kv(p, " block=", S195_BLOCK_BYTES);
   p = kv(p, " qpc_hz=", (uint64_t)hz); p = kv(p, " deadline_s=", S195_DEADLINE_SECONDS);
   p = txt(p, "\r\n"); output(s, p); }
 uint32_t attr = GetFileAttributesW(PARENT);
 if (attr == 0xffffffffU || !(attr & 0x10)) fail("parent-missing err=", 3);
 if (!GetDiskFreeSpaceExW(PARENT_SLASH, &avail, &total, &freed)) fail("free-space err=", 4);
 line1("S195 SPACE", " avail_bytes=", avail);
 if (avail < 2 * S195_TOTAL_BYTES) fail("insufficient-space err=", 4); /* 256 MiB file + 256 MiB margin */
 mem = VirtualAlloc(0, S195_BLOCK_BYTES, 0x3000, 4);
 if (!mem) fail("alloc err=", 5);
 if ((uintptr_t)mem & 0xffff) fail("alloc-alignment err=", 5); /* VirtualAlloc granularity; sector aligned */
 /* GENERIC_WRITE, no sharing, CREATE_NEW (fails with 80 if file exists), NO_BUFFERING|WRITE_THROUGH */
 file = CreateFileW(PATH, 0x40000000U, 0, 0, 1, 0x20000000U | 0x80000000U, 0);
 if (file == INVALID_HANDLE) fail("create-new err=", 6);
 /* One shared 5-minute budget from a single start reading, spanning the write
  * loop, flush, reopen and read loop. QPC/frequency products are saturated. */
 int64_t start = clock_cb(0);
 int64_t budget = s195_budget(hz, S195_DEADLINE_SECONDS);
 s195_result w, r;
 s195_run(&w, 0, mem, S195_TOTAL_BYTES, S195_BLOCK_BYTES, write_cb, clock_cb, 0, start, budget);
 int flushed = FlushFileBuffers(file); uint32_t flush_err = flushed ? 0 : GetLastError();
 report("S195 WRITE END", &w);
 if (w.code) finish(7);
 if (!flushed) { line1("S195 ERROR ", "flush err=", flush_err); finish(8); }
 if (expired(start, budget)) { line1("S195 ERROR ", "deadline-after-flush code=", S195_TIMEOUT); finish(15); }
 if (!CloseHandle(file)) { file = INVALID_HANDLE; fail("close err=", 13); }
 file = INVALID_HANDLE;
 if (expired(start, budget)) { line1("S195 ERROR ", "deadline-before-reopen code=", S195_TIMEOUT); finish(15); }
 /* GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NO_BUFFERING */
 file = CreateFileW(PATH, 0x80000000U, 1, 0, 3, 0x20000000U, 0);
 if (file == INVALID_HANDLE) fail("reopen err=", 10);
 int64_t size = 0;
 if (!GetFileSizeEx(file, &size)) fail("size err=", 11);
 if ((uint64_t)size != S195_TOTAL_BYTES) { line1("S195 ERROR ", "size-mismatch bytes=", (uint64_t)size); finish(11); }
 if (expired(start, budget)) { line1("S195 ERROR ", "deadline-before-read code=", S195_TIMEOUT); finish(15); }
 s195_run(&r, 1, mem, S195_TOTAL_BYTES, S195_BLOCK_BYTES, read_cb, clock_cb, 0, start, budget);
 report("S195 READ END", &r);
 if (!s195_pass(&r, S195_TOTAL_BYTES)) finish(12);
 { char s[320], *p = txt(s, "S195 SUMMARY");
   p = kv(p, " qpc_hz=", (uint64_t)hz); p = kv(p, " size=", S195_TOTAL_BYTES);
   p = kv(p, " write_us=", s195_ticks_to_us(w.elapsed, hz));
   p = kv(p, " read_us=", s195_ticks_to_us(r.elapsed, hz));
   p = kv(p, " words_verified=", r.words_verified); p = txt(p, " first_mismatch=none\r\n"); output(s, p); }
 finish(0);
}
