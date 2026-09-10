# S226 review: admin_state.c/.h, test.py, test_bounds.c, README.md

Read-only source review. This file is the only thing written inside the
repository. No hardware, USB, UI, credentials or raw device logs were touched
and no source was edited. Independent checks were compiled and run from /tmp
against copies of the S226/S225 sources and the local Python oracle with
bytecode writing disabled.

Scope as reviewed: an uninstalled, pure command-state model driven through
fake callbacks. It does not fetch from an SQ, resolve PRPs, write CQ entries,
raise IRQs or touch ANS. Nothing below implies any hardware result.

## Verdict

No semantic divergence found between `admin_state.c` and the inherited
nvme-s139/nvme-s124 `Controller.admin` and reset paths for the S139
configuration (MAX_Q = 1). The manifest hashes match the reviewed files, so
the recorded 11,739-command differential pass corresponds to this source.

Findings are: one sanitizer coverage claim that is not met (F1), one API
contract inconsistency (F2), one missing result signal that the live S160 fast
path will need (F3), and a set of integration requirements that the C result
does not and cannot carry (F6). None change bytes or statuses of the reviewed
model.

## What was compared

Oracle actually exercised by the live chain and by test.py:

- `nvme-s139/controller.py` subclasses `nvme-s124/controller.py` and overrides
  only the CQ-head doorbell path and `MAX_Q = 1`. `admin()` and `reset()` are
  the nvme-s124 versions (`controller.py:45-48`, `145-196`).
- Runtime reset paths: CC.EN=0 write (`controller.py:119-120`) clears SQ/CQ
  dicts and the AER list, preserves `features`. `Controller.reset()` is only
  called from `__init__`; no runtime caller exists in nwoas_scripts. So C
  `full_reset=true` corresponds to controller construction and
  `full_reset=false` to the CC-disable path.
- Cache callbacks: live `memory-s180/link_module.py:52-54` `CachedPair`
  forwards `set_cache`/`get_cache` to `WindowWritableNamespace` unchanged, so
  test.py's `NS(NamespacePair)` shim is equivalent for admin purposes.
- Payload: `native-s225/admin_payload.c` handles opcodes 6 and 2; S226 calls it
  first and dispatches everything else itself.

## Semantics walk-through (C vs Python)

Verified by reading both sides and by directed execution against the imported
S139 Controller (all matched):

- Flags/MPTR: Python rejects at `controller.py:147` before dispatch. C relies on
  the payload's own check for opcodes 6/2 and `admin_state.c:37` for the rest.
  Same status (2) and same precedence.
- Create CQ1/SQ1: NSID != 0, QID != 1, PC = 0, bad base/depth, existing queue,
  IV != 0 (CQ) and CQID != 1 (SQ) all give status 2, in the same order as
  `controller.py:149-161`. Probed PC = 0, CQID = 0 and CQID = 2 directly.
  `queue_valid()` adds a wrap guard before calling `contains`; Python passes
  the un-wrapped Python int, so for any sane `contains` this is equivalent, and
  the S209 model documents the same deliberate boundary.
- Delete: QID != 1 or absent gives 2; Delete CQ1 with SQ1 present gives 0x10c
  (`controller.py:162-166`). Same.
- Features: FID 7 returns 0 for both Get and Set (MAX_Q-1 packed twice, which
  is 0 for MAX_Q = 1) and Set ignores the requested count; allowed FIDs are
  1,2,4,5,6,8,9,10,11; FID 6 with a cache callback validates dw10 upper bits and
  dw12..15, SEL 1 gives 1, SEL 3 gives 4, other nonzero SEL gives 2, Set with
  SEL != 0 or dw11 & ~1 gives 2, callback failure gives status 6 with result 0,
  success returns `get_cache()`. Other FIDs store/return dw11 wholesale without
  validating other dwords (probed FID 1 with dw12 != 0: both succeed).
  Matches `controller.py:167-184`.
- AER: second held AER gives 0x105; first is held with `deferred=true`,
  `status=0`, `bytes=0`. Reset drops it silently. Matches `controller.py:192-194`
  and `:120`.
