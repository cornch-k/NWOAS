# HANDOFF: USB-A (FL1100) read-data corruption under Windows on the m1n1 hypervisor

Owner after handoff: Fable 5.2 subagent. Goal: find the root cause and ship a fix that passes the
validation below on real hardware. Everything here is FACT unless marked HYPOTHESIS.

## 1. System
- Mac mini M1 (J274, T8103, 16 GB), system firmware macOS 26 Tahoe (mBoot-18000.121.3).
- Boot: installed m1n1 (D4) -> MacBook host uploads EL2 hypervisor via USB (m1n1 proxyclient
  chainload.py) -> m1n1_windows/proxyclient/tools/run_guest.py starts the guest: guest m1n1 prefix +
  Project Mu UEFI (payload nwoas_scripts/loader-s223/m1n1-payload-s223-fd-tail.bin, source of record
  nwoas_scripts/firmware-s216/source-candidate/) -> Windows 11 Pro 25H2 ARM64 (build 26200.8037) on
  the internal SSD (emulated NVMe through the HV).
- Host python modules (all loaded by the launcher): display-s190/tahoe_dcp_guest_hook.py (pre-init),
  cpufreq-s193/capture.py, usb-s174/guest_map.py (low 4 GiB DMA window, BYPASS tracers, pcie_init),
  smp-s133/pmgr_gate.py, layout-s231/guest_module.py, memory-s180/link_module.py, layout-s231/module.py,
  native-s230/query_module.py, cpufreq-s189/observe.py.
- HV source trees (rsync copies, not git):
  - m1n1-layout-s231/src (S231 baseline), binary nwoas_scripts/layout-s231/out/m1n1-s231-layout80.bin
  - m1n1-usb-s232/src (S232 = S231 + FL1100 DEVCTL RO/NS clear), nwoas_scripts/usb-s232/
  - m1n1-usb-s233/src (S233a/b), nwoas_scripts/usb-s233/ (README has full diff + build command)
  - Reproducible build: see nwoas_scripts/usb-s233/README.md (llvm@20 20.1.8, lld@20, rust 1.88.0,
    RUSTFLAGS remap, same-length M1N1_VERSION_TAG). S230 rebuild is byte-identical (2d763bfb...).
- USB-A ports: Fresco Logic FL1100 xHCI behind Apple PCIe (port with DART stream 1). HV logs its
  register reads as "NWOAS-FLC" (hv_vm.c ~4032-4061), FL1100 BAR writes are trapped (hv_emulate_rw),
  HCCPARAMS1.AC64 is masked (hv_vm.c ~3875-3886, log "FL32 HCCPARAMS1 ... AC64=0"), so Windows usbxhci
  uses 32-bit DMA. DSDT (apple_silicon_platforms_mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl):
  XHC0/SCB0 _CCA=0 (:348, :399), SCB0 _DMA 0..0xBFFFFFFFF (:381-394), no _DMA on XHC0 (:428-436).
- Guest RAM: IPA 0x83BD04000..0xAE0FCC000; low window IPA [0,4GiB) -> PA 0xAE0FCC000+IPA (stage-2
  and DART L2 flat window programmed by UEFI AppleDartIoMmuDxe at ExitBootServices); high_keep
  [0x83BD04000,0xABBD04000) identity (~10 GiB visible to Windows); NwoasHideHighRamDxe hides the rest.
- HV cache maintenance on FL1100 doorbell/USBCMD writes: nwoas_fl_clean_cmd (hv_vm.c ~3020-3061):
  civac CRCR+0x8000, DCBAA+0x4000, contexts+0x1000, output-context TR dequeue +0x100. On USBSTS EINT
  read: ERST clean + event ring 0x1000 invalidate (S231/S232) or civac (S233).

## 2. Symptom and reproduction
- USB stick WINARM25H2 (Ultra USB 3.0, FAT32, 15.4 GB) on a Mac mini USB-A port, enumerated as USB3
  SuperSpeed (PORTSC 0x1203), BOT (dci 3/4 only, xstrm=0 in S233 counters).
- Ground truth (stick read on the MacBook after re-mount, wimlib verify passed):
  install.swm 3,434,190,299 B sha256 9b827189..., install2.swm 3,544,781,245 B c19b48920f62...,
  boot.wim 663,454,398 B b68afdfd6d8d....
