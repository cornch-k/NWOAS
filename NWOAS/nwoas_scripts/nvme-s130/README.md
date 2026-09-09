# S130: bounded NVMe diagnostic logging

S129 reached the Windows 11 desktop, but its log contained several serial lines
per storage request.  At 115200 baud the observed 52 IOPS was close to the
amount of text the console could carry.  S130 keeps the S124 storage semantics,
write guards, S126 transport, and S129 firmware unchanged.  It samples routine
successful commands and queue doorbells while preserving controller state
changes and every error.

The first hardware run, while INTMS/INTMC were still logged for every request,
reduced cumulative host+guest time from about 19.1 ms/I/O to 5.5 ms/I/O (roughly
3.5x). The final policy samples those per-completion writes too. Compare the
cumulative `S97 STAT` values and desktop responsiveness on the next boot. This
experiment changes diagnostics only; it does not claim native ANS storage.

For the eight-core S133 path, higher-level ANS counters are sparse as well:
read/write counts and timing statistics retain early milestones and then emit
at 4096-unit intervals. Flushes retain the first eight samples and every 256th
sample. This prevents diagnostic output from serializing concurrent storage
work at 115200 baud.
