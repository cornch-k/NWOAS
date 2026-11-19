# NWOAS P42: x86 (WOW64) Mesa NIR closure, same pinned Mesa source and the x64 NIR options of 05x (platforms=[],
# -NDebug, i686 target, executable wrapper declared) + dxil-spirv c49 x86 libraries. Once per builder.
# Runs from the share copy of the J274 tree (\\Mac\media\nwoas\gpu-driver).
$ErrorActionPreference='Continue'
$B='C:\agx'; $env:APPLE_AGX_BUILDER_ROOT=$B
$src='\\Mac\media\nwoas\gpu-driver\drivers\apple-agx'
$fc="$B\AD04-fullcompiler-001"
(Get-Content "$src\mesa\scripts\compiler-x86.ini" -Raw).Replace("agx = 'C:/Users/pauls/APB12E~1/active'","agx = 'C:/agx'").Replace('needs_exe_wrapper = false','needs_exe_wrapper = true') | Set-Content -Encoding ascii "$fc\compiler-x86.ini"
$t0=Get-Date
if(!(Test-Path "$fc\nir-x86\build.ninja")){ & "$src\mesa\scripts\configure-x86-nir.ps1" -NDebug -CrossFile "$fc\compiler-x86.ini" -Platforms '[]'; "CONFIGURE_RC=$LASTEXITCODE" }
"CONFIGURE_DONE $((Get-Date)-$t0)"
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'; $kit='C:\Program Files (x86)\Windows Kits\10'
$env:PATH="$fc\winflex;$fc\venv\Scripts;$B\AD04-asahi-windows-compiler\llvm20\bin;$tool\bin\HostX64\x86;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x86;$kit\Lib\10.0.26100.0\ucrt\x86;$kit\Lib\10.0.26100.0\um\x86"
$t=@('src/compiler/nir/libnir.a','src/compiler/libcompiler.a','src/util/libmesa_util.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a','src/compiler/spirv/libvtn.a')
& "$fc\venv\Scripts\ninja.exe" -C "$fc\nir-x86" -k 0 @t *> "$fc\nir-x86\ninja-targets.log"
"NINJA_RC=$LASTEXITCODE $((Get-Date)-$t0)"
Select-String -Path "$fc\nir-x86\ninja-targets.log" -Pattern 'FAILED:|error:' | Select -First 15 | % { $_.Line.Substring(0,[Math]::Min(300,$_.Line.Length)) }
foreach($l in $t){ "$l $(Test-Path (Join-Path "$fc\nir-x86" $l))" }
$cc=Get-Content "$fc\nir-x86\compile_commands.json" -Raw
"HAVE_NDEBUG $($cc.Contains('NDEBUG'))  HAVE_X86_TARGET $($cc.Contains('i686-pc-windows-msvc'))"
if(!(Test-Path "$B\c49-dxspv-x86")){ & "$src\mesa\dxil\patches\c49-build-dxspv.ps1" -Architecture x86 }
"DXSPV_DONE $((Get-Date)-$t0)"
$miss=@($t | ? { !(Test-Path (Join-Path "$fc\nir-x86" $_)) })
$dx=@(Get-ChildItem "$B\c49-dxspv-x86\build" -Recurse -Filter 'dxil-spirv-c-static.lib' -ErrorAction SilentlyContinue)
if($miss.Count -or !$dx.Count){ 'NIR_X86_FAIL missing='+($miss -join ',')+' dxspv='+$dx.Count; exit 3 }
'NIR_X86_DONE'
