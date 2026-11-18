# J274(Mac mini M1) GPU 드라이버 — windows-on-m1 드라이버 이식 준비본

## 요약

1. **드라이버 소스를 복사하고 J274용 최소 패치를 넣었습니다.** `/p:AppleAgxBoard=J274`를 주면 8 GB 기준 메모리 주소 3개(GPU 영역, 핸드오프, 공유 영역)가 16 GB 기준 J274 값으로 바뀝니다. 아무것도 안 주면 원본 J313과 똑같이 빌드됩니다(전처리 결과 비교로 확인).
2. **아직 Windows 드라이버(.sys/.dll)를 빌드하지 못했습니다.** 이 맥북에 Windows SDK/WDK 헤더가 없기 때문입니다. 내려받기 목록은 아래에 있고, 콘치님 승인 전에는 아무것도 내려받지 않았습니다.
3. **J274 빌드는 지금 그대로 Mac mini에 넣어도 GPU가 켜지지 않습니다.** 막히는 곳이 셋입니다. ① 우리 하이퍼바이저에 GPU 중개 기능이 없음(하이퍼바이저 담당), ② GPU 전원·성능 값 66개가 J313 Air 값이라 J274 실기기 ADT를 읽어 다시 만들어야 함, ③ 화면 경로가 내장 2560x1600 패널 전용이라 HDMI에서는 정식 빌드가 시작 단계에서 멈춤. 다만 "펌웨어 켜고 끄기만 하는 시험 빌드"는 ③ 전에 끝나므로 ①②가 풀리면 시험할 수 있습니다.
4. **빌드 방법 추천: 이 맥북 위의 Windows 11 ARM64 가상머신에 VS 2022 + WDK 10.0.26100을 깔고 MSBuild로 빌드**합니다. Mac mini는 건드리지 않습니다. macOS에서 clang-cl로 바로 교차 빌드하는 길도 가능성은 있지만(어셈블리 파일 하나는 실제로 Windows ARM64 형식으로 만들어 봄), 서명·카탈로그는 결국 Windows 도구가 필요합니다.

| 상태 | 내용 |
|---|---|
| 끝남 | 소스 복사(975개 파일), 패치 P1~P9, J313 동일성 확인, J274 값 컴파일 시험, 기기별 가정 목록 |
| 안 함 | Windows용 실제 빌드, 서명, 실기기 시험(전부 콘치님 승인·하드웨어 필요) |
| 빌드 성공 ≠ 동작 | 이 폴더의 어떤 결과도 GPU가 동작한다는 뜻이 아닙니다 |

## 폴더 구성

| 경로 | 내용 |
|---|---|
| `drivers/apple-agx/` | windows-on-m1 `drivers/apple-agx` 전체 사본 + `NWOAS-J274 PATCH` 표시된 수정 |
| `Directory.Build.props`, `packages.config` | 원본 최상위 파일 그대로(NuGet WDK/SDK 10.0.28000.2526 연결) |
| `PATCHES.md` | 바꾼 것 P1~P9, 일부러 안 바꾼 것 O1~O7 |
| `INVENTORY.md` | 드라이버 속 J313 가정 전체와 J274 값(또는 "모름") |
| `j274-patch.diff` | 원본 대비 전체 차이(13개 파일) |
| `nwoas-tests/board_selftest.c` | 보드 선택 값 컴파일 시험(우리 코드) |
| `acpi-draft/J274AppleAgxAbiAdmission.asl.inc` | ACPI 담당용 J274 장치 정의 초안(주소 2개만 바꿈, 빌드 안 함) |
| `upstream-acpi-reference/` | 원본 J313 ACPI 파일 참고본 |
| `THIRD_PARTY.md`, `LICENSE.windows-on-m1` | 라이선스(MIT)와 출처 |

## 이번에 한 시험 (이 맥북, Mac mini 안 건드림)

