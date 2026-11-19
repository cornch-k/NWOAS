# NWOAS P42: as 12-package-arm64x.ps1 (P37) plus the x86 (WOW64) UMD of 11y as AppleAgxRenderAdmissionUmd32.dll
# and AppleAgxRenderAdmission.arm64xwow.inf (SysWOW64 copy, UserModeDriverNameWow; upstream repackage-driver.ps1
# -WowUmd), then signed with the build's WDK test certificate. Collects the out dir.
param([string]$Run='j274g4m',[string]$Stamp)
$ErrorActionPreference='Continue'; $env:VSCMD_SKIP_SENDTELEMETRY='1'
$B='C:\agx'; $ra="$B\AD04-persistent-dwm-next\drivers\apple-agx\render-admission"; $fc="$B\AD04-fullcompiler-001"
$kit='C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0'
$thumb='13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF'
$src="$ra\ARM64\Release\AppleAgxRenderAdmission"; $x64="$ra\umd\x64\Release\AppleAgxRenderAdmissionUmd.dll"; $x86="$ra\umd\Win32\Release\AppleAgxRenderAdmissionUmd.dll"; $fwd="$B\p37-forwarder-$Stamp\AppleAgxRenderAdmissionUmd.dll"
$p="$B\p37-package-$Stamp"; if(Test-Path $p){ Remove-Item $p -Recurse -Force }; New-Item -ItemType Directory $p | Out-Null
Copy-Item "$src\AppleAgxRenderAdmission.sys" $p
Copy-Item "$src\AppleAgxRenderAdmissionUmd.dll" "$p\AppleAgxRenderAdmissionUmdArm64.dll"
Copy-Item $fwd "$p\AppleAgxRenderAdmissionUmd.dll"
Copy-Item $x64 "$p\AppleAgxRenderAdmissionUmdX64.dll"
Copy-Item $x86 "$p\AppleAgxRenderAdmissionUmd32.dll"
Copy-Item "$ra\AppleAgxRenderAdmission.arm64xwow.inf" $p
$info=[Diagnostics.FileVersionInfo]::GetVersionInfo("$p\AppleAgxRenderAdmission.sys")
$ver='{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
"KMD_VERSION=$ver"
& "$kit\x86\stampinf.exe" -d '*' -a 'ARM64' -v $ver -k '1.15' -x -f "$p\AppleAgxRenderAdmission.arm64xwow.inf"; "STAMPINF_RC=$LASTEXITCODE"
& "$kit\x86\inf2cat.exe" /os:Server10_arm64 /USELOCALTIME "/driver:$p\" *> "$B\inf2cat-$Stamp.log"; "INF2CAT_RC=$LASTEXITCODE"; Get-Content "$B\inf2cat-$Stamp.log" -Tail 4
& "$kit\x86\signtool.exe" sign /ph /fd sha256 /sha1 $thumb "$p\appleagxrenderadmission.cat" *> "$B\sign-$Stamp.log"; "SIGN_RC=$LASTEXITCODE"; Get-Content "$B\sign-$Stamp.log" -Tail 3
& "$ra\scripts\verify-package-version.ps1" -PackageDirectory $p -InfName 'AppleAgxRenderAdmission.arm64xwow.inf'
# collect (as 10-collect.ps1)
$o="\\Mac\media\nwoas\out\j274-full-$Stamp"; New-Item -ItemType Directory -Force "$o\package","$o\symbols","$o\native-runtime","$o\native-runtime-x64","$o\native-runtime-x86","$o\logs","$o\source" | Out-Null
Copy-Item "$p\*" "$o\package\" -Force
foreach($f in "$ra\ARM64\Release\AppleAgxRenderAdmission.pdb","$ra\umd\ARM64\Release\AppleAgxRenderAdmissionUmd.pdb","$ra\umd\ARM64\Release\AppleAgxRenderAdmissionUmd.map","$ra\ARM64\Release\AppleAgxRenderAdmission.cer","$ra\assert-gate-result.json"){ if(Test-Path $f){Copy-Item $f "$o\symbols\" -Force} }
Copy-Item "$ra\umd\x64\Release\AppleAgxRenderAdmissionUmd.pdb" "$o\symbols\AppleAgxRenderAdmissionUmdX64.pdb" -Force
Copy-Item "$ra\umd\x64\Release\AppleAgxRenderAdmissionUmd.map" "$o\symbols\AppleAgxRenderAdmissionUmdX64.map" -Force -EA SilentlyContinue
Copy-Item "$ra\umd\Win32\Release\AppleAgxRenderAdmissionUmd.pdb" "$o\symbols\AppleAgxRenderAdmissionUmd32.pdb" -Force -EA SilentlyContinue
Copy-Item "$B\p37-forwarder-$Stamp\exports.log" "$o\logs\forwarder-exports.txt" -Force
foreach($a in 'arm64','x64','x86'){ $rt="$fc\asahi-runtime-$a-$Run"; $d= if($a -eq 'arm64'){"$o\native-runtime"}else{"$o\native-runtime-$a"}
  Copy-Item "$rt\NativeRuntime.props","$rt\result.json","$rt\native_runtime.lib" "$d\" -Force
  Copy-Item "$fc\asahi-native-$a-$Run\result.json" "$d\prepared-result.json" -Force }
Copy-Item "$B\source-manifest\source-manifest.json","$B\source-manifest\source.zip" "$o\source\" -Force
Get-Content "$B\driver-build-$Stamp.log" | Set-Content -Encoding UTF8 "$o\logs\build-driver.log"
Get-Content "$B\umd-x64-build-$Stamp.log" | Set-Content -Encoding UTF8 "$o\logs\build-umd-x64.log"
Get-Content "$B\umd-x86-build-$Stamp.log" | Set-Content -Encoding UTF8 "$o\logs\build-umd-x86.log"
Copy-Item "$B\inf2cat-$Stamp.log" "$o\logs\inf2cat.txt" -Force
$d='C:\VS2022Community\VC\Tools\MSVC\14.44.35207\bin\Hostarm64\arm64\dumpbin.exe'
foreach($n in 'AppleAgxRenderAdmissionUmd.dll','AppleAgxRenderAdmissionUmdArm64.dll','AppleAgxRenderAdmissionUmdX64.dll','AppleAgxRenderAdmissionUmd32.dll'){
  & $d /headers /dependents /exports "$o\package\$n" *> "$o\logs\dumpbin-$n.txt"
  'PE '+$n+' '+((Select-String -Path "$o\logs\dumpbin-$n.txt" -Pattern 'machine \(' | Select -First 1).Line.Trim()) }
$iv=Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Tools' -Recurse -Filter infverif.exe | ? { $_.FullName -match 'arm64' } | Select -First 1
if(!$iv){ $iv=Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Tools' -Recurse -Filter infverif.exe | Select -First 1 }
& $iv.FullName /w /v "$o\package\AppleAgxRenderAdmission.arm64xwow.inf" *> "$o\logs\infverif.txt"; "INFVERIF_RC=$LASTEXITCODE ($($iv.FullName))"
$st=Get-ChildItem $kit -Recurse -Filter signtool.exe | ? { $_.FullName -match 'arm64' } | Select -First 1
& $st.FullName verify /pa /v "$o\package\appleagxrenderadmission.cat" *> "$o\logs\signtool-verify-cat.txt"
& $st.FullName verify /pa /v "$o\package\AppleAgxRenderAdmission.sys" *> "$o\logs\signtool-verify-sys.txt"
Get-ChildItem $o -Recurse -File | ? { $_.Name -ne 'SHA256SUMS.txt' } | % { (Get-FileHash $_.FullName).Hash.ToLower() + '  ' + $_.FullName.Substring($o.Length+1).Replace('\','/') } | Set-Content -Encoding ascii "$o\SHA256SUMS.txt"
Get-Content "$o\SHA256SUMS.txt" | Select-String 'package/'
'PACKAGE_DONE'
