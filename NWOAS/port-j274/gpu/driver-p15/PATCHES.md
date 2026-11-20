# J274 패치 목록 (PATCHES)

결론: **J313 기본 빌드는 원본과 똑같이 두고, `/p:AppleAgxBoard=J274`(또는 `APPLE_AGX_BOARD_J274=1`)를 줄 때만 J274 값으로 바뀌게 했습니다.** 바꾼 것은 보드마다 다른 메모리 주소뿐입니다. GPU 전원·성능 표, 화면(DCP) 경로처럼 J274 실기기 값을 아직 모르는 것은 일부러 J313 값 그대로 두었습니다. 그래서 J274 빌드는 그 단계에서 **조용히 틀린 값을 쓰지 않고 오류로 멈춥니다.**

- 빌드가 되는 것은 GPU가 동작하는 것이 아닙니다. 아래 어느 것도 Mac mini에서 시험하지 않았습니다.
- 소스에는 모두 `NWOAS-J274 PATCH Pn` 주석이 붙어 있습니다. `grep -rn "NWOAS-J274" drivers/`로 찾을 수 있습니다.
- 전체 차이: `j274-patch.diff` (원본 `external/windows-on-m1/drivers/apple-agx` 대비, 14개 파일).
- 원본에는 **보드 번호(0x26)를 검사하는 코드가 드라이버 안에 없습니다**(grep 결과 없음). 0x26 검사는 하이퍼바이저(`m1n1_windows/src/hv_agx_retained_platform.c:454`)와 프로필 생성 도구(`tools/generate_agx_hwdata_profile.py:120`)에 있습니다. 그래서 드라이버 쪽 "0x22 받아들이기"는 보드 번호 비교가 아니라 아래 주소 값 교체입니다.

## 바꾼 것

| 번호 | 파일 | 바꾼 내용 | J313 기본 빌드 영향 | J274 값의 근거 |
|---|---|---|---|---|
| P1 | `shared/include/apple_agx_board.h` (새 파일) | 보드 선택 한 곳. 보드 번호, GPU 영역(gpu-region), 핸드오프(handoff), 공유 영역(shared) 시작 주소, HwDataB 안 GPU 영역 주소 위치(0xa78) | 없음(J313 값이 기본) | 아래 "J274 값" 표 |
| P2 | `shared/include/j313_agx_g2.generated.h:90,96,98,100` | `J313_AGX_G2_GPU_BASE`, `_SHARED_BASE`, `_HANDOFF_BASE`를 P1 값으로 | 없음(전처리 결과 같음) | P1 |
| P3 | `render-admission/include/j313_agx_abi_admission.generated.h:18,20` | `J313_AGX_ABI_ADMISSION_GPU_BASE`, `_HANDOFF_BASE`를 P1 값으로 | 없음 | P1 |
| P4 | `shared/src/apple_agx_config_snapshot.c:49`, `shared/src/apple_agx_regionc.c:133` | 하이퍼바이저가 넘겨 주는 구성 정보의 `GpuRegionBase`를 고정값 0x9fffb8000 대신 P1 값과 비교 | 없음(공백 하나 차이) | P1 |
| P5a | `shared/include/apple_agx_hwdata_profile.generated.h` (`AgxHwdataInputBytes39`) | ADT 입력 `gpu-region-base` 바이트를 J274일 때 0xbfffb8000으로 | 없음 | P1 |
| P5b | `shared/include/apple_agx_hwdata_profile.h` (`AgxHwdataMaterialize`) | J274일 때 HwDataB의 0xa78 위치에 J274 GPU 영역 주소를 씀 | 없음(`#if` 안) | [사실] 원본 HwDataB 바이트에서 0x9fffb8000이 0xa78에 딱 한 번 나옴. [가설, 높음] 그 자리가 m1n1 `AGXHWDataB.gpu_region_base`(`m1n1_windows/proxyclient/m1n1/fw/agx/initdata.py:1143,1372`) |
| P6 | `render-admission/AppleAgxRenderAdmission.vcxproj`, `render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj`, `windows/AppleAgx.vcxproj`, `render-admission/scripts/build-driver.ps1` | MSBuild 속성 `AppleAgxBoard`(기본 J313). J274이면 `APPLE_AGX_BOARD_J274=1`. 다른 값이면 빌드 오류. 스크립트에 `-Board J313|J274` 인자 | 없음 | — |
| P7 | `render-admission/AppleAgxRenderAdmission.vcxproj:25-28, 326` | `armasm64.exe` 경로가 `Hostx64`로 박혀 있던 것을, ARM64 빌드 기계(예: Windows 11 ARM64)에서는 `HostARM64`를 쓰게 함 | x64 기계에서는 그대로 | [사실] 원본 경로 `vcxproj:318`(원본 줄) |
| P8 | `render-admission/src/arm64_hypercalls_gnu.S` (새 파일) | 원본 `arm64_hypercalls.asm`(armasm 문법)과 같은 두 함수(`hvc #0x4d32`, `hvc #0x4d31`)를 clang 문법으로. macOS에서 clang으로 교차 빌드할 때용. MSBuild는 원본 `.asm`을 계속 씀 | 없음 | [사실] 이 맥북에서 Windows ARM64 COFF로 어셈블하고 역어셈블해 같은 명령 확인 |
| P9 | `render-admission/AppleAgxRenderAdmission.arm64only.inf` (새 파일), `render-admission/AppleAgxRenderAdmission.vcxproj` (Inf·FilesToPackage 조건) | `/p:AppleAgxArm64Only=true`이면 32비트(WOW64) D3D 드라이버·x86 OpenGL 파일 없이 ARM64 커널 드라이버 + ARM64 사용자 드라이버만 담은 패키지를 만듦. 원본 패키지는 x86 파일 두 개가 없으면 만들어지지 않음(원본 INF `:53-62,101-102`, vcxproj `FilesToPackage`) | 없음(속성을 안 주면 원본 INF·파일 목록) | 첫 J274 시험(펌웨어 켜기/끄기)에 x86 파일은 필요 없음 |
| P10 | `render-admission/AppleAgxRenderAdmission.kmdonly.inf` (새 파일), `render-admission/AppleAgxRenderAdmission.vcxproj` (Inf·FilesToPackage 조건) | `/p:AppleAgxKmdOnly=true`이면 사용자 드라이버(.dll)·OpenGL 없이 **커널 드라이버(.sys)만** 담은 시험 패키지를 만듦. INF에서 `UserModeDriverName`·`OpenGLDriverName`을 뺐고 장치 ID(`ACPI\APPL0002`)·서비스·`Supports64KBPages`는 그대로. 그쪽이 실제로 내보내는 ARM64 사용자 드라이버는 Mesa/Asahi 묶음(`NativeRuntimeProps`)이 있어야만 빌드되고, 그 묶음은 여기서 만들 수 없어서(BUILD-ATTEMPT-2 참고) 넣음. 원본에 없는 패키지 형태 | 없음(속성을 안 주면 P9/원본 INF·파일 목록) | 첫 J274 커널 드라이버 시험(펌웨어 켜고 멈추기)에는 사용자 드라이버가 필요 없음 [가설: D3D만 못 쓰고 커널 드라이버 시작에는 영향 없음, 실기기 확인 전] |
| P11 | `render-admission/scripts/build-driver.ps1`, `render-admission/scripts/verify-package-version.ps1` | `build-driver.ps1 -Arm64OnlyPackage`를 주면 32비트(WOW64) x86 OpenGL·x86 D3D 사용자 드라이버 없이 원본 G3 순서(사용자 드라이버 → 커널 드라이버 → 판 검사) 그대로 빌드하고, 커널 드라이버 단계에 `/p:AppleAgxArm64Only=true`(P9 INF)를 넘김. CRT assert 검사는 ARM64 사용자 드라이버만 대상. `verify-package-version.ps1`에 `-InfName`(기본값 원본 이름) 추가 | 없음(스위치를 안 주면 원본과 같이 `-WowOpenGLIcd`·`-WowUmd` 필수) | x86 두 파일은 x86 Mesa·x86 런타임 묶음을 따로 만들어야 해서 이번 빌드에서 뺌(BUILD-ATTEMPT-3) |
| P12 | `shared/include/apple_agx_hwdata_profile.generated.h`, `shared/include/apple_agx_hwdata_profile_j274.generated.h` (새 파일) | `APPLE_AGX_BOARD_J274`일 때 Mac mini 실측 13.5 ADT로 만든 Hwdata 프로필(`port-j274/gpu/hwdata-j274/`의 바이트 그대로 사본, 같은 기호·같은 include guard)을 고르고, 아니면 원래 J313 프로필 | 없음(J313 전처리 결과 바이트 동일 확인, `clang -E -P` 비교) | [사실] 실측 ADT 입력 `gpu-region-base` = 0xbfffb8000, HwDataB 0xa78 위치 값도 0xbfffb8000(P1·P5b 값과 같음). J313과 바이트 줄 69개 다름(전압 표·전원 한도) |
| P13 | `render-admission/scripts/build-driver.ps1` | `-KeepUmdDynamicCrt`를 주면 G3 사용자 드라이버 MSBuild에 `/p:OverrideDefaultRuntimeLibrary=true`를 더함. 설치형 WDK 10.0.26100의 `SetHybridCrtCl` 대상(`WindowsDriver.Common.targets:466-485`)이 프로젝트가 정한 `/MD`(`umd/AppleAgxRenderAdmissionUmd.vcxproj:140`, 링크 목록에 `msvcrt.lib;ucrt.lib;vcruntime.lib`)를 `/MT`로 바꿔, `/MD`로 만든 Mesa 묶음과 링크가 안 됨(LNK2038). 프로젝트 원래 의도인 `/MD`를 지키는 것 | 없음(스위치를 안 주면 원본과 같음) | [사실] 1차 G3 빌드 로그의 LNK2038 수십 건, 모든 CL 명령이 `/MT` |
| P14 | `shared/include/apple_agx_board.h`, `shared/src/apple_agx_config_snapshot.c`, `shared/src/apple_agx_regionc.c` | `APPLE_AGX_BOARD_J274`일 때 구성 정보 해독기와 RegionC 입력 검사가 기본 성능 단계 3(Asahi `t8103-j274.dts`, m1n1 `AGXHWDataA` 기본값, P12 Hwdata와 같음)과 J274 실측 전압 표(400,593,628,678,768,846,912 mV)를 받음. RegionC 바이트 자체는 바꾸지 않음 | 없음(J313 전처리 결과 토큰 동일, 원본 RegionC 시험 통과) | [사실] J274 13.5 ADT로 m1n1 RegionC 빌더를 돌리면 J313 오라클과 바이트가 같음(fnv1a64 0xc3bc91a9acf61290, `port-j274/gpu/regionc-j274`). P14 전에는 하이퍼바이저가 3을 주면 해독기에서, 1을 주면 RegionC 전압 검사에서 시작이 멈춤 |

### J274 값 (P1)

| 항목 | J313(원본) | J274(이 패치) | 구분·근거 |
|---|---|---|---|
| 보드 번호 | 0x26 | 0x22 | [사실] `nwoas_scripts/logs/appark-chainload-1558.log:49` |
| GPU 영역(gpu-region) | 0x9fffb8000 | 0xbfffb8000 | [사실] m1n1이 이 주소에 0x4000 크기 Normal-NC 영역을 잡음(13.5 부팅 `appark-chainload-1558.log:108`, Tahoe 부팅 `logs/usb-s159-20260909-234847.C3i6zw:68`). [가설, 높음] 그것이 GPU 영역이라는 점. J313도 같은 크기로 "8 GB 끝 - 0x48000"에 있음 |
| 핸드오프(gfx-handoff) | 0x9fff70000 | 0xbfff70000 | 위와 같음(`:109`, `:69`) |
| 공유 영역(gfx-shared-region) | 0x9fff78000 | 0xbfff78000 | [가설, 중간] J313의 "핸드오프 + 0x8000" 위치를 그대로 옮김. 우리 로그 어디에도 이 영역은 안 찍힘 |
| HwDataB 안 GPU 영역 위치 | 0xa78 | 0xa78 | P5b 참고 |

## 일부러 바꾸지 않은 것 (J274에서는 여기서 멈춤)

| 번호 | 무엇 | 어디(원본 줄) | J274에서 일어날 일 | 풀려면 |
|---|---|---|---|---|
| O1 | GPU 성능 단계 표: 단계 7개, 기본 1, 최대 6, 측정 주기 8 ms, 주파수·전압 7쌍 | `shared/src/apple_agx_config_snapshot.c:44-48`, `shared/src/apple_agx_regionc.c:125-140` | J274 값이 다르면 `STATUS_DEVICE_CONFIGURATION_ERROR`로 시작 실패 | 13.5 스텁에서 J274 ADT `/arm-io/sgx`의 `perf-states`, `gpu-perf-base-pstate`, `gpu-num-perf-states`, `gpu-power-sample-period` 읽기 |
| O2 | **(2026-11-16 P12로 J274 빌드는 실측 ADT 프로필을 씀. 아래는 P12 전 기록)** GPU 전원·온도 제어 입력 66개(`gpu-*`)와 HwDataA/B 바이트, 프로필 ID | `shared/include/apple_agx_hwdata_profile.generated.h:1505`(ID), `:1611`(입력 목록) | 하이퍼바이저의 `AgxHwdataInputsMatch`(`hv_agx_retained_platform.c:456-458`)가 J274 ADT와 하나라도 다르면 프로필을 거부 → 펌웨어 시작 거부. 팬이 있는 Mac mini는 전원 값이 다를 가능성이 큼 [가설] | J274 ADT를 떠서 생성 도구를 J274용으로 고쳐 다시 생성. 원본 도구는 0x26과 "J313" 문자열을 고정(`tools/generate_agx_hwdata_profile.py:100,120`) |
| O3 | RegionC 초기 설정 값 표(J313 ADT에서 만든 값) | `shared/src/apple_agx_regionc.c:54` 이후 `AppleAgxRegionCJ313Words`, 지문 `:6` | J274와 다르면 거부 | O2와 같은 ADT 덤프로 다시 생성 |
| O4 | 화면: 내장 패널 2560x1600 고정 | `shared/include/apple_agx_scanout.h:20,35-38,44-50`, `shared/include/apple_agx_post_display_route.h:22-25`, `render-admission/src/display.c:252-269`, `render-admission/src/lifecycle.c:465-475` | 정식 빌드는 UEFI 화면이 2560x1600이 아니면 `STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER`로 시작 실패. 우리 UEFI 화면은 1280x720(`plan-20261115/gpu.md:29`) | HDMI용 화면 경로를 새로 짜야 함(하이퍼바이저 화면 중개 + 드라이버, `port/gpu.md` P7). 펌웨어 시험 빌드(`-FirmwareQualification`)는 이 단계 전에 끝나므로 막히지 않음(`backend_platform_windows.c:8319`이 `lifecycle.c:399`의 플랫폼 시작 안에 있고, 화면 단계는 `:425` 이후) |
| O5 | 펌웨어 IO 매핑 표(SoC 레지스터 주소 10개) | `shared/include/apple_agx_firmware_io.h:33-38` | 같은 칩이라 같을 것 [가설, 높음] | J274 ADT로 확인 |
| O6 | 오래된 G2 개발용 드라이버 `windows/`, `admission/` | — | 쓰지 않을 예정이라 손대지 않음(공용 헤더 P2는 함께 적용됨) | — |
| O7 | `shared/tests/*`의 J313 기대값(0x9fffb8000 등) | 예: `shared/tests/apple_agx_config_snapshot_test.c:53` | J313 빌드에서는 그대로 맞음. J274로 빌드하면 일부러 틀림 | J274 값 확정 뒤 보드별 기대값으로 |

