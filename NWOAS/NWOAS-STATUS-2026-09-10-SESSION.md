# NWOAS session — 2026-09-10

## Objective and operating contract
Native Windows ARM64 on the M1 Mac mini, retain Tahoe firmware. User authorized live autonomous work with Claude Code until 09:00 KST, not a scheduler. MacBook/unrelated data must remain intact. Korean honorifics, address 콘치님. Windows desktop is already reached, but stability/native hardware support is unfinished. Current 8-core execution still uses EL2 hardware mediation and host-backed service transport; do not claim completed direct native drivers or macOS-equivalent performance.

## S159 result — rejected
Corrected 256-block backend S159 submitted physical I/O only on next 5 kHz tick. Actual 8-thread 64 KiB read test became ~2x slower (388016..434688 us for 64 MiB vs S158 ~196..200 ms). Uninterrupted run usb-s159-20260909-234847.C3i6zw crashed 0x133 subtype0. It did not remove blocking I/O/global-lock occupancy. Initial S159 and rebuilt S158 control failed because their backend was compiled for 16 instead of 256 blocks; those are invalid scheduling comparisons. See nvme-s159/HARDWARE.md. Restore/fallback S158 also later crashed subtype0; neither is stable.

## S160 local NVMe interrupt-mask fast path
C handles INTMS/INTMC locally when no Python-owned interrupt-enabled CQ has pending entries. Host pulls the C mask before fallback operations, verifies action8 signature; env gate defaults off. Original inline SQ processing retained. Driver register semantics and INTx-disable preserved.

S160a HV f95bee4537f0d961b54ea3546af34100a8909e4ef9491066fc051a8d8c0ae0c7:
run usb-s160-20260910-000739.m0HOW6. 8-core reads all matching checksums, 64 MiB 118707..134699 us in repeated test (~1.5x faster than specific S158 test). USB-A root, keyboard and trackpad PnP Code0 primary nodes. No sleeping-user physical input confirmation. 30 min soak failed at sample4, 0x133 subtype0. NS1 max6.10275ms, NS2 max10.627375ms; no I/O active, CQpending2, mask0. 67794 local mask writes already processed. Removing mask transport overhead did not solve watchdog.

Fable reviewed actual code. S160b corrected missing action1 flag initialization, duplicate action2 assignment, and disabled local mask interception after fastpath fault. Eight actual C/host contract tests and 22 prior checks pass. S160b HV b9cdfb5b2696f0e56e3f686a1dc52a0f77dd46e6fb9da989bcc03ecfd7e7b7cd, 2146304 bytes. Host guest_module.py SHA5f8cf61b3ccc32fc53a40f3ea14aa3ffa5ffbd34f672e1e7092e6eb291db3c05.

Live as of00:36: usb-s160b-20260910-001651.tY3ae4, TTY37783, PID23346. Jobs1..3 exported minidump and exact stornvme/storport images. Job4 30-minute read/CPU soak running, first3 samples pass. No manual HV pause in this run.

## Crash evidence and S162 observational candidate
New saved S159 dump: logs/S159-050722-5265-01.dmp,261875bytes,SHAfd4091a3886b4d2a44ff5a779cf7fca1e81fe4d8a5af10a76c360ca365b3bbf9.
New saved S160a dump: logs/S160-050722-5203-01.dmp,275555bytes,SHA3e5831b5da04080dd7746dc5115e534e635181abb6f7009228ea15a2e1536ecd.
Both 0x133 subtype0, eight CPUs. Raw extra stack candidates and watchdog sample buffers repeatedly point to storport+0xd8fc, LR stornvme+0x18924. These scans are NOT full stack unwinds.

Verified exact installed driver images exported via NWOS:
- stornvme.sys191832bytes SHA9e5f07359caba0cd988d74e77f4469b2f70af4075c561eecdfa5cb9b75a1422d.
- storport.sys1099608bytes SHA7d3cf7c634fe74a66893a070a3b976eb9538c55af3e585c627623af683fd64f7.
Images retained privately in nvme-s162, do not commit Microsoft binaries.

Actual storport instruction: +d8f8 STR W2,[X8] (INTMC register), +d8fc ADD SP,SP,#0x10, then RET. DSB is earlier at+d8e4. Thus repeated sampled PC is immediately AFTER the emulated unmask store, not a CQ phase polling load or DSB. NVMeCompletionDpcRoutine clears adapter byte+0x19 only after return, at+18924. Opus reviewing whether pending interrupt reinjection can starve this return; not proven yet.

CQ coherency hypothesis remains conditional: S160 publisher has DMB phase ordering but no explicit clean, old Python writer civac'd. All-WB coherent views would undermine mismatch hypothesis. Need actual guest stage1 attributes, not assume from stage2WB.

S162 candidate ONLY records CQ virtual/physical mapping on a bounded validated Windows CQhead write. x21 context is trusted only after RAM translation and [x21] maps to exact armed CQ IPA/PA. AT+ISB, preserves PAR; no guest memory/cache/MMIO writes. Diagnostic in RAM, print only action10/bugcheck. No IRQ change. Built but not run as of00:36:
m1n1_windows-s159/build/m1n1-s162-cq-map.bin2162688bytes SHA6fea29b58a8803077e0d66bef32a61dad5e8213e6317401d85f7c3febedc7a37.
Launcher usb-s162-guest-test.sh. S159-named isolated worktree source now S162, not S159!

## S161 memory normalization — offline built, not deployed
Fable supplied shared layout header and proposed diff in memory-s161. Main found padding-dependent memcmp in test; changed to member comparisons. Both compile modes now179checks each pass. Actual UEFI incremental build and96image validation succeeded, sources restored exactly. Separate payload output/manifest in memory-s161; original S131/S139 preserved.

Fixes double subtraction: mode1 BootArgs.mem_size already excludes 4GiB backing. PrePi and early DART previously subtracted again, earlybacking9E0FCC000 vs finalAE0FCC000, actual early wideL1[1054..1264] shorter than logged final identity coverage. Shared helper normalizes backing and extends allocated tables. Windows high-RAM cap remains unchanged. This changes UEFI allocation placement by4GiB; hardware validation required, do not mix into S162 storage comparison.

## Remaining
Watchdog root cause, stable USB-C, monitor hotplug/power, native hardware drivers/network, full physical RAM, genuine CPU frequency reporting/benchmark parity, and final SSD partition layout remain unfinished. C is only~24.8GB with~1.2GB free after managed hiberfil removal, not128GB. Preserve C:\S117SRC install.swm/install2.swm until verified external backup. No new git push yet; main repo tracked source remains S158, isolated candidates in scripts/worktree. Never git add all.

## 00:47 update — S160b failed, CQ attributes measured, S163 comparison started
S160b job4 failed during DISKREAD of sample7 after6complete samples. Same0x133subtype0,p4fffff800c7d19338,NS1max184053ticks(7.668875ms),NS2max381938(15.914083ms),NS1calls46821,NS2calls3060,CQacks47557,backlog1,pending2,mask0,localmaskwrites93908. No HV pause in S160b. Auto reset/CDCdisconnect followed crash; not an agent forced reboot.

S162 run usb-s162-20260910-004054.3nJetb job1 hardware validation passed. One short intentional HV shell pause for action10 CQ-map query, immediately cont(): found1 attempts1 CQVAffffc309d4980000 IPA9de580000,PARs1=PARs12=ff000009de580b80, rawS2walk9de580001. Normal WB, inner-shareable in both translation results. CQcontextffffc309d3ad28b0,PCfffff802772ed8f8. No support for NC/WB CQ mismatch; no extra cache-clean trial. This paused run is NOT an uninterrupted stability result. Job2 gracefulWindowsrestartcompleted.

