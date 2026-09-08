@echo off
if not exist E:\NWOAS-S122.RUNNING exit /b 21
if not exist E:\sources\install.swm exit /b 22
if not exist E:\sources\install2.swm exit /b 23
if not exist E:\SCRIPTS\NWHASH.EXE exit /b 24
if not exist D:\NWAGENT.EXE exit /b 25
vol E:
if exist E:\AUTOUNATTEND.PRE123.XML exit /b 26
copy /b /y E:\autounattend.xml E:\AUTOUNATTEND.PRE123.XML
if errorlevel 1 exit /b 27
>X:\NWOAS-LINK.XML echo ^<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State"^>^<settings pass="windowsPE"^>^<component name="Microsoft-Windows-Setup" processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"^>^<UserData^>^<ProductKey^>^<Key^>00000-00000-00000-00000-00000^</Key^>^<WillShowUI^>Always^</WillShowUI^>^</ProductKey^>^</UserData^>^<RunSynchronous^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>1^</Order^>^<Path^>reg.exe add HKLM\SYSTEM\Setup\LabConfig /v BypassTPMCheck /t REG_DWORD /d 1 /f^</Path^>^</RunSynchronousCommand^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>2^</Order^>^<Path^>reg.exe add HKLM\SYSTEM\Setup\LabConfig /v BypassSecureBootCheck /t REG_DWORD /d 1 /f^</Path^>^</RunSynchronousCommand^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>3^</Order^>^<Path^>reg.exe add HKLM\SYSTEM\Setup\LabConfig /v BypassRAMCheck /t REG_DWORD /d 1 /f^</Path^>^</RunSynchronousCommand^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>4^</Order^>^<Path^>reg.exe add HKLM\SYSTEM\Setup\LabConfig /v BypassCPUCheck /t REG_DWORD /d 1 /f^</Path^>^</RunSynchronousCommand^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>5^</Order^>^<Path^>reg.exe add HKLM\SYSTEM\Setup\LabConfig /v BypassStorageCheck /t REG_DWORD /d 1 /f^</Path^>^</RunSynchronousCommand^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>6^</Order^>^<Path^>cmd.exe /d /c "for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do @if exist %%D:\NWOAS-S84.TAG call %%D:\SCRIPTS\FIX11.CMD"^</Path^>^</RunSynchronousCommand^>^<RunSynchronousCommand wcm:action="add"^>^<Order^>7^</Order^>^<Path^>cmd.exe /d /c for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do @if exist %%D:\NWAGENT.EXE %%D:\NWAGENT.EXE^</Path^>^</RunSynchronousCommand^>^</RunSynchronous^>^</component^>^</settings^>^</unattend^>
E:\SCRIPTS\NWHASH.EXE X:\NWOAS-LINK.XML e322d3b13f197bdf70c9a3e199a804684a07b6b750515b55fa2d699048f17a4c
if errorlevel 1 exit /b 28
copy /b /y X:\NWOAS-LINK.XML E:\AUTOUNATTEND.S123.XML
if errorlevel 1 exit /b 29
E:\SCRIPTS\NWHASH.EXE E:\AUTOUNATTEND.S123.XML e322d3b13f197bdf70c9a3e199a804684a07b6b750515b55fa2d699048f17a4c
if errorlevel 1 exit /b 30
copy /b /y E:\AUTOUNATTEND.S123.XML E:\autounattend.xml
if errorlevel 1 exit /b 31
E:\SCRIPTS\NWHASH.EXE E:\autounattend.xml e322d3b13f197bdf70c9a3e199a804684a07b6b750515b55fa2d699048f17a4c
if errorlevel 1 exit /b 32
echo S123 USB AUTOSTART CONFIGURED AND HASH VERIFIED
exit /b 0
