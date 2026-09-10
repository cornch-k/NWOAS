# S227 review: admin_ring.c/.h, test_bridge.c, test_faults.c, test.py, README.md

Read-only source review. This file is the only thing written inside the
repository. No hardware, USB, UI, credentials or raw device logs were touched
and no source was edited. Independent checks were compiled and run from /tmp
against copies of the S227/S226/S225 sources and the local nvme-s139/nvme-s124
Python oracle with fake RAM only.

Scope as reviewed: an uninstalled, callback-driven admin SQ0/CQ0 engine. It
fetches 64-byte commands through a caller callback, runs S226/S225, resolves at
most two PRP spans, writes payload and CQE through callbacks and reports a
pending level. It has no MMIO, ANS, IRQ line, transport, threads or real
memory. Nothing below implies any hardware result; passing every test here
establishes parity with the Python model, not controller compatibility.

## Verdict

No correctness defect was found in admin_ring.c relative to the S139/S124
`Controller.process`/`write` path and `prp.resolve`, within the stated
contracts. Full-CQ gating, backlog, held AER, body-then-status publication,
all-spans-before-any-write PRP validation, fetch/write/reconcile failure
handling and reset/generation behavior all hold under the shipped suite and
under the additional probes listed below. The README numbers reproduce exactly
on a fresh /tmp build.

The findings are about test strength and contract wording, not engine logic:
the 50,000-command sanitizer fuzz exercises exactly one status path (measured),
the differential never reaches the fatal/CFS path or a multi-command doorbell,
and two API comments overstate or understate what the code does. Owner
requirements that this component cannot satisfy by itself are listed
separately.

## What was compared

Sources read in full: `admin_ring.c/.h`, `test_bridge.c`, `test_faults.c`,
`test.py`, `test.sh`, `README.md`, `differential-result.json`;
`native-s226/admin_state.c/.h`, `INTEGRATION.md`, `claude-resolution.md`;
`native-s225/admin_payload.c/.h`; `nvme-s139/controller.py`;
`nvme-s124/controller.py` (`write`, `process`, `admin`, `valid_queue`,
`fatal`, `update_irq`); `nvme-s124/prp.py`.

Independent execution from /tmp copies (symlinked oracle directories):

1. `test.sh` end to end: dylib build, differential, ARM64 freestanding link
   with empty undefined-symbol list, ASan/UBSan fault binary. Results match the
   checked-in `differential-result.json` field for field (11,830 comparisons,
   7,273 commands, 21 configurations, 3,176 acks without fetch, 2,259 full-CQ
   submissions, 411 phase wraps) and the fault binary prints 17 directed
   fixtures.
2. Status histogram of the 50,000-command LCG fuzz in `test_faults.c`,
   reproduced with the same seed and generator (see F1).
3. Four additional differential probes against the actual S139 `Controller`
   that the shipped `test.py` does not perform: one SQ-tail doorbell carrying
   seven commands including two AERs, Identify and Get Log Page into a full
   8-entry CQ; SQ 8 / CQ 2 with five commands in one doorbell followed by six
   ack/redrive rounds; fatal parity for invalid SQ tail, invalid CQ head and
   CQ head beyond pending, including ignored doorbells afterwards and recovery
   through CC disable plus reconfigure; Create CQ1, Create SQ1, Delete SQ1,
   Delete CQ1 in one doorbell. All probes matched ring state, IRQ level and
   every byte of fake RAM after each step.

## Semantics walk-through (C vs Python)

| Aspect | Python S139/S124 | C admin_ring | Result |
|---|---|---|---|
| CQ full gate | `cq.pending >= cq.size-1` stops loop | `pending < cq_depth-1` loop condition | same, one slot unused |
| Per-doorbell budget | `for _ in range(256)`, deferred counts | budget 256, `consumed++` also on deferred | same |
| CQ ack | retire, `update_irq`, never `process` | retire, `pending(bool)`, never poll | same |
| Ack bounds | `value>=size` fatal; `consumed>pending` fatal | `head>=cq_depth` fail; `count>pending` fail | same |
| Tail bounds | `value>=size` fatal | `tail>=sq_depth` fail | same |
| Held AER | `result is None: continue`, SQ head advanced, no CQE, no irq update | `deferred: continue`, same | same |
| SQ head in CQE | post-increment head | post-increment head | same |
| PRP1 | `0<prp1`, `prp1&3==0`, first span `min(len, 4096-off)` | same | same |
| PRP2 | required only if remainder, nonzero, page aligned | same | same |
| Span end at exactly 2^64 | accepted if `ram_contains` agrees | rejected (`a<=UINT64_MAX-n`) | documented conservative divergence, unreachable with a real RAM validator |
| Invalid PRP | `Result(INVALID_FIELD)`, no payload write | status 2, result 0, bytes 0, no payload write | same |
| Payload write failure | exception after head advance, fatal | `fail()` after head advance | same |
| Fetch failure | exception before head advance, fatal | `fail()` before head advance | same |
| CQE write | one 16-byte write | 14-byte body, barrier, 2-byte status | intentional ordering improvement, bytes identical |
| Fatal | CFS set, `irq(False)`, doorbells ignored | `fatal=true`, `pending(false)`, `fault()`, doorbells return 0 | same |
| CC disable | queues and AER cleared, features kept, CSTS cleared | `reset(false)`: ring cleared, admin queues and AER cleared, features kept, fatal cleared | same |
| Controller reset | features reset to `{6:1}` | `reset(true)`: features zeroed, `features[6]=1` | same |
| Configure | at CC.EN 0→1 from AQA/ASQ/ACQ, depth 2..256, 4 KiB aligned, nonzero, contained | explicit call, same checks, rejected when active or fatal | same checks; register derivation is the owner's |

