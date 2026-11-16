@echo off
echo S248 D2 BOOT SIGNING STATE
bcdedit /enum {current}
echo S248 D2 SECUREBOOT
powershell -NoProfile -Command "try { Confirm-SecureBootUEFI } catch { $_.Exception.Message }"
echo S248 D2 DROP
dir C:\NWOAS\drop /s /b
echo S248 D2 TESTCERTS
certutil -store Root | findstr /i "WDKTestCert Subject"
certutil -store TrustedPublisher | findstr /i "WDKTestCert Subject"
exit /b 0