- Abort: result 1, dw10 ignored. Unknown opcode: status 1. Matches.
- Reset: CC-disable via the real `Controller.write(0x14,0,32)` (not the manual
  dict clear test.py uses) leaves `features` intact and clears queues and AER;
  C `nwoas_admin_reset(s,false)` matches and increments generations only for
  queues that were present. `nwoas_admin_init` zeroes generations and restores
  `{6:1}`; matches `reset()`.
- Result struct layout: 12 bytes, offsets 0/4/8/10/11, matching the ctypes
  definition in test.py.

## Findings

### F1. Sanitizer run never reaches queue or cache code (test claim)

README: "100,000 malformed-command/canary cases under ASan/UBSan; API bounds
and queue-span overflow checks use fake memory callbacks only." Instrumenting
a /tmp copy of `admin_state.c` and running `test_bounds.c` unchanged gives:

| Path in admin_state.c | Hits in 100,000 cases |
|---|---|
| `queue_valid()` (incl. wrap guard) | 0 |
| Create CQ/SQ success | 0 |
| Delete CQ/SQ success | 0 |
| FID 6 cache-callback branch | 0 |

Cause: create/delete require `dw10 & 0xffff == 1`, which a uniformly random
dword hits once in 65,536 commands; `test_bounds.c:8` also passes NULL cache
callbacks. The only sanitized paths are the API preamble, flags/MPTR
rejection, the payload, generic features, AER and Abort. The differential run
(unsanitized `-O2` dylib) does cover these paths, but the README's
overflow-check statement is not backed by the sanitizer harness. Recommend
biasing `test_bounds.c` (force QID = 1 and aligned in-range bases on a
fraction of cases, install fake cache callbacks) and, ideally, building the
dylib used by test.py with sanitizers as well. Severity: medium for the
claim, low for the code.

### F2. NULL `contains` is a command error, NULL cache pair is an API error (contract)

`admin_state.c:32-33` returns -1 when exactly one of `set_cache`/`get_cache`
is set, but `queue_valid()` at `:22` treats a NULL `contains` as "not
contained", so every Create silently completes with Invalid Field. The header
does not say `contains` is mandatory. Either reject `!cb->contains` in the
preamble alongside the cache-pair check or document that a NULL `contains`
disables queue creation. Severity: low.

### F3. No signal for cache-state transitions (missing result field)

`nwoas_admin_result.changed` covers only SQ1/CQ1 descriptors. The live S160
fast path publishes `_namespace.cache_enabled` to the target in
`_fast_flags()` (`nvme-s160/guest_module.py:204`) and re-syncs after every
MMIO write. After a successful Set Features FID 6 the owner has no result bit
telling it the flags word changed; it must diff `get_cache()` itself or
re-publish after every FID 6 Set. Adding a `changed` bit for "cache state
may have changed" (or documenting the owner obligation) would close this.
Not a correctness defect in the model. Severity: low.

### F4. Generation counters differ in width and trigger from the live S160 scheme (integration)

The live stack already has queue generations: `fast_sq_generation` /
`fast_cq_generation` are 12-bit, wrap with `& 0xfff`, are bumped by object
identity comparison across each MMIO write (`guest_module.py:257-263`), and
force a fast-path disarm when they change. C generations are `uint64_t` and
are bumped per descriptor transition inside `execute`/`reset`. For one
Create or Delete per admin command these coincide, but a reset that clears
both queues bumps both C counters in one call, and the S160 word only carries
the low 12 bits. Integration must define the mapping (mask to 12 bits, compare
deltas, not values) and keep the disarm-on-change rule. README's statement
that counters alone do not prove stale-completion safety is correct.

### F5. test.py oracle strength (test)

- `reset(False)` at `test.py:51` re-implements CC-disable with dict clears
  instead of calling `c.write(0x14,0,32)`. My probe used the real write path
  and it matches, but the shipped test should drive the oracle, not a copy.
- The fuzz never produces the no-cache-callback path (bridge always installs
  both callbacks), so Set/Get FID 6 through the plain feature dictionary is
  only verified by inspection.
- Flush-failure injection is one directed case; the fuzz never toggles it.
- The bridge counts flushes but not `set_cache` invocations, so a spurious
  `set_cache(true)` call would go unnoticed (state would still match).