The reconcile callback has no Python counterpart. It runs after the S226 state
mutation and before the CQE is published, so the I/O owner learns about a new
or deleted queue before the guest sees the completion. That is the right order.

## Findings

### F1. The 50,000-command sanitizer fuzz exercises one status path (test claim)

`test_faults.c` fills all 64 command bytes from an LCG, so byte 1 and bytes
16..23 are nonzero with probability about 1-2^-72. `nwoas_admin_execute` and
`nwoas_admin_payload` reject such commands in the preamble before any opcode
dispatch. Measured on a /tmp copy with the identical generator: 0 of 50,000
commands pass the preamble and all 50,000 complete with status 0x2. The loop
therefore proves ring wrap, publication ordering and ack bookkeeping under
sanitizers, which is useful, but it does not reach Create/Delete, features,
cache, AER, Identify or Get Log Page under ASan/UBSan. The README sentence
"50,000 additional malformed commands through valid rings under sanitizers" is
literally true and should be read that way. The directed fixtures and the
Python-side 15x700 fuzz in `test.py` do reach those paths, but `test.py` runs
the dylib without sanitizers. Same class as S226 F1.

### F2. Differential never reaches fatal/CFS or multi-command doorbells (test gap)

`test.py` compares `bool(c.csts & 2)` with `r.fatal` on every step, but the
Python controller never enters CFS in the run: `Mem.read`/`Mem.write` assert
instead of failing, `submit()` refuses SQ overrun, and doorbell values are
always in range. The fatal path exists only in `test_faults.c`, without an
oracle. Likewise every `submit()` advances the tail by one and rings once, so a
doorbell carrying several commands, a held AER and a full CQ at the same time
is only covered by one three-command fixture in `test_faults.c`. My probes
(section "What was compared", item 3) show both behaviours agree with S139, so
this is a coverage gap, not a defect. The `deferred` counter in `test.py` is
computed but not written to `differential-result.json`; S226 reported its
held-AER count and S227 does not.

### F3. `nwoas_ring_ack` documentation (contract)

The header says tail/ack/poll return ">=0 number consumed". `nwoas_ring_ack`
returns 0 on every successful call regardless of how many entries were
retired. Harmless, but an owner reading the comment may try to use the value.

### F4. Entry points after a failed init (contract)

`nwoas_ring_init` correctly leaves the object untouched when it returns false.
Every other entry point then dereferences `r->cb.pending` and friends without
checking. On a zero-initialised object whose init was rejected, `reset`,
`configure`, `tail`, `ack` and `poll` call a NULL function pointer. The header
should say that a rejected init leaves the object unusable until a successful
init, or the functions should guard on a valid-callback marker. Tests always
init successfully first, so this is a documentation/hardening item.

### F5. Redundant per-access `range()` checks (note, not a defect)

`configure` validates the whole SQ and CQ once; `poll` re-validates each
64-byte fetch and 16-byte CQE slot through `contains`. This is deliberate
conservatism against mapping changes between calls and costs one callback per
access. Keep it, but the README's "mapping validity must remain stable during
one call" already permits the owner to change mappings between calls, so the
per-access check is what actually makes that statement safe. Worth one
sentence in the header.

### F6. Reproduced Python leniencies (note, not a defect)

Inherited unchanged from S124/S139 and therefore not differential failures:
SQ tail passing SQ head (guest overrun) is not detected by either side; an
SQ-tail doorbell equal to the current tail re-enters the loop with no work;
a CQ depth smaller than the SQ depth leaves backlog that nothing drains until
the guest rings the SQ tail again (see O4). A held AER is never completed by
either implementation because neither has an event source.

## Things confirmed as correct

- Both PRP spans are validated (`resolve`) before the first payload write;
  the `deny_second` fixture and the fake-RAM zero check at the PRP1 target
  prove no partial write, and status 0x2 is published with result 0.
