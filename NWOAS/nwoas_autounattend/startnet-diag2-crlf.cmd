@echo off
rem ============================================================
rem NWOAS STAGE3 DIAG v2.1 (2026-07-08, post-adversarial-review)
rem Purpose: determine WHY WinPE sees 0 USB disks.
rem   A1=NO-NODE      -> hypothesis (a): ACPI PNP0D10 not in PnP tree
rem   A1=NODE-EXISTS  -> check USBXHCI Enum + problem code (hypothesis c)
rem Tools verified present in THIS WIM: reg.exe pnputil.exe find.exe
rem   ping.exe mode.com diskpart.exe cmd.exe more.com wpeutil.exe
rem Tools ABSENT (do not use): findstr.exe timeout.exe choice.exe
rem   powershell driverquery setupapi logs
rem v2.1 changes: A1 marker from NON-recursive query (recursive /s can
rem   return nonzero on ACL-denied subkeys even when node exists);
rem   problem-code lines + A1 verdict repeated at BOTTOM of screen.
rem NON-DESTRUCTIVE: read-only queries + diskpart list only.
rem ============================================================

wpeinit

rem --- settle wait ~25s for PnP (timeout.exe absent; ping -n N = N-1 sec) ---
ping -n 26 127.0.0.1 >nul

set OUT=X:\nwoas-diag.txt
echo NWOAS-DIAG-V21-BEGIN > %OUT%

rem ---------- 1. disks baseline ----------
(echo list disk & echo list volume) > X:\dp.txt
echo [Q0-DISKPART] >> %OUT%
diskpart /s X:\dp.txt > X:\dp-out.txt 2>&1
type X:\dp-out.txt >> %OUT%

rem ---------- 2. is the ACPI PNP0D10 node in the PnP tree? ----------
rem full recursive dump (file only; may hit ACL-denied subkeys, that is OK)
echo [Q1-ACPI-PNP0D10-RECURSIVE] >> %OUT%
reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\PNP0D10 /s >> %OUT% 2>&1

rem verdict marker from NON-recursive query (robust vs ACL denials)
reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\PNP0D10 > X:\q1.txt 2>&1
if errorlevel 1 echo NWOAS-A1=NO-NODE > X:\a1.txt
if not errorlevel 1 echo NWOAS-A1=NODE-EXISTS > X:\a1.txt
type X:\a1.txt >> X:\q1.txt
echo [Q1-VERDICT] >> %OUT%
type X:\q1.txt >> %OUT%

rem ---------- 3. did USBXHCI service attach to any device? ----------
echo [Q2-USBXHCI-SERVICE] >> %OUT%
reg query HKLM\SYSTEM\CurrentControlSet\Services\USBXHCI\Enum > X:\q2.txt 2>&1
if errorlevel 1 echo USBXHCI-ENUM-KEY-ABSENT ^(driver never attached^) > X:\q2.txt
reg query HKLM\SYSTEM\CurrentControlSet\Services\USBXHCI /v Start >> X:\q2.txt 2>&1
type X:\q2.txt >> %OUT%

rem ---------- 4. full connected-device dump (file only, big) ----------
echo [Q3-PNPUTIL-CONNECTED] >> %OUT%
pnputil /enum-devices /connected >> %OUT% 2>&1
if errorlevel 1 pnputil /enum-devices >> %OUT% 2>&1

rem ---------- 5. problem devices only (small, screen-worthy) ----------
pnputil /enum-devices /problem > X:\q3prob.txt 2>&1
if errorlevel 1 echo pnputil /problem nonzero: unsupported OR zero problem devices > X:\q3prob.txt
echo [Q4-PNPUTIL-PROBLEM-DEVICES] >> %OUT%
type X:\q3prob.txt >> %OUT%

rem ---------- 6. USB / USBSTOR enumeration presence ----------
echo [Q5-ENUM-USB-USBSTOR] >> %OUT%
reg query HKLM\SYSTEM\CurrentControlSet\Enum\USB >> %OUT% 2>&1
reg query HKLM\SYSTEM\CurrentControlSet\Enum\USBSTOR >> %OUT% 2>&1

rem ---------- 7. what ACPI devices exist at all (top level) ----------
echo [Q6-ENUM-ACPI-TOPLEVEL] >> %OUT%
reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI >> %OUT% 2>&1

echo NWOAS-DIAG-V21-END >> %OUT%

rem ---------- 8. copy out if any FS letter exists ----------
for /l %%r in (1,1,8) do (
  for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do (
    if exist %%d:\ copy /y %OUT% %%d:\NWOAS-DIAG.txt >nul 2>&1
  )
)

rem ---------- 9. screen summary (most important LAST, then hold) ----------
mode con cols=110 lines=68 >nul 2>&1
cls
echo ================= NWOAS DIAG V2.1 ^(photo this screen^) =================
echo ---- disks ----
type X:\dp-out.txt
echo ---- Q1: ACPI PNP0D10 node ^(non-recursive^) ----
type X:\q1.txt
echo ---- Q2: USBXHCI service Enum ----
type X:\q2.txt
echo ---- Q3: PNP0D10 lines in connected dump ----
find /i "PNP0D10" X:\nwoas-diag.txt
echo ---- Q4: problem devices ----
type X:\q3prob.txt
echo ---- Q5: all Problem lines in full dump ----
find /i "Problem" X:\nwoas-diag.txt
echo ======================================================================
echo A1=NO-NODE   : ACPI did not expose xHCI  -^> fix DSDT   ^(case a^)
echo A1=NODE-EXISTS + USBXHCI Enum + problem code : start/DMA failure ^(case c^)
echo ---- FINAL VERDICT ^(repeat, scroll-safe^) ----
type X:\a1.txt
type X:\q2.txt
echo Full dump: X:\nwoas-diag.txt  ^(also copied to any drive letter^)
echo This screen STAYS. Force power off after photographing.
:nwoashold
ping -n 61 127.0.0.1 >nul
goto nwoashold
