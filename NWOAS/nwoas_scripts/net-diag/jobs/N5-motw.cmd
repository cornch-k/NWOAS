@echo off
echo NWOAS N5 MARK OF THE WEB ON CINEBENCH COPIES
powershell -NoProfile -Command "Get-ChildItem -Path C:\NWOAS\cb26,C:\Users\*\Downloads,C:\Users\*\Desktop -Filter Cinebench*.exe -Recurse -ErrorAction SilentlyContinue | ForEach-Object { $z=Get-Content -LiteralPath $_.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue; 'EXE '+$_.FullName+' modified='+$_.LastWriteTime.ToString('u')+' zone='+(($z -join ' ') -replace '\s+',' ') }"
echo NWOAS N5 DONE