- Payload, 14-byte CQE body, barrier, 2-byte status order is asserted by the
  fault harness on every CQ write and matched the Python 16-byte image in
  11,830 checked-in plus all independent comparisons.
- Failure of any of the four write points (span 1, span 2, body, status) or
  of the fetch or reconcile callback sets fatal, lowers pending, calls fault,
  publishes nothing, and leaves the phase byte untouched. Later doorbells
  return 0 and perform no reads.
- Held AER consumes the SQ slot, publishes nothing, does not raise pending,
  and a second AER returns 0x105 exactly as Python.
- CQ ack never fetches; `redrive` and `poll` are the only drains. Backlog with
  CQ depth 2 and SQ depth 8 drained correctly over six ack/redrive rounds.
- `reset(false)` preserves features and clears queues, AER, ring and fatal;
  `reset(true)` also clears features to `{6:1}`; both bump `lifecycle` and the
  S226 generations only for present queues.
- `configure` is transactional, rejects active or fatal engines, depths
  outside 2..256, unaligned or zero bases and spans outside `contains`
  (bit sweep over 64 positions plus near-UINT64_MAX case).
- Completion fields: DW0 result, DW1 zero, SQ head post-increment, SQID 0, CID
  copied from the fetched command, `(status<<1)|phase`; phase flips on CQ tail
  wrap to 0.
- The combined ARM64 freestanding object has no undefined imports (reproduced).

## Future owner requirements (not defects in S227)

O1. **Publication store width.** The two-byte status store at CQE offset 14 is
the guest-visible commit. The owner's `write` must emit it as one 16-bit
single-copy-atomic store (an ARM64 `strh`), never two byte stores: for
status 0x105 and 0x10c the high byte is nonzero and a byte-wise store could let
the guest see the new phase with a stale status high byte. `barrier` must be a
real store-ordering barrier with respect to the guest's CPUs (DMB ISH or
stronger) and the 14-byte body must be fully visible before it.

O2. **Fault scope.** In Python, CFS gates every doorbell including I/O queues.
The `fault` callback must set CFS and disarm I/O fast paths; `pending(false)`
only lowers the admin contribution. After a reconcile failure the S226
descriptor state is ahead of the I/O owner; the only recovery is
`nwoas_ring_reset`, which the owner must call on CC.EN 1→0 (`full_reset=false`)
and on controller/PCI reset (`full_reset=true`). `configure` refuses while
fatal, so reset must precede it.

O3. **Register derivation.** `configure` takes ASQ/ACQ/AQA-derived values but
does not check CC.MPS, CSS, IOSQES or IOCQES as `Controller.write(0x14)` does
(fatal on mismatch). The control owner must keep those checks, call
`configure` exactly at CC.EN 0→1 and set CSTS.RDY only if it returns true.

O4. **Backlog scheduling.** With ACQS smaller than ASQS, commands beyond CQ
capacity remain in the SQ until the next tail doorbell, identically to S139.
If that matters for the chosen guest, an explicitly scheduled owner context
must call `nwoas_ring_poll` after acks; do not call it from the CQ-ack path
itself, which is the S139 DPC rule.

O5. **IRQ aggregation.** `pending(bool)` is admin CQ pending only. The owner
must combine it with I/O CQ pending and IEN, INTMS/INTMC and PCI INTx disable
into one line, equivalent to `update_irq`, and preserve the measured S208
50-microsecond reassert gap. Recompute on reset, Delete CQ1 and IEN changes.

O6. **Held AER lifetime.** A held AER is dropped on reset, as in Python. No
completion for a previous lifecycle's AER may ever be published; the lifecycle
counter is informational and does not enforce this.

O7. **Memory callbacks.** `contains` must accept validated guest RAM only,
never MMIO or carveout; `read`/`write` must be all-or-nothing as documented,
since the ordering argument in O1 assumes no partial stores; mappings and
callback objects must not change during one call.

O8. **Generations.** Compare the full 64-bit S226 generations before encoding
the low 12 bits into S160 flags (S226 F4). Lifecycle and generations are
distinct counters; neither alone protects against stale completions.

O9. **Optional hardening.** SQ overrun (tail passing head) is undetected in
both implementations; an owner may choose to fault on it.

## Suggested follow-ups (no code changed)

1. Make the sanitizer fuzz reach admin logic: zero byte 1 and bytes 16..23 in
   most iterations, or seed the opcode/dword fields from the S226 fuzz table,
   and assert a status histogram with more than one bucket (F1).
2. Add an oracle-backed fatal case and a multi-command doorbell case to
   `test.py`, and report the held-AER count in `differential-result.json` (F2).
3. Fix the ack return-value comment and document post-failed-init behaviour
   (F3, F4).
4. Carry O1 through O9 into the next INTEGRATION document as owner
   obligations; none of them is implemented by this component.