- PC = 0 and CQID = 2 creates are only reached via the 1-in-12 byte
  corruption; adding them to the directed list is cheap.
- The `differential-result.json` count is plausible (24 directed plus 12,000
  fuzz iterations minus about 300 resets) and its timestamp follows the
  reviewed sources.
Severity: low.

### F6. Integration requirements the C result cannot express (future work, not the reviewed code)

Python performs these as side effects of the same command; the C model
deliberately does not. They are listed so the owner contract is explicit:

1. **IRQ recomputation on Delete CQ1.** Python calls `update_irq()`
   (`controller.py:166`). Probed: CQ1 with 3 pending entries and IEN set
   asserts the line; deleting CQ1 (SQ1 absent) deasserts it. The owner must
   recompute the aggregate pending level whenever `changed & 2` is set, and on
   reset.
2. **Ring state reset on create.** Python creates fresh `SQ`/`CQ` objects
   (head = tail = 0, phase = 1, pending = 0). The C descriptor has no cursors;
   the owner must reinitialise its fetch/produce/phase state on every
   generation change and discard any SQ1 backlog on delete.
3. **Delete SQ with outstanding I/O.** In the current synchronous Python
   design no SQ1 command is in flight while an admin command runs. Once
   completion production moves to an async worker (S139 note), Delete SQ1,
   Delete CQ1 and CC-disable must cancel or drain in-flight I/O and drop
   completions tagged with an older generation.
4. **Held AER.** `deferred=true` means no CQ entry, but the SQ head must
   still advance (Python `sq.head` advances before `continue`,
   `controller.py:208-209`). The held CID is dropped on reset without
   completion, as in Python. No event is ever generated by either side.
5. **Cache failure mapping.** Python maps only `OSError`/`TimeoutError` from
   `set_cache` to status 6; any other exception escapes to `process()` and
   becomes CFS. The C boolean cannot distinguish these, so the callback must
   decide what is fatal and must guarantee the transactional property the
   header demands (Python's `WindowWritableNamespace.set_cache` guarantees it
   by flushing before assignment).
6. **Fast-path flags.** Any change to CQ1 IEN, cache state or queue
   generations must trigger the S160 `_fast_sync` equivalent (see F3/F4).
7. **Control-plane gating.** Python ignores doorbells unless `csts & 1` and
   not `csts & 2`; the S209 owner supplies that, and S226 assumes the owner
   only calls `execute` for commands fetched from a valid admin SQ.

### F7. Reproduced Python leniencies (note, not a defect)

Preserved quirks: features survive CC-disable (spec resets non-persistent
features); Set Features FID 7 ignores the request; non-FID-6 Set Features
ignore dw10 bits 8..31 and dw12..15 and store dw11 wholesale; Delete ignores
NSID and dw10 bits 16..31; Abort always reports "not aborted". These match
Python by design; any future spec-conformance change must be made on both
sides or the differential breaks.

## Things confirmed as correct

- State is never mutated on a -1 return; all validation precedes mutation.
- `changed` is set only on success paths; error paths leave it 0.
- `depth` fits `uint16_t` because it is bounded to 256 before the store;
  `depth*stride` is at most 16,384; `features[fid]` index is at most 11.
- Deferred results carry `status=0`, `bytes=0`, `result=0` and no output
  write; `test_bounds.c:22` asserts this.
- Output buffer is untouched on every non-payload path (test.py canary
  check at `:42`).
- `nwoas_admin_init` initialises every field of the state struct.
- Freestanding ARM64 link check in `test.sh:9-11` is enforced (empty
  `undefined.txt`), consistent with the S225 build-flag requirement.

## Suggested follow-ups (no code changed)

1. Make `test_bounds.c` reach create/delete/cache paths under ASan/UBSan and
   reword the README overflow-check sentence until it does (F1).
2. Reject NULL `contains` in the preamble or document it (F2).
3. Add a cache-changed indication or document the owner's re-publish duty (F3).
4. Write down the generation mapping to the S160 12-bit scheme (F4).
5. Drive CC-disable through `Controller.write` in test.py and add the
   no-callback FID 6 path and more failure injection to the fuzz (F5).
6. Carry the F6 list into the integration plan as owner obligations.
