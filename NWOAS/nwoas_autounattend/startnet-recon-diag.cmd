@echo off
wpeinit
timeout /t 20 /nobreak >nul 2>&1

REM ===== 기존 디스크 정찰 =====
(echo list disk & echo list volume) > X:\dp.txt
diskpart /s X:\dp.txt > X:\nwoas-disks.txt 2>&1

REM ===== [DIAG] setupapi.dev.log : USB/xHCI/disk/ACPI/PNP0D10 열거 및 드라이버 바인딩 =====
if exist X:\Windows\inf\setupapi.dev.log (
  findstr /i "xhci\|usbhub\|usbstor\|disk\|PNP0D10\|ACPI\XHC\|PCI\\VEN" X:\Windows\inf\setupapi.dev.log > X:\diag-setupapi.txt 2>&1
) else (
  echo setupapi.dev.log NOT FOUND > X:\diag-setupapi.txt
)

REM ===== [DIAG] setupact.log (UEFI/WinPE 부팅 중 USB/xHCI 관련) =====
if exist X:\Windows\System32\LogFiles\Setup\setupact.log (
  findstr /i "xhci\|usb\|disk\|PNP0D10\|ACPI" X:\Windows\System32\LogFiles\Setup\setupact.log > X:\diag-setupact.txt 2>&1
) else (
  echo setupact.log NOT FOUND > X:\diag-setupact.txt
)

REM ===== [DIAG] 로드된 USB/디스크 드라이버 =====
driverquery /FO CSV > X:\diag-drv-raw.txt 2>nul
if exist X:\diag-drv-raw.txt (
  findstr /i "xhci\|usbhub\|usbstor\|disk\|stornvm\|partmgr\|usbxhci" X:\diag-drv-raw.txt > X:\diag-drv.txt 2>&1
) else (
  echo driverquery unavailable > X:\diag-drv.txt
)

REM ===== [DIAG] USB PnP 디바이스 (powershell 있으면 / 없으면 15s 후 스킵) =====
timeout /t 15 powershell -NoProfile -Command "Get-PnpDevice -Class USB | Format-Table Name,Status,InstanceId -AutoSize" > X:\diag-pnp.txt 2>&1
if errorlevel 1 echo powershell unavailable or skipped > X:\diag-pnp.txt

REM ===== 화면 출력 =====
mode con: cols=140 lines=90 >nul 2>&1
cls
echo ==================== NWOAS DISK RECON ====================
type X:\nwoas-disks.txt
echo.
echo ==================== [DIAG] setupapi.dev.log (USB/xHCI/disk/ACPI) ====================
type X:\diag-setupapi.txt
echo.
echo ==================== [DIAG] setupact.log (USB/xHCI/ACPI) ====================
type X:\diag-setupact.txt
echo.
echo ==================== [DIAG] loaded USB/disk drivers ====================
type X:\diag-drv.txt
echo.
echo ==================== [DIAG] USB PnP devices ====================
type X:\diag-pnp.txt
echo.
echo =========================================================
echo   WOA = ~931GB disk. STAGE3 판정: 디스크가 보이면 성공.
echo   위 DIAG에서 XHC0가 ACPI\PNP0D10 으로 잡히는지, 드라이버 바인딩됐는지 확인.
echo   This screen STAYS. Force power off after reading.
echo =========================================================

REM ===== [SERIAL DUMP] 화면 내용을 직렬(COM1)로도 전송 -> run_guest가 hv-log.txt로 Proxy =====
mode com1:115200,n,8,1 >nul 2>nul
echo NWOAS_DIAG_BEGIN >> COM1 2>nul
type X:\nwoas-disks.txt >> COM1 2>nul
echo ----- [DIAG] setupapi.dev.log (USB/xHCI/disk/ACPI) ----- >> COM1 2>nul
type X:\diag-setupapi.txt >> COM1 2>nul
echo ----- [DIAG] setupact.log (USB/xHCI/ACPI) ----- >> COM1 2>nul
type X:\diag-setupact.txt >> COM1 2>nul
echo ----- [DIAG] loaded USB/disk drivers ----- >> COM1 2>nul
type X:\diag-drv.txt >> COM1 2>nul
echo ----- [DIAG] USB PnP devices ----- >> COM1 2>nul
type X:\diag-pnp.txt >> COM1 2>nul
echo NWOAS_DIAG_END >> COM1 2>nul

:nwoashold
timeout /t 3600 /nobreak >nul 2>&1
goto nwoashold