## 다른 담당(하이퍼바이저·ACPI)과 맞물리는 것

| 항목 | 내용 |
|---|---|
| 하이퍼바이저가 같은 소스를 씀 | 그쪽 하이퍼바이저는 드라이버 폴더를 `../../drivers/apple-agx/...` 상대 경로로 직접 포함함: `apple_agx_hwdata_profile.h`(`m1n1_windows/src/hv_agx_retained_platform.c:15`), `apple_agx_firmware_io.h`·`apple_agx_hwdata_profile_abi.h`(`hv_agx_power_mmio.c:16-17`), `shared/src/apple_agx_uat.c`·`apple_agx_uat_table.c`(`hv_agx_retained_tables.c:2-3`) 등. 새 하이퍼바이저 트리(`m1n1-gpu-s240`)도 **이 폴더의 패치된 파일**을 가리키고 같은 `APPLE_AGX_BOARD_J274=1`로 빌드해야 프로필 ID·입력 바이트·주소가 드라이버와 맞음 |
| 보드 번호 0x26 검사 | `hv_agx_retained_platform.c:454` — 하이퍼바이저 담당이 0x22를 받아야 함. 드라이버 쪽에는 `APPLE_AGX_BOARD_ID`로 값만 둠 |
| ACPI 장치 | 정식 드라이버는 `ACPI\APPL0002` 장치에 메모리 5개(GPU 레지스터, GPU 영역, 핸드오프, 중개 페이지 0x300000000, 예약 메모리)와 인터럽트 정확히 1개(889)가 있어야 시작함(`backend_platform_windows.c:2603-2662`). 우리 MacMini2020Pkg DSDT에는 AGX0가 없음(`apple_silicon_platforms_mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl`, `APPL0100`·`APPL8900`만 있음). 초안: `acpi-draft/J274AppleAgxAbiAdmission.asl.inc` |

## P15 (NWOAS-J274, 2026-11-16) — J274 scanout: POST HDMI framebuffer + software scanout broker

J274(`APPLE_AGX_BOARD_J274`) 빌드에서만 적용. J313 빌드는 전처리 결과가 바뀌지 않음.

| 파일 | 변경 |
|---|---|
| `shared/include/apple_agx_scanout.h` | 기본 화면 상수(`APPLE_AGX_SCANOUT_J313_*`)를 1920x1080, 줄 간격 7680, 크기 0x7e9000으로. 맥미니 HDMI POST 화면 버퍼(S244 실측)와 다르면 post-display 경로가 거절 |
| `shared/include/apple_agx_fixed_panel_modes.h` | 모드 표를 {1920x1080} 하나로 |
| `render-admission/src/display.c` | 신호 정보를 CEA-861 1080p60(148.5 MHz, 2200x1125)으로 |
| `render-admission/src/scanout_windows.c` | 화면 중개 ABI v2(MMIO +0x400) 레지스터 접근을 드라이버 안 소프트웨어 상태로 처리. REGISTER_POOL은 맵해 둔 풀과 같을 때만 READY. PRESENT는 BGRA8888 1920x1080 표면을 풀에서 POST 화면 버퍼로 행 단위 복사하고 APPLIED·LATCHED 순번을 올림. LATCHED 인터럽트는 대기 교체(PendingValid)가 기록된 뒤 DPC → DxgkCbSynchronizeExecution 안에서 세우고 기존 ISR 경로(`AdmissionScanoutProcessInterrupt`)를 그대로 탐. 풀·화면 버퍼는 시작할 때 `MmMapIoSpaceEx(PAGE_READWRITE|PAGE_WRITECOMBINE)`로 맵, 정지·실패 때 해제 |

한계[가설]: PRESENT 복사(약 8 MB)가 호출 IRQL에서 일어남, 쓰기 결합 매핑 읽기 속도, 해상도는 1920x1080 고정, 화면 회전·여러 화면 없음.

## P16 (NWOAS-J274, 2026-11-16) — 복사를 별도 스레드로, 여러 CPU로 나눠 복사, 측정 기록

J274 빌드에서만 적용. 판 2746(측정만) → 2747(복사 스레드) → 2748(방식 비교) → 2749(분할 복사 + 자동 선택).

| 파일 | 변경 |
|---|---|
| `render-admission/src/scanout_windows.c` | PRESENT는 표면 검사와 위치 기록만 하고 복사는 하지 않음. 복사는 시작 때 만든 PASSIVE 시스템 스레드(우선순위 LOW_REALTIME)가 함: PRESENT 뒤 SoftKick → 깨우기 DPC → 스레드가 최신 표면을 복사 → LATCHED를 기존 DPC/ISR 경로로 올림. 복사는 그 스레드 + 도우미 스레드 7개로 1·4·8등분 가능. 처음 128회씩 세 방식을 재고 가장 빠른 방식으로 고정. 장치 키 `NwSoftPoolCached`(1이면 풀을 캐시 연결로 맵), `NwSoftOverlay`(1이면 진단 막대)를 시작 때 읽음. 측정값은 장치 키 `NwSoftStats`(16 QWORD)에 128회마다, 정지 때 한 번 더 씀 |

실측(맥미니, 1920x1080, 판별):

| 판 | 조건 | 1장 복사 평균 (ms) | 비고 |
|---|---|---|---|
| 2747 | 캐시 안 씀, CPU 1개 | 33.4 | PRESENT가 IRQL 10에서 불림 확인(2746까지는 이 수준에서 복사 → 그 CPU 정지) |
| 2747 | 캐시 씀, CPU 1개 | 25.9 | |
| 2748 | 캐시 씀, CPU 1개 | 21.2 | |
| 2748 | 캐시 씀, 읽기만 / 쓰기만 | 21.4 / 0.45 | 느린 쪽은 풀 읽기 |
| 2748 | 캐시 씀, CPU 4개 | 8.9 | |

[사실] 실행 중 `pnputil /restart-device`는 GPU 정리(retire·close·전원 끄기) 없이 다음 시작으로 넘어가 0xC0000483으로 실패함(2746·2747 모두). 실패 상태에서의 재시작, 설치 직후 시작은 정상. 판을 바꿀 때는 Windows를 재시작.
[가설] 풀 읽기가 느린 이유: 같은 물리 영역의 다른 CPU 맵(`pool_windows.c`, `physical_memory_windows.c`)이 쓰기 결합이라 Windows가 캐시 요청을 맞춰 바꾸는 것. 미확인.

DWM 설정(드라이버 밖): `HKLM\SOFTWARE\Microsoft\Windows\Dwm` `DisableDDisplay`=1. 없으면 DWM이 DDisplay 경로에서 UMD `PresentMultiplaneOverlay1`(E_NOTIMPL)를 부른 뒤 `CD3DDevice::GetPresentError`가 고른 0x889800d0으로 종료함(dwmcore 10.0.26100.7920 공개 심볼로 확인). 원본 windows-on-m1도 EXP951~1782 동안 같은 값을 씀.

## P17~P19 (NWOAS-J274, 2026-11-16) — 절전(모던 스탠바이) 대응

| 패치 | 판 | 파일 | 내용 | 근거 |
|---|---|---|---|---|
| P17 | 2753 | `render-admission/src/scheduler_windows.c` | `AdmissionDdiResetFromTimeout` 성공 시 `PagingRecordsUnsubmitted`를 0으로. 리셋 순간 화면 상태를 `NwResetDisplay`(장치 키)에 기록(RestartFromTimeout에서 씀) | [사실] 2752: 리셋 뒤 148건 남아 CPU 복사가 조건 62(STATUS_DEVICE_BUSY)로 실패 → explorer.exe 887a0005 무한 충돌 |
| P18 | 2754 | `render-admission/src/work_queue_windows.c` | 대기 수만 남고 큐·작업자·DPC·콜백이 모두 비어 있는 상태가 500ms 넘으면 남은 수를 버림(횟수는 `NwSoftStats[12]`) | [사실] 2753: 650건 남음. [가설] VidMm이 만들고 제출하지 않은 페이징 버퍼를 알리지 않음 |
| P18 | 2754 | `render-admission/src/scanout_windows.c` | 화면 숨김 중 받은 화면 전환을 다음 수직 틱에 완료로 알림(MPO PresentId 포함, `HiddenFlipPending`) | [사실] 2753: 숨김 중 보류 3건, GPU 작업은 모두 끝난 상태에서 VidSch가 어댑터 리셋 |
| P19 | 2755 | `render-admission/src/scanout_windows.c` | 화면 숨김 → 복사 스레드가 HDMI 화면 메모리를 검은색으로, 다시 보임 → 현재 표면을 다시 복사 | 절전 때 마지막 화면이 남던 문제. 실제 신호 끄기(DPMS)는 DCP 경로가 있어야 함 |

[사실] 2754에서 연속 두 번째 절전 뒤 펌웨어 Timeout(4ms) → RTKit 드레인 실패 → 리셋 3회 실패가 한 번 있었음. 이후 하이퍼바이저 g249D(GPU 예약 1GB)로 바꾼 뒤에는 콘치님 시험에서 재현되지 않음(원인 미확정).

## P20 (NWOAS-J274, 2026-11-16) — 하이퍼바이저 화면 서비스(DCP) 자동 선택, 화면 전원

| 파일 | 변경 |
|---|---|
| `render-admission/src/scanout_windows.c` | 시작 때 브로커 창의 MAGIC·ABI·CAPS를 읽어, ABI v2이고 CAPS에 `V2_PRESENT_CAPABILITIES`와 `CAP_DISPLAY_POWER`(1<<9)가 모두 있으면 하이퍼바이저 서비스 사용(`SoftActive`=0), 아니면 기존 복사 방식(P15~P19). 장치 키 `NwSoftForce`=1이면 항상 복사 방식. 고른 경로는 `NwScanoutPath`(1=하이퍼바이저 서비스, 2=복사), CAPS는 `NwScanoutHwCaps`에 기록. 서비스 경로에서 화면 숨김/보임은 PASSIVE에서 PresentGate(최대 250회×1ms)를 잡고 `AppleAgxScanoutSetPower`를 부름, 실패 수는 `PowerFailures` |
| `shared/include/apple_agx_scanout.h`, `shared/src/apple_agx_scanout.c` | `REG_POWER`(+0x9c), `CMD_SET_POWER`(4), `CAP_DISPLAY_POWER`, `AppleAgxScanoutSetPower`(순번 받기 → REG_POWER·REQUEST_SEQUENCE·COMMAND=4 쓰기 → RECEIPT/RESULT/APPLIED 확인) |

실측(2756판):

| 하이퍼바이저 | 고른 경로 | 결과 |
|---|---|---|
| g249D(서비스 없음) | 복사 | 바탕화면·작업 관리자 GPU 표시 정상(콘치님 확인) |
| g252H(서비스) | 하이퍼바이저 서비스 | 화면 출력·끄기/켜기 동작, 약 45초 뒤 Windows 0x124 |
| g252J(서비스, 인터럽트 우선순위 버그 수정) | 하이퍼바이저 서비스 | 31분 넘게 멈춤 없음, Windows가 쉬는 동안 화면 끄기 요청·완료 기록됨 |

0x124 원인과 수정은 하이퍼바이저 쪽 `port-j274/gpu/hv/README-dcp-s251.md` §10에 있습니다.

## P21 (NWOAS-J274, 2026-11-16) — 하이퍼바이저 화면 서비스가 확대 표시를 지원하면 해상도 여러 개 제공

J274 빌드에서만 적용. J313 빌드는 바뀐 파일 모두 `unifdef -UAPPLE_AGX_BOARD_J274` + 주석 제거 비교로 원본과 같은 코드임을 확인(`scanout_windows.c`·`apple_agx_scanout.h`의 차이는 P17~P20 것 그대로).

원본 rc106(J313 노트북 화면)과 같은 규칙입니다. 브로커 CAPS에 `CAP_SCALED_SOURCE`(1<<8)가 있으면 아래 표 전체를, 없으면 0번(1920x1080) 하나만 Windows에 알립니다. 0번이 아닌 모드는 하이퍼바이저가 DCP로 1920x1080 화면 안 가운데 사각형(가로세로 비율 유지, 기본값)에 맞춰 키워 보여 주는 방식입니다.