- Reproduction (Windows job via NS2 agent; script nwoas_scripts/usb-s232/U3-fresh-copy.cmd):
  fresh boot, `copy /b E:\sources\<file> C:\NWT\...` for the three files in order, then SHA256 with
  D:\NWREAD.EXE (direct). Automated: nwoas_scripts/usb-s232/run-u3-cycles2.sh (env LAUNCHER=<launcher
  script name> TAG=<tag>, arg = cycles); it restarts Windows between cycles (each restart power-cycles
  the Mac mini; the script relaunches the HV). Results in nwoas_scripts/usb-s232/u3-results-<tag>.txt.
- Corruption is read-side: cache-only re-read of E: returns the same wrong hash (U1 job); SSD->SSD
  copy is faithful; copy of the bad file is consistent across re-reads within a boot.
- One characterised bad region (S231, installed Windows, install2.swm): file offset
  0x7E300000..0x7E3EFFFF = 983,040 B (15 x 64 KiB), every byte 0x00, the following 64 KiB correct
  (nwoas_scripts/layout-s231/jobs/U2-ranges.cmd). WinPE copy of the same file had a different bad hash.
- September history (different HV, NWOAS-STATUS-2026-09-08-SESSION.md S105-S121, lines ~30-130):
  buffered reads gave a different wrong hash every run; S111: bad blocks were whole 4 KiB pages, 298
  of 299 equal to an EARLIER page of the same file at 1 MiB-multiple deltas; FILE_FLAG_NO_BUFFERING
  reads were correct; S113 (No Snoop cleared) and S114 (doorbell clean sweep removed) did not fix.
- Unbuffered sequential reads are always correct: robocopy /J (133 MB/s) and NWREAD direct/buffered
  1 MiB single-outstanding.

## 3. Experiments so far (2026-11-15)
| Config | install.swm (1st) | install2.swm (2nd) | boot.wim (3rd) |
|---|---|---|---|
| S231 WinPE copy | bad 5ef00890 | bad 848281b4 | - |
| S231 installed, 1 boot | - | bad aa61fd05 (960 KiB zeros) | - |
| S232 (RO/NS cleared, verified DEVCTL 0x2810->0x2000), 1st boot | - | good x3 + cache | - |
| S232, LPM on, 3 fresh boots | 3/3 good | 0/3 | 1/2 (one run: drive E: vanished, "invalid drive") |
| S232, Windows USB3 LPM + selective suspend OFF, 3 boots | 3/3 good | 0/3 | 3/3 good |
| S232, bcdedit truncatememory 0x100000000 (Windows 4,096 MB), 3 boots | 0/3 | 0/3 | 0/3 |
| S233b (event-ring civac + full transfer-ring walk-and-clean via Link TRBs), 3 boots | 3/3 good | 0/3 (714d0560, 21e792d7, b8a73f87) | 3/3 good |
Raw: nwoas_scripts/usb-s232/u3-results-*.txt. Every bad hash differs (nondeterministic).
- S233b counters at t=113s: db=44178 xfull=351979 xtrbs=87,066,510 xsegs=674,483 xcap=258,910
  xout=602,867 xstrm=0 evt=28 evtop=civac — the full ring clean ran heavily and did not fix it.
- PORTSC (0x4c0) shows U0/U1/U2 transitions and one disconnect/reconnect per session
  (0x1251 -> 0x4202b0 -> 0x2b0 -> 0x221203 -> 0x1203); timing vs copy unknown (no timestamps).

## 4. Hypotheses status
- REJECTED: PCIe No Snoop / Relaxed Ordering alone (S232); USB3 LPM (power plan off); bounce buffers
  for >4 GiB pages (truncatememory made it WORSE); stale transfer-ring TRBs due to HV under-cleaning
  (S233b full clean); USB disconnects as the only cause (corruption without E: vanishing).
