# S193 CPU-init lineage review (read-only)

Scope: whether the host-side `cpufreq-s140/init.py` call in the S189 launcher is
still needed now that UEFI ReadyToBoot selects P12 natively, and what the
1376256-byte prefix of the guest payload actually is and does. No code, build,
or hardware was touched. The running S191 Cinebench session was not disturbed
(no CDC pipe opened; only files under `nwoas_scripts/`, `m1n1_windows/`,
`apple_silicon_platforms_mu/` and top-level reports were read).

## 1. Verdict

1. **Removing the S140 host init is legitimate to test, and source analysis
   says it is redundant.** The guest payload is *not* a bare UEFI image. It is a
   guest m1n1 followed by a Linux-style DTB followed by a UEFI FD that
   deliberately carries a Linux ARM64 `Image` header. The guest m1n1 therefore
   takes exactly the `kernel && fdt` branch of `payload_run()` and calls
   `cpufreq_init()` itself before starting secondaries and jumping into UEFI.
   The S140 README premise ("the UEFI chainload path skips the call made by the
   normal Linux payload path") is contradicted by the bytes.
2. **It is not yet hardware-proven.** Every P12 run to date has had S140 in the
   launcher. The guest m1n1 console is not captured by these launchers and the
   guest's cluster-register MMIO is untraced, so the guest-side `cpufreq_init()`
   has only indirect hardware evidence (section 5). One controlled boot without
   S140 (section 8) settles it, and the failure mode of that boot is safe.
3. **The ReadyToBoot P12 helper depends on a prior full cluster init.** It is a
   P-cluster-only read-modify-write of the DESIRED fields, it refuses to write
   unless the baseline is already P7, and it never touches APSC, throttle
   controls, PLL relock, or the E cluster. Today that prior init is done twice:
   by S140 on the host, then again by the guest m1n1. Without S140 it must come
   from the guest m1n1 alone.

## 2. What the launcher actually loads (verified from bytes)

Payload: `cpufreq-s189/m1n1-payload-s189-native-p12.bin` (32342016 B, sha256
`3b66d5d8...6836b2`). `firmware-s192/m1n1-payload-s192-native-rtc-p12.bin` has
a byte-identical first 1376256 bytes.

| Offset | Size | Content | How verified |
|---|---|---|---|
| 0x000000 | 0x140000 (1310720) | Guest m1n1 raw image, entry at +0x800 | Version tag at 0x58d7c: `##m1n1_ver##v1.0.2-1471-g59fb544-dirty`; string table contains `payload_run`, `Found a kernel at %p`, `cpufreq: Initializing clusters`, `Preparing to boot kernel at %p with fdt at %p`; last non-zero byte at 0x14f5f2 |
| 0x140000 | 0x10000 (65536) | FDT, compatible `apple,j274` | Magic `d00dfeed`, totalsize 65536; byte-identical to `m1n1_windows/apple-j274-padded.dtb` and `apple-j274-pad.dtb` (sha256 `ecc93b24...190f76`) |
| 0x150000 | 30965760 | `J274MACMINI2020_EFI.fd` | First 0x8000 bytes are the DATA block from `Platform/MacMini2020Pkg/MacMini2020.fdf` lines 56-72: `adr x1,.` / `b 0x8000`, text_offset 0x80000, image_size 0x100e0000, magic `ARM\x64` at +0x38; `_FVH` at +0x8028 |

The FDF comment states the intent explicitly: "Implement the Linux kernel header
layout so that the loader (m1n1) will identify it as something bootable, and
execute it with a FDT pointer in x0". So the "Linux kernel+FDT path" is not an
assumption here; it is the designed mechanism by which this UEFI image boots.

Lineage of the prefix (all byte-identical first 1376256 B, checked with `cmp`):
`m1n1_windows/m1n1-payload.bin` (Jul 7) = s93 = s99 = s102 = s129 = s131 =
s189 = s192. Every builder since S125 (`uefi-s125/build_candidate.py`,
`uefi-s127/128/129`, `smp-s131/build_candidate.py`, and the `s131_builder.py`
copies under `cpufreq-s170`, `cpufreq-s189`, `firmware-s192`) does
`old[:1376256] + fd`, taking `old` from the previous payload. The original
recipe is recorded in the July reports (`NWOAS-진행보고-2026-07-06.md` line 194,
`NWOAS-현황보고-2026-07-09-v14-handoff.md` line 45):
`cat build/m1n1.bin apple-j274-padded.dtb <UEFI.fd> > m1n1-payload.bin`.

## 3. Guest boot path under the hypervisor (verified from source)

`run_guest.py -r` calls `hv.load_raw(image, entry=0x800)`. `load_raw` writes
the whole 32 MB blob at `guest_base`, appends SEPFW / preoslog / boot-args,
sets secondary RVBARs to `entry & ~0xfff`, and `hv.start()` jumps to
`guest_base + 0x800`, which is the guest m1n1's `_start`. Nothing in
`load_raw`, `hv.start()`, or the HV C code parses or re-arranges the payload.
There is no separate "UEFI loader" in the host path; the host just runs a
second m1n1 as the guest.

Inside the guest m1n1 (`src/main.c` -> `run_actions()` -> `payload_run()`):

1. `_payload_start` = end of the m1n1 image = 0x140000. `load_one_payload`
   sees `d00dfeed`, `load_fdt` accepts it because `fdt_node_check_compatible`
   matches `apple,j274` (from ADT `target-type`), advances by totalsize
   (0x10000).
2. Next pointer = 0x150000. Bytes 0..1 are not gzip/xz/fdt/cpio/sig/initramfs/
   logo; `p + 0x38` is `ARM\x64`, so `load_kernel` runs. Note the check order
   in `load_one_payload`: the kernel-magic test precedes `check_var`, so no
   variable parsing happens. Size 0 => `load_kernel` returns NULL, loop ends.
3. `kernel && fdt` is true and `chainload_spec` is NULL, so `payload_run`
   executes, in this order:
   `cpufreq_init()` -> `smp_start_secondaries()` -> `mitigations_perform()` ->
   `kboot_prepare_dt(fdt)` -> `kboot_boot(kernel)`.
4. `kboot_boot` sets `next_stage.entry = kernel` (the FD base, i.e. the
   `adr x1,.` / `b 0x8000` stub), `x0 = fdt`, `x4 = &cur_boot_args`.
   `m1n1_main` then does `nvme_shutdown`, `exception_shutdown`,
   `usb_iodev_shutdown`, `display_shutdown`, `fb_shutdown`, `mmu_shutdown`
   and vectors into the FD.

There is no other call site. Grep of `m1n1_windows/src` for `cpufreq` gives
exactly three: `payload.c:317` (`cpufreq_init()` in the branch above),
`proxy.c:670` (`P_CPUFREQ_INIT`, the RPC S140 uses), and `main.c:186`
(`cpufreq_fixup()`, which only writes when `os_firmware.version <= V13_3`, so it
is a no-op on the Tahoe firmware under test).

## 4. Every cpufreq initialisation call in the guest path (host and guest)

| # | Where | Runs on | Mechanism | Present today | Effect on hardware |
|---|---|---|---|---|---|
| 1 | Host m1n1 HV image (`m1n1-s163-eoi-gap50.bin`, v1.0.2-1474) `main.c` `cpufreq_fixup()` | Host EL2, at HV m1n1 boot | Source | Yes | None on Tahoe (version gate) |
| 2 | `cpufreq-s140/init.py` -> `p.cpufreq_init()` -> `proxy.c` -> `cpufreq_init()` | Host EL2, before `hv.start()` | RPC | Yes (`-m` in `cpufreq-s189-native-guest-test.sh`) | Full init of E and P clusters (table below); logged before/after; fails closed |
| 3 | Guest m1n1 prefix `payload.c` `cpufreq_init()` | Guest EL1/EL2-emulated, after HV entry, before UEFI | Source of the prefix's commit; bytes carry the strings | Yes, unconditional given the payload layout | Same full init, writes pass straight to hardware (see below) |
| 4 | UEFI `NwoasHideHighRamDxe` ReadyToBoot -> `nwoas_cpu_pstate_enable_p12()` | Guest, last firmware-owned instant | Source (`cpufreq-s189/Include/Library/NwoasCpuPstate.h`, `NwoasHideHighRamDxe-s167.c` line 477) | Yes | P cluster DESIRED 7->12 only, if baseline is P7 |
| 5 | `cpufreq-s164/pstate12.py` / `late_pstate12.py` | Host | RPC | No (`NWOAS_LATE_P12=0`, not in `-m` list) | n/a |
| 6 | `cpufreq-s164/capture.py`, `cpufreq-s189/observe.py` | Host | RPC reads only | Yes | None |

No cluster P-state code exists in the UEFI tree other than #4: grep of
`apple_silicon_platforms_mu/Silicon` and `Platform` for `211e20020`,
`cpufreq`, `CLUSTER_PSTATE`, `210e00000`, `211e00000` finds nothing outside the
injected `NwoasCpuPstate.h` (which lives in `cpufreq-s189/Include`, copied in
at build time by `build_memory.py`). Windows has no Apple cluster driver.

Guest MMIO reachability: `hv.init()` maps every `/arm-io` range as `HW` with
`TraceMode.OFF`. The saved Tahoe ADT
(`logs/tahoe-dcp-late-20260906-222055.K2gj5O.adt`) has an `/arm-io` range
0x200000000..0x300000000 that contains 0x210e20020, 0x211e20020, 0x211e70210
and 0x23b754000. `map_essential` has the `cpu_hack` list for 0x210e20020 /
0x211e20020 commented out, so there is no hook and no trace on those
addresses. Consequences: the guest's writes really reach the cluster
registers, and they leave no line in the launcher log.

## 5. Evidence that #3 actually runs on hardware

Direct evidence is missing, because the guest m1n1's `printf` goes to the
physical UART0 (hooked in PMGR only), not to the NY3 diagnostic pipe, and the
S189 log contains no guest `cpufreq:` / `Found a kernel` line. What exists:

- **S164 revert (strongest).** `cpufreq-s164/preboot-p12-reverted-p7.json`:
  a host P12 request issued before `hv.start()` was found back at P7 once
  Windows ran. `late_pstate12.py` docstring and `NwoasCpuPstate.h` header both
  already record "the inner payload resets a pre-UEFI request to P7". The only
  P7 writer anywhere in the chain is `cpufreq_init_cluster` default pstate.
- **S133 CPUSTART sequence.** In the S189 log immediately after
  `Jumping to entrypoint at 0x83e7dc800` come `[cpu0]` traps for HID/EHID
  chicken registers (guest m1n1 `init_cpu`), PMGR reads, then
  `CPUSTART W 23b754000+4 = 1<<n` / `+8` (E cluster) / `+c` (P cluster) for
  cores 0:0:1..0:1:3. That is byte-for-byte `smp_start_cpu()` in `src/smp.c`
  lines 168/171 (`cpu_start_base + 0x4`, `+ 0x8 + 4*cluster`), which
  `payload_run` calls one line after `cpufreq_init()`. UEFI/Windows start APs
  via PSCI, not via those registers.
- **ReadyToBoot baseline.** `before=40000107107` (P7, bit42 and bit20 set,
  bit22 clear) matches the post-S140 host snapshot bit for bit. Consistent
  with #3 being idempotent over #2, but not distinguishing on its own.

What this does not prove: that the prefix's compiled `payload.c` / `cpufreq.c`
equals the checked-in source. See section 7.

## 6. What S140 initialises that ReadyToBoot P12 does not

`cpufreq_init_cluster()` for T8103, applied to both `ECPU` (0x210e00000) and
`PCPU` (0x211e00000). ADT pmgr flags from the saved Tahoe ADT are shown; each
feature is set or cleared according to its flag (no blanket enable).

| Step | Register | Operation | ADT flag |
|---|---|---|---|
| APSC pstate reset | base+0x20020 | DESIRED1/2 := 1, SET, poll BUSY | - |
| cpu-apsc | base+0x20020 | clear bit22 (M1_APSC_DIS), wait bit7 (APSC_BUSY) clear | 1 |
| ppt-thrtl | base+0x48400 | set bit63 | 1 |
| llc-thrtl | base+0x40240 | clear bit63 | 0 |
| amx-thrtl | base+0x40250 | set bit63 | 1 |
| cpu-fixed-freq-pll-relock | base+0x20020 | set bit42 | 1 |
| unknown | base+0x440f8 | write64 1 | - |
| APSC init | base+0x200f8 | set bit40 | - |
| table copy | base+0x70000+0x20*1 -> base+0x70210/0x70218 | copy P-state table entry for APSC pstate | - |
| default pstate | base+0x20020 | DESIRED1/2 := 5 (E) / 7 (P), SET, poll BUSY | - |

The S189 post-S140 snapshot (`...cpustate.json`) matches: E cmd
`0x40000105105`, P cmd `0x40000107107`, ppt/amx bit63 set, llc bit63 clear,
bit22 clear, bit42 set on both clusters.

`nwoas_cpu_pstate_enable_p12()` by contrast:

- P cluster only, addresses 0x211e20020 / 0x211e20050.
- Returns `NwoasPstateBaselineNotP7` without writing if DESIRED fields are not
  7/7, and `NwoasPstateBaselineBusy` if bit31 is set.
- Read-modify-write of bits 0x0f01f plus bit25 only; verifies bits 22 and 42
  unchanged; no throttle, APSC, 0x440f8, 0x200f8, or table writes; no E cluster.

So the helper has a hard dependency on a prior full init: with S140 removed,
that dependency is carried entirely by #3. If #3 did not run, the helper would
log `status=4` (`baseline-not-p7`) and leave the P cluster at the iBoot state,
P1 (the S140 "before" line shows the host sees `P=...101/p1` at proxy time).
Windows would then boot at P1, slowly but safely.

Secondary effect worth knowing: with S140 present the P cluster transitions
1->7 (host), then 7->1->7 (guest #3, because the APSC reset step always runs
first), then 7->12 (UEFI). Without S140 it is 1->7 (guest, the APSC step is a
no-op because it is already 1) then 7->12. Removing S140 removes one
redundant transition pair, it does not add any.

## 7. Verified code versus unproven binary provenance

Verified (bytes or checked-in source):

- Prefix layout, DTB identity, FD header origin, and cross-payload identity
  (section 2).
- Current `src/payload.c` and `src/cpufreq.c` are identical between commit
  `59fb544` and HEAD `bddf7f06` (`git diff 59fb544..HEAD` touches only
  `kboot.c`, `main.c`, `proxy.c`, `smp.c` among the relevant files, and none
  of those hunks affect the kernel+fdt branch or `cpufreq_init`). The working
  tree has no further changes to these files.
- Launcher, `run_guest.py`, `hv.load_raw`, `hv.start`, `hv.init` mapping, and
  the UEFI helper are all read from source as used.

Unproven:

- The prefix m1n1 reports `v1.0.2-1471-g59fb544-dirty`. `-dirty` means it was
  built from an uncommitted working tree in late June / early July 2026
  (`NWOAS-현황보고-2026-07-10.md` line 97: 16 modified files at the time). No
  diff of that tree was recorded. The md5 of the prefix's 0x140000 bytes
  (`905e8856...`) matches no stored binary: `m1n1-bare.bin` (`b64269a0...`,
  same version tag, differs at byte 2061), `m1n1-nodtb.bin` (non-dirty
  `g59fb544`, differs), `build/m1n1.bin` (1474). The prefix cannot be
  reproduced from any commit today.
- Therefore "the guest calls `cpufreq_init()`" is established from the string
  table plus the unchanged upstream logic of `payload.c` at 59fb544, not from
  a bit-exact rebuild. The strings `Found a kernel at %p`,
  `cpufreq: Initializing clusters`, `Preparing to boot kernel at %p with fdt
  at %p` and `ERROR: Kernel found but no devicetree` are all present, and the
  observed CPUSTART trace requires the same branch, so a modified branch that
  skipped only `cpufreq_init` is implausible but not excluded.
- No launcher log contains guest m1n1 console output, so "cpufreq:
  Initializing clusters" has never been observed from the guest.

## 8. Minimal controlled next test (after S191 finishes)

Change exactly one thing relative to the S189 launcher: drop the line
`-m "$ROOT/nwoas_scripts/cpufreq-s140/init.py"`. Keep the same HV image
(`m1n1-s163-eoi-gap50.bin`), same payload sha `3b66d5d8...`, same module list
otherwise (`capture.py` stays; it is read-only and will now record the
pre-guest iBoot state, its phase label "after S140 init" is only text), same
`--strict-init`, same PERCPU measurement (`cpufreq-s164/measure.cmd`, 5 runs).
Do not combine with the S192 RTC payload or any storage/USB/memory change.
Also drop the `init.py` line from the `shasum` bookkeeping in the log header
or leave it; it does not affect the run.

Expected observations and their meaning:

| Observation | Meaning |
|---|---|
| `.cpustate.json` pre-UEFI: P cmd `...101`, status `0x11`, E `...105`/`0x55` | Host did not init; iBoot state confirmed as baseline |
| vuart: `S189 ReadyToBoot ... status=0 before=40000107107 ... current=12 target=12 protected=1` | Guest m1n1 `cpufreq_init()` ran and produced the P7 baseline; S140 is redundant. Adopt removal. |
| vuart: `S189 ReadyToBoot ... status=4 before=...101 ...` (`baseline-not-p7`) | Guest init did not take effect; helper wrote nothing; Windows at P1. S140 must stay (or the guest path must be fixed). Safe outcome. |
| `observe.py` first-worker snapshot: `p_status 0xcc`, `e_status 0x55` | Same as S189 |
| PERCPU medians: P cores ~59 ms, E cores ~85 ms, 40/40 checksums | Same as S189 `hardware-result.json` |

Only the P-core median and the ReadyToBoot line are decision inputs. If the
run passes, the follow-up documentation change is to correct the S140 README
premise and the phase label in `capture.py`; if it fails, capture the
`.cpustate.json` and vuart line into `cpufreq-s193/` before any retry.

Optional second boot, only if the first passes and the guest m1n1 console is
wanted as direct evidence: attach the physical UART0 capture (macvdmtool
serial) during one boot to record the guest's `cpufreq: Initializing clusters`
and `Found a kernel at` lines. This is observational and does not change the
launcher.

## 9. Files examined

- `nwoas_scripts/cpufreq-s189-native-guest-test.sh`, `cpufreq-s189/{manifest.json,hardware-result.json,build.log,build_candidate.py,build_memory.py,s131_builder.py,observe.py,Include/Library/NwoasCpuPstate.h,NwoasHideHighRamDxe-s167.c}`
- `nwoas_scripts/firmware-s192/{manifest.json,build.log,build_candidate.py,build_memory.py,s131_builder.py}` (diff vs S189: paths and change string only)
- `nwoas_scripts/cpufreq-s140/{init.py,README.md,test_init_source.py}`
- `nwoas_scripts/cpufreq-s164/{README.md,capture.py,pstate12.py,late_pstate12.py,*.json}`
- `nwoas_scripts/cpufreq-s170/{INTEGRATION-REVIEW.md,ready_to_boot.inc}`
- `nwoas_scripts/uefi-s125/README.md`, `uefi-s12{7,8,9}/build_candidate.py`, `smp-s131/build_candidate.py`
- `nwoas_scripts/logs/cpufreq-s189-native-20260910-051312.0p23NQ{,.vuart,.cpustate.json}`, `logs/tahoe-dcp-late-20260906-222055.K2gj5O.adt`
- `m1n1_windows/src/{payload.c,main.c,cpufreq.c,cpufreq.h,smp.c,kboot.c}`, `proxyclient/tools/run_guest.py`, `proxyclient/m1n1/hv/__init__.py` (`init`, `map_essential`, `load_raw`, `start`), `proxyclient/m1n1/{proxy.py,proxyutils.py}`, git history 59fb544..HEAD
- `apple_silicon_platforms_mu/Platform/MacMini2020Pkg/MacMini2020.fdf`
- Payload bytes: `cpufreq-s189/m1n1-payload-s189-native-p12.bin`, `firmware-s192/m1n1-payload-s192-native-rtc-p12.bin`, `m1n1_windows/m1n1-payload*.bin`, `m1n1-bare.bin`, `m1n1-nodtb.bin`, `apple-j274-pad*.dtb`
- Reports: `NWOAS-진행보고-2026-07-0{6,7}.md`, `NWOAS-현황보고-2026-07-09-v14-handoff.md`, `NWOAS-현황보고-2026-07-10.md`, `NWOAS-STATUS-2026-09-06.md`, `NWOAS-STATUS-2026-09-10-SESSION.md`, `rtc-s178/PLAN.md`