| 파일 | 변경 |
|---|---|
| `shared/include/apple_agx_fixed_panel_modes.h` | J274 표를 8개로: 1920x1080(기본), 1680x1050, 1600x900, 1440x900, 1280x800, 1280x720, 1024x768, 800x600 |
| `render-admission/src/scanout_windows.c` | `AdmissionScanoutScaledModes`: `SoftActive`(복사 방식)이면 무조건 FALSE. `AdmissionScanoutCommit`: 복사 방식에서 1920x1080/7680이 아닌 모드는 `STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE`로 거절 |
| `render-admission/src/display.c` | 확대 모드의 신호 정보를 J274 1080p60 기준으로: 블랭킹 +280/+45, HSync = 148500000×세로총합/(2200×1125) = 60×세로총합, VSync는 정확히 60 Hz 그대로. 원본 식은 J313 패널 주파수(266.63 MHz, 2642×1682)를 써서 J274의 60 Hz VSync와 맞지 않았음 |
| `shared/include/apple_agx_scanout.h` | 주석만(1920x1080은 기본 화면이자 확대 목적지) |
| `shared/tests/apple_agx_fixed_panel_j274_test.c` (새 파일) | J274 전용 호스트 시험(아래) |
| `render-admission/umd/tests/direct_flip_contract_test.c` | 기본 화면 크기·모드 수를 보드별 값으로(J313 값은 그대로) |

모드 표 (각 모드가 통과해야 하는 검사):

| 모드 | 줄 간격 (B) | 표면 크기 (B) | 64 KiB로 올림 (B) | 가로세로 유지 목적지 (x,y 너비x높이) |
|---|---|---|---|---|
| 1920x1080 | 7680 | 0x7e9000 | 0x7f0000 | 0,0 1920x1080 (그대로) |
| 1680x1050 | 6720 | 0x6baa80 | 0x6c0000 | 96,0 1728x1080 |
| 1600x900 | 6400 | 0x57e400 | 0x580000 | 0,0 1920x1080 |
| 1440x900 | 5760 | 0x4f1a00 | 0x500000 | 96,0 1728x1080 |
| 1280x800 | 5120 | 0x3e8000 | 0x3f0000 | 96,0 1728x1080 |
| 1280x720 | 5120 | 0x384000 | 0x390000 | 0,0 1920x1080 |
| 1024x768 | 4096 | 0x300000 | 0x300000 | 240,0 1440x1080 |
| 800x600 | 3200 | 0x1d4c00 | 0x1e0000 | 240,0 1440x1080 |

- 가로는 모두 16의 배수 → 줄 간격이 64의 배수(DCP 규칙), 세로는 모두 짝수 → 표면 크기가 128의 배수라 UMD의 128바이트 올림 크기와 KMD 화면 전환 검사(`Size == Stride × Height`)가 맞음.
- 1920x1080 자체가 64 KiB 배수가 아니라서(원본 EXP2055의 0xc0000141 원인), 화면 전환 주소 변환은 이미 64 KiB로 올린 길이를 씀(원본 d099a808이 이 트리에 있음). KMD 할당도 64 KiB로 올려 잡으므로 올린 길이가 할당 안에 들어감 — 시험으로 확인.
- 1366x768(줄 간격 5464, 64의 배수 아님)과 1920x1080보다 큰 모드는 넣지 않음.

복사 방식(P15~P19)은 모드 하나 그대로:

- [사실, 코드] 복사 방식 브로커가 돌려주는 CAPS는 `APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES`(0x7f) 고정이고 1<<8이 없음 → `Panel.Scanout.Capabilities`에도 없음 → `AdmissionScanoutScaledModes` FALSE → 모드 1개. P21에서 `SoftActive`면 CAPS와 상관없이 FALSE가 되도록 한 겹 더 막음.
- 복사 방식에서 다른 모드가 들어오면 세 곳에서 거절: `AdmissionScanoutCommit`(P21 새 검사), `AppleAgxFixedPanelCommitMode`(모드 1개 표), `AdmissionScanoutSoftPresent`(1920x1080/7680/BGRA가 아니면 INVALID_SURFACE).
- 복사 방식은 DEST_ORIGIN/DEST_SIZE(0xa0/0xa4)를 쓰지 않음(CAPS에 1<<8이 없으면 공용 코드가 쓰지 않음, 시험으로 확인).

P20 경로 선택: 새 하이퍼바이저 CAPS 0x37f에서 `(caps & want) == want`(want = 0x7f|0x200 = 0x27f)가 참 → 하이퍼바이저 서비스 선택. 0x37f에는 RGBA8888(1<<7)이 없으므로 RGBA 형식 전환은 지금처럼 거절됨.

호스트 시험(이 맥북 clang, `-std=c11 -Wall -Wextra -Werror`):

| 시험 | 보드 정의 | 결과 |
|---|---|---|
| `shared/tests/apple_agx_fixed_panel_test.c` | 없음(J313) | 통과 |
| `shared/tests/apple_agx_scanout_test.c` | 없음(J313) | 통과 |
| `render-admission/umd/tests/direct_flip_contract_test.c` | 없음(J313) | 통과 |
| `shared/tests/apple_agx_fixed_panel_j274_test.c` | `APPLE_AGX_BOARD_J274=1` | 통과 (정의 없이 빌드하면 `#error`로 거절) |
| `render-admission/umd/tests/direct_flip_contract_test.c` | `APPLE_AGX_BOARD_J274=1` | 통과 |

J274 시험 내용: 표 8개가 모든 검사(16 배수·짝수·64 배수·128 올림 = 표면 크기·64 KiB 올림이 할당 안·목적지 사각형) 통과 / 복사 방식 CAPS(0x7f)로는 모드 1개, 확대 모드 7개 모두 거절, DEST 레지스터 안 씀 / CAPS 0x37f로는 7개 모두 자기 크기와 가운데 사각형을 브로커에 씀, 기본 모드는 0,0 1920x1080을 씀 / 풀 끝 2 MiB에 800x600은 들어가고 1920x1080은 거절 / P20 경로 선택 식(0x27f·0x37f 선택, 0x17f·0x7f 안 함).
`apple_agx_fixed_panel_test.c`·`apple_agx_scanout_test.c`는 J313 값(2560x1600)을 직접 쓰는 원본 시험이라 J274 정의로는 빌드하지 않습니다(P15 이전부터 그랬음).

하드웨어에서만 알 수 있는 것:

- [가설] dxgkrnl이 하이퍼바이저 서비스 경로에서 8개 모드를 실제로 보여 주는지(원본은 EXP2053~2056에서 시작 전 모드 질의 문제를 고친 뒤에 됨, 그 수정은 이 트리에 있음).
- [가설] 하이퍼바이저가 DEST_ORIGIN/DEST_SIZE를 받아 DCP가 HDMI 1080p 출력 안에 검은 띠와 함께 맞춰 보여 주는지(하이퍼바이저 쪽 작업, 이 패치 밖).
- [가설] 확대 모드의 가상 신호 정보(1080p60 블랭킹을 그대로 붙인 값)를 Windows가 받아들이는지. HDMI 신호 자체는 하이퍼바이저가 1080p로 유지한다는 전제.
- [가설] 저장된 화면 설정이 확대 모드인 채로 복사 방식(서비스 없는 하이퍼바이저나 `NwSoftForce`=1)으로 부팅하면, 확대 모드 커밋이 거절되고 Windows가 1920x1080으로 되돌아갈 것으로 예상. 원본에서 CAP 없는 브로커와 같은 상황.
- 절전·화면 끄기(P19·P20)와 모드 변경이 겹칠 때의 동작은 시험하지 않음.

P21 실기기(2026-11-16, g253K): Windows 디스플레이 설정에 8개 해상도가 보였고, 1280x720(화면 가득)·1024x768(양옆 필러박스) 모두 정상 표시(콘치님 확인).

## P22 (NWOAS-J274, 2026-11-18) — 풀 창이 없을 때 일반 버퍼 한도를 16 MiB에서 구역 2 크기로

결론: **풀 창 없이(`NWOAS_AGX_POOL_IPA=0`) 도는 지금 구성에서는 D3D12 힙·버퍼·텍스처가 16 MiB를 넘으면 모두 E_OUTOFMEMORY였습니다.** P22는 그 한도를 일반 버퍼가 실제로 놓이는 구역 2의 크기(952 MiB)로 바꿉니다. J274 빌드에서만 적용되고, J313 빌드와 풀 창이 있는 구성은 원본과 같습니다. 판 2758.

| 파일 | 변경 |
|---|---|
| `render-admission/src/allocation_windows.c` `AdmissionGeneralMaximumBytes` | 풀 창이 있으면 원본처럼 풀 크기. 없으면(J274만) `Context->Memory.LocalAllocationBytes`(구역 2, 64 KiB 정렬)를 돌려줌. 원본은 이 경우 16 MiB(`ADMISSION_WIN32_ALLOCATION_LEGACY_MAX_BYTES`) |

왜 막혔나 [사실, 코드]:

| 단계 | 위치 | 하는 일 |
|---|---|---|
| 1 | `lifecycle.c:723` | UMD에 일반 버퍼 최대 크기로 `AdmissionGeneralMaximumBytes`를 알림 |
| 2 | `allocation_windows.c:81-92`(P21) | 풀 창이 없으면 16 MiB |
| 3 | `mesa/winsys/agx_win32_screen.c:114` | `Bytes > MaximumBytes`면 KMD를 부르기도 전에 거절 |
| 4 | `mesa/winsys/agx_d3d12_windows.cpp:1192` `AgxD3d12OwnerCreateBuffer` | `resource_create` 실패 → `E_OUTOFMEMORY` |
| 5 | `d3d12/agx_d3d12_ddi.cpp:2175` `CreateHeap` | 그 값을 그대로 돌려줌. D3D11 텍스처·버퍼도 같은 3단계에서 막힘 |

- 같은 때 DXGI는 전용 메모리 약 1 GB(구역 2 = 952 MiB)를 알려 주므로, 앱은 큰 힙을 만들려 합니다.
- 구역 2는 16 KiB 단위로 연속이고 처음부터 GPU 주소표에 모두 올라가 있습니다(`memory_runtime_windows.c` 시작 경로). 하이퍼바이저 GPU 주소 중개도 1 GiB까지 받습니다(`m1n1-gpu-hv2/src/hv_agx_gpuva_v5.h` `HV_AGX_GPUVA_V5_LOCAL_PAGES` 65536 × 16 KiB). 그래서 하이퍼바이저는 바꾸지 않았습니다.
- 셰이더·인코더 버퍼 한도는 그대로입니다.

빌드(Parallels VM, `build-p22.sh`, 실행 이름 j274g3v): `/W4 /WX` 통과, 경고 줄 수 2757과 같음(97), 테스트 서명(WDKTestCert, 2757과 같은 인증서), InfVerif 통과. VM 쪽 signtool 확인의 "신뢰하지 않는 루트"는 2757과 같은 결과입니다(맥미니는 이 인증서를 신뢰함).

| 파일 | 크기 (B) | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 647880 | `6d83224d7cf6165d8c5947c36719d8655fa53288a2699cea5a3eeb85a6303ec2` |
| AppleAgxRenderAdmissionUmd.dll | 13502976 | `1a04727132f8e4c7d691406ebe47450fdc787235b9885b0e4a8a41fdcbfbd27c` |
| AppleAgxRenderAdmission.arm64only.inf | 3613 | `9c5d30ae7ae69e72ee8920300ec56caf80050a7d832b2f30329116fe62f1c055` |
| appleagxrenderadmission.cat | 3254 | `9c9006d578e697f134fa59b6478065560ee25e6cabf3235f77ea6c53dc4deb1c` |

결과물: `out/j274-full-20261118-1816-p22`. 차이: `P22-driver.diff`(2757 소스 묶음 대비, 이 파일 하나).

하드웨어에서만 알 수 있는 것:

- [가설] Cinebench 2026 UI가 요청하는 힙이 16 MiB보다 큼. 확인: `nwoas_scripts/gpucov/jobs/K4-cb26-ui.cmd`의 UMD 기록 `measure-d3d12-heap hr=0x8007000e`의 두 번째 값 × 64 KiB가 요청 크기.
- [가설] 큰 버퍼가 구역 2를 많이 차지하면 VidMm이 바탕화면 표면을 시스템 메모리로 내보냅니다. 그 내보내기·다시 들이기 경로가 큰 크기에서도 맞는지는 `K3-fill.cmd`(64 MiB × 최대 16개)로 확인해야 합니다.
- 952 MiB는 여러 앱을 함께 쓰기에는 작습니다. 진짜 해결은 풀 창(구역 3, 수요 기반 1 GiB+)이고, 하이퍼바이저 g248C 보호가 실기기에서 검증된 뒤에 켭니다(`research-20261118/gpu/GPU-REAL-DRIVER-PLAN.md`).

## P23 (NWOAS-J274, 2026-11-18) — 풀 구역이 없으면 D3D12의 "CPU 풀 직접 매핑"을 끔

결론: **2758 실기기(18:52)에서 Cinebench가 여전히 E_OUTOFMEMORY였던 진짜 원인은 D3D12 장치 내부 객체(native owner)가 처음 CPU 매핑에서 망가진 것입니다.** 그 매핑 방식은 "모든 쪽이 풀(구역 3)에 있음"을 증명해야 하는데, 풀 창이 없는 J274 구성에서는 절대 통과할 수 없습니다. P23은 KMD가 풀 유무를 UMD에 알리고, UMD는 풀이 있을 때만 그 방식을 기본으로 켭니다. 판 2759(P22 포함).

UMD 기록 풀이 (2758, Cinebench 프로세스, 시각 순서) [사실, 소스 대조]:

| 줄 | 값 | 뜻 | 소스 |
|---|---|---|---|
| `reject-cpu-pool-map hr=0x8007000e` | 40002340 / 3 / 01540000 / 11 | 할당 0x40002340의 CPU 매핑 증명 실패. 사유 3 = `AppleAgxG3WalkLeafAbsent`(GPU 주소 0x1101540000의 쪽이 GPU 주소표에 없음). 다시 들이기 시도 뒤에도 실패 → E_OUTOFMEMORY | `umd_win32_screen.c:1591` → `umd_gpuva_windows.c:2310`, 사유 표 `apple_agx_g3_copy_query_receipt.h:20` |
| `reject-batch hr=0x80004005` | 4 / 0x103e6 | 종류 4 = Asahi 백엔드 실패. 위치 = 파일 1(`agx_win32_asahi_bo.c`) 998줄 `native_map`의 `AgxWin32NativeBoMap` 실패. 이 뒤로 이 장치의 모든 버퍼 생성·매핑이 NULL | `agx_d3d12_windows.cpp:486`, `agx_win32_asahi_bo.h:148-152` |
| `measure-d3d12-heap-refused hr=0x8007000e` | 3 / 1 / 0x98a | 예비 경로(소유 객체 없음)의 거절 사유 3 = 크기 초과. CPU에서 보이는 힙, 0x98a × 64 KiB = 152.6 MiB. 예비 경로의 CPU 힙 한도는 UMD 상수 16 MiB(`kCpuVisibleMaximum`) | `agx_d3d12_ddi.cpp:2214-2224`, `:1768` |
| 성공한 `measure-d3d12-heap` | 0줄 | 정상 경로 힙이 한 번도 없음 → 처음부터 소유 객체가 없었음 | — |

- 이 방식(`DirectCpuPool`, `APPLE_AGX_D3D12_CPU_POOL_MAP`)은 D3D12 소유 객체에서 기본으로 켜져 있었습니다(`agx_d3d12_windows.cpp:1125`). 증명은 `POOL_ONLY` 질의(`gpuva_g3_windows.c:1908`)라 구역 2·1에 놓인 할당은 언제나 실패합니다.
- DWM 등 D3D11 경로는 이 방식을 쓰지 않아 지금까지 문제가 보이지 않았습니다.
- 따라서 2757의 회색 창도 같은 원인입니다. P22(16 MiB → 952 MiB)는 정상 경로에서 필요하지만 이번 실패의 첫 원인은 아니었습니다.

| 파일 | 변경 |
|---|---|
| `shared/include/apple_agx_win32_device_info.h` | 기능 비트 `AGX_WIN32_DEVICE_FEATURE_POOL_SEGMENT`(0x8), 아는 비트 목록에 추가 |
| `render-admission/src/lifecycle.c` | UMD 장치 정보 질의에서 풀 구역이 있으면 그 비트를 켬 |
| `mesa/winsys/agx_d3d12_windows.cpp` | J274: `DirectCpuPool` 기본값 = 풀 비트. 환경 변수 `APPLE_AGX_D3D12_CPU_POOL_MAP`가 정확히 `1`이면 켬, `0`이면 끔. J313은 원본 그대로 |

풀이 없을 때는 D3D12도 D3D11처럼 GPU 전용 기준 사본(구역 2) + 프로세스 메모리 CPU 사본 + 스테이징으로 CPU 쓰기를 옮깁니다(`umd_win32_screen.c:970-976`의 원래 분기).

빌드(`build-p23.sh`, 실행 이름 j274g3w): `/W4 /WX` 통과, 경고 줄 수 97(2758과 같음), WDKTestCert 테스트 서명, InfVerif 통과. 결과물 `out/j274-full-20261118-1859-p23`, 차이 `P23-driver.diff`(2758 소스 묶음 대비).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `471a9ee4b63a789e280120083f9a644147af7ce9f3e8c1d13252d499814848a9` |
| AppleAgxRenderAdmissionUmd.dll | `245ff6f3debc205e24b01c3edc5383abb4aedce7cb6c63a5412f05bbeaf70aee` |
| AppleAgxRenderAdmission.arm64only.inf | `5add533aa1e835f803fecab5e43bed736b16fca4cbdeda33dcd5e852a46a7548` |
| appleagxrenderadmission.cat | `be6aa4d191d2ca599c7778c41171b3c96cfc02212fdd6d73c3406d8bc2513702` |

하드웨어에서만 알 수 있는 것:
- [가설] 풀 없는 스테이징 경로에서 Cinebench 장치가 살아남는지. 2758에서도 재빌드 없이 `APPLE_AGX_D3D12_CPU_POOL_MAP=0`으로 같은 동작을 시험할 수 있음(`gpucov/jobs/K4b-cb26-ui-nopoolmap.cmd`).
- [가설] 152 MiB CPU 힙의 CPU 사본(프로세스 메모리)과 스테이징 비용.
- [가설] 라이선스 창 버튼이 안 보인 것도 같은 원인. 콘솔에 "No supported drawport type found"가 있어 Cinebench가 DirectX 그리기 자체를 포기했음.

## P24 (NWOAS-J274, 2026-11-18) — 기능 질의 기록(진단 전용, 동작 변화 없음)

결론: **Cinebench가 드라이버에 무엇을 물었는지 보려고, 켜야만 동작하는 기록을 넣었습니다.** 환경 변수 `APPLE_AGX_UMD_TRACE_CAPS=1`과 `APPLE_AGX_UMD_TRACE_FILE`이 함께 있을 때만 기록하고, 프로세스당 4000줄까지입니다. 판 2760(P23·P22 포함).

| 기록 줄 | 값 | 위치 |
|---|---|---|
| `measure-caps-d3d12` | 질의 종류, 크기, 답의 앞 8단어 | `d3d12/agx_d3d12_ddi.cpp` `GetCaps` 끝 |
| `measure-caps-d3d12-format` | DXGI 형식, 선택 지원 비트 | `CheckFormatSupport` |
| `measure-caps-d3d12-msaa` | 형식, 표본 수, 플래그, 품질 수준 | `CheckMultisampleQualityLevels` |
| `measure-caps-d3d11-adapter` | 런타임 Interface, Version | `umd.c` `OpenAdapter10_2` |
| `measure-caps-d3d11` | 질의 종류, 크기, 답의 앞 4단어 | Mesa `GetCaps`를 감싼 함수 |
| `measure-caps-d3d11-device` | Interface, Version, Flags | Mesa `CreateDevice`를 감싼 함수 |

- D3D11 쪽은 Mesa 프런트엔드(`MesaD3d10OpenAdapter10_2`)가 채운 표에서 `pfnGetCaps`·`pfnCreateDevice`만 기록용 함수로 바꿉니다. 기록이 꺼져 있으면 표를 바꾸지 않습니다.
- D3D12 앱의 `CheckFeatureSupport` 대부분은 런타임이 장치를 만들 때 한 번 물어 둔 값으로 답합니다. 그래서 이 기록은 "드라이버가 무엇을 알렸는가"이지 "앱이 무엇을 물었는가" 전체는 아닙니다.
- 빌드(`build-p24.sh`, 실행 이름 j274g3x): `/W4 /WX` 통과, 경고 줄 수 97, WDKTestCert, InfVerif 통과. 결과물 `out/j274-full-20261118-1932-p24`, 차이 `P24-driver.diff`(2759 대비).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `af299924550ff2c90d366e8a839db7b6a7b2b493e55f0f29886b29098942cf31` |
| AppleAgxRenderAdmissionUmd.dll | `5c64ba5734acf8b17e63109c4f0a11b915b6bb5f543a9e0e3fedb786aa23f4ad` |
| AppleAgxRenderAdmission.arm64only.inf | `2012d352600ddf30feb9caaff2469bd7e90f877159e04dc7552a3a5be6018681` |
| appleagxrenderadmission.cat | `cf20d36853b0adb934331f73cbb10d485c601f07dc76f89c3df5bb9f7f2bd72e` |

## P25 (NWOAS-J274, 2026-11-18) — D3D10/11 셰이더 기록과 DXBC 저장(진단 전용)

결론: **Cinebench의 UI는 D3D12가 아니라 D3D11 장치 두 개로 그려집니다(2760 K12: D3D12 그리기 0개).** D3D11 셰이더마다 한 줄씩 남는 기록과, 셰이더 코드를 파일로 저장하는 기능을 넣었습니다. 드라이버 동작은 바뀌지 않습니다. 판 2761(P24·P23·P22 포함).

| 기록·기능 | 값 | 위치 |
|---|---|---|
| `measure-d3d11-shader` (생성 성공) / `reject-d3d11-shader` (실패, 항상 기록) | 단계, 경로(0 TGSI, 1 DXBC→NIR 변환기, 2 없음), 버전 토큰, DWORD 수, FNV-1a 해시, 생성 여부, 변환기 오류 opcode, 오프셋 | VS·GS·PS: Mesa `Shader.cpp` 오버레이 `AgxD3d10CreateShaderState`(`mesa/scripts/build-native-asahi-state.py`). CS·HS·DS: `mesa/d3d11/agx_d3d11_ddi.cpp`. 구현 `mesa/winsys/agx_d3d10_windows.cpp` `AgxD3d10WindowsShaderRecord` |
| DXBC 저장 | `APPLE_AGX_D3D_SHADER_DUMP=1` → `C:\NWOAS\gpucov\dxbc\<단계>-<해시>.bin`(`APPLE_AGX_D3D_SHADER_DUMP_DIR`로 바꿈), 프로세스당 200개, 있는 파일은 덮지 않음 | 같은 함수 |

이미 있던 것(새로 만들지 않음) [사실, 코드]:
- Mesa `SetError`는 모두 `reject-seterror fn=<함수> line=<줄>`로 남습니다(오버레이 `AgxSetErrorWithOrigin`). 셰이더 생성 실패와 D3D11 그리기 거절(`Draw*`의 E_NOTIMPL)도 여기로 옵니다. 2760 Cinebench 기록에는 이 줄이 0개였습니다.
- 대상 없는 그리기는 `reject-no-target`. 0개였습니다.
- `APPLE_AGX_D3D_SHADER_NIR=1`이면 SM4 셰이더도 TGSI 대신 변환기로 보냅니다(비교용).
- `APPLE_AGX_UMD_DDI_TRACE=1`이면 D3D11 `Draw*`·리소스·SRV 호출마다 `ddi-` 줄이 남습니다.

빌드(`build-p25.sh`, 실행 이름 j274g3y): `/W4 /WX` 통과, 경고 줄 수 97, WDKTestCert, InfVerif 통과, VM 소스에 P25 오버레이가 들어간 것 확인. 결과물 `out/j274-full-20261118-1955-p25`, 차이 `P25-driver.diff`(2760 대비).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `6895ecb65d67e4bcf77d39aecdae972749c8766cd52f79141243dd4163e91c03` |
| AppleAgxRenderAdmissionUmd.dll | `576257d6723cf135244e9142a83398d97756bbbeb36983d67005cd754638c2f3` |
| AppleAgxRenderAdmission.arm64only.inf | `e230a10f4c2bc23880e3e2c4fe77e6e81e06e340f62038265e3b2612961d78b1` |
| appleagxrenderadmission.cat | `a59856382bf0ae5aa43838e58766e9b2cbf9a61b09d0b61a70c8b59e20111acb` |

## P26 (NWOAS-J274, 2026-11-18) — D3D11 그리기 상태 기록과 셰이더 입출력 신호 저장(진단 전용)

결론: **2761에서 Cinebench의 D3D11 셰이더 22개(VS 4.0, PS 4.0)는 TGSI 경로와 변환기 경로 모두에서 문제없이 만들어졌고, 버튼은 두 경우 모두 그려지지 않았습니다.** 그래서 셰이더 변환이 아니라 그리기 상태나 자원 문제를 의심하고, 그리기마다 묶인 상태를 남기는 기록을 넣었습니다. `APPLE_AGX_D3D_DRAW_STATE=1`일 때만 동작하며, 꺼져 있으면 함수 표를 바꾸지 않습니다. 판 2762(P25~P22 포함).

| 기록 | 값 |
|---|---|
| `measure-d3d11-blend` | 객체, A2C, 독립 섞기, RT0 섞기·논리 연산 켬, 원본·대상 계수와 연산(색·알파), 논리 연산, RT0 쓰기 마스크, RT1~7 마스크 |
| `measure-d3d11-raster` | 객체, 채우기, 컬링, 앞면 방향, 깊이 바이어스, 깊이 자르기, 가위 켬, MSAA, AA 선, 강제 표본 수, 보수적 래스터 |
| `measure-d3d11-dsa` | 객체, 깊이 켬·쓰기·비교, 스텐실 켬·앞뒤·마스크, 앞면 연산·비교 |
| `measure-d3d11-shader-sig` | 단계, 해시(P25와 같은 방식), DWORD 수, 버전, 입력·출력 수, 항목(시스템 값·레지스터·마스크) 최대 10개씩 |
| `measure-d3d11-drawstate` | 순번, 그리기 종류, 정점 수, 인스턴스 수, 묶인 섞기·래스터·깊이 객체, 표본 마스크, 스텐실 기준값, 뷰포트 0, 가위 사각형 0, 색 대상 수·형식·폭, 깊이 형식, VS·PS 해시, PS SRV0 형식, 도형, 프레임버퍼 크기. 처음 1500개, 그 뒤 50개마다 |
| `<단계>-<해시>.sig` | `APPLE_AGX_D3D_SHADER_DUMP=1`일 때 셰이더 토큰(`.bin`) 옆에 입출력 신호를 저장. 나중에 VM·WARP에서 같은 셰이더를 다시 돌리는 데 씀 |

- 위치: `mesa/d3d11/agx_wddm2_ddi.cpp` `namespace drawstate`. `AgxWddm2_0FillDeviceFuncs` 끝에서 설치합니다.
- 빌드(`build-p26.sh`, 실행 이름 j274g3z): `/W4 /WX` 통과, 경고 줄 수 97, WDKTestCert, InfVerif 통과. UMD 안에 새 기록 이름 문자열이 들어간 것 확인. 결과물 `out/j274-full-20261118-2014-p26`, 차이 `P26-driver.diff`(2761 대비).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `b04ebf49312c06a73252eec35657c22f6a7d9ccbc0bb4313de8bffd682d9b2ff` |
| AppleAgxRenderAdmissionUmd.dll | `f9ec583945226f772307b2b2da207f775cc3de6b65db038c65a9799245256f0a` |
| AppleAgxRenderAdmission.arm64only.inf | `7d9b08b4220306bcab36c97e7a085ae3f511952c10a55c6ea2a02d6b7e673b8d` |
| appleagxrenderadmission.cat | `8622e3b34a1127543663fcd8ab72b533fbb37168dc4f3e11dde109946d71d6f1` |

