$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

$OutDir = 'C:\NWOAS\S134'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Save-Text {
    param([string]$Name, [scriptblock]$Body)
    $Path = Join-Path $OutDir $Name
    try {
        & $Body 2>&1 | Out-String -Width 4096 | Set-Content -Encoding UTF8 $Path
    } catch {
        $_ | Out-String | Set-Content -Encoding UTF8 $Path
    }
}

Save-Text '00-summary.txt' {
    $cpu = Get-CimInstance Win32_Processor
    $cs = Get-CimInstance Win32_ComputerSystem
    [pscustomobject]@{
        Timestamp = Get-Date -Format o
        Windows = [Environment]::OSVersion.VersionString
        Architecture = $env:PROCESSOR_ARCHITECTURE
        ProcessorName = ($cpu.Name -join '; ')
        Sockets = @($cpu).Count
        Cores = ($cpu | Measure-Object NumberOfCores -Sum).Sum
        LogicalProcessors = ($cpu | Measure-Object NumberOfLogicalProcessors -Sum).Sum
        CurrentClockMHz = ($cpu.CurrentClockSpeed -join ',')
        MaxClockMHz = ($cpu.MaxClockSpeed -join ',')
        VisibleMemoryBytes = $cs.TotalPhysicalMemory
        BootTime = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
    } | Format-List
}

Save-Text '01-cpu.txt' {
    Get-CimInstance Win32_Processor | Format-List *
    1..3 | ForEach-Object {
        Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor |
            Sort-Object Name |
            Select-Object Name,PercentProcessorTime,PercentUserTime,PercentPrivilegedTime,
                PercentIdleTime,InterruptsPersec
        Start-Sleep -Seconds 1
    }
}

Save-Text '02-pnp-problems.txt' {
    Get-PnpDevice -PresentOnly | Where-Object Status -ne 'OK' |
        Sort-Object Class, FriendlyName | Format-List Status,Class,FriendlyName,InstanceId,Problem
}

Save-Text '03-pnp-all.txt' {
    Get-PnpDevice -PresentOnly | Sort-Object Class, FriendlyName |
        Format-Table -AutoSize Status,Class,FriendlyName,InstanceId
}

Save-Text '04-network.txt' {
    Get-CimInstance Win32_NetworkAdapter | Sort-Object Index | Format-List *
    Get-NetAdapter -IncludeHidden | Sort-Object Name | Format-List *
    Get-NetIPConfiguration -All | Format-List *
}

Save-Text '05-storage.txt' {
    Get-Disk | Format-List *
    Get-PhysicalDisk | Format-List *
    Get-Volume | Sort-Object DriveLetter | Format-List *
}

Save-Text '06-drivers.txt' {
    Get-CimInstance Win32_PnPSignedDriver | Sort-Object DeviceClass,DeviceName |
        Format-Table -AutoSize DeviceClass,DeviceName,DriverProviderName,DriverVersion,InfName,DeviceID
}

Save-Text '07-power.txt' {
    powercfg /getactivescheme
    powercfg /query
}

Save-Text '08-bcd.txt' { bcdedit /enum all }
Save-Text '09-systeminfo.txt' { systeminfo }
Save-Text '10-pnputil-problems.txt' { pnputil /enum-devices /problem /deviceids /drivers }

$Archive = 'C:\NWOAS\S134-Windows-inventory.zip'
Remove-Item -Force -ErrorAction SilentlyContinue $Archive
Compress-Archive -Force -Path "$OutDir\*" -DestinationPath $Archive
Write-Host "S134 complete: $Archive"
