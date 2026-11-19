# 2773 (P37) 결과 — x64 앱(Prism 에뮬레이션)용 GPU 사용자 드라이버

## 1. 결론

**판 2773 패키지를 만들었습니다. 맥미니에서는 아직 시험하지 않았습니다** [빌드만]. x64 앱이 실제로 Apple AGX로 그리는지는 K42 시험으로만 판정합니다.

- 방식: **같은 UMD 소스를 x64로 그대로 빌드한 DLL + 이름이 기존 UMD와 같은 ARM64X "전달 전용" DLL**입니다. ARM64 프로세스(DWM, Edge 등)는 전달 DLL을 거쳐 지금과 같은 ARM64 UMD를, x64 프로세스는 x64 UMD를 씁니다. KMD 소스는 바꾸지 않았습니다(.sys 해시는 판 번호 때문에 바뀜).
- VM에서 확인한 것 [사실, VM]: 실제 2773 DLL 세 개로 ARM64 프로세스는 ARM64 UMD, x64 프로세스는 x64 UMD가 두 진입점을 맡았고, x64 셰이더 컴파일러 출력이 ARM64와 바이트까지 같았습니다.
- **가장 큰 위험: 맥미니에 x64용 C/C++ 런타임이 없을 수 있습니다** [가설]. x64 UMD는 `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `MSVCP140.dll`을 씁니다. 맥미니에는 `vc_redist.arm64.exe`만 설치했습니다(I4). 이게 없으면 x64 UMD가 올라오지 못해 x64 앱은 지금처럼 GPU 가속 없이 돕니다. K42의 `crt_x64_ready` 줄로 바로 드러납니다. 없으면 콘치님이 `vc_redist.x64.exe`를 받아 주셔야 합니다(4장 4번).
- x86(32비트) 앱용은 이번에 만들지 않았습니다(7장).

## 2. Windows on Arm에서 x64 프로세스가 UMD를 찾는 방식 (조사)

**결론: x64 프로세스는 ARM64 프로세스와 같은 `UserModeDriverName` 값을 읽고 System32에서 같은 파일 이름을 불러옵니다. 그래서 그 이름의 파일이 두 아키텍처에 다 맞는 ARM64X여야 합니다.** x86(32비트)만 따로 `UserModeDriverNameWow` + SysWOW64를 씁니다.

| 출처 | 내용 |
|---|---|
| Microsoft Learn, [Arm64X PE files](https://learn.microsoft.com/en-us/windows/arm/arm64x-pe) (2025-11-06) | "both x64 applications and Arm64 applications can load binaries … in `System32`". x64용 별도 폴더(SysWoW64 같은 것)는 없음. 플러그인처럼 두 종류 프로세스에 올라가는 코드는 Arm64X 또는 "pure forwarder"를 쓰라고 함 |
| Microsoft Learn, [Build Arm64X files](https://learn.microsoft.com/en-us/windows/arm/arm64x-build) | pure forwarder 만드는 법: `link /dll /noentry /machine:arm64x /defArm64Native:… /def:…`. ARM64 프로세스가 불러오면 ARM64 DLL을, x64 프로세스는 x64 DLL을 대신 씀 |
| Microsoft Learn, [WDDM 64-Bit Issues](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/microsoft-windows-vista-display-driver-64-bit-issues) | 32비트 UMD는 `UserModeDriverNameWow`에 등록하고 `%systemroot%\SysWOW64`에 복사. x64 에뮬레이션 이야기는 없음 |
| 원본 windows-on-m1 EXP1800 (`investigation/analysis/D3D12-X64-WOA-UMD-ABI.md`) | 같은 패키지에서 x64·ARM64 프로세스의 D3DKMT UMD 이름 질의 결과가 바이트까지 같음(`AppleAgxRenderAdmissionUmd.dll`). 이름 다른 x64 DLL만 더하면 런타임이 부르지 않음 |
| 원본 EXP1805·EXP1820~1824 (`GPU_CURRENT_STATE.md`) | ARM64X 전달 DLL + UmdArm64 + UmdX64를 System32에 둔 패키지로 J313에서 x64 D3D12 앱(nBody) ~45 fps, x64 프로브 68 PASS |
| Qualcomm Adreno INF `qcdx8250.inf` (WOA-Project/windows_silicon_qcom_kona, 직접 읽음) | 19041 이후 설치 구간: `UserModeDriverName = <>, %13%\qcdx11arm64xum8250.dll, …, %13%\qcdx12arm64xum8250.dll`(ARM64X 하나, 드라이버 저장소), `UserModeDriverNameWow` = x86 DLL(SysWow64) + 같은 이름의 CHPE판(SyChpe32), `UserModeDriverNameX64 = ""` |
| Parallels 디스플레이 INF `prl_dd.inf` + 이 VM의 `System32\prl_umdd12.dll` | ARM64 구간에서 `UserModeDriverNameX64`를 지움(DelReg). VM의 `prl_umdd12.dll`은 dumpbin으로 `AA64 machine (ARM64) (ARM64X)` [사실, VM] |

- `UserModeDriverNameX64`라는 값이 있다는 것은 두 회사 INF로 확인했지만, Microsoft 문서에서는 찾지 못했습니다(검색어 그대로, GitHub 코드 검색 1건). 두 회사 모두 비우거나 지우므로 2773은 건드리지 않았습니다.

## 3. 방식 결정

**결론: "x64 UMD를 그대로 빌드 + ARM64X 전달 DLL"로 했습니다.** 원본이 실기기에서 검증한 경로이고, 지금 빌드 도구로 바로 되며, ARM64 UMD 바이트를 그대로 둘 수 있기 때문입니다.

| 방식 | 장점 | 단점 | 판단 |
|---|---|---|---|
| x64 UMD + ARM64X 전달 DLL (택함) | 원본 J313 실기기 통과. ARM64 UMD 그대로. MSVC x64 컴파일러로 ARM64와 같은 프로젝트·정의 | x64 드라이버 코드(Mesa 컴파일러 포함)가 Prism 에뮬레이션으로 돎 → 셰이더 컴파일·명령 기록이 ARM64보다 느림 | 지금 |
| ARM64EC UMD (+ ARM64X로 합침) | 드라이버 코드가 x64 앱 안에서도 네이티브로 돎(빠름) | Mesa 전체·dxil-spirv를 ARM64EC로 다시 빌드해야 함. clang-cl의 ARM64EC 지원으로 이 Mesa를 빌드한 사례 없음 | 다음 단계 후보 |
| Qualcomm처럼 하나의 합친 ARM64X | 파일 하나 | 위와 같은 ARM64EC 빌드가 필요 | 보류 |

- 전달 DLL의 위치는 2772처럼 System32(DIRID 11)로 두었습니다. Qualcomm은 드라이버 저장소(DIRID 13) 경로를 씁니다. 13으로 옮기면 InfVerif 1322 오류가 없어지지만, 전달 DLL이 가리키는 파일을 로더가 저장소 폴더에서 찾는지는 확인하지 않았으므로 이번에는 바꾸지 않았습니다.

## 4. 만든 것

### 4.1 빌드 방법 (ARM64 UMD와 같게)

| 단계 | ARM64 (2772와 같음) | x64 (새로) |
|---|---|---|
| Mesa NIR 라이브러리 | `nir-arm64` | `nir-x64`: 같은 Mesa 9aa1215f, 같은 meson 옵션, `-NDebug`, `--target=x86_64-pc-windows-msvc` (`05x-nir-x64.ps1`) |
| dxil-spirv | `c49-dxspv-arm64` | `c49-dxspv-x64` (원본 스크립트 `c49-build-dxspv.ps1 -Architecture x64`) |
| 컴파일러 증거 | `build-full-asahi.py --architecture arm64` (실행 안 함) | `--architecture x64`: 빌드·링크·**실행**(에뮬레이션) 통과 |
| 네이티브 런타임 | `asahi-runtime-arm64-j274g4m` | `asahi-runtime-x64-j274g4m` (같은 소스 커밋 `734dfd79`, 160개 단위) |
| UMD | `build-driver.ps1` → UMD vcxproj `Platform=ARM64` | 같은 vcxproj `Platform=x64`, 같은 속성 (`build-umd-x64-msbuild.ps1`) |
| 패키지 | — | 전달 DLL(`umd/forwarder`, 원본 그대로) + `AppleAgxRenderAdmission.arm64x.inf` → stampinf → inf2cat → 같은 WDK 시험 인증서(`13338B38…`)로 cat 서명 |

전체 명령: `bash port-j274/gpu/driver-p15/build-p37.sh` (VM "Windows 11"에서 03 → 07 → 08b → 08x → 09 → 11 → 12).

### 4.2 결과물 (`out/j274-full-20261119-0458-p37/package`)

| 파일 | 종류 | 바이트 | sha256 |
|---|---|---|---|
| AppleAgxRenderAdmission.sys | ARM64 KMD 30.0.2773.0, 시험 서명 | 651496 | `28cb665ecded4afc07f38f599c80af6d5410b9dc9697ae6b00c95e0006ea667a` |
| AppleAgxRenderAdmissionUmd.dll | ARM64X 전달 DLL(코드 없음) | 14848 | `6f10255356dd2956864f5bfdb8a59c25fe0501d3474459f6f66b04f9db562fbd` |
| AppleAgxRenderAdmissionUmdArm64.dll | ARM64 UMD (2772와 같은 소스, 판 2773) | 13522432 | `71e99344179800b6993691643fec1e88ef37de0d06ad8187a11934fad5132a58` |
| AppleAgxRenderAdmissionUmdX64.dll | x64 UMD | 14092288 | `712d067f0db4fc0684a9ff6508b94582286f746547d648b2c4c68ff633b1b064` |
| AppleAgxRenderAdmission.arm64x.inf | INF | 4290 | `56fd01224e4e60dea1d1a7bbc665c7f293c100886af90eb6b483e8fa10decbd1` |
| appleagxrenderadmission.cat | 카탈로그(위 5개 파일 포함 확인) | 4112 | `6d137825f912ef65d1d5ba2efcd598c9503a9ee369df686faeedc4eaed525653` |
| gpucov/bin/nwgpuprobe10-arm64.exe | 시험 도구 | 260608 | `bdc02e2e66c5e0e3bdde2406a08ed59b4dd325852550c027a26ab8066cc50cad` |
| gpucov/bin/nwgpuprobe10-x64.exe | 시험 도구 | 280064 | `6f1ca52f4b3896b34e5f4896058b2ffdc45401130950bc220c96e6f3ac3699e8` |

### 4.3 오프라인 검증

**결론: VM에서 할 수 있는 검사는 모두 통과했고, 남은 판정은 실기기뿐입니다.**

| 검사 | 결과 | 구분 |
|---|---|---|
| x64 UMD 컴파일(`/W4 /WX /sdl`, MSVC 14.44 x64) | 오류·경고 0 | [사실] |
| ARM64·x64 UMD 컴파일 정의 비교(빌드 로그) | 아키텍처 정의만 다름(`ARM64`·`_ARM64_` ↔ `AMD64`·`_AMD64_`·`_WIN64`), 나머지 18개 같음 | [사실] |
| CRT 단언 검사(`check_no_crt_asserts.py`) | `assert_paths: 0` | [사실] |
| x64 셰이더 컴파일러 출력 = ARM64 출력 | 시험 셰이더 2개 sha256 같음 (`8f5ad95c…`, `a6a1a83b…`) | [사실, VM] |
| PE 종류 | 전달 DLL `ARM64 (ARM64X)`, UmdArm64 `ARM64`, UmdX64 `x64` | [사실] |
| 전달 동작(실제 2773 DLL, VM) | ARM64 프로세스: 두 진입점 모두 UmdArm64. x64 프로세스: 두 진입점 모두 UmdX64, x64 의존 DLL 모두 해결 | [사실, VM] |
| 판 번호 일치(`verify-package-version.ps1`) | `Package versions coherent: 30.0.2773.0` (sys, 전달, Arm64, X64) | [사실] |
| InfVerif `/w /v` | **NOT VALID**: ERROR(1322) 3건 — UMD 세 파일이 System32(DIRID 13 아님). 2772는 같은 오류 1건이었고 pnputil 설치는 됨 | [사실] |
| signtool verify | 오류 1(시험 인증서 루트를 VM이 신뢰하지 않음). 2772와 같음 | [사실] |
| x64 프로세스가 AGX로 그림 | 시험 안 함 | 실기기 필요 |

### 4.4 ARM64 전용 가정 점검

**결론: 고칠 것이 없었습니다.** UMD·winsys·D3D12 소스와 P15~P36 차이에서 아키텍처 전용 코드를 찾았습니다.

| 항목 | 결과 |
|---|---|
| 인라인 어셈블리, NEON, `__dmb`·`__isb`, 캐시 정리 명령 | UMD 쪽에 없음(KMD의 `arm64_hypercalls.asm`은 KMD 전용, 그대로) |
| `_M_ARM64` 분기 | `umd.c`의 예외 기록 2곳뿐이고 이미 `_M_X64` 분기가 있음 |
| 포인터 크기·구조체 배치 | x64와 ARM64 모두 64비트 LLP64, 기본 정렬 8. x64 프로세스는 커널에서 32비트(WOW64)로 보이지 않으므로 KMD에 오가는 사설 데이터 배치가 같음 |
| `_InterlockedAdd64`(clang-cl에서 AArch64 전용) | `uatomic_clang_compat.h`에 x64 대체가 이미 있음 |
| 모듈 이름에 기대는 코드 | 없음(UMD가 자기 DLL 이름을 찾지 않음) |

## 5. 실기기 시험 (콘치님 맥미니)

**지금 맥미니에서 돌고 있는 GPU 시험이 끝난 뒤에 실행합니다.**

### 5.1 명령

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
bash agx-s248/run-2773-chain.sh <지금 실행기 .out> <지금 .link>
```

