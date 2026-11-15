@echo off
echo S231 J1 READ-ONLY INVENTORY
ver
echo DRIVES
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%d:\nul vol %%d:
echo USB SOURCE
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%d:\sources\install.swm dir %%d:\sources\install*.swm
(echo list disk)>X:\S231L.TXT
diskpart /s X:\S231L.TXT
for %%n in (0 1 2 3 4) do call :disk %%n
echo S231 J1 COMPLETE
exit /b 0
:disk
(echo select disk %1& echo detail disk& echo list partition)>X:\S231D.TXT
diskpart /s X:\S231D.TXT
for %%p in (1 2 3 4 5 6 7) do call :part %1 %%p
exit /b 0
:part
(echo select disk %1& echo select partition %2& echo detail partition)>X:\S231P.TXT
diskpart /s X:\S231P.TXT | findstr /i /c:"Partition %2" /c:"Type" /c:"Hidden" /c:"Offset" /c:"Volume ###" /c:"Volume " /c:"There is no"
exit /b 0
