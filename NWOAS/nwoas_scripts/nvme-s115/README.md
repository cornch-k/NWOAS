# S115 file-cache comparison

This experiment runs the original S103 hypervisor and S102 payload. It does not combine S113 or S114 hardware changes.

`sh build.sh` builds NWREAD.EXE for Windows ARM64. The program hashes an input file without requesting input write access. Both modes allocate a 1 MiB VirtualAlloc buffer. `buffered` uses FILE_FLAG_SEQUENTIAL_SCAN; `direct` adds FILE_FLAG_NO_BUFFERING. The latter checks 64 KiB buffer alignment and logical-sector/request alignment before opening the file. All reads are synchronous. Each run checks SHA256(abc) and reports the complete input size and actual digest.

Microsoft documents [file buffering and alignment](https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering) and [ReadFile completion and EOF](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile). This flag does not bypass the storage driver, DMA path, or drive hardware cache.

HASH115.CMD runs five cases: old HeapAlloc-buffered tool once, aligned-buffered twice, aligned-direct twice. Source file is sources/install.swm, expected size 3987720636 and SHA256 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c. Results are assembled on X: RAM disk, copied once to USB, followed by automatic shutdown. Runtime output is not yet collected. Read every CASE_RC and digest; a finished marker does not mean a match. Guest elapsed_ms is not reliable wall-clock performance evidence.

If aligned-buffered passes while legacy fails, allocation/alignment or changed memory placement is implicated; do not attribute that result solely to cache bypass. If aligned-direct passes while aligned-buffered fails, focus on buffered-read/cache interaction, while recognizing that request placement and DMA behavior can also differ. If both fail, investigate below file caching and collect per-block evidence before another global hardware-flag change.
