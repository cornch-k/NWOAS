# J274 GPU 드라이버 — 두 번째 빌드 (2026-11-15 23:05~23:15, Parallels 가상머신)

## 결론

1. **커널 드라이버(.sys + .inf + .cat, 테스트 서명) 빌드에 성공했습니다.** 그쪽(windows-on-m1)이 J313에서 실제로 내보내는 커널 드라이버 구성(`-GpuvaG3Qualification -GpuvaG1bPageProfile 16`, Release)에 `AppleAgxBoard=J274`를 더했습니다. 오류 0, 코드 분석 경고 90개이며 경고 목록은 같은 구성의 J313 빌드와 한 줄도 다르지 않습니다. INF 검사(infverif)도 통과했습니다.
2. **ARM64 사용자 드라이버(.dll)는 빌드하지 못했습니다.** 그쪽이 내보내는 사용자 드라이버는 Mesa/Asahi로 미리 만든 묶음(`NativeRuntimeProps`)이 있어야만 빌드되는데, 그 묶음을 만들려면 그쪽 빌드 PC의 폴더 구조와 Microsoft가 아닌 곳의 내려받기(Mesa 소스, LLVM 20, Python 도구)가 필요합니다. 승인받지 않은 내려받기라 하지 않았습니다.
3. 그래서 **사용자 드라이버 없이 커널 드라이버만 담는 시험 패키지 방식(P10, `/p:AppleAgxKmdOnly=true`)을 새로 넣었습니다.** 원본 기본 빌드는 바뀌지 않습니다.
4. **빌드 성공은 GPU 동작이 아닙니다. Mac mini에서는 아무것도 시험하지 않았습니다.** 지금 Mac mini의 펌웨어에는 이 드라이버가 붙을 장치(`ACPI\APPL0002`)가 없어서, 설치해도 드라이버가 올라오지 않습니다(아래 근거).

## 환경 (사실, 1차 시도와 같음)