| 시험 | 방법 | 결과 |
|---|---|---|
| J313 기본 빌드가 원본과 같은가 | 원본과 패치본을 `clang -E -P`로 전처리해 비교: `apple_agx_config_snapshot.c`, `apple_agx_regionc.c`, `apple_agx_gfx_handoff.c`, `apple_agx_uat_publication.c`, `apple_agx_render_provider.c`, hwdata 프로필 헤더, 주소 매크로 | 6개 같음, `config_snapshot.c`는 공백 1개만 다름(토큰은 같음) |
| J274 값이 맞게 들어가는가 | `nwoas-tests/board_selftest.c`의 `_Static_assert` 11개(주소, 보드 번호, 영역 사이 간격, 16 KiB 정렬, 40비트 한계, HwDataB 범위) | 두 보드 모두 통과. macOS clang(컴파일+실행)과 `clang-cl --target=aarch64-pc-windows-msvc /W4 /WX`(구문 검사) 둘 다 |
| 두 보드를 동시에 고르면 막히는가 | `-DAPPLE_AGX_BOARD_J274=1 -DAPPLE_AGX_BOARD_J313=1` | 의도대로 `#error` |
| J274로 공용 소스가 컴파일되는가 | 위 5개 소스 + hwdata 헤더를 `-DAPPLE_AGX_BOARD_J274=1`로 `clang -fsyntax-only` | 오류 0. 전처리 결과에 `0xbfffb8000ULL`과 HwDataB `0xa78` 쓰기 확인 |
| HVC 어셈블리 교차 빌드 | `arm64_hypercalls_gnu.S`를 Homebrew clang 23.1.5로 `--target=aarch64-pc-windows-msvc` 어셈블 | Aarch64 COFF 생성, `hvc #0x4d32`/`hvc #0x4d31` + `ret` 확인 |
| 커널 드라이버 본체 컴파일 | — | **못 함.** `ntddk.h`, `d3dkmddi.h` 등 WDK 헤더 없음 |

원본 저장소의 스크립트·테스트 프로그램은 실행하지 않았습니다(읽기만).

## 빌드하는 방법

### 결론

| 길 | 커널 드라이버(.sys) | 사용자 드라이버(.dll, Mesa 없이) | Mesa 포함 D3D 드라이버 | 서명·패키지 | 판단 |
|---|---|---|---|---|---|
| A. Windows(이 맥북 위 ARM64 가상머신, 또는 x64 PC) + VS 2022 + WDK 26100 | 됨(그쪽이 쓰는 정식 방법) | 됨 | 됨(그쪽 방식, 단 그쪽 빌드 폴더 구조 재현 필요) | 됨 | **추천** |
| B. macOS + clang-cl/lld-link + NuGet에서 푼 WDK/SDK 헤더 | 가능성 있음 [가설]. MSBuild가 없어 컴파일 목록·옵션을 따로 옮겨 적어야 함 | MSVC C++ 표준 라이브러리 헤더가 더 필요 | 가능성 있음 [가설] | **안 됨**: `stampinf`, `inf2cat`, `signtool`은 Windows 전용 | 컴파일 오류를 빨리 잡는 보조 수단 |
| C. Docker(이 맥북) | B와 같음(리눅스 컨테이너 안 clang-cl) | B와 같음 | B와 같음 | 안 됨 | 이점 없음. 그쪽 `Dockerfile.build`는 Mu·m1n1 전용이고 드라이버는 다루지 않음 |
| D. Mac mini의 Windows에서 직접 | 됨(WDK 26100부터 ARM64 기계에서 빌드 지원) | 됨 | 됨 | 됨 | 지금은 안 됨: Mac mini는 다른 작업이 쓰는 중이고, 하이퍼바이저 위 Windows에 수 GB 설치는 부담 |
| E. GitHub Actions의 Windows 빌드 기계(그쪽 CI 방식) | 됨(`WOM/.github/workflows/apple-agx-wdk.yml:20`, `windows-2025-vs2026` + `nuget restore`) | 됨 | — | 됨 | 코드를 GitHub에 올려야 해서 콘치님 승인 필요. 그쪽 CI는 옛 G2 드라이버만 빌드함 |

