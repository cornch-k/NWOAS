# Next dependency: NS2 RAM data, then service transport

This is a source-audited plan, not an installed S231 component.

The next host dependency is not the NS1 physical SSD transfer path: S149 already
executes that path in target C. S230 now owns synthetic PCI/control/admin.
`memory-s180/link_module.py:_fast_link_handle` still sends every NS2 command
through the MacBook. NS2 supplies a 8448 x4096-byte (34,603,008-byte) RAM disk
and the automation service. Keep that service working during migration.

## Ownership that must be preserved

- transport-s123/transport.py: partition starts at LBA256, length8192; Windows
  may write FAT metadata in [256,8448). Writes are limited to16 blocks; MBR/gap
  writes are refused except exact mailbox operations. LBA128 is the existing
  job/stdout mailbox, including token, CRC, sequence and completion state.
- file-delivery-s168: LBA127 is a single-block request; reads intersecting
  [129,145) overlay the response window. Do not treat every non-128 LBA as
  static RAM. Current query_module.py also runs at NS2 rendezvous.
- NS2 status/results, PRP validation and bounds must match the existing adapter
  composition, not merely the base LinkNamespace class. NS1 bounds and ANS
  ownership must remain untouched.

## Smallest useful implementation

Reserve a target-owned RAM image outside guest allocations during initial host
setup. Serve ordinary image reads and allowed FAT writes locally, using full
span validation before mutation. Keep dynamic request/mailbox/response handling
behind explicit callbacks initially. This removes bulk tools/FAT copy traffic
while preserving the existing interactive worker.

A read can intersect both static RAM and the dynamic window. Forwarding that
entire read to the old host image is incorrect once target-only FAT writes
have diverged from the host copy. Read the target image and overlay only the
dynamic byte intersection, or maintain an explicit coherent image owner. Never
permit two unsynchronized writable images. A target response buffer and bounded
request callback make the ownership easier to prove.

After that, rehost the mailbox state machine and move optional host transport
onto an explicit service channel. Worker polling should return an empty local
frame when no job is pending; a cable disconnect must not stall storage. The
remaining host boot/DCP setup, virtual interrupt model and Windows drivers are
separate milestones. Removing NS2 data transfers alone is not standalone boot.

## Required qualification

Differential tests against the fully composed current namespace must cover
all ranges crossing127/128/129/145/256/end, write refusal without partial
mutation, PRP boundary failures, reset with a pending mailbox message, repeated
sequence delivery, and image-plus-overlay coherence. Then perform an isolated
boot with target local/forwarded counters, tools-file hashes, acknowledged
worker round trips and the S230 SSD/memory/reboot qualification. Preserve S230
as fallback; do not disconnect the current host runtime to test a hypothesis.
