@echo off
echo S258 W3 WIFI SCAN (WlanScan on every WLAN interface, wait 12 s, list networks)
powershell -NoProfile -Command "Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class W{[DllImport(\"wlanapi.dll\")]public static extern int WlanOpenHandle(uint v,IntPtr r,out uint n,out IntPtr h);[DllImport(\"wlanapi.dll\")]public static extern int WlanEnumInterfaces(IntPtr h,IntPtr r,out IntPtr l);[DllImport(\"wlanapi.dll\")]public static extern int WlanScan(IntPtr h,ref Guid g,IntPtr s,IntPtr i,IntPtr r);[DllImport(\"wlanapi.dll\")]public static extern int WlanCloseHandle(IntPtr h,IntPtr r);}'; $n=[uint32]0; $h=[IntPtr]::Zero; 'OPEN '+[W]::WlanOpenHandle(2,[IntPtr]::Zero,[ref]$n,[ref]$h); $l=[IntPtr]::Zero; 'ENUM '+[W]::WlanEnumInterfaces($h,[IntPtr]::Zero,[ref]$l); $c=[Runtime.InteropServices.Marshal]::ReadInt32($l); 'INTERFACES '+$c; for($i=0;$i -lt $c;$i++){ $b=New-Object byte[] 16; [Runtime.InteropServices.Marshal]::Copy([IntPtr]($l.ToInt64()+8+532*$i),$b,0,16); $g=New-Object Guid (,$b); 'SCAN '+$g+' rc='+[W]::WlanScan($h,[ref]$g,[IntPtr]::Zero,[IntPtr]::Zero,[IntPtr]::Zero) }; Start-Sleep -Seconds 12; [void][W]::WlanCloseHandle($h,[IntPtr]::Zero)"
netsh wlan show networks mode=bssid
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaLastScanBss
reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters" /v NwoaCntScans
echo S258 W3 DONE
exit /b 0
