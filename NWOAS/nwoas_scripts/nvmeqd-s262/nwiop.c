/* NWOAS S262S: run a storage tool at a chosen I/O priority (ARM64 Windows, no CRT).
 *
 *   NWIOP <io-priority 0..2> <program> [args...]
 *
 * Why: the NWOAS Agent scheduled task is created with "schtasks /Create" and no priority, so it
 * runs at the Task Scheduler default priority 7 = BELOW_NORMAL class + IoPriorityLow (Microsoft
 * Learn "Priority (settingsType) Element").  Every job, and NWPWR/NWQD started from it, then
 * issues low-priority I/O.  classpnp (disk.sys) treats I/O with priority <= IoPriorityLow as
 * "idle" I/O: it is queued and at most IdleActiveIoMax (default 1) is outstanding, the next one
 * being started from the completion of the previous one (Windows-driver-samples
 * storage/class/classpnp: ClasspIsIdleRequestSupported, ClasspInitializeIdleTimer,
 * ClasspCompleteIdleRequest).  That matches the 2026-11-19 g262R hardware counters
 * (HV in-flight mean 1.00 at QD 8/32, latency = QD x device time).
 *
 * What it does: prints its own process/thread I/O priority (what the agent gives), creates the
 * program suspended with NORMAL_PRIORITY_CLASS, sets the child's process AND main-thread I/O
 * priority (NtSetInformationProcess ProcessIoPriority 33, NtSetInformationThread
 * ThreadIoPriority 22), reads both back, resumes it, waits, and exits with its exit code.
 * Changes nothing persistent (no task, registry or driver setting).
 * Lines start with "S262S IOPRIO ".  Exit = child's exit code, or 90..93 on a wrapper failure. */
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;
typedef long long S64;
typedef long NTSTATUS;
typedef void *H;
#define IMP __declspec(dllimport)
IMP H GetStdHandle(U32);
IMP int WriteFile(H, const void *, U32, U32 *, void *);
IMP void ExitProcess(U32);
IMP U16 *GetCommandLineW(void);
IMP U32 GetLastError(void);
IMP H GetCurrentProcess(void);
IMP H GetCurrentThread(void);
IMP U32 GetPriorityClass(H);
IMP int CreateProcessW(const U16 *, U16 *, void *, void *, int, U32, void *, const U16 *, void *,
                       void *);
IMP U32 ResumeThread(H);
IMP U32 WaitForSingleObject(H, U32);
IMP int GetExitCodeProcess(H, U32 *);
IMP int TerminateProcess(H, U32);
IMP int CloseHandle(H);
IMP NTSTATUS NtQueryInformationProcess(H, U32, void *, U32, U32 *);
IMP NTSTATUS NtSetInformationProcess(H, U32, void *, U32);
IMP NTSTATUS NtQueryInformationThread(H, U32, void *, U32, U32 *);
IMP NTSTATUS NtSetInformationThread(H, U32, void *, U32);

#define PROCESS_IO_PRIORITY 33
#define THREAD_IO_PRIORITY 22
#define CREATE_SUSPENDED 0x4u
#define NORMAL_PRIORITY_CLASS 0x20u
#define STARTF_USESTDHANDLES 0x100u

typedef struct {
    U32 cb;
    U16 *reserved, *desktop, *title;
    U32 x, y, xsize, ysize, xchars, ychars, fill, flags;
    U16 show, reserved2;
    U8 *reservedptr;
    H input, output, error;
} SI;
typedef struct { H process, thread; U32 pid, tid; } PI;
_Static_assert(sizeof(SI) == 104, "STARTUPINFOW layout (ARM64)");

void *memset(void *d, int c, U64 n)
{
    U8 *p = d;
    while (n--)
        *p++ = (U8)c;
    return d;
}

