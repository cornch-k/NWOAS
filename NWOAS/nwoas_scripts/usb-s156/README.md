# S156 FL1100 ERDP-segment event coherence candidate

S155 correctly discovered all four Windows ERST entries, but invalidated all
four 4 KiB event segments on every USBSTS.EINT or IMAN0.IP pending read. The
hardware run remained at a frozen Windows loading ring for more than seven
minutes and never connected NWOS, with no fatal marker.

S156 is a limited A/B candidate that reduces maintained event bytes. Neither
the cause of S155's stall nor full multi-segment correctness is established. It reads RTSOFF, ERSTSZ, ERDP, and every validated ERST entry; then it
invalidates only the segment containing the masked ERDP address. When ERDP is
within the final 64-byte cache line, it also invalidates the next segment in
ERST order so the no-Link-TRB boundary is visible. The ERST table is cleaned
before descriptor reads. USB-C D83, S154 poll coherence, S149 NVMe fast path,
eight CPUs, and S140 clocks are unchanged.

Pinned image:

- `build/m1n1-s156-fl1100-erdp-segment.bin`
- size: `2146304`
- SHA-256: `e8a81a9f744a3a5f540bf8424e2342417db9e558a0ff60c8e0c517bd5c720748`

Success requires normal Windows boot, NWOS connection, both xHCI controllers
and root hubs at Code 0, sustained USB-A and USB-C input, and no watchdog beyond
the S154 failure interval.

Limitation: hardware ERDP may lag the driver software cursor. A batch drain
across segment boundaries without an ERDP update can still read stale TRBs;
the boundary heuristic is not a complete coherency solution. Source-string
checks and compilation do not establish runtime correctness.

## S156 first hardware run and Claude CLI collaboration

- Run: `logs/usb-s156-20260909-223352.M0tJOT`.
- NWOS connected; device inventory job 1 and hardware job 2 exited 0.
- User confirms sustained Magic Trackpad pointer movement this boot.
- Both xHCI controllers/root hubs Code0; Apple MI_01/MI_02 and pointer HID OK;
  MI_00 remains CM_PROB_FAILED_START.
- 8 cores / 8 logical; CPU test 91,176 us; 64 MiB read 207,367 us, zero failures.
  Short read test is not a raw SSD throughput benchmark.
- Long stability and ERST wrap remain unproven. Keep this boot running.
- Claude Code 2.1.263 review sessions dispatched with explicit model IDs
  claude-opus-4-8 and claude-fable-5-1; results/modelUsage pending. No fallback
  model configured. Prompts/results in nwoas_scripts/claude-s156.