| 항목 | 값 |
|---|---|
| 빌드 기계 | Parallels 가상머신 "Windows 11"(ARM64), `prlctl exec`로 SYSTEM 권한 실행 |
| 컴파일러 | VS 2022 Community 17.14, MSVC 14.44.35207 `HostArm64\arm64`, MSBuild `Current\Bin\arm64` |
| WDK/SDK | WDK 10.0.26100.6584, SDK 10.0.26100 (설치판, NuGet `packages\` 폴더 없음 → `Directory.Build.props`는 아무것도 불러오지 않음) |
| 소스 | 맥 `port-j274/gpu/driver` → rsync → `/Volumes/media/nwoas/gpu-driver` → 가상머신 robocopy → `C:\nwoas\gpu-driver` |
| 원본 판 | paulsmir/windows-on-m1 `6c65673` + NWOAS-J274 패치 P1~P10 |

## 1. 그쪽 실제 빌드 구성 (사실, 소스·기록에서 확인)

| 항목 | 내용 | 근거 |
|---|---|---|
| 그쪽이 내보내는 구성 | `build-driver.ps1 -Configuration Release -GpuvaG3Qualification -UmdAdmissionTrace -NativeFrontend -GpuvaG1bPageProfile 16 -PackageBuild N` (+일부 회차는 `-EnablePreparedNirOwner`) | `external/windows-on-m1/investigation/EXPERIMENTS.md:64204`(EXP1764 "0 errors, 18 warnings"), `:64456`(EXP1821, PreparedNirOwner 없이) |
| 스크립트 제약 | G3는 `GpuvaG1bPageProfile`이 16 또는 64여야 하고 `-NativeFrontend`·`NativeRuntimeProps`(GPUVA용)·소스 목록 검증이 필수. FirmwareQualification 등 다른 단계 구성과 함께 쓸 수 없음 | `render-admission/scripts/build-driver.ps1:76-91,110-114` |
| x86 파일 | 스크립트는 `-WowOpenGLIcd`(x86 Mesa OpenGL)와 `-WowUmd`(x86 사용자 드라이버)가 없으면 시작부터 멈춤. 둘 다 Mesa 묶음으로 만듦 | `build-driver.ps1:49-70`, `umd/build-umd-x86.ps1` |
| 그쪽 CI | `.github/workflows/apple-agx-wdk.yml`은 옛 G2 드라이버(`windows/AppleAgx.vcxproj`, `admission/AppleAgxAdmission.vcxproj`)만 Debug로 빌드. 지금의 render-admission 드라이버는 CI에서 빌드하지 않음 | 같은 파일 |
| G3가 아닌 구성 | 지금 원본 코드는 G3가 아닌 커널 드라이버, Mesa가 없는 사용자 드라이버를 컴파일하지 못함(1차 시도 222개·6개 오류). 그쪽이 더는 빌드하지 않는 구성이라 원본 코드는 고치지 않음 | `BUILD-ATTEMPT-1-20261115.md` |

### 커널 드라이버에 실제로 들어간 정의 (빌드 기록의 CL 명령줄에서 그대로)

`CODE_ANALYSIS`, `APPLE_AGX_GPUVA_G3_QUALIFICATION=1`, `APPLE_AGX_EXP907_FRAME_RECEIPT=1`, `DXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_WDDM3_2`, `ADMISSION_GPUVA_G1B_PAGE_PROFILE=16`, `ADMISSION_GPUVA_G1B_ALLOCATION_HINT=0`, `APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY=1`, `APPLE_AGX_FULL_CONTEXT0_BROKER=1`, `APPLE_AGX_VERSION_BUILD=2741`, `APPLE_AGX_BOARD_J274=1` + WDK 기본값(`_ARM64_`, `ARM64`, `_WIN32_WINNT=0x0A00`, `NTDDI_VERSION=0xA000010` 등). 옵션 `/kernel /W4 /WX /Qspectre /guard:cf /analyze`(DriverMinimumRules).

### 그쪽 사용자 드라이버 구성 (빌드 못 함)

`EnableNativeFrontend=true`, `EnableGpuvaWinsys=true`, `AppleAgxUmdAdmissionTrace=true` → `APPLE_AGX_UMD_ADMISSION_TRACE=1`, `APPLE_AGX_UMD_NATIVE_FRONTEND=1`, `APPLE_AGX_GPUVA_WINSYS=1`, `APPLE_AGX_EXP907_FRAME_RECEIPT=1`, `ADMISSION_UMD_PIPE_FACTORY_TEST=1`, `APPLE_AGX_D3D12_NATIVE_OWNER=1`, `/MD`, 그리고 `$(NativeRuntimeProps)`를 불러옴(`umd/AppleAgxRenderAdmissionUmd.vcxproj:39-46,103-108,160`).

## 2. 실행한 명령 (그대로)

스크립트 원본: `/Volumes/media/nwoas/out/j274-20261115-2310/scripts/` (`sync.cmd`, `kmd.cmd`, `umd.cmd`, `collect.cmd`). 실행은 맥에서:

```
rsync -a --delete --exclude BUILD-ATTEMPT-1-20261115.md port-j274/gpu/driver/ /Volumes/media/nwoas/gpu-driver/
prlctl exec "Windows 11" cmd /c '\\Mac\media\nwoas\build2\sync.cmd'
prlctl exec "Windows 11" cmd /c '\\Mac\media\nwoas\build2\kmd.cmd J274 j274-g3 Arm64Only'      (이때는 /p:AppleAgxArm64Only=true 고정)
prlctl exec "Windows 11" cmd /c '\\Mac\media\nwoas\build2\umd.cmd J274 j274-native-nomesa'
prlctl exec "Windows 11" cmd /c '\\Mac\media\nwoas\build2\kmd.cmd J274 j274-g3-kmdonly KmdOnly'
prlctl exec "Windows 11" cmd /c '\\Mac\media\nwoas\build2\kmd.cmd J313 j313-g3-kmdonly KmdOnly'
```
(`build2` 폴더는 끝난 뒤 지우고 스크립트를 산출물 폴더로 옮김.)

`kmd.cmd`가 부르는 MSBuild 명령(= `build-driver.ps1`의 커널 드라이버 단계, 값만 채움):

```
MSBuild.exe drivers\apple-agx\render-admission\AppleAgxRenderAdmission.vcxproj /m /t:Clean,Build
 /p:Configuration=Release /p:Platform=ARM64 /p:RunCodeAnalysis=true /p:Inf2CatUseLocalTime=true /p:AppleAgxVersionBuild=2741
 /p:AppleAgxMemoryQualification=false /p:AppleAgxManagementQualification=false /p:AppleAgxRetainedRootQualification=false
 /p:AppleAgxStopAfterEndpoints=false /p:AppleAgxFirmwareQualification=false /p:AppleAgxBackendQualification=false
 /p:AppleAgxSubmitQualification=false /p:AppleAgxGpuvaB1Qualification=false /p:AppleAgxGpuvaG3Qualification=true
 /p:AppleAgxBltProbeQualification=false /p:AppleAgxVisibleScanoutQualification=false /p:AppleAgxVisibleAgxQualification=false
 /p:AppleAgxGpuvaG1bPageProfile=16 /p:AppleAgxGpuvaG1bAllocationHint=0 /p:AppleAgxBoard=J274 /p:AppleAgxKmdOnly=true
 /p:WindowsTargetPlatformVersion=10.0.26100.0
 "/p:WDKContentRoot=C:/Program Files (x86)/Windows Kits/10/" "/p:WindowsSdkDir=C:/Program Files (x86)/Windows Kits/10/"
 "/p:UniversalCRTSdkDir=C:/Program Files (x86)/Windows Kits/10/"
 "/p:UniversalCRT_IncludePath=C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt"
 "/p:UniversalCRT_LibraryPath_arm64=C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/arm64"
 "/p:WindowsSDK_LibraryPath_ARM64=C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/arm64"
```

`PackageBuild`는 그쪽 기본값 461 대신 2741을 써서 패키지 판이 `30.0.2741.0`입니다(그쪽 판과 헷갈리지 않게 하려는 것).

## 3. 결과

| 대상 | 결과 | 오류 | 경고 | 시간 |
|---|---|---|---|---|
| 커널 드라이버 J274, G3, P9 패키지(`Arm64Only`) | .sys 컴파일·서명 성공, **패키지 실패** | 1 (`MSB3030`: `AppleAgxRenderAdmissionUmd.dll` 없음) | — | 약 50초 |
| 커널 드라이버 J274, G3, **P10 패키지(`KmdOnly`)** | **성공** | 0 | 90 (코드 분석) | 약 50초 |
| 커널 드라이버 J313, G3, P10 (비교용) | 성공 | 0 | 90, J274와 목록 동일 | 약 50초 |
| 사용자 드라이버 J274, 그쪽 구성(Mesa 포함) | **실패** | 1 (`MSB4020`, `AppleAgxRenderAdmissionUmd.vcxproj(160)`: 불러올 `NativeRuntimeProps` 없음) | — | 수 초 |
| 사용자 드라이버, Mesa 없는 구성 (1차) | 실패 | 6 | — | — |
| 커널 드라이버, FirmwareQualification 구성 (1차) | 실패 | 222 | — | — |

패키지 검사 (J274, P10)

| 검사 | 결과 |
|---|---|
| `file` | `PE32+ executable (native) Aarch64, for MS Windows` |
| ApiValidator (Universal) | 통과(빌드 중) |
| stampinf | `DriverVer` 30.0.2741.0, ARM64 |
| inf2cat `/os:Server10_arm64` | Errors: None, Warnings: None |
| infverif `/w /v` | `INF is VALID` |
| 서명 | .sys와 .cat 모두 WDK 테스트 인증서 `WDKTestCert SYSTEM,134392251612905395`(SHA1 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF`, 만료 2036-11-15). `signtool verify /pa`는 "신뢰하지 않는 루트"로 실패 — 테스트 서명이라 당연함 |
| J274 주소가 들어갔는가 | .sys 안의 8바이트 상수 개수: J274 빌드 `0xbfffb8000` 9개·`0xbfff70000` 4개·`0x9fffb8000` 1개 / J313 빌드 `0x9fffb8000` 9개·`0x9fff70000` 4개. J274 빌드에 남은 `0x9fffb8000` 1개는 HwDataB 원판 바이트(`apple_agx_hwdata_profile.generated.h:1279`, 파일 위치 0x854d8)로, P5b가 실행 중에 J274 값으로 덮어씀 |

