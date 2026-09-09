# S140 CPU frequency initialization

S140 calls m1n1's existing T8103 `cpufreq_init()` before the Windows UEFI
payload starts.  The UEFI chainload path otherwise skips the call made by the
normal Linux payload path.  The module records both cluster registers before
and after initialization and refuses to boot if the call fails, a transition
remains busy, or the expected E/P default P-states (5/7) are not reached.

It keeps the S139 eight-core payload, single NVMe queue, DPC-safe completion
acknowledgement path, and USB-A rescue input unchanged so the hardware test
isolates the CPU clock initialization.

`CPUSTRES.EXE` is a no-CRT native ARM64 test. It starts eight bounded compute
threads, waits for all of them, and reports the active processor count, elapsed
microseconds, and a result checksum. It performs no disk I/O. The executable is
included on the host-RAM tools namespace for subsequent boots as `CPU140.CMD`.
