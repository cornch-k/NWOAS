# S207: source-backed dependency map, power-on to Windows desktop, and the next bounded milestone

Update after this review: S215 reproduced a guarded guest prefix from source
and passed Windows integration plus five-minute active reads. S216 combines
that prefix with source-built UEFI; its hardware evidence is separate. The
previous prefix-reproducibility blocker below is historical; the static DTB
artifact still lacks an exact upstream source revision. S209 implements a
source-only control-state foundation, not an integrated local controller.

Read-only review, 2026-09-10. No hardware, USB, UI, secret, raw ADT or binary
dump was touched. No source outside this directory was edited. The live S202
25 us soak (`nvme-s202-gap25-nohost-guest-test.sh`) was not interfered with;
its in-progress log was not opened. Only finished logs were parsed
(`logs/firmware-s200-nohost-20260910-055702.XlGKN7` and
`logs/cpufreq-s193-nohost-20260910-054545.sKqH3f`).

Files in this directory:

| File | Purpose |
|---|---|
| `README.md` | this review |
| `dependency_census.py` | pure log parser; counts observed category log lines per boot phase in a finished launcher log. No hardware, no proxy imports. Counts are an observed lower bound on paths exercised, not round-trip totals and not proof of zero traffic. |
| `test_census.py` | pure parser tests for overlapping category lines and truncated logs. No hardware, no files. |

Proven baseline being mapped (not re-measured here): native ARM64 Cinebench
2026 CPU score 1905.281 on 8 cores with 15.08 GB (about 14.05 GiB) Windows RAM
(`bench-s196/result.json`). Same-machine macOS parity is not measured.
Current execution is EL2-mediated with a host-backed service transport; this
document does not claim a standalone boot, a native driver, or that any of
the recommended work is done.

## 1. What actually executes, in order (S200/S202 launcher)

Source: `firmware-s200-nohost-guest-test.sh` and
`nvme-s202-gap25-nohost-guest-test.sh` (identical module list; only the HV
binary differs), `m1n1_windows/proxyclient/tools/run_guest.py`,
`m1n1_windows/proxyclient/m1n1/hv/__init__.py`.

| # | Step | Runs where | Source |
|---|---|---|---|
| 1 | Boot ROM, iBoot, installed bare m1n1 waits on the USB CDC proxy (NY1) | target | `NWOAS-STATUS-2026-09-06.md` bootstrap section; launcher `proxy_ready.py` |
| 2 | MacBook uploads the HV m1n1 image (2162688 B) and chainloads it | host Python over USB | launcher: `chainload.py -r "$HV"` |
| 3 | `--pre-init-script tahoe_dcp_guest_hook.py`: host boots the DCP over AFK, selects timing, rewrites `u.ba.video` geometry | host Python (RTKit/AFK client in Python) | `display-s190/tahoe_dcp_guest_hook.py:114-166`, `:278-279` |
| 4 | `hv.init()`: `p.hv_init()` (C: PSCI, vGIC, timers, stage-2 tables), then all `/arm-io` ranges mapped HW, HACR/MDCR/AMX/keys, `map_vuart`, ADT edits | C init; Python decides the map | `hv/__init__.py:1842-1955`; `m1n1_windows-s159/src/hv.c:56-150` |
| 5 | `hv.load_raw()`: 32 MB payload upload, SEPFW/TrustCache/preoslog copies, ADT memory-map rewrite, BootArgs build, secondary RVBAR writes | host Python (data moves over USB) | `hv/__init__.py:2186-2295` |
| 6 | `-m` modules in order: `capture.py` (read-only cluster regs), `guest_map.py` (`p.pcie_init()`, low-window `map_hw`, BYPASS tracers), `pmgr_gate.py` (replaces `hv.start_secondary`), `guest_module.py` (S102 `mem_size` edit, `p.nvme_init()`, `p.nvme_guest_map()`, ECAM/BAR HOOK tracers, Python NVMe controller model), `link_module.py` (NS2 RAM FAT + NS2 link handler), `file-delivery-s168/module.py`, `target-query-s163/module.py`, `observe.py` | host Python; C executes the proxy calls | `run_guest.py:621-628`; module files listed |
| 7 | `hv.start()`: `map_essential()` installs the PMGR/CPUSTART proxy hooks, `pt_update()`, ADT upload, `pre_guest_start()` = second DCP mode/swap commit and 5 s colour-bar hold, then `p.hv_start()` | host Python until the final proxy call | `hv/__init__.py:2402-2465` |
| 8 | Guest m1n1 prefix (payload offset 0, entry +0x800): `init_cpu` chicken registers (HID/EHID traps handled in C), `pmgr` reads, `cpufreq_init()`, `smp_start_secondaries()` (CPUSTART writes), `kboot_boot` into the FD | target guest EL1 under EL2 | `m1n1_windows/src/payload.c:291-335`, `smp.c:130-185`; `cpufreq-s193/REVIEW.md` §2-3 |
| 9 | UEFI (Project Mu FD at payload +0x150000): registers the emulated NVMe BAR as a non-discoverable device, boots from it, ReadyToBoot selects P12, native PMU RTC | target guest; storage served by host | `NwoasHideHighRamDxe.c:497-513`; `cpufreq-s189/NwoasHideHighRamDxe-s167.c` ReadyToBoot |
| 10 | Windows: PCI enumeration on ECAM segment 1, stornvme admin bring-up (depth 256), PSCI CPU_ON x7, vGICv3, timers, storage I/O | target guest; PCI config and admin served by host, I/O queue 1 served by C | S200 log lines 871-1153; vuart lines 170-252 |
| 11 | Windows runtime: I/O doorbells, INTMS/INTMC, IRQ 900 injection, EOI gap all in C; periodic admin commands and CC/CSTS/ASQ/ACQ reads to host; NS2 agent traffic to host | mixed | `hv_vm.c:1656-1730`, `hv_exc.c:489-506`; S200 log after line 1153 |