- OPEN (ranked by me, HYPOTHESIS):
  1. Coherency of DATA pages: device writes (snooped or not) vs CPU cache lines of destination pages
     that Windows zeroes/reuses right before DMA. Corruption grows with memory pressure (page reuse),
     the bad data is zeros or earlier file pages. With _CCA=0 Windows should flush/invalidate
     (KeFlushIoBuffers / HAL DMA v3); if Windows ARM64 assumes coherent DMA regardless, or the
     invalidate happens before the device finishes, stale lines win. Check: does Windows honour _CCA=0
     on ARM64 for this PCI device? Is the low-window alias (IPA != PA) breaking Windows' cache
     maintenance by VA/PA (e.g. HAL flushing by physical address through a different mapping)?
     Stage-2 attributes for the low window (normal cacheable?) vs DART/PCIe coherency of Apple fabric.
  2. HV-side cache maintenance that DISCARDS data: any remaining `dc ivac` on ranges that can include
     Windows data pages (S233 left some ivac paths: diagnostic dumps, USB-C/HID); also civac racing
     with in-flight DMA (cleaning a dirty CPU line while the device writes it -> CPU's older line
     overwrites DMA data). The CRCR+0x8000 sweep covers 32 KiB around Windows common buffers each
     doorbell; are data buffers ever inside it?
  3. DART/IOTLB: identity/offset maps are correct statically, but IOTLB invalidation when Windows
     remaps? (DART is programmed once; Windows cannot change it.)
- Untested ideas: _CCA=1 in DSDT (payload rebuild); mapping the low window as non-cacheable for the
  guest; stage-2 attribute of the low window; trapping nothing but logging every TRB data-buffer
  address vs the bad page; using a different stick / USB2 port to see if SuperSpeed matters.

## 5. Constraints and safety
- Never open /dev/cu.* while a guest runs except through the launcher; one serial client only.
- Do not touch macOS partitions; S231+ write zone guard must stay. Do not push to git.
- Windows restarts power-cycle the Mac mini; relaunch with the matching launcher. A fresh boot clears
  the Windows cache (needed for each measurement).
- Job scripts must be ASCII; WinPE lacks find/findstr/fc (installed Windows has them); a pipe into a
  missing command aborts the job. queue_job.py requires the previous job to be complete.
- Validation for a fix: 3 fresh-boot cycles of U3 with all three files correct, then 3 more with
  truncatememory 0x100000000 (worst case) all correct; no regression in keyboard/trackpad/USB
  enumeration; record results and commit (no push).

---

## 6. 2026-11-15 (Fable 5.2 subagent) S234: guest DC IVAC semantics under stage 2 -- REJECTED as the cause

Hypothesis tested: with HCR_EL2.VM=1 an EL1 `DC IVAC` is architecturally a clean+invalidate, so a Windows
invalidate after a non-coherent DMA read could write stale dirty lines back over device data.
Build: `m1n1-usb-s234` = `m1n1-usb-s233` + HCR_EL2.TPCP trap and EL2 emulation of DC IVAC/CVAC/CVAU/CIVAC and
IC IVAU (`hv.h`, `hv.c`, `hv_exc.c`, `hv_vm.c: nwoas_dc_emulate`), `-DNWOAS_FL_RING_FULL_CLEAN=1`.

| Binary | NWOAS_DC_IVAC_TRUE | Size (B) | sha256 | Launcher |
|---|---|---|---|---|
| `usb-s234/out/m1n1-s234A.bin` | 0 (IVAC emulated as civac = control) | 2211840 | 4e69b467bec7c24b39f6a27544e0bdc8f6d4e034c5a76fee81c320f7c7a75a61 | `usb-s234a-guest-test.sh` (not run) |
| `usb-s234/out/m1n1-s234B.bin` | 1 (IVAC = real invalidate at EL2) | 2211840 | 4978c91b0f445ce56881353284e3ea8dc0f7c3a19e79ed2877539c786b52321d | `usb-s234b-guest-test.sh` |

Result S234B, 3 fresh-boot U3 cycles (`usb-s232/u3-results-s234b.txt`):

| Cycle | install.swm | install2.swm | boot.wim |
|---|---|---|---|
| 1 | good | bad cc0789ca | good |
| 2 | good | bad f3f03265 | good |
| 3 | good | good | good |

