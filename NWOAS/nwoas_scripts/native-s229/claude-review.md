# S229 review: adapter.inc, prepare.py, test_adapter.c, test_adapter.sh

Read-only independent review, 2026-09-10. No source in native-s229, its
dependencies, the m1n1-admin-s229 worktree or the live host service was
changed. No hardware, device, UI or credential access. The only execution was
`test_adapter.sh` on a private copy under /tmp/s229rev (native-s209/225/226/
227/228/229 copied; it slices the worktree's hv_vm.c/nvme.c read-only). It
printed the same PASS line the main session reports.

Snapshot reviewed: `native-s229/adapter.inc` is byte-identical to
`m1n1-admin-s229/src/nwoas_s229_adapter.inc`; the worktree is detached at
bddf7f06 with the S217 companion patch applied; `out/m1n1-s229-admin.bin`
sha256 6ae35d6a… matches manifest.json; build.log has only the three
pre-existing unused-function warnings. `module.py`, `test_module.py`,
`query_module.py` and `native-s229-admin-guest-test.sh` appeared during the
review and were read because NS2/IRQ ownership cannot be judged without them.

This is a build/integration candidate. Nothing below is boot evidence.

## Verdict

No blocker for an isolated test boot was found in the adapter itself. Every
item the task listed was traced to code and holds under the stated locking
model (all entry points under `bhl`, NS2 proxy round trip keeps `bhl` held,
no CPU pinning during the run). Three items should be fixed or consciously
accepted before the boot; none of them is expected to trigger on a
well-behaved Windows/UEFI guest, but each turns a benign event into a
fail-closed stop:

- F1 (medium): a `printf` can now run from the 5 kHz tick path
  (`s229_poll` → ring poll → Create SQ → `s229_reconcile` → S149 arm →
  `printf("NWOAS-S149 NVMe fastpath armed …")`). hv.c:468 documents that
  printf in `hv_tick` desynced the proxy. It only fires when a Create SQ was
  left in backlog by a tail write (budget/CQ-full), which stornvme does not
  do, but the path exists.
- F2 (low): an SQ1 doorbell after shutdown completes (SHST=10, CC.SHN later
  cleared, CC.EN still 1) falls through both C owners and reaches the
  Python `_unexpected_write` hook, which raises. Guest misbehaviour only.
- F3 (low, test gap): `test_adapter.c` mocks `nwoas_nvme_fastpath_process`,
  so the real S149 process/execute → `update_irq` → frontend fault/pending
  propagation, the PCI path and poll-path arming are not exercised against
  the extracted code. The S228 differential test covers the frontend side.

The rest of this file is the per-topic trace the task asked for, with the
evidence lines, so the main session can check each claim quickly.

## Integration shape (what actually runs)

- prepare.py splices hv_vm.c at three anchors: forward declarations +
  `s229_fast_irq_update()` early-return at the top of
  `nwoas_nvme_fastpath_update_irq` (hv_vm.c:1364-1368), `s229_poll()` first
  in `nwoas_nvme_fastpath_poll` (1567), `#include` before
  `nwoas_nvme_fastpath_control` (1572), actions 16-22 (1579-1585), and
  `s229_mmio` / `s229_cq_ack_unarmed` at the top of
  `nwoas_nvme_fastpath_mmio` (1672-1673). `nvme_guest_page_pa` is made
  external (nvme.c:588). Makefile adds the five frontend objects. All anchors
  are asserted unique; the diff matches exactly.
- The C intercept is reached only through `SPTE_PROXY_HOOK_*` entries
  (hv_vm.c:3821, 4078), i.e. only because module.py registers HOOK tracers
  over ECAM 0x700000000+1M and BAR 0x700100000+16K with the same keys S160
  used; `add_tracer` keys `mmio_maps[zone, ident]`, so the S160 handlers are
  replaced, not stacked. Anything that leaks past C raises in Python
  (fail-closed).
- Guest is SMP (MADT advertises 8 CPUs, smp-s133 gate). Serialization relies
  on `hv_exc_entry` taking `bhl` for every trap including the FIQ tick
  (hv_exc.c:1837), and on `_hv_exc_proxy` not releasing `bhl` around
  `uartproxy_run` (hv_exc.c:81-129; it is released only in the pin/switch
  wait loops, hv_exc.c:140/161). Assumption: `hv_pinned_cpu == -1` during
  the run (only hv.c:381 sets it). If the operator pins a CPU from the shell
  mid-boot, another CPU can enter the adapter while an NS2 link is in
  flight; the adapter then fails closed (`link_busy` → `reset_io`/
  `reconcile` return false → CFS) rather than corrupting state.

## Owner callback reentry suppression

- `s229_control_local` sets `s229_owner_callback` around every call into
  `nwoas_nvme_fastpath_control` (adapter.inc:11-16). Actions 0 and 1 both end
  in `nwoas_nvme_fastpath_update_irq` (hv_vm.c:1590, 1663), whose first line
  is `if (s229_fast_irq_update()) return;` and `s229_fast_irq_update`
  returns true immediately while the flag is set (adapter.inc:124). So no
  `nwoas_frontend_fault`/`nwoas_frontend_io_pending` call is made from inside
  a frontend callback. Confirmed for both callers: `s229_reset_io` (from the
  control model's reset callback) and `s229_reconcile` (from the ring's
  reconcile callback).
- The frontend's own reentry rule (model.h "callbacks must not reenter") is
  respected by the adapter: `s229_irq` only writes `nwoas_nvme_fp` fields and
  calls `nwoas_nvme_set_fast_irq`; `s229_flush`/`s229_set_cache` only touch
  `nvme_flush` and `nwoas_nvme_fp`.
- Outside callbacks, `s229_fast_irq_update` is entered from S149's process
  tail (hv_vm.c:1559), the CQ-ack path (1728) and the cancelled path (1518).
  In all three the frontend is idle (`control_busy` false), so
  `nwoas_frontend_io_pending` → `nwoas_control_pending` → `s229_irq` is a
  legal top-level entry. The frontend's S228-documented reentry of
  `nwoas_control_pending` from inside `setup`/`reset` (S228 review F2) is
  unchanged and still benign.

## CQ ack when SQ is deleted

- Delete SQ1: `nwoas_admin_execute` bumps `sq_generation` only
  (admin_state.c:64-66) → `s229_reconcile` sees `sq_changed` → disarms S149
  (`control_local(0)`) and clears SQ cursors, but keeps `cq_base/size/head/
  tail/pending/phase` (adapter.inc:92-96). Not re-armed because `sq.present`
  is false (102).
- Guest then writes CQ1 head: `s229_mmio` validates (`cq.present`,
  `value < cq_size`, `consumed <= cq_pending`) and returns false
  (adapter.inc:147-151); the S149 gate now admits it via
  `s229_cq_ack_unarmed` (hv_vm.c:1673, adapter.inc:131-132); S149 retires
  entries and calls `update_irq` → `s229_fast_irq_update` →
  `nwoas_frontend_io_pending(cq_pending != 0)` (127). test_adapter.c:78-79
  covers exactly this (pending 1 → 0, IRQ drops, no process call).
- Delete CQ1 with SQ1 present is refused with 0x10c (admin_state.c:63), and
  Delete CQ1 after SQ1 clears everything (`cq_changed`, phase reset to 1)
  and frontend clears `io_pending` (frontend.c:23). Consistent with NVMe.
- Strictness note: a bad CQ head (`consumed > pending`) is now fatal (CFS)
  where S149 alone only counted an error. This restores the Python oracle's
  behaviour (nvme-s124/controller.py:103-105 `fatal('CQ head advances beyond
  completions')`), so it is not a regression against the previous owner.

## 64-bit → 12-bit generation transitions

- `s229_reconcile` compares the full 64-bit `sq_generation`/`cq_generation`
  against its own saved copies before truncating (adapter.inc:90-91), and on
  change it explicitly disarms and clears the affected cursors itself
  (95-96). It then pre-writes `nwoas_nvme_fp.{sq,cq}_{base,size,generation}`
  (98-101) *before* calling arm, so inside S149 action 1 `same_sq`/`same_cq`
  are always true (hv_vm.c:1637-1650) and S149's own "reset cursors when
  different" logic is bypassed. That is fine precisely because the adapter
  already did the clearing on the untruncated comparison; a 4096-wrap that
  makes the 12-bit values collide cannot resurrect old cursors.
  test_adapter.c:80-83 asserts this (gen+4096 → cursors 0).
- Consequence worth knowing: `cq_phase` is no longer taken from `flags&1` on
  arm (S149's `!same_cq` branch is dead here). The adapter sets phase 1 on
  every CQ recreate (96) and on reset (73), which is what a fresh CQ needs.
- NS2 link requests still carry the 12-bit generations
  (hv_vm.c:1397-1398) and the post-link cancellation check compares 12-bit
  values plus `lifecycle_epoch` plus `sq_head` (1415-1418). Because every
  descriptor change goes through `control_local(0)` (epoch++), a link that
  spans a Delete/Create pair is cancelled by the epoch even if the 12-bit
  generation wrapped to the same value. No gap found.
- `s229_sq_generation`/`s229_cq_generation` are never reset after
  `s229_enable`; `nwoas_admin_init` starts at 0 and only increments, so the
  first Create is always seen as a change. Single enable per boot is
  enforced (`s229_enabled` check, adapter.inc:112).

## Physical synchronous failure / cancellation assumptions

- The adapter assumes there is never in-flight physical work when
  `reset_io`, `flush` or `reconcile` run. That holds because: NS1 I/O is
  synchronous inside the trap (`nvme_rw_guest`, `nvme_flush` block until
  completion, hv_vm.c:1467/1473); NS2 is synchronous through the proxy with
  `bhl` held; both run on the trapping CPU. `link_busy` is therefore only
  observable from the pin/switch corner above, and the adapter treats it as
  fatal (reset_io:67, flush:78, reconcile:89) rather than proceeding.
- Failure propagation: `nvme_flush` failure in `s229_flush` sets
  `nwoas_nvme_fp.faulted` sticky and returns false → shutdown path gets CFS,
  Set Features path gets -1 → `nwoas_frontend_fault`. `s229_reset_io` refuses
  while `faulted`, so a physical fault is terminal for the controller (the
  guest cannot recover with CC.EN=0). This matches the previous Python owner
  (S160 `_fast_sync` → `c.fatal`, re-arm refused when faulted) and the
  hv_vm.c:1521-1527 comment about late completions/tag reuse. Accept as
  design; document it in the S229 README.
- Fault visibility timing: when S149 faults inside `process`, the tail
  `update_irq` (hv_vm.c:1559) immediately reaches `nwoas_frontend_fault`, so
  the next guest CSTS read shows CFS via the frontend (S149's own
  `faulted && off==0x1c → 3` shortcut at 1697 is no longer reached because
  `s229_mmio` claims all reads first; the value is equivalent).

## Exact 16-bit phase store and barrier

- Ring publishes 14 bytes, calls `barrier`, then writes 2 bytes at
  `cq_base + cq_tail*16 + 14` (admin_ring.c:75-79). The adapter matches that
  exact address with the *pre-increment* `cq_tail` (adapter.inc:40) and
  performs a single `volatile u16` store (42) — the same idiom S149 uses for
  CQ1 (hv_vm.c:1546). Alignment: `cq_base` is 4 KiB-aligned, +14 is 2-aligned.
- `s229_barrier` is `dma_wmb()` = `dmb oshst` (utils.h:333), identical to
  S149's proven CQ1 publication. CPU-to-CPU visibility on inner-shareable
  cacheable RAM only needs `dmb ishst`; `oshst` is stronger. No cache
  maintenance is needed or performed (EL2 identity map and guest map are both
  Normal cacheable on the same PA; S149 relies on the same).
- The generic `memcpy` fallback in `s229_write` would also be used for any
  non-matching 2-byte write; that only happens for payload writes the guest
  aimed at odd places and is harmless.

## Registered guest map + stage-2 + carveout checks

- `s229_contains` walks page by page (handles 4 KiB stride across a 16 KiB
  stage-2 page, tested at test_adapter.c:67) and requires, per page:
  `nvme_guest_page_pa(page) != 0` (registered low window / high identity
  range, MCC carveout exclusion, nvme.c:588-609), stage-2 walk equality
  `nwoas_ipa_to_pa(page) == pa` (hv_vm.c:1164-1174), and
  `nwoas_in_dram(pa, 4K)` (1150-1153). Reads/writes reuse
  `nvme_guest_page_pa` after `contains` succeeded in the same call, under
  `bhl`, so no TOCTOU.
- Overflow guards: `address > UINT64_MAX - bytes` rejected; the ring's
  `range()` also rejects `a + n` overflow. `bytes == 0` rejected.
- The registered map is installed pre-boot by S160 guest_module.py:108
  (`p.nvme_guest_map(low_backing, phys_base, LOW_BACKING)` with
  NWOAS_EXCLUDE_WINDOW=1). Guest RAM is exactly [0,4G) → low window plus
  [phys_base, LOW_BACKING) identity, so any admin queue Windows/UEFI places
  in its RAM passes; this is the same predicate the Python owner used for
  ASQ/ACQ (`memory.contains`, guest_module.py:65-66) plus the stage-2
  agreement S149 already required for I/O rings. Not a new reachability
  risk. If `nvme_guest_map` were ever loaded *after* module.py, CC.EN would
  fail closed with CFS; the run script's module order (guest_module before
  module.py) is correct.
- Carveout: covered by `nvme_guest_page_pa`; test_adapter.c:70-71 shows a
  PRP into a carveout yields Invalid Field without touching memory.

## NS2 handler compatibility

- The NS2 path is untouched C: `nwoas_nvme_fastpath_execute` →
  `nwoas_nvme_link_execute` → `hv_exc_proxy(HV_NWOAS_NVME_LINK)`. The Python
  side (memory-s180/link_module.py `_fast_link_handle`) validates magic/
  version/size/nsid, then queries `p.nwoas_nvme_fastpath(4)` and requires
  `armed`, `!faulted`, `link_busy` and matching `lifecycle_epoch`. All four
  are produced by S149 exactly as before; S229 arms through action 1, so
  `armed` and the epoch are real. It never consults the Python
  `Controller`'s queue state, only `_controller.ns.io(...)` (the
  `CachedPair`), so the Python controller being an empty container is fine.