Main driver finding confirmed exact-image NVMeMaskInterrupt skips INTMS if byte19 already1. DPCunmasksINTMCbeforeclearingbyte19. Fresh CQ entries + immediate post-unmask IRQ can plausibly starve DPC epilogue. Opus and Fable raw reports contain overclaims; main adjudication nvme-s162/REVIEW.md authoritative. In particular fast_level ALREADY gates !(mask&1), contrary to Fable report; do not "fix" missing gate that exists.

S163 candidate gap200us after maintenanceEOI900, leaves requestedlevel intact, allqueue/head/mask semantics unchanged; timer/USBunmodified. Default source macro0, explicitly build200us. ActualCtest passes macro0and200, affinity/dedup/LRcapacity/cancel/exactdeadline/wrap/redelivery. Hardware comparison nowstarting:
HV2162688bytes SHA5c14b870824bffd1a62d81e7a18edb579801323b484194e5b22905711156a173.
run usb-s163-20260910-004716.Z3njqz TTY86415. Source isolatedworktree nowS163 (hv_exc.c plusS162cqprobe inhv_vm.c). Mainrepo unchanged. Opusactualcandidate review running. Timing mitigation is an experiment, notprovenrootcause/finalsolution.

## 01:10 progress

S163200us run job2 has completed20 CPU/read samples without failure so far.
No HV pause or new queue task during the soak. Its read test is~390ms/64MiB,
slower than S160's~120ms. Await full30minresult before comparison.
S16350us HV already built, SHA bd8f16f286c8d1141df4a17d9b85eafdd4c2d39d5680c6ed75b63429f061d166;
launcher usb-s163g50-guest-test.sh adds opt-in read-only target-query module
and pre-UEFI CPU state capture. Query requests must stay outside timed tests.

S164 CPU preparation: PERCPU.EXE4608bytes SHAefdb26b96228f42ae8077a3c0f0194e3fa4acaf2fc89549916cd1771ffcc1da0,
ARM64/noCRT, bounded sequential per-logical-CPU workload with verified affinity
and aggregate. Built, not run. Chunked upload commands prepared; no USB swap.
S140 currently requestsE5/P7. Upstream Linux mapsP7=1956MHz,P12=2988MHz;
no clock change yet. ActualSTATUS and measured throughput are required.
Opus reviewed CPU control; main corrected its blanket thermal-enable claim:
savedTahoeADT cpu-apsc1,ppt1,llc0,amx1. Preserve existingfeaturechoices.
Fable reviewed IRQ return order; identified invalid S163 last_eoi_pc because
directIRQpath lacks hv_get_context. Timestampgap unaffected. See nvme-s163README.
S161 builtcandidate review confirmsmappingrange fits; allbackinglogsmustagree
onAE0FCC000 and WIDE-DART L1[1054..1392] before acceptinghardwaretest.

## 01:20 S163200us completed uninterrupted30minsoak

usb-s163-20260910-004716.Z3njqz job2 completedexit0. All31CPU/read samples
passed expected checksums, active8/threads8, readfailed0. Hostqueue-to-lastwrite
1820.2228s; guesttimestampspan1816.4212s. CPUus min90521/median128713/max156095;
64MiBreadus min389201/median392143/max406414. No manualHVpause or reboot.
This is a bounded successful run, not proof of lifetime stability/rootcause.
Structured result nvme-s163/soak-200-result.json. PERCPUupload/measurement
queued sequentially asjobs3..7 afterresultcapture; not partofsoak.

## 01:35 S16350us active load pass, 30minsoak starting

S163200usjob8 continuous5minreadPASS:745samples,300141ms,49,996,103,680nominalbytes;
median394556us,min354100,max705132. Job9gracefulWindowsrestartcompleted.
Newrun usb-s163g50-20260910-012616.VFQrah,TTY1202,50usHVbd8f16...
SameS1398coreUSB-Apayload. Read-onlyprebootcapture: Edesired/status5,
Pdesired/status7; bothBUSY/APSC_BUSYfalse; APSCenabled,ppt/amxbit63set,
llcbit63clear, matching actualTahoeADT. NoPstatechange.
Job1hardwarePASS:CPU90611us,64MiBread144850us,USB-ArootCode0.
Job2PERCPU5runs40samplesPASS. Job3continuous5minreadPASS:1979samples,
300079ms,132,808,441,856nominalbytes; min130332,median138505,max1181356us.
Duringpartofjob3 only, officialCinebenchdownloadtoX31limited4MiB/s,nice15;
no deployment/guestspacechange. Firstjob1andPERCPUprecededdownload.

Firstopt-inread-onlytargetquerycompletedafterjob3in2.226ms, noHVshellpause:
IRQinject=EOI=2049048,gated49265; last_inject_pcfffff800aeab5fa4.
last_eoi_pc isinvaliddiagnostic inthisbinary, do notinterpret.
CQmaps1=s12=ff000009de571b80, bothWBinner-shareable,found1/attempt1.
NS1max200005ticks=8.33354ms,NS2max555883=23.16179ms BEFOREthisquery's
completion. No I/O errors fromaction3highbits. Queryresultstoredinlinkfolder.
Job4same30minread-mostlysoakqueuedafterquery. Do notqueryduringtimedsoak.

Hostrun_guest.py nowadds--strict-init option. Withoutit, moduleexception
entersshellandcanproceedonEOF. Candidate50/query/USB-C launchersoptin;
4actualASTtailtestsPASS. Defaultinteractivebehaviorpreserved.
S164pstate12.py updatedtoavoidrollbackwritewhileBUSY,recordSTATUS,abort
onfailedrestore;5modulefakeproxytestsPASS. P12stillnotdeployed.

### 01:59 KST — live S163 gap50 soak and isolated next candidates

- Current `usb-s163g50-20260910-012616.VFQrah.link` job4 remains uninterrupted;
  sample23 CPU+DISK passed. No query or reboot during this 30-minute run.
- S167 isolated payload built, not deployed: `memory-s167/manifest.json`,
  SHA c253beb63eed818c319bdda57bc8e5a25c8042c31f55c23f10811ec758a12e4d.
  It adds a bounded first-4GiB high-RAM keep interval to S161 normalization.
  Full descriptor split preserves upper tail; insufficient capacity returns
  BUFFER_TOO_SMALL for retry, no allocation or live MapKey mutation.
  Fable's 365 real-header checks and main actual-wrapper tests (off/on modes,
  probe/retry, low-DMA guard, metadata/MapKey) passed under ASan/UBSan.
  All temporary UEFI sources restored byte-identically. Hardware S161 control
  must pass before S167. Expected Windows RAM is not yet measured.
- S165 stateless USB-C comparison built atop S163 gap50, ONLY S157 function
  cursor-persistence removal: SHA 2742051583cb237ed8f2a1060c3fbeb9b69882a7dc8f9b26316a75fdb3a9c189.
  Both `usb-s165-control-guest-test.sh` and `usb-s165-stateless-guest-test.sh`
  prepared, not run. S157 still does not prove TRB ownership or sustained input.
- `cpufreq-s164-p12-guest-test.sh` prepared, not run. Strict init, P7 baseline,
  bounded busy-aware P12 request/fallback, post-change register capture;
  5 module tests and 4 strict-init tests passed. No thermal/SMC feature changes.
- Official Cinebench 2026 ARM64 ZIP downloaded and hash/Content-MD5/PE machine
  verified on X31. Not installed/run. Current Windows RAM/C: free space remain
  blockers for meaningful benchmarking; no score or macOS parity claimed.
