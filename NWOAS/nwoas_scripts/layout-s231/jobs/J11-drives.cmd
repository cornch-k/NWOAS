@echo off
echo S231 J11 DRIVE CHECK
for %%d in (C D E F G H) do if exist %%d:\nul vol %%d:
dir E:\sources\install*.swm
(echo list disk& echo list volume)>X:\S231L.TXT
diskpart /s X:\S231L.TXT
exit /b 0
