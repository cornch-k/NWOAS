# NWOAS status 2026-11-15 (S231)

## Result
Windows 11 Pro 25H2 (10.0.26200.8037, ARM64) was installed onto the Mac mini internal SSD on the new 80 GB
layout and reached the desktop (user-confirmed), booted through m1n1 EL2 S231 with the MacBook host attached.
This is NOT standalone boot and NOT hypervisor-free Windows.

| Item | Value |
|---|---|
| CPUs seen by Windows | 8 |
| Windows-visible RAM | 14,388 MB |
| Problem devices | ACPI\APPL8900\0, ACPI\APPL0100\0 (code 28) |
| Network | none |
| Remote job channel | "NWOAS Agent" ONSTART task (worker-s177 NWOS.EXE, SYSTEM) via NS2 |

## What changed today
- Project moved to ~/Desktop/DEV/NWOAS; 400 scripts rewritten from /Volumes/X31/NWOAS, 46 sha256 pins refreshed
  (backup path-migration-backup-20261115.tar.gz). .venv-hv rebuilt (Python 3.14; capstone/construct/pyserial).
  Toolchain: Homebrew llvm@20/lld@20 20.1.8 + rustup 1.88.0 reproduce the S230 HV byte-for-byte.
- m1n1 boot registration had been lost (bputil: Reduced, coih absent); D4 re-registered in 1TR by the user.
- User had deleted the old WINTEST. New GPT (nwoas_scripts/layout-s231/manifest.json): ISC | macOS 159.09 GB |
  NWSTUB 4 GB (Basic Data, attrs 0xD000000000000000, reserved for the future dual-boot stub) | ESP 512 MiB |
  MSR 16 MiB | Windows 81.46 GB | RecoveryOS. ESP/MSR/Windows entries written from the host by
  layout-s231/gpt_partition_s231.py (original blocks saved in layout-s231/gpt-plan/before-apply-*).
- S231 HV = S230 with Windows write zone start 39944960 (19-byte binary diff). Launcher
  nwoas_scripts/layout-s231-guest-test.sh (Cinebench delivery module removed). Python guard PROTECTED re-captured.
- Install media WINARM25H2 (Win11 25H2 ko-KR ARM64 ISO, install.swm/install2.swm, autounattend.xml = LINK.XML
  that auto-starts NWAGENT in WinPE).
- Install path: WinPE jobs via NS2 agent (layout-s231/jobs/J1..J15, W1..W3): diskpart format ESP/Windows,
  robocopy /J USB->SSD + SHA256 verify, DISM apply index 2 from SSD, bcdboot, specialize-pass worker staging.

## Known problems found today
- USB-A mass storage: copy/DISM reads from the USB returned corrupt data; sequential unbuffered reads (NWREAD,
  robocopy /J) were correct. See nwoas_scripts/layout-s231/USB-READ-CORRUPTION-20261115.md. Unresolved.
- WinPE 26100 lacks find/findstr/fc; a pipe into a missing command aborts the whole job script.
- A WinPE/Windows restart resets the whole Mac mini; the host launcher must be restarted each time.

## Next (pending user decision on goal scope)
See nwoas_scripts/layout-s231/STANDALONE-CENSUS-20261115.md (12 milestones to MacBook-free dual boot) and
M8-NOTES-20261115.md. The user's notion of "native" = usable for real work / League of Legends, which additionally
needs GPU acceleration (no Windows Apple-GPU driver exists), networking, audio, USB reliability, TPM for Vanguard.
