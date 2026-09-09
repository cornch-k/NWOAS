/* Host mock of the S195 Win32 wrapper. Compiles the REAL iotest.c with -DIMP=
 * and supplies stub kernel32 bodies plus a fault-injection scenario driver, so
 * the actual CreateFileW/WriteFile/ReadFile/FlushFileBuffers/CloseHandle path,
 * flags, ordering and cleanup are exercised without touching a real disk or the
 * Mini. Runs each scenario in a fresh setjmp context because ExitProcess never
 * returns in the real wrapper. Built plain and with ASan/UBSan by test_host.py.
 *
 * wchar handling: iotest.c uses uint16_t for the wide APIs and u"..." char16_t
 * literals. char16_t is uint_least16_t (== uint16_t here), so the arrays and
 * the stub prototypes line up; the wide path is compared word for word. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>

/* ---- fixed wide path we require the wrapper to use, nothing else ---- */
static const uint16_t EXPECT_PATH[]   = u"C:\\ProgramData\\NWOAS\\IO195.DAT";
static const uint16_t EXPECT_PARENT[] = u"C:\\ProgramData\\NWOAS";
static const uint16_t EXPECT_PSLASH[] = u"C:\\ProgramData\\NWOAS\\";

#define TOTAL 268435456ULL
#define BLOCK 1048576ULL
/* opaque handle tokens the stubs hand back */
#define WH ((void *)(intptr_t)0x100) /* write open */
#define RH ((void *)(intptr_t)0x200) /* read open */
#define SH ((void *)(intptr_t)0x300) /* std output */

/* ---- scenario knobs, set by the driver before each mainCRTStartup run ---- */
static uint32_t k_parent_attr;      /* GetFileAttributesW return */
static uint64_t k_free_avail;       /* GetDiskFreeSpaceExW available bytes */
static int      k_create_new_fails; /* first CreateFileW refuses (file exists) */
static uint32_t k_create_new_err;
static int      k_reopen_fails;     /* second CreateFileW fails */
static int      k_flush_fails;      /* FlushFileBuffers fails */
static uint64_t k_short_at;         /* byte offset at which WriteFile is short */
static uint32_t k_short_len;        /* bytes reported for that short write (< BLOCK) */
static int      k_short_armed;

/* ---- observed effects ---- */
static uint32_t o_lasterr;
static int      o_create_calls;
static uint32_t o_cf_access[4], o_cf_share[4], o_cf_disp[4], o_cf_flags[4];
static int      o_path_ok;          /* every CreateFileW saw the exact fixed path */
static int      o_parent_ok;        /* attr + free-space used exact parent paths */
static uint64_t o_file_writes;      /* bytes written to the backing store */
static int      o_flush_calls;
static int      o_closed_wh, o_closed_rh;
static int      o_valloc_calls, o_vfree_calls;
static void    *o_valloc_ptr;
static uint64_t o_pos;              /* current file position in the backing store */
static uint32_t o_exit;             /* code passed to ExitProcess */

static unsigned char *g_store;      /* 256 MiB shared backing store */
static jmp_buf g_jmp;

static int wideq(const uint16_t *a, const uint16_t *b) {
 for (size_t i = 0;; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; }
}

/* iotest.c declares these with IMP expanded to nothing; define them here. */
#define IMP
#include "iotest.c"

/* ---- stub kernel32 bodies ---- */
uint32_t GetLastError(void) { return o_lasterr; }

