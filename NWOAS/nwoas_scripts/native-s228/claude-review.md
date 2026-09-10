# S228 review: frontend.c/.h, test_bridge.c, test_faults.c, test.py, README.md

Read-only independent review, 2026-09-10. No source in native-s228 or its
dependencies was changed. No hardware, device, UI or credential access. All
compilation and execution happened on private copies under /tmp (dylib, ASan/
UBSan directed test, Python differential, plus eight extra probes). The
snapshot reviewed is the one whose README reports 11,663 comparisons / 5,867
submissions; the /tmp rerun reproduced exactly those numbers.

This is an uninstalled, callback-driven composition exercised only against fake
RAM and the S139/S124 Python oracle. Nothing here is boot or hardware evidence.

## Verdict

No defect was found in the composed semantics that the current tests and the
Python oracle define. Callback ordering, aggregate IRQ, fatal suppression,
reset/flush/cancellation propagation, the cache tri-state and the "no CQ
publication after fatal" property all hold under code reading and under
directed probes of paths the shipped tests do not reach.

Two concrete items should be fixed or documented before an owner integrates it:

- F1 (contract, low): the doorbell fallback returns NOT_HANDLED for more than
  SQ1/CQ1. 64-bit doorbell writes (including 64-bit writes to the admin SQ0
  doorbell) and every 32-bit offset from 0x1010 to 0x3ffc are also handed to
  the I/O owner, whereas the oracle ignores them. The README describes only
  the SQ1/CQ1 case.
- F2 (contract, benign today): the ring's `pending` callback reenters the S209
  control model (`nwoas_control_pending`) from inside the control model's own
  `setup` and `reset` callbacks, contrary to model.h's "callbacks must not
  reenter the model". It is harmless with the current S209 code but is an
  undocumented dependency on that code's shape.

The rest are test-coverage gaps and owner requirements, listed separately.

## What was compared

- native-s228: frontend.c/.h, test_bridge.c, test_faults.c, test.py, test.sh,
  README.md, differential-result.json.
- native-s209 model.c/.h (control), native-s227 admin_ring.c/.h (ring),
  native-s226 admin_state.c/.h and INTEGRATION.md, native-s225
  admin_payload.c/.h, the prior S226/S227 reviews and resolutions.
- Oracle: nvme-s139/controller.py (CQ-ack DPC rule, MAX_Q=1) over
  nvme-s124/controller.py (`update_irq`, `fatal`, `reset`, CC/AQA/ASQ/ACQ,
  doorbell decode), nvme-s124/writable_namespace.py (`set_cache`/`flush`),
  transport-s123 `NamespacePair`.

## Semantics walk-through (C vs oracle)

