@echo off
set "TOOLS="
for %%d in (C D E F G H I J) do if exist %%d:\NWOS.EXE set "TOOLS=%%d:"
if not defined TOOLS exit /b 20
%TOOLS%\NWGUARD.EXE C: identify
if errorlevel 1 exit /b 21
if not exist C:\S124-APPLY-PASS.TXT exit /b 22
if not exist C:\Windows\System32\ntoskrnl.exe exit /b 23
if exist C:\Windows\Panther\unattend.xml exit /b 30
if exist C:\Windows\Panther\Unattend\unattend.xml exit /b 31
if not exist C:\Windows\Panther md C:\Windows\Panther
if not exist C:\Windows\Setup\Scripts md C:\Windows\Setup\Scripts
if not exist C:\Windows\Temp md C:\Windows\Temp
copy /y %TOOLS%\NWOS.EXE C:\Windows\System32\NWOS.EXE
if errorlevel 1 exit /b 24
>C:\Windows\Panther\unattend.xml echo ^<?xml version="1.0" encoding="utf-8"?^>
>>C:\Windows\Panther\unattend.xml echo ^<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State"^>
>>C:\Windows\Panther\unattend.xml echo ^<settings pass="specialize"^>
>>C:\Windows\Panther\unattend.xml echo ^<component name="Microsoft-Windows-Deployment" processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"^>
>>C:\Windows\Panther\unattend.xml echo ^<RunSynchronous^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>1^</Order^>^<Description^>NWOAS local diagnostic transport^</Description^>^<Path^>cmd.exe /d /c C:\Windows\Setup\Scripts\NWOAS127.cmd^</Path^>^<WillReboot^>Never^</WillReboot^>^</RunSynchronousCommand^>^</RunSynchronous^>
>>C:\Windows\Panther\unattend.xml echo ^</component^>^</settings^>^</unattend^>
%TOOLS%\NWREAD.EXE C:\Windows\Panther\unattend.xml 4dc55f43056bc8b47194a72cbb61bd1072e557e2b1f2883d87fee2cb4f28de2f direct
if errorlevel 1 exit /b 25
>C:\Windows\Setup\Scripts\NWOAS127.cmd echo @echo off
>>C:\Windows\Setup\Scripts\NWOAS127.cmd echo start "" /b C:\Windows\System32\NWOS.EXE
>>C:\Windows\Setup\Scripts\NWOAS127.cmd echo exit /b 0
%TOOLS%\NWREAD.EXE C:\Windows\Setup\Scripts\NWOAS127.cmd 340096de83c400f4b8462883f51aacde8c8e2cac8c9caa3711efa0e5d45c3494 direct
if errorlevel 1 exit /b 25
%TOOLS%\NWREAD.EXE C:\Windows\System32\NWOS.EXE da2f0072d1159cdc866851781116909824eaafcbe7548f2ef0424c8ba1b78df8 direct
if errorlevel 1 exit /b 26
%TOOLS%\NWVFLUSH.EXE C: flush
if errorlevel 1 exit /b 27
echo S128 NATIVE DIAGNOSTIC AUTOSTART STAGED - FIRST BOOT UNTESTED
exit /b 0