Every "host" row after step 7 is a synchronous `hv_exc_proxy()` round trip:
`_hv_exc_proxy` rendezvouses all CPUs (`hv_exc.c:81-127`, `hv_rendezvous()`
at line 93 with `time_stealing` default true) and blocks in `uartproxy_run()`
(`hv_exc.c:109`, `uartproxy.c:125`) until the MacBook answers. If the MacBook
is absent at that moment, all eight CPUs stay parked. That is the transport
dependency in one sentence.

## 2. Observed category-line counts from the finished S200 log

`python3 nwoas_scripts/native-s207/dependency_census.py <finished log>`.
S200 and S193 logs give identical numbers (same modules, same payload).

These are counts of matching log lines per phase, not round-trip totals. A
logged line is not one round trip: the S133 gate line is a nested log emitted
from inside a single CPUSTART write trap, and an admin `CMD q=0` line and the
controller-register (`MMIO`) lines around it can belong to the same doorbell
trap. Quiet-logging caps mean a zero is "no matching line seen", not "no
callback happened". NS2 link requests are not logged per call and are not in
the table at all. Read the numbers as an observed lower bound on the paths
that were exercised.

| category (observed lines) | pre-entry | entry..arm1 | arm1..arm2 | after arm2 |
|---|---|---|---|---|
| pmgr_read | 0 | 39 | 0 | 0 |
| pmgr_write | 0 | 2 | 0 | 0 |
| cpustart_write | 0 | 14 | 0 | 0 |
| s133_gate (nested inside a cpustart_write trap) | 0 | 7 | 0 | 0 |
| pci_cfg_read | 0 | 0 | 168 | 0 |
| pci_cfg_write | 0 | 0 | 32 | 0 |
| nvme_reg_read | 0 | 5 | 45 | 49 |
| nvme_reg_write | 0 | 14 | 17 | 1 |
| nvme_admin_cmd | 0 | 4 | 16 | 5 |
| ans_read_python (namespace probe) | 4 | 0 | 0 | 0 |
| ans_flush_python | 0 | 0 | 0 | 1 |

The columns are not summed: a per-phase total would overstate traffic, since
gate lines share a trap with their CPUSTART write and NS2 traffic is absent
from the counts entirely.

Phase markers (approximate): "arm1" is the first `[S149] target NVMe fastpath
armed` (UEFI NvmExpressDxe, admin depth 2, log line 870); "arm2" is the second
(Windows stornvme, depth 256, line 1153). The split is decided by the order
markers appear in the host log, which can differ slightly from the guest's own
ordering. On the observed logs UEFI registers the NVMe BAR through
`RegisterNonDiscoverableMmioDevice` and does not touch ECAM, so the PCI config
lines fall to Windows. The two Python tracebacks after arm2 are the serial
device disappearing at the end-of-run reboot, not a guest fault.

What the counts show (observed on these logs, not a completeness proof):

- No PMGR, CPUSTART or CPU-state category line appears once UEFI runs. The
  PMGR/CPUSTART lines all fall in the guest m1n1 prefix window (step 8). This
  is consistent with those paths being prefix-only; it is not a proof that no
  other callback ever fired, since not every event is logged.
- No `Guest exception`/shell line appears in the run. Python's `handle_sync`
  fallback was not observed to be exercised on this path.
- The Windows runtime still calls the host: after arm2 the log shows admin
  Get Log Page 0xc1, Get Features 0x0c/0x7f, Identify CNS 3, and CSTS/CC/
  ASQ/ACQ reads around each admin command, then the CC shutdown write and
  flush. These are not boot-only.