- module.py's preflight reads `_controller.ns.primary/.link` attributes
  (`block_count`, `write_first/last`, `cache_enabled`, `mdts`) — all exist
  on WindowWritableNamespace/NamespacePair. It is loaded after link_module
  in the run script, as required.
- Cache policy coherence with NS2: the C owner is now the only writer of the
  volatile-write-cache state (`s229_cache`); the Python `CachedPair.set_cache`
  is never called. NS2 is host-RAM and ignores cache anyway. No conflict.
- `query_module.py` admits only actions 3-10 and 17-22; action 2 (sync
  policy) and 0/1/16 are excluded, so the host cannot desynchronize the
  adapter's view of mask/cache/arm state at a rendezvous. Action 10 prints
  from proxy context (acceptable).

## Aggregate IRQ / mask / cache ownership

- After enable the only writer of the fast level is `s229_irq`; the S149
  base level computation is dead (early return). `s229_irq` receives a level
  already reduced by the model (`pending && !(mask&1) && !(command&0x400)`,
  model.c:9) and the frontend (`!CFS && !backend_fault && !ring.fatal`,
  frontend.c:11), where `pending = io_pending || admin pending`
  (frontend.c:13). `io_pending` is fed from `nwoas_nvme_fp.cq_pending != 0`
  gated by `cq.present && cq.ien` (frontend.c:56). Masks are applied once.
