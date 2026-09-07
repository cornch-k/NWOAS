# Mac mini dual boot constraint

User requests preserving macOS and allocating roughly128GB to Windows on the
reported256GB internal SSD. User subsequently explicitly authorized deleting existing Mac mini data
("그거 싹 밀어도 되는데"). Fresh macOS plus Windows dual boot remains the goal. Host MacBook and X31 are not partition targets.

Proposed logical layout, pending actual namespace/GPT and APFS limits:
Apple ISC | macOS APFS | separate custom-OS boot stub | EFI/MSR/Windows NTFS
and Windows recovery as needed | existing Apple system recovery.
Exact sizes and creation sequence are unresolved. Native Apple boot picker
integration needs a separate boot volume; the current experiment has used a
custom boot object associated with Macintosh HD and is not a finished dual-boot
installation. Preserve/recover the normal macOS boot path when separating it.

Do not treat APFS free space or GPT partition size as shrinkable space.
Obtain target macOS/recovery diskutil apfs resizeContainer <verified target>
limits output and volume usage before determining Windows allocation. With
boot/recovery overhead,128GB per OS is not an exact split of256GB hardware.
Deletion/recreation of target Mac mini user/OS volumes is now authorized.
Do not erase host MacBook/X31 or infer permission to destroy ISC/Recovery.

Current step S87 only reads primary namespace GPT metadata after controller
initialization. No partition or filesystem edits. Windows storage driver and
read/write correctness remain prerequisites to any Windows installation.

Reference: https://asahilinux.org/docs/sw/partitioning-cheatsheet/
Reference: https://asahilinux.org/docs/platform/open-os-interop/

## Measured S91 APFS allocation (no partition writes)

Primary namespace251000193024 bytes. APFS container245107195904 bytes.
Latest valid NX checkpoint XID2008561 mapped a checksum-valid spaceman:
allocated210954633216 bytes, free34152562688 bytes. These are container
allocation counters, not user-file size and not the macOS shrink limit.
Approximately100GB must be reclaimable to fit128GB Windows plus its boot
components while preserving macOS. Check snapshots/purgeable space and actual
diskutil shrink limits before selecting or deleting any files. No resize now.

S90 nvme.c fixes enable read-only real ANS2 access with clean shutdown:
remove obsolete PRP register access, NLB zero-based, correct NVMMU TCB flags.
Windows-visible storage implementation remains unfinished.

## Authorization update and concrete capacity budget

Existing Mac mini data may be erased. Retain Tahoe-family firmware and working
Apple recovery. Recreate macOS for dual boot; do not treat permission as a request
to remove macOS permanently. No extra permission request needed for this scope.

Preliminary decimal-byte budget on measured251000193024-byte namespace:
- macOS APFS110000000000
- Windows NTFS128000000000
- Separate custom-OS APFS boot stub4000000000 (verify actual Tahoe stub size)
- Windows EFI512000000, MSR16000000, WinRE1500000000
- Preserve ISC524288000 and Apple Recovery5368664064
- Remaining1079240960 bytes for alignment/reserve; final partition boundaries
  must be computed and checked after recovery inventory and stub requirements.
This is a budget, not executable partition commands or a validated installation.

Execution dependency: the current m1n1 boot object is attached to the existing
Macintosh HD volume group. Erasing it removes the current custom boot route.
First prepare recovery inventory, exact Tahoe reinstall/stub bootstrap assets,
and a Windows storage path. Erase from target Recovery using Apple's supported
volume-group procedure, never raw-GPT overwrite or host disk0 assumptions.
Current host can only issue validated read operations to ANS2; it cannot format
APFS or execute Recovery diskutil through the idle m1n1 proxy. Recovery Terminal
requires physical/UI interaction in this environment. Record that dependency
without asking again for permission to erase.

Sources: https://support.apple.com/en-us/102506
https://support.apple.com/en-us/102655
