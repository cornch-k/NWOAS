@echo off
echo S256 A3 PLAY QUIET TONE (440 Hz -30 dBFS 1.5 s; amp volume never above -24 dB)
set "W=C:\NWOAS\drop\snd-j274-20261118-1131\nwoas-tone-quiet.wav"
if not exist "%W%" (echo S256 A3 FAIL no wav & exit /b 1)
powershell -NoProfile -Command "$k='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\NWAU0001\0\Device Parameters'; $n='NwAudioStarts','NwAudioStops','NwAudioLastStopReason','NwAudioLastRunStatus','NwAudioDescSubmitted','NwAudioDescCompleted','NwAudioPushBytes','NwAudioPadChunks','NwAudioDmaRanDry','NwAudioOsStarved','NwAudioAmpState','NwAudioVolReg'; $i=Get-ItemProperty $k; 'BEFORE '+(($n | ForEach-Object { $_.Substring(7)+'='+('{0:x}' -f [uint32]$i.$_) }) -join ' '); $t0=Get-Date; try { $p=New-Object Media.SoundPlayer $env:W; $p.PlaySync(); 'PLAYSYNC OK '+[int]((Get-Date)-$t0).TotalMilliseconds+' ms' } catch { 'PLAYSYNC FAIL '+$_.Exception.Message }; Start-Sleep -Seconds 6; $i=Get-ItemProperty $k; 'AFTER  '+(($n | ForEach-Object { $_.Substring(7)+'='+('{0:x}' -f [uint32]$i.$_) }) -join ' ')"
echo S256 A3 DONE
exit /b 0
