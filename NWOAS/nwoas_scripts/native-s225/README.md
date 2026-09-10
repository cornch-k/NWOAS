# S225 — native admin response payload foundation

This is a freestanding C response builder for Identify (CNS 0/1/2) and Get Log
Page (error, SMART, firmware) matching the current S139/S124 controller and
S123 NamespacePair. It is **not installed**, does not publish CQ entries, does
not read guest memory, and removes no runtime host dependency by itself.

Its policy describes the existing synthetic controller: primary capacity,
optional RAM namespace capacity, MDTS, primary read-only attribute and volatile
cache advertisement. It is not the physical Apple ANS identity or tag budget.
The live memory-s180 `CachedPair` inherits this admin path and does not apply
its unused `TransportReadOnly` wrapper: current NS1 NSATTR is zero.
The old synthetic model/firmware strings and response quirks are preserved for
byte compatibility; those strings are not descriptions of current storage policy.

Callers supply disjoint command, policy, result and output buffers and at least
4096 output bytes. Unsupported opcodes return NOT_HANDLED, invalid API arguments
return -1, and owned commands return a status plus a bounded payload length.
There is no allocation, DMA, file/device I/O, transport or queue state.
The ARM64 build uses `-ffreestanding -fno-builtin` and checks for no undefined
imports; a hosted compiler may turn local loops into libc calls. Errors leave the data buffer untouched; the result length is zero.

Validation compares 12,182 commands against the actual Python stack, including
paired/single namespace modes, successful full payload bytes and malformed
commands. A further 48,864 commands cover 12 policy profiles spanning
read-only/writable, single/paired and MDTS 0/4/8 configurations. ASan/UBSan and canary tests cover lengths, all opcode/CNS byte values,
null pointers and large policy values. The ARM64 object has no undefined imports.

This is the response-construction portion of the future local admin controller.
Submission/completion lifecycle, PRP validation, queue/IRQ ownership, namespace
I/O and reset/generation integration still belong to their current owners.

Policy requires nonzero capacities below 2^40 LBAs and MDTS 0..8. Read the
result only on return 1; initial Invalid Field on return 0/-1 is not a command
completion. The caller must dispatch NOT_HANDLED to another owner.
