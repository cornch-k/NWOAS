@echo off
echo S273 I40 VC REDIST x86: Microsoft Visual C++ 2015-2022 x86 runtime for the x86 WOW64 UMD (AppleAgxRenderAdmissionUmd32.dll imports VCRUNTIME140.dll, MSVCP140.dll)
set "F=C:\NWOAS\drop\vcredist-x86\vc_redist.x86.exe"
set "H=0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64"
if not exist "%F%" (echo I40 FAIL missing %F% & exit /b 1)
powershell -NoProfile -Command "$g=(Get-FileHash -LiteralPath $env:F -Algorithm SHA256).Hash.ToLower(); $s=Get-AuthenticodeSignature -LiteralPath $env:F; 'I40 SHA '+$g; 'I40 SIG '+$s.Status+' | '+$s.SignerCertificate.Subject; if($g -ne $env:H){ 'I40 REFUSE sha'; exit 1 }; if($s.Status -ne 'Valid' -or $s.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation'){ 'I40 REFUSE signature'; exit 1 }; 'I40 CHECK OK'"
if errorlevel 1 (echo I40 FAIL check & exit /b 1)
powershell -NoProfile -Command "foreach($n in 'vcruntime140.dll','msvcp140.dll'){ $f='C:\Windows\SysWOW64\'+$n; 'I40 BEFORE '+$n+' '+$(if(Test-Path $f){(Get-Item $f).VersionInfo.FileVersion}else{'MISSING'}) }"
powershell -NoProfile -Command "$l='C:\NWOAS\drop\vcredist-x86\install-'+(Get-Date -Format yyyyMMdd-HHmmss)+'.log'; $p=Start-Process -FilePath $env:F -ArgumentList '/install','/quiet','/norestart','/log',$l -Wait -PassThru; $c=$p.ExitCode; 'I40 RC '+$c+' '+$(switch($c){0{'ok'}3010{'ok, restart wanted'}1638{'a newer or same version is already installed'}default{'FAIL'}}); Get-Content $l -ErrorAction SilentlyContinue | Where-Object { $_ -match 'error|Exit code|Apply complete|restart' } | Select-Object -Last 8 | ForEach-Object { 'I40 LOG '+$_.Substring(0,[Math]::Min(220,$_.Length)) }"
powershell -NoProfile -Command "function M($f){ $b=[IO.File]::ReadAllBytes($f); $o=[BitConverter]::ToInt32($b,60); '{0:X}' -f [BitConverter]::ToUInt16($b,$o+4) }; $ok=1; foreach($n in 'vcruntime140.dll','msvcp140.dll'){ $f='C:\Windows\SysWOW64\'+$n; if(Test-Path $f){ $m=M $f; 'I40 AFTER '+$n+' machine='+$m+' ver='+(Get-Item $f).VersionInfo.FileVersion; if($m -ne '14C'){ $ok=0 } } else { 'I40 AFTER '+$n+' MISSING'; $ok=0 } }; 'I40 VERDICT '+$(if($ok){'PASS (x86 CRT present in SysWOW64)'}else{'FAIL'})"
echo S273 I40 DONE
exit /b 0
