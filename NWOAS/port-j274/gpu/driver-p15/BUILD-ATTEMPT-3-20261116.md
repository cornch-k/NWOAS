# J274 GPU 드라이버 — 세 번째 빌드: 사용자 드라이버까지 포함한 전체 패키지 (2026-11-16 11:35~12:27, Parallels 가상머신)

## 결론

1. **커널 드라이버(.sys) + 사용자 드라이버(.dll, Mesa/Asahi 런타임 포함) + .inf + .cat 전체 패키지를 테스트 서명까지 빌드했습니다.** 원본(paulsmir/windows-on-m1)이 실제로 내보내는 G3 구성(`-GpuvaG3Qualification -UmdAdmissionTrace -NativeFrontend -GpuvaG1bPageProfile 16`, Release)에 `-Board J274`를 더해 원본 `build-driver.ps1`로 빌드했습니다. 원본 스크립트의 소스 무결성 검사(975개 파일)와 런타임 출처 검사가 모두 통과했습니다.
2. 2차 시도에서 막혔던 **Mesa 런타임 묶음(`NativeRuntime.props` + `native_runtime.lib`)을 원본 빌드 PC 폴더 구조 그대로 가상머신 안에 처음부터 만들었습니다.** 원본이 고정해 둔 생성 헤더 `libagx_shaders.h`의 해시(b10e6ec3…)가 우리가 새로 만든 것과 **정확히 같았습니다.**
3. **32비트(x86) 부품 두 개(WOW64 OpenGL·D3D 사용자 드라이버)는 넣지 않았습니다.** x86 Mesa와 x86 런타임 묶음을 따로 만들어야 하는 큰 작업이라, 원본 기본 동작은 그대로 두고 켜야만 동작하는 `-Arm64OnlyPackage`(P11)로 ARM64 전용 패키지를 만들었습니다.
4. 코디네이터가 준 **Mac mini 실측 13.5 ADT로 만든 J274 전원·성능 값(Hwdata) 헤더를 J274 빌드에서만 쓰게 했습니다(P12).** .sys 안에 J274 프로필 ID가 들어가 있고 J313 프로필 ID와 J313 주소(0x9fffb8000)는 하나도 남아 있지 않습니다.
5. **빌드 성공은 GPU 동작이 아닙니다. Mac mini에는 아무것도 설치·실행하지 않았습니다.** 하드웨어 검증 0건입니다.

## 결과

| 대상 | 결과 | 오류 | 경고 | 시간 |
|---|---|---|---|---|
| 사용자 드라이버 ARM64 (G3, Mesa 런타임 링크, `/MD`) | 성공 | 0 | 0 (원본 G3도 이 단계 코드 분석 끔) | 약 11초(링크 포함) |
| CRT assert 검사(`check_no_crt_asserts.py`) | 통과(assert 경로 0개) | — | — | — |
| 커널 드라이버 ARM64 (G3, J274, P9 INF) | 성공 | 0 | 45 (코드 분석 C6011 13·C28213 11 등, 기록에는 같은 줄이 두 번씩 찍혀 90줄. 2차 보고서의 "90개"도 같은 이중 집계였던 것으로 보임) | 약 52초 |
| 패키지 판 검사(`verify-package-version.ps1`) | `30.0.2742.0` 일치 | — | — | — |
| infverif 기본(`/v`) | **INF is VALID** | — | — | — |
| infverif Windows Driver 규칙(`/w /v`) | 실패: ERROR 1322 (사용자 드라이버를 System32=DIRID 11로 복사) | 1 | — | — |
| 같은 검사를 **원본 INF**에 | 같은 1322 오류 2건(원본 설계 그대로) | 2 | — | — |
| 서명 | .sys/.dll/.cat 모두 `WDKTestCert SYSTEM,134392251612905395` 테스트 인증서. `signtool verify /pa`는 "신뢰하지 않는 루트"로 실패(테스트 서명이라 당연) | — | — | — |

ERROR 1322는 원본 INF에서도 똑같이 나므로 우리 패치 때문이 아닙니다. 원본은 사용자 드라이버를 System32에 복사하는 설계이고 그대로 두었습니다.

### .sys 안의 J274 값 확인

