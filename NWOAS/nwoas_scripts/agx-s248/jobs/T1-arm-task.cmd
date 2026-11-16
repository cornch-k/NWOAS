@echo off
echo S248 T1 INSTALL AGX ARM TASK
> C:\NWOAS\agx-arm.cmd echo @echo off
>> C:\NWOAS\agx-arm.cmd echo ping -n 11 127.0.0.1 ^>nul
>> C:\NWOAS\agx-arm.cmd echo echo %%DATE%% %%TIME%% start ^>^> C:\NWOAS\agx-arm.log
>> C:\NWOAS\agx-arm.cmd echo pnputil /enum-devices /problem ^| findstr /i "APPL0002" ^>nul ^|^| exit /b 0
>> C:\NWOAS\agx-arm.cmd echo reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f ^>^> C:\NWOAS\agx-arm.log
>> C:\NWOAS\agx-arm.cmd echo pnputil /restart-device "ACPI\APPL0002\0" ^>^> C:\NWOAS\agx-arm.log
type C:\NWOAS\agx-arm.cmd
schtasks /create /tn "NWOAS-AgxArm" /sc onstart /ru SYSTEM /rl HIGHEST /f /tr "C:\NWOAS\agx-arm.cmd"
schtasks /query /tn "NWOAS-AgxArm" /fo list | findstr /i "TaskName Status"
exit /b 0