순서: 다시 시작 → 2773 전달(6개 파일, `agx-j274-2773-pkg.cmd`) → I35 설치 → probe10 전달(`gpucov13-j274-20261119.cmd`) → D17b → 재부팅(run-s259.sh, D28) → `K42 K41 K25b K41`. 설치 완료는 `I35 DONE link=` 줄로만 기다립니다(작업 로그 안의 `S248 I35 DONE`과 헷갈리지 않게, 2772 때 고친 방식).

설치된 세션에서 x64 시험만 다시: `KIT="K42" LAUNCHER=agxdcp-s259-guest-test.sh bash gpucov/run-gpucov.sh run <link>`

### 5.2 통과 기준

| 작업 | 통과 기준 | 비고 |
|---|---|---|
| I35 | `PNPUTIL EXIT 0`, `DEV1 OK`, `VC Apple AGX … (NWOAS ARM64+x64) \| 30.0.2773.0`, `SYS32` 세 줄 해시가 4.2 표와 같음 | `CRT … MISSING`이면 5.3 |
| K41(앞·뒤) | `dwm_events=0` | 전달 DLL로 바뀐 DWM 회귀 확인 |
| K25b | driver pass=17 fail=0 | ARM64 회귀 확인(2772와 같은 값) |
| **K42 x64** | `umd crt_x64_ready=1`, `umd summary arch=x64 agx_umd=x64 … result=PASS`, `draw11 result=PASS arch=x64 … x64_umd_loaded=1`, `d3d11 device arch=x64 adapter="Apple AGX…" hr=0x00000000`, 마지막 줄 `K42 VERDICT … => PASS` | 계획서 A12 |
| K42 x64 (참고) | `umd d3d12 arch=x64 hr=0x00000000 x64_umd_loaded=1`, `d3d11 clear_readback result=PASS`, `d3d11 compute result=PASS` | D3D12·계산 |
| K42 arm64 | `umd summary arch=arm64 agx_umd=arm64 … result=PASS`, `draw11 result=PASS arch=arm64` | 전달 DLL을 거친 ARM64 경로 |

