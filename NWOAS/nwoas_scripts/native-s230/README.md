# S230 target-owned NVMe frontend

S230 links S209 control state, S225 payload construction, S226 admin state,
S227 bounded admin rings and S228 aggregate IRQ/frontend into the target C
runtime, connecting them to the existing S149 ANS data path. Supported synthetic
PCI/control/admin requests no longer rendezvous with the Python controller.
Unclaimed accesses fail closed. NS2 RAM/service transport and host boot/DCP
setup remain. This is EL2-mediated Windows, not standalone boot or a Windows
ANS/GPU/network driver. No end-to-end performance gain has been measured.

S229 first-boot testing passed. S230 fixes its Claude review F1/F2: no arming
printf through the tick path, and no unhandled post-shutdown SQ1 doorbell.
The source function prefix remains s229 because the adapter contract is shared.
Action23 identifies the S230 binary; action16 enables once before guest boot.

## Reproduction

Run prepare.py against the pinned bddf7f06 base and S217 companion patch, then
build.sh (Clang/LLD20.1.8, Rust1.88, two jobs). The prepared worktree is isolated;
existing worktrees and conflicting recorded binary hashes are refused. Run
test_adapter.sh and test_module.py; component differential/sanitizer/ARM64
checks are recorded under S227/S228. The live launcher is
../native-s230-admin-guest-test.sh and uses the unchanged S223 guest payload.

## Evidence and limits

First integration, 10GiB x3 memory and the ten-minute mixed test passed
(11 samples over 602.505147 seconds). Normal Windows restart preserved the
256MiB archive hash. Second-boot five-minute active reads passed 2555 iterations over300030ms.
Median64MiB read was125558us; maximum1177709us is an unresolved latency
outlier. These are logical compressed-file reads, not raw SSD throughput.
Final controller CSTS was1 and the target I/O error count was0.
Counter19 includes I/O doorbells, not only control/admin writes. Counters20/21
are admin command fetches and published completions; a held AER can leave a
one-command difference. Counter22 is CSTS plus sticky physical backend fault.

The adapter harness uses mock RAM, stage2 lookup and physical callbacks. It
checks actual control/MMIO source. test_process.py adds actual execute, process,
poll, IRQ, NS2 link validation and guest_ptr coverage with 28 directed cases.
It requires test_adapter.sh to generate the included production declarations.
Production constants and structures are extracted and their hashes recorded.
Physical I/O, proxy response, clock and IPA/DRAM callbacks remain mocked; do
not equate these injected errors with physical ANS fault injection. All target
entry points require bhl serialization and no CPU pin/switch during NS2 calls.
The existing physical LBA write window is unchanged. Physical backend faults
remain terminal until a fresh runtime boot.

Keep the current host runtime alive. S224 is the previous bounded-qualified
fallback. Never run a second launcher while the serial port is occupied.

Current live session: native-s230-admin-20260910-111548.TRiw43.link.
Both qualification jobs have acknowledged exit0; no reboot or mutation remains
queued. HV/runtime stays alive. Reboot inventory still reports USB Input
Device code10 and two unnamed code28 entries; VideoController returned no rows.
C: free at that inventory was3,584,143,360B. No driver improvement is claimed.
See NEXT.md for the source-audited NS2 ownership migration plan.