- NS2 link requests (`HV_NWOAS_NVME_LINK`, `hv_vm.c:1385-1425`) are not
  logged per call and are additional to the table. They exist only for the
  Windows-side NWOAS agent, not for reaching the desktop.

## 3. Dependency map: who owns what today

Legend: HOST-SETUP = MacBook Python before `hv.start()`; HOST-RUNTIME =
MacBook Python after guest entry via `hv_exc_proxy`; C = HV m1n1 C on the
target; GUEST = guest m1n1 prefix / UEFI / Windows on the target.

| Operation | Owner | Evidence | Standalone status |
|---|---|---|---|
| Loading the HV image | HOST-SETUP (USB upload) | `chainload.py -r` in launcher | needs a target-local loader (out of scope here) |
| Loading the 32 MB guest payload, SEPFW/TrustCache copy, BootArgs, ADT rewrite | HOST-SETUP | `hv/__init__.py:2186-2295` | needs porting to C or a chainload stage |
| Guest RAM stage-2 map, low 4 GiB window backing, S102 `mem_size` shrink | HOST-SETUP (page tables built in C by proxy calls) | `usb-s174/guest_map.py:11-16`, `guest_module.py:41-55` | policy is 6 constants; mechanically portable |
| Tahoe DCP boot, timing choice, two mode/swap commits, HDMI settle | HOST-SETUP (AFK client in Python; HV m1n1 skips `display_init()` on this firmware) | `tahoe_dcp_guest_hook.py:114-166,168-275`; `m1n1_windows-s159/src/main.c:167-172` (`NWOAS-DCP-DEFER`) | largest setup-time port; not runtime |
| ANS2 bring-up, guest DMA window, PCIe init | C, triggered by HOST-SETUP | `guest_module.py:106-108`, `usb-s174/guest_map.py:7`; `nvme.c:299,578`, `proxy.c:634-637` | one-line trigger; trivially local |
| UART0 guest console | C (`hv_map_vuart`, non-blocking `iodev_can_write`) | `hv_vuart.c:116-198`, `hv/__init__.py:1957-1964` | local already |
| PMGR PS-register hooks for UART0/ATC0_USB/ATC0_USB_AON (+parents), pg_overrides | HOST-RUNTIME (41 round trips, all in step 8) | `hv/__init__.py:1966-2026` | portable to C with exact semantics (§4); does not remove transport |
| CPU_START writes (0x23b754000 +4/+8/+c) and S133 gate | HOST-RUNTIME (14 round trips, all in step 8) | `hv/__init__.py:2056-2087`, `smp-s133/pmgr_gate.py:19-31` | portable to C with exact semantics (§4); does not remove transport |
| PSCI CPU_ON for CPUs 1-7 | C (`hv_psci_turn_on_cpu` -> `hv_start_secondary`) | `hv_psci.c:1210-1252`, `hv.c:294-330`; vuart lines 170-252 | local already |
| vGICv3, timer PPIs, SMC/HVC traps, WFI trap, EL2 chicken traps | C | `hv.c:56-150`, `hv_exc.c` | local already |
| PCI config space of the virtual NVMe device (ECAM 0x700000000, segment 1) | HOST-RUNTIME (200 round trips at Windows enumeration) | `guest_module.py:272`; `nvme-s124/controller.py:54-78`; `MCFG.aslc:47` | **not local; boot-critical** |
| NVMe controller registers CAP/VS/INTMS/INTMC/CC/CSTS/AQA/ASQ/ACQ and admin doorbells | HOST-RUNTIME, except INTMS/INTMC once the fast path is armed and no Python-owned CQ is pending | `controller.py:80-141`; `hv_vm.c:1656-1683` (S160 local mask) | **not local; boot-critical and periodic at runtime** |
| Admin commands: Identify (CNS 0/1/2), Create/Delete SQ/CQ, Get/Set Features (1,2,4,5,6,7,8,9,10,11), Get Log Page (1,2,3), AER, Abort | HOST-RUNTIME | `controller.py:145-196`, `readonly_namespace.py:44-76`, `nvme-s139/controller.py:105-136` | **not local; boot-critical and periodic at runtime** |
| I/O queue 1: SQ tail / CQ head doorbells, command fetch, PRP validation, ANS2 read/write/flush, write-window guard, CQE publish, phase | C after arm (`nwoas_nvme_fastpath_control(1)`) | `hv_vm.c:1475-1560,1570-1655,1684-1730`; `nvme.c:559-700` | local already, but arming and disarming are host actions (`guest_module.py:215-226,258-266`) |
| I/O queue 1 before the fast path is armed, or after `_fast_disable()` | HOST-RUNTIME (Python `process()` + `backend_direct`) | `controller.py:198-225`, `guest_module.py:113-131` | would vanish with a local admin path |
| NVMe IRQ 900 level, dedup, EOI gap (25/50 us candidates) | C | `hv_exc.c:447-506` | local already; host-level input only for admin CQ (`P_HV_NWOAS_NVME_IRQ`, `proxy.c:501`) |
| GPT-block validated writes (LBA 0-5, 61279339-61279343) | HOST-RUNTIME | `guest_module.py` `GptGuard`; C refuses these writes (`nvme_write_allowed`) | keep host-only (safety) |
| NS2 host RAM FAT (tools delivery, agent mailbox, Cinebench artifact) | HOST-RUNTIME | `memory-s180/link_module.py:213-249`, `transport-s123/transport.py` | not needed for desktop; stays optional |
| P12 selection, RTC | GUEST (UEFI ReadyToBoot; PMU RTC library) | `cpufreq-s193/REVIEW.md`, `firmware-s200/result.json` (`host_cpu_init: false`) | local already |
| FL1100 USB-A xHCI (keyboard/trackpad), INTx 698 bridge, DART/coherence helpers | C (BYPASS/SPTE_MAP paths) | `usb-s174/guest_map.py:8-9,19`; `hv_exc.c:675-830`; `hv_vm.c:3529-3660` | local already |
| Watchdog | none (`wdt_cpu` None) | `hv/__init__.py:2455` | n/a |