- `file-export-s168` raw-binary stdout test and exact source export jobs prepared.
  `verify_export.py` requires exit0+exact length+SHA and can retain a verified
  backup via same-volume hard link. No source deletion/export yet. Opus is
  implementing an isolated NS2 artifact channel; current host modules untouched.

### 02:06 KST — S163 50us uninterrupted 30-minute PASS

- Job4 completed exit0, 31/31 checksum-correct CPU and read samples, no query or
  HV pause/reboot during the measured run. Host duration1812.339s.
- CPU us min112378/median128502/max144769; disk us min137207/median141249/max147818.
- Same 200us control median392143us => 2.776x shorter read-test elapsed at50us.
  Limited repeated64MiB workload, not general SSD bandwidth or lifetime stability.
- Structured `nvme-s163/soak-50-result.json` saved before overwriting job.json.
- Job5 now running known1MiB+37binary rawstdout test; export/backup only after
  exact size/SHA verification. No deletion or reboot yet.

### 02:30 KST — actual P12 gain; preboot reset identified

- S163gap50 job5 rawstdout1MiB+37test matched length/SHA.
- Job6 install2.swm export completed564691107B, exact knownSHA, retained as
  `file-export-s168/verified-backup/install2.swm` + verifiedJSON. Source retained.
  Slow legacy throughput~0.74MiB/s motivates S16964KiB transfer for install.swm.
- Job7 gracefulWindowsrebootcompleted, oldTTY1202ended/portsfree.
- CURRENT run: `cpufreq-s164-p12-20260910-021853.lQ4qfS`, TTY86917.
  SameS163gap50/S139USB-A8core. Preboot P12readbackCMD0x4000010c10c STATUScc.
  Windowsjob1 hardwareCPU+SSDpassed8cores/8logical, USB-A/rootCode0.
  But job2 PERCPU40samples stillP7speed (~90msPcores).
- BriefpostmeasurementHVshellread confirmedP_CMD0x40000107107 STATUS77:
  prebootP12wasresetduringpayload/boot. Inner m1n1 payload.c calls cpufreq_init;
  that is the leading overwrite path (hardware confirmsreset, exactwriteuntraced).
- A second briefHVshell entry applied the existing boundedP12helper afterboot.
  First attempt used shell `exec`, which is an ASSEMBLY alias and failedcompile
  beforeexecution. Correct `__import__('builtins').exec(..., {'p':p,'hv':hv})`
  appliedP12 and resumed. No resulting compiledassembly wasexecuted.
- Job3PERCPU5runs40checkspassed: Pcores median59091/59066/59009/59038us vs
  job2P7 median90283/90257/90263/90253us =>1.528..1.530x speedup.
  Ecoresunchanged~85ms. Saved`preboot-p12-reverted-p7.json`,`live-p12.json`.
  Thisiscore-localPRNGtest, notCinebench, and thisrunhadexplicitpausesbeforejob3.
- CURRENT job4 CPU-heavy5minsoak plusDISKREAD every16iterations isrunning;
  lastobserved971CPU+60diskpasses, nofailure. Do notpause/queryduringthistest.
- `late_pstate12.py` + `cpufreq-s164-latep12-guest-test.sh` prepared (notrun):
  boundedone-shotP12onfirstWindowsworkerNS2rendezvous, nointeractivepause,
  noretryonfailure, result`late-p12.json`;2callbacktestsplushelper5tests pass.
  Originalprebootlauncherremainsforhistory, notapersistencefix.
- S168downloadclientmainreviewfixed non-NUL-terminatedPhysicalDrivepath and
  addedper-responseSHA/extentvalidation. NativeARM64binarySHA
  1b40db539c222cd80c29cb03149e0f5d5a010848203914272eaee77434d895d9.
  RealPythonchannelgoldenvectors+nativewiretests pass; hardwarepending.
- S16964KiBsourceuploadhost+nativeclient implementedmainagent afterOpusCLI
  returnedcybersafeguardrefusal (nofallback/rewordretry). Clientfixedartifact,
  exactNS2identity, onlyLBA160..175writes,ACK176, CRC/token/seq/offset/knownSHA,
  read-onlysourcehandlewithOPEN_REPARSE_POINT, neversourcedelete.
  Hostexclusivepartial/fullhash/no-clobberfinal; fsync+rehashoutsideNVMehook.
  11hosttests+actualCframe/Pythonreceiverinteroptests pass. FinalNWPUT.EXESHA
  9789268cc68311776c8f80fe8f10e6503ccc56b3a5b902a40d8546c39383985b.
  `memory-s161-upload-guest-test.sh` prepared/notrun; includesinactiveS169and
  NWGET/NWPUTRAM-FATtools whileS161layoutcontroltested. Allrelevanthashespinned.
- NewClaudeOpus4.8 task`opus-s170-pstate`,session34406, buildingofflineUEFI
  callback-basedP12helper+testsunder`cpufreq-s170` forReadyToBootintegration.
  Noactivehardwarepermission orsource-treeeditsdelegated.

### 02:55 KST — S161 hardware layout PASS, automatic P12, durable backup

- Manual late-P12 run completed CPU-heavy 5-minute soak: 3156 CPU and 197 disk checks passed, 300055 ms. P-core gain persisted in 40 follow-up checks. Evidence `cpufreq-s164/live-p12-soak.json`, `live-p12-after-soak.json`.
- Fresh automatic late-P12 run `cpufreq-s164-latep12-20260910-023227.4uDOb8` passed 40 per-core checks, P-core medians ~59 ms versus P7 ~90 ms, plus hardware inventory/CPU/SSD checks. Callback 2.2 ms, no interactive pause. Graceful reboot completed.
- CURRENT run `memory-s161-upload-p12-20260910-023617.ve8o1p`, TTY60712: S161 normalization with existing RAM cap, S163 gap50, automatic lateP12, USB-A/8 cores. Host backing == early UEFI == DXE == final DART = 0xAE0FCC000, 339 wide L1 tables, 692001 final identity PTEs. Actual layout checker PASS saved `memory-s161/hardware-layout.json`. Hardware job1 PASS, P12 callback 1.8 ms.
- S169 job2 transferred install.swm 3987720636 bytes, exact full SHA256, exit0. Host independently fsynced and rehashed both SWM files in `file-export-s168/verified-backup`; JSON evidence beside each. No original files deleted during transfer.
- Job3 NOW queued: revalidate guest source length/hash and delete exactly C:\S117SRC\install.swm and install2.swm, reclaiming ~4.55GB. Other guest/MacBook files untouched. Cleanup result pending.
- S170 ReadyToBoot CPU-P12 helper: Opus4.8 completed six review corrections, 65 checks under plain and ASan/UBSan passed. Main integrated and built isolated S139-layout control candidate, SHA 19aa9dcf50ce257eee860031c22f36208140877bd13fd602a1869aad778c6e3f, 32342016B. Source tree restored. `cpufreq-s170-guest-test.sh` prepared; hardware NOT YET tested, host lateP12 intentionally absent. Header prose saying setting survives permanently is prospective/overstated: only hardware can establish persistence.
- Claude Opus4.8 currently implementing isolated native ARM64 memory-integrity test in `memory-s171-test`; no hardware access delegated.
- NEXT: S167 bounded 4GiB high RAM plus low window, download module + automatic lateP12 launcher prepared. Validate inventory/checksums/USB-A before larger memory or Cinebench. S165 USB-C stateless candidate remains built, untested. No Cinebench score yet.

### 03:11 KST — 12.93 GB Windows RAM; actual Cinebench trial started