- Host level: `s229_enable` clears it (`nwoas_nvme_set_irq(0)`), and no
  Python path calls the S160 `irq()` afterwards, so `hv_bridge_nvme_irq`'s
  OR of host/fast levels reduces to the fast level.
- INTMS/INTMC (0x0c/0x10) are claimed by `s229_mmio` before S149's S160
  local-mask branch; `local_mask_allowed` is forced false (adapter.inc:58)
  so that branch is unreachable even if the ordering changed. `mask`,
  `irq_enabled`, `cache_enabled` mirrored into `nwoas_nvme_fp` are diagnostic
  only, except `cache_enabled`, which S149's write path consumes
  (hv_vm.c:1473). Its only update after arm is inside `s229_irq` (57), which
  is reached on every Set Features because the ring's reconcile callback
  ends in `update_pending → irq_update → cb.irq` unconditionally
  (frontend.c:24, model.c:9). Functional (test_adapter.c:85) but indirect;
  a one-line `nwoas_nvme_fp.cache_enabled = s229_cache;` in
  `s229_set_cache` would remove the dependency on the IRQ path.
- `s229_irq` also disarms S149 when CFS is set (60). This bypasses
  `lifecycle_epoch++`, but no link can be in flight at that point (see
  above) and any later reset goes through `control_local(0)`.

