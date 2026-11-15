@echo off
echo S231 J3 FORMAT ESP P4 FAT32 AND WINDOWS P6 NTFS ON DISK 0 ONLY
(echo select disk 0& echo select partition 4& echo format fs=fat32 quick label=System& echo assign letter=S)>X:\S231F4.TXT
diskpart /s X:\S231F4.TXT
if errorlevel 1 exit /b 41
(echo select disk 0& echo select partition 6& echo format fs=ntfs quick label=Windows)>X:\S231F6.TXT
diskpart /s X:\S231F6.TXT
if errorlevel 1 exit /b 42
(echo select disk 0& echo list volume)>X:\S231V.TXT
diskpart /s X:\S231V.TXT
vol S:
vol C:
dir S:\ /a
dir C:\ /a
echo S231 J3 COMPLETE
exit /b 0