- S161 upload-control job3 cleanup completed; C free5,314,715,648B. Job4 graceful reboot finished, TTY60712 ended/portsfree.
- S167 run `memory-s167-download-p12-20260910-025625.HdLLoE`, TTY39024 (ended): job1 Windows RAM8,639,746,048B/8cores; job2 CPU+SSD+USB-A/root Code0 pass; job3 officialCinebench ZIP783,373,346B delivered and full SHA matched. Jobs4..7 uploaded native MEMTEST.EXE, SHA d632f73fde4294ad7118aa2d4c91f606e0d0537a09020f3d1acff286661ebaab. Job8 full4096MiB write/read3passes passed6668ms. Job9 extracted Cinebench2026 successfully; job10 graceful reboot. `memory-s167/hardware-layout.json` and `memory-s171-test/s167-4g-result.json` saved.
- Main implemented S171 memory tester after a SECOND distinct Opus4.8 API safeguard refusal (memory-test request), no model fallback/reword retry. Actual pattern header tested against2000 independent Python vectors+13arg cases; nativeARM64 PE/kernel32-only imports. Private VirtualAlloc test, physicalPFNs notidentified,600s budget,availRAM+1GiBguard. All memory words compared, not only sampled checksums.
- Built S172 candidate: same S161/S167 logic, high keep extent8GiB instead4GiB, pluslow4GiBwindow. SHA e55e257efb7efec44c3968d4bd73c0aff009e75cc9228d34b9f30a68303cc8c7,32342016B. UEFI source restored; no active source-writing agents.
- CURRENT run `memory-s172-download-p12-20260910-030718.LDYXny`, TTY62642. S163gap50HV, S172RAM,lateP12callback. LayoutcheckerPASS backingAE0FCC000,339tables,692792PTEs. Job1 Windows13? Exact **12,934,713,344 bytes**,12,631,556KiB,8cores/8logical. Job2 full8192MiB write/read3passes passed19120ms. `memory-s171-test/s172-8g-result.json`. Job3 removed only duplicate verifiedCinebenchZIP aftercheckingarchiveandinstalledEXEhash; hostoriginalretained. C free2,498,879,488B. Job4 CPU+SSD+USB-A/rootCode0PASS.
- CURRENT job5 first official ARM64 Cinebench2026CPU-only trial started with12GiBguard and defaulttestduration. Commandofficial `start /b /wait "parentconsole" Cinebench.exe g_CinebenchCpuXTest=true`. InitialoutputtotalKB12631556/freeKB11054380. **No score or renderer progress yet.** Do not reboot/interrupt a live benchmark prematurely.
- OfficialCLI source https://www.maxon.net/en/tech-info-cinebench ; extractedCinebench.exe SHA e7b15c3b032950b7b074d223dc43acbc7394b2c1db5e33919b5670bed3edb943.
- Opus4.8 reviewed S170integration, MMIO/ADT reachabilityplausible. Maincorrectedreviewfalseconfounding/concurrentagent/outer-vs-innerclaims in`cpufreq-s170/INTEGRATION-REVIEW.md`. S170hardwaretrialstillpending, compareagainstS139baseline, notS172. No permanentMHzclaim.
- Fable5.1 CLI read-only USB-C DART/addressrewrite review nowrunning(session72404); inspectactualmodelUsagewhencomplete. Distinctsubtaskfromrefusedmemorytest. AsahiupstreamDWC3driverdocuments reconnectPHY/PDlifecycle; saved`usb-s165/ASAHI-REFERENCE-20260910.md`. No claimUSB-Chotplugfixed.

### 03:43 KST — benchmark execution diagnosis, USB-C candidates ready

- S172 job5 Cinebench produced only its start line for >30 minutes, no score or completion. Worker is synchronous SYSTEM/session0, so a GUI wait is possible but not established. At03:41 host interrupt entered HV shell; this ends any valid timing interval. Requested controlled host hard restart at03:43 because worker could not accept a graceful reboot command while its child was pending. No completion/score claimed.
- S176 user-session launcher prepared and native source tested plain+ASan/UBSan. Fixed existing-console-user benchmark launch returns immediately, allowing independent process/log observations. No new scheduled tasks/password changes. S172 payload/CPU/SSD settings unchanged in next launcher.
- S174 USB-C-visible control built with same S172 RAM layout. S175 Fable5.1 helper integrated into isolated candidate: DART1 SID1 translation retains DAPF bit, validates backing and halted controller, removes D83 pointer rewrite, bounded rollback; native helper11groups and integration56cases passed. Neither hardware tested yet. Source restored; named binaries hash-pinned.

### 03:51 KST — Cinebench startup blocker established, certificate/time corrected

- Correction: previous restart entry was03:41, not03:43. First S176 launch at03:41:52failedonlybecauseCDCstillreenumerating; second03:42:09startedwithoutanotherreboot.
- Current S172 samehardware run `memory-s172-usercb-20260910-034209.BNBQFN`, TTY35881. USERCB launchedcmd6580/session1; Cinebench5060 CPUstuck3.875s, userobserverreportedApplicationMessage. Separate USERREAD fixed-path native launcher used sameuserSession to enumeratewindowtextwithoutfocus/input. Actualmessage: missingDigicertroot/brokenlibraries. SYSTEMwindowenumerationcouldnotseeuserwindows; expectedsessionisolation.
- Signatureinspectionjob10: UnknownError/PartialChain/NotTimeValid; guestUTC2022-05-07,Maxoncertvalid2024-07-29..2027-08-16. Job11setcurrentUTC; jobs12/13installedofficialDigiCertTrustedRootG4toLocalMachineRoot andcode-signingICAtoCA. DERfingerprints verified againstDigiCertpublishedSHA256 beforetransfer andagainonMini. FilesonlyonMini,MacBooktruststoreuntouched.
- Job14: Authenticode **Valid**, X509ChainTrue. Sources and DERs in`bench-session-s176/certificates`. No signature-verification bypass, no certificate-expiration override.
- Job15stoprefusedbecauseSYSTEMcannotreaduserwindowtitle; noprocesskilled. Job17usedknownPID/path/session+CPU<=10guardtoenddiagnosedCinebench only. Oldlogsretained. V2observerpreparedwithsamplingexit-racehandling andimmediatestartrecord. No Cinebenchscoreyet.
- Opus4.8 reviewedS176ABI:correct. Its missingobserver/Windowswcharflagclaims correctedinOPUS-REVIEW-ASSESSMENT.md. Fable5.1S175reviewclaimaboutmissingallhighRAMignoredcurrentUEFIWIDE-DART; correctionreviewpending. CRCRreadbackconcernstillbeingchecked. NoUSBhardwaretestyet.
- 03:55: job17 Stop-Process has not completed after several minutes. It omitted -Force for another user's process; confirmation waiting is plausible, not confirmed. HV read-only snapshot showed CPU0 in EL0, NVMeerrors0/armed/notfaulted; does not establish Windows globally frozen. Diagnostic pause/resume occurred03:52. Next preparedstopuses-Force -Confirm:$false. Current benchmark was still at independently observed trust-error dialog beforestop, so no rendering result invalidated. Worker robustness timeout/NULstdin task dispatched toOpus4.8offline; noactiveworkerreplacement.

### 04:03 KST — C disk-full blocked benchmark and command scratch; rescue prepared

