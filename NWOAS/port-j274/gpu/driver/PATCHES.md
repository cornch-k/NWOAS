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