어댑터 이름만으로는 판정하지 않습니다. DXGI는 AGX UMD가 올라오지 않아도 어댑터를 "Apple AGX"로 부르므로, 프로세스 안에 `AppleAgxRenderAdmissionUmdX64.dll`이 실제로 있는지(`x64_umd_loaded=1`)를 함께 봅니다.

### 5.3 x64 C/C++ 런타임이 없을 때

`umd crt … loaded=0` 또는 I35의 `CRT vcruntime140_1.dll MISSING`이 나오면 x64 UMD는 올라오지 못합니다. Microsoft 문서([Latest supported VC++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist))는 x64 재배포 패키지가 "contains both ARM64 and X64 binaries"라고 합니다. VM에 캐시된 `VC_redist.x64.exe`(686 KB)는 내려받기용 껍데기라 쓸 수 없었습니다.

1. 콘치님이 `https://aka.ms/vc14/vc_redist.x64.exe`를 `nwoas_scripts/agx-s248/redist/vc_redist.x64.exe`로 받아 주십니다(저는 내려받지 않았습니다).
2. 매니페스트·전달 작업을 만들고(`make_job.py`), 켜진 세션에 전달한 뒤 `agx-s248/jobs/I36-vcredist-x64.cmd`를 보냅니다. 통과 기준: `S248 I36 EXIT 0`(또는 1638), `CRT` 세 줄에 판 번호.
3. 재부팅 없이 `KIT="K42"`만 다시 돌립니다.