| 8바이트 값 | 개수 |
|---|---|
| 0xbfffb8000 (J274 GPU 영역) | 10 |
| 0xbfff70000 (J274 핸드오프) | 4 |
| 0x9fffb8000 / 0x9fff70000 (J313) | 0 / 0 |
| J274 Hwdata 프로필 ID 32바이트 | 1 |
| J313 Hwdata 프로필 ID 32바이트 | 0 |

2차 빌드에 남아 있던 0x9fffb8000 1개(J313 HwDataB 원판 바이트)는 P12로 실측 HwDataB를 쓰면서 없어졌습니다. 실측 ADT의 `gpu-region-base`도 0xbfffb8000이었고 HwDataB 0xa78 위치 값도 0xbfffb8000이었습니다. 그래서 P1의 J274 GPU 영역 주소는 이제 가설이 아니라 **실측 ADT와 일치하는 값**입니다(핸드오프·공유 영역 주소는 여전히 ADT에서 따로 확인하지 않음).

## 산출물과 해시

폴더: `/Volumes/media/nwoas/out/j274-full-20261116-1224/` (같은 사본: `port-j274/gpu/driver/out/j274-full-20261116-1224/`, `SHA256SUMS.txt` 56개 전부 일치 확인)

| 파일 | sha256 |
|---|---|
| `package/AppleAgxRenderAdmission.sys` | `66d650b4d714eccb77c90b2cd84d630ebbae1a6f405a44a3c8c53a196e567020` |
| `package/AppleAgxRenderAdmissionUmd.dll` | `416e3edd729d230828493836a92cb345803097459273f9dffb6d41003fd476cc` |
| `package/AppleAgxRenderAdmission.arm64only.inf` | `d72f8d4e9e4bff708e75ab24167863093e9378857fa4569ed8faa2882c4b27fa` |
| `package/appleagxrenderadmission.cat` | `4abcf56791b808f3b1a8d96588c85a63b99b8067c3cc0fc34c47f0c7754c439b` |
| `symbols/AppleAgxRenderAdmission.cer` (테스트 인증서, 2차와 같음) | `f7b551e680907726fc506fff9ad906735384f9beeb779fbecc214b09228f1c57` |
| `native-runtime/native_runtime.lib` | `d97a1d0320c51d533ef3de5702b7481aefaf2b6f06ddf4aa3b18c7c67851774b` |
| `native-runtime/libagx_shaders.h` (원본 고정값과 같음) | `b10e6ec36950ecd55878f4665f644969a93ba7bdbeb75f7c6584e8a1da11bc37` |
| `native-runtime/libagx_shaders.c` (미리 컴파일한 GPU 셰이더) | `78a1cccd7515963ba0bcbd8ca370f9fe395bb8cf2d1482b43ec7d8df5c8773ca` |
| `source/source-manifest.json` (가상머신 안 로컬 커밋 `3fbc55fc…`, 975개 파일) | `5c53fa5e3eeb2e38cf2a718cccb7d9bae2b4e7c679b062ef04ed775f71388a42` |

같은 폴더에 `symbols/`(PDB·map·assert 검사 결과), `native-runtime/NativeRuntime.props`·`result.json`·`prepared-result.json`, `logs/`(단계별 기록, infverif, signtool), `scripts/`(아래 재현 스크립트 전부)가 있습니다.

## 재현 방법

스크립트 원본: `/Volumes/media/nwoas/agx-build/scripts/` (산출물 폴더 `scripts/`에도 사본). 빌드 루트는 원본의 `C:\Users\pauls\APB12E~1\active` 대신 `APPLE_AGX_BUILDER_ROOT=C:\agx`이고, 그 아래 폴더 이름은 원본과 같습니다. 맥에서 순서대로:

```
rsync -a --delete --exclude out/ port-j274/gpu/driver/ /Volumes/media/nwoas/gpu-driver/
V='prlctl exec "Windows 11"'; S='\\Mac\media\nwoas\agx-build\scripts'
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\01-download.ps1     # 내려받기(아래 표)
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\02-setup.ps1        # Python venv(meson 1.8.3 등 원본 고정판), LLVM 20.1.8, MinGit, win_flex_bison, C:\VS2022Community 연결
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\03-sources.ps1      # 저장소 사본, Mesa 9aa1215f + shader_info 투영, dxil-spirv 소스 + c49 패치(원본 스크립트)
$V cmd /c $S\04-clc-deps.cmd                                                  # pkgconf, libxml2, SPIRV-Tools, SPIRV-LLVM-Translator (생성 도구용)
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\05-nir-dxspv.ps1    # 원본 configure-arm64-nir.ps1 -NDebug / build-arm64-nir.ps1 / c49-build-dxspv.ps1 -Architecture arm64
$V cmd /c $S\05b-nir-targets.cmd                                              # 런타임이 쓰는 Mesa 라이브러리 6개를 명시적으로 빌드
$V cmd /c $S\06-mesa-gen.cmd ; $V cmd /c $S\06c-mesa-gen-fix.cmd ; $V cmd /c $S\06d-libpoly.cmd   # 생성 트리용 Mesa(mesa_clc, vtn_bindgen2)
$V cmd /c $S\07-manifest.cmd                                                  # 가상머신 안 로컬 git 커밋 + 원본 g3_build_source_manifest.py
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\08a-generated.ps1   # 생성 트리, 원본 build-full-asahi.py, asahi_clc, libagx_shaders
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\08b-runtime.ps1 -Run j274g3d   # 원본 build-native-asahi-state.py --gpuva --prepare-only, build-asahi-runtime-closure.py --gpuva
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\09-driver.ps1 -Run j274g3d -Stamp <시각>   # 원본 build-driver.ps1 (아래 인자)
$V powershell -NoProfile -ExecutionPolicy Bypass -File $S\10-collect.ps1 -Run j274g3d -Stamp <시각>
```

`09-driver.ps1`이 부르는 명령(원본 `g3-verified-build.ps1`과 같은 인자 + 우리 인자):

```
build-driver.ps1 -Configuration Release -GpuvaG3Qualification -UmdAdmissionTrace -NativeFrontend
  -NativeRuntimeProps C:\agx\AD04-fullcompiler-001\asahi-runtime-arm64-j274g3d\NativeRuntime.props
  -MesaSourceRoot C:\agx\AD04-d3d10-frontend-build\mesa -MesaGeneratedRoot C:\agx\AD04-asahi-windows-compiler\b5\generated
  -PackageBuild 2742 -GpuvaG1bPageProfile 16 -SourceManifestPath C:\agx\source-manifest\source-manifest.json
  -NativeProvenancePython C:\agx\AD04-fullcompiler-001\venv\Scripts\python.exe
  -Board J274 -Arm64OnlyPackage -KeepUmdDynamicCrt
```

### 내려받은 것 (전부 무료 공개 소스·도구, 가상머신 `C:\agx\dl`)

| 무엇 | 판 | sha256 |
|---|---|---|
| Mesa | 커밋 `9aa1215f878b504f66159dd2ead4c7973142126e` (원본 `mesa-source.lock.json`) | `e3295090…3d57` (라이선스 파일 해시 원본 고정값과 같음) |
| LLVM | 20.1.8 `clang+llvm-…-aarch64-pc-windows-msvc.tar.xz` (공식 릴리스) | `0df3e81e…0c13` |
| Python | 3.13.17 ARM64 (nuget `pythonarm64`) + 원본 고정 meson 1.8.3·ninja 1.11.1.4·Mako 1.3.10·PyYAML 6.0.2·packaging 25.0·ply 3.11 | `aaa8c60c…456b` |
| win_flex_bison | 2.5.25 | `8d324b62…c135` (원본 `enable-full-nir.ps1` 고정값과 같음) |
| dxil-spirv / dxbc-spirv / SPIRV-Headers | `8434b063` / `8dae2d85` / `c63848ec` (+ 하위 `c8ad050f`, `199cb207`, `be71ee8c`) | 앞 3개는 원본 `dxil_spirv_dependency_lock.json` 해시와 같음 |
| CMake | 3.31.6 (PyPI, 원본 `c49-build-dxspv.ps1`의 `cmake-tool\cmake\data\bin` 배치) | — |
| SPIRV-Tools / SPIRV-Headers | vulkan-sdk-1.4.341.0 | `15bfb678…` / `cab0a654…` |
| SPIRV-LLVM-Translator | v20.1.19 (+ 헤더 `cb42dec3`) | `67cd56f2…` |
| pkgconf / libxml2 | 2.3.0 / 2.13.8 | — |
| MinGit | 2.56.1 arm64 | `96571e44…` |

