# S195 storage integrity benchmark (native Windows ARM64)

IOTEST.EXE is a no-CRT ARM64 console executable that writes exactly 256 MiB
to one fixed new file, flushes it, reopens it unbuffered and verifies every
64-bit word against a deterministic offset-dependent pattern. It follows the
memory-s180/memtest and cpufreq-s164 tooling: clang cross-compile, dlltool
kernel32 import library, lld-link, kernel32 imports only, no WDK.

Status: built locally and host-tested. Not deployed. Not run on the Mini.
The Mini is running Cinebench and must not be interrupted; nothing in this
directory launches, uploads or schedules anything.

## What it does, in order

1. Prints `S195 START` with the path, size, block size, QPC frequency and
   the 300 s deadline.
2. Checks that `C:\ProgramData\NWOAS` exists and is a directory. It does not
   create the directory.
3. Reads free space for that volume and refuses to start if fewer than
   512 MiB are available (256 MiB file plus 256 MiB margin).
4. Allocates one 1 MiB buffer with VirtualAlloc (64 KiB granularity, so
   sector aligned for NO_BUFFERING) and checks the alignment.
5. Opens `C:\ProgramData\NWOAS\IO195.DAT` with CREATE_NEW, GENERIC_WRITE,
   no sharing, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH. If the file
   already exists the open fails with error 80 and the tool exits without
   touching it.
6. Writes 256 blocks of 1 MiB. Each 8-byte word is `s195_pattern(byte
   offset)`, a splitmix64 finaliser over the offset.
7. Calls FlushFileBuffers, prints `S195 WRITE END`, closes the handle.
8. Reopens read-only with FILE_FLAG_NO_BUFFERING and OPEN_EXISTING, checks
   GetFileSizeEx equals 256 MiB, reads 256 blocks and compares every word.
9. Prints `S195 READ END`, then `S195 SUMMARY` and `S195 PASS exit=0` only if
   the read loop completed with all 33554432 words verified.

Every exit path closes any open handle and frees the buffer, then prints
`S195 PASS exit=0` or `S195 FAIL exit=N`. Failures print `S195 ERROR <what>
err=<GetLastError>` before that line.

## Output lines

Only start, space, write end, read end, summary and the final verdict are
printed. There is no per-block log.

```
S195 START path=C:\ProgramData\NWOAS\IO195.DAT size=268435456 block=1048576 qpc_hz=N deadline_s=300
S195 SPACE avail_bytes=N
S195 WRITE END code=0 bytes=268435456 offset=268435456 done=0 err=0 expected=0 actual=0 words=0 elapsed_us=N
S195 READ END code=0 bytes=268435456 offset=268435456 done=0 err=0 expected=0 actual=0 words=33554432 elapsed_us=N
S195 SUMMARY qpc_hz=N size=268435456 write_us=N read_us=N words_verified=33554432 first_mismatch=none
S195 PASS exit=0
```

On a read mismatch, `S195 READ END` carries `code=4 offset=<byte offset of
the first bad word> expected=<pattern> actual=<read value>` and the run exits
12. On a short transfer, `code=2 offset=<block> done=<bytes reported>`. On an
API failure, `code=1 err=<GetLastError>`. On deadline expiry, `code=3` with
`offset` at the block boundary reached; a timeout can happen before a block is
issued or right after a block returned, and it never counts as a PASS.

| exit | meaning |
|------|---------|
| 2 | QueryPerformanceFrequency failed |
| 3 | parent directory missing or not a directory |
| 4 | free-space query failed or under 512 MiB available |
| 5 | VirtualAlloc failed or unaligned |
| 6 | CREATE_NEW failed (80 = file already exists) |
| 7 | write loop failed (see WRITE END code) |
| 8 | FlushFileBuffers failed |
| 9 | console output failed |
| 10 | read-only reopen failed |
| 11 | file size is not 256 MiB after write |
| 12 | read loop failed or not every word verified |
| 13 | CloseHandle failed |
| 14 | VirtualFree failed |
| 15 | shared 5 min budget exceeded between the write and read loops (around flush/reopen) |

## The 256 MiB test file is not deleted

The tool never deletes anything, including its own output. IO195.DAT stays
on disk after every run, including failures, so the on-disk bytes can be
analysed against the pattern. Because CREATE_NEW refuses to overwrite, a
second run on the same machine fails with exit 6 until the operator removes
or renames the file by hand. The tool has no repeat option; main chooses
runs and candidates, and each run needs a fresh path state.

Space budget: 256 MiB for the file plus a 256 MiB margin is required before
the file is created. Nothing else is allocated on disk. RAM use is one 1 MiB
buffer plus the image.

## Time bound and what it does not cover

