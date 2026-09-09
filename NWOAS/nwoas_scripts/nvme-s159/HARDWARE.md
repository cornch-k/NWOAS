# S159 hardware comparison — in progress

S158 baseline log usb-s158-20260909-232239.ykRAuz completed jobs1–8. Job7:8cores/8logical, CPU121077us checksum14964600543233568963;64MiB diskread196241us failed0 checksum2755686512017749371;USB-A controller/root Code0. Job8 requested Windows graceful restart and completed. No interactiveHV pauses were used in that baseline run.

S159 candidate f1375e8a... launched twice:
- usb-s159-20260909-234234.b1e4jO: UEFI initialized NVMe queue1depth2, then wrote normalshutdownCC and flush completed; serial disconnected. No Windows NWOS connection or capturedbugcheck.
- usb-s159-20260909-234343.4TDdvC: UEFI queue1depth2 initialized; serial disconnected again before NWOS. No capturedbugcheck. This second run did not show the same normalshutdownlines. Do not claim both were graceful or assert causality from absence of bugcheck.

These are failed boot-to-diagnostics attempts, not successful performance tests. No new partition changes. Candidate is not promoted. A same-toolchain rebuilt S158 baseline92d260c1... is now running through usb-s159-control-test.sh, log usb-s159-control-20260909-234442.cVg1OM. This controls for build differences versus the original S158 binary before assigning regression to submission deferral. Await hardware outcome.

Rebuilt control also disconnected before NWOS. Original S158 usb-s158-20260909-234547.ShA93G booted, hardwarejob1 passed8cores/8threads,CPU90768us,disk200319us,failed0,matchingchecksums,USB-ACode0. Found omitted build flag NWOAS_NVME_MAX_BLOCKS=256: new objects limited to16blocks despite1MiB hostadvertisement. Disassembly confirms corrected256block nvme.o equals original. Earlier attempts are invalid scheduling comparisons. Corrected candidate SHA1acd193d... prepared and oldbaselinejob2 queued graceful restart.

Corrected256blockS159 run234847.C3i6zw booted and job1plus3repeats passed8coreCPU and64MiBchecksums;disk388016/434688/392245us vsoriginalS158196241/200319us. Job3idlecheck failed beforesecond sample:0x133p1=0,p2=501,p3=500,p4=fffff801dd519338,cpu0. CapturedS158latencyfreq24000000,NS1max95686ticks(3.9869ms),NS2max319852(13.3272ms),NS1calls35245,NS2calls233,backlogmax59,CQacks34026,SQ41/41,CQpending12,noexecutecurrentlyactive. LR0timer18,LR1pending900,LR2pending698. No interactiveHVpauseorDCPinterventioninthisrun. Do not infer whichDPCisresponsiblefrompendingIRQs. AfterbugcheckWindowscreateddepth64queues,thenflushedandreset;newminidumpmayexist. CandidateNOTPROMOTED:slowerandwatchdogpersists. RecoveroriginalS158andexportlatestdump.