### 단계별 확인값

| 단계 | 결과 |
|---|---|
| shader_info 투영(원본 `project-shader-info-abi.py`) | `551fc36c…` — 원본 기록(EXP1811)과 같음 |
| `agx_state.c` 원본 고정 해시 | `5015b758…` 같음 |
| dxil-spirv c49 패치 후 `dxil_arithmetic.cpp` | `f90631e4…` — 원본 기록(EXP1858)과 같음 |
| ARM64 Mesa(`nir-arm64`, `b_ndebug=true`) | 라이브러리 6개 생성, 컴파일 명령에 `nir.c`·`gtest-all.cc` 있음 |
| 원본 `build-full-asahi.py --architecture arm64` | `ARM64_BUILD_LINK_PASS EXECUTION=NOT_RUN`(원본도 ARM64는 실행 안 함) |
| `libagx_shaders.h` | `b10e6ec3…` — 원본 `build-native-asahi-state.py`의 고정값과 같음 |
| 원본 `build-native-asahi-state.py --gpuva --prepare-only` | exit 0 |
| 원본 `build-asahi-runtime-closure.py --gpuva` | `NATIVE_RUNTIME_ARCHIVE_PASS`, 160개 단위, 라이브러리 15개(DXIL 포함) |
| 원본 `verify-committed-sources.ps1` / `verify-native-runtime-provenance.py` | 둘 다 PASS |

시간: 내려받기·도구 준비 약 25분, dxil-spirv 약 10분, 생성용 Mesa 약 15분, 런타임 묶음 약 1분, 드라이버 패키지 약 65초. 시행착오 포함 전체 약 50분(11:35~12:27).

## 원본과 다른 점

### 저장소 패치 (원본 기본 빌드에는 영향 없음, `PATCHES.md`)

| 번호 | 내용 |
|---|---|
| P11 | `build-driver.ps1 -Arm64OnlyPackage`: x86 부품 없이 P9 INF로 패키지. `verify-package-version.ps1 -InfName` |
| P12 | `AppleAgxBoard=J274`이면 실측 13.5 ADT로 만든 Hwdata 프로필 헤더 사용. J313 전처리 결과 바이트 동일 확인 |
| P13 | `build-driver.ps1 -KeepUmdDynamicCrt`: 설치형 WDK의 `SetHybridCrtCl`이 G3 사용자 드라이버의 `/MD`를 `/MT`로 바꿔 Mesa 묶음과 링크 실패(LNK2038)하던 것을 `/p:OverrideDefaultRuntimeLibrary=true`로 막음. 원본 프로젝트가 `/MD`와 `msvcrt.lib;ucrt.lib;vcruntime.lib`를 명시하므로 원본 의도와 같은 결과 [가설: 원본 빌드 PC는 NuGet WDK라 이 변환이 안 일어났을 것, 확인 안 함] |

### 빌드 PC 쪽 차이 (저장소 밖, `C:\agx`)