경고 90개는 전부 정적 분석(C28167, C28213, C28251, C6011, C6385/6386, C28715 등)이며 원본 코드 쪽입니다. 그쪽 기록(EXP1764)은 18개였는데, 그쪽은 사용자 드라이버까지 합친 다른 집계라 직접 비교는 못 합니다 [확인 안 함].

## 4. 산출물과 해시

폴더: `/Volumes/media/nwoas/out/j274-20261115-2310/` (가상머신에서는 `\\Mac\media\nwoas\out\j274-20261115-2310\`)

| 파일 | sha256 |
|---|---|
| `kmd-j274-g3-kmdonly/package/AppleAgxRenderAdmission.sys` | `1965872e848383311793a6383ca289748c42e50b3ff27a2c0798d9a4e70ee580` |
| `kmd-j274-g3-kmdonly/package/AppleAgxRenderAdmission.kmdonly.inf` | `24e5a1a047e94f7cb435949c04468288648b08da1db5aa07f0461c177938ef44` |
| `kmd-j274-g3-kmdonly/package/appleagxrenderadmission.cat` | `7e8f980d7e4436b5355f155610524af23025f16755e5d1334b6a528edcc1ce5a` |
| `kmd-j274-g3-kmdonly/AppleAgxRenderAdmission.pdb` | `d4855614a89f748236532c208474c11f152a18bf2693fc39bfcd4a396be44e7f` |
| `kmd-j274-g3-kmdonly/AppleAgxRenderAdmission.cer` (테스트 인증서) | `f7b551e680907726fc506fff9ad906735384f9beeb779fbecc214b09228f1c57` |
| `baseline-j313-g3-kmdonly/package/AppleAgxRenderAdmission.sys` (비교용, Mac mini에 넣지 말 것) | `137e33b7a8dbe41cd135084aa4591a7decade51e2ce18984992cfc9a43573d75` |
| `baseline-j313-g3-kmdonly/package/appleagxrenderadmission.cat` | `46f509b1807495eec5b378d252d0c53b1d9b31dced8ea5d1c1bad348530d960e` |

같은 폴더에 `SHA256SUMS.txt`, `build-options.txt`(쓴 옵션·정의), `logs/`(UTF-8로 바꾼 MSBuild 기록), 각 결과 폴더의 `infverif.txt`, `signtool-verify-cat.txt`, `release-dir.txt`가 있습니다. 원래 기록(CP949)은 `/Volumes/media/nwoas/build-logs/b2/`.

## 5. J274 때문에 바꾼 소스

| 번호 | 파일 | 내용 |
|---|---|---|
| P10 (새로) | `render-admission/AppleAgxRenderAdmission.kmdonly.inf`(새 파일), `render-admission/AppleAgxRenderAdmission.vcxproj` | `/p:AppleAgxKmdOnly=true`이면 .sys만 담은 패키지. INF에서 사용자 드라이버 복사, `UserModeDriverName`, `OpenGLDriverName`을 뺌. 장치 ID·서비스·`Supports64KBPages`는 그대로. 속성을 안 주면 이전과 같음 |

J274 패치(P1~P9) 때문에 생긴 컴파일 오류는 없었습니다(J313과 오류·경고가 같음). 원본 코드는 고치지 않았습니다. `PATCHES.md`에 P10을 넣었고 `j274-patch.diff`를 다시 만들었습니다(14개 파일). `build-driver.ps1`에는 P10 인자를 넣지 않았습니다(스크립트가 x86 파일·사용자 드라이버를 필수로 요구해서 어차피 이 환경에서는 못 씀).

## 6. 막힌 곳 (사용자 드라이버)

| 필요한 것 | 그쪽 경로(빌드 PC 기준) | 근거 |
|---|---|---|
| Mesa 소스(커밋 `9aa1215f` + 그쪽 덧씌우기) | `AD04-d3d10-frontend-build\mesa` | `mesa/scripts/build-asahi-runtime-closure.py:24`, `mesa/mesa-source.lock.json` |
| LLVM 20 `clang-cl` | `AD04-asahi-windows-compiler\llvm20\bin` | 같은 파일 `:25` |
| Mesa meson 생성물 | `AD04-asahi-windows-compiler\b5\generated` | `:26` |
| Asahi 셰이더 생성 헤더(`libagx_shaders.h` 등) | `AD04-umd-owner-004-generated` | `:27`, `build-native-asahi-state.py:6087` |
| uatomic 헤더 | `AD04-uatomic\include` | `build-asahi-runtime-closure.py:564` |
| Python 3.13 + meson 1.8.3, ninja, Mako, PyYAML, packaging, ply | `AD04-fullcompiler-001\venv` | `mesa/scripts/setup-full-compiler.ps1` |
| 위를 묶는 단계 | `build-native-asahi-state.py` → `build-asahi-runtime-closure.py` → `NativeRuntime.props` + `native_runtime.lib` | `mesa/scripts/run-asahi-runtime-closure.ps1` |

모든 스크립트의 기본 경로가 `C:\Users\pauls\APB12E~1\active`로 박혀 있습니다(`APPLE_AGX_BUILDER_ROOT`로 바꿀 수 있음). 위 폴더들을 처음부터 만드는 절차가 저장소에 전부 있는지는 확인하지 못했습니다 [확인 안 함]. 내려받기(Mesa, LLVM, Python 패키지, dxil-spirv 등)는 Microsoft 도구가 아니라 콘치님 승인이 필요해 하지 않았습니다. README의 D9~D11과 LLVM 20(목록에 없음)이 여기에 해당합니다.

## 7. 대상 기계(Mac mini)에 설치하려면 필요한 것

| 순서 | 필요한 것 | 지금 상태 | 근거 |
|---|---|---|---|
| 1 | 테스트 서명 허용(`bcdedit /set testsigning on`, Secure Boot 꺼짐) + 테스트 인증서(`AppleAgxRenderAdmission.cer`)를 Root·TrustedPublisher에 등록 | 확인 안 함 | .sys/.cat이 WDK 테스트 인증서로만 서명됨 |
| 2 | Windows 칸의 13.5 펌웨어 묶음 스텁(결정 D1): 드라이버의 HwData 프로필·RegionC 값이 13.5 기준 | 진행 전 | `README.md` "실기기에서 시험하기 전에 필요한 것" 1번 |
| 3 | ACPI 장치 `ACPI\APPL0002` (INF가 맞추는 유일한 ID, `kmdonly.inf`의 `[Models.NTarm64.10.0...22000]`) | **없음.** 지금 Mac mini 펌웨어(S242 실행 = `fw-s237` 페이로드)의 소스 `fw-s237-mu/Platform/MacMini2020Pkg/AcpiTables/`에 `APPL0002`가 없고, `apple_silicon_platforms_mu`·다른 `fw-s237-mu*` 트리의 MacMini2020Pkg에도 없음(grep) [사실, 소스 기준. 실기기에서는 아래 1단계로 확인] | 위 grep |
| 4 | ACPI 초안 `acpi-draft/J274AppleAgxAbiAdmission.asl.inc` | `_HID "APPL0002"`라 INF와 맞음. 단 G3 드라이버는 메모리 5개(GPU 레지스터, GPU 영역, 핸드오프, 중개 페이지 0x300000000, 예약 메모리) + 인터럽트 1개가 모두 있어야 시작함. 초안의 `BAS0`(하이퍼바이저 영수증이 없을 때)는 메모리 4개라 드라이버가 하드웨어를 건드리기 전에 `STATUS_DEVICE_NOT_READY`로 멈춤. 5개짜리 `LOCR`는 하이퍼바이저가 0x300000D00에 `AGXL` 영수증(`LMAG=0x4C584741`, 판 1, 유효 1)을 써 줘야 나옴 | `render-admission/src/lifecycle.c:120-172,273-282`, 초안 `_CRS` |
| 5 | 하이퍼바이저 GPU 중개: HVC `0x4d31`/`0x4d32`, 0x300000000 중개 페이지와 예약 메모리 영수증, GPU 레지스터 0x204000000 통과, 인터럽트 889, 보드 0x22 허용, HwData 프로필 일치 | 지금 하이퍼바이저(S233 계열 `m1n1-s233b-ringclean.bin`)에는 없음. S233은 모르는 HVC를 처리하지 않고 프록시로 넘겨 게스트가 멈춤(`m1n1-usb-s233/src/hv_exc.c:2049-2059`) | `PATCHES.md` "다른 담당과 맞물리는 것" |
| 6 | J274 실제 값(O1 성능 단계, O2 전원 입력 66개·HwData, O3 RegionC) | 진행 전. 값이 다르면 하이퍼바이저·드라이버가 시작을 거부(의도된 안전 정지) | `PATCHES.md` O1~O3 |
| 7 | 매 시험마다 1회용 허용 값 `G3Armed=1`(DWORD, `HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters`). 드라이버가 읽고 지운 뒤 HVC `0x4d32`로 하이퍼바이저에 알림. 없으면 `STATUS_NOT_SUPPORTED`로 하드웨어를 건드리지 않고 멈춤 | — | `lifecycle.c:65-113,278-282`, 그쪽 `windows/scripts/arm-gpuva-experiment.ps1` |

화면: 이 구성은 펌웨어 시작(플랫폼 시작, `lifecycle.c:399`) 뒤에 UEFI 화면 크기를 검사하고, 2560x1600 내장 패널이 아니면 펌웨어를 다시 멈추고 `STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER`(0xC01E0002)로 끝납니다(`lifecycle.c:425-476`). 지금 Mac mini 화면은 1920x1080(S242)이라 **이 빌드는 J274에서 "펌웨어 켜고 → 화면 단계에서 멈추기"까지만 갈 수 있습니다** [사실은 소스 순서, 실제 동작은 가설].

## 8. 제안하는 실기기 시험 순서 (Mac mini, 아직 하지 않음)

통로 제약: 원격 작업 통로는 ASCII `.cmd` 파일(4032바이트 이하)만 되고 PowerShell 출력은 돌아오지 않으므로, 아래는 전부 CMD 기본 명령(`pnputil`, `certutil`, `reg`, `bcdedit`, `wevtutil`)이며 결과를 파일로 남겨 `type`으로 돌려받는 방식입니다. 하이퍼바이저 위 Windows가 재시작하면 Mac mini 전체가 재부팅되므로 런처를 다시 띄워야 합니다.

| 단계 | 할 일 | 바뀌는 것 | 성공·실패를 보는 법 |
|---|---|---|---|
| 0. 지금 상태 읽기(바꾸는 것 없음) | `bcdedit /enum {current} > r0.txt`, `pnputil /enum-devices /deviceid "ACPI\APPL0002" >> r0.txt`, `pnputil /enum-drivers >> r0.txt` | 없음 | `testsigning` 줄 유무, APPL0002 장치가 "없음"으로 나오는지(지금은 없어야 정상) |
| 1. 패키지만 넣어 두기(지금 펌웨어에서도 안전) | 패키지 3개 + `.cer`를 넘겨 받은 뒤 `certutil -addstore Root AppleAgxRenderAdmission.cer`, `certutil -addstore TrustedPublisher AppleAgxRenderAdmission.cer`, `pnputil /add-driver AppleAgxRenderAdmission.kmdonly.inf` (`/install` 없이) | 드라이버 저장소에 `oemNN.inf` 추가, 인증서 2개 | `pnputil /enum-drivers`에 원래 이름 `AppleAgxRenderAdmission.kmdonly.inf`, 판 30.0.2741.0. 장치가 없으니 드라이버는 올라오지 않음 [가설: 테스트 서명 없이도 넣어 두기는 될 수 있으나, 시작하려면 testsigning 필요] |
| 2. 테스트 서명 켜기 | `bcdedit /set testsigning on` 후 재부팅(Mac mini 전체 재부팅) | 부팅 설정 | 바탕화면에 "테스트 모드" 글자, `bcdedit` 확인. Secure Boot가 켜져 있으면 거부됨 |
| 3. ACPI 장치만 넣기(하이퍼바이저 중개 없이) | ACPI 담당이 초안을 넣은 Mu 페이로드로 부팅 | 펌웨어 | 드라이버가 붙고 `BAS0`(메모리 4개)라 하드웨어를 건드리기 전에 멈춰야 함: 장치 관리자 오류 코드 10, `reg query "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters"`에 `Wom1TranslatedResources` 같은 `Wom1*` 기록. **위험 [가설]:** 초안의 `LREG`가 0x300000D00을 읽으므로, 하이퍼바이저가 그 주소를 매핑하지 않았으면 부팅 중 ACPI가 `_CRS`를 읽을 때 게스트가 멈출 수 있음 → 이 단계 전에 하이퍼바이저가 그 페이지를 0으로라도 보여 주는지 확인 |
| 4. 하이퍼바이저 GPU 중개 + J274 값이 준비된 뒤 1회 시험 | `reg add "HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters" /v G3Armed /t REG_DWORD /d 1 /f` 후 `pnputil /restart-device "ACPI\APPL0002\0"` | 1회용 허용 값(드라이버가 지움) | 하이퍼바이저 로그 `HV: retained Hwdata input profile matched=1`, 펌웨어 시작 기록, 이어서 화면 단계 거부(0xC01E0002)로 코드 10. `matched=0`이면 O2 값이 다르다는 뜻(의도된 안전 정지). `wevtutil qe System /c:30 /rd:true /f:text /q:"*[System[Provider[@Name='Microsoft-Windows-Kernel-PnP']]]"`로 시작 실패 기록 |
| 되돌리기 | `pnputil /enum-drivers`에서 `oemNN.inf` 찾기 → `pnputil /delete-driver oemNN.inf /uninstall /force` → `reg delete "...\Device Parameters" /v G3Armed /f` → `certutil -delstore TrustedPublisher <일련번호>`, `certutil -delstore Root <일련번호>` → `bcdedit /set testsigning off` | 원래대로 | 가장 확실한 되돌리기는 **AGX0가 없는 지금 펌웨어(S242)로 부팅**하는 것: 장치가 없으면 드라이버는 절대 올라오지 않음. 부팅이 안 되면 WinRE 명령 창에서 `dism /image:C:\ /remove-driver /driver:oemNN.inf` |

멈춤·블루스크린이 나면 하이퍼바이저 직렬 로그(알 수 없는 HVC, 데이터 중단)와 위 `Wom1*` 기록이 원인을 가리는 주 신호입니다. 블루스크린은 Mac mini 전체 재부팅으로 이어집니다.

## 9. 정직성 표시

- 이 문서의 어떤 결과도 GPU가 동작한다는 뜻이 아닙니다. 빌드·서명·INF 검사만 가상머신에서 통과했습니다.
- Mac mini에서는 설치·실행하지 않았습니다. 하드웨어 검증 0건입니다.
- J274 주소 값(GPU 영역 0xbfffb8000, 핸드오프 0xbfff70000, 공유 영역 0xbfff78000)은 여전히 가설입니다(`apple_agx_board.h`). 전원·성능 값(O1~O3)은 J313 값 그대로라 J274에서는 시작이 거부될 가능성이 큽니다.
- 사용자 드라이버 없이 커널 드라이버만 넣었을 때 Windows가 장치를 시작하는 데 문제가 없는지는 확인하지 않았습니다 [가설: D3D만 못 쓰고 커널 시작은 됨].
- 가상머신 안 `C:\nwoas\gpu-driver`에 빌드 중간 파일이 남아 있고(빌드 폴더), WDK가 SYSTEM 인증서 저장소에 `WDKTestCert SYSTEM` 테스트 인증서를 만들어 두었습니다.
