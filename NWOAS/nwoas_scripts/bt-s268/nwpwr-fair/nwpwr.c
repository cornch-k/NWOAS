/* NWOAS S260 Windows-side measurement tool (ARM64, no CRT).  Read-only:
 *   NWPWR cpuset                 GetSystemCpuSetInformation: EfficiencyClass per logical CPU (C-3 check)
 *   NWPWR load <sec> <threads> [hi]  bounded integer load; per-thread Mops and the CPU each thread ran on.
 *                                'hi' = this process only: NORMAL priority class + power throttling (EcoQoS)
 *                                opted out, to tell Windows QoS/core-class policy apart from HV wake issues
 *   NWPWR idle <sec>             sleeps; per-CPU busy %% and interrupts/s (NtQuerySystemInformation class 8)
 *   NWPWR timer <n>              timeBeginPeriod(1) + n x Sleep(1): real gap mean/p50/p99/max (C-4 check)
 *   NWPWR io <sec>               raw \\.\C: volume READS only (GENERIC_READ, no buffering):
 *                                4 KiB random QD1/QD8/QD32 and 1 MiB sequential QD4, <sec> each (S-0)
 * Every line starts with "S260 ".  Exit 0 = ran, 2 = usage, 3 = API failure. */
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;
typedef long long S64;
typedef void *H;
typedef U32(__stdcall *THREAD_FN)(void *);
#define IMP __declspec(dllimport)
#define INVALID_HANDLE ((H)(S64)-1)
IMP H GetStdHandle(U32);
IMP int WriteFile(H, const void *, U32, U32 *, void *);
IMP H CreateThread(void *, U64, THREAD_FN, void *, U32, U32 *);
IMP U32 WaitForMultipleObjects(U32, const H *, int, U32);
IMP int CloseHandle(H);
IMP int QueryPerformanceCounter(S64 *);
IMP int QueryPerformanceFrequency(S64 *);
IMP U32 GetActiveProcessorCount(U16);
IMP void ExitProcess(U32);
IMP char *GetCommandLineA(void);
IMP U32 GetCurrentProcessorNumber(void);
IMP void Sleep(U32);
IMP int GetSystemCpuSetInformation(void *, U32, U32 *, H, U32);
IMP U32 GetLastError(void);
IMP H CreateFileW(const U16 *, U32, U32, void *, U32, U32, H);
IMP int ReadFile(H, void *, U32, U32 *, void *);
IMP int GetOverlappedResult(H, void *, U32 *, int);
IMP H CreateEventW(void *, int, int, const U16 *);
IMP int ResetEvent(H);
IMP int DeviceIoControl(H, U32, void *, U32, void *, U32, U32 *, void *);
IMP void *VirtualAlloc(void *, U64, U32, U32);
IMP H GetCurrentProcess(void);
IMP U32 timeBeginPeriod(U32);
IMP U32 timeEndPeriod(U32);
IMP S64 NtQuerySystemInformation(U32, void *, U32, U32 *);
IMP U32 GetPriorityClass(H);
IMP int SetPriorityClass(H, U32);
IMP int SetProcessInformation(H, U32, void *, U32);
IMP int SetThreadInformation(H, U32, void *, U32);
IMP H GetCurrentThread(void);

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