## Findings in detail

### F1. printf reachable from the tick path (medium)

Chain: hv.c:480 `nwoas_nvme_fastpath_poll` → adapter.inc:130
`nwoas_frontend_poll(&s229_front, 1)` → admin_ring.c:48 executes a backlogged
Create SQ → reconcile → `s229_control_local(1, …)` → hv_vm.c:1664
`printf("NWOAS-S149 NVMe fastpath armed …")`. hv.c:468-471 records that a
printf in `hv_tick` desynced the uart proxy and dropped the HV to its shell.
With M1N1_SPLIT_CONSOLE=1 the log goes to the USB vuart, which may make this
moot, but that has not been established for this path. Trigger requires a
Create SQ that the tail-doorbell pass could not complete (CQ0 full or >256
commands), which stornvme/NvmExpressDxe do not produce. Options: suppress
the S149 arm printf when `s229_owner_callback` is set, or accept and note.

### F2. Post-shutdown SQ1 doorbell leaks to the Python hook (low)

Sequence: CC.SHN=01 → `s229_mmio` disarms S149 (adapter.inc:153-154), model
flushes, SHST=10. Guest then writes CC with SHN=00, EN=1 (no EN 1→0). The
model accepts (no state change), CSTS keeps RDY=1, CFS=0, SHST=10. A later
SQ1 tail write: `s229_mmio` passes the `(cc>>14)&3` check (cleared), SQ1
`present` is still true, so it returns false; S149 sees `!armed` and the
IPA is not 0x100c → returns false → `hv_exc_proxy(HV_HOOK_VM)` →
`_unexpected_write` raises → guest stops. Only a non-conforming guest does
this. A cheap guard: treat `(csts & 12) == 8` like CFS in the doorbell
pre-checks (return true).