## P27 (NWOAS-J274, 2026-11-18) — D3D11 상태 비교 스위치(진단 전용)

결론: **2762 실기기에서 Cinebench는 WARP(K18)에서는 버튼까지 모두 보이고 우리 D3D11에서는 안 보였습니다. D3D12는 원인이 아니었습니다(K22).** 어느 상태가 그리기를 안 보이게 만드는지 앱에서 직접 가르려고, 프로세스별로 앱이 요청한 상태를 바꾸는 스위치를 넣었습니다. 켜지 않으면 동작은 2762와 같습니다. 판 2763.

| 환경 변수(=1) | 바꾸는 것 |
|---|---|
| `APPLE_AGX_D3D11_CULL_NONE` | 모든 래스터 상태의 컬링을 끔 |
| `APPLE_AGX_D3D11_FLIP_FRONT` | 앞면 방향(FrontCounterClockwise)을 뒤집음 |
| `APPLE_AGX_D3D11_SCISSOR_OFF` | 가위 사각형을 끔 |
| `APPLE_AGX_D3D11_BLEND_OFF` | 모든 RT의 섞기·논리 연산을 끄고 쓰기 마스크를 0xf로 |

- 하나라도 켜면 기록에 `measure-d3d11-knobs {비트}`가 한 번 남습니다. 위치: `mesa/d3d11/agx_wddm2_ddi.cpp` `namespace drawstate`. 그리기 상태 기록(P26)은 `APPLE_AGX_D3D_DRAW_STATE=1`일 때만 남깁니다.
- 빌드(`build-p27.sh`, 실행 이름 j274g4a): `/W4 /WX` 통과, 경고 줄 수 97, WDKTestCert, InfVerif 통과, UMD 안에 새 문자열 확인. 결과물 `out/j274-full-20261118-2107-p27`, 차이 `P27-driver.diff`(2762 대비).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `98f74ec94cab9e7dab4bc94241d83cb560ec6a466626a2f649487da96181f907` |
| AppleAgxRenderAdmissionUmd.dll | `b698f83c61b85eb79178947ba149b43ec5e147029567430edf5f6a79117bebd7` |
| AppleAgxRenderAdmission.arm64only.inf | `d519027f1d67159192a6120d8dbb50e666d3e9c9c0cd91af4eb7cfe0f47753e9` |
| appleagxrenderadmission.cat | `7dc7423bff5367e7f15fd9535b657f9854c15c4f565f512ebc87d4616bf6d089` |

## P28 (NWOAS-J274, 2026-11-18) — 크기 고정 없는 구형 블릿 화면 내보내기, 원래 해상도를 부팅 화면에서 얻음

결론: **2763 K25에서 구형 블릿 스왑체인 창에 그림이 안 나온 원인은 UMD의 DXGI Blt가 2560x1600(맥북 화면)일 때만 받도록 박혀 있던 것입니다.** 맥미니에서는 창 크기 버퍼로 오는 Blt가 전부 E_INVALIDARG로 거부됐습니다(코드로 확인, 실기기 확인은 2764 K25b). 같은 김에 드라이버 전체에서 화면 크기 숫자를 없앴습니다. 판 2764.

바꾼 것:

| 위치 | 전(2763) | 후(2764) |
|---|---|---|
| UMD `_Blt`(덧씌움 스크립트) | 대상 사각형이 0,0-2560,1600, 플래그 0x8, 두 리소스 모두 프레젠테이션일 때만 | 호출의 대상 사각형(대상 크기로 자름), 플래그 0x1·0x2·0x4·0x8 조합, 아무 텍스처 리소스. Stretch면 GPU로 늘임(선형), 아니면 1:1 |
| UMD `Blt1` | 원본 부분 사각형이면 E_NOTIMPL | 원본·대상 사각형 모두 받음 |
| UMD `_Present`·`Present1` | hDstResource≠0, AllowTearing, 간격 2~5 거부 | 대상이 있으면 원본을 대상 크기로 GPU 블릿한 뒤 대상을 내보냄. Blt/Flip + AllowTearing·AllowFlexibleRefresh·NoScanoutTransform, 간격 0~5 받음 |
| winsys 블릿(`AgxD3d10WindowsPresentationBlt`, CopyResource) | 2560x1600 상자 | 리소스 자신의 크기. 새 범용 함수 `AgxD3d10WindowsResourceBlt` |
| KMD 화면 블릿 형식 | 원본·대상 모두 21(A8R8G8B8) | 대상 22(X8R8G8B8)도 받음(원본 21·22). A8 대상에 X8 원본은 계속 거부 |
| 원래 해상도 | 빌드 상수(J274 1920x1080, J313 2560x1600) | 시작 때 POST 화면 크기를 채택(`AppleAgxScanoutNativeAdopt`). POST가 없으면 스캔아웃 중개자의 QUERY 값 |
| 늘여 보이는 모드 목록 | 보드별 고정 표 | 후보 목록 중 원래 해상도 안에 들어가는 것만. 1920x1080·2560x1600에서는 2763과 같은 목록 |
| 모니터 신호 정보 | 보드별 고정 활성 크기 | 활성 크기 = 모드 크기, 보드별 블랭킹·주사율만 남김. 1920x1080(J274)·2560x1600(J313)에서는 2763과 같은 값 |
| GDI 화면 대상 검사 | 원래 해상도와 같아야 함 | 현재 확정된 모드와 같아야 함 |
| UMD 주 화면·다이렉트 플립 검사 | 고정 모드 표 | 선형 32비트 규칙(너비 16의 배수, 높이 짝수, 8192 이하). 현재 주 화면과 같은 크기여야 하는 짝 검사는 그대로 |

새 기록(UMD 기록, 거부 기록만 켜도 남음): `measure-dxgi-blt`(앞 12번: 플래그, 대상 사각형, 두 리소스 크기·형식), `measure-dxgi-blt1`, `measure-dxgi-present-dst`, `reject-dxgi-present`, `reject-dxgi-present-source`, `reject-dxgi-present-dst`, `reject-dxgi-present-dst-copy`, `reject-dxgi-blt1*`. 기존 `reject-BltDXGI`도 남습니다.

해상도 숫자 감사(드라이버 트리, tests·one-shot·icd 제외):

| 남긴 곳 | 이유 |
|---|---|
| `shared/include/apple_agx_fixed_panel_modes.h` 후보 목록 | 늘여 보이는 모드의 후보일 뿐이고 원래 해상도로 걸러서 냅니다. 모니터가 알려 주는 모드 목록(EDID·DCP)을 KMD가 받는 길이 아직 없습니다 |
| `render-admission/src/display.c` 블랭킹·주사율 | 크기가 아니라 보드별 화면 타이밍입니다(J274 CEA 1080p60 블랭킹, J313 EXP495 측정값) |
| `shared/include/apple_agx_exp208_framebuffer.h`, `shared/src/apple_agx_exp208_framebuffer.c`, `render-admission/src/render_backend_image.c` | macOS에서 잡아 온 2560x1600 GPU 명령 묶음(EXP208)을 다시 돌리는 시험 경로입니다. 크기는 그 명령 묶음의 성질입니다 |
| `render-admission/src/render_dynamic_output.c` | 알려진 시험 렌더(2560x1600·16x16)의 기대값 표입니다 |
| `mesa/compiler-host/agx_vs_fs_fixture.c` | 맥에서만 도는 컴파일러 시험 고정값입니다 |
| `apple_agx_scanout.c` 8192 상한 | D3D 텍스처 최대 크기입니다 |
| 0x640 등(firmware_io, g4_builder, render_dynamic_overlay), 1080·1200·1920·2160(render_template.generated, indirect-stream 셰이더) | 해상도가 아니라 오프셋·배열 길이입니다 |

알려진 한계:
- 실행 중에 다른 원래 해상도의 모니터로 바꿔 꽂으면, 원래 해상도는 부팅 때 값으로 남습니다(DCP가 늘여 보냄). 부팅 때 꽂힌 모니터는 그 크기를 씁니다.
- 하이퍼바이저(m1n1) 쪽 DCP 서비스가 1920x1080 밖의 원래 해상도를 받는지는 이 트리 밖이라 확인하지 않았습니다.
- `tests/`의 호스트 시험(고정 표·상수를 확인하는 것)과 `windows/one-shot/apple_agx_d3dkmt_render.c`는 고치지 않았고 이 빌드에 들어가지 않습니다.
- KMD `DdiPresent`는 여전히 Flags.Value==1(Blt만)일 때만 블릿을 만듭니다.

- 빌드(`build-p28.sh`, 실행 이름 j274g4b): 성공, 판 30.0.2764.0. 결과물 `out/j274-full-20261118-2150-p28`, 차이 `P28-driver.diff`(2763 대비, 15개 파일).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `f03d37865c6c990fe407a2667681e2d9c683cc6c2c845a39932e895aeae845b0` |
| AppleAgxRenderAdmissionUmd.dll | `5e5a04020792a2129204bf6a708d80087381c5214c480e5d15dc2c8b2983ea47` |
| AppleAgxRenderAdmission.arm64only.inf | `9b6d481fa71790e617784e9f59b4bd858e0cade689123666ec99e1fb522cef1e` |
| appleagxrenderadmission.cat | `13ca9e5c3fc2bd12500f266383fbab4d76538ff7973ae1a4b45ffb8274a72791` |

## P29 (NWOAS-J274, 2026-11-18) — 블릿 방식 프레젠트가 끊기는 곳을 찾는 기록(진단 전용)

결론: **2764 실기기에서 DXGI는 블릿 스왑체인을 UMD `pfnPresent`(Blt, 대상 없음)로만 보냈고, UMD는 `pfnPresentCb`에서 S_OK를 받았습니다. 그런데 KMD `DdiPresent`에는 Blt가 오지 않았습니다.** dxgkrnl이 창의 리디렉션 표면으로 옮기는 단계에서 끊기므로, 그 단계에 걸리는 DDI를 모두 기록하도록 했습니다. 동작은 2764와 같습니다. 판 2765.

| 기록 | 위치 | 내용 |
|---|---|---|
| `Wom1PresentDdiRing` | KMD `DdiPresent` | 최근 16번: pid, 컨텍스트 플래그, 프레젠트 플래그, 원본·대상 수, 포인터 비트, 사각형, 원본·대상 {w,h,형식,종류}, 결과. DWM의 MMIO 플립은 수만 셉니다 |
| `Wom1StdAllocRing` | `GetStandardAllocationDriverData` | 모든 호출: 종류, 표면 {w,h,형식,GDI 종류,GDI 플래그}, 결과 |
| `Wom1OpenAllocRing` | `OpenAllocation` | 처음 48번과 모든 실패: pid, 수, 우리 설명 {w,h,형식,종류,피치} |
| `Wom1CreateAllocRing` | `CreateAllocation` | 실패만: pid, 플래그, 사설 데이터 앞 8단어 |
| 레지스트리로 켜는 UMD 기록 | `HKLM\SOFTWARE\NWOAS\AgxUmdTrace` (Dir, Process, RefusalsOnly) | 환경 변수를 줄 수 없는 프로세스(dwm.exe, Edge GPU 프로세스). 2초마다 확인, 키를 지우면 꺼짐, 파일당 16 MiB 상한 |
| `measure-dxgi-entry_*`, `measure-dxgi1-*` | UMD DXGI 진입점 전부 | 각 함수의 첫 8번 |

- 기록 링은 PASSIVE에서 처음 48번, 그 뒤는 실패(최대 64번 더) 또는 1초에 한 번만 레지스트리에 씁니다. 반환값은 바꾸지 않습니다.
- 빌드(`build-p29.sh`, 실행 이름 j274g4c): 성공, 판 30.0.2765.0. 결과물 `out/j274-full-20261118-2215-p29`, 차이 `P29-driver.diff`(2764 대비, 6개 파일).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `fc34e4f9ed7571ba40f467877607f3260cec3747312a1db24fa18e7e936ad9b7` |
| AppleAgxRenderAdmissionUmd.dll | `7d92c273948cccc4146e5ed76b9070e19f85e717141a804799e03467b5deaba5` |
| AppleAgxRenderAdmission.arm64only.inf | `4a7e333a34bad865cf4b79e9232dae333c7d08c0e599bc9a58b346439d5186ed` |
| appleagxrenderadmission.cat | `7a06ba2383664588d71920abce1a5565a053116de5059eb9c6dc9d7d6177e300` |

## P30 (NWOAS-J274, 2026-11-18) — 스테이징 표준 할당 지원

결론: **2765 `Wom1StdAllocRing`에서 `GetStandardAllocationDriverData` 112번 중 108번이 STAGINGSURFACE(종류 3) 요청이었고, 전부 c000000d로 거부됐습니다(pid 0x290).** 우리 KMD는 공유 주 화면·그림자·GDI 표면 세 종류만 받았습니다. 판 2766에서 스테이징 표면을 받습니다.

- 스테이징 표면(`pCreateStagingSurfaceData`: 너비·높이 입력, 피치 출력): 주어진 크기의 선형 32비트, CPU에서 보이는 표면입니다. 형식은 현재 확정된 모드 형식이고, 없으면 A8R8G8B8입니다. 배치는 그림자 표면과 같습니다(종류 `STAGING_CPUVISIBLE`, 클래스 0의 CPU 가시 할당은 aperture 세그먼트, EXP837-839). 피치는 16바이트 정렬이고 크기는 피치×높이입니다.
- Lock DDI는 추가하지 않았습니다. WDDM 2 GPUVA에서 CPU 가시 aperture 할당은 dxgkrnl이 직접 대응시키고, 같은 배치의 그림자 표면이 이미 이 경로를 씁니다.
- GPUVA 프레젠트 블릿(`AdmissionPresentBltExecuteGpuvaSeparate`)은 KMD가 CPU로 페이지 표를 따라 복사하므로, 대상이 aperture여도 GPU 작업이 되지 않습니다.
- 기록 링(`Wom1StdAllocRing`)이 스테이징 표면의 너비·높이·피치도 남깁니다.
- 빌드(`build-p30.sh`, 실행 이름 j274g4d): 성공, 판 30.0.2766.0. 결과물 `out/j274-full-20261118-2226-p30`, 차이 `P30-driver.diff`(2765 대비).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `3aa3d9c615b0e860f1ae2f3d546c424aae3b3759c4fb0f99fba77da40fa38196` |
| AppleAgxRenderAdmissionUmd.dll | `7ad9aaab7f86be5976be6a75eddb978a0fac1498c2990e4ba814eecca5893e00` |
| AppleAgxRenderAdmission.arm64only.inf | `daea6d8bf26be0601c19d2348ec57076f6eb499694175ada81f5cecd0efdbab5` |
| appleagxrenderadmission.cat | `e77a75f5cbabdd96d287a4522c14e7ec3702da2369e27214c17d491b046d9bdd` |

