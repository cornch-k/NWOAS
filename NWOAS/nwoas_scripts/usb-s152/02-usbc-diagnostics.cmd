@echo off
echo === S152 USB-C DIAGNOSTICS BEGIN %date% %time% ===
echo --- APPLE XHCI CONTROLLER ---
pnputil /enum-devices /instanceid "ACPI\PNP0D15\1" /drivers
echo --- FAILED ROOT HUB ---
pnputil /enum-devices /instanceid "USB\ROOT_HUB30\3&23492A9B&0&0" /drivers
echo --- PNP ENTITY ERROR CODES ---
wmic path Win32_PnPEntity where "PNPDeviceID='ACPI\\PNP0D15\\1' or PNPDeviceID='USB\\ROOT_HUB30\\3&23492A9B&0&0'" get Name,PNPDeviceID,Status,ConfigManagerErrorCode /format:list
echo --- CONTROLLER PROPERTIES ---
powershell -NoProfile -Command "$ids=@('ACPI\PNP0D15\1','USB\ROOT_HUB30\3&23492A9B&0&0'); foreach($id in $ids){ Write-Output ('DEVICE '+$id); Get-PnpDeviceProperty -InstanceId $id | Where-Object { $_.KeyName -match 'Problem|Driver|Service|Parent|Location' } | Format-Table -AutoSize KeyName,Type,Data }"
echo --- USB CONTROLLER SERVICES ---
sc query usbxhci
sc query usbhub3
echo === S152 USB-C DIAGNOSTICS END ===
exit /b 0
