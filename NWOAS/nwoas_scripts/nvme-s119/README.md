# S119: robust WinPE source discovery before S118 image apply

S118 report contained only BEGIN/FAIL/END, without NWGUARD output or a target directory
listing. Source-marker discovery failed before target validation, cleanup, or DISM.
The exact reason (late automount vs absent marker) is not yet proven.
S119 waits for both actual SWMs instead of relying on VERIFIED-S117.TXT, logs each attempt,
and records mounted roots and diskpart list output if discovery fails.
The independent partition/NTFS identity guard and both full source hashes remain mandatory.
S118 and S119 started markers both prevent automatic repeated cleanup.
All cleanup and DISM options are otherwise unchanged from S118. NWGUARD binary reused.
HARDWARE-UNVERIFIED until report recovered.
