@echo off
echo S248 D5 WRITE c.cmd
> C:\NWOAS\c.cmd echo @echo off
>> C:\NWOAS\c.cmd echo cd /d C:\NWOAS\drop\agx-j274-2742
>> C:\NWOAS\c.cmd echo dir
>> C:\NWOAS\c.cmd echo certutil -addstore Root AppleAgxRenderAdmission.cer
>> C:\NWOAS\c.cmd echo certutil -addstore TrustedPublisher AppleAgxRenderAdmission.cer
type C:\NWOAS\c.cmd
exit /b 0