근거
- [사실] 그쪽 정식 빌드 스크립트는 Windows PowerShell + MSBuild이고 `C:/Program Files (x86)/Windows Kits/10`의 SDK/WDK 10.0.26100.0을 고정합니다(`drivers/apple-agx/render-admission/scripts/build-driver.ps1`의 `$pinnedWdkProperties`). VS 2022 Community MSVC 14.44.35207을 씁니다(같은 파일, `mesa/scripts/configure-full-compiler.ps1:6-7`).
- [사실] 최상위 `Directory.Build.props`는 `packages\` 폴더가 있을 때만 NuGet WDK/SDK 10.0.28000.2526을 불러옵니다. 26100 설치판과 섞지 않습니다.
- [사실] Microsoft 문서: WDK 10.0.26100.1부터 ARM64 기계에서 설치·빌드 지원. 최신 WDK 28000.2526은 VS 2026용이고, VS 2022를 계속 쓰려면 WDK 26100.6584를 쓰라고 안내합니다(https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk).
- [사실] 이 맥북에 있는 도구: Homebrew LLVM 23.1.5(`clang-cl`, `llvm-lib`, `llvm-rc`, `llvm-mt`), `lld-link`, `meson`, `ninja`, `cmake`, `docker`. 없는 것: Windows SDK/WDK 헤더·라이브러리, MSVC 표준 라이브러리, `osslsigncode`, `xwin`.

### A안 순서 (추천) — Windows 11 ARM64 가상머신

1. (승인 필요) 이 맥북에 가상머신 프로그램과 Windows 11 ARM64 설치 이미지를 받습니다.
2. 가상머신 안에 VS 2022(17.14 계열) 설치. "Desktop development with C++" + 개별 구성 요소: MSVC v143 ARM64/ARM64EC 빌드 도구, "C++ Spectre-mitigated libraries for ARM64/ARM64EC", Windows 11 SDK 10.0.26100, "Windows Driver Kit".
3. WDK 10.0.26100.6584 설치(Microsoft "Other WDK downloads"). SDK와 빌드 번호(26100)가 같아야 합니다.
4. 이 폴더(`port-j274/gpu/driver`)를 가상머신으로 복사합니다(예: `C:\nwoas\driver`). 경로에 공백이 없게 합니다.
5. 사용자 드라이버(ARM64, Mesa 없는 기본 구성) 빌드:
   ```
   msbuild drivers\apple-agx\render-admission\umd\AppleAgxRenderAdmissionUmd.vcxproj /m /t:Clean,Build /p:Configuration=Release /p:Platform=ARM64 /p:AppleAgxBoard=J274 /p:WindowsTargetPlatformVersion=10.0.26100.0
   copy drivers\apple-agx\render-admission\umd\ARM64\Release\AppleAgxRenderAdmissionUmd.dll drivers\apple-agx\render-admission\
   ```
6. 커널 드라이버 — 첫 J274 시험용 "펌웨어 켜고 끄기" 빌드, ARM64 전용 패키지:
   ```
   msbuild drivers\apple-agx\render-admission\AppleAgxRenderAdmission.vcxproj /m /t:Clean,Build /p:Configuration=Release /p:Platform=ARM64 /p:AppleAgxBoard=J274 /p:AppleAgxFirmwareQualification=true /p:AppleAgxArm64Only=true /p:RunCodeAnalysis=true /p:WindowsTargetPlatformVersion=10.0.26100.0
   ```
   결과는 `drivers\apple-agx\render-admission\ARM64\Release\AppleAgxRenderAdmission\`(.sys, .inf, .cat). vcxproj에 서명 설정이 따로 없으므로 WDK 기본값(테스트 인증서 서명)이 쓰일 것입니다 [가설]. 정확한 출력 폴더·이름은 빌드해 봐야 압니다.
7. 정식(전체) 빌드는 `render-admission\scripts\build-driver.ps1 -Board J274 ...`를 씁니다. 이 스크립트는 x86 OpenGL ICD와 x86 사용자 드라이버 파일을 반드시 요구합니다.
8. (콘치님, 나중에) Mac mini Windows에 설치하는 것은 아래 "실기기에서 시험하기 전에 필요한 것"이 모두 갖춰진 뒤에 합니다.

### B안 순서 — macOS에서 컴파일 오류만 먼저 잡기 (보조)

1. (승인 필요) nuget.org에서 아래 3개 `.nupkg`를 받습니다(zip 형식이라 macOS에서 풀기만 하면 됨, 실행하는 것 없음).
2. 풀어서 `km`, `shared`, `um`, `ucrt` 헤더와 `arm64` 라이브러리 경로를 잡습니다.
3. vcxproj의 `ClCompile` 목록(render-admission 커널 드라이버 128개 파일)과 매크로(`DXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_WDDM3_0`, `APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY=1`, `APPLE_AGX_FULL_CONTEXT0_BROKER=1`, `APPLE_AGX_BOARD_J274=1`, 커널 드라이버 기본값 `_ARM64_`, `_WIN64` 등)를 옮긴 빌드 파일(CMake 또는 Makefile)을 만들고 `clang-cl --target=aarch64-pc-windows-msvc /kernel`로 컴파일합니다. 어셈블리는 `arm64_hypercalls_gnu.S`(P8)를 씁니다.
4. `lld-link /DRIVER /SUBSYSTEM:NATIVE ntoskrnl.lib displib.lib ...`로 링크까지는 가능성이 있습니다 [가설]. WDK 빌드 규칙이 넣는 숨은 옵션(보안 쿠키 진입점 등)은 MSBuild 로그와 비교해야 합니다.
5. 패키지(`stampinf`, `inf2cat`)와 서명은 A안 Windows에서 합니다.

### Mesa가 들어간 D3D 사용자 드라이버

| 항목 | 내용 | 근거 |
|---|---|---|
| Mesa 없이 빌드되는 기본 사용자 드라이버 | `EnableNativeFrontend=false`가 기본. D3D12 DDI·자체 DXIL 코드·전송 코드만 | [사실] `render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj` |
| Mesa 포함 구성 | `EnableNativeFrontend=true` + `MesaSourceRoot`, `MesaGeneratedRoot`, `NativeRuntimeProps`(그쪽 스크립트가 미리 만든 Asahi 런타임 묶음) | [사실] 같은 파일, `build-driver.ps1` |
| Mesa 판 | `https://gitlab.freedesktop.org/mesa/mesa.git` 커밋 `9aa1215f878b504f66159dd2ead4c7973142126e` | [사실] `mesa/mesa-source.lock.json` |
| meson 교차 파일 | `mesa/scripts/compiler-arm64.ini`: LLVM 20 `clang-cl --target=aarch64-pc-windows-msvc`, `llvm-lib`, `llvm-strip`. 경로가 그쪽 PC(`C:/Users/pauls/...`)로 박혀 있어 고쳐 써야 함 | [사실] |
| Python 도구 | Python 3.13 가상환경에 meson 1.8.3, ninja 1.11.1.4, Mako 1.3.10, PyYAML 6.0.2, packaging 25.0, ply 3.11 | [사실] `mesa/scripts/setup-full-compiler.ps1:9` |
| 추가 소스 | dxil-spirv `8434b06`, dxbc-spirv `8dae2d8`, SPIRV-Headers `c63848e` (codeload.github.com, sha256 고정) | [사실] `mesa/scripts/dxil_spirv_dependency_lock.json` |
| macOS에서 meson 교차 빌드 | Mesa 자체는 clang-cl 빌드를 지원하지만, 그쪽 절차(PowerShell 스크립트 70여 개, Windows 경로)를 macOS용으로 옮겨야 하고 SDK·MSVC 헤더도 필요 | [가설] 시간이 많이 듦. A안 가상머신에서 그쪽 절차대로 하는 편이 빠름 |
| 판단 | 첫 J274 시험(펌웨어 켜고 끄기)에는 Mesa가 필요 없음. Mesa는 그리기 단계(P6)에서 | — |

