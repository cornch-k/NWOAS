@echo off
echo S231 J12 ROBOCOPY UNBUFFERED TEST
if exist X:\Windows\System32\robocopy.exe echo ROBOCOPY PRESENT
robocopy.exe E:\sources C:\S231RC install2.swm /J /R:0 /W:0 /NP
echo ROBOCOPY EXIT %errorlevel%
dir C:\S231RC
D:\NWREAD.EXE C:\S231RC\install2.swm 0000000000000000000000000000000000000000000000000000000000000000 direct
echo S231 J12 COMPLETE
exit /b 0
