/* Native ARM64 bounded CPU test for NWOAS S140.  No CRT and no disk I/O. */
typedef unsigned long U32;
typedef unsigned long long U64;
typedef void *H;
typedef U32 (__stdcall *THREAD_FN)(void *);
#define IMP __declspec(dllimport)
IMP H GetStdHandle(U32);
IMP int WriteFile(H, const void *, U32, U32 *, void *);
IMP H CreateThread(void *, U64, THREAD_FN, void *, U32, U32 *);
IMP U32 WaitForMultipleObjects(U32, const H *, int, U32);
IMP int CloseHandle(H);
IMP int QueryPerformanceCounter(long long *);
IMP int QueryPerformanceFrequency(long long *);
IMP U32 GetActiveProcessorCount(U32);
IMP void ExitProcess(U32);

static volatile U64 results[8];

static U32 __stdcall worker(void *arg) {
    U64 x = 0x9e3779b97f4a7c15ULL ^ ((U64)arg + 1);
    for (U32 i = 0; i < 25000000; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        x += 0x6a09e667f3bcc909ULL;
    }
    results[(U64)arg] = x;
    return 0;
}

static char *append_u64(char *p, U64 value) {
    char rev[24];
    U32 n = 0;
    do { rev[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = rev[--n];
    return p;
}

static char *append(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

void mainCRTStartup(void) {
    H threads[8];
    U32 ids[8], written = 0;
    long long begin = 0, end = 0, frequency = 0;
    char line[256], *p = line;
    U32 active = GetActiveProcessorCount(0xffff);
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    for (U64 i = 0; i < 8; i++) {
        threads[i] = CreateThread(0, 0, worker, (void *)i, 0, &ids[i]);
        if (!threads[i]) ExitProcess(2);
    }
    if (WaitForMultipleObjects(8, threads, 1, 120000) != 0) ExitProcess(3);
    QueryPerformanceCounter(&end);
    for (U32 i = 0; i < 8; i++) CloseHandle(threads[i]);
    U64 checksum = 0;
    for (U32 i = 0; i < 8; i++) checksum ^= results[i];
    U64 elapsed_us = frequency > 0 ? (U64)(end - begin) * 1000000ULL / (U64)frequency : 0;
    p = append(p, "S140 CPU PASS active="); p = append_u64(p, active);
    p = append(p, " threads=8 elapsed_us="); p = append_u64(p, elapsed_us);
    p = append(p, " checksum="); p = append_u64(p, checksum);
    p = append(p, "\r\n");
    WriteFile(GetStdHandle((U32)-11), line, (U32)(p - line), &written, 0);
    ExitProcess(active >= 8 ? 0 : 4);
}