static char line[256];
static char *lp;
static void out_begin(void) { lp = line; const char *s = "S262S IOPRIO "; while (*s) *lp++ = *s++; }
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
    int b = 28;
    while (b > 0 && !((v >> b) & 15)) b -= 4;
    for (; b >= 0; b -= 4) *lp++ = hx[(v >> b) & 15];
}
static void out_end(void)
{
    U32 w;
    *lp++ = '\r';
    *lp++ = '\n';
    WriteFile(GetStdHandle((U32)-11), line, (U32)(lp - line), &w, 0);
}
static void die(const char *what, U64 code, U32 rc)
{
    out_begin(); s_("FAIL "); s_(what); s_(" code="); x_(code); out_end();
    ExitProcess(rc);
}

/* -1 when the query fails (printed as "?") */
static S64 proc_prio(H p)
{
    U32 v = 0xffffffffu, got = 0;
    return NtQueryInformationProcess(p, PROCESS_IO_PRIORITY, &v, 4, &got) < 0 ? -1 : (S64)v;
}
static S64 thread_prio(H t)
{
    U32 v = 0xffffffffu, got = 0;
    return NtQueryInformationThread(t, THREAD_IO_PRIORITY, &v, 4, &got) < 0 ? -1 : (S64)v;
}
static void prio_(S64 v)
{
    static const char *names[] = { "0(very-low)", "1(low)", "2(normal)", "3(high)", "4(critical)" };
    if (v < 0) s_("?");
    else if (v <= 4) s_(names[v]);
    else u_((U64)v);
}

void mainCRTStartup(void)
{
    U16 *c = GetCommandLineW();
    /* skip our own program name (quoted or not) */
    if (*c == '"') { c++; while (*c && *c != '"') c++; if (*c) c++; }
    else while (*c && *c != ' ' && *c != '\t') c++;
    while (*c == ' ' || *c == '\t') c++;
    if (*c < '0' || *c > '2' || (c[1] != ' ' && c[1] != '\t'))
        die("usage: NWIOP <0|1|2> <program> [args]", 0, 92);
    U32 want = (U32)(*c - '0');
    c++;
    while (*c == ' ' || *c == '\t') c++;
    if (!*c)
        die("usage: NWIOP <0|1|2> <program> [args]", 0, 92);

    out_begin(); s_("self process="); prio_(proc_prio(GetCurrentProcess()));
    s_(" thread="); prio_(thread_prio(GetCurrentThread()));
    s_(" class="); x_(GetPriorityClass(GetCurrentProcess())); out_end();

    SI si;
    PI pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.flags = STARTF_USESTDHANDLES; /* the agent's job pipe, inherited by cmd.exe and by us */
    si.input = GetStdHandle((U32)-10);
    si.output = GetStdHandle((U32)-11);
    si.error = GetStdHandle((U32)-12);
    if (!CreateProcessW(0, c, 0, 0, 1, CREATE_SUSPENDED | NORMAL_PRIORITY_CLASS, 0, 0, &si, &pi))
        die("CreateProcessW", GetLastError(), 90);

    NTSTATUS sp = NtSetInformationProcess(pi.process, PROCESS_IO_PRIORITY, &want, 4);
    NTSTATUS st = NtSetInformationThread(pi.thread, THREAD_IO_PRIORITY, &want, 4);
    S64 gp = proc_prio(pi.process), gt = thread_prio(pi.thread);
    out_begin(); s_("child pid="); u_(pi.pid); s_(" want="); prio_(want);
    s_(" set_process_status="); x_((U32)sp); s_(" set_thread_status="); x_((U32)st);
    s_(" process="); prio_(gp); s_(" thread="); prio_(gt);
    s_(" class="); x_(GetPriorityClass(pi.process)); out_end();
    if (sp < 0 || st < 0 || gp != (S64)want || gt != (S64)want) {
        TerminateProcess(pi.process, 93);
        die("child I/O priority not applied (not started)", (U64)(U32)(sp < 0 ? sp : st), 91);
    }
    if (ResumeThread(pi.thread) == 0xffffffffu) {
        TerminateProcess(pi.process, 93);
        die("ResumeThread", GetLastError(), 90);
    }
    WaitForSingleObject(pi.process, 0xffffffffu);
    U32 code = 93;
    GetExitCodeProcess(pi.process, &code);
    out_begin(); s_("child exit="); u_(code); out_end();
    CloseHandle(pi.thread);
    CloseHandle(pi.process);
    ExitProcess(code);
}