- FreshS176run `memory-s172-usercb-20260910-035427.2fSdXl`,TTY7175: job1UTCresetagainfrom2022; officialcertificatespersisted/AuthenticodeValid. Job2V2userlaunchcmd6008,CB3996/session1. CPU82.98s/working3.1GB at30s,83.125s/2.55GBat61s; titleCinebench2026.1.3; stdoutBEFORERENDERING0.470. **No score, no successful render conclusion.**
- Job5 failed nativeworker scriptWriteFile with **ERROR_DISK_FULL112**. MinimalCinebenchstopjob6 also112. ThuscommandchannelcannotwriteC:\Windows\Temp\NWOAS123.CMD. Earlierstopjob17hangmayalsoinvolvedspacepressure;confirmationhypothesisnotproven.
- Ctestpartitiononly24.77GB, lastfree2.499GB; exactconsumer(paging/cache/other)pendingfilesysteminspection. No claimRAM/CPUwasfrozen. A USB-first WinPErecoverycandidateS179built fromsameS172RAM/8core/USB-A withonlybootorderchanged. SHAadea72ef09592aa55b74e5b9bb1f748500469c61c5744b62027baaa37bb93ebf,32342016B. Source restored. Recoverylauncherprepared; no automatic installation task. WINARM2lastdocumentedonMini; physicalpresencewillbedeterminedbybootlogs.
- Third distinctOpus4.8API safeguardrefusal onworkerrobustnessrequest; no fallback/rewordretry. MainwillhandlelimitedRAMscratch/NULstdinimprovement. Fable5.1independentRTCread-onlyresearchrunning. No completedworker-s177artifactfromOpus.

### 04:26 KST — disk space recovered; S177 staged, S180 next

- S179 USB-first candidate fell through to installed WinOS, not WinPE. Current log rescue-s179-20260910-040432.mbjjDF, TTY90787. No proof of USB physical absence.
- CompactOS always completed successfully: 41,754 files, logical8,222,001,111B -> stored4,386,338,327B. Then supported DISM ReservedStorageState Disabled reclaimed6,439,010,304B reserve; C free11,264,270,336B. No files deleted. MacBook data/trust store untouched. C remains24.77GB, not requested128GB partition.
- Job14 postcompact CPU8thread PASS111249us/checksum14964600543233568963; disk64MiB PASS116729us/checksum2755686512017749371. CompactOS changes storage representation, so not a pure NVMe comparison. ntoskrnl SHA7c357da8f62a93bd71e2f4c79eb8cedb396f61d78144def95c2eb4db435904b0; Cinebench SHAe7b15c3b032950b7b074d223dc43acbc7394b2c1db5e33919b5670bed3edb943. Job14exit0.
- S177 nativeworker RAMscratch+NULstdin built/tested7scenarios plain+ASan/UBSan. NWOS177.EXE staged with verified SHA14a1c914cd54f122a6899d3ae784c290bea641dadc4378883a150abc80db1b7f. Existing NWOAS Agent boot task action updated, priorXML andoldEXE retained. Hardware startup untested. No timeout logic added.
- S180 highkeep10GiB pluslow4GiB candidate built SHA9d4664041dcf048a0ffb5688f2d157b1aff2cc8377633d9ce423a6a5846d7a65; MEM180 up to10GiB privateRAM test built/tested. NativeRAMtotal unmeasured.
- Fable5.1 RTC research completed. Main bounded100ms-per-read SPMI probe, exactT8103ADT guards and3readtransactions tested; no PMU writes/guestADT mutation. Added to nextS180boot for evidenceonly. ActualPMUd002/CLKM equivalence unproven.
- Job15 graceful reboot queued forS180. No Cinebenchscore yet; certified trust/time fix and diskpressure recovery address observed blockers.

### 04:32 KST — 15.08GB Windows RAM, full10GiB validation, actual rendering

- S180 live log memory-s180-20260910-042559.VzdkML, TTY65480. Job1 proves S177 installed startupworker works, RAMscratch chosen successfully; Windows15,082,192,896B/8logical, C free9.3GB. Job2 full10GiB allwordwrite/read3passes PASS25,030ms, CPU8threads PASS82775us, SSDsample PASS291323us. HardwarelayoutcheckerPASSall4backingstagesAE0FCC000,691827identityPTEs. PhysicalbufferPFNs notidentified.
- Read-only S178probe completed6.8ms, PMUcounteradvanced150ticks, but LinuxCLKMformula withdirectPMUd002+offset gives2031epoch1928472640, ~139,490,675s aheadofhost. NotavalidatedRTCsource; noADTseedapplied. Saved rawhardwareevidence. NeedcompareSMCCLKMbeforeusingcalendarvalue. Fable5.1 CLI offlinebootseedUEFIlibrarycandidate workrunning, nohardwareaccess.
- Job3 currentUTCset, AuthenticodeValid; priorV2logarchived. CinebenchPID388/session1 launched19:28:41UTC. At152s CPU1023.5s and10.14GBworking, log Rendering/Preparation33979.8ms. **Actual render progressing, no scoreyet.** C free5.979GB. NoHVpauses/restartduringtrial.
- S182USB-Ccandidate adds pre-RunCRCRwritelatch(validationgapinS175readbackzero),263plain+ASan/UBSanboundarychecks and56actualmappingCchecksPASS. SHA96da22db21c78f309d7faff2eb62ff50e12976e0eb4b67a635cc9697a4a43e78. Hardwareuntested, retainsS174payload12.93GBcontrol; nochangeactiveliveS180. Source restored, genericbuildimageS182.

### 04:43 KST — first completed official Windows ARM64 Cinebench score

- **Cinebench2026.1.3 CPUX score1647.353 (display1647.35), one render728441.1ms, defaultminimum600000ms.** Wrapper801.25s; started19:28:41UTC, completed~19:42:02UTC; observed19:43:05UTC. S18015.08GB/8cores/S16350us/lateP12. No benchmark-timeHVpause/reboot. Read-onlyNWOSobservationsroughly61sapartaddoverhead.
- Fulloutputjob18 identifiesARM binary,8threads, officialbuild1659ae2c9f3b_2626460137,MicrosoftBasicRenderDriver. MaxonassetserverDNS11001warningoccurredwithoutnetwork; CPUrendercompletedandemittedscore. ProcessgoneandmemoryAvailable12,066MiB afterexit.
- OriginalstdoutguestSHAd00d88943d9b0b1cebfe2ca43fb597da94e06a850ec083408bcbd1db0a400a9c;stderrSHAempty;evidence+metadata savedmemory-s180/cinebench-result.json andcinebench-evidence.txt. WrapperExitCodeblank, so no capturednativeexit0claim. macOSsameMini/sameversionbaselineunmeasured; no parityclaim.
- S183OpenBSDprimarysource confirmsdirectPMUcounter32.16, offset33.15. CorrectrawconversionmatcheshostUTCwithin2s. FableS181bootseedUEFIcorecompleted183checks+calendarcrosschecks. FableS184directSPMIChelper nowinprogress. OpusS186read-onlyperfmeasurementreviewinprogress, mayseeearlierongoingrenderquestion.

### 04:48 KST — RTC schema guard corrected, no unsafe read

- S183live log rtc-s183-20260910-044615.524WJj,TTY97158. SMCCLKMreadresponse arrived, but bufferreadrefusedbyincorrectSRAMguard; quiescecompletedandWindowsworkerconnected. MainhadconfusedLinuxSMCreg[1]withADTsmcreg[1]. Linux t8103.dtsi explicitlySRAM0x23fe00000/1MiB matchesADT iop-smc-nub region-base/size, notADTreg[1]0x23e050000. Correctedguardandrecordpointerbeforevalidation; actualsharedmemorynotreadfirsttrial.
- S180job19confirmedKoreaStandardTimeandRealTimeIsUniversalabsent. Preparedbackup+UTCclockpolicycmd fornativeRTCcandidate, notexecutedyet.
- OpusS186fourthdistinctAPIcyberfilterrefusal, noanalysisreturned/nofallbackretry. MaincheckedactualcompletedCBscore, earlierpossiblepaging/deadlockhypothesesarenotestablishedfailures. Fable184independentSPMIhelperongoing.

