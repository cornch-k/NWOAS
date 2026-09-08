# S128: UEFI access to the trial internal SSD

Hardware verified 2026-09-09 on Mac mini J274, Tahoe firmware retained.

S125's NVMe registration alone did not produce UEFI MMIO. S127 explicitly connects the registered NVMe handle at BDS BeforeConsole. That connection reported success, but the NVMe driver still returned before MMIO: Mu's NonDiscoverable facade sets VendorId=0xffff, while NvmeControllerInit rejects VID or DID 0xffff as an absent controller.

S128 sets VID1234/DID0010 for the NVMe facade only when its first BAR is 0x700100000, matching the existing Python controller. Other non-discoverable devices are unchanged. It retains S127's explicit connection.

Evidence: `../logs/usb-s128-20260909-003243.dPSbr1`, UEFI-stage depth-2 admin and I/O queues, Identify completions and physical GPT/ESP reads before EBS. WinPE reconnect and commands succeeded afterward. This is a firmware interface over the experimental host-assisted backend, not a production native Windows storage driver.

`build_candidate.py` snapshots/restores all source files byte-for-byte and creates a distinct payload. `../usb-s128-guest-test.sh` uses S103 HV, S124 storage, S126 RAM filesystem transport. It retains USB-first boot order for recovery.

S129 differs only by registering Internal Storage before USB in temporary firmware. The installed-OS boot outcome must be checked in the current status file; do not infer it from this build.

Windows preparation:
- `03-create-boot-files.cmd`: verify C and ESP guards, BCDBoot into existing trial ESP, check fallback BOOTAA64.EFI, flush both volumes. No formatting/GPT changes.
- `04-verify-boot-files.cmd`: collect hashes and offline boot state. On the verified run all three 1790304-byte boot manager files matched SHA256 b219c6648564ead2c3ebd3ddf9ba34a2d5f3815996074cd660dabe70a631b303. Hash commands use a zero expectation as a diagnostic sentinel; examine the actual hashes, not their mismatch exit codes.

Microsoft references: [BCDBoot](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/bcdboot-command-line-options-techref-di?view=windows-11), [specialize commands](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-deployment-runsynchronous), [Panther answer file placement](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/update-windows-settings-and-scripts-create-your-own-answer-file-sxs?view=windows-11).