| Topic | Oracle (S139/S124) | S228 | Result |
|---|---|---|---|
| IRQ level | `any(q.ien and q.pending)` gated by INTMS bit0 and PCI CMD bit10 | `io_pending || (ring.active && ring.pending)` fed to S209, which applies mask/CMD; wrapper adds `!CFS && !backend_fault && !ring.fatal` | same, plus fatal suppression |
| Fatal | `csts|=2; irq(False)`; doorbells ignored; CQ pending left as is (PCI status bit3 stays) | `nwoas_frontend_fault`: `backend_fault=1; csts|=2; irq(false)`; doorbells/poll ignored; PCI status bit3 still reflects pending | same (probe P4: pci_status=0x8 with CFS) |
| CC.EN 1→0 | queues, AER cleared; CSTS=0; features kept | `reset_io` → `io_pending=0; backend_fault=!ok` → ring reset (partial) → S209 sets CSTS=0 → wrapper reapplies CFS if cancellation failed | same on success; failure path adds CFS with RDY=0 (test_faults:27) |
| Recovery | CC.EN=0 clears CFS | CC.EN=0 with successful `reset_io` clears `backend_fault` and CFS | same (test_faults:29) |
| CC.EN 0→1 | validates CC/AQA/ASQ/ACQ, else CFS | same via S209; `setup` additionally refuses while `backend_fault` | same, stricter after backend fault |
| Shutdown | SHST 1 → flush → SHST 2; failure CFS; skipped if CFS or already complete | identical (S209) with `ops.flush` | same |
| Set Features 6 | `set_cache` raises → status 6; cache unchanged | `set_cache` 0 → status 6; -1 → `nwoas_frontend_fault` then the CQE write is refused by `write_memory` → ring fatal | tri-state as documented; -1 publishes nothing (test_faults:33) |
| Delete/Create CQ1 | queue removed/added, `update_irq` | `reconcile` then `io_pending=0` on `changed&2` | same; Delete SQ1 leaves io_pending (probe P5) |
| Doorbell decode | width!=32 or off≥0x1010: ignored; SQ0/CQ0 local; SQ1/CQ1 local (Python owns I/O) | width 64 or off≥0x1010: NOT_HANDLED; SQ0/CQ0 local; SQ1/CQ1 NOT_HANDLED; all ignored when !RDY/CFS/backend_fault | F1 |
| Doorbell read | 0 | 0 (HANDLED) | same |
| CQ0 ack | retire + irq, never processes | `nwoas_ring_ack`, never polls (test.py asserts no fetch on ack) | same |
| Held AER | SQ head advances, no CQE, no IRQ | same via S227 | same |

## Findings

### F1. NOT_HANDLED is broader than the README's SQ1/CQ1 contract (concrete, low)

frontend.c:78-83. After S209 returns NOT_HANDLED for any aligned write at
offset ≥0x1000, the frontend returns NOT_HANDLED when `w!=32 || o>=0x1010`,
before the RDY/CFS gate. Probe P3 on an enabled controller:

```
w64@0x1000=NOT_HANDLED  w64@0x1008=NOT_HANDLED
w32@0x1010=NOT_HANDLED  w32@0x1014=NOT_HANDLED  w32@0x1ffc=NOT_HANDLED
```

The oracle ignores all five (`width!=32 or offset>=0x1000+8*(MAX_Q+1): return`).
Consequences:

- A 64-bit write to 0x1000/0x1004 is an admin doorbell. The frontend owns
  admin doorbells but declines it and passes it to the I/O owner, which has no
  legitimate action for it. If the owner decodes NOT_HANDLED as "SQ1/CQ1 by
  offset parity" without checking width, a 64-bit write at 0x1000 becomes an
  SQ1 doorbell.
- The gate ordering means these accesses are NOT_HANDLED even when disabled or
  fatal, so the owner must also apply the RDY/CFS rule itself for them, while
  for 32-bit SQ1/CQ1 the frontend applies it (HANDLED when disabled).

Neither test asserts these return codes (test_faults.c:43-44 covers only
32-bit 0x1008/0x100c; the 50,000-case fuzz discards results; test.py never
writes doorbells other than 0x1000/0x1004). Smallest fix: return HANDLED for
`w!=32 || o>=0x1010` (mirrors the oracle) so NOT_HANDLED means exactly
"32-bit SQ1/CQ1 doorbell on a ready, nonfatal controller". If the wider
contract is intentional, the README and frontend.h must say that the owner
receives, and must ignore, 64-bit and qid≥2 doorbells regardless of RDY/CFS.

### F2. Ring `pending` reenters the control model from control callbacks (contract)

Call chains, both inside `nwoas_reg_write`/`nwoas_control_reset`:

- CC.EN 0→1: `nwoas_reg_write` → `cb.setup` → `nwoas_ring_configure` →
  `cb.pending(false)` → `update_pending` → `nwoas_control_pending` → `irq_update`
  → `cb.irq`.
- CC.EN 1→0 / init / full reset: `queues_reset` → `cb.reset` →
  `nwoas_ring_reset` → `cb.pending(false)` → same chain.

