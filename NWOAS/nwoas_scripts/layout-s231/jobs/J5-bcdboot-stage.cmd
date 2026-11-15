@echo off
echo S231 J5 BCDBOOT AND FIRST-BOOT WORKER STAGING
if not exist C:\Windows\System32\ntoskrnl.exe exit /b 70
bcdboot C:\Windows /s S: /f UEFI /v
if errorlevel 1 exit /b 71
if not exist S:\EFI\Microsoft\Boot\bootmgfw.efi exit /b 72
if not exist S:\EFI\Boot\bootaa64.efi exit /b 73
bcdedit /store S:\EFI\Microsoft\Boot\BCD /enum all
set "TOOLS="
for %%d in (D E F G H I J) do if exist %%d:\NW177.EXE set "TOOLS=%%d:"
if not defined TOOLS exit /b 74
if exist C:\Windows\Panther\unattend.xml exit /b 75
if exist C:\Windows\Panther\Unattend\unattend.xml exit /b 76
if not exist C:\Windows\Panther md C:\Windows\Panther
if not exist C:\Windows\Setup\Scripts md C:\Windows\Setup\Scripts
if not exist C:\ProgramData\NWOAS md C:\ProgramData\NWOAS
copy /y %TOOLS%\NW177.EXE C:\ProgramData\NWOAS\NWOS.EXE
if errorlevel 1 exit /b 77
%TOOLS%\NWREAD.EXE C:\ProgramData\NWOAS\NWOS.EXE 14a1c914cd54f122a6899d3ae784c290bea641dadc4378883a150abc80db1b7f direct
if errorlevel 1 exit /b 78
>C:\Windows\Panther\unattend.xml echo ^<?xml version="1.0" encoding="utf-8"?^>
>>C:\Windows\Panther\unattend.xml echo ^<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State"^>
>>C:\Windows\Panther\unattend.xml echo ^<settings pass="specialize"^>
>>C:\Windows\Panther\unattend.xml echo ^<component name="Microsoft-Windows-Deployment" processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"^>
>>C:\Windows\Panther\unattend.xml echo ^<RunSynchronous^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>1^</Order^>^<Description^>NWOAS S231 local diagnostic transport^</Description^>^<Path^>cmd.exe /d /c C:\Windows\Setup\Scripts\NWOAS231.cmd^</Path^>^<WillReboot^>Never^</WillReboot^>^</RunSynchronousCommand^>^</RunSynchronous^>
>>C:\Windows\Panther\unattend.xml echo ^</component^>^</settings^>^</unattend^>
%TOOLS%\NWREAD.EXE C:\Windows\Panther\unattend.xml d196d3f8f0dbaa8f383e69f721ffeaeec59289fb81795b89ffcfe40e2e042c37 direct
if errorlevel 1 exit /b 79
>C:\Windows\Setup\Scripts\NWOAS231.cmd echo @echo off
>>C:\Windows\Setup\Scripts\NWOAS231.cmd echo schtasks.exe /Create /TN "NWOAS Agent" /SC ONSTART /RU SYSTEM /RL HIGHEST /TR "C:\ProgramData\NWOAS\NWOS.EXE" /F
>>C:\Windows\Setup\Scripts\NWOAS231.cmd echo start "" /b C:\ProgramData\NWOAS\NWOS.EXE
>>C:\Windows\Setup\Scripts\NWOAS231.cmd echo exit /b 0
%TOOLS%\NWREAD.EXE C:\Windows\Setup\Scripts\NWOAS231.cmd 836c1638b1ac78af73a33e06b7d915f76337c4171b40bc6f8600f7010ba2e0d5 direct
if errorlevel 1 exit /b 80
dir S:\EFI\Microsoft\Boot\bootmgfw.efi S:\EFI\Boot\bootaa64.efi
dir C:\ /a
echo S231 J5 PASS - BOOT FILES AND FIRST-BOOT WORKER STAGED; INSTALLED OS BOOT UNTESTED
exit /b 0