| 무엇 | 왜 |
|---|---|
| `compiler-arm64.ini`의 경로 상수를 `C:/agx`로 | 원본은 원본 빌드 PC 경로가 박혀 있음 |
| `C:\VS2022Community` → 실제 VS 폴더 연결(junction) | 원본 스크립트가 이 경로를 고정 사용 |
| ARM64 Mesa를 `-NDebug`로 구성하고 라이브러리 6개를 이름으로 빌드 | 원본 `build-arm64-nir.ps1`(기본 대상)로는 47개 대상만 빌드되고 라이브러리가 안 생김. NDEBUG는 패키지 assert 검사를 통과하려면 필요(원본 EXP1857·X64B1 기록과 같은 방향) |
| `AD04-uatomic\include`에 `drm-uapi\drm_fourcc.h` 하나만(Mesa 원본을 감싸고 drm.h의 BSD 형식 정의만 제공) | 원본 빌드 PC에 있던 같은 경로 파일은 공개되지 않음(EXP1468에 경로만 나옴). Mesa `drm.h`는 Windows에서 `sys/ioccom.h`를 찾아 실패 |
| 생성 트리(`b5\generated`, `AD04-umd-owner-004-generated`)를 같은 Mesa 소스의 Windows 빌드(`C:\agx\mesa-gen`, LLVM 20.1.8 + 번역기 v20.1.19)로 만듦 | 원본은 "AD03 생성 트리"를 다른 곳에서 가져왔다고만 기록. 헤더 해시가 원본과 같으므로 커널 함수 목록·인자 배치는 같음 [사실]. 셰이더 바이너리(.c)는 원본에 고정값이 없어 같은지 모름 |
| 생성용 Mesa 도구를 MSVC `binmode.obj`와 링크 | `mesa_clc`가 SPIR-V를 텍스트 모드(`fopen "w"`)로 써서 Windows에서 0x0A가 0x0D0A로 바뀌어 깨짐 → `vtn_bindgen2` 접근 위반. Mesa 소스는 안 고침 |
| `asahi_clc`를 원본 런타임 묶음 스크립트의 변환 함수(64비트 opcode 플래그, unsigned bit-field, 지연 스케줄 등)를 그대로 불러와 만든 도우미(`tools/build_asahi_clc.py`)로 빌드, `unistd.h`·`sys/mman.h` 대체 헤더(`compat/`) 사용 | 원본 그대로의 Asahi 컴파일러는 MSVC ABI에서 GPU 명령을 틀리게 만듦(원본 EXP1306/EXP1848). 런타임 안 컴파일러와 같은 변환을 써야 셰이더가 같은 규칙으로 만들어짐. 이 도구는 패키지에 안 들어감 |
| `PYTHONUTF8=1` | 한국어 Windows(cp949)에서 원본 스크립트가 UTF-8 파일을 못 읽음 |
| 원본 `run-asahi-runtime-closure.ps1`의 `UmdContractTest`·`AppleAgxD3dKmRender` 링크 단계는 돌리지 않음 | 패키지 빌드에는 필요 없음. 실제 사용자 드라이버 링크가 같은 묶음을 링크해 대신 확인함 |
| 패키지 판 2742 | 원본 기본 461, 2차 KMD 전용 2741과 구분 |

## 아직 없는 것과 이유

| 없는 것 | 이유 |
|---|---|
| x86 WOW64 OpenGL ICD(`AppleAgxOpenGL32.dll`), x86 D3D 사용자 드라이버(`AppleAgxRenderAdmissionUmd32.dll`) | x86용 Mesa 빌드, x86 dxil-spirv, x86 런타임 묶음, `build-icd-x86.ps1`·`build-umd-x86.ps1` 전부가 따로 필요. 32비트 프로그램(옛 게임 등)용이라 첫 실기기 시험에는 필요 없음 [가설] |
| J274 RegionC 값(O3), 화면 경로(O4, 2560x1600 고정) | 이번 범위 밖. 정식 구성은 펌웨어 시작 뒤 화면 단계에서 `STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER`로 멈출 것(2차 보고서 근거) |
| Mac mini 쪽 준비: ACPI `APPL0002` 장치, 하이퍼바이저 GPU 중개, 테스트 서명 허용 | 2차 보고서 7절 그대로 |
| 셰이더 바이너리가 원본과 같은지 | 원본에 고정 해시가 없음. 확인하려면 원본 빌드 PC의 `libagx_shaders.c`가 필요 |
| 원본 기본(J313, 스위치 없음) 경로로 `build-driver.ps1` 재실행 | 하지 않음. P11/P13은 스위치 안에서만 동작하도록 코드로 확인 |

## 정직성 표시

- 이 문서의 어떤 결과도 GPU가 동작한다는 뜻이 아닙니다. 가상머신에서 빌드·링크·서명·INF 검사만 통과했습니다.
- Mac mini에는 설치·실행하지 않았습니다. 하드웨어 검증 0건입니다. 코디네이터가 알려 준 "13.5 스텁에서 G1 펌웨어 시험 10/10 통과"는 하이퍼바이저 쪽 결과이며, 이 패키지로 얻은 결과가 아닙니다.
- 가상머신 `C:\agx`에 빌드 루트(도구·소스·중간 파일 약 7.4 GB)와 로컬 git 저장소(`C:\agx\AD04-persistent-dwm-next`, 원격 없음, 푸시 없음)를 남겼습니다. 다시 빌드할 때 그대로 씁니다. 가상머신은 켜 둔 상태입니다.