### F3. Test gap on the real S149 process path (low)

`test_adapter.c:37` replaces `nwoas_nvme_fastpath_process` with a counter,
so the extracted-function test never runs the actual process/execute tail
(`update_irq` at hv_vm.c:1559, fault at 1524 → `nwoas_frontend_fault`),
never issues a real NS1 command through `nwoas_nvme_fastpath_execute`, and
never exercises `s229_poll` arming from the tick path or the PCI window
(`pci=true`) of `s229_mmio`. The slicing also drops `nwoas_probe_at_el1`
(mocked). These are the pieces that only matter with the real S149 code, so
the "actual adapter" claim is accurate for control/MMIO but not for
process. Not a blocker for a boot; it limits what a failed boot can be
attributed to.

## Minor / cosmetic

- adapter.inc:138 `width>3` is unreachable (`width` is log2 bytes 0..3 from
  the abort decoder); harmless.
- adapter.inc:98-99 writes `sq_base/size=0` into `nwoas_nvme_fp` on Delete
  SQ; downstream users are guarded by `armed`/`present`, and `cq_size` is
  never zero while `cq.present`, so the `%` in the CQ-ack check cannot divide
  by zero.
- `s229_admin_fetches` counts every ring read (only commands are read), and
  `s229_admin_completions` counts status stores — both fine for action 20/21.
- Identify still reports model "NWOAS ANS2 READ ONLY BRIDGE" with
  `primary_readonly=false`; unchanged from the S225 payload the oracle
  accepted.
- prepare.py refuses to replace an existing worktree and pins the S217
  patch hash; build.sh refuses to overwrite a recorded `hv_sha256`. Good.

## What an isolated test boot would tell you

Given the above, the decisive signals are: action 22 (CSTS | backend_fault)
staying 0x1 with no CFS through UEFI and Windows admin bring-up; action 4
showing `armed` after Create SQ1; action 20/21 advancing; and the existing
S149 counters (3/7) behaving as in S224. A CFS during CC.EN with actions
20/21 at zero points at the `contains` predicate for ASQ/ACQ (guest map
vs stage-2); a CFS after Set Features points at `nvme_flush`; an
`_unexpected_*` RuntimeError in the host log points at F2 or a width/offset
the frontend routed to NOT_HANDLED.
