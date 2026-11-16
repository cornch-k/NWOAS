@echo off
echo S248 D40 WER
powershell -NoProfile -Command "$d=Get-ChildItem C:\ProgramData\Microsoft\Windows\WER\ReportArchive,C:\ProgramData\Microsoft\Windows\WER\ReportQueue -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'explorer' } | Sort-Object LastWriteTime -Descending; 'DIRS '+$d.Count; $d | Select-Object -First 2 | ForEach-Object { 'DIR '+$_.FullName; Get-ChildItem $_.FullName | ForEach-Object { '  '+$_.Name+' '+$_.Length } }; $r=$d | Select-Object -First 1; if($r){ Get-Content (Join-Path $r.FullName 'Report.wer') -Encoding Unicode | Where-Object { $_ -match 'Sig|Stowed|Exception|Error|Hresult|HRESULT|ModName|Module|Dyn' } | Select-Object -First 40 }"
exit /b 0