static char line[512];
static char *lp;
static void out_begin(void) { lp = line; const char *s = "S260 "; while (*s) *lp++ = *s++; }
static void s_(const char *s) { while (*s && lp < line + sizeof(line) - 4) *lp++ = *s++; }
static void u_(U64 v)
{
    char r[24];
    int n = 0;
    do { r[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n && lp < line + sizeof(line) - 4) *lp++ = r[--n];
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
    out_begin(); s_("FAIL "); s_(what); s_(" err="); u_(GetLastError()); out_end();
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

/* ---------------------------------------------------------------- cpuset */
static U8 csbuf[8192];
static void cmd_cpuset(void)
{
    U32 len = 0;
    if (!GetSystemCpuSetInformation(csbuf, sizeof(csbuf), &len, GetCurrentProcess(), 0))
        fail("GetSystemCpuSetInformation");
    U32 at = 0, n = 0;
    while (at + 32 <= len) {
        U32 size = *(U32 *)(csbuf + at);
        if (size < 32) break;
        if (*(U32 *)(csbuf + at + 4) == 0) { /* CpuSetInformation */
            out_begin(); s_("CPUSET id="); u_(*(U32 *)(csbuf + at + 8));
            s_(" lp="); u_(csbuf[at + 14]); s_(" core="); u_(csbuf[at + 15]);
            s_(" llc="); u_(csbuf[at + 16]); s_(" efficiency_class="); u_(csbuf[at + 18]);
            s_(" sched_class="); u_(csbuf[at + 20]); s_(" flags="); u_(csbuf[at + 19]); out_end();
            n++;
        }
        at += size;
    }
    out_begin(); s_("CPUSET count="); u_(n); s_(" active="); u_(GetActiveProcessorCount(0xffff)); out_end();
}

/* ---------------------------------------------------------------- load */
#define MAXT 16
static volatile U64 load_iter[MAXT];
static volatile U32 load_cpuhist[MAXT][MAXT];
static S64 load_deadline;
static int load_hi;
typedef struct { U32 version, control, state; } PWR_THROTTLE; /* PROCESS/THREAD_POWER_THROTTLING_STATE */
static U32 __stdcall load_worker(void *arg)
{
    U64 id = (U64)arg, x = 0x9e3779b97f4a7c15ULL ^ (id + 1), it = 0;
    if (load_hi) { /* ThreadPowerThrottling = 3: EXECUTION_SPEED controlled, state off */
        PWR_THROTTLE t = {1, 1, 0};
        SetThreadInformation(GetCurrentThread(), 3, &t, sizeof(t));
    }
    while (now() < load_deadline) {
        for (U32 i = 0; i < 1000000; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17; x += 0x6a09e667f3bcc909ULL;
        }
        it++;
        U32 cpu = GetCurrentProcessorNumber();
        if (cpu < MAXT) load_cpuhist[id][cpu]++;
    }
    load_iter[id] = it + (x & 0);
    return (U32)(x & 1);
}
static void cmd_load(U64 sec, U64 threads, int hi)
{
    static H th[MAXT];
    U32 tid;
    U32 before = GetPriorityClass(GetCurrentProcess());
    int ok_prio = 1, ok_thr = 1;
    load_hi = hi;
    if (hi) {
        PWR_THROTTLE t = {1, 1, 0}; /* ProcessPowerThrottling = 4 */
        ok_prio = SetPriorityClass(GetCurrentProcess(), 0x20 /* NORMAL_PRIORITY_CLASS */);
        ok_thr = SetProcessInformation(GetCurrentProcess(), 4, &t, sizeof(t));
    }
    out_begin(); s_("LOAD mode="); s_(hi ? "hi" : "default"); s_(" prio_class_before=0x");
    { static const char hx[] = "0123456789abcdef"; for (int b = 28; b >= 0; b -= 4) *lp++ = hx[(before >> b) & 15]; }
    s_(" set_prio_ok="); u_(ok_prio); s_(" set_throttle_ok="); u_(ok_thr); out_end();
    for (U32 i = 0; i < MAXT; i++) { load_iter[i] = 0; for (U32 c = 0; c < MAXT; c++) load_cpuhist[i][c] = 0; }
    load_deadline = now() + (S64)sec * qpf;
    for (U64 i = 0; i < threads; i++)
        if (!(th[i] = CreateThread(0, 0, load_worker, (void *)i, 0, &tid))) fail("CreateThread");
    WaitForMultipleObjects((U32)threads, th, 1, (U32)(sec * 1000 + 60000));
    U64 total = 0;
    for (U64 i = 0; i < threads; i++) {
        CloseHandle(th[i]);
        total += load_iter[i];
        out_begin(); s_("LOAD thread="); u_(i); s_(" Mops="); u_(load_iter[i]); s_(" ran_on=");
        for (U32 c = 0; c < MAXT; c++)
            if (load_cpuhist[i][c]) { s_("cpu"); u_(c); s_(":"); u_(load_cpuhist[i][c]); s_(" "); }
        out_end();
    }
    out_begin(); s_("LOAD seconds="); u_(sec); s_(" threads="); u_(threads); s_(" total_Mops="); u_(total);
    s_(" Mops_per_s="); u_(total / (sec ? sec : 1)); out_end();
}

/* ---------------------------------------------------------------- idle */
/* SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION: Idle, Kernel(incl. idle), User, Dpc, Interrupt (100 ns),
 * ULONG InterruptCount, 4 bytes padding = 48 bytes per CPU. */
typedef struct { S64 idle, kernel, user, dpc, intr; U32 count, pad; } SPPI;
static SPPI pp0[MAXT], pp1[MAXT];
static U32 pp_query(SPPI *o)
{
    U32 got = 0;
    if (NtQuerySystemInformation(8, o, sizeof(SPPI) * MAXT, &got) < 0) fail("NtQuerySystemInformation");
    return got / sizeof(SPPI);
}
static void cmd_idle(U64 sec)
{
    U32 n = pp_query(pp0);
    S64 t0 = now();
    Sleep((U32)(sec * 1000));
    S64 t1 = now();
    pp_query(pp1);
    U64 ms = us_of(t1 - t0) / 1000;
    U64 isum = 0;
    for (U32 i = 0; i < n; i++) {
        S64 tot = (pp1[i].kernel - pp0[i].kernel) + (pp1[i].user - pp0[i].user);
        S64 idl = pp1[i].idle - pp0[i].idle;
        U64 busy_pm = tot > 0 ? (U64)((tot - idl) * 1000 / tot) : 0;
        U64 irq = pp1[i].count - pp0[i].count;
        isum += irq;
        out_begin(); s_("IDLE cpu="); u_(i); s_(" busy_pct="); f1_(busy_pm); s_(" dpc_pct=");
        f1_(tot > 0 ? (U64)((pp1[i].dpc - pp0[i].dpc) * 1000 / tot) : 0); s_(" intr_pct=");
        f1_(tot > 0 ? (U64)((pp1[i].intr - pp0[i].intr) * 1000 / tot) : 0);
        s_(" interrupts_per_s="); u_(ms ? irq * 1000 / ms : 0); out_end();
    }
    out_begin(); s_("IDLE seconds_measured_ms="); u_(ms); s_(" cpus="); u_(n); s_(" interrupts_per_s_total=");
    u_(ms ? isum * 1000 / ms : 0); out_end();
}

/* ---------------------------------------------------------------- timer */
static U32 gaps[5000];
static void sort_u32(U32 *a, U32 n)
{
    for (U32 gap = n / 2; gap; gap /= 2)
        for (U32 i = gap; i < n; i++) {
            U32 v = a[i], j = i;
            while (j >= gap && a[j - gap] > v) { a[j] = a[j - gap]; j -= gap; }
            a[j] = v;
        }
}
static void cmd_timer(U64 n)
{
    timeBeginPeriod(1);
    S64 prev = now();
    U64 sum = 0;
    for (U64 i = 0; i < n; i++) {
        Sleep(1);
        S64 t = now();
        U64 g = us_of(t - prev);
        gaps[i] = g > 0xffffffffULL ? 0xffffffffU : (U32)g;
        sum += g;
        prev = t;
    }
    timeEndPeriod(1);
    sort_u32(gaps, (U32)n);
    out_begin(); s_("TIMER sleep1 n="); u_(n); s_(" mean_us="); u_(sum / n); s_(" p50_us="); u_(gaps[n / 2]);
    s_(" p99_us="); u_(gaps[(n * 99) / 100]); s_(" max_us="); u_(gaps[n - 1]); s_(" min_us="); u_(gaps[0]); out_end();
}

/* ---------------------------------------------------------------- io */
typedef struct { U64 internal, internal_high; U32 offset, offset_high; H event; } OVL;
#define QMAX 32
static OVL ovl[QMAX];
static H evs[QMAX];
static S64 issued_at[QMAX];
static U8 *iobuf;
static U64 vol_bytes, rng = 0x2545f4914f6cdd1dULL;
static U32 lat_hist[8]; /* <50 <100 <200 <500 <1000 <2000 <5000 >=5000 us */
static void issue(H f, U32 slot, U64 off, U32 len)
{
    ovl[slot].internal = ovl[slot].internal_high = 0;
    ovl[slot].offset = (U32)off;
    ovl[slot].offset_high = (U32)(off >> 32);
    ResetEvent(evs[slot]);
    issued_at[slot] = now();
    if (!ReadFile(f, iobuf + (U64)slot * 0x100000, len, 0, &ovl[slot]) && GetLastError() != 997)
        fail("ReadFile");
}
static U64 next_off(int seq, U64 *seqpos, U32 len)
{
    if (seq) {
        U64 o = *seqpos;
        *seqpos += len;
        if (*seqpos + len > vol_bytes) *seqpos = 0;
        return o;
    }
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (rng % (vol_bytes / 4096)) * 4096;
}
static void io_test(H f, const char *name, U32 qd, U32 len, int seq, U64 sec)
{
    U64 seqpos = 0, done = 0, bytes = 0, lsum = 0, lmax = 0;
    for (int i = 0; i < 8; i++) lat_hist[i] = 0;
    S64 start = now(), end = start + (S64)sec * qpf;
    for (U32 i = 0; i < qd; i++) issue(f, i, next_off(seq, &seqpos, len), len);
    U32 inflight = qd, rot = 0;
    static H evr[QMAX];
    while (inflight) {
        /* S268 fair reaping: WaitForMultipleObjects returns the SMALLEST signaled index, so with the reap
         * loop as the bottleneck a high slot could wait seconds after its I/O completed (NWPWR S260 measured
         * that wait as I/O latency).  Rotate the array start past the slot served last (round robin). */
        for (U32 k = 0; k < qd; k++) evr[k] = evs[(k + rot) % qd];
        U32 w = WaitForMultipleObjects(qd, evr, 0, 10000);
        if (w >= qd) fail("WaitForMultipleObjects");
        U32 r = (w + rot) % qd;
        rot = (r + 1) % qd;
        U32 got = 0;
        if (!GetOverlappedResult(f, &ovl[r], &got, 0) || got != len) fail("GetOverlappedResult");
        U64 l = us_of(now() - issued_at[r]);
        lsum += l; if (l > lmax) lmax = l;
        lat_hist[l < 50 ? 0 : l < 100 ? 1 : l < 200 ? 2 : l < 500 ? 3 : l < 1000 ? 4 : l < 2000 ? 5 : l < 5000 ? 6 : 7]++;
        done++; bytes += got;
        if (now() < end) issue(f, r, next_off(seq, &seqpos, len), len);
        else { ResetEvent(evs[r]); inflight--; } /* drained: no new I/O, event stays reset */
    }
    U64 us = us_of(now() - start);
    out_begin(); s_("IO "); s_(name); s_(" reap=rr qd="); u_(qd); s_(" bytes_per_io="); u_(len);
    s_(" ios="); u_(done); s_(" iops="); u_(us ? done * 1000000ULL / us : 0);
    s_(" MBps="); f1_(us ? bytes * 10ULL / us : 0); s_(" lat_mean_us="); u_(done ? lsum / done : 0);
    s_(" lat_max_us="); u_(lmax); s_(" hist<50,100,200,500,1k,2k,5k,inf=");
    for (int i = 0; i < 8; i++) { u_(lat_hist[i]); if (i < 7) s_(","); }
    out_end();
}
static void cmd_io(U64 sec)
{
    static const U16 path[] = {'\\', '\\', '.', '\\', 'C', ':', 0};
    /* GENERIC_READ, share read|write, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING|FILE_FLAG_OVERLAPPED */
    H f = CreateFileW(path, 0x80000000U, 3, 0, 3, 0x20000000U | 0x40000000U, 0);
    if (f == INVALID_HANDLE) fail("CreateFileW \\\\.\\C:");
    U64 lenbuf = 0;
    U32 ret = 0;
    if (!DeviceIoControl(f, 0x7405c /* IOCTL_DISK_GET_LENGTH_INFO */, 0, 0, &lenbuf, 8, &ret, 0))
        fail("IOCTL_DISK_GET_LENGTH_INFO");
    vol_bytes = lenbuf & ~0xfffffULL;
    iobuf = VirtualAlloc(0, (U64)QMAX * 0x100000, 0x3000, 4);
    if (!iobuf || vol_bytes < 0x10000000) fail("VirtualAlloc/volume size");
    for (U32 i = 0; i < QMAX; i++) {
        evs[i] = CreateEventW(0, 1, 0, 0);
        if (!evs[i]) fail("CreateEventW");
        ovl[i].event = evs[i];
    }
    out_begin(); s_("IO NWPWRF(S268 fair reaping) volume=C: bytes="); u_(vol_bytes); s_(" seconds_per_test="); u_(sec); out_end();
    io_test(f, "rand4k", 1, 4096, 0, sec);
    io_test(f, "rand4k", 8, 4096, 0, sec);
    io_test(f, "rand4k", 32, 4096, 0, sec);
    io_test(f, "seq1m", 4, 0x100000, 1, sec);
    CloseHandle(f);
}

void mainCRTStartup(void)
{
    QueryPerformanceFrequency(&qpf);
    parse_args();
    const char *c = argc_ > 1 ? argv_[1] : "";
    if (eq(c, "cpuset")) cmd_cpuset();
    else if (eq(c, "load")) cmd_load(num(argc_ > 2 ? argv_[2] : 0, 1, 600), num(argc_ > 3 ? argv_[3] : 0, 1, MAXT),
                                     argc_ > 4 && eq(argv_[4], "hi"));
    else if (eq(c, "idle")) cmd_idle(num(argc_ > 2 ? argv_[2] : 0, 1, 600));
    else if (eq(c, "timer")) cmd_timer(num(argc_ > 2 ? argv_[2] : 0, 10, 5000));
    else if (eq(c, "io")) cmd_io(num(argc_ > 2 ? argv_[2] : 0, 1, 120));
    else { out_begin(); s_("usage: NWPWR cpuset | idle <sec> | load <sec> <threads> [hi] | timer <n> | io <sec>"); out_end(); ExitProcess(2); }
    ExitProcess(0);
}
