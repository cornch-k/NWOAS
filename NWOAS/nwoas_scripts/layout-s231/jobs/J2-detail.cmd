@echo off
echo S231 J2 READ-ONLY PARTITION DETAIL DISK 0
(echo select disk 0& echo detail disk)>X:\S231A.TXT
diskpart /s X:\S231A.TXT
for %%p in (3 4 5 6) do call :part %%p
echo S231 J2 COMPLETE
exit /b 0
:part
(echo select disk 0& echo select partition %1& echo detail partition)>X:\S231P.TXT
diskpart /s X:\S231P.TXT
exit /b 0