uint32_t GetFileAttributesW(const uint16_t *p) {
 if (!wideq(p, EXPECT_PARENT)) o_parent_ok = 0;
 return k_parent_attr;
}
int GetDiskFreeSpaceExW(const uint16_t *p, uint64_t *avail, uint64_t *total, uint64_t *freed) {
 if (!wideq(p, EXPECT_PSLASH)) o_parent_ok = 0;
 if (avail) *avail = k_free_avail;
 if (total) *total = k_free_avail;
 if (freed) *freed = k_free_avail;
 return 1;
}
H CreateFileW(const uint16_t *p, uint32_t access, uint32_t share, void *sa,
              uint32_t disp, uint32_t flags, H tmpl) {
 (void)sa; (void)tmpl;
 if (!wideq(p, EXPECT_PATH)) o_path_ok = 0;
 int i = o_create_calls < 4 ? o_create_calls : 3;
 o_cf_access[i] = access; o_cf_share[i] = share; o_cf_disp[i] = disp; o_cf_flags[i] = flags;
 int first = (o_create_calls == 0);
 o_create_calls++;
 if (first) {
  if (k_create_new_fails) { o_lasterr = k_create_new_err; return INVALID_HANDLE; }
  o_pos = 0; return WH;
 }
 if (k_reopen_fails) { o_lasterr = 10; return INVALID_HANDLE; }
 o_pos = 0; return RH;
}
int WriteFile(H h, const void *b, uint32_t n, uint32_t *done, void *ov) {
 (void)ov;
 if (h == SH) { if (done) *done = n; return 1; }   /* console output, accepted */
 if (h != WH) { o_lasterr = 6; return 0; }
 uint32_t m = n;
 if (k_short_armed && o_pos == k_short_at) { m = k_short_len; k_short_armed = 0; }
 if (o_pos + m > TOTAL) { o_lasterr = 112; return 0; }
 memcpy(g_store + o_pos, b, m); o_pos += m; o_file_writes += m;
 if (done) *done = m;
 return 1;
}
int ReadFile(H h, void *b, uint32_t n, uint32_t *done, void *ov) {
 (void)ov;
 if (h != RH) { o_lasterr = 6; return 0; }
 uint32_t m = n;
 if (o_pos + m > TOTAL) m = (uint32_t)(TOTAL - o_pos);
 memcpy(b, g_store + o_pos, m); o_pos += m;
 if (done) *done = m;
 return 1;
}
int FlushFileBuffers(H h) {
 if (h != WH) { o_lasterr = 6; return 0; }
 o_flush_calls++;
 if (k_flush_fails) { o_lasterr = 21; return 0; }
 return 1;
}
int GetFileSizeEx(H h, int64_t *size) { (void)h; if (size) *size = (int64_t)TOTAL; return 1; }
int CloseHandle(H h) {
 if (h == WH) { o_closed_wh++; return 1; }
 if (h == RH) { o_closed_rh++; return 1; }
 return 0;
}
H VirtualAlloc(H addr, uint64_t len, uint32_t type, uint32_t prot) {
 (void)addr; (void)type; (void)prot;
 o_valloc_calls++;
 void *m = aligned_alloc(65536, (size_t)len); /* 64 KiB aligned, like the real granularity */
 o_valloc_ptr = m;
 return m;
}
int VirtualFree(H addr, uint64_t len, uint32_t type) {
 (void)len; (void)type;
 o_vfree_calls++;
 free(addr);
 if (addr == o_valloc_ptr) o_valloc_ptr = NULL;
 return 1;
}
int QueryPerformanceFrequency(int64_t *f) { if (f) *f = 10000000; return 1; }
static int64_t g_qpc;
int QueryPerformanceCounter(int64_t *c) { if (c) *c = ++g_qpc; return 1; }
H GetStdHandle(uint32_t which) { (void)which; return SH; }
void ExitProcess(uint32_t code) { o_exit = code; longjmp(g_jmp, 1); }

/* ---- scenario driver ---- */
static int fails;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "  FAIL %s: %s\n", scen, #cond); fails++; } } while (0)

static void reset(const char *scen) {
 (void)scen;
 /* reset iotest.c statics (visible: same translation unit via #include) */
 file = INVALID_HANDLE; mem = 0; hz = 0;
 /* default happy-path knobs */
 k_parent_attr = 0x10;              /* directory */
 k_free_avail = 4ULL * TOTAL;       /* plenty of headroom */
 k_create_new_fails = 0; k_create_new_err = 80;
 k_reopen_fails = 0; k_flush_fails = 0;
 k_short_at = 0; k_short_len = 0; k_short_armed = 0;
 o_lasterr = 0; o_create_calls = 0; o_path_ok = 1; o_parent_ok = 1;
 o_file_writes = 0; o_flush_calls = 0; o_closed_wh = 0; o_closed_rh = 0;
 o_valloc_calls = 0; o_vfree_calls = 0; o_valloc_ptr = NULL; o_pos = 0;
 o_exit = 0xffffffff; g_qpc = 0;
 memset(o_cf_access, 0, sizeof o_cf_access); memset(o_cf_share, 0, sizeof o_cf_share);
 memset(o_cf_disp, 0, sizeof o_cf_disp); memset(o_cf_flags, 0, sizeof o_cf_flags);
}

/* run mainCRTStartup once; returns the ExitProcess code */
static uint32_t drive(const char *scen) {
 reset(scen);
 if (setjmp(g_jmp) == 0) mainCRTStartup();
 return o_exit;
}