## 내려받기 목록 (전부 승인 필요, 아직 하나도 안 받음)

| 번호 | 이름 | 출처 | 크기 | 어느 길에 | 비고 |
|---|---|---|---|---|---|
| D1 | Microsoft.Windows.WDK.arm64 10.0.28000.2526 | nuget.org | 47.08 MB | B (A에서 NuGet 방식이면 A도) | [사실] nuget.org 페이지. Microsoft 라이선스 동의 필요 |
| D2 | Microsoft.Windows.SDK.CPP 10.0.28000.2526 | nuget.org | 153.43 MB | B | [사실] 같음 |
| D3 | Microsoft.Windows.SDK.CPP.arm64 10.0.28000.2526 | nuget.org | 101.18 MB | B | [사실] 같음 |
| D4 | Windows 11 ARM64 설치 이미지(ISO 또는 VHDX) | Microsoft | 약 5~6 GB [추정, 확인 안 함] | A | Windows 라이선스 문제 확인 필요 |
| D5 | 가상머신 프로그램(UTM 또는 VMware Fusion 등) | 각 배포처 | 수백 MB [추정] | A | 이미 있으면 불필요 |
| D6 | Visual Studio 2022 Community 또는 Build Tools + 위 구성 요소 | Microsoft | 설치 후 수~십수 GB [추정] | A, D | 가상머신 안에서 받음 |
| D7 | WDK 10.0.26100.6584 (또는 EWDK ISO) | Microsoft "Other WDK downloads" | 확인 안 함 | A, D | EWDK 크기는 Microsoft 페이지에 없음 |
| D8 | MSVC C/C++ 표준 라이브러리 헤더·라이브러리(`xwin` 도구로 VS 배포처에서 추출) | crates.io/GitHub(`xwin`) + Microsoft | 수백 MB [추정] | B의 사용자 드라이버 | Microsoft 라이선스 동의 필요 |
| D9 | Mesa 소스(커밋 9aa1215f) | gitlab.freedesktop.org | 압축본 수십 MB [추정] | Mesa 포함 D3D(나중) | — |
| D10 | dxil-spirv, dxbc-spirv, SPIRV-Headers 압축본 | codeload.github.com | 확인 안 함(풀었을 때 각 최대 256 MiB로 제한됨) | Mesa 포함 D3D(나중) | sha256 고정 |
| D11 | Python 패키지(meson, ninja, Mako, PyYAML, packaging, ply) | PyPI | 수 MB~수십 MB [추정] | Mesa 포함 D3D(나중) | meson·ninja는 이 맥북에 이미 있음 |

