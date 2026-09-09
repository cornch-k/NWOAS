# S84 Windows 11 Setup repair package

Status: USB-STAGED-AND-VERIFIED / WINPE-NOT-YET-TESTED. The generic hardware-requirement
message has not yet been attributed to a specific check. D83 USB input stays live.

The package writes and reads back five existing experimental LabConfig bypass
values, preserves pre-change Setup logs, checks the actual installation image
path, and offers a separate log collector. It does not select/format a disk,
apply an image, accept the EULA, terminate Setup, or reboot the machine.

Files:

- `setup-repair/autounattend.xml`: ARM64 windowsPE commands applied before the
  installation UI. No DiskConfiguration or ImageInstall directives.
- `SCRIPTS/FIX11.CMD`: MiniNT/ARM64/media-marker guards, before/after registry
  logs, readback validation, and Panther logs. `LABCONFIG_READBACK_PASS` proves
  settings only; it is not proof that Setup's block was resolved.
- `SCRIPTS/WATCH11.CMD`: bounded background capture (40 samples, approximately
  ten minutes) of registry and Setup logs to the marked USB after FIX11.
- `SCRIPTS/COLLECT11.CMD`: capture the persistent failure and read-only disk list
  to the marked USB's `NWOAS-SETUP-LOGS` directory.
- `SCRIPTS/RETRY11.CMD`: optional interactive Setup with explicit `/installfrom`
  pointing at install.swm/wim/esd and the safe answer file. WMI must confirm no
  Setup process exists; it does not force-close an existing installer.
- `stage-setup-repair.py`: checks the exact recorded 15.4 GB WINARM2 external USB
  FAT32 volume, ARM64 boot files, package hashes, and split-image headers/parts.
  Replaced files are backed up. Image/WIM contents are not modified.

Preferred next run:

1. Move WINARM2 to the MacBook, leaving the Mac mini running for now.
2. Inspect actual media configuration, then run
   `python3 nwoas_scripts/stage-setup-repair.py --apply` on the host. Inspect its
   result and safely eject that exact media.
3. Return it to the same Mac mini USB-A port. Use one normal bootstrap and the
   exact D83 harness; this fresh run avoids inheriting cached old answer files.
4. The new answer file applies keys and collects logs automatically. Actual
   success is the Setup UI advancing beyond the hardware-compatibility block.
5. If still blocked, run COLLECT11.CMD in WinPE and retrieve the USB log folder.
   Investigate the recorded blocker before further bypass/firmware changes.

Current live-session FIX11 Back/Next is an alternative only after checking that
the already loaded answer file has no automatic disk/image configuration. The
workspace-root legacy autounattend.xml includes WillWipeDisk=true and is NOT used.

Validation performed on macOS: package hash checks, XML exclusion of automatic
disk/image/account settings, eight wrong-media rejection cases, missing SWM part
and mismatched GUID rejection, and ASCII/CRLF checks. Windows batch/WMI execution
has not been tested. Only commands present in the local WinPE inventory are used;
there is no dependency on PowerShell, findstr, timeout, taskkill, or tasklist.

References:

- Microsoft Setup log locations:
  https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-setup-log-files-and-event-logs?view=windows-11
- Microsoft documents `/InstallFrom` with the first split `.swm`, and `/Unattend`
  for WinPE. `/Compat IgnoreWarning` is not treated as a hard-requirement bypass:
  https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-setup-command-line-options?view=windows-11

No hardware-specific blocker has yet been confirmed. LabConfig is an experimental
compatibility workaround here, not a Microsoft support or installation guarantee.

## Media staging completed 2026-09-07

Actual WINARM2 initially had no root autounattend.xml. Eight final package files
passed SHA256/size readback, with ASCII/CRLF checks for command files. Backups:
NWOAS-S84-BACKUP-20260907-114343 and NWOAS-S84-BACKUP-20260907-114522.
Full wimlib verification passed for sources/boot.wim and the complete two-part
install.swm/install2.swm set (ARM64 ko-KR Windows 11 build 22621.525).
Evidence: logs/setup-s84-staging.json, logs/setup-s84-boot-verify.log, and
logs/setup-s84-install-verify.log. No WIM contents were changed.
Next: return USB to the target before one fresh exact D83 boot. Setup bypass
and automatic script execution are still unverified on the target.

## S85 correction (current local package; not yet on USB)

S84 failed on target with a user-reported ProductKey answer-file error.
Both answers now include the interactive all-zero placeholder Key and
WillShowUI=Always. This follows the interactive branch in
https://github.com/cschneegans/unattend-generator/blob/master/modifier/ProductKey.cs
and the UI setting documented at
https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-setup-userdata-productkey-willshowui
The placeholder does not activate Windows or select an edition.
Host validation PASS; target validation pending. Preserve any USB diagnostic
logs before applying the corrected package with the same guarded staging tool.