model.h states "Callbacks must not reenter the model". Today this is benign:
`nwoas_control_pending` only assigns `c->pending` and calls `irq_update`, the
value written (false) equals what `queues_reset` already wrote, and the setup
path reads `c->pending` only through `irq_update` afterwards. It becomes a
real bug only if S209 ever caches state across the callback or the frontend's
`update_pending` starts producing true during setup (it cannot now because
`reset` cleared `io_pending` and the ring is empty). Recommend one of: (a) a
comment in frontend.c naming this as a known, tolerated reentry with the
invariant that makes it safe, or (b) have `pending()` only mark dirty and let
`setup`/`reset` wrappers call `update_pending` after the ring call returns.
No behavior change is needed for the current code.

### F3. Test-coverage gaps (tests, not code)

All passed when probed in /tmp, so these are gaps, not failures:

1. `nwoas_frontend_reset` with failing `reset_io` (frontend.c:62) is untested;
   test_faults.c:27 only covers the CC.EN=0 path. Probe P1: `backend_fault=1
   csts=2 irq=0 active=0` — correct.
2. `nwoas_frontend_init` with failing `reset_io` (frontend.c:56) is untested;
   `initialize()` clears `reset_fail` first. Probe P2: returns 0 with
   `backend_fault=1 csts=2` — matches the README.
3. Multi-command batch where a later command faults: shipped tests fault only
   the first command. Probe P4 (`fail_write=3`, three Abort commands): first
   CQE published, `sq_head=2 cq_tail=1 pending=1`, CFS set, IRQ low — matches
   the oracle's behavior (irq raised then dropped).
4. Aggregate IRQ with I/O pending is exercised only in test_faults.c; the
   differential never sets `io_pending`, so the oracle never checks the
   `io_pending || admin` combination. Acceptable for a fake-RAM oracle without
   an I/O owner, but the README's "against actual S139 IRQ level" claim covers
   the admin-only term.
5. F1's return codes are unasserted (see above).
6. test.py counts `deferred` (held AER submissions) but neither asserts nor
   reports it; `full_cq`/`wraps` are reported only. A minimum count assertion
   would make the "AER without completion" claim checkable from the result
   file.
7. `inspect(7)` returns `ring.fatal` and is compared with `bool(c.csts&2)`;
   the differential never enters fatal, so this comparison is always
   false==false. CFS itself is compared through the 0x1c register read, which
   is the meaningful check.

### F4. README/header wording

- README: "SQ1/CQ1 return NOT_HANDLED ... and are ignored when disabled/fatal"
  is accurate only for 32-bit 0x1008/0x100c (F1).
- frontend.h: `nwoas_frontend_reset` is called a full reset but it does not
  clear PCI `command` (INTx disable) or BAR probe flags, since S209's
  `nwoas_control_reset` leaves them and only `init` zeroes them. If the owner
  uses it for a bus-level reset, INTx-disable state carries over. Document
  which reset it is (controller-level with feature defaults), or note that a
  bus-level reset requires re-init.

## Things confirmed as correct

- `read_memory`/`write_memory` refuse once `backend_fault` is set, so after a
  -1 cache result (or any `nwoas_frontend_fault`) the ring cannot publish a
  CQE: the next write fails, S227 `fail()` runs, `pending(false)` and `fault()`
  follow. Verified by test_faults.c:33 (`!writes`) and code path.
- `irq` wrapper gates on CFS, `backend_fault` and `ring.fatal`; every later
  path that would raise the line (INTMC, PCI CMD, `io_pending`, `reconcile`,
  `nwoas_control_pending`) goes through it. test_faults.c:38-41 and probe P6/P7.
- Cancellation ordering on CC.EN=0: `reset_io` runs before the ring and admin
  descriptors are cleared and before S209 clears CSTS; failure is latched
  after S209 returns by frontend.c:86/62/56, the only three callers of
  `queues_reset`. Success on a later CC.EN=0 clears the latch (recovery
  matches the oracle).
