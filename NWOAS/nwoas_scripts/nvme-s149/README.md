# S149 target-side NVMe I/O fast path

S149 keeps synthetic NVMe controller discovery and the admin queue in the
existing Python model. After Windows creates I/O SQ/CQ 1, the host sends the
queue bases and depths to m1n1 once. Subsequent SQ1 and CQ1 doorbell traps are
handled inside EL2 and submit directly to ANS2 through `nvme_rw_guest()`.

This removes the synchronous MacBook USB CDC and Python round trip from every
Windows storage command. Reads are allowed across namespace 1. Writes remain
restricted independently in C to LBA 53839104 through 59968629. Flush and FUA
preserve the S130 durability behavior.

The admin and fast-path interrupt levels are separate and ORed before virtual
IRQ 900 injection. CQ acknowledgements only retire completions; remaining work
resumes one command at a time from the 5 kHz hypervisor tick, outside the
Windows completion doorbell trap. Queue generations, CQ interrupt enable,
shutdown state, offset PRP1 mappings, phase-last CQE publication, and fatal
physical-controller errors are all tracked in the target path.

Run `python3 test_fastpath_source.py` before building. The S149 launcher expects
the 1 MiB transfer build and verifies its exact SHA-256 before chainloading:
`345d9678d2f3f2d1828522d20f87cb35d5892747f3aac66303840859b15236ea`.