## 6. 위험

| 위험 | 가능성 | 드러나는 곳 | 대응 |
|---|---|---|---|
| 맥미니에 x64 런타임 없음 → x64 UMD 안 올라옴(지금처럼 GPU 없이 돎) | 높음 [가설] | K42 `crt_x64_ready=0` | 5.3 |
| 전달 DLL 때문에 ARM64 프로세스(DWM)가 UMD를 못 찾음 | 낮음(원본 J313 통과, VM 전달 시험 통과) | I35 뒤 화면, K41, K25b | 7.2 되돌리기 |
| x64 UMD가 에뮬레이션에서 느림(셰이더 컴파일이 x64 코드) | 확실히 ARM64보다 느림 | 앱 첫 실행 지연 | 다음 단계 ARM64EC |
| x64 코드의 메모리 순서(Prism 에뮬레이션)와 GPU 공유 메모리 쓰기 | 모름 [가설] | x64에서만 그림 깨짐·장치 제거 | K42 `removed_reason`, 원본 J313에서는 문제 보고 없음 |
| InfVerif 1322 3건 | 2772와 같은 종류 | pnputil이 거부하면 I35 `PNPUTIL EXIT` ≠ 0 | 2772까지는 설치됨 |
| 2772로 되돌릴 때 낮은 판이라 pnputil이 바꾸지 않음 | 있음 | 되돌린 뒤 `VC … 30.0.2773.0` | R35가 2773을 먼저 지움(7.2) |
| x64 앱이 이 UMD에서 새 DDI 문제를 드러냄(게임 등) | 있음 | 앱별 | 앱별 UMD 기록(D17) |

