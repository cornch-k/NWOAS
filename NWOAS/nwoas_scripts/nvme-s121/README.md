# S121: fix stale ERRORLEVEL at cleanup marker

S120 verified target, recopied corrupt install2, passed its original hash and EOF/file flush,
passed NTFS volume flush, and passed both full source hashes. Then it wrote the STARTED
marker and stopped BEFORE CLEANUP. The last fsutil reparsepoint query normally returns
nonzero for a non-reparse directory; ECHO does not establish a fresh success errorlevel.
The script incorrectly tested that stale status after writing a valid marker.

S121 verifies actual marker content with FINDSTR and scratch-directory existence.
The S120 marker is retained and does not block this specific continuation because the
recovered S120 report shows no cleanup/DISM started. S118/S119 and own S121 markers still block.
The untouched install1 passed full hashes after reboot in S119 and S120; do not repeat 4GB
hash yet again. Verify its exact length and retain DISM CheckIntegrity/Verify. install2 is
still independently verified (NWTRIM) and flushed, with one recopy if content differs.
All target identity, explicit cleanup scope, space check and apply conditions unchanged.
No ESP/BCD configuration or first boot is done by this script. Hardware execution pending.