## P31 (NWOAS-J274, 2026-11-18) — 레지스트리로 켜는 UMD 기록 고침(진단 전용)

결론: **2765·2766에서 레지스트리 키는 64비트 보기에 제대로 있었는데 DWM·Edge 기록 파일이 하나도 생기지 않았습니다.** 기록은 64 KiB 버퍼에 모았다가 250 ms가 지나거나 거부 줄이 나올 때만 파일에 씁니다. 그래서 FATAL로 죽는 Edge GPU 프로세스는 버퍼를 비우지 못했고, DWM의 프레젠트 측정은 2의 거듭제곱 번째 호출에만 남아 오래 돈 DWM에서는 줄이 거의 안 나왔습니다 [코드로 본 설명, 실기기 확인은 2767 K34]. 판 2767.

- 레지스트리로 켜지면 모든 줄을 바로 씁니다(`APPLE_AGX_UMD_TRACE_SYNC`와 같은 동작).
- 켜지는 순간 파일에 `measure-trace-armed registry` 한 줄을 씁니다. 빈 실행에서도 키를 읽었는지 알 수 있습니다.
- `Dir`에 파일을 만들 수 없으면 그 프로세스의 `%TEMP%`에 같은 이름으로 씁니다.
- 빌드(`build-p31.sh`, 실행 이름 j274g4e): 성공, 판 30.0.2767.0. 결과물 `out/j274-full-20261118-2245-p31`, 차이 `P31-driver.diff`(2766 대비, umd_runtime_device.c 하나).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `ff6548ee918b1054d7d896799a9e83d12fab6b8e05b24b1566110dd64e9bcf5f` |
| AppleAgxRenderAdmissionUmd.dll | `7f94aff1ef60ef6e7e4005cbde3c5bbbe4470b4476d18ff7328aeb6fdd126558` |
| AppleAgxRenderAdmission.arm64only.inf | `ac40372ca8ec392c6eedb5e96321190579862c6c23bfdd1cf23f7b2bac5a41c9` |
| appleagxrenderadmission.cat | `458bc5e3821a0996b39512ecb8ca5a4ae2010ea42e76f7b2884563e25196858c` |

## P32 (NWOAS-J274, 2026-11-18) — D3D11.1 공유 형식 지원, 생성 실패가 장치 제거로 번지지 않게 함

결론: **2767 K35에서 공유 R10G10B10A2 텍스처 생성이 거부되었고, 그 거부를 E_INVALIDARG로 런타임에 알려서 장치가 제거됐습니다. 그 뒤 모든 호출이 DXGI_ERROR_DEVICE_REMOVED였습니다.** Chromium GPU 프로세스가 죽는 모양과 같습니다 [가설, 2768 K34·K23로 확인]. 판 2768.

| 바꾼 것 | 전 | 후 |
|---|---|---|
| 공유·프레젠트 텍스처 형식(umd.c `AdmissionUmdSharedStorage`) | BGRA8, RGBA8, A8 | + BGRX8, RGB10A2, RGBA16F(8바이트), R8, R8G8, sRGB 변형 |
| KMD 할당 설명(render_allocation.c) | 픽셀당 4바이트 이하 | 1·2·4·8바이트 |
| 공유 리소스 열기 크기 상한 | 16 MiB | 설명에서 계산(피치×높이) |
| winsys 선형 가져오기 | BGRA8·RGBA8 | 위 형식 전부, GPU가 그 형식을 그리고 읽을 수 있을 때만 |
| 프레젠트 가능 판정 | BGRA8(화면 표시)·RGBA8 | 그 밖의 형식도 화면 표시 없이(DWM 합성) |
| Create*·Open* DDI 오류 | 그대로 전달(E_INVALIDARG면 장치 제거) | E_OUTOFMEMORY로 바꿔 그 호출만 실패, `reject-create-error-as-oom {줄, 원래 hr}` 기록 |

- 남긴 제한: 공유 텍스처의 밉 2개 이상·배열 2장 이상·MSAA·UAV 바인드, NV12·P010 같은 평면 형식. 선형 단일 평면 저장으로는 표현하지 못합니다. 이제는 장치 제거 없이 그 호출만 E_OUTOFMEMORY로 실패합니다.
- 생성 외 DDI의 SetError 감사(생성된 프런트엔드 소스 기준): ResourceCopy·CopyRegion·Map·Unmap·UpdateSubresourceUP·ResolveSubresource·GenMips·Clear*View·SetSamplers·SetShaderResources·SoSetTargets·Destroy* 등이 E_NOTIMPL·E_INVALIDARG를 씁니다. 이 DDI들은 허용된 오류가 없어 어떤 오류든 장치 제거입니다. 조용히 넘기면 화면이 틀린 채로 남으므로 바꾸지 않았고, 모두 `reject-seterror` 줄을 남깁니다.
- 빌드(`build-p32.sh`, 실행 이름 j274g4f): 성공, 판 30.0.2768.0. 결과물 `out/j274-full-20261118-2303-p32`, 차이 `P32-driver.diff`(2767 대비, 7개 파일).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `3f17bd26ce9f0d875a9ead279de21e3ded54577eb45c9dbcfa8e75bf4ad9a202` |
| AppleAgxRenderAdmissionUmd.dll | `54763d89a474263f5704b8520c61ea2a8d58551ccfaeb5706201c6305658492f` |
| AppleAgxRenderAdmission.arm64only.inf | `9de093957c2a8992c1fada11a971353b9836d73c3ff40f39aadb8cba5298a167` |
| appleagxrenderadmission.cat | `f743eeb7a06891bbf8e22c752ed3bd8cbfa3eaa38e368013c6edb554275dfa42` |

## P33 (NWOAS-J274, 2026-11-18) — RGB10A2·FP16 스왑체인 버퍼

결론: **2768 K35에서 RGB10A2·FP16 텍스처와 공유 텍스처는 통과했지만, 그 형식의 플립·DComp 스왑체인 버퍼 생성은 거부됐습니다(원래 hr 0x80070057, CreateResource 746번 줄 → E_OUTOFMEMORY).** DXGI는 플립 모델 버퍼마다 DISPLAYABLE_SURFACE 표시를 붙이는데, UMD가 이 표시를 BGRA8과 BIND_PRESENT가 있는 RGBA8에만 허용했습니다. 판 2769.

- BIND_PRESENT가 있으면 공유 가능한 모든 형식이 DISPLAYABLE 표시를 가질 수 있습니다. 화면에 직접 내보내는(Displayable 1) 할당은 여전히 BGRA8뿐이고, 나머지는 DWM이 합성합니다.
- 빌드(`build-p33.sh`, 실행 이름 j274g4h): 성공, 판 30.0.2769.0. 결과물 `out/j274-full-20261118-2320-p33`, 차이 `P33-driver.diff`(umd.c 하나).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `606b18e70f802783e621bf8929107eb36102ca69cd3a4db8bcdeccbeed20e9dc` |
| AppleAgxRenderAdmissionUmd.dll | `2eb40e50499763bdc387e11bfba158957870d29d4d876087fc87109cfc3e876f` |
| AppleAgxRenderAdmission.arm64only.inf | `f4e2af14b8b74eb2353e36baf45f6d06d3cf514efb8618959e67a38f9f5d8fb0` |
| appleagxrenderadmission.cat | `4578b426f83ca06696223d3ded147da32de991f52301ce0bc25ca7da0b0c9345` |

## P34 (NWOAS-J274, 2026-11-19) — Edge FATAL 원인 시험용 MSAA 정책 0x80, 레지스트리 기록 흔적(진단 전용)

결론: **Edge GPU 프로세스의 FATAL(`skia_output_surface_impl.cc:1228`, `backend_format.isValid()=0`)은 우리 D3D11 UMD가 어떤 형식에도 MULTISAMPLE_RENDERTARGET을 보고하지 않아서 생긴다고 봅니다** [소스 코드로 이은 가설, 실기기 확인은 K37m·K37f]. 그 사슬은 다음과 같습니다.

| 단계 | 근거 |
|---|---|
| 우리 D3D11 형식 답에 MULTISAMPLE_RENDERTARGET(0x200000)이 없음 | 2769 K35: BGRA8 0x11def3f3, RGBA8 0x13def3f3, RGB10A2 0x13def3f3, FP16 0x12def3f3. WARP는 넷 다 이 비트가 있음 |
| 원인 코드 | CheckFormatSupport 덧씌움이 이 비트를 지운 뒤 `APPLE_AGX_D3D_MSAA_CAPS` 정책이 켜졌을 때만 다시 넣음. 기본값 0 |
| ANGLE D3D11은 이 비트가 없으면 GL 샘플 수 목록을 비움 | renderer11_utils.cpp GenerateTextureFormatCaps("Assume 1x"는 비트가 있을 때만) |
| ANGLE `GL_NUM_SAMPLE_COUNTS` = 목록 크기 → 0 | libANGLE queryutils.cpp |
| Skia는 그 값이 0이면 그 형식을 렌더 불가로 둠 | GrGLCaps::setupSampleCounts(`if (count)` 밖에서는 채우지 않음) → 기본 백엔드 형식 무효 |
| Chromium이 무효 형식에서 FATAL | 로그의 `backend_format.isValid()=0`와 일치 |
| D3D11On12 경로는 됨 | 우리 D3D12 UMD는 2x·4x와 MULTISAMPLE_RENDERTARGET을 보고(agx_d3d12_ddi.cpp C32). 콘치님 눈 확인: `--use-angle=d3d11on12`에서 Edge가 그려짐 |

- 새 정책 0x80: 렌더 대상 형식(그리고 깊이 형식과 typeless 부모)에 MULTISAMPLE_RENDERTARGET 비트만 넣고 2x·4x 단계는 주지 않습니다(ANGLE이 1x만 가짐). EXP1288의 런타임 규칙(부모보다 많은 단계 금지, 읽기 전용 형식에는 비트 금지)을 지킵니다. 기본값은 바꾸지 않았고, 환경 변수로만 켭니다.
- 기존 정책 0x20 + `APPLE_AGX_D3D_SCREEN_MSAA=1`은 실제 2x·4x입니다(윈도우즈 온 M1 EXP1310에서 4x 렌더 통과). 이것도 환경 변수로 시험합니다.
- 레지스트리로 켜는 UMD 기록이 어떤 프로세스에서도 켜지지 않았습니다(2765~2769). VM 단독 시험에서는 같은 코드가 켜졌습니다. 그래서 `C:\NWOAS\umdtrace-seen` 폴더가 있을 때 각 프로세스가 어느 단계에서 멈췄는지(0 시작, 1 Dir 읽기 실패+오류, 2 Process 읽기 실패, 3 이름 불일치, 4 켜짐, 5 파일 실패) 남기게 했습니다. Edge 기록은 환경 변수로도 받습니다(Edge 자식 프로세스가 물려받음).
- 빌드(`build-p34.sh`, 실행 이름 j274g4j): 성공, 판 30.0.2770.0. 결과물 `out/j274-full-20261119-0021-p34`, 차이 `P34-driver.diff`(4개 파일).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `928018a1b1d39e7f8b15de2317bd9066a939448cc406f3656e5818120181b4ac` |
| AppleAgxRenderAdmissionUmd.dll | `a8cf1eac5dc965af050fc347600a5bab62b479a77680af002ec1716e87de987e` |
| AppleAgxRenderAdmission.arm64only.inf | `01cb7cf08ee6af544617b8853300d65cfbe83e52d4da33af7eb0d3317410803d` |
| appleagxrenderadmission.cat | `e25bd57a713941af9f77e8e869dcd5d1f5d7312e0cb300cfc3db61ec2e480c5c` |

## P35 (NWOAS-J274, 2026-11-19) — D3D10/11 MSAA 기본 켜기, 되돌리는 레지스트리 스위치

결론: **2770 K37f에서 정책 0x20 + 화면 MSAA로 Edge 기본 경로(ANGLE D3D11)가 우리 드라이버에서 FATAL 없이 돌았습니다(fatals=0, 충돌 보고 0, WARP로 내려가지 않음).** 그래서 이 둘을 기본값으로 켰습니다. 정책 0x80(비트만)은 장치 생성이 0x887a0020으로 실패해(K35m) 버렸습니다. 판 2771.

| 항목 | 2770 | 2771 |
|---|---|---|
| CheckFormatSupport / 품질 단계 | 정책 0(MSAA 없음) | 정책 0x20: BGRA8·BGRX8·D24S8·D32S8·RGBA8·RGBA16·R32·R16 계열, 2x·4x, 읽기 전용 형식은 없음 |
| 화면의 다중 샘플 렌더 대상 | 꺼짐 | 켜짐 |
| 되돌리기 | — | `HKLM\SOFTWARE\NWOAS\AgxD3d` `D3d11Msaa`=0(REG_DWORD)이면 2770과 같음. DWM 포함 모든 프로세스, 5초마다 다시 읽음. 작업 D60(끄기)·D61(켜기)이 값을 쓰고 GPU 장치를 다시 시작 |
| 프로세스별 덮어쓰기 | 환경 변수만 | `APPLE_AGX_D3D_MSAA_CAPS`, `APPLE_AGX_D3D_SCREEN_MSAA=0/1`이 레지스트리보다 우선 |

