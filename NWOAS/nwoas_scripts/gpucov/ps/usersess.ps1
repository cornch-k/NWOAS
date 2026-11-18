# Runs one .cmd in the logged-on user's session (Task Scheduler, Users group, Limited),
# because GUI/D3D present/browser tests do not work in session 0. Waits for a done-file.
param([string]$Name,[string]$Cmd,[string]$Done,[int]$Timeout=300)
$ErrorActionPreference='Continue'
$s=@(Get-Process -IncludeUserName -Name explorer -ErrorAction SilentlyContinue | ForEach-Object { ''+$_.SessionId+':'+$_.UserName })
'USERSESS '+$Name+' explorer_sessions='+($s -join ',')
Remove-Item -LiteralPath $Done -ErrorAction SilentlyContinue
$a=New-ScheduledTaskAction -Execute 'cmd.exe' -Argument ('/c "'+$Cmd+'"')
$p=New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Limited
Register-ScheduledTask -TaskName $Name -Action $a -Principal $p -Force | Out-Null
Start-ScheduledTask -TaskName $Name
$t0=Get-Date
while(-not (Test-Path -LiteralPath $Done) -and ((Get-Date)-$t0).TotalSeconds -lt $Timeout){ Start-Sleep 3 }
'USERSESS '+$Name+' waited_s='+[int]((Get-Date)-$t0).TotalSeconds+' done='+(Test-Path -LiteralPath $Done)
$i=Get-ScheduledTaskInfo -TaskName $Name -ErrorAction SilentlyContinue
if($i){ 'USERSESS '+$Name+(' last_result=0x{0:x}' -f $i.LastTaskResult) }
Unregister-ScheduledTask -TaskName $Name -Confirm:$false -ErrorAction SilentlyContinue
