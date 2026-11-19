@echo off
rem S264C T1: install a Windows startup task that does what the MacBook sends as agx-s248/jobs/D28-rearm.cmd
rem (G3Armed=1 + restart ACPI\APPL0002\0), but ONLY when no second NVMe namespace is present, i.e. only in a
rem C-launcher (S264, NS1-only) boot.  In a MacBook-connected boot NS2 exists and the MacBook keeps sending D28.
rem Run once through the MacBook job transport.  Undo: T2-remove-d28-boot-task.cmd.
echo S264C T1 INSTALL D28 BOOT TASK
if not exist C:\NWOAS\s264 mkdir C:\NWOAS\s264
> C:\NWOAS\s264\D28-boot.ps1 echo $l='C:\NWOAS\s264\d28-boot.log'; $n=@(Get-PhysicalDisk -ErrorAction SilentlyContinue ^| Where-Object { $_.BusType -eq 'NVMe' }).Count; $t=(Get-Date).ToString('s')
>> C:\NWOAS\s264\D28-boot.ps1 echo if($n -ge 2){ Add-Content $l ($t+' NVMe disks='+$n+' (NS2 present: MacBook stack) -> skip'); exit 0 }
>> C:\NWOAS\s264\D28-boot.ps1 echo $k='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'; New-ItemProperty -Path $k -Name G3Armed -PropertyType DWord -Value 1 -Force ^| Out-Null
>> C:\NWOAS\s264\D28-boot.ps1 echo $o=(pnputil /restart-device 'ACPI\APPL0002\0' 2^>^&1) -join ' '; Start-Sleep -Seconds 45
>> C:\NWOAS\s264\D28-boot.ps1 echo $d=Get-PnpDevice -InstanceId 'ACPI\APPL0002\0' -ErrorAction SilentlyContinue; $s=(Get-Item $k).GetValue('Wom1StartStatus')
>> C:\NWOAS\s264\D28-boot.ps1 echo Add-Content $l ($t+' NVMe disks='+$n+' D28: G3Armed=1, restart: '+$o+' -^> DEV '+$d.Status+' '+$d.Problem+' Wom1StartStatus='+$s)
type C:\NWOAS\s264\D28-boot.ps1
schtasks /create /f /tn "NWOAS-S264-D28" /sc onstart /delay 0001:00 /ru SYSTEM /rl HIGHEST /tr "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File C:\NWOAS\s264\D28-boot.ps1"
echo S264C T1 SCHTASKS EXIT %ERRORLEVEL%
schtasks /query /tn "NWOAS-S264-D28" /fo LIST
echo S264C T1 DONE
exit /b 0
