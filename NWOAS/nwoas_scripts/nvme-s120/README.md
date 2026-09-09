# S120: verify and repair install2 EOF, flush NTFS, then apply

S119 hardware report: target guard passed. install.swm after reboot still matched original.
install2.swm length was 564695040 instead of 564691107 (+3933), digest
1d82445d7bbda4bf920af15d61c563f002c84b34bb43dcc84aeefadbb3661203.
Original plus zero padding does NOT produce that digest. Prefix correctness is unknown.

NWTRIM accepts only drive-letter S117SRC/install2.swm, exact original digest, direct mode,
and original or 4KiB-rounded size. It hashes only original-length bytes. Mismatch returns1
without any mutation. Match reopens the file exclusively with buffering/write-through,
checks size unchanged, sets exact EOF and flushes the file. Unbuffered file handles cannot
seek to the non-sector-aligned EOF (Microsoft SetFilePointerEx documentation).
APPLY120 first passes unchanged NWGUARD. If prefix mismatch only, recopy install2 once
using xcopy /J then repeat NWTRIM. Other failures stop. NWVFLUSH revalidates the exact
NTFS partition/serial/size before flushing the volume, then both full hashes are checked.
Only then does the S118/S119 scoped cleanup and DISM apply continue. Old started markers stop retries.
Final report also retained on target and volume flushed. No ESP/BCD or first-boot changes.
HARDWARE-UNVERIFIED. ARM64 builds and host stub tests are not hardware proof.
Tests: test_trim.py (10 cases), inherited target guard tests (12 cases).
References:
https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex
https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile
https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
