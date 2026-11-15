# J274 패치 목록 (PATCHES)

결론: **J313 기본 빌드는 원본과 똑같이 두고, `/p:AppleAgxBoard=J274`(또는 `APPLE_AGX_BOARD_J274=1`)를 줄 때만 J274 값으로 바뀌게 했습니다.** 바꾼 것은 보드마다 다른 메모리 주소뿐입니다. GPU 전원·성능 표, 화면(DCP) 경로처럼 J274 실기기 값을 아직 모르는 것은 일부러 J313 값 그대로 두었습니다. 그래서 J274 빌드는 그 단계에서 **조용히 틀린 값을 쓰지 않고 오류로 멈춥니다.**

- 빌드가 되는 것은 GPU가 동작하는 것이 아닙니다. 아래 어느 것도 Mac mini에서 시험하지 않았습니다.
- 소스에는 모두 `NWOAS-J274 PATCH Pn` 주석이 붙어 있습니다. `grep -rn "NWOAS-J274" drivers/`로 찾을 수 있습니다.
- 전체 차이: `j274-patch.diff` (원본 `external/windows-on-m1/drivers/apple-agx` 대비, 13개 파일).
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
| O2 | GPU 전원·온도 제어 입력 66개(`gpu-*`)와 HwDataA/B 바이트, 프로필 ID | `shared/include/apple_agx_hwdata_profile.generated.h:1505`(ID), `:1611`(입력 목록) | 하이퍼바이저의 `AgxHwdataInputsMatch`(`hv_agx_retained_platform.c:456-458`)가 J274 ADT와 하나라도 다르면 프로필을 거부 → 펌웨어 시작 거부. 팬이 있는 Mac mini는 전원 값이 다를 가능성이 큼 [가설] | J274 ADT를 떠서 생성 도구를 J274용으로 고쳐 다시 생성. 원본 도구는 0x26과 "J313" 문자열을 고정(`tools/generate_agx_hwdata_profile.py:100,120`) |
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
