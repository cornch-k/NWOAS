# NWOAS P42: x86 (WOW64) counterpart of 08x-runtime-x64.ps1 (compiler evidence, native projection, runtime closure).
# Same repo, Mesa source, generated roots and source manifest as the ARM64 closure of the same run.
param([string]$Run='j274g4m')
$ErrorActionPreference='Continue'; $env:PYTHONUTF8='1'; $env:VSCMD_SKIP_SENDTELEMETRY='1'
$B='C:\agx'; $env:APPLE_AGX_BUILDER_ROOT=$B
$repo="$B\AD04-persistent-dwm-next"; $s="$repo\drivers\apple-agx\mesa\scripts"
$fc="$B\AD04-fullcompiler-001"; $py="$fc\venv\Scripts\python.exe"
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'; $kit='C:\Program Files (x86)\Windows Kits\10'
$env:PATH="$B\AD04-asahi-windows-compiler\llvm20\bin;$tool\bin\HostX64\x86;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x86;$kit\Lib\10.0.26100.0\ucrt\x86;$kit\Lib\10.0.26100.0\um\x86"
$t0=Get-Date
# 4. compiler evidence for nir-x86 (x86 build + link + execution of the shader fixture under emulation)
$before=@(Get-ChildItem $fc -Directory -Filter 'asahi-x86-*' | % FullName)
& $py "$s\build-full-asahi.py" --architecture x86 *> "$B\logs-full-asahi-x86-$Run.log"
"FULL_ASAHI_RC=$LASTEXITCODE"; Get-Content "$B\logs-full-asahi-x86-$Run.log" -Tail 5 | % { $_.Substring(0,[Math]::Min(600,$_.Length)) }
$ev=Get-ChildItem $fc -Directory -Filter 'asahi-x86-*' | ? { $before -notcontains $_.FullName } | Select -First 1
"EVIDENCE=$($ev.FullName)"
if(!$ev -or !(Test-Path "$($ev.FullName)\artifacts.json")){ 'NO_EVIDENCE'; exit 3 }
# 5. GPUVA native source projection (prepare-only)
$nat="$fc\asahi-native-x86-$Run"; $out="$fc\asahi-runtime-x86-$Run"
& $py "$s\build-native-asahi-state.py" --project $repo --architecture x86 --output $nat --windows-platform-declarations --native-batch-lifecycle --gpuva --prepare-only *> "$fc\asahi-native-x86-$Run.prepare.log"
"PREPARE_RC=$LASTEXITCODE"; Get-Content "$fc\asahi-native-x86-$Run.prepare.log" -Tail 3 | % { $_.Substring(0,[Math]::Min(400,$_.Length)) }
if($LASTEXITCODE){ exit 4 }
# 6. native runtime archive + NativeRuntime.props
& $py "$s\build-asahi-runtime-closure.py" --project $repo --architecture x86 --native-source $nat --output $out --gpuva --source-manifest "$B\source-manifest\source-manifest.json" --compiler-evidence $ev.FullName *> "$fc\asahi-runtime-x86-$Run.runner.log"
"CLOSURE_RC=$LASTEXITCODE"; Get-Content "$fc\asahi-runtime-x86-$Run.runner.log" -Tail 12 | % { $_.Substring(0,[Math]::Min(600,$_.Length)) }
"ELAPSED $((Get-Date)-$t0)"