int main(void) {
 g_store = calloc((size_t)TOTAL, 1);
 if (!g_store) { fprintf(stderr, "no backing store\n"); return 2; }

 const char *scen;

 /* 1. pre-existing file: CREATE_NEW refused, no write, file untouched, exit 6 */
 scen = "preexisting-refused";
 {
  reset(scen); k_create_new_fails = 1; k_create_new_err = 80;
  if (setjmp(g_jmp) == 0) mainCRTStartup();
  CHECK(o_exit == 6);
  CHECK(o_create_calls == 1);
  CHECK(o_file_writes == 0);        /* nothing written */
  CHECK(o_flush_calls == 0);
  CHECK(o_path_ok);                 /* only the fixed path was requested */
  CHECK(o_cf_access[0] == 0x40000000U && o_cf_share[0] == 0 && o_cf_disp[0] == 1 &&
        o_cf_flags[0] == (0x20000000U | 0x80000000U)); /* GENERIC_WRITE, CREATE_NEW, NO_BUF|WRITE_THROUGH */
  CHECK(o_vfree_calls == o_valloc_calls); /* buffer freed on the exit path */
  CHECK(o_valloc_ptr == NULL);
 }

 /* 2. wrong parent: attribute lookup says missing, no CreateFileW, exit 3 */
 scen = "wrong-parent";
 { reset(scen); k_parent_attr = 0xffffffffU;
   if (setjmp(g_jmp) == 0) mainCRTStartup();
   CHECK(o_exit == 3); CHECK(o_create_calls == 0); CHECK(o_parent_ok);
   CHECK(o_valloc_calls == 0); }

 /* 3. no headroom: free space under 512 MiB, no CreateFileW, exit 4 */
 scen = "no-headroom";
 { reset(scen); k_free_avail = 2ULL * TOTAL - 1;
   if (setjmp(g_jmp) == 0) mainCRTStartup();
   CHECK(o_exit == 4); CHECK(o_create_calls == 0); CHECK(o_valloc_calls == 0); }

 /* 4. short write on a block: write loop reports SHORT_IO, exit 7, file kept, cleaned up */
 scen = "short-write";
 { reset(scen); k_short_armed = 1; k_short_at = 5 * BLOCK; k_short_len = 4096;
   if (setjmp(g_jmp) == 0) mainCRTStartup();
   CHECK(o_exit == 7);
   CHECK(o_create_calls == 1);       /* never reopened */
   CHECK(o_closed_wh == 1);          /* write handle closed on failure */
   CHECK(o_vfree_calls == 1 && o_valloc_ptr == NULL);
   CHECK(o_file_writes == 5 * BLOCK + 4096); }

 /* 5. flush fails: exit 8 after the full write, handle closed, buffer freed */
 scen = "flush-fail";
 { reset(scen); k_flush_fails = 1;
   if (setjmp(g_jmp) == 0) mainCRTStartup();
   CHECK(o_exit == 8);
   CHECK(o_flush_calls == 1);
   CHECK(o_file_writes == TOTAL);
   CHECK(o_closed_wh == 1 && o_vfree_calls == 1); }

 /* 6. reopen fails: exit 10, write handle already closed, read handle never opened */
 scen = "reopen-fail";
 { reset(scen); k_reopen_fails = 1;
   if (setjmp(g_jmp) == 0) mainCRTStartup();
   CHECK(o_exit == 10);
   CHECK(o_create_calls == 2);
   CHECK(o_closed_wh == 1 && o_closed_rh == 0);
   CHECK(o_vfree_calls == 1); }

 /* 7. full success: all words written and read back, correct flags on both opens,
       both handles closed, buffer freed, exit 0 */
 scen = "success-all-word-read";
 { uint32_t code = drive(scen);
   CHECK(code == 0);
   CHECK(o_create_calls == 2);
   CHECK(o_file_writes == TOTAL);
   CHECK(o_flush_calls == 1);
   CHECK(o_closed_wh == 1 && o_closed_rh == 1);
   CHECK(o_valloc_calls == 1 && o_vfree_calls == 1 && o_valloc_ptr == NULL);
   CHECK(o_path_ok && o_parent_ok);
   /* write open flags */
   CHECK(o_cf_access[0] == 0x40000000U && o_cf_share[0] == 0 && o_cf_disp[0] == 1 &&
         o_cf_flags[0] == (0x20000000U | 0x80000000U));
   /* read open flags: GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NO_BUFFERING */
   CHECK(o_cf_access[1] == 0x80000000U && o_cf_share[1] == 1 && o_cf_disp[1] == 3 &&
         o_cf_flags[1] == 0x20000000U); }

 free(g_store);
 if (fails) { fprintf(stderr, "wintest: %d check(s) failed\n", fails); return 1; }
 printf("PASS wintest: preexisting-refused, wrong-parent, no-headroom, short-write, "
        "flush-fail, reopen-fail, success-all-word-read; fixed path, flags and cleanup asserted\n");
 return 0;
}
