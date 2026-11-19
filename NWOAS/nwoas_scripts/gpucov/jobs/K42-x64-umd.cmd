@echo off
echo GPUCOV K42 x64 UMD (2773/P37): nwgpuprobe10 "umd draw11 d3d11" in the user session, x64 (Prism) and ARM64. PASS = x64 'umd summary ... agx_umd=x64 ... result=PASS' + 'draw11 result=PASS ... x64_umd_loaded=1' + 'd3d11 device arch=x64 adapter="Apple AGX..." hr=0x00000000'; ARM64 the same with agx_umd=arm64 (forwarder regression check)
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k42.cmd echo @echo off
>> %D%\k42.cmd echo %D%\nwgpuprobe10-x64.exe umd draw11 d3d11 ^> %D%\k42-x64.txt 2^>^&1
>> %D%\k42.cmd echo %D%\nwgpuprobe10-arm64.exe umd draw11 ^> %D%\k42-arm64.txt 2^>^&1
>> %D%\k42.cmd echo echo done^> %D%\k42-done.txt
del /q %D%\k42-done.txt %D%\k42-x64.txt %D%\k42-arm64.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K42 -Cmd %D%\k42.cmd -Done %D%\k42-done.txt -Timeout 300
powershell -NoProfile -Command "foreach($a in 'x64','arm64'){ $f='C:\NWOAS\gpucov\k42-'+$a+'.txt'; if(!(Test-Path $f)){'K42 '+$a+' NO OUTPUT'; continue}; $l=Get-Content $f; 'K42 '+$a+' lines='+$l.Count+' last='+$l[-1]; $l | Where-Object { $_ -match ' umd | draw11 | d3d11 device| d3d11 clear_readback| d3d11 compute| d3d11 end|begin ' } | Select-Object -First 40 | ForEach-Object { 'K42 '+$a+' '+$_ } }; $x=Get-Content 'C:\NWOAS\gpucov\k42-x64.txt' -ErrorAction SilentlyContinue; $p1=@($x -match 'umd summary arch=x64 agx_umd=x64 .*result=PASS').Count; $p2=@($x -match 'draw11 result=PASS arch=x64 .*x64_umd_loaded=1').Count; $p3=@($x -match 'd3d11 device arch=x64 adapter=.Apple AGX.*hr=0x00000000').Count; 'K42 VERDICT x64 crt_ready='+@($x -match 'umd crt_x64_ready=1').Count+' summary='+$p1+' draw='+$p2+' device='+$p3+' => '+$(if($p1 -and $p2 -and $p3){'PASS'}else{'FAIL'})"
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-10)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 } | Select-Object -First 6 | ForEach-Object { 'K42 APPEVT '+$_.Id+' '+(($_.Message -split [char]10 | Select-Object -First 3) -join ' / ').Substring(0,[Math]::Min(260,$_.Message.Length)) }"
echo GPUCOV K42 DONE
exit /b 0
