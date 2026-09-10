# S226 — native admin command-state foundation

This freestanding C component extends S225 response construction with the
current synthetic NVMe admin command state: Create/Delete CQ1/SQ1, supported
Get/Set Features, cache-transition callbacks, one held AER and Abort. It matches
the inherited S139/S124 command semantics, including compatibility quirks.

It is **not installed in the HV or Windows**. It does not fetch commands from
an SQ, resolve PRPs, publish CQ entries, signal IRQs, access physical ANS or
remove a runtime host dependency. Descriptors in this component refer to the
existing 256-depth synthetic controller, not physical ANS tags.

The owner serializes state and non-reentrant callbacks. Creation validates
alignment, depth, overflow and the caller's memory range without dereferencing
a guest address. Descriptor transitions carry changed bits and generation
counters for later atomic reconciliation with the existing I/O owner. Those
counters alone do not prove stale-completion safety; integration must enforce
that lifecycle. Administrative SQ0/CQ0 setup remains the S209 control owner's
responsibility and is not duplicated here.

CC-disable-style reset clears queue descriptors and held AER, preserving
features. Full reset also restores the default feature dictionary. Neither
operation changes the cache backend itself. Set-cache callbacks must either
succeed or leave the prior cache state unchanged on failure. Return deferred
for a held AER means **do not produce a completion**. Callers must check the
API return before interpreting the command result. All buffers must be valid
and disjoint, including policy/state/result; initialization requires non-NULL
state. S225 policy and build constraints also apply.

## Reproducible validation

Run `bash nwoas_scripts/native-s226/test.sh`.

- 11,714 commands against the actual imported S139 Controller and current
  NamespacePair, comparing result/status/full payload and queue descriptors,
  generation deltas, features, held AER, cache state, set-cache/flush calls and cache-resync signals after every
  command or reset. Directed cases include CQ-in-use deletion and cache flush
  failure. 241 deferred AERs match Python's lack of completion.
- 32 additional commands exercise the generic FID 6 path without cache callbacks.
- Directed sanitizer cases reach 100 complete queue lifecycles, 200 range callbacks
  and 300 cache callbacks, including failures and address overflow.
- 100,000 malformed-command/canary cases under ASan/UBSan; API bounds and
  queue-span overflow checks use fake memory callbacks only.
- Combined S225/S226 freestanding ARM64 object has no undefined imports.

No hardware compatibility or performance claim is made for this component.
Before integration, add bounded queue fetch/completion production, validated
PRP writes for S225 payloads, IRQ state ownership, lifecycle cancellation and
existing NS2 behavior. Preserve the existing write-window guard and physical
backend; do not equate an admin-state model with a native storage driver.

Successful cache Set returns changed bit 2, including same-value Set, so the
owner must resynchronize fast-path flags. A NULL contains callback disables
queue creation; cache callbacks must both be present or both absent. See
`claude-resolution.md` for changes made after the independent review.
