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
