# S86: Setup core-count threshold experiment

Recovered evidence: ../logs/setup-s86-recovered-20260907-120259/capture-13267-2068.
Five LabConfig DWORD values are 1. Setup passes memory (8192 vs3686 MB) but
VerifyProcessorSupported rejects 1 active core vs2. DiskPart lists only 14 GB
WINARM2: no internal SSD yet. S85 ProductKey interactive correction succeeded.

M1 has eight physical cores. Existing T810XFamilyPkg/AcpiTables/MADT_Static.aslc
marks CPUs1..7 disabled (NWOAS uniprocessor boot). This patch does not enable SMP.

patch_corecheck.py accepts exactly SHA2565368c4f724a56f89c7cb907bc4580ef6884ede01e7d19a3f063e8344b1de7189
of ARM64 winsetup.dll. VerifyProcessorSupported uses GetActiveProcessorCount,
compares w21 with2, logs the minimum and returns cset w0,hs. Three comparisons
and the logged minimum are changed to1. Zero still fails; 1/2/8 pass.
PE checksum is recalculated. Original Authenticode signature is invalidated;
actual WinPE loader acceptance is not yet tested. No global signature checks
are disabled by this patch.

The identical original DLL existed in USB sources and boot.wim index2 sources.
A local boot.wim copy was updated at index2 /sources/winsetup.dll. Full wimlib
verification passed, and extraction confirms the exact patched DLL. Index1
and install.swm/install2.swm are not targeted. Originals remain on host and
USB NWOAS-S86-BACKUP directory. stage.py pins the exact USB UUID, originals,
and patched hashes; writes temporary files, verifies, then renames.

Next: return USB, boot exact D83 once, then verify Setup reports1 vs1 and passes
the previous hardware page. Other blockers may follow. Internal SSD driver
support and actual multiple CPU execution remain separate unresolved work.