- `setup` refuses while `backend_fault`, so a faulted backend cannot reach
  RDY=1 without a successful cancellation first.
- `nwoas_frontend_io_pending` cannot resurrect pending on an absent or
  non-IEN CQ1 (test_faults.c:22-23); `reconcile` clears it on every CQ1
  transition and leaves it on SQ1 transitions (probe P5).
- The `pending` callback ignores its argument and recomputes from ring state.
  This is more faithful than trusting `v`: after S227 `fail()` the ring's
  `pending` count is intentionally kept, so PCI status bit 3 matches the
  oracle's `any(q.ien and q.pending)` even under CFS.
- Doorbell gate `!(csts&1) || (csts&2) || backend_fault` equals the oracle's
  `not csts&1 or csts&2` plus the stricter backend latch; SQ0 tail is
  truncated to 32 bits as the oracle masks it.
- `poll` is a separate entry point, never called from ack, honoring the S139
  DPC rule; the differential asserts zero fetches on every ack.
- Init validates all mandatory callbacks and the policy before any mutation,
  mirrors S227's set/get cache pairing rule, and requires a 16 KiB-aligned BAR.
- Freestanding ARM64 relink of the five objects has no undefined imports
  (test.sh check reproduced by the shipped `out/undefined.txt` being empty).

## Future physical I/O owner requirements (not S228 defects)

O1. **Decode NOT_HANDLED narrowly.** Until F1 is fixed, treat only 32-bit
    0x1008/0x100c as SQ1/CQ1 and ignore everything else, and apply the
    RDY/CFS rule yourself for those. The oracle also makes a doorbell to a
    nonexistent SQ1/CQ1 fatal ("invalid SQ tail"); the owner must call
    `nwoas_frontend_fault` in that case to keep parity.

O2. **`flush` must drain in-flight I/O writes first.** Under the oracle every
    write is synchronous, so shutdown flush had nothing outstanding. With an
    asynchronous or direct-DMA owner (S101 path), `ops.flush` returning true
    before outstanding NS1 writes land would let CSTS.SHST=complete be
    reported with unpersisted data.

O3. **`reset_io` and `reconcile` must cancel or drain, not just mark.** The
    frontend clears `io_pending` and bumps ring lifecycle immediately after
    either returns true; a late completion from the old generation must not
    be published to the new ring (S226 INTEGRATION item 4). `reset_io`
    failure leaves the guest with RDY=0 CFS=1 and the mapping must remain
    valid until a later successful cancellation.

O4. **Same lock, same thread, no reentry.** All frontend entry points,
    `nwoas_frontend_io_pending` and `nwoas_frontend_fault` must run under the
    HV big lock; no hook may call back into the frontend. Stale-generation
    `io_pending` reports must be rejected by the owner before calling in.

O5. **Memory callbacks.** `contains` must accept validated guest RAM only;
    `write` must be all-or-nothing and emit the 2-byte status word as one
    16-bit store after `barrier`, which must be at least a DMB-ISH-strength
    store barrier visible to the guest CPU that polls the CQ (inherited S227
    O1/O7).

O6. **IRQ reassert timing.** The frontend emits level changes synchronously;
    the S208 50 µs reassert gap and INTx edge behavior are entirely the
    owner's.

## Suggested follow-ups (no code changed)

1. Fix or document F1; add assertions for 64-bit and ≥0x1010 doorbell return
   codes in both enabled and disabled state.
2. Add the P1/P2/P4 cases from this review to test_faults.c (full-reset
   failure, init failure, mid-batch fault).
3. Add a comment or restructuring for F2.
4. Assert `deferred>0` in test.py and include it in differential-result.json.

None of the above is hardware or standalone-boot evidence; the composition
remains uninstalled and untested against a real guest.
