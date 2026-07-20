@echo off
wpeinit
timeout /t 20 /nobreak >nul 2>&1
(echo list disk & echo list volume) > X:\dp.txt
diskpart /s X:\dp.txt > X:\nwoas-disks.txt 2>&1
for /l %%r in (1,1,8) do (
  for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do @if exist %%d:\ copy /y X:\nwoas-disks.txt %%d:\NWOAS-DISKS.txt >nul 2>&1
  timeout /t 3 /nobreak >nul 2>&1
)
mode con: cols=110 lines=70 >nul 2>&1
cls
echo ==================== NWOAS DISK RECON ====================
type X:\nwoas-disks.txt
echo =========================================================
echo   WOA = the ~931 GB (1000 GB) disk. READ its Disk number.
echo   This screen STAYS (no scrolling). Force power off after.
echo =========================================================
:nwoashold
timeout /t 3600 /nobreak >nul 2>&1
goto nwoashold