### 04:58 KST — native UEFI RTC first boot passed, USB-C controls prepared

- CorrectedS183run rtc-s183-20260910-044937.XU6Z2W (TTY68126ended): CLKMfa437e3d2804,directPMU5e88fc7a5008,offsete370ae9e2831; directminus2*CLKM106ticks, expectedsequentialreadlatency. Epoch1788983385/UTC+2.21s vs host. SharedSMCSRAM0x23fe50000 validatedwithin0x23fe00000+1MiB. Managementquiescecomplete. NoPMUoffsetwrites/noADTseed.
- PreviousS183job2 exportedtimezonekey toC:\ProgramData\NWOAS\RTC-timezone-before.reg then setRealTimeIsUniversalDWORD1; KoreaStandardTimezoneunchanged.
- FableS184helper429checks; mainshared100msdeadlinefixandlate-readytestadded,449plain+ASanUBSancheckspassed.
- S185 nativeUEFI directSERAbootread+CNTPCTRuntime librarybuiltSHAfa8711f5446f3f135cda50648fe3121a6dcee6e82103f458033263bb5d38da83. No hostRTCmodule inlauncher andnoSetDatejob. Live rtc-s185-20260910-045432.SyWFYl/TTY68979. Job1 UTC2026-09-09T19:55:33.3298216Z, hostloglastwrite19:55:32.393328Z (not synchronizedsample; offset+0.936s afterlaterWMIoutput). AuthenticodeValid;RAM15,082,229,760B/8cores. Job2 CPU8threads95800us+SSD126957usPASS; hashesunchanged. FirstGetTimehardwaretestpassed. SetTime/wakeupstillunsupported,100ppmAccuracyfieldnominalunqualified, notfullRTCdriver.
- S187USB-CvisiblecontrolpayloadpreparedSHA0064df925bdccd4771cc79c4d5d570ac832dab7c1dc6e6c3cb8b8df0c44eef70,32342016B. SameS185RAM/RTC/8cores, XHC1visible; oneDXEdiagnosticupgradedtoenabledDEBUG_ERRORlevelwithHVLOGprefix. Threecontrol/translate/CRCRlaunchers useS163/S175/S182HVrespectively, samepayloadandS180RAMworkerimage. NativeRTCsecondbootandUSBtrialnext. FableS185read-onlyadapterreviewrunning.

### 05:10 KST — USB controller controls and native P12 candidate

- S187 control three jobs passed; S175 samepayload translate run setupOK TCR1100->1080, worker2jobs passed. Crucial PnP parent evidence places BOTH trackpad05ac0324 and keyboard3434d030 under XHC0/USB-A, while XHC1 has only its root hub. Thus empty USB-C rings do not establish a DMA failure or success. MI00 trackpadCode10 belongs to USB-A path. No physical hotplug/input claim. Logs usb-s187-control-20260910-045957.aLelRo and usb-s187-translate-20260910-050757.UwMq91.
- S185 Fable integration review358+449+182plain/sanitizerchecks PASS, no blocking defect. Independent S188 GetTime unseeded status conformance task running with Fable5.1, no hardware/source overlap.
- S189 native ReadyToBootP12 on S187RAM/RTC/USB-Ccontrol builtSHA3b66d5d858bb93f07772f7ac0cf185329ea2daf05438b888d076ed8f3e6836b2,32342016B. UEFI source restored. Launcher removes latehostP12write and adds read-only postWindows cluster snapshot. Hardwaretestpending.

### 05:20 KST — native P12 hardware pass, repeat benchmark underway

- S189 run cpufreq-s189-native-20260910-051312.0p23NQ: ReadyToBoot status0 current12/target12/protected1; read-only firstWindows callback confirmsCMD0x4000010c10c STATUScc. LatehostP12write absent, initialS140cpufreq_init stillpresent. FiveperCPU runs/40checksumsPASS, Pcoremedian~59042..59055us, E~85..87ms. NativeP12firstbootproven, notdynamicpowerdriver.
- S191 officialCinebenchrepeat started20:15:16UTC, userSession1. V3CMDwrapperusesofficialstart /b /wait andcapturesERRORLEVEL separately; oldwrapperbackedup. NativeP12/RTC/S187USB-Cvisiblecontrol/15.08GB/8cores. Lowfrequencyread-only120sobserver, noHVpause/rebootduringrender. No scoreyet.
- S192 conformantRTCunseededDEVICE_ERROR plusS189P12builtSHAb67313c48e057e3c1e608521b69a611e129328c04554785e429f4a4211d4c7c9; actualouter+innercandidatefilescapturedforpublication. Hardwarepending. FableS190HDMIwait initialcallbackdeadlinegapreturnedforcorrection; FableS193CPUinitlineagereviewrunning.

### 05:48 KST — second official score and removal of host CPU initialization

- S191 rendering finished but CMD wrapper lost stdout and exit code; this run is not an accepted score. V4 uses an owned .NET Process with redirected asynchronous output, tested with native exit 19 before deployment.
- S192 live run firmware-s192-native-20260910-052906.cYExcF passed Windows startup, native PMU RTC, ReadyToBoot P12, and first bounded HPD helper integration (ready immediately; delayed HPD recovery not yet demonstrated). Cinebench S196 CPUX returned **1905.281 points**, render 629828.2 ms, default 600000 ms minimum, native exit 0, no timeout. ARM64/8 threads, 15,082,278,912 B RAM. Start 20:31:45 UTC, end 20:42:43 UTC. Curated evidence and hashes: nwoas_scripts/bench-s196/result.json and cinebench-evidence.txt. +15.657% versus S180 single sample; wrapper and firmware changed, so causal attribution is not isolated. Matching macOS baseline remains unmeasured.
- S193 removes initial S140 host cpufreq_init from exact S189 control. Run cpufreq-s193-nohost-20260910-054545.sKqH3f booted Windows successfully: guest m1n1 established P7, UEFI selected P12, host read-only snapshot confirmed P12/E5. Five repeated per-CPU runs (40 correct checksums) passed, P-core median 59038–59055 us. No initial or late host CPU-state write. EL2 mediation, other host setup and storage transport remain.
- Claude Fable S197 rebuilt guest m1n1 from tracked bddf7f06; two repeated builds byte-identical. Head and compat candidates assembled with pinned DTB and S192 FD, each 33177600 B. Hardware not tested. Larger prefix and guest-visible initialization differences are recorded in loader-s197/README.md; no replacement of the stable candidate yet.
- S194 25us EOI-gap candidate built and offline-tested, not booted. S195 256MiB all-word SSD write/read tool undergoing wrapper/deadline review, not deployed. No hardware restart while S196 rendered. No scheduled automation enabled.

### 06:12 KST — integration, SSD integrity and source reproducibility

