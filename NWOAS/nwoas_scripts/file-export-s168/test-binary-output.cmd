@echo off
powershell -NoProfile -Command "$ErrorActionPreference='Stop'; $s=[Console]::OpenStandardOutput(); $b=New-Object byte[] 65536; for($i=0;$i -lt $b.Length;$i++){ $b[$i]=$i -band 255 }; for($j=0;$j -lt 17;$j++){ $b[0]=$j; $b[1]=255-$j; $n=if($j -eq 16){37}else{65536}; $s.Write($b,0,$n) }; $s.Flush()"
exit /b %errorlevel%