Read of the table (scope: the observed S200/S202 launcher configuration and
its eight-module list; a different module set could add other host paths):
after guest entry, the HOST-RUNTIME items on the path to the desktop that
appear on these logs are (a) the PMGR/CPUSTART hooks during the guest m1n1
prefix, and (b) the PCI-config + controller-register + admin-command model
of the virtual NVMe device. (a) is 62 observed category lines confined to
about one second of boot. (b) is 336 observed boot-time lines plus an
open-ended stream at runtime; it is also a path that parks all CPUs if the
cable drops. These are observed line counts, not proof that no other callback
exists, and NS2 traffic (below) is not counted at all.

## 4. `map_essential` PMGR hooks and the S133 gate: exact semantics and a local equivalent

### 4.1 What the Python hooks do (facts)

`hv/__init__.py:1966-2087`:

- `hook_devs = ["UART0", "ATC0_USB", "ATC0_USB_AON"]` plus each device's
  PMGR parents. For each, the PS register address is computed from the ADT
  (`pmgr.ps_regs`, `psidx*8`). On T8103 the S200 log shows exactly these
  hooked addresses: `0x23b7001c0`, `0x23b7001c8`, `0x23b700220`,
  `0x23b700270`, `0x23b700420`, `0x23d280088`, `0x23d280098`.
- Write hook `wh` (lines 1970-1973): `mask32(addr, 0x3ff, (data | 0xf) & ~0x80000400)`.
  Exact semantics, which any future C port must match bit-for-bit:
  `mask32(addr, clear, set)` is `(old & ~clear) | set` (`utils.h:133-144`,
  `bic` then `orr`), not "only the low 10 bits are written". With
  `clear = 0x3ff` only bits 0-9 of the hardware word are replaced. The set
  operand `(data | 0xf) & ~0x80000400` clears bit 31 (RESET, 0x80000000) and
  bit 10 (DEV_DISABLE, 0x400) *within the set value*, but because those bits
  are not in the clear mask, the store leaves the pre-existing hardware bit 31
  and bit 10 unchanged. Bits 11-30 of the result are `old | data` bits (the
  set value contributes data's bits 11-30 via `orr`). So the effect is: bits
  0-3 forced to 1, bits 4-9 set to the guest's value, bit 10 and bit 31 kept
  as hardware had them, bits 11-30 OR-ed in from the guest write (`pmgr.c:9-17`
  bit names). This does NOT clear a pre-existing RESET or DEV_DISABLE bit. It
  then stores a virtual value `(data & 0xfffffc0f) | ((data & 0xf) << 4)`
  so that a later read shows ACTUAL == TARGET as the guest asked.
- Read hook `rh` (1975-1979): returns the stored virtual value if one exists,
  else the hardware value, and caches that hardware value
  (`setdefault`). So the first read pins the guest-visible value forever.
- `pg_overrides` (2019-2026): reads of `0x23d29c05c` and `0x23d29c044`
  return the constant `0xc000000`.
- `cpu_hack` list is empty (2028-2032): cluster P-state MMIO is not hooked;
  the guest's `cpufreq_init()` writes go to hardware (confirmed in
  `cpufreq-s193/REVIEW.md` §4).