## 7. 그 밖

### 7.1 x86(32비트) 앱

**결론: 이번에는 만들지 않았습니다. 싸게 되지 않습니다.**

| 이유 | 내용 |
|---|---|
| 빌드 재료 | x86 Mesa 구성 하나 더 + dxil-spirv x86(원본 `c49-build-dxspv.ps1`이 x64·arm64만 받음) + x86 네이티브 런타임 |
| 커널 경계 | x86 프로세스는 WOW64라 커널이 32비트 호출로 봄. Microsoft 문서대로 사설 데이터의 포인터·SIZE_T·정렬을 KMD가 따로 다뤄야 하는지 확인 필요(KMD는 이미 `UserModeDriverNameWow`를 읽는 코드가 있음, `lifecycle.c` EXP1225) |
| 설치 | `UserModeDriverNameWow` + SysWOW64 복사. Qualcomm은 같은 이름의 CHPE판을 SyChpe32에도 둠 |

### 7.2 2772로 되돌리기

**결론: 2772 파일은 맥미니 `C:\NWOAS\drop\agx-j274-2772`에 남아 있으므로 새로 보낼 것 없이 R35로 되돌립니다.**

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
python3 -I transport-s123/queue_job.py <link> agx-s248/jobs/R35-rollback-2772.cmd
bash wifi-s259/run-s259.sh <.out> <link>        # 재부팅 + D28
python3 -I transport-s123/queue_job.py <새 link> agx-s248/jobs/R35b-remove-2773-leftovers.cmd
```

| 작업 | 하는 일 | 통과 기준 |
|---|---|---|
| R35 | 2772 파일 4개 해시 확인(다르면 아무것도 안 바꾸고 멈춤) → 2773 패키지를 `pnputil /delete-driver … /uninstall /force` → 2772 설치(I34와 같은 레지스트리) | `R35 OK` 4줄, `PNPUTIL EXIT 0`, `DEV1 OK`, `VC … 30.0.2772.0`, `SYS32 AppleAgxRenderAdmissionUmd.dll 8647b5df…` |
| R35b (재부팅 뒤) | System32에 남은 UmdArm64·UmdX64 삭제(2772일 때만) | `R35b REMOVED` 두 줄 |

전달 작업 파일: 매니페스트 `file-delivery-s243/manifests/agx-j274-2773.json`(생성 `make-2773.py`), 설치 `agx-s248/jobs/I35-install-2773.cmd`, 실행 `agx-s248/run-install-2773.sh`, `agx-s248/run-2773-chain.sh`, 시험 `gpucov/jobs/K42-x64-umd.cmd`.
