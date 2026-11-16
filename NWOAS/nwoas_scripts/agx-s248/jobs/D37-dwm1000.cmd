@echo off
echo S248 D37 DWM 1000
powershell -NoProfile -Command "$e=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=(Get-Date).AddMinutes(-120)} -ErrorAction SilentlyContinue); 'N1000 '+$e.Count; $e | Group-Object { $_.Properties[0].Value+' '+$_.Properties[3].Value+' '+$_.Properties[6].Value+' '+$_.Properties[7].Value } | ForEach-Object { ''+$_.Count+' x '+$_.Name+' first '+($_.Group[-1].TimeCreated.ToString('HH:mm:ss'))+' last '+($_.Group[0].TimeCreated.ToString('HH:mm:ss')) }; reg query HKLM\SOFTWARE\Microsoft\Windows\Dwm /v DisableDDisplay"
exit /b 0
