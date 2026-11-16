@echo off
echo S248 D7 SIGNING
bcdedit /enum {current} | findstr /i "testsigning"
certutil -store Root | findstr /i "WDKTestCert"
certutil -store TrustedPublisher | findstr /i "WDKTestCert"
exit /b 0
