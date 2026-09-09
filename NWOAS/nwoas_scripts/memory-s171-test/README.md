# S171 user-mode memory integrity test

Implemented directly by main agent after Opus4.8 API safeguard refusal; no model fallback or reword retry.
Native ARM64, no CRT, only kernel32 imports. Requests 64..8192 MiB (default4096),1..10 passes(default3), refuses unless physical availability includes an extra1GiB. Allocates private committed VirtualAlloc memory, writes and verifies every64-bit word, with index/pass-dependent pattern and full comparison plus sum. Releases memory, returns nonzero on failure, checks600s budget every256MiB. Progress every256MiB. This is user-mode virtual memory testing; it does not identify physicalPFNs or prove all mapped high RAM was exercised. Paging/faults are included in timing.

`sh build.sh`; `python3 test_host.py` tests actual header pattern against2000 independently calculated Python vectors and13 argument cases, plus ARM64 PE machine. Imports independently inspected.

Hardware results pending. upload01..03 then upload-verify deliver a known-hash file to a new exact C:\NWOAS-S171 directory. No settings, drivers or disks are modified by the memory test.
