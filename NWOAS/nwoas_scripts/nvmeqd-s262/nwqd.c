/* NWOAS S262 Windows-side NVMe integrity + write-path tool (ARM64, no CRT).
 *
 *   NWQD integ <file> <MiB> <qd> <seed>
 *       Creates <file> (64..2048 MiB) on an NTFS volume and, with unbuffered overlapped I/O:
 *       P1 writes every 4 KiB block sequentially (1 MiB, QD4, generation 1),
 *       FlushFileBuffers,
 *       P2 overwrites a seed-chosen random quarter of the blocks (4 KiB, distinct blocks, never two
 *          in flight on the same block; generation 2): the first 1024 at QD1, the rest at <qd>;
 *          every second one through a FILE_FLAG_WRITE_THROUGH handle (Windows sends those as FUA),
 *       FlushFileBuffers,
 *       P3 reads every block in a random order at <qd> and checks it, P4 reads it all sequentially
 *          (1 MiB, QD4) and checks it, then writes the header "complete" + FlushFileBuffers.
 *   NWQD verify <file> <qd>
 *       Re-reads a completed file (e.g. after a restart) with P3 + P4 and checks every block.
 *
 * Every block carries magic, its own index, the seed, its generation and 4064 bytes of
 * seed/index/generation-derived data, so a lost write (old generation), a misdirected one
 * (other index) and zeros are told apart.  Writes only to <file> (never to a raw volume).
 * Every line starts with "S262 ".  Exit 0 = PASS, 1 = data mismatch, 2 = usage, 3 = API failure. */
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;
typedef long long S64;
typedef void *H;
#define IMP __declspec(dllimport)
#define INVALID_HANDLE ((H)(S64)-1)
IMP H GetStdHandle(U32);
IMP int WriteFile(H, const void *, U32, U32 *, void *);
IMP int ReadFile(H, void *, U32, U32 *, void *);
IMP int GetOverlappedResult(H, void *, U32 *, int);
IMP H CreateFileW(const U16 *, U32, U32, void *, U32, U32, H);
IMP H CreateEventW(void *, int, int, const U16 *);
IMP int ResetEvent(H);
IMP U32 WaitForMultipleObjects(U32, const H *, int, U32);
IMP int CloseHandle(H);
IMP int FlushFileBuffers(H);
IMP int SetFilePointerEx(H, S64, S64 *, U32);
IMP int SetEndOfFile(H);
IMP int QueryPerformanceCounter(S64 *);
IMP int QueryPerformanceFrequency(S64 *);
IMP void ExitProcess(U32);
IMP char *GetCommandLineA(void);
IMP U32 GetLastError(void);
IMP void *VirtualAlloc(void *, U64, U32, U32);