- CPUSTART (2056-2087): `map_hook(pmgr0 + 0x54000, 0x20, write=cpustart_wh)`.
  Writes at offsets < 8 are dropped (only logged). Writes at +8/+c decode
  cluster/core bits and call `self.start_secondary(die, cluster, i)`, then
  install a read hook `cpu_state_rh` on the ACC state register of that
  core. Nothing is forwarded to the physical CPU_START register in either
  case.
- S133 `pmgr_gate.py:19-31` replaces `hv.start_secondary` with a logger
  (first 16 events). Net effect with the gate: all 14 CPUSTART writes are
  dropped, seven `HVLOG: NWOAS-S133 gate` lines are produced, and the APs
  stay in the HV m1n1 spin table until Windows issues PSCI CPU_ON.

### 4.2 Who issues those writes (correction of an attribution)

`pmgr_gate.py`'s docstring says "Windows' Apple AIC HAL writes the Apple
PMGR CPU_START registers". The evidence says otherwise:

- In the S200 log the 14 CPUSTART writes (lines 803-834) come immediately
  after `Jumping to entrypoint` (752), interleaved with the guest m1n1
  `init_cpu` HID/EHID traps and PMGR reads, and before any UEFI or Windows
  storage activity (UEFI's first NVMe register access is line 844).
- The write pattern `+4 = 1<<(4*cluster+core)` then `+8+4*cluster = 1<<core`
  is byte-for-byte `smp_start_cpu()` in `m1n1_windows/src/smp.c:168-171`,
  called from `smp_start_secondaries()` which `payload_run()` calls right
  after `cpufreq_init()` (`payload.c:317-318`).
- Windows' CPU starts are the seven `PSCI DEBUG: turning on CPU` lines in
  the vuart capture (lines 170-248), after the UEFI ReadyToBoot line (20).
- `windows_drivers/` contains design documents and uncompiled skeletons only
  (`windows_drivers/README.md` honesty notice: no driver was ever compiled
  or loaded from this source directory). The absence of driver binaries in
  the source tree does not by itself prove that no Apple AIC HAL extension is
  installed in the live Windows image. We did not inventory the loaded Windows
  drivers in this run.

What the evidence does support: the source and the logged AP-start sequence
strongly identify the 14 CPUSTART writes as the guest m1n1 prefix's
`smp_start_cpu()` (byte-for-byte pattern match, timing before any UEFI/Windows
storage) and Windows' seven CPU starts as PSCI CPU_ON handled in C. The claim
that no AIC HAL driver is *loaded* in live Windows is unverified; it is not
needed for the finding, since the logged CPUSTART writes precede Windows.

So on this evidence the S133 gate protects against the guest m1n1 prefix's
CPUSTART, not against a Windows AIC HAL write. This does not change the gate's
value (the double-start deadlock it fixed is real, S131/S132), only its
documented cause. Hypothesis, not verified: the S131/S132 deadlock was
Python's `start_secondary` entering the AP at the guest m1n1 RVBAR from the
prefix's CPUSTART, followed by Windows' PSCI CPU_ON of an already-running
core.

Side effect worth knowing: with the writes dropped, `smp_start_cpu()` polls
its spin flag for 100 x 1 ms and prints `Failed!` (`smp.c:173-183`) for each
of seven cores, about 0.7 s of guest boot time. That output goes to the
guest UART0, which in split-console mode is not forwarded to NY3
(`hv_vuart.c:139-141` gates on `USAGE_CONSOLE`), so it has never appeared in
a launcher log. Also, the `add_tracer(irange(addr, 8), "CPU STATE HACK")`
at line 2067 uses the stale loop variable `addr` (last pg_overrides
address), which is why the log shows `PT[23d29c048:23d29c04c] -> RESERVED
CPU STATE HACK` instead of the ACC state register. Harmless today: no
`CPU STATE R` event was ever logged.

### 4.3 Can a target-local equivalent preserve exact semantics without changing CPU start ordering?

Yes, mechanically. The two hooks are pure functions of the access plus a
tiny per-address state:

- PMGR PS hooks: a C `hv_hook_t` installed with `hv_map_hook()`
  (`hv_vm.c:509`, type `SPTE_HOOK`, dispatched at `hv_vm.c:3796-3803` without
  any proxy call) holding a small table `{addr, has_virtual, virtual_value}`
  for at most 16 addresses. Write: `mask32` with the same constants, store
  `(data & 0xfffffc0f) | ((data & 0xf) << 4)`. Read: return stored value if
  present, else read hardware, store it, return it. `pg_overrides`: constant
  reads. The address list must still come from the ADT walk
  (`pmgr_dev_get_parents`), which exists in C (`pmgr.c`), or be passed by
  Python at setup as a list, which keeps the policy identical.
- CPUSTART: a C hook on `pmgr0 + 0x54000` size 0x20 that drops every write
  and prints the S133 line for offsets >= 8, first 16 occurrences. No
  `hv_start_secondary` call. This is exactly the S133-gated behaviour.
  The PSCI path is untouched, so CPU start ordering (CPU0 first, then
  Windows-requested CPU_ON 1..7 through `hv_psci_turn_on_cpu`) is unchanged.
  Omitting the `cpu_state_rh` read hook is a reduced-scope option, not an
  exact-equivalent guarantee. No `CPU STATE R` line was observed in the logs,
  which is consistent with the hook never being hit while the gate is active,
  but absence of a logged read does not prove the guest never reads that ACC
  state register. If exact semantics must be preserved, port the read hook
  too; drop it only as a deliberate scope reduction, and treat it as a
  hypothesis to confirm on hardware, not as proven dead code.

Plain statement, as requested: implementing these hooks locally removes the
62 observed prefix-phase proxy round trips that all occur inside roughly one
second of the guest m1n1 prefix, and removes nothing else on the observed
path. It does not remove the transport
dependency, because 336 boot-time and an unbounded number of runtime
round trips remain on the NVMe PCI-config/register/admin path (§2, §3).
It also cannot be tested for regression from launcher logs alone, since the
gate lines would disappear from the host log and `summarize_log.py`'s
`gated == 7` check would have to move to the vuart. It is a reasonable
cleanup to bundle later, not the next milestone.

## 5. Current native C IRQ/storage path: what is and is not local

Local in C today (`m1n1_windows-s159/src`, the tree the S163/S194 HV
binaries were built from; `git status` shows `hv_exc.c`, `hv_vm.c`,
`nwoas_stage8.inc` modified relative to `bddf7f06`):

- `nwoas_nvme_fastpath_mmio()` (`hv_vm.c:1656-1730`): SQ1 tail doorbell
  (`+0x1008`) executes one command per doorbell under `bhl`, CQ1 head
  (`+0x100c`) retires entries and recomputes the level without I/O
  (S139 semantics), INTMS/INTMC (`+0x0c/+0x10`) handled locally when
  `local_mask_allowed` (S160), faulted CSTS read returns RDY|CFS.
- `nwoas_nvme_fastpath_process()` / `_execute()` (`1427-1560`): opcode
  gate (read/write/flush only), NSID 1 only (NSID 2 goes to the host link),
  LBA range, write window `53839104..59968629`, `nvme_rw_guest()` with PRP
  validation and carveout checks, flush on FUA or cache-off, CQE with phase.
- Deferred processing one command per tick after a CQ ack
  (`nwoas_nvme_fastpath_poll`, `1562-1566`).
- IRQ: `hv_bridge_nvme_irq()` (`hv_exc.c:489-506`) injects SPI 900 on the
  pinned/boot CPU when either the host level or the fast level is set,
  deduplicated by `hv_vgic_virq_outstanding`, with the S163/S194 EOI gap.

Not local (host Python), on the same device:

- PCI configuration space (`controller.py:54-78`): vendor/device
  `0x1234:0x0010`, class `01/08/02`, BAR0 = `0x700100000` with size-probe
  semantics, command register bits `0x407`, INTx status bit from CQ
  pending, no MSI capability.
- Controller registers (`controller.py:80-141`): CAP `MQES 255, CQR, TO 20,
  CSS NVM, MPSMIN/MAX 4 KiB`, VS 1.3, CC enable/disable and shutdown
  (with `ns.flush()`), AQA/ASQ/ACQ latch when disabled, admin doorbells.
- Admin command set (`controller.py:145-196`, `readonly_namespace.py:44-76`):
  the identify blobs Windows has already accepted (model
  `NWOAS ANS2 READ ONLY BRIDGE`, serial `NWOAS-RO-00000000001`, one
  namespace, 4096-byte LBA, MDTS from `NWOAS_MAX_TRANSFER`), Create/Delete
  queues with `MAX_Q = 1` (S139), Get/Set Features including the volatile
  write cache feature 6 that toggles `set_cache`, Get Log Page 1/2/3, AER
  parking, Abort.
- Fast path arming/disarming policy (`guest_module.py:215-226,258-266`) and
  the S160 mask pull before every Python fallback (`_fast_pull_mask`).
- NS2 link execution and the S168/S163/S189 handlers layered on it.

## 6. Recommended next implementation: S207 target-local NVMe controller front end (NS1)

### 6.1 Scope

Move the PCI-config, controller-register and admin-command model for the
virtual NVMe device (namespace 1 only) into the HV C, beside the existing
fast path, so that after `hv.start()` no storage access on the desktop path
requires a proxy round trip. Python keeps: ANS2 init and guest DMA window
setup (already one-line proxy calls), the GPT-block guard (writes to LBA
0-5 and the backup table stay refused in C, exactly as now), NS2 (an admin
Identify with `NN = 1` when the host link is absent, `NN = 2` when the host
registered it), and every diagnostic module unchanged.

This is the smallest change that removes a real runtime-host dependency on
the storage path: after it, the admin-command and CSTS/CC/ASQ/ACQ reads that
today rendezvous all CPUs would complete locally, and the observed 336
boot-time storage lines would no longer be host round trips.

It does NOT by itself make the guest survive a USB cable drop. As long as the
host-owned NS2 path and its worker are active (NS2 RAM FAT and link handler,
`link_module.py`; the NWOAS agent polling, S177), a cable drop still parks the
CPUs on the next NS2 rendezvous even with a local NS1 admin front end. The
vuart and any future `Guest exception` also still reach the host. A future
cable-drop-resilience test is only meaningful after explicitly disabling the
host-owned NS2/worker and any other host callbacks first; do not state
unconditional cable-drop resilience for this change. It is also not standalone
boot; §3 lists what still runs on the MacBook before `hv.start()`.

### 6.2 Exact entry points

C (`m1n1_windows-s159/src`, or a fresh worktree from the same base):

- `hv_vm.c:1656` `nwoas_nvme_fastpath_mmio()`: extend the `off` switch to
  serve `0x00..0x3f` reads and `0x0c/0x10/0x14/0x24/0x28..0x34` writes and
  the admin doorbells `0x1000/0x1004` when a new `nwoas_nvme_fp.local_ctrl`
  flag is set. Keep the current early return for `width != 2` except for
  the 64-bit CAP/ASQ/ACQ accesses that `controller.py:80-89` already
  accepts (`width in (32, 64)`).
- New `nwoas_nvme_admin_execute()` next to `nwoas_nvme_fastpath_execute()`
  (`hv_vm.c:1427`): port `controller.py:145-196` and
  `readonly_namespace.py:44-76` opcode by opcode, producing byte-identical
  identify data (the bytes Windows has already enumerated; any change would
  be a new device to Windows).
- New `nwoas_nvme_pci_cfg()` hooked with `hv_map_hook()` (`hv_vm.c:509`) on
  `0x700000000` size `0x1000`; port `controller.py:54-78`. Config-space
  reads outside 4 KiB return all-ones as today.
- Arming: call the existing `nwoas_nvme_fastpath_control(1, ...)` logic
  from the C Create SQ 1 handler with the same `flags` bit layout
  (`hv_vm.c:1622-1652`), and the existing disable path on CC.EN clear or
  shutdown, mirroring `guest_module.py:258-266`.
- `proxy.c:506` `P_HV_NWOAS_NVME_FASTPATH`: add action 11 "enable local
  controller front end" taking the same policy words Python computes today
  (`irq_enabled`, `cache_enabled`, MDTS, NS count). Python calls it once in
  `guest_module.py` right after `hv.add_tracer(...)` (line 272-273), and in
  that mode installs the ECAM/BAR tracers as `TraceMode.BYPASS` instead of
  `HOOK` so the C hook owns the range. A launcher env gate
  `NWOAS_NVME_LOCAL_CTRL=1` keeps every existing launcher unchanged.
- Host-level IRQ: with the admin CQ local, `P_HV_NWOAS_NVME_IRQ` becomes
  unused in this mode; `hv_bridge_nvme_irq()` needs no change because it
  already ORs the fast level (`hv_exc.c:492`).

Python (new isolated module, not an edit of `nvme-s160/guest_module.py`):
copy `guest_module.py` to `native-s207/guest_module.py`, keep ANS2 init,
`nvme_guest_map`, GPT guard construction and the `Controller` object (as a
shadow for differential checking, see tests), and route the ECAM/BAR
tracers per the env gate.

### 6.3 Semantics that must be preserved exactly

1. Identify CNS 0/1/2 bytes, Get Features results (`features={6:1}`
   default, feature 7 returning `MAX_Q-1`), Get Log Page 1/2/3 bytes.
2. `MAX_Q = 1`, `MAX_DEPTH = 256`, one CQ slot unused, CQ-head ack never
   starts I/O (S139).
3. CC enable validation (`IOSQES 6`, `IOCQES 4`, `MPS 0`, `CSS 0`), CFS on
   violation, shutdown flush with CSTS.SHST 1 then 2.
4. INTx level = any interrupt-enabled CQ pending AND mask bit 0 clear AND
   PCI command bit 10 clear (`controller.py:50-52`); the S160 local-mask
   eligibility rule collapses to "always" once no Python-owned CQ exists.
5. Write window and GPT refusal unchanged (`nvme_write_allowed`).
6. Fast path arm/disarm points unchanged relative to Create SQ 1 / CC
   changes, so the S163/S194 EOI-gap comparisons remain valid.

### 6.4 Tests

Offline, no hardware:

- C harness in the style of `nvme-s160/test_local_mask.py` (which already
  extracts real functions from `hv_vm-s160.c` with `extract_c()` and
  compiles them with `/usr/bin/clang`): compile the new admin, register and
  PCI-config handlers with fake `nwoas_nvme_guest_ptr`/`nvme_*` and drive
  them from Python.
- Differential replay: parse the `[S130] PCI R/W`, `MMIO R/W` and `CMD q=0`
  lines of the finished S200 log (871-1153 and after) into an access
  sequence, feed it to both the Python `Controller` (S139 subclass) and the
  compiled C handlers, and require identical register read values, CQE
  status/result words and identify payload bytes. The census helper's
  regexes are a starting point.
- Existing `nvme-s124/test_controller.py`, `nvme-s160/test_local_mask.py`
  and the S149/S150 source checks must still pass against the candidate
  file.

Hardware acceptance (after review and offline qualification, and after the
S202 soak has finished; never during a timed comparison). Autonomous Mini
experiments are already authorized, so this stage carries no new permission
gate and asks for no fresh approval; it simply follows the review/qualify
order:

- New launcher cloned from `nvme-s202-gap25-nohost-guest-test.sh` with the
  S207 module, `NWOAS_NVME_LOCAL_CTRL=1`, hash-pinned HV and payload.
- PASS requires: Windows desktop reached; `dependency_census.py` on the
  finished log shows no observed `pci_cfg_*`, `nvme_reg_*` or `nvme_admin_cmd`
  lines after guest entry (an observed-line check, read as evidence not as a
  completeness proof); same 8-core/15.08 GB inventory; the existing
  checksum-correct CPU+DISK samples; one uninterrupted 30-minute soak with
  no 0x133; and a graceful reboot completing the CC shutdown flush locally.
  Bandwidth is not the metric of this milestone; a 64 MiB read timing is
  recorded for regression only.
- Do not combine with any EOI-gap, memory-layout, USB-C or display change.

## 7. Facts versus hypotheses

Facts (read from source or finished logs this session; scope is the observed
S200/S202 configuration):

- In the observed S200/S202 configuration the launchers load eight Python
  modules. The modules seen installing runtime proxy hooks on the desktop path
  are `map_essential` (PMGR/CPUSTART), `guest_module.py` (ECAM/BAR HOOK
  tracers), and `link_module.py` (NS2 link handler). This is the observed set
  for this module list, not a universal claim that only these paths can exist.
- The observed PMGR/CPUSTART lines all appear before UEFI's first NVMe access
  and match the guest m1n1 `smp_start_cpu()` write pattern.
- Windows starts CPUs 1-7 through PSCI, handled in C (per the vuart capture).
- On the observed logs UEFI uses the emulated NVMe through a non-discoverable
  MMIO device and does not access ECAM, so the PCI config lines fall to
  Windows.
- Windows issues admin commands and CSTS/CC/ASQ/ACQ reads after boot; on this
  path each is a host round trip that rendezvous all CPUs.
- I/O queue 1 read/write/flush, mask registers, IRQ injection and the EOI
  gap are in C.
- The HV C tree used for the live binaries has uncommitted changes in
  `hv_exc.c`, `hv_vm.c`, `nwoas_stage8.inc`; the tracked main tree is S158.

Hypotheses (not verified here):

- That the S131/S132 double start was Python `start_secondary` on the
  guest m1n1 prefix's CPUSTART followed by Windows PSCI CPU_ON (consistent
  with all evidence, but no S131 log was re-read).
- That the local front end changes no Windows-visible timing other than
  removing round trips. Admin commands would complete synchronously in the
  doorbell trap instead of after a USB round trip; stornvme tolerates both,
  but only hardware shows whether the 0x133 watchdog behaviour shifts.
- Cable-drop resilience is NOT claimed for the S207 change alone. While the
  host-owned NS2 path and its worker (link handler and S177 agent polling),
  the vuart, and any future `Guest exception` still reach the host, a cable
  drop still parks the CPUs on the next such rendezvous. Only the storage
  admin/register path becomes host-free. A cable-drop test would first require
  explicitly disabling the host-owned NS2/worker and other callbacks; until
  that is done and measured, resilience across a cable drop is unverified.

## 8. Not done, not claimed

- No code in `m1n1_windows*`, `apple_silicon_platforms_mu`, or any launcher
  was changed. The S207 front end is a recommendation with entry points and
  tests, not an implementation.
- No standalone boot, no native Windows driver, no macOS parity measurement.
- The guest m1n1 prefix binary remains unreproducible from any commit
  (`cpufreq-s193/REVIEW.md` §7); nothing here changes that.
