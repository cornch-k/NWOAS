@echo off
echo S252 D52 WHEA
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='System';ProviderName='Microsoft-Windows-WHEA-Logger'} -MaxEvents 6 -ErrorAction SilentlyContinue | ForEach-Object { '== '+$_.TimeCreated.ToString('MM-dd HH:mm:ss')+' id '+$_.Id; ($_.Message -replace '\s+',' ').Substring(0,[Math]::Min(600,$_.Message.Length)); $x=[xml]$_.ToXml(); $x.Event.EventData.Data | ForEach-Object { '  '+$_.Name+'='+(''+$_.'#text').Substring(0,[Math]::Min(300,(''+$_.'#text').Length)) } }"
exit /b 0