void *memset(void *d, int c, U64 n)
{
    U8 *p = d;
    while (n--)
        *p++ = (U8)c;
    return d;
}
void *memcpy(void *d, const void *s, U64 n)
{
    U8 *p = d;
    const U8 *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

/* ---------------------------------------------------------------- output */
static char line[512];
static char *lp;
static void out_begin(void) { lp = line; const char *s = "S262 "; while (*s) *lp++ = *s++; }
static void s_(const char *s) { while (*s && lp < line + sizeof(line) - 4) *lp++ = *s++; }
static void u_(U64 v)
{
    char r[24];
    int n = 0;
    do { r[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n && lp < line + sizeof(line) - 4) *lp++ = r[--n];
}
static void x_(U64 v)
{
    static const char hx[] = "0123456789abcdef";
    s_("0x");
    for (int b = 60; b >= 0; b -= 4)
        if (lp < line + sizeof(line) - 4) *lp++ = hx[(v >> b) & 15];
}
static void f1_(U64 tenths) { u_(tenths / 10); *lp++ = '.'; *lp++ = (char)('0' + tenths % 10); }
static void out_end(void)
{
    U32 w;
    *lp++ = '\r';
    *lp++ = '\n';
    WriteFile(GetStdHandle((U32)-11), line, (U32)(lp - line), &w, 0);
}
static void fail(const char *what)
{
    out_begin(); s_("FAIL api "); s_(what); s_(" err="); u_(GetLastError()); out_end();
    ExitProcess(3);
}

static S64 qpf;
static S64 now(void) { S64 t; QueryPerformanceCounter(&t); return t; }
static U64 us_of(S64 ticks) { return (U64)ticks * 1000000ULL / (U64)qpf; }

/* ---------------------------------------------------------------- args */
static char *argv_[8];
static int argc_;
static void parse_args(void)
{
    static char buf[512];
    char *c = GetCommandLineA();
    int i = 0;
    while (c[i] && i < 511) { buf[i] = c[i]; i++; }
    buf[i] = 0;
    char *p = buf;
    while (*p && argc_ < 8) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char q = 0;
        if (*p == '"') { q = '"'; p++; }
        argv_[argc_++] = p;
        while (*p && (q ? *p != q : (*p != ' ' && *p != '\t'))) p++;
        if (*p) *p++ = 0;
    }
}
static U64 num(const char *s, U64 lo, U64 hi)
{
    U64 v = 0;
    if (!s || !*s) return lo;
    while (*s >= '0' && *s <= '9') v = v * 10 + (U64)(*s++ - '0');
    return v < lo ? lo : v > hi ? hi : v;
}
static int eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static U16 wpath[300];
static void to_wide(const char *s)
{
    int i = 0;
    while (s && s[i] && i < 299) { wpath[i] = (U8)s[i]; i++; }
    wpath[i] = 0;
}

/* ---------------------------------------------------------------- pattern */
#define BLK        4096ULL
#define WORDS      (BLK / 8)
#define CHUNK_BLKS 256ULL /* 1 MiB */
#define MAGIC_BLK  0x314b4c4244515752ULL /* "RWQDBLK1" */
#define MAGIC_HDR  0x3152444844515752ULL /* "RWQDHDR1" */
static U64 seed, nblocks, n_over;
static U32 *idx;   /* permutation of 1..nblocks-1 */
static U8 *genmap; /* expected generation per block */

static U64 splitmix(U64 x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
static void fill_block(U64 *w, U64 b, U64 gen)
{
    w[0] = MAGIC_BLK; w[1] = b; w[2] = seed; w[3] = gen;
    U64 s = splitmix(seed ^ (b * 0xd1b54a32d192ed03ULL) ^ (gen << 56));
    for (U64 k = 4; k < WORDS; k++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        w[k] = s + k;
    }
}
static void fill_header(U64 *w, U64 state)
{
    memset(w, 0, BLK);
    w[0] = MAGIC_HDR; w[1] = seed; w[2] = nblocks; w[3] = n_over; w[4] = state; w[5] = 1;
    w[6] = w[0] ^ w[1] ^ w[2] ^ w[3] ^ w[4] ^ w[5] ^ 0x5a5a5a5a5a5a5a5aULL;
}
static void shuffle(U64 salt)
{
    U64 s = splitmix(seed ^ salt);
    for (U64 i = 0; i < nblocks - 1; i++) idx[i] = (U32)(i + 1);
    for (U64 i = nblocks - 2; i > 0; i--) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        U64 j = s % (i + 1);
        U32 t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
}
static void plan(void) /* deterministic from seed + nblocks: which blocks are generation 2 */
{
    shuffle(0xa5a5a5a5ULL);
    for (U64 b = 0; b < nblocks; b++) genmap[b] = 1;
    for (U64 i = 0; i < n_over; i++) genmap[idx[i]] = 2;
}

static U64 bad, bad_stale, bad_foreign, bad_zero, checked;
static U64 expect_hdr_state;
static void check_block(const U64 *w, U64 b)
{
    checked++;
    if (b == 0) {
        static U64 h[WORDS];
        fill_header(h, expect_hdr_state);
        for (U64 k = 0; k < 7; k++)
            if (w[k] != h[k]) {
                bad++;
                out_begin(); s_("BAD header word="); u_(k); s_(" got="); x_(w[k]); s_(" want="); x_(h[k]); out_end();
                return;
            }
        return;
    }
    static U64 e[WORDS];
    U64 gen = genmap[b];
    fill_block(e, b, gen);
    for (U64 k = 0; k < WORDS; k++)
        if (w[k] != e[k]) {
            bad++;
            const char *why = "corrupt";
            int zero = 1;
            for (U64 j = 0; j < WORDS; j++) if (w[j]) { zero = 0; break; }
            if (zero) { why = "zero"; bad_zero++; }
            else if (w[0] == MAGIC_BLK && w[1] == b && w[3] != gen) { why = "stale-generation"; bad_stale++; }
            else if (w[0] == MAGIC_BLK && w[1] != b) { why = "foreign-block"; bad_foreign++; }
            if (bad <= 8) {
                out_begin(); s_("BAD block="); u_(b); s_(" kind="); s_(why); s_(" want_gen="); u_(gen);
                s_(" word="); u_(k); s_(" got_magic="); x_(w[0]); s_(" got_index="); u_(w[1]);
                s_(" got_gen="); u_(w[3]); out_end();
            }
            return;
        }
}

/* ---------------------------------------------------------------- async I/O */
typedef struct { U64 internal, internal_high; U32 offset, offset_high; H event; } OVL;
#define QMAX 32
static OVL ovl[QMAX];
static H evs[QMAX], slot_h[QMAX];
static S64 t_issue[QMAX];
static U64 slot_blk[QMAX];
static U32 slot_len[QMAX];
static int slot_write[QMAX];
static U8 *iobuf;
static U8 *slot_buf(U32 s) { return iobuf + (U64)s * 0x100000; }

static void issue(U32 s, H h, int write, U64 blk, U32 len)
{
    ovl[s].internal = ovl[s].internal_high = 0;
    U64 off = blk * BLK;
    ovl[s].offset = (U32)off;
    ovl[s].offset_high = (U32)(off >> 32);
    slot_h[s] = h; slot_blk[s] = blk; slot_len[s] = len; slot_write[s] = write;
    ResetEvent(evs[s]);
    t_issue[s] = now();
    int ok = write ? WriteFile(h, slot_buf(s), len, 0, &ovl[s]) : ReadFile(h, slot_buf(s), len, 0, &ovl[s]);
    if (!ok && GetLastError() != 997) fail(write ? "WriteFile" : "ReadFile");
}

/* op generator: op i -> handle, write?, first block, length; fills the buffer for writes */
typedef void (*GEN)(U64 i, U32 slot, H *h, int *write, U64 *blk, U32 *len);
static H g_h, g_hw;

static void phase(const char *name, U64 n_ops, U32 qd, GEN gen)
{
    if (!n_ops) return;
    if (qd > n_ops) qd = (U32)n_ops;
    U64 next = 0, done = 0, bytes = 0, lsum = 0, lmax = 0;
    S64 t0 = now();
    for (U32 s = 0; s < qd; s++) {
        H h; int w; U64 b; U32 l;
        gen(next++, s, &h, &w, &b, &l);
        issue(s, h, w, b, l);
    }
    U32 inflight = qd;
    while (inflight) {
        U32 r = WaitForMultipleObjects(qd, evs, 0, 60000);
        if (r >= qd) fail("WaitForMultipleObjects");
        U32 got = 0;
        if (!GetOverlappedResult(slot_h[r], &ovl[r], &got, 0) || got != slot_len[r]) fail("GetOverlappedResult");
        U64 l = us_of(now() - t_issue[r]);
        lsum += l; if (l > lmax) lmax = l;
        done++; bytes += got;
        if (!slot_write[r])
            for (U32 k = 0; k < got / BLK; k++)
                check_block((const U64 *)(slot_buf(r) + (U64)k * BLK), slot_blk[r] + k);
        if (next < n_ops) {
            H h; int w; U64 b; U32 ln;
            gen(next++, r, &h, &w, &b, &ln);
            issue(r, h, w, b, ln);
        } else {
            ResetEvent(evs[r]);
            inflight--;
        }
    }
    U64 us = us_of(now() - t0);
    out_begin(); s_("PHASE "); s_(name); s_(" qd="); u_(qd); s_(" ios="); u_(done);
    s_(" iops="); u_(us ? done * 1000000ULL / us : 0); s_(" MBps="); f1_(us ? bytes * 10ULL / us : 0);
    s_(" lat_mean_us="); u_(done ? lsum / done : 0); s_(" lat_max_us="); u_(lmax);
    s_(" bad_so_far="); u_(bad); out_end();
}

static U64 p2_base; /* P2 runs in two parts: QD1 then <qd> */
static void gen_seq_write(U64 i, U32 s, H *h, int *w, U64 *b, U32 *l)
{
    *h = g_h; *w = 1; *b = i * CHUNK_BLKS; *l = (U32)(CHUNK_BLKS * BLK);
    for (U64 k = 0; k < CHUNK_BLKS; k++) {
        U64 blk = *b + k;
        if (blk == 0) fill_header((U64 *)slot_buf(s), 0);
        else fill_block((U64 *)(slot_buf(s) + k * BLK), blk, 1);
    }
}
static void gen_overwrite(U64 i, U32 s, H *h, int *w, U64 *b, U32 *l)
{
    U64 j = p2_base + i;
    *h = (j & 1) ? g_hw : g_h; *w = 1; *b = idx[j]; *l = (U32)BLK;
    fill_block((U64 *)slot_buf(s), *b, 2);
}
static void gen_rand_read(U64 i, U32 s, H *h, int *w, U64 *b, U32 *l)
{
    (void)s;
    *h = g_h; *w = 0; *b = i == 0 ? 0 : idx[i - 1]; *l = (U32)BLK;
}
static void gen_seq_read(U64 i, U32 s, H *h, int *w, U64 *b, U32 *l)
{
    (void)s;
    *h = g_h; *w = 0; *b = i * CHUNK_BLKS; *l = (U32)(CHUNK_BLKS * BLK);
}

static void setup_mem(void)
{
    iobuf = VirtualAlloc(0, (U64)QMAX * 0x100000, 0x3000, 4);
    idx = VirtualAlloc(0, nblocks * 4, 0x3000, 4);
    genmap = VirtualAlloc(0, nblocks, 0x3000, 4);
    if (!iobuf || !idx || !genmap) fail("VirtualAlloc");
    for (U32 i = 0; i < QMAX; i++) {
        evs[i] = CreateEventW(0, 1, 0, 0);
        if (!evs[i]) fail("CreateEventW");
        ovl[i].event = evs[i];
    }
}

static void verify_all(U32 qd, U64 hdr_state)
{
    expect_hdr_state = hdr_state;
    shuffle(0x5a5a5a5aULL); /* new random read order; genmap is already planned */
    phase("P3-rand4k-read-verify", nblocks, qd, gen_rand_read);
    phase("P4-seq1m-read-verify", nblocks / CHUNK_BLKS, 4, gen_seq_read);
}

static void verdict(const char *mode)
{
    out_begin(); s_(bad ? "INTEG FAIL " : "INTEG PASS "); s_(mode); s_(" blocks="); u_(nblocks);
    s_(" overwritten="); u_(n_over); s_(" checked="); u_(checked); s_(" bad="); u_(bad);
    s_(" stale="); u_(bad_stale); s_(" foreign="); u_(bad_foreign); s_(" zero="); u_(bad_zero);
    s_(" seed="); u_(seed); out_end();
    ExitProcess(bad ? 1 : 0);
}

static void sync_flush(const char *what)
{
    S64 t0 = now();
    if (!FlushFileBuffers(g_h)) fail("FlushFileBuffers");
    out_begin(); s_("FLUSH "); s_(what); s_(" us="); u_(us_of(now() - t0)); out_end();
}

static void cmd_integ(const char *path, U64 mib, U32 qd, U64 sd)
{
    seed = sd;
    nblocks = mib * CHUNK_BLKS;
    n_over = (nblocks - 1) / 4;
    setup_mem();
    plan();
    to_wide(path);
    /* GENERIC_READ|GENERIC_WRITE, share read|write, CREATE_ALWAYS, NO_BUFFERING|OVERLAPPED */
    g_h = CreateFileW(wpath, 0xC0000000U, 3, 0, 2, 0x60000000U, 0);
    if (g_h == INVALID_HANDLE) fail("CreateFileW create");
    if (!SetFilePointerEx(g_h, (S64)(nblocks * BLK), 0, 0) || !SetEndOfFile(g_h)) fail("SetEndOfFile");
    /* same file, OPEN_EXISTING, + FILE_FLAG_WRITE_THROUGH: Windows sends these writes with FUA */
    g_hw = CreateFileW(wpath, 0xC0000000U, 3, 0, 3, 0xE0000000U, 0);
    if (g_hw == INVALID_HANDLE) fail("CreateFileW write-through");
    out_begin(); s_("INTEG BEGIN file="); s_(path); s_(" MiB="); u_(mib); s_(" blocks="); u_(nblocks);
    s_(" overwrite="); u_(n_over); s_(" qd="); u_(qd); s_(" seed="); u_(seed); out_end();

    phase("P1-seq1m-write", nblocks / CHUNK_BLKS, 4, gen_seq_write);
    sync_flush("after-P1");
    shuffle(0xa5a5a5a5ULL); /* the same order plan() used: idx[0..n_over) are the generation-2 blocks */
    U64 first = n_over < 1024 ? n_over : 1024;
    p2_base = 0;
    phase("P2a-rand4k-write", first, 1, gen_overwrite);
    p2_base = first;
    phase("P2b-rand4k-write", n_over - first, qd, gen_overwrite);
    sync_flush("after-P2");
    verify_all(qd, 0);
    /* mark complete (header, state 1) and flush, so "verify" after a restart can trust it */
    fill_header((U64 *)slot_buf(0), 1);
    issue(0, g_h, 1, 0, (U32)BLK);
    U32 got = 0;
    if (WaitForMultipleObjects(1, evs, 0, 60000) != 0 || !GetOverlappedResult(g_h, &ovl[0], &got, 0) ||
        got != BLK) fail("header write");
    sync_flush("after-header");
    CloseHandle(g_hw);
    CloseHandle(g_h);
    verdict("integ");
}

static void cmd_verify(const char *path, U32 qd)
{
    to_wide(path);
    g_h = CreateFileW(wpath, 0x80000000U, 1, 0, 3, 0x60000000U, 0);
    if (g_h == INVALID_HANDLE) fail("CreateFileW open");
    iobuf = VirtualAlloc(0, (U64)QMAX * 0x100000, 0x3000, 4);
    if (!iobuf) fail("VirtualAlloc");
    evs[0] = CreateEventW(0, 1, 0, 0);
    ovl[0].event = evs[0];
    issue(0, g_h, 0, 0, (U32)BLK);
    U32 got = 0;
    if (WaitForMultipleObjects(1, evs, 0, 60000) != 0 || !GetOverlappedResult(g_h, &ovl[0], &got, 0) ||
        got != BLK) fail("header read");
    const U64 *w = (const U64 *)slot_buf(0);
    U64 ck = w[0] ^ w[1] ^ w[2] ^ w[3] ^ w[4] ^ w[5] ^ 0x5a5a5a5a5a5a5a5aULL;
    if (w[0] != MAGIC_HDR || w[6] != ck || w[5] != 1 || w[2] < 64 * CHUNK_BLKS ||
        w[2] > 2048 * CHUNK_BLKS || (w[2] % CHUNK_BLKS) || w[3] != (w[2] - 1) / 4) {
        out_begin(); s_("INTEG FAIL verify header invalid magic="); x_(w[0]); out_end();
        ExitProcess(1);
    }
    if (w[4] != 1) {
        out_begin(); s_("INTEG FAIL verify file was never completed (state="); u_(w[4]); s_(")"); out_end();
        ExitProcess(1);
    }
    seed = w[1]; nblocks = w[2]; n_over = w[3];
    CloseHandle(evs[0]);
    {   /* setup_mem() without a second 32 MiB buffer */
        idx = VirtualAlloc(0, nblocks * 4, 0x3000, 4);
        genmap = VirtualAlloc(0, nblocks, 0x3000, 4);
        if (!idx || !genmap) fail("VirtualAlloc");
        for (U32 i = 0; i < QMAX; i++) {
            evs[i] = CreateEventW(0, 1, 0, 0);
            if (!evs[i]) fail("CreateEventW");
            ovl[i].event = evs[i];
        }
    }
    plan();
    out_begin(); s_("VERIFY BEGIN file="); s_(path); s_(" blocks="); u_(nblocks); s_(" seed="); u_(seed); out_end();
    verify_all(qd, 1);
    CloseHandle(g_h);
    verdict("verify");
}

void mainCRTStartup(void)
{
    QueryPerformanceFrequency(&qpf);
    parse_args();
    const char *c = argc_ > 1 ? argv_[1] : "";
    if (eq(c, "integ") && argc_ > 5)
        cmd_integ(argv_[2], num(argv_[3], 64, 2048), (U32)num(argv_[4], 1, QMAX), num(argv_[5], 1, 0xffffffffULL));
    else if (eq(c, "verify") && argc_ > 3)
        cmd_verify(argv_[2], (U32)num(argv_[3], 1, QMAX));
    out_begin(); s_("usage: NWQD integ <file> <MiB 64..2048> <qd 1..32> <seed> | verify <file> <qd>"); out_end();
    ExitProcess(2);
}
