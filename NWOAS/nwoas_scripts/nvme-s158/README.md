# S158 NVMe latency diagnostics

Based on committed S154 source, with unchanged NVMe queue scheduling. S157 USB-C cursor experiment is preserved separately and not included. Adds CNTPCT maximum command latency split NS1 vs NS2, in-flight marker, SQ backlog maximum, CQ acknowledgements; control actions 5/6/7 read diagnostics. On bugcheck, prints two bounded diagnostic lines with CNTFRQ before queue recreation. No periodic logging added.

Image SHA256: 3b76f4a0c9d03603fb985b3808aea1b67594177aa9777b1b0ceec7bb059efb51

8-core USB-A-only launcher: usb-s158-guest-test.sh (matches S151 configuration).
One-core recovery launcher: rescue-s158-guest-test.sh, NWOAS_NVME_FASTPATH=0, 64KiB transfers, S139 Python controller. This avoids old S103 firmware/new host fastpath command incompatibility. Actual boot remains untested until target returns to proxy.

S157 and S151 both hit 0x133. Latest dump export did not finish. Old S138 rescue failed due unsupported fastpath command; host stopped, bounded ! interrupt and NOP failed. Physical power cycle requested; no device commands currently in flight.

## S158 corrected rescue, live 8-core run and dump recovery (23:13 KST)

- Rescue run 225909 reached Recovery. User chose Continue, causing reboot/disconnect.
- Rescue 230139.3R75Tm stopped before Windows: UEFI PC=ffffffffffffffff, misaligned PC exception. Captured logs; hv.reboot() from confirmed TTY shell completed. No disk reinstallation performed.
- Live run usb-s158-20260909-230605.feQGfd, TTY session38486. Modern S158 HV + S139 8-core USB-A payload, fastpath ON. NWOS connected. Jobs1..4 all exit0.
- Job1 recovered latest dump 050722-3109-01.dmp into logs/S158-recovered-050722-3109-01.dmp,237327 bytes,SHA256 55512e267b63a8421167630b1f1d8e34653f2b20d1bc0ad957202cd9b8570797. Bugcheck0x133,p1=0,p2=501,p3=500,p4=fffff80339319338 matches S151 run225143, not S157. Raw stack candidates include storport submission path; not a proven offending DPC.
- Job2 PresentOnly PnP sees FL1100 ACPI\PNP0D10\0 root hub Code0, keyboard VID3434/PIDD030 and Apple trackpad VID05AC/PID0324 MI01/MI02 pointer Code0, MI00 failed start. Both devices' parent chain is this USB-A root hub. USB-C controller is not exposed in this run; do NOT claim USB-C fixed. Actual physical input remains user confirmation pending.
- Job3:8core/8logical, CPU156767us,64MiB read195024us,failed0. Clock metadata still29MHz; memory4273668KiB. Not raw SSD throughput or clock proof.
- Job4 three low-load WMI samples completed; low CPU/DPC and zero disk queue in sampled intervals. Formatted disk latency fields integer0 cannot establish zero latency.
- Brief diagnostic shell pause (Ctrl+C) then cont() succeeded. Live query3=73622 (commands,zero errors),4=4294967299 (armed epoch3),5=96956 NS1max ticks,6=355272 NS2max ticks,7=221014722084865 (backlogmax1,CQack51459). Timer frequency not yet read; no seconds conversion claimed. Measurements do not show rare failure bound.
- Current run preserved, no new bugcheck at observation. Continued stability not established yet.
- Fable CLI wrote dump_extra_map.py; review caught missing16-byte ArchitectureSpecific union and wrong validity marker. Corrected tail offsets to60/70/78 and marker44475254. Actual S154 dump now maps56 extra blocks, KDBG match and watchdog signatureaebecede. Watchdog profile start itself is not in mapped regions. DumpBlob trailer beyond SizeOfDump not decoded. Three behavioral regression tests pass. Initial Fable report of missing blocks superseded; see nvme-s158/S154-extra-map.txt.
- Opus CLI follow-up read-only review running (session52658), prompt/result under claude-s156/opus-s158-*. Main alone controls hardware. Automation remains paused.
