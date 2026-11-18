# J274 GPU driver - first build in the Parallels VM (2026-11-15 ~23:00)

Environment (FACT, checked with vswhere/Test-Path in the VM "Windows 11", ARM64, run as SYSTEM via `prlctl exec`):
VS 2022 Community 17.14.37906.3, MSVC 14.44.35207 (arm64, arm64ec, spectre libs), MSBuild arm64,
WDK 10.0.26100.6584 + VS component `Component.Microsoft.Windows.DriverKit` (toolset WindowsKernelModeDriver10.0 present for ARM64),
SDK 10.0.26100 (installed builds 6901 and 9457). Source copy: Mac `/Volumes/media/nwoas/gpu-driver` = VM `\\Mac\media\nwoas\gpu-driver`
-> robocopy to `C:\nwoas\gpu-driver`. Script: `\\Mac\media\nwoas\setup\build1.cmd`. Logs: `/Volumes/media/nwoas/build-logs/`.

Commands = README "A안" steps 5 and 6 verbatim (UMD, then KMD with AppleAgxBoard=J274, FirmwareQualification, Arm64Only, RunCodeAnalysis).

| Target | Result | Errors |
|---|---|---|
| UMD AppleAgxRenderAdmissionUmd | FAIL | 6 (agx_d3d12_ddi.cpp: AGX_D3D12_BARRIER_COUNTERS / AgxD3d12BarrierCount undeclared) |
| KMD AppleAgxRenderAdmission | FAIL | 222 (paging_windows.c 66, display.c 48, allocation_windows.c 42, receipts.c 34, backend_platform_windows.c 25, work_queue_windows.c 4, callbacks.c 2, lifecycle.c 1) |

Cause (FACT, from source): the README's "plain" configuration is not a configuration upstream compiles.
- UMD: `agx_d3d12_barrier_stats.h` is included only under `#ifdef APPLE_AGX_D3D12_NATIVE_OWNER` (agx_d3d12_ddi.cpp:34-39),
  but agx_d3d12_ddi.cpp:6258/6342/7353/7952 use it unconditionally.
- KMD: `_ADMISSION_CONTEXT` members PagingProfile/PagingCensus/DisplayRing/AllocationLife sit inside
  `#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)` (render_admission.h:692-782) but are used unguarded.
So upstream's real build (render-admission/scripts/build-driver.ps1) must define APPLE_AGX_D3D12_NATIVE_OWNER,
APPLE_AGX_GPUVA_G3_QUALIFICATION and probably more. Next step: derive the exact define set and MSBuild properties
from build-driver.ps1 (and its x86 ICD/UMD prerequisites), build with them for J274, Arm64-only if possible.
Build success would still not mean the driver works; only the Mac mini can show that.
