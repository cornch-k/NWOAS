@echo off
echo GPUCOV K50 x86 UMD (2777/P42): nwgpuprobe15 umd draw11 d3d11 d3d12 in the user session, x86 (WOW64), x64 and ARM64. PASS = each arch 'umd summary ... agx_umd=ARCH ... result=PASS' + 'draw11 result=PASS' (x86: x86_umd_loaded=1) + 'd3d11 device ... Apple AGX ... hr=0x00000000'
set D=C:\NWOAS\gpucov
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\k50.cmd echo @echo off
>> %D%\k50.cmd echo %D%\nwgpuprobe15-x86.exe umd draw11 d3d11 d3d12 ^> %D%\k50-x86.txt 2^>^&1
>> %D%\k50.cmd echo %D%\nwgpuprobe15-x64.exe umd draw11 d3d11 ^> %D%\k50-x64.txt 2^>^&1
>> %D%\k50.cmd echo %D%\nwgpuprobe15-arm64.exe umd draw11 ^> %D%\k50-arm64.txt 2^>^&1
>> %D%\k50.cmd echo echo done^> %D%\k50-done.txt
del /q %D%\k50-done.txt %D%\k50-x86.txt %D%\k50-x64.txt %D%\k50-arm64.txt 2>nul
powershell -NoProfile -Command "$t0=Get-Date; while(-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -gt 0 }) -and ((Get-Date)-$t0).TotalSeconds -lt 240){ Start-Sleep 5 }; 'WAIT explorer_after_s='+[int]((Get-Date)-$t0).TotalSeconds"
powershell -NoProfile -ExecutionPolicy Bypass -File %D%\usersess.ps1 -Name NWOAS-K50 -Cmd %D%\k50.cmd -Done %D%\k50-done.txt -Timeout 300
powershell -NoProfile -Command "foreach($a in 'x86','x64','arm64'){ $f='C:\NWOAS\gpucov\k50-'+$a+'.txt'; if(!(Test-Path $f)){'K50 '+$a+' NO OUTPUT'; continue}; $l=Get-Content $f; 'K50 '+$a+' lines='+$l.Count+' last='+$l[-1]; $l | Where-Object { $_ -match ' umd | draw11 | d3d11 device| d3d11 clear_readback| d3d11 compute| d3d11 end| d3d12 device| d3d12 end|begin ' } | Select-Object -First 40 | ForEach-Object { 'K50 '+$a+' '+$_ } }; foreach($a in 'x86','x64','arm64'){ $x=Get-Content ('C:\NWOAS\gpucov\k50-'+$a+'.txt') -ErrorAction SilentlyContinue; $p1=@($x -match ('umd summary arch='+$a+' agx_umd='+$a+' .*result=PASS')).Count; $p2=@($x -match ('draw11 result=PASS arch='+$a+' ')).Count; $p3=@($x -match ('d3d11 device arch='+$a+' adapter=.Apple AGX.*hr=0x00000000')).Count; $p4=if($a -eq 'x86'){ @($x -match 'draw11 result=PASS arch=x86 .*x86_umd_loaded=1').Count }else{1}; 'K50 VERDICT '+$a+' summary='+$p1+' draw='+$p2+' device='+$p3+' umd32='+$p4+' => '+$(if($p1 -and $p2 -and $p3 -and $p4){'PASS'}else{'FAIL'}) }"
powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-10)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -in 1000,1001,1002 } | Select-Object -First 6 | ForEach-Object { 'K50 APPEVT '+$_.Id+' '+(($_.Message -split [char]10 | Select-Object -First 3) -join ' / ').Substring(0,[Math]::Min(260,$_.Message.Length)) }"
echo GPUCOV K50 DONE
exit /b 0
