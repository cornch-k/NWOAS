# NWOAS P42: x86 (WOW64) UMD = the same vcxproj as the ARM64/x64 UMD, Platform=Win32, x86 closure of 08y.
# Packaged as AppleAgxRenderAdmissionUmd32.dll (SysWOW64, UserModeDriverNameWow); no forwarder.
param([string]$Run='j274g4r',[int]$PackageBuild=2777,[string]$Stamp)
$ErrorActionPreference='Continue'; $env:PYTHONUTF8='1'; $env:VSCMD_SKIP_SENDTELEMETRY='1'
$B='C:\agx'; $env:APPLE_AGX_BUILDER_ROOT=$B
$repo="$B\AD04-persistent-dwm-next"; $ra="$repo\drivers\apple-agx\render-admission"
$fc="$B\AD04-fullcompiler-001"; $py="$fc\venv\Scripts\python.exe"
$env:PATH="C:\VS2022Community\MSBuild\Current\Bin\arm64;"+$env:PATH
$t0=Get-Date
$log="$B\umd-x86-build-$Stamp.log"
& "$ra\scripts\build-umd-x86-msbuild.ps1" -NativeRuntimeProps "$fc\asahi-runtime-x86-$Run\NativeRuntime.props" -PackageBuild $PackageBuild `
  -MesaSourceRoot "$B\AD04-d3d10-frontend-build\mesa" -MesaGeneratedRoot "$B\AD04-asahi-windows-compiler\b5\generated" -NativeProvenancePython $py -Board J274 *> $log
"X86_SCRIPT_OK=$? ELAPSED=$((Get-Date)-$t0)"
Select-String -Path $log -Pattern ' error |error [A-Z]+[0-9]+|warning\(s\)|error\(s\)|X86_UMD_PASS|ASSERT_GATE|failed' | Select -Last 20 | % { $_.Line.Substring(0,[Math]::Min(300,$_.Line.Length)) }
if(!(Select-String -Path $log -Pattern 'X86_UMD_PASS' -Quiet)){ 'X86_UMD_FAIL'; Get-Content $log -Tail 30 | % { $_.Substring(0,[Math]::Min(300,$_.Length)) }; exit 5 }
'X86_UMD_STEP_DONE'