추천 순서: 지금 필요한 것은 **A안의 D4~D7**(가상머신에서 정식 빌드) 또는 **B안의 D1~D3(합계 301.69 MB)**(macOS에서 컴파일 오류만 먼저 확인)입니다. D8~D11은 그리기 단계에서 받습니다.

## 실기기에서 시험하기 전에 필요한 것

| 순서 | 무엇 | 담당 | 상태 |
|---|---|---|---|
| 1 | Windows 칸에 13.5 펌웨어 묶음 스텁(결정 D1) | 콘치님 + 듀얼부팅 갈래 | 진행 전 |
| 2 | 13.5 스텁에서 J274 ADT `/arm-io/sgx`, `/arm-io/gfx-asc` 덤프 → O1·O2·O3 값 생성, 공유 영역 주소 확인 | 콘치님(실기기 읽기) + 분석 | 진행 전 |
| 3 | 하이퍼바이저에 GPU 중개(전원·구성 창·페이지 표·HVC 0x4d31/0x4d32·예약 메모리·인터럽트 889) | 하이퍼바이저 담당(`m1n1-gpu-s240`) | 진행 중(다른 작업) |
| 4 | Mu ACPI에 AGX0(`APPL0002`) 추가 | ACPI 담당 | 초안만(`acpi-draft/`) |
| 5 | 이 폴더의 펌웨어 시험 빌드 설치 → 장치 관리자에서 시작 기록 확인 | 콘치님 | 1~4 뒤 |

검증 방법(5번): 하이퍼바이저 로그에 `HV: retained Hwdata input profile matched=1`이 찍히고, 드라이버가 펌웨어 시작 → 1초 안 응답 → 정지 기록을 남기는지 봅니다. `matched=0`이면 O2 값이 J274와 다르다는 뜻이고, 그것이 의도된 안전 정지입니다.