- S193 five-minute continuous load completed 3,219 CPU checks and 201 unbuffered 64MiB read checks, all correct (300029 ms). S195 dedicated CREATE_NEW 256MiB write-through/readback verified every one of 33,554,432 words. File SHA256 eb11897202f621134a7ec3c737c4ac3fbeee61ef69faa50d4b9469d8998538d6; renamed to IO195-s193-gap50.DAT without deletion. S195 artifact effd43ff105cb5d4ede2d72a9f415eac02557012f0b419254688f53f47fa2bb8,10752B; actual Win32 wrapper mock passed plain and ASan/UBSan. Main corrected backwards QPC to fail closed.
- S200 integrates S192 native RTC/P12/HPD with S193 host-init omission. Run firmware-s200-nohost-20260910-055702.XlGKN7: Windows 15,082,278,912B (15.08GB, about14.05GiB),8cores; CPU/read tests and256MiB full integrity passed. S200 file hash matches S193; archived IO195-s200-gap50.DAT. No initial/late host CPU writes. Still EL2/host transport dependent.
- S202 changes ONLY S200 HV interrupt spacing50us->25us using S194 binary3801b716...5b06. Run nvme-s202-gap25-nohost-20260910-060000.yOxl9b: Windows startup and256MiB integrity passed. Five-minute read loop2524samples300042ms allchecksums correct. Job3 thirty-minute soak now running; do not restart or claim long-soak pass yet.
- Fable S197/S199 built replacement guest m1n1 from pinned tracked source twice identically and fixed vendor verification to compare content/mode/type, not filenames alone. S203compat/S206head launchers prepared, hardware untested. S199 found FDF image_size misencoded256.875MiB instead of30MiB; S204 source-built correction in progress, separate from the live storage comparison.
- Fable S205 reconstructed the stable S16350us HV from an empty build directory twice. Both binaries exactly match bd8f16f286c8d1141df4a17d9b85eafdd4c2d39d5680c6ed75b63429f061d166. Companion source audit49checks passed; source export includes final nested UEFI overrides. Original dirty guest prefix still unreproducible until substitute qualification. Explicit source/evidence snapshot prepared for GitHub; raw logs/device transcripts and proprietary binaries excluded.

### 07:20 KST — completed25us soak, source-loader collision isolated, corrected candidates built

- S202 gap25 completed2524 continuous64MiB reads in300042ms and31 mixed samples spanning1807.03s, all checksums correct. Read medians110878.5us(active)/111582us(mixed). Full256MiB file hash matched the expected pattern and was archived. No failure during that run. S214 currently tests the diagnostic-corrected S208 gap50 with the original S192 payload:2219 active reads/300073ms passed; its30min job3 is still running (23samples as of07:20). No timing query or restart during either soak.
- S203 source-built S197compat prefix booted Windows once and passed8cores/15.08GB/P12/256MiB integrity. S20430MiB header and S211original header then both failed in PrePi atPCffffffffffffffff/ESR8a000000 (PC alignment fault). Both placed the FD at0x840000000, overlapping fixedBootArgs0x840000000/ADT0x840004000. Header-size-only attribution is rejected. Correct old header units:0x100e0000=269352960B=256.875MiB; excess over30MiB is226.875MiB.
- Main built S215 dedicated source-prefix placement guard (196608 compiled/sanitized range cases) and S21630MiB-header UEFI with pre-copy FD/source checks plus permanent handoff HOB reservation (1048576 size cases and actual split-helper tests). Both reproduce byte-identically; S216 payload0ae1cb75bfc3cbc2b7b15e6408c489fe5badd3d12d53fc4353a85b6b5ad3d197. FableCLI source review found no blocking defect. Hardware is still pending; do not adopt these as qualified yet.
- S208 one-line EOI-PC diagnostic fix readsarchitecturalELR; it is not a watchdog fix. Main corrected harnessGICfieldconstants and reran plain/UBSan/ASan successfully. S209 pureC NVMe control model passes10000 differentialoperations, native sanitizer tests, freestandingARM64 compile, and partial control-trace replay267reads/48writes. Admin/DMA/NS2 are not implemented or integrated; no hostdependency removed byS209.
- Source/evidence checkpoint93ecb11 was pushed to cornch-k/NWOAS main. New S217 companion export preserves executablemode and matches candidate buildinputs; hardwarestatus remainspending. Live work continues until09:00KST without scheduling. S218 officialbenchmark repeat prepared with unique output files, notlaunched.

## 07:33 KST — S214 control qualified; S215 placement trial boots

S214 diagnostic-corrected S208 gap50 HV with original S192 payload passed
2,219 continuous reads / 300.073 s and 31 checksum-correct CPU/read samples
over 1,807.56 s. EOI and injection counts matched 2,333,497 at the post-test
query; last EOI PC now comes from the architectural ELR. This is diagnostic
accuracy, not a demonstrated watchdog cure. IO195 was hash-verified and
archived, then Windows rebooted normally.

S215 guarded source prefix + original S192 FD has booted 8-core Windows,
15,082,278,912 bytes RAM, native P12 without host CPU writes. CPU/read and
256MiB complete write/read passed; the five-minute read test is still active.
S216 combined corrected header and permanent UEFI handoff reservation remains
hardware-pending. No standalone boot or native Windows driver is claimed.

## 07:41 KST — S216 combined handoff fix booted and memory-tested

S215 passed2,214 continuous reads/300.038s. After hash-confirmed retirement
of four owned256MiB old test files, Windows reported5,232,082,944 free bytes.
S216 payload0ae1cb75…d197 then booted8-core Windows with15,081,889,792B RAM
and nativeP12. 256MiB complete integrity passed;10GiB memory tested every
word on each of3passes in27.907s, followed by CPU/read checks. UEFI emitted
`HVLOG: S216 reserved handoff base=840000000 bytes=60000`, proving the384KiB
reservation branch executed. This fixes the observed pre-Windows placement
collision; it does not establish the cause of earlier Windows watchdogs.
Repeat boot/long soak remain pending. S218 benchmark has not been launched.

## 08:01 KST — repeated S216 boot and complete source-input reconstruction

The second S216 boot passed8-core Windows/nativeP12/full256MiB integrity and
2,309 continuous reads over300.061s. The uninterrupted30min soak is active.
No hardware configuration was changed during that test.

S221 recovered a readable DTS from the existing static J274 FDT and DTC1.8.1
reconstructed the exact64KiB `ecc93b24…190f76` artifact. The entire S216 build
pipeline was then rerun using that DTS instead of the pre-existing DTB file,
and reproduced payload`0ae1cb75…d197`; tracked sources restored successfully.
This resolves the local opaque-input requirement, while the exact historical
upstream DT source revision remains unconfirmed. Compared Asahi revisions
differ; recovered source attribution/provenance limitations remain explicit.
Claude reviewed the source-only reconstruction and found no machine-specific
identity beyond template placeholders. No raw machine ADT/NVRAM is published.

## 08:20 KST — S216 qualified; S218 Cinebench running

Second S216 boot passed the complete30-minute mixed test:31 valid samples
over1,807.61s, acknowledged exit0. CPU median106,838us and logical64MiB-read
median128,434us are integrity-test timings, not isolated raw-SSD performance.
No initial or late host CPU-state writes were used. EOI/inject counts matched
2,224,361 at the post-soak read-only query; actual architectural EOI PC captured.

The repeated256MiB test file was hash-verified and archived. S218 uploads
(job5–8) and guarded launcher installation/job9 all acknowledged exit0.
Official ARM64 Cinebench CPUX started23:19:09UTC /08:19:09KST, PID3500 in
user session1, with its unique output files. Native completion/score pending.
No further device probes, firmware changes, reboots or host builds during it.

## 08:34 KST — S218 benchmark PASS; S223 packaging correction trial

S218 official ARM64 CPUX completed1888.454 points, render635,440.5ms,
minimum600,000ms, native exit0, no timeout, wrapper663.500s. This is0.883%
below S1961905.281, a single-sample difference across changed configurations,
not evidence of an improvement/regression cause or same-Mini macOS parity.
S218 usedS216/S208gap50 with no initial/late host CPU-state writes. Curated
output and SHA256 are in bench-s218; the raw unique OS ID was not published.

Further source review found S216 supplies an FD480KiB shorter than its Image
copy size. load_raw puts later allocations directly after it; inline guest
copy can therefore consume those neighbouring bytes. S223 appends explicit
0xff tail bytes following FDF erase polarity, changing no existing payload
byte. Claude confirmed the source issue; no watchdog causality is claimed.
S223 payload2343f7fc…8b5e7 was built identically twice. Hardware trial started
08:34:26, after S218 collection and normal Windows reboot. Qualification pending.

