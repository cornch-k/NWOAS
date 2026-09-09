@echo off
echo === S154 USB-C DEVICE STATE BEGIN %date% %time% ===
powershell -NoProfile -Command "$classes='USB','HIDClass','Mouse','Keyboard'; Get-PnpDevice -PresentOnly | Where-Object { $classes -contains $_.Class } | ForEach-Object { $parent=(Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data; [PSCustomObject]@{Class=$_.Class;Name=$_.FriendlyName;Status=$_.Status;Problem=$_.Problem;InstanceId=$_.InstanceId;Parent=$parent} } | Sort-Object Class,InstanceId | Format-List"
echo === S154 USB-C DEVICE STATE END ===
exit /b 0
