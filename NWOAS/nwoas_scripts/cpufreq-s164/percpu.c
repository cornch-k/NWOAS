/* Bounded ARM64 per-logical-CPU measurement; no clock/power setting writes. */
typedef unsigned long U32;
typedef unsigned long long U64;
typedef void *H;
#define IMP __declspec(dllimport)
IMP H GetCurrentThread(void);
IMP H GetCurrentProcess(void);
IMP U64 SetThreadAffinityMask(H, U64);
IMP int GetProcessAffinityMask(H, U64 *, U64 *);
IMP U32 GetCurrentProcessorNumber(void);
IMP U32 GetActiveProcessorCount(U32);
IMP int QueryPerformanceCounter(long long *);
IMP int QueryPerformanceFrequency(long long *);
IMP H GetStdHandle(U32);
IMP int WriteFile(H, const void *, U32, U32 *, void *);
IMP void ExitProcess(U32);

__declspec(noinline) static U64 work(U64 id) {
    U64 x = 0x9e3779b97f4a7c15ULL ^ (id + 1);
    for (U32 i = 0; i < 25000000; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        x += 0x6a09e667f3bcc909ULL;
    }
    return x;
}

static char *text(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *number(char *p, U64 n) {
    char r[24]; U32 count = 0;
    do { r[count++] = '0' + n % 10; n /= 10; } while (n);
    while (count) *p++ = r[--count];
    return p;
}
static void emit(char *start, char *end) {
    U32 written = 0;
    if (!WriteFile(GetStdHandle((U32)-11), start, (U32)(end-start), &written, 0) ||
        written != (U32)(end-start)) ExitProcess(9);
}

void mainCRTStartup(void) {
    U64 allowed = 0, system = 0, aggregate = 0;
    long long frequency = 0;
    U32 active = GetActiveProcessorCount(0xffff);
    if (active != 8 || !GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system) ||
        (allowed & 255) != 255 || !QueryPerformanceFrequency(&frequency) || frequency <= 0)
        ExitProcess(2);
    char line[256], *p = text(line, "S164 CPU BEGIN active=");
    p=number(p, active); p=text(p," qpc_hz=");p=number(p,frequency);
    p=text(p,"\r\n");emit(line,p);
    for (U32 cpu = 0; cpu < 8; cpu++) {
        U64 previous = SetThreadAffinityMask(GetCurrentThread(), 1ULL << cpu);
        if (!previous) ExitProcess(3);
        U32 before = GetCurrentProcessorNumber();
        long long begin=0,end=0;
        if (before != cpu || !QueryPerformanceCounter(&begin)) ExitProcess(4);
        U64 result = work(cpu);
        if (!QueryPerformanceCounter(&end) || end <= begin) ExitProcess(5);
        U32 after = GetCurrentProcessorNumber();
        if (!SetThreadAffinityMask(GetCurrentThread(), previous) || after != cpu)
            ExitProcess(6);
        aggregate ^= result;
        p=text(line,"S164 CPU cpu=");p=number(p,cpu);
        p=text(p," elapsed_us=");p=number(p,(U64)(end-begin)*1000000ULL/(U64)frequency);
        p=text(p," checksum=");p=number(p,result);
        p=text(p,"\r\n");emit(line,p);
    }
    p=text(line,"S164 CPU ");
    p=text(p,aggregate == 14964600543233568963ULL ? "PASS" : "FAIL");
    p=text(p," aggregate=");p=number(p,aggregate);p=text(p,"\r\n");emit(line,p);
    ExitProcess(aggregate == 14964600543233568963ULL ? 0 : 7);
}