## 08:57 KST — S223 bounded qualification complete

S223 integration passed with eight cores and 15,081,889,792 bytes RAM. Its 10 GiB memory test passed three full passes. Mixed CPU/read validation passed 16 samples over 903.888258 seconds. Active reads passed 2247 iterations over 300074 ms (median 126125 us, maximum 974580 us). The large read latency outlier remains unexplained. No S223 Cinebench was run; 1888.454 belongs to S216. Final inventory is queued, without reboot.

The deterministic 480 KiB FD tail supplies the complete inline copy span while preserving all existing executable bytes. No historical watchdog cause is claimed. Source rebuilds, evidence, Claude Code reviews and current/fallback instructions are included in the public delta. Host-independent native boot and Windows drivers remain incomplete.

Final S222 inventory exited 0: eight cores, 15,081,889,792 bytes RAM, C: free 4,416,102,400 bytes; subsequent CPU/read checks passed. Device enumeration still reports one USB Input Device with code 10 and two unnamed code-28 entries. No present network adapter was listed. This does not prove pointer operation or hotplug support. IO195.DAT was hash-verified and renamed IO195-s223-tail.DAT. No final reboot was issued.

## 09:18–10:18 KST extension — work in progress

The user requested one additional live hour. No scheduler was created. S224
adds a target-local read projection for the existing synthetic PCI/NVMe
controller. It preserves the Python admin/control writer and adds a synchronized
snapshot publication after its writes. First Windows integration, 10 GiB x3
memory, and five-minute active reads (2253 iterations) passed. Target counters
showed 209 PCI and 93 register reads handled locally; no sampled Python read
lines appeared in this candidate boot. This is partial host-dependency removal,
not a native Windows driver, standalone boot or proven overall speedup.

S225 response construction and S226 admin-command state are separate uninstalled
C foundations, with differential, sanitizer and freestanding ARM64 checks.
Claude Code reviewed S224/S225; confirmations and contract/test corrections are
recorded beside each component. The S224 30-minute mixed soak is still active
at this checkpoint; final results and reboot validation will follow below.

## 10:12 KST — S224 clean reboot and persistence PASS

The 30-minute test completed with exit0: 31 correct CPU/read samples spanning
1807.494624 seconds. First-boot projection counters ended at 209 PCI reads,
99 register reads and 325 publications. Read counter growth is predominantly
boot/status traffic, not a demonstrated bulk-I/O speedup. Normal Windows restart
followed a SHA256-verified rename of the owned 256 MiB test file. The second
S224 boot re-read the identical archive hash, reported 8 cores/15,081,889,792 B,
and passed P12, CPU and read checks. Second-boot five-minute active reads are
underway. Source and binary hash pins are unchanged between these boots.

S225 adds 12,182 response comparisons plus 48,864 commands across 12 policies.
S226 adds 11,714 state comparisons, 241 held AER cases, 32 generic feature
commands and directed plus 100,000 malformed sanitized cases. Claude Code's
S226 findings prompted cache-resync signaling, callback/range coverage and
explicit lifecycle obligations. Both components remain offline-only.

`native-s224/EXECUTION-MODE.md` now explains the vGIC/PSCI EL2 guest arrangement,
ACPI/memory setup and remaining MacBook service dependency. The public README
states this distinction explicitly. No native GPU/network completion, standalone
boot, newer Cinebench result or macOS performance parity is claimed.

## Final S224 extension result

Second-boot active reads passed 2229 iterations over 300093 ms, median 125800 us (logical compressed-file reads, not raw SSD throughput). Final read-only target query returned the S224 capability and zero target I/O error count. Both boot sessions have acknowledged test exit0. The current MacBook runtime remains running and required; no reboot or mutation job remains queued. Source, build and first-boot/second-boot evidence are included. No S224 Cinebench, native GPU/network driver, standalone boot or macOS parity result is claimed.

## 10:19–11:30 KST extension — S227–S230

The user extended this live work until 11:30 KST; no scheduler was created.
S227 bounded admin rings and S228 control/admin/IRQ composition were built,
differential-tested and reviewed with Claude Code Fable5.1. S229 connected
those components to the existing target C S149 ANS data path and booted Windows.
Supported PCI, control and admin requests now stay on the target. The host
Python controller no longer owns those requests. NS2 service/RAM transport,
host boot/DCP setup and EL2 mediation still remain. This is not a standalone
Windows ANS driver, native GPU/network completion or measured overall speedup.

S230 fixes the S229 review's tick-path printf and post-shutdown doorbell
handling. First boot passed 8-core integration, P12, 256 MiB complete-word
write/read verification and 10 GiB memory across three passes. It also verified
the 256 MiB archive saved on S229 before a normal Windows restart.
The S230 ten-minute mixed soak and second-boot qualification are pending at
this checkpoint; final evidence will follow. Current launcher is
`nwoas_scripts/native-s230-admin-guest-test.sh`; host runtime must remain alive.
HV SHA256: `2d763bfb3aa6c55d8fbcd3d8ed90eeb4bb322f177d87ae279c1c53efbe7fa2c1`.
The unchanged S223 payload SHA256 is
`2343f7fc35acd0401bafd63bfdc10d047ad8c4bc78207056c42e478051a8b5e7`.

S224 is the previous bounded-qualified fallback. Earlier paragraphs saying
S225/S226 are uninstalled describe their earlier checkpoint; they are now
linked into S229/S230. No new Cinebench run was made during this extension.

## 11:22 KST — S230 bounded qualification complete

Two S230 Windows boots passed. The first completed 8-core/P12 integration,
256 MiB full-word write/read, 10 GiB memory x3, and 11 mixed CPU/read samples
over602.505147s. Normal Windows restart preserved the exact256MiB archive hash.
The second boot passed2555 consecutive logical64MiB reads over300030ms, median
125558us and maximum1177709us. The maximum remains an unexplained latency
outlier; these results do not prove raw SSD throughput or overall speedup.
Final target I/O error count was0 and CSTS was1. Admin fetch/completion counters
were112/111; a held AER accounts for a possible one-command difference.

Claude Code Fable5.1 reviews of S227–S230 prompted bounded-ring/IRQ lifecycle
corrections, the target tick-log and shutdown fixes, and stronger test coverage.
The final extracted production process/poll/IRQ/link harness passed28 directed
ASan/UBSan cases with physical/proxy/memory callbacks mocked. Source hashes and
review dispositions are recorded. The first soak assessment's default30-minute
completion marker was corrected to the actual10-minute marker; checksums,
exit0 and the600-second duration threshold were unchanged.

Current runtime: `native-s230-admin-20260910-111548.TRiw43.link`; launcher
`nwoas_scripts/native-s230-admin-guest-test.sh`. Keep the MacBook runtime alive.
No pending reboot/mutation remains after acknowledged second-boot checks.
The previous S224 runtime is the fallback, not the current execution.

Windows still sees15,081,889,792B and8cores. USB Input Device code10 and two
unnamed code28 entries remain; VideoController inventory is empty. No new GPU,
network or USB hotplug support is claimed. C: free was3,584,143,360B.
No new Cinebench was run;1888.454 remains the earlier S216 CPU multi-core result.
Target C now owns synthetic PCI/control/admin plus the existing ANS data path;
NS2 transport and host boot/DCP/EL2 dependencies remain. `native-s230/NEXT.md`
records the exact dynamic NS2 sectors and RAM coherence requirements for the
next migration. Earlier status paragraphs are historical checkpoints.