- EXP1285 회귀 위험: 그때 DWM이 반복해서 멈춘 원인은 형식 답과 품질 단계가 서로 어긋난 것이었습니다(EXP1285~1289). 정책 0x20은 그 규칙을 지키고, EXP1290부터 런타임이 받아들였으며, EXP1310부터 4x 렌더가 맞습니다. 2770 K35f(FL 11_1)도 장치 생성과 2x·4x 시험을 통과했습니다. DWM에서는 아직 시험하지 않았으므로 K41로 봅니다.
- 0x80 코드는 지웠습니다.
- 빌드(`build-p35.sh`, 실행 이름 j274g4k): 성공, 판 30.0.2771.0. 결과물 `out/j274-full-20261119-0042-p35`, 차이 `P35-driver.diff`(3개 파일).

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `2aac81ee9178491c289a9ad6389b52570e2d19e8bea7189151deaf3cf8a96e3c` |
| AppleAgxRenderAdmissionUmd.dll | `04ae6a91d4f29c95306795666b1fedadd29d4df537a7eace643526c8b2878710` |
| AppleAgxRenderAdmission.arm64only.inf | `1b31d5ece1eaea93920de97e43d730a0bfa05581688b2961de2e9898fd8b6f2b` |
| appleagxrenderadmission.cat | `cb84d8d5f2d9f22ef405804422947e0fc0c19cf13dc7e275bba3f6cd58ec4466` |

## P36 (NWOAS-J274, 2026-11-19) — 표준 다중 표본 패턴(Quality 0xffffffff) 허용

결론: **2771에서 Edge GPU 프로세스가 처음 한 번 사라졌다 다시 뜨는 원인은, ANGLE이 4x 렌더 대상을 `SampleDesc.Quality = D3D11_STANDARD_MULTISAMPLE_PATTERN`(0xffffffff)으로 만들고, 우리 UMD가 Quality 0만 받아 E_NOTIMPL로 거부한 것입니다** [사실, 기록·공개 소스 대조]. 판 2772에서 D3D11 렌더 대상·깊이와 D3D12 2D 텍스처가 이 값을 품질 0과 같게 받습니다 [빌드만].

| 단계 | 근거 |
|---|---|
| ANGLE이 표준 패턴을 요청 | ANGLE `Renderer11::getSampleDescQuality`: 표본 수 > 0이고 FL 10_1 이상이면 `D3D11_STANDARD_MULTISAMPLE_PATTERN` (`createRenderTarget`, `TextureStorage11` 두 곳에서 사용) |
| 런타임이 그 값을 DDI에 그대로 넘김 | 2771 K38t `reject-d3d11-render-target` 값: 형식 0x1c(RGBA8), 바인드 0x28(RT+SRV), 단계 1, 배열 1, `ffffff04`(표본 4, Quality 0xffffffff), 차원 3(2D) |
| 우리 UMD가 거부 | `build-native-asahi-state.py` 렌더 대상 조건 `SampleDesc.Quality == 0` → E_NOTIMPL → P32의 `reject-create-error-as-oom` → E_OUTOFMEMORY |
| ANGLE → WebGL | `ResourceManager11::allocate:492` `ANGLE_TRY_HR` → `GL_OUT_OF_MEMORY`(로그의 `HRESULT: 0x8007000E`) |
| GPU 프로세스 종료 | Chromium `GpuChannelManager::OnContextLost`: 패스스루 디코더(ANGLE)면 `force_restart` → `MaybeExitOnContextLost`. 로그에서 OOM 0.15초 뒤 새 GPU 프로세스(`--gpu-recent-crash-count=1`), 충돌 보고 0 |

- 표본 위치: Asahi Gallium 드라이버는 `u_default_get_sample_position`(D3D·Vulkan 표준 위치)을 씁니다(VM의 Mesa 9aa1215f `agx_pipe.c:1812`). 그래서 표준 패턴을 품질 0과 같은 것으로 받습니다. 중앙 패턴(0xfffffffe)은 여전히 거부합니다.
- D3D12(`TextureBytes`)도 같은 값을 받게 했습니다. K38d(`--use-angle=d3d11on12`)의 첫 GPU 프로세스 OOM이 같은 원인이라는 것은 [가설]입니다(그 실행의 D3D12 할당 기록은 걸러져 보이지 않았음). K38dg로 확인합니다.
- 품질 단계 질의(CheckMultisampleQualityLevels)·형식 답은 2771과 같습니다. KMD 소스는 바꾸지 않았습니다(.sys 해시는 판 번호 때문에 바뀜).
- 빌드(`build-p36.sh`, 실행 이름 j274g4l): 성공, 판 30.0.2772.0. 결과물 `out/j274-full-20261119-0346-p36`, 차이 `P36-driver.diff`(2개 파일). 생성된 `Resource.cpp`에 P36 줄 3개 들어간 것을 VM에서 확인. InfVerif는 2771과 같은 ERROR(1322)(UMD를 System32에 복사, DIRID 13 아님)를 냅니다 — 2771에도 있던 것이고 pnputil 설치는 됐습니다. signtool verify 오류 1(시험 서명, VM에 신뢰 루트 없음)도 2771과 같습니다.

| 파일 | sha256 |
|---|---|
| AppleAgxRenderAdmission.sys | `cac0dabb81b712f5e375bb4ed22118244a529c58904858be01df3e1c04f059f5` |
| AppleAgxRenderAdmissionUmd.dll | `8647b5df6627d2b9b2f795e043ed4a308576fb65d67d45836a4505c48b96b496` |
| AppleAgxRenderAdmission.arm64only.inf | `bf89c9c42503987842b79a8a30d2daca1c0dabf216e890b44c68f4817638e96d` |
| appleagxrenderadmission.cat | `15144633a9b74b79c62e79bb9a0a0577fb46c8042b0b86dd209a3d0d35f029d9` |

## P37 (NWOAS-J274, 2026-11-19) — x64 UMD + ARM64X 전달 DLL (Prism으로 도는 x64 앱용)

결론: **판 2773은 2772의 KMD 소스·ARM64 UMD 소스를 그대로 두고, 같은 UMD 프로젝트를 x64로 빌드한 `AppleAgxRenderAdmissionUmdX64.dll`과, 이름이 기존 UMD와 같은 ARM64X 전달 DLL(`AppleAgxRenderAdmissionUmd.dll`)을 더한 패키지입니다** [빌드만]. 실기기에서 x64 앱이 이 UMD로 도는지는 아직 보지 않았습니다(K42로 봄). 자세한 근거·시험 명령은 `RESULT-2773.md`.

| 바뀐 것 | 내용 |
|---|---|
| `umd/AppleAgxRenderAdmissionUmd.vcxproj` | `Release\|x64` 구성 한 줄 추가. ARM64 구성은 그대로 |
| `scripts/build-umd-x64-msbuild.ps1` (새 파일) | build-driver.ps1의 ARM64 UMD 호출과 같은 속성으로 `/p:Platform=x64`만 바꿔 빌드, x64 PE 확인, CRT 단언 검사 |
| `AppleAgxRenderAdmission.arm64x.inf` (새 파일) | P9 INF + System32에 UmdArm64·UmdX64 두 파일 추가, 장치 이름 끝 "(NWOAS ARM64+x64)". UserModeDriverName 값은 그대로 |
| `mesa/scripts/compiler-x64.ini`, `configure-x64-nir.ps1` (새 파일) | ARM64판과 같은 Mesa 옵션의 x64 교차 구성(`--target=x86_64-pc-windows-msvc`, -NDebug) |
| VM 단계 (`agx-build/scripts`) | 05x(x64 NIR + dxil-spirv x64, 한 번만), 08x(x64 컴파일러 증거·네이티브 런타임), 11(x64 UMD + 전달 DLL), 12(패키지·서명·수집) |

- ARM64·x64 UMD 컴파일 정의 비교(빌드 로그): 아키텍처 정의(`ARM64`/`_ARM64_` 대 `AMD64`/`_AMD64_`/`_WIN64`)만 다르고 나머지 18개는 같음.
- x64 Asahi 셰이더 컴파일러(시험 셰이더 2개)를 VM에서 에뮬레이션으로 실행한 출력이 ARM64 컴파일러 출력과 바이트까지 같음(variant0 `8f5ad95c…`, variant1 `a6a1a83b…`).
- 실제 2773 DLL로 VM에서 전달 시험: ARM64 프로세스는 UmdArm64, x64 프로세스는 UmdX64가 두 내보내기(OpenAdapter10_2, OpenAdapter12)를 맡음 [사실, VM].
- InfVerif: ERROR(1322) 3건(UMD 세 파일이 System32, DIRID 13 아님). 2772의 같은 오류 1건이 3건이 된 것이고 pnputil 설치는 2772까지 이 상태로 됐음. signtool verify 오류 1(시험 인증서, VM에 신뢰 루트 없음)은 2772와 같음.
- 빌드(`build-p37.sh`, 실행 이름 j274g4m): 성공, 판 30.0.2773.0, 소스 커밋 `734dfd79`. 결과물 `out/j274-full-20261119-0458-p37`, 차이 `P37-driver.diff`.

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 651496 | `28cb665ecded4afc07f38f599c80af6d5410b9dc9697ae6b00c95e0006ea667a` |
| AppleAgxRenderAdmissionUmd.dll (ARM64X 전달) | 14848 | `6f10255356dd2956864f5bfdb8a59c25fe0501d3474459f6f66b04f9db562fbd` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13522432 | `71e99344179800b6993691643fec1e88ef37de0d06ad8187a11934fad5132a58` |
| AppleAgxRenderAdmissionUmdX64.dll | 14092288 | `712d067f0db4fc0684a9ff6508b94582286f746547d648b2c4c68ff633b1b064` |
| AppleAgxRenderAdmission.arm64x.inf | 4290 | `56fd01224e4e60dea1d1a7bbc665c7f293c100886af90eb6b483e8fa10decbd1` |
| appleagxrenderadmission.cat | 4112 | `6d137825f912ef65d1d5ba2efcd598c9503a9ee369df686faeedc4eaed525653` |

## P38 (NWOAS-J274, 2026-11-19) — 2 GiB 넘는 D3D12 복사, 구역 3 경계 거절, 페이징 실패 영수증

결론: **판 2774는 2773 위에 (1) D3D12 버퍼 복사의 2 GiB 상한 제거, (2) 구역 3 크기 − 256 MiB를 넘는 예약을 깨끗한 메모리 부족으로 거절, (3) 페이징 실행 실패·주소표 갱신 실패 영수증 `Wom1PagingExecFailure`를 더한 것입니다** [빌드만]. S265 K43에서 2048 MiB 버퍼의 장치가 사라진 직접 원인은 아직 확정하지 못했습니다(3번이 그것을 가리기 위한 것). 근거·시험 명령은 `RESULT-2774.md`.

| 바뀐 것 | 내용 |
|---|---|
| `mesa/winsys/agx_d3d12_windows.cpp` | `AgxD3d12OwnerCopy`: `Bytes > 0x7fffffff` 거절 → 4 GiB 상한, 1 GiB 조각, 32비트 위치를 2의 보수로 `pipe_box.x`에 |
| `mesa/winsys/agx_win32_asahi_bo.c` | `agx_bo_create`: GPU 주소 연결 실패가 깨끗이 되돌려졌으면 백엔드 고장 처리 없이 이번 생성만 실패 |
| `render-admission/src/pool_windows.c`, `include/render_pool_demand.h` | 예약 상한(사유 6 SEGMENT), `Wom1PoolAdmitMarginMiB`(기본 256, 0 = 끔), 영수증 v16(752·760에 두 칸 추가) |
| `render-admission/src/allocation_windows.c` | 일반 버퍼 최대 4 GiB − 64 KiB(풀이 있을 때만) |
| `paging_windows.c`, `gpuva_g3_paging_windows.c`, `receipts.c`, `render_paging.h`, `render_admission.h` | `Wom1PagingExecFailure` v1(112 B, 즉시 기록) |

- 빌드(`build-p38.sh`, 실행 이름 j274g4n): 성공, 판 30.0.2774.0, 소스 커밋 `b600a8a6`, 경고 98개(2773과 같음). 결과물 `out/j274-full-20261119-0754-p38`, 차이 `P38-driver.diff`(10개 파일).

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 652520 | `ef926ebf2226d0e63ca8dcd2a0d5f3a7ba43fc19ce471fceff79b95a709bcbe5` |
| AppleAgxRenderAdmissionUmd.dll (ARM64X 전달) | 14848 | `d8f5b7006c32aca88c97a14c8490f099f80f09913cb7a387b33547c8c2d6b923` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13522944 | `0e076329fcaf39bf450caf608fdd71e03c5a9d6564d2dc58fc542acf740d6819` |
| AppleAgxRenderAdmissionUmdX64.dll | 14092288 | `3bd02d9c7da3116bdf14d089c4138da1d21e76869bde16ae98edcf95ff58c43b` |
| AppleAgxRenderAdmission.arm64x.inf | 4290 | `e884378040af056b28e5bf4341da02169776fa503ba757fdeb3c2db20aa3e9b5` |
| appleagxrenderadmission.cat | 4112 | `813316feab6a0f1254c986b4c3646d38f02075c877fbd03032e372812a1e81d6` |

## P39 (NWOAS-J274, 2026-11-19) — 구역 3 배치 기록 영수증(기록 전용)

결론: **판 2775는 2774에 KMD 영수증 `Wom1PoolTimeline`·`Wom1PoolTimelineFirst`만 더한 기록판입니다. 배치·구역 집합·결과는 하나도 바꾸지 않습니다** [빌드만]. 2304·2560 MiB 실패에서 VidMm이 64 MiB 올리기 버퍼를 풀 밖에 둔 이유를 가리려는 것입니다(`RESULT-2775.md` 8·9장).

