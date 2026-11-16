@echo off
echo S248 D6 WRITE nwcert.cmd
copy /y C:\NWOAS\c.cmd C:\Windows\nwcert.cmd
type C:\Windows\nwcert.cmd
where nwcert
exit /b 0
