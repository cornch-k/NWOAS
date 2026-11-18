@echo off
echo S258 W19 PING STOP: stop flag, then the ping log (state changes, 60 s beats, end line; no addresses)
if not exist C:\NWOAS\wifitrace (echo S258 W19 no trace dir & exit /b 1)
echo stop> C:\NWOAS\wifitrace\ping.stop
ping -n 4 127.0.0.1 >nul
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$f='C:\NWOAS\wifitrace\ping.txt'; if(Test-Path $f){ $t=Get-Content $f; 'W19 LINES '+$t.Count; $o=0; foreach($l in $t){ $o+=$l.Length; if($o -gt 20000){ 'W19 CAP'; break }; $l } } else { 'W19 NO_LOG' }" 2>&1
echo S258 W19 STOP DONE
exit /b 0
