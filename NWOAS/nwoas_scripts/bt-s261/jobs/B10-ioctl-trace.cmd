@echo off
echo S261 B10 BTHX IOCTL TRACE (kind P=at child A=at FDO C=completed W=written R=read done H=held F=forward failed; func 100 GET_VERSION 101 SET_VERSION 102 CAPS 103 WRITE 104 READ)
ping -n 11 127.0.0.1 >nul
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='SilentlyContinue'; foreach ($d in @(Get-PnpDevice | Where-Object { $_.InstanceId -like 'NWBT\BCM4378BTH\*' -or $_.InstanceId -like 'ACPI\NWOA5F69\*' -or ($_.Class -eq 'Bluetooth' -and $_.Present) })) { 'DEV '+$d.InstanceId+' status='+$d.Status+' problem='+$d.Problem+' class='+$d.Class+' name='+$d.FriendlyName; Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName DEVPKEY_Device_ProblemStatus,DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_Service | ForEach-Object { '  PROP '+$_.KeyName+' = '+('{0:x}' -f $_.Data) } }"
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtStage
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCapsScoSupport
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCapsMaxScoChannels
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtBthxVersionSet
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoGetVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoSetVersion
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoCaps
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoWrite
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoRead
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoOther
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoPdoOther
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoFwdFail
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntCmdTx
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntEvt
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntReadDone
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntAclRx
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtCntDropFifo
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtDead
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoLogTotal
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoLog
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtIoLogLast
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtArmGen
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtArmResult
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtDeadReason
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v NwbtDeadGen
echo S261 B10 DONE
exit /b 0