| 바뀐 것 | 내용 |
|---|---|
| `include/render_admission.h` | `ADMISSION_POOL_TIMELINE` v1(5,696 B, 항목 88 B × 64), `AdmissionRecordPoolTimeline` |
| `src/gpuva_g3_paging_windows.c` | `AdmissionG3PoolPlace`: 16 MiB 이상 할당의 UpdatePageTable마다 구역 번호·구역 3 위치·페이지 수 기록(같은 할당·프로세스·위치는 합침) |
| `src/gpuva_g3_windows.c` | 배치 조사(PLACEMENT v4)가 풀 밖 페이지를 찾으면 기록 + 영수증 쓰기 |
| `src/allocation_windows.c` | 16 MiB 이상 할당 파괴 기록(1 GiB 이상이면 영수증 쓰기) |
| `src/receipts.c` | 링·스냅숏(작업자 스택 밖), 쓰기 최대 64회/KMD 시작, 첫 조사 때 것은 `Wom1PoolTimelineFirst`에 따로 |

- 빌드(`build-p39.sh`, 실행 이름 j274g4p): 성공, 판 30.0.2775.0, 경고 줄 수 2774와 같음(step5 로그 2줄). 결과물 `out/j274-full-20261119-2115-p39`, 차이 `P39-driver.diff`(5개 파일). (같은 소스의 앞선 판 j274g4o는 소스를 고치기 전 것이라 버렸고 VM 중간물도 지움.)

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 654096 | `2c451f39a30c7d98c4b309bd130ad8f7cb397d15834ab7b57eaa5636685128e3` |
| AppleAgxRenderAdmissionUmd.dll (ARM64X 전달) | 14848 | `9b3d5876b97ceeb5d873dc67231305aee7db2de52bbaa37cd07ee3125e5ec1de` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13522944 | `4210c890b80144f549f5535134fe896873e323a9782871216f9ab234f5351687` |
| AppleAgxRenderAdmissionUmdX64.dll | 14092288 | `825cf0f079683359a6d9dfcd716eb7280667d7ac7c47361d76bf9d84118d9fd2` |
| AppleAgxRenderAdmission.arm64x.inf | 4290 | `2352da497e6d7c0aad893b54588edafa4ba8996d3df8e5e26ee7ef2e2edda4ca` |
| appleagxrenderadmission.cat | 4112 | `a261e41ba6c2e3c0bc158f9edc1a6e09839c9d8b13fafc7186f561a0cdcdcbfb` |

## P40 (NWOAS-J274, 2026-11-19) — 고침 후보 스위치(시험판, 기본은 모두 꺼짐 = 2775)

결론: **판 2776은 2775에 고침 후보 3개를 스위치로만 더했습니다. 스위치를 켜지 않으면 2775와 같게 동작합니다** [빌드만]. 어느 후보가 실제로 고치는지는 같은 부팅 안에서 꺼짐/켜짐을 번갈아 돌려 가립니다(`RESULT-2775.md` 12장).

| 스위치 | 위치 | 하는 일 |
|---|---|---|
| `APPLE_AGX_CPU_POOL_REHOME_ROUNDS=<n>` (기본 2) / `APPLE_AGX_CPU_POOL_REHOME_SLEEP_MS=<ms>` (기본 0) | UMD 환경 변수 | rc107 되살리기 횟수와, 두 번째 회차부터 앞에 쉬는 시간 |
| `APPLE_AGX_CPU_POOL_PRIORITY=1\|2` | UMD 환경 변수 | CPU에서 보이는 풀 버퍼(SystemDirect) 상주 우선순위 HIGH / MAXIMUM (기본 NORMAL) |
| `Wom1PoolCpuVisibleTopDown=1` | KMD 레지스트리(장치 재시작 때 읽음) | CPU에서 보이는 구역 3 버퍼를 위에서부터 배치(Direction0) |

- 빌드(`build-p40.sh`, 실행 이름 j274g4q): 성공, 판 30.0.2776.0, step5 경고 줄 수 2775와 같음. 결과물 `out/j274-full-20261119-2305-p40`, 차이 `P40-driver.diff`(4개 파일).

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 654096 | `7fcdbd7d787662992f02559fa149bf813db382633a338b66100f1b245ea9eaa8` |
| AppleAgxRenderAdmissionUmd.dll | 14848 | `8502d1f11eabdf218fe8da6cd443782792e57db8403123e6d5e19e64c09abc77` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13523456 | `b6bd3eb9652758151a4124476d86fd2f9bb3b9aed8b79ac8d3be74d60b26ab8f` |
| AppleAgxRenderAdmissionUmdX64.dll | 14093312 | `d0bf3c00a2358699f3d15040971ae2c5471778a00c915852cfaca097450fad74` |
| AppleAgxRenderAdmission.arm64x.inf | 4290 | `6f9d076ce411978aada5f7853db52b04bfe05a20071f3bccfc0432be5e823fb0` |
| appleagxrenderadmission.cat | 4112 | `e747c36ab8797f213da0ed11fa23aeed0f6af1a3b9fd049501b24c8d33863023` |

## P42 (NWOAS-J274, 2026-11-20) — x86(WOW64) D3D UMD (판 2777)

결론: **판 2777 = 2776(P40, 스위치 모두 꺼짐) + x86 UMD `AppleAgxRenderAdmissionUmd32.dll`(SysWOW64, `UserModeDriverNameWow`), INF `AppleAgxRenderAdmission.arm64xwow.inf`** [빌드만]. TopDown 기본값은 넣지 않았습니다(아침 시험 뒤 결정).

| 바뀐 것 | 내용 |
|---|---|
| `umd/AppleAgxRenderAdmissionUmd.vcxproj` | `Release|Win32` 구성, Win32는 기본 호출 규약 Cdecl(WDK 도구 세트의 /Gz 기본값 때문에 런타임 라이브러리 이름이 맞지 않던 링크 오류) |
| `umd/src/umd_draw_composer.c`, `umd_gpuva_windows.c` | 32비트에서 `UINT_PTR >> 32` 미정의 동작 → 64비트로 넓혀서, qsort 비교 함수 `__cdecl` (64비트 동작은 같음) |
| `scripts/build-umd-x86-msbuild.ps1`(새), `AppleAgxRenderAdmission.arm64xwow.inf`(새) | x86 UMD 빌드, 설치 정의 |
| `mesa/dxil/patches/c49-build-dxspv.ps1`, `mesa/scripts/configure-x86-nir.ps1` | x86 dxil-spirv, 교차 설정 파일·플랫폼 인자 |

- 빌드(`build-p42.sh`, 실행 이름 j274g4t): 성공, 판 30.0.2777.0. 처음 두 번(j274g4r·g4s)은 위 x86 컴파일·링크 오류로 멈췄고 고친 뒤 통과, 실패한 중간물은 VM에서 지움. 결과물 `out/j274-full-20261120-0454-p42`, 차이 `P42-driver.diff`. InfVerif ERROR(1322) 4건(System32 3 + SysWOW64 1, 2772부터 있던 종류).

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 654096 | `f256dbc4249f5665ffae7d1bfe2c34aa0b6da201f5d3c69878c17de91c60f8cf` |
| AppleAgxRenderAdmissionUmd.dll (ARM64X 전달) | 14848 | `6e7a375b2a738f1acbbca00edd7ee0a9b5156b189d01a38a71bc932bc90b404f` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13523456 | `c3e2a3d851da501703d32fa0b2e20d741c1acb2a4c1e1e74d14aa0d12c6130ac` |
| AppleAgxRenderAdmissionUmdX64.dll | 14093312 | `fc1475acddb21827280b0025657225eba624653bec755db55cebe6e448977178` |
| AppleAgxRenderAdmissionUmd32.dll (x86) | 13739520 | `5601111d01fdf721a3165668eb155667d4af9e1919e758641c273e53556b2a9a` |
| AppleAgxRenderAdmission.arm64xwow.inf | 5248 | `2ea86858e37a3d6a86c129604e324a8d129e042cd6f161927e252ed27a74d193` |
| appleagxrenderadmission.cat | 4637 | `bce86a72e4c9179c6d346d56d04cbba37f2807f7bcf892882c4360292fe844cf` |

## P43 (NWOAS-J274, 2026-11-20) — CPU가 보는 버퍼를 풀 위쪽부터 놓기를 기본으로 (판 2778)

결론: **판 2778 = 2777(P42) + `Wom1PoolCpuVisibleTopDown` 기본 켜짐(값 없음 = 켜짐, 0 = 2775~2777 배치)** [빌드만].
근거: 11-20 교대 부팅 시험(고친 연쇄, 2777 위)과 01:15 시험을 합쳐 T=1 3부팅 24/24 갈래 통과, T=0 6부팅 모두 실패 있음(RESULT-2775.md 17장).

| 바뀐 것 | 내용 |
|---|---|
| `src/pool_windows.c` AdmissionPoolReadCpuVisibleTopDown | 키를 못 열거나 값이 없으면 1, 값이 0일 때만 0 (P41 초안과 같은 동작) |
| `src/allocation_windows.c`, `include/render_admission.h` | 주석만 |
| `build-p43.sh` | build-p42.sh 복사, PACKAGE_BUILD 2778, 실행 이름 j274g4u |
- 빌드(`build-p43.sh`, 실행 이름 j274g4u, 12:21~12:30): 한 번에 성공, DriverVer 30.0.2778.0, STAMPINF/INF2CAT/SIGN 0, INFVERIF_RC 1627(2777과 같음).
  차이 파일 `P43-driver.diff` sha256 `0dfad6d57bc7acc7d149d78bc12df131d971d1fa57e523e1bb749c6e307ca0d8`. 결과물 `out/j274-full-20261120-1221-p43`.

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 654096 | `c06de374e1dfd20c05d8719b076ce197e5386abd81ef7444f789fc1bf7bc1cb0` |
| AppleAgxRenderAdmissionUmd.dll (ARM64X 전달) | 14848 | `77ae9e45fb5d06a5aed8ebe8e06f9a1d5e290db72f151ed09b085ae08261388d` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13523456 | `e5e4f27b6c5e433a124803e3bdcf7eedc9ca1e878f3929f2b2674891f116629e` |
| AppleAgxRenderAdmissionUmdX64.dll | 14093312 | `529d46c272c016a83cef5305820350023dc2e484ed03b2f56340d06517702c96` |
| AppleAgxRenderAdmissionUmd32.dll (x86) | 13739520 | `8c59885bf15c951601be4de77930a238e2352ca10ead1560e1ce28f0a2372fd4` |
| AppleAgxRenderAdmission.arm64xwow.inf | 5248 | `e57fa00fcfe18fae9575db96bde8be74efba2aa56f72f68860209c245e18844d` |
| appleagxrenderadmission.cat | 4637 | `edbb8708cd227f5b7748065fc3887101d54ef0434d37c5a930ae27946e4663de` |

## P44 (NWOAS-J274, 2026-11-20) — DXBC→NIR 변환기: mov·movc·swapc의 부호·절댓값 표시를 소수 방식으로 (판 2779)

결론: **판 2779 = 2778(P43) + `mesa/dxbc/dxbc_to_nir.c` 세 군데. UMD(ARM64·x64·x86)만 바뀌고 KMD 소스는 그대로입니다** [빌드만].
근거: 2778 실기에서 ps_5_0의 atan·atan2가 틀리고, `movc r, 조건, -a, a` 꼴만 따로 재도 틀림(K57·K58, `RESULT-2775.md` 26·27장). 변환기가 이 명령들의 값을 정수형으로 읽어 `-x`를 `nir_ineg`, `|x|`를 `nir_iabs`로 적용하고 있었습니다.

| 바뀐 것 | 내용 |
|---|---|
| `mesa/dxbc/dxbc_to_nir.c` DXBC_OP_MOV | `SRC(1, saturate ? F : U)` → `SRC(1, F)` |
| 같은 파일 DXBC_OP_MOVC | 고를 값 두 개 `SRC(2, U)`, `SRC(3, U)` → `F` (조건은 그대로 U) |
| 같은 파일 DXBC_OP_SWAPC | 바꿀 값 두 개 `SRC(3, U)`, `SRC(4, U)` → `F` |
| `build-p44.sh` | build-p43.sh 복사, PACKAGE_BUILD 2779, 실행 이름 j274g4v |

- 표시가 없는 mov·movc·swapc는 읽는 방식이 달라지지 않습니다(`read_src`는 형을 표시 적용에만 씀, 467-476행). 이 변환기는 D3D12 경로도 같이 쓰므로 그쪽에도 적용됩니다.
- 빌드(`build-p44.sh`, 실행 이름 j274g4v, 14:17~14:24): 한 번에 성공, DriverVer 30.0.2779.0, 경고 46 · 오류 0, STAMPINF/INF2CAT/SIGN 0, INFVERIF_RC 1627(2777·2778과 같음). 고친 소스가 VM 빌드 트리(`C:\agx\AD04-persistent-dwm-next\…\dxbc_to_nir.c`, 14:16:35)에 있는 것 확인.
  차이 파일 `P44-driver.diff`. 결과물 `out/j274-full-20261120-1417-p44`.

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 654096 | `472b97c03e04ac127b1dac6e595240def6956efa3adc451783a08a1c7bbc7299` |
| AppleAgxRenderAdmissionUmd.dll (ARM64X 전달) | 14848 | `f7477213290b9827f00091e3a595c3e94937f35b378eaba19ae28b1ddb3d6140` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13523456 | `c0122e1780ad83607ade4713d75bf51b65101e2d5ff9d0db3d6d269621806330` |
| AppleAgxRenderAdmissionUmdX64.dll | 14093312 | `c2352c6cf6f53ebc3711f3d724eb8e544d6da27e372a1c1912da1193f6c951e7` |
| AppleAgxRenderAdmissionUmd32.dll (x86) | 13739520 | `0be8462d2156ec52a728f7a4f96e627caca68d6d252ac2d72f85981a4e829221` |
| AppleAgxRenderAdmission.arm64xwow.inf | 5248 | `eb55bdeb761e86c20b8252328e21ad29738a65a18a1c00ac8e6162b6d58d304f` |
| appleagxrenderadmission.cat | 4637 | `3b316ba49a2bb7474893a569d1d6cbc8e726e623f4a939935f2eb75800adde8e` |
