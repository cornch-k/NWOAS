@echo off
echo S252 D53 DUMP 0
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('C:\Windows\Minidump\111626-6953-01.dmp'); $o=0; $n=[Math]::Min(160000,$b.Length-$o); if($n -gt 0){ 'LEN '+$b.Length; 'B64 '+[Convert]::ToBase64String($b,$o,$n) }"
exit /b 0
