@echo off
echo S276 W41 knob rom: RomDump=1, device restart, NwoaRom0..3 printed gzip+base64, then removed; RESTART ACPI\NWOA4378\0
set "K=HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters"
for %%v in (FwSnap TestConnScanSec TestScanShape) do reg delete "%K%" /v %%v /f >nul 2>&1
reg add "%K%" /v RomDump /t REG_DWORD /d 1 /f
pnputil /restart-device "ACPI\NWOA4378\0"
echo S276 W41 RESTART EXIT %ERRORLEVEL%
ping -n 46 127.0.0.1 >nul
powershell -NoProfile -Command "$k='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters'; $ms=New-Object IO.MemoryStream; $gz=New-Object IO.Compression.GZipStream($ms,[IO.Compression.CompressionMode]::Compress); $n=0; foreach($i in 0..3){ $b=(Get-ItemProperty -Path $k -Name ('NwoaRom'+$i) -ErrorAction SilentlyContinue).('NwoaRom'+$i); if($b){ $gz.Write($b,0,$b.Length); $n+=$b.Length } }; $gz.Close(); $s=[Convert]::ToBase64String($ms.ToArray()); 'ROMLEN '+$n+' B64 '+$s.Length; for($o=0;$o -lt $s.Length;$o+=1000){ 'ROMB64 '+$s.Substring($o,[Math]::Min(1000,$s.Length-$o)) }; 'ROMEND'"
for %%i in (0 1 2 3) do reg delete "%K%" /v NwoaRom%%i /f >nul 2>&1
reg delete "%K%" /v RomDump /f
pnputil /restart-device "ACPI\NWOA4378\0"
ping -n 31 127.0.0.1 >nul
for %%v in (NwoaStage NwoaFwVersion NwoaCfgS258o) do reg query "%K%" /v %%v 2>nul | findstr /c:"Nwoa"
powershell -NoProfile -Command "(Get-PnpDeviceProperty -InstanceId 'ACPI\NWOA4378\0' -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue) | ForEach-Object { 'PROP '+$_.KeyName+' = '+$_.Data }"
echo S276 W41 DONE
exit /b 0
