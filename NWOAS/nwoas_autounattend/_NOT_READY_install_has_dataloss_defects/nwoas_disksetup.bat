@echo off
setlocal enabledelayedexpansion

REM ================================================================
REM  NWOAS Safe Disk Selection + Partition  (DESTRUCTIVE on success)
REM ================================================================
REM  Called from FINAL_install_autounattend.xml -> RunSynchronous (windowsPE).
REM  Place in the ROOT of the USB install media, next to autounattend.xml.
REM
REM  Safety model (verified assumptions vs hypotheses):
REM    Apple internal NVMe  -> InterfaceType != "USB"          -> EXCLUDED (hypothesis: NVMe reports SCSI, not USB)
REM    FL1100 USB-A media   -> MediaType == "Removable Media"  -> EXCLUDED (hypothesis: it is a flash stick)
REM    USB-C external SSD   -> InterfaceType="USB" + non-Removable -> SELECTED
REM       (host macOS confirms this SSD: Protocol=USB, Removable=Fixed, so the filter should match it)
REM
REM  HARD GUARD: proceed with diskpart ONLY IF exactly 1 disk matches AND
REM  the matched index is a clean integer. Otherwise ABORT, touch nothing.
REM  The integer check also defeats the classic wmic trailing-CR corruption:
REM  if TGT came back as e.g. "2<CR>", the regex fails -> ABORT (fail-safe).
REM
REM  ALL logs -> X:\nwoas_log.txt (WinPE RAM disk). Read via hv.readmem BEFORE reboot.
REM ================================================================

set LOG=X:\nwoas_log.txt
set DP=X:\nwoas_diskpart.txt

echo NWOAS_DISKSETUP_BEGIN > %LOG%

REM --- Step 1: full enumeration for the record (read-only) ---
echo === diskdrive enumeration ===>> %LOG%
wmic diskdrive get Index,InterfaceType,MediaType,Size,Model /format:list >> %LOG% 2>&1
echo.>> %LOG%

REM --- Step 2: find USB non-removable disk(s) ---
set MC=0
set TGT=NONE
for /f "usebackq tokens=2 delims==" %%i in (`wmic diskdrive where "InterfaceType='USB' and MediaType!='Removable Media'" get Index /format:list ^| findstr /r "Index="`) do (
  set /a MC+=1
  set "TGT=%%i"
)
REM strip stray spaces/CR that wmic may append
set "TGT=%TGT: =%"

echo NWOAS_MATCH count=!MC! target=[!TGT!]>> %LOG%

REM --- Step 3: guards. Any failure => ABORT, no disk touched ---
if not "!MC!"=="1" (
  echo NWOAS_ABORT reason=match_count_not_1 count=!MC! ^(expected exactly 1^). No disk touched.>> %LOG%
  echo Likely: USB-C SSD missing, wmic unavailable, or install media also non-removable.>> %LOG%
  goto :done
)

REM integer-only validation of TGT (defeats trailing-CR / garbage corruption)
echo(!TGT!| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
  echo NWOAS_ABORT reason=target_not_integer value=[!TGT!]. No disk touched.>> %LOG%
  goto :done
)

REM extra paranoia: never allow disk 0 (typical internal-disk index) without explicit override
if "!TGT!"=="0" (
  echo NWOAS_ABORT reason=refuse_disk0 ^(internal-disk guard^). No disk touched.>> %LOG%
  echo If you REALLY intend disk 0, remove this guard manually after verifying it is the USB-C SSD.>> %LOG%
  goto :done
)

echo NWOAS_PROCEED target_disk=!TGT!>> %LOG%

REM --- Step 4: build diskpart script (GPT: EFI + MSR + Windows + Recovery) ---
> %DP% echo select disk !TGT!
>> %DP% echo clean
>> %DP% echo convert gpt
>> %DP% echo create partition efi size=100
>> %DP% echo format quick fs=fat32 label=System
>> %DP% echo create partition msr size=16
>> %DP% echo create partition primary
>> %DP% echo shrink desired=650
>> %DP% echo format quick fs=ntfs label=Windows
>> %DP% echo create partition primary
>> %DP% echo format quick fs=ntfs label=Recovery
>> %DP% echo set id=de94bba4-06d1-4d40-a16a-bfd50179d6ac
>> %DP% echo gpt attributes=0x8000000000000001

echo === diskpart script ===>> %LOG%
type %DP% >> %LOG%
echo.>> %LOG%

REM --- Step 5: execute ---
echo NWOAS_DISKPART_RUN>> %LOG%
diskpart /s %DP% >> %LOG% 2>&1
echo NWOAS_DISKPART_EXIT=%errorlevel%>> %LOG%
echo NWOAS_DISKSETUP_END>> %LOG%

:done
endlocal
