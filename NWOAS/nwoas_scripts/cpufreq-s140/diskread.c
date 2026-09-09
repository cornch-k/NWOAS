/* Native ARM64, read-only, no-buffering storage validation for NWOAS S140. */
typedef unsigned char U8;
typedef unsigned long U32;
typedef unsigned long long U64;
typedef unsigned short W;
typedef void *H;
typedef U32 (__stdcall *THREAD_FN)(void *);
#define IMP __declspec(dllimport)
#define INVALID_HANDLE ((H)-1)
IMP H GetStdHandle(U32);
IMP int WriteFile(H, const void *, U32, U32 *, void *);
IMP int ReadFile(H, void *, U32, U32 *, void *);
IMP H CreateFileW(const W *, U32, U32, void *, U32, U32, H);
IMP int SetFilePointerEx(H, long long, long long *, U32);
IMP int GetFileSizeEx(H, long long *);
IMP H VirtualAlloc(H, U64, U32, U32);
IMP int VirtualFree(H, U64, U32);
IMP H CreateThread(void *, U64, THREAD_FN, void *, U32, U32 *);
IMP U32 WaitForMultipleObjects(U32, const H *, int, U32);
IMP int CloseHandle(H);
IMP int QueryPerformanceCounter(long long *);
IMP int QueryPerformanceFrequency(long long *);
IMP U32 GetActiveProcessorCount(U32);
IMP void ExitProcess(U32);

enum { THREADS = 8, READ_SIZE = 65536, READS_PER_THREAD = 128 };
static volatile U64 results[THREADS];
static volatile U32 failures[THREADS];
static const W path[] = L"C:\\Windows\\System32\\ntoskrnl.exe";

static U32 __stdcall worker(void *arg) {
    U64 id = (U64)arg;
    H file = CreateFileW(path, 0x80000000, 3, 0, 3, 0x28000000, 0);
    U8 *buffer = VirtualAlloc(0, READ_SIZE, 0x3000, 4);
    long long size = 0;
    if (file == INVALID_HANDLE || !buffer || !GetFileSizeEx(file, &size) || size < READ_SIZE) {
        failures[id] = 1;
        if (file != INVALID_HANDLE) CloseHandle(file);
        if (buffer) VirtualFree(buffer, 0, 0x8000);
        return 1;
    }
    U64 slots = (U64)size / READ_SIZE;
    U64 hash = 0xcbf29ce484222325ULL ^ id;
    for (U32 i = 0; i < READS_PER_THREAD; i++) {
        U64 slot = (id * 17 + (U64)i * 29) % slots;
        U32 got = 0;
        if (!SetFilePointerEx(file, (long long)(slot * READ_SIZE), 0, 0) ||
            !ReadFile(file, buffer, READ_SIZE, &got, 0) || got != READ_SIZE) {
            failures[id] = 2;
            break;
        }
        for (U32 at = 0; at < READ_SIZE; at += 4096) {
            hash ^= buffer[at];
            hash *= 0x100000001b3ULL;
        }
    }
    results[id] = hash;
    CloseHandle(file);
    VirtualFree(buffer, 0, 0x8000);
    return failures[id];
}

static char *append(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *append_u64(char *p, U64 value) {
    char rev[24]; U32 n = 0;
    do { rev[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = rev[--n];
    return p;
}

void mainCRTStartup(void) {
    H threads[THREADS]; U32 ids[THREADS], written = 0, failed = 0;
    long long begin = 0, end = 0, frequency = 0;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
    for (U64 i = 0; i < THREADS; i++) {
        threads[i] = CreateThread(0, 0, worker, (void *)i, 0, &ids[i]);
        if (!threads[i]) ExitProcess(2);
    }
    if (WaitForMultipleObjects(THREADS, threads, 1, 120000) != 0) ExitProcess(3);
    QueryPerformanceCounter(&end);
    U64 checksum = 0;
    for (U32 i = 0; i < THREADS; i++) {
        CloseHandle(threads[i]); checksum ^= results[i]; failed += failures[i] != 0;
    }
    U64 elapsed_us = frequency > 0 ? (U64)(end - begin) * 1000000ULL / (U64)frequency : 0;
    U64 bytes = (U64)THREADS * READS_PER_THREAD * READ_SIZE;
    char line[256], *p = line;
    p = append(p, failed ? "S140 DISK FAIL active=" : "S140 DISK PASS active=");
    p = append_u64(p, GetActiveProcessorCount(0xffff));
    p = append(p, " bytes="); p = append_u64(p, bytes);
    p = append(p, " elapsed_us="); p = append_u64(p, elapsed_us);
    p = append(p, " failed="); p = append_u64(p, failed);
    p = append(p, " checksum="); p = append_u64(p, checksum); p = append(p, "\r\n");
    WriteFile(GetStdHandle((U32)-11), line, (U32)(p-line), &written, 0);
    ExitProcess(failed ? 4 : 0);
}
