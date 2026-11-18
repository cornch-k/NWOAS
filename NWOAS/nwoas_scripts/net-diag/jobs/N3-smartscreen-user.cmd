@echo off
echo NWOAS N3 SMARTSCREEN IN USER SESSION + DEBUG LOG
wevtutil sl Microsoft-Windows-SmartScreen/Debug /e:true 2>&1
set D=C:\NWOAS\netdiag
if not exist %D% mkdir %D%
icacls %D% /grant *S-1-5-32-545:(OI)(CI)M /T >nul
> %D%\u.ps1 echo $o=@()
>> %D%\u.ps1 echo $o+='USER '+[Environment]::UserName
>> %D%\u.ps1 echo $p=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
>> %D%\u.ps1 echo $o+='INET ProxyEnable='+$p.ProxyEnable+' ProxyServer='+$p.ProxyServer+' AutoConfigURL='+$p.AutoConfigURL
>> %D%\u.ps1 echo foreach($u in 'https://checkappexec.microsoft.com/windows/shell/service/beacon','https://apprep.smartscreen.microsoft.com/','https://nav.smartscreen.microsoft.com/windows/browser/edge/service/navigate/5'){ $t=Get-Date; try { $w=Invoke-WebRequest -Uri $u -Method Post -Body '{}' -ContentType 'application/json' -UseBasicParsing -UseDefaultCredentials -TimeoutSec 20 -ErrorAction Stop; $o+='POST '+$u+' status='+$w.StatusCode+' ms='+[int]((Get-Date)-$t).TotalMilliseconds } catch { $s=''; if($_.Exception.Response){$s=[int]$_.Exception.Response.StatusCode}; $o+='POST '+$u+' http='+$s+' ms='+[int]((Get-Date)-$t).TotalMilliseconds+' err='+$_.Exception.Message } }
>> %D%\u.ps1 echo $o ^| Set-Content -Encoding UTF8 C:\NWOAS\netdiag\u.txt
>> %D%\u.ps1 echo 'done' ^| Set-Content C:\NWOAS\netdiag\u-done.txt
> %D%\u.cmd echo @echo off
>> %D%\u.cmd echo powershell -NoProfile -ExecutionPolicy Bypass -File C:\NWOAS\netdiag\u.ps1
del /q %D%\u.txt %D%\u-done.txt 2>nul
powershell -NoProfile -ExecutionPolicy Bypass -File C:\NWOAS\gpucov\usersess.ps1 -Name NWOAS-N3 -Cmd %D%\u.cmd -Done %D%\u-done.txt -Timeout 120
type %D%\u.txt
echo == certificate chain and revocation for checkappexec
powershell -NoProfile -Command "$c=New-Object Net.Sockets.TcpClient('checkappexec.microsoft.com',443); $s=New-Object Net.Security.SslStream($c.GetStream(),$false,({$true})); $s.AuthenticateAsClient('checkappexec.microsoft.com'); 'TLS '+$s.SslProtocol+' cipher='+$s.CipherAlgorithm; $x=New-Object Security.Cryptography.X509Certificates.X509Certificate2($s.RemoteCertificate); [IO.File]::WriteAllBytes('C:\NWOAS\netdiag\cae.cer',$x.RawData); 'CERT '+$x.Subject+' notAfter='+$x.NotAfter; $ch=New-Object Security.Cryptography.X509Certificates.X509Chain; $ch.ChainPolicy.RevocationMode='Online'; $ok=$ch.Build($x); 'CHAIN ok='+$ok; $ch.ChainStatus | ForEach-Object { 'CHAINSTATUS '+$_.Status+' '+$_.StatusInformation }"
certutil -urlfetch -verify C:\NWOAS\netdiag\cae.cer 2>&1 | findstr /i "Verified Failed Error CRL OCSP AIA 0x8"
echo NWOAS N3 DONE
