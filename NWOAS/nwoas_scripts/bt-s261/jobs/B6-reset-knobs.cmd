@echo off
echo S261 B6 RESET bisect knobs to the driver defaults (StopAfter is kept) AND RESTART ACPI\NWOA5F69\0
for %%v in (DoFlr PollIntervalMs FixQuirks SetBdaddr SendCal SendPtb PokeAwake MsiToRam BootTimeoutMs StepTimeoutMs HciTimeoutMs CreatePdo AutoRestart NwbtAutoRestarts NwbtAutoRestartAt) do reg delete "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters" /v %%v /f >nul 2>&1
pnputil /restart-device "ACPI\NWOA5F69\0"
echo S261 B6 RESTART EXIT %ERRORLEVEL%
ping -n 31 127.0.0.1 >nul
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters"
echo S261 B6 DONE
exit /b 0