There is one 300 s wall budget for the whole run. The tool takes a single
QueryPerformanceCounter reading before the write loop and measures every
later check against it, so the write loop, FlushFileBuffers, the reopen and
the read loop all draw down the same budget; the budget is never reset
between phases. The budget is checked before each transfer, including the
first, and again after each transfer returns, so a call that returned only
after the budget expired, including the final read, is a timeout and cannot
count as a PASS. Backwards or negative QPC readings fail closed with an expired
sentinel. Unsigned elapsed subtraction avoids signed overflow, and frequency
and reporting products are checked before multiplication.

The budget bounds how much more work the loop will issue after the deadline
passes. It does not and cannot bound a WriteFile, ReadFile or
FlushFileBuffers call that is already inside the kernel: a synchronous call
that hangs in the storage stack will hang this process with it. This tool is
not a DPC watchdog proof. A pass shows that 256 MiB of write-through
unbuffered writes and reads completed with correct data within the budget on
this run. It says nothing about DPC latency, timeouts inside the NVMe
driver, or behaviour under a different queue depth.

No raw disk or volume handle is opened. No path is chosen at runtime. No
temp directory is used. No environment file is read.

## Build and test

```
sh build.sh          # cross-builds IOTEST.EXE with clang/lld from Homebrew LLVM
python3 test_host.py # host tests: iocore.h logic + the real wrapper under a mock
```

test_host.py has two layers. First it compiles iocore.h with the host cc
(with UBSan on) and drives s195_run through ctypes: 2000 pattern vectors
against an independent Python splitmix64; a full 256 MiB write then all-word
read verify (pass); short write and short read; API failure with error 112
and 5 propagated; the shared-budget deadline, checked before and after each
transfer with the strict bound so equal elapsed is still in bound; the first
transfer of a loop timing out when a shared budget is already spent, with
nothing verified and no PASS; a run where every block transfers but the final
read returns past the budget, which is a timeout and not a PASS even though
the data was correct; a backwards clock that must fail closed without signed overflow; the bounded budget and ticks-to-microseconds helpers
at extreme inputs; a flipped bit at block 200 reporting the exact first
mismatch; first mismatch winning over later ones; a zeroed final block;
argument guards including a non-positive deadline.

Second, it compiles the real iotest.c through wintest.c with `-DIMP=` and
mock kernel32 bodies, built plain and with ASan/UBSan, and runs seven
scenarios against the actual CreateFileW/WriteFile/ReadFile/FlushFileBuffers/
CloseHandle path: a pre-existing file refused by CREATE_NEW with no write
issued, a missing parent, insufficient headroom, a short write, a flush
failure, a reopen failure, and a fully successful all-word read. Each
scenario asserts that only the fixed owned path is ever opened, that the
write open uses GENERIC_WRITE, no sharing, CREATE_NEW and
NO_BUFFERING|WRITE_THROUGH and the read open uses GENERIC_READ,
FILE_SHARE_READ, OPEN_EXISTING and NO_BUFFERING, and that every handle is
closed and the buffer freed on every exit path. Finally it checks the built
EXE is an ARM64 PE and that the embedded path uses single UTF-16 separators
with no doubled separator anywhere.

## Files

- iocore.h: pattern, shared-budget write/verify loop, clock/arithmetic helpers, pass rule. Pure C, no OS calls.
- iotest.c: Win32 wrapper and mainCRTStartup. IMP is overridable so the host mock can supply stub bodies.
- kernel32.def: the 15 kernel32 imports.
- build.sh: cross-build only.
- wintest.c: host mock of the Win32 wrapper; compiles iotest.c with stub kernel32 bodies.
- test_host.py: host tests (iocore.h logic and the wrapper mock).
- IOTEST.EXE, manifest.json: built artifact and its SHA256.

## Artifact hash history

Main verifies the SHA256 of IOTEST.EXE against manifest.json before any
upload. The current artifact after the review fixes is
`d1d3e081e89b1cae5090be8b83230c917902c16446ce0f1f8d952e7839b987af`
(10752 bytes). The original pre-review candidate was
`fd8be7cda63fa972a66164c7135830bcc1284d6f6921b2c208c27b31d2b7fe9d`
(9728 bytes). Neither has been run on hardware; no hardware pass is claimed.

## Main final review before deployment

Backwards or negative QPC now fails closed instead of returning zero elapsed. Microsecond reporting saturates on overflow. Full host suite, including the actual wrapper plain and ASan/UBSan, passed after this change. Artifact SHA256: `effd43ff105cb5d4ede2d72a9f415eac02557012f0b419254688f53f47fa2bb8`. Upload scripts create a new owned executable and verify its digest; CREATE_NEW storage file refusal remains. Hardware pending.