FACTS from the trap counters (cycle 1, t=203 s): Windows kernel issued 220,203,704 DC IVAC (64 B each = 14.1 GB),
271,505 DC CIVAC, 876,878 DC CVAC, 0 DC CVAU, 0 IC IVAU; every sampled IVAC run (1,080 samples in 9 windows)
came from one kernel PC (0xfffff8005b25b800) and covered only the HAL bounce (map-register) region, guest-physical
0xFFED0000..0xFFF60000, before the bulk doorbells. Trap cost about 0.37 us per instruction (24 us per 4 KiB).
Conclusion: making DC IVAC a true invalidate does not remove the corruption (2 of 3 cycles still bad) -> not the cause.
S234 is an experiment only; do not ship it.

## 7. 2026-11-15 S235: bulk TRB / event / LBA-hash probe -- ROOT CAUSE LOCATED

Probe: `m1n1-usb-s235*/src/nwoas_s235_probe.inc` (included from `hv_vm.c`, observation only). On every FL1100
doorbell it follows the bulk rings like the controller does (dequeue pointer | DCS from the output context,
then every TRB whose cycle bit matches; Windows rings are dynamic, walking them by Link TRBs from a stale
pointer does not work -- this is also why S233b's "full ring clean" reported xcap on 74 % of its walks). It
parses the BOT CBW on bulk OUT (READ(10) LBA / blocks), and on every ERDP write it processes the events usbxhci
has just consumed (Windows ends each TD with an Event Data TRB; one 512 KiB READ(10) = one TD). For each
completed bulk-IN data TRB it hashes the destination buffer per 4 KiB as the CPU sees it and compares the hash
with the first time the same LBA was read (table of 4 M entries keyed by LBA/8).
Windows job `usb-s235/U5a-ref-copy.cmd`: unbuffered NWREAD of install.swm + install2.swm (SHA256 verified,
fills the table), then the U3 `copy /b` sequence; `U5b-map.cmd`: robocopy /J reference copy + 4 KiB compare of
the bad file (PowerShell/C#), prints the bad file ranges. Runner `usb-s235/run-cycles3.sh`.

Probe binaries (all `-DNWOAS_FL_RING_FULL_CLEAN=1`; P/Q/R have incomplete TRB tracking and are kept only for
the bad-range results):

| Binary | Probe options | sha256 |
|---|---|---|
| `usb-s235/out/m1n1-s235S.bin` | consumer tracking, read-only (no pre-hash, no post-ivac) | 1073bddbedcf76e5f4c8ff906a0d06434c1ec86104ecc2a45e8638abe80c3025 |
| `usb-s235/out/m1n1-s235V.bin` | S + per-mismatch diagnosis (DRAM view after dc ivac, DART leaf, re-check) | 98fb63ed13db56a02af97cb46c9ed8cfc21f0728c68d5e7df9feee320a8f9d96 |
| `usb-s235/out/m1n1-s235W.bin` | V + one-shot ECAM dump of the root-port windows + counter of IN TRBs inside 0xC0000000..0xC00FFFFF | 3ab0b14c6b1946aaeb3ae400c6790309165c49312c3f1242289c93df1246b889 |

Bad ranges of install2.swm (file offset, length; every bad byte is 0x00), one range per boot:

| Run | Offset | Length (B) | Length (64 KiB units) |
|---|---|---|---|
| S231 (earlier) | 0x7E300000 | 0xF0000 | 15 |
| S235P c1 / c2 | 0x9CAF0000 / 0x96E00000 | 0x10000 / 0x60000 | 1 / 6 |
| S235Q c1 / c2 | 0xB0100000 / 0xB6290000 | 0xD0000 / 0x70000 | 13 / 7 |
| S235R c1 / c2 | 0xBD400000 / none (all good) | 0xE0000 / 0 | 14 / 0 |
| S235S(old) c1 / c2 | 0x99CE0000 / none (all good) | 0x20000 / 0 | 2 / 0 |
| S235S c1 / c2 | 0xB0C00000 / 0xB01D0000 | 0xD0000 / 0x30000 | 13 / 3 |
| S235V c1 | 0xA8810000 | 0xF0000 | 15 |

FACTS (S235S cycle 1, complete tracking: 34,800 CBW, 69,602 TDs, 4,272,373 IN TRBs, 0 orphan events,
0 completion codes other than Success on READ(10) data TDs):
- 1,698,405 4 KiB pieces re-read during the copy matched the reference pass; 208 did not (pdiff). The copied file
  had exactly 208 bad 4 KiB pages (0xD0000 B), all zero. The mismatch count stepped 0 -> 208 in one 20 s window.
- So the data is already wrong in the DMA destination buffer when usbxhci acknowledges the transfer; nothing
  above the xHCI transfer (HAL copy-back, cache manager, FAT) is involved.

FACTS (S235V cycle 1, the 217 logged zero mismatches of the 240 bad pages):
- every one is a direct (not bounced) 64 KiB TRB whose buffer is guest-physical = IOVA 0xC0010000..0xC00EFFFF
  (TRB buffers 0xC0010000, 0xC0020000, ... 0xC00E0000; log cap reached before the last one);
- CPU view = zero AND DRAM view after `dc ivac` = zero, also 30 ms and 1.5 s later: the device data never reached
  the RAM behind that IOVA. It is not a CPU-cache effect and not a late DMA;
- completion code Success, EDTLA = full TD length: the controller believes it delivered the data;
- the DART (stream 1) leaf for those IOVAs is the same flat map as for working addresses, DART error latch
  unchanged (0x0c000400, FLAG=0).
- The other mismatches in that log (203, "cpu=other dram=other", 4 KiB TRBs in the bounce region 0xFFEFxxxx..
  0xFFF5xxxx) are probe races on recycled bounce pages (the probe looks at ERDP-write time), not corruption:
  those bytes are correct in the file.
- All bad-range lengths ever observed are <= 0xF0000 = 15 x 64 KiB = exactly [0xC0010000, 0xC0100000).

ROOT CAUSE (FACT: address range, missing data, and the bridge-window register values read from config space in
section 9; the only inferred step is the standard PCI-PCI bridge forwarding rule itself):
- DSDT: the FL1100 BAR is at PCI bus address 0xC0000000 (CPU 0x6C0000000, 64 KiB). The root port above it
  therefore decodes a memory window of 1 MiB granularity, bus 0xC0000000..0xC00FFFFF.
- The low 4 GiB guest window is declared to Windows as ordinary RAM in full, and guest-physical == bus address
  (DSDT _DMA, no translation). So guest RAM 0xC0000000..0xC00FFFFF has the same bus addresses as the bridge window.
- A PCI-PCI bridge does not forward upstream a transaction from its secondary side whose address lies inside its
  own memory window. FL1100 DMA to those addresses never reaches the DART / RAM; a bulk-IN leaves the page
  untouched (Windows had handed out zeroed pages -> zeros in the file), a bulk-OUT from such a page would send
  garbage (not tested).
- Why it looked like a memory-pressure / "second file" problem: Windows uses those 256 page frames late. With
  ~14 GiB visible they come into use for the page cache only when free memory runs out (about 2.1-3.0 GiB into
  the second file); with truncatememory 4 GiB they are reused all the time (all files bad). Unbuffered reads go
  through a user buffer in high RAM -> always bounced -> always correct. September's "earlier page at 1 MiB
  deltas" pattern was a different hypervisor/DMA layout and is not explained here.
- Not involved: CPU cache coherency of data pages, PCIe No Snoop / Relaxed Ordering, USB3 LPM, TRB 64 KiB
  boundary crossing (Windows splits TRBs by 64 KiB length, 13,098 crossings in one run with no corruption),
  FL1100 errata, the USB stick.

## 8. 2026-11-15 S236: FIX (validated on hardware) -- keep guest RAM 0xC0000000..0xC00FFFFF out of use

Fix applied: Windows BCD `{badmemory} badmemorylist` = PFN 0xC0000..0xC00FF (256 x 4 KiB = 1 MiB), set by job
`usb-s236/B1-set-badmemory.cmd` (bcdedit exit code 0), active from the next boot. Persistent in the installed
system's BCD store; revert with `usb-s236/B2-revert-badmemory.cmd`. No hypervisor change and no payload change:
validation ran on the unchanged S233b HV (`usb-s233/out/m1n1-s233b-ringclean.bin`, sha256 d2eb62b5...) and
the S223 payload (sha256 2343f7fc...), launcher `usb-s233b-guest-test.sh`.
Orchestrator `usb-s236/validate.sh`, raw results `usb-s236/results-s236bm.txt`.

| Condition | Cycle | install.swm | install2.swm | boot.wim |
|---|---|---|---|---|
| normal (about 14 GiB visible) | 1 | good | good | good |
| normal | 2 | good | good | good |
| normal | 3 | good | good | good |
| truncatememory 0x100000000 (Windows reports 4,095 MB) | 1 | good | good | good |
| truncatememory | 2 | good | good | good |
| truncatememory | 3 | good | good | good |
| normal, S235W probe HV, U5a job (reference read + copy) | 1 | good | good | good |
| normal, S233b, final session | 1 | good | good | good |

Before the fix, same test: install2.swm bad in 9 of 9 fresh-boot U3 cycles on S232/S233b (section 3), bad in
11 of 14 cycles on the S234B/S235 experiment builds (sections 6-7); with truncatememory all three files bad in
3 of 3 cycles.
truncatememory was REVERTED (job T2, confirmed absent in `usb-s236/final-devices-state.log`). USB3 LPM and USB
selective suspend are still OFF in the power plan (unchanged, earlier session's setting).
Final state: Windows running under S233b, launcher log `logs/launcher-s236final-*.out`, session process alive.
Devices connected in the final session: USB keyboard receiver 3434:D030 (HID keyboard/mouse collections) and
the USB stick 0781:5591; physical keyboard/trackpad feel was not checked by a person.

## 9. 2026-11-15 S235W: config-space facts and the fix seen from the HV

`logs/usb-s235w-*.vuart`, one boot with the badmemorylist fix active:

| Device (ECAM) | Bus range | Memory window (bus address) | Notes |
|---|---|---|---|
| root port 0 (bus 0 dev 0) | 1..1 | disabled (base 0xFFF00000 > limit 0x000FFFFF) | |
| root port 1 (bus 0 dev 1) | 2..2 | 0xC0000000..0xC00FFFFF | FL1100 path |
| root port 2 (bus 0 dev 2) | 3..3 | disabled; prefetchable 0x4_00000000..0x4_000FFFFF | |
| FL1100 (bus 2 dev 0) | - | BAR0 = 0xC0000004 (64-bit memory BAR at 0xC0000000), BAR1 = 0 | |

| Probe counter (whole boot) | Value |
|---|---|
| bulk-IN TRBs tracked | 2,060,049 |
| bytes in those TRBs | 14,629,381,257 |
| IN TRBs with a buffer inside 0xC0000000..0xC00FFFFF | 0 |
| 4 KiB pieces equal to the reference read | 1,700,647 |
| zero-type mismatches (the corruption signature) | 0 |
| other mismatches (bounce-page probe race, file correct) | 391 |
| U5a result | all three files good |

## 10. Remaining risks / follow-ups

- The validated fix lives in the INSTALLED Windows BCD only. WinPE / Windows Setup booted from the USB stick, a
  reinstalled Windows, or another OS will again treat 0xC0000000..0xC00FFFFF as RAM. Platform-level form of the
  same fix: `usb-s236/firmware-reserve-bridge-window.patch` (reserve that 1 MiB in the UEFI memory map).
  It is a PROPOSAL: not built, not booted. It needs a payload rebuild through the S216 -> S223 pipeline and the
  same 3 + 3 cycle validation; after it is in place the BCD entry can be removed with B2.
- Bulk OUT (writing to a USB device from a page in that range) was never tested; by the same mechanism it would
  have sent wrong data. With the pages excluded it cannot occur.
- If the FL1100 BAR / root-port window is ever moved or enlarged (other firmware, PCI re-enumeration), the
  excluded range must follow it. The S235W probe prints the live windows (`S235 ECAM` lines).
- Other PCIe devices (Wi-Fi on port 0, Ethernet on port 2) have no Windows driver here; when they get one,
  their own bridge windows need the same treatment (port 2 already has a prefetchable window at
  0x4_00000000, which is not guest RAM in the current layout).
- S233b's whole-ring clean and the S232/S233 cache-maintenance changes were not needed for this bug; whether to
  go back to a simpler HV was not evaluated. S234 (cache-instruction trap) and S235 (probe) are experiments,
  not production hypervisors.
- The September "page equals an earlier page at 1 MiB deltas" corruption (different HV) is not explained by
  this root cause and was not reproduced today.
- Launchers `usb-s234a`, `usb-s235t`, `usb-s235u` exist but were never run. `m1n1-s235U.bin` carries the version
  tag string "s235R", and V/W carry "s235S" (incremental builds in the same tree).
