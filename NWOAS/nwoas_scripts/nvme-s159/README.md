## Correction after hardware comparison

The original candidate and rebuilt control below omitted EXTRA_CFLAGS=-DNWOAS_NVME_MAX_BLOCKS=256. They only permitted64KiB physical transfers while the host advertised1MiB, and failed before Windows diagnostics. They are invalid performance comparisons. The original S158 nvme.o disassembly permits256blocks; rebuilt default permits16. Corrected nvme.o with256 has identical disassembly to original. Corrected candidate build/m1n1-s159-submit-deferred-256.bin SHA2561acd193daa19864b577a7d9d548de577b76ce309f1e784eda6e08af9efaac510, actual-C harness passes; hardware comparison now pending.

# S159 candidate: submission deferral

BUILT IN SEPARATE WORKTREE, NOT HARDWARE TESTED. Patch is against current S158 source. Running hardware remains S158.

The candidate removes synchronous execution of one physical/host-link I/O from the SQ-tail MMIO handler. The handler keeps its bounds check, records producer cursor and sets deferred. The existing 5 kHz HV tick processes commands in SQ order. CQ acknowledgement path is unchanged. Completion is still published only after physical execution, including existing flush/error/lifecycle rules.

This is a scheduling experiment, not a native Windows ANS2 driver. It does not remove the global HV lock or blocking physical I/O inside the tick; other vCPUs can still be delayed. Moving work off the submit MMIO path does not guarantee that a guest DPC's elapsed time excludes it. It could reduce throughput or expose timer/queue-progress defects. Do not raise watchdog thresholds or queue budget to hide failures.

S158 observations: per-execute NS1 max4.04ms, NS2max31.50ms, backlogmax1; later0x133 subtype1 after monitorHPD event and interactive debug/display servicing. These are confounded observations, not proof of submission blocking as root cause. First finish uninterrupted baseline with automatic sleep off. Then require build checks and a behavioral queue-progress test before a read-mostly hardware comparison. Preserve S158 binary/launcher for recovery. No SSD layout changes are part of this patch.

Build completed with explicit RUSTUP_TOOLCHAIN=1.88.0-aarch64-apple-darwin, make -j2. Worktree /Volumes/X31/NWOAS/m1n1_windows-s159. Default Rust toolchain was not changed. Both S158 rebuilt baseline and S159 candidate use the same fresh build dependencies. Binary sizes2146304 bytes.
- S158 rebuilt baseline SHA256:92d260c1d10773e4c97868bc5b3d1e000efb23bf1cc8deb6d580546790c06800.
- S159 final candidate SHA256:f1375e8af0b948cbaff5d428ea441c60fd43d8360f67dda83f9ea6b4c1c9c360.
Actual-C MMIO/poll harness passes candidate, fails old baseline on inline-execution assertion. Existing S149/S150 source suites:21 checks pass; the legacy test_only_sq_doorbell_starts_io assertion intentionally fails because it requires the behavior this candidate removes. Do not report22/22 unchanged tests passing. The actual-C harness validates the new deferral contract, but real timer scheduling and disk behavior still require hardware comparison. Full build has existing warnings; final candidate removes the newly unused ctx warning.
