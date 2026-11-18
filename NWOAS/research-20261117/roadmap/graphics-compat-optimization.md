# 그래픽·호환성·최적화 로드맵 (2026-11-17)

> 범위: 읽기와 계획만 했습니다. 아무것도 빌드·설치·실행하지 않았고 맥미니·시리얼·USB는 건드리지 않았습니다.
> 표기: **[사실]** = 파일:줄 또는 URL로 확인한 것. **[가설]** = 확인하지 않은 추측. "원본" = paulsmir/windows-on-m1(J313 맥북 에어). 원본 기록의 숫자는 원본 기기에서 잰 값이며 우리 맥미니(J274)에서 잰 값이 아닙니다.
> 경로는 모두 `~/Desktop/DEV/NWOAS/` 기준입니다. 소리·와이파이는 안전장치로 막혀 있어 다루지 않았습니다.

---

## 0. 결론 (먼저 읽을 것)

1. **롤은 그래픽이 아니라 Riot 쪽 조건에 막혀 있습니다.** Riot은 ARM CPU를 지원하지 않는다고 적고 있고, ARM용 Vanguard는 발표만 있고 출시 날짜가 없습니다 [사실, §B1]. 그래서 롤은 "우리가 다 만들어도 Riot이 ARM용을 내놓기 전에는 안 됨"입니다. 그 뒤에도 Vanguard가 요구할 수 있는 TPM·Secure Boot·메모리 무결성(HVCI)·테스트 서명 끄기를 우리 구조가 채울 수 있는지가 남습니다. 특히 **메모리 무결성은 M1에서 Windows 자체 하이퍼바이저가 돌아야 하는 기능이라 지금 구조로는 켤 방법이 보이지 않습니다** [가설, §B5].
2. **롤과 상관없이, 지금 설치된 2757판은 x64 앱에 GPU를 주지 못합니다.** 패키지에 ARM64 사용자 드라이버만 들어 있습니다 [사실, §A1]. x64 게임·앱(롤 포함, Prism으로 도는 프로그램 전부)은 GPU 가속을 못 받습니다. 원본은 x64 드라이버를 껍데기(ARM64X 전달 DLL)까지만 확인했습니다 [사실]. **일상 사용에서 가장 값어치가 큰 그래픽 작업은 x64(그리고 x86) 사용자 드라이버를 넣는 것입니다.**
3. **원본이 11-07 이후에 화면 쪽을 크게 고쳤습니다.** `PresentMultiplaneOverlay`(MPO)·DDisplay 경로, 외부 DisplayPort 연결, 절전 꾸러미 등이 `gpu/memory-latency` 가지에 있고 우리 드라이버의 바탕(main `6c65673`, 11-07)에는 없습니다 [사실, §A6]. DisableDDisplay 우회를 없애는 일은 원본을 따라가는 것부터 시작합니다.
4. **하이퍼바이저에서 지금 바로 고칠 수 있고 효과가 클 것으로 보이는 것이 셋 있습니다.**
   - 쉬는 CPU의 대기 명령(WFI)을 하이퍼바이저가 잡아서 **그냥 다음 명령으로 넘깁니다**. 그래서 CPU가 실제로 쉬지 못합니다 [사실, 코드]. 원본은 하이퍼바이저가 대신 진짜로 기다립니다 [사실].
   - 보조 CPU 7개도 하이퍼바이저 주기 작업을 **초당 5000번** 돕니다(원본은 100번) [사실, 코드].
   - 시스템 공용 캐시(SLC, 8 MiB)가 우리 경로에서 꺼져 있을 가능성이 높습니다. 원본은 이것을 켜서 GPU 시험 점수가 8.5% 올랐습니다 [사실(원본), 가설(우리)].
5. **결정이 필요한 돈 문제는 하나입니다.** 정식 드라이버 서명에는 EV 코드 서명 인증서가 필요하고, 판매처 기준 1년 약 349~359달러입니다 [사실, 판매 페이지 검색 요약]. 롤이 열리기 전에는 사지 않는 것을 추천합니다. 이유: 서명만으로 롤이 되지 않고(1번), Windows 쪽 일상 사용에는 지금 테스트 서명으로 충분합니다.

### 우선순위 표 (제 판단)

| 순위 | 일 | 갈래 | 크기 | 콘치님 손 | 기대 효과 |
|---|---|---|---|---|---|
| 1 | 쉬는 CPU를 진짜로 쉬게 하기(WFI) | C2 | 작음 | 재부팅만 | 전력·열·팬 소음, 다른 CPU에 일할 시간 [가설] |
| 2 | 보조 CPU 주기 5000 → 100번/초 | C3 | 작음 | 재부팅만 | 초당 하이퍼바이저 진입 약 3만 4천 번 감소 [계산, §C3] |
| 3 | SLC 켜기 | C8 | 작음 | 재부팅만 | 원본 GPU +8.5% [사실(원본)] |
| 4 | x64 사용자 드라이버 + ARM64X 전달 DLL | A1 | 큼 | VM 빌드, 재부팅 | x64 앱·게임 GPU 가속 |
| 5 | 원본 최신 화면 코드(MPO/DDisplay) 반영 | A6 | 큼 | 재부팅, 화면 확인 | DisableDDisplay 없이 동작, 깜빡임·지연 [가설] |
| 6 | 시간 기준 화면 교체 완료(17 ms)를 진짜 신호로 | C5 | 중간 | 화면 확인 | 화면 출력 초당 약 56장 상한 해제 |
| 7 | 성능 측정 기준 만들기(같은 기기 macOS 대비) | A2 | 작음 | macOS로 부팅 1회 | 이후 모든 판단의 근거 |
| 8 | GPU 주소 중개를 드라이버로 옮기기 | C6 | 큼 | 재부팅 반복 | 원본 기록상 GPU 경로의 가장 큰 하이퍼바이저 비용 |
| 9 | TPM·Secure Boot·정식 서명 | B2~B4 | 큼 | **돈(EV 인증서)**, 사업자 등록 가능성 | 롤이 ARM을 지원한 뒤에만 의미 |

---

## A. 그래픽

### A1. 지원하는 그래픽 API와 x64·x86 앱

#### 지금 상태

| API | 2757판(지금 설치된 것) | 근거 |
|---|---|---|
| D3D12 | 기능 수준 11_0 기본, 11_1·12_0·12_1을 기능 조건에 따라 알림 | [사실] `port-j274/gpu/driver-p15/drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:300-322` |
| D3D12 셰이더 모델 | 6.0~6.5 목록을 알림 | [사실] 같은 파일 `:422-435` |
| D3D11 | 기능 수준 10_0, 10_1, 11_0, 11_1을 알림(Mesa D3D10 앞단 위에 D3D11 함수 표). 8x MSAA는 0으로 알림 | [사실] `mesa/scripts/build-native-asahi-state.py:1269-1283`, `mesa/d3d11/agx_d3d11_ddi.cpp:1-19` |
| D3D11 지연 컨텍스트(멀티스레드 명령 목록) | 드라이버가 지원 안 함 → Windows 런타임이 흉내 냄 | [사실] `agx_d3d11_ddi.cpp:15-16` |
| D3D9 | 드라이버 없음 → Windows의 D3D9On12가 D3D12 위에서 돌림 | [사실] `render-admission/AppleAgxRenderAdmission.inf:65-70` |
| OpenGL(ARM64 앱) | ARM64 ICD를 만들지 않음 → Microsoft GDI 소프트웨어 구현 | [사실] INF `:78-82` 주석 |
| OpenGL(x86 앱) | 원본에는 x86 Mesa ICD가 있으나 **우리 패키지에 없음** | [사실] INF `:49-54`, 패키지 목록 아래 |
| Vulkan | 드라이버 없음 | [사실] INF에 Vulkan 등록 없음 |
| x64 앱의 D3D11/12 | **GPU 가속 없음**: 패키지에 x64 이미지가 없음 | [사실] 아래 패키지 목록, 원본 `investigation/analysis/D3D12-X64-WOA-UMD-ABI.md`(`integration/ad04-windows-compiler` 가지) |
| x86(32비트) 앱의 D3D | **없음**: WOW64 사용자 드라이버를 뺐음 | [사실] `PATCHES.md:23` (P9) |

2757판 패키지에 들어 있는 파일 [사실, `port-j274/gpu/driver-p15/out/j274-full-20261116-1938-p21/package/`]:

| 파일 | 종류 |
|---|---|
| `AppleAgxRenderAdmission.sys` | ARM64 커널 드라이버 |
| `AppleAgxRenderAdmissionUmd.dll` | ARM64 사용자 드라이버 |
| `AppleAgxRenderAdmission.arm64only.inf`, `.cat` | ARM64만 담은 설치 정보 |

원본이 확인한 x64 쪽 사실 [사실, 원본 `D3D12-X64-WOA-UMD-ABI.md`]:
- x64 앱이 `D3D12CreateDevice`를 부르면 `DXGI_ERROR_UNSUPPORTED`(0x887A0004)를 받았습니다(EXP1795).
- x64 앱과 ARM64 앱은 **같은 파일 이름**(`UserModeDriverName`)을 받습니다(EXP1800). 그래서 이름만 다른 x64 DLL을 더하는 것으로는 안 되고, 한 파일이 두 아키텍처를 겸하는 ARM64X 전달 DLL이 필요합니다.
- 내용 없는 ARM64X 전달 DLL이 x64·ARM64 양쪽에 실리는 것은 J313에서 확인했습니다(EXP1805). **진짜 x64 드라이버와 서명 패키지는 원본도 끝내지 않았습니다.**
- 빌드 스크립트는 이미 우리 사본에 있습니다 [사실]: `render-admission/umd/build-umd-x64.ps1`, `umd/forwarder/build-arm64x-forwarder.ps1`, `scripts/repackage-driver.ps1:36-41`.

#### 목표
- x64 앱(Prism으로 도는 일반 프로그램, 게임)이 D3D11 기능 수준 11_0 이상으로 GPU를 씁니다.
- x86 앱도 D3D·OpenGL 가속을 받습니다.
- Vulkan·OpenGL이 필요한 앱은 D3D12 위에 얹는 방법으로 돌립니다.

#### 단계별 계획

**A1-1. x64 Mesa 런타임 묶음과 x64 사용자 드라이버 빌드**
- 할 일: Parallels VM에서 ARM64 판과 **같은 원본 스냅숏**으로 x64 Mesa/Asahi 런타임 묶음을 만들고 `build-umd-x64.ps1`로 `AppleAgxRenderAdmissionUmdX64.dll`을 만듭니다. 원본 문서가 요구하는 대로 내보내기 함수(`OpenAdapter10_2`, `OpenAdapter12`), 가져오기 목록, PE 형식을 검사합니다.
- 바꿀 파일: `build-umd-x64.ps1`의 고정 경로(`C:\Users\pauls\...`를 우리 VM 경로로), VM 빌드 스크립트(scratchpad `build-p21.sh` 계열의 새 판).
- 실기기 검증: 없음(이 단계는 VM 안 검사만).
- 콘치님 손·결정: Parallels VM 켜 두기. 돈 없음.
- 위험: x64 묶음이 ARM64 묶음과 원본 판이 다르면 섞여서 동작이 달라질 수 있음(원본 문서가 금지한 섞기).
- 선행 조건: 없음.

**A1-2. ARM64X 전달 DLL + 세 파일 패키지**
- 할 일: `build-arm64x-forwarder.ps1`로 전달 DLL을 만들고 `repackage-driver.ps1 -X64Umd -Arm64xForwarder`로 ARM64 드라이버를 `...UmdArm64.dll` 이름으로, 전달 DLL을 원래 이름으로 넣습니다. J274 INF(`arm64only.inf`)에 세 파일을 더합니다.
- 바꿀 파일: `render-admission/AppleAgxRenderAdmission.arm64only.inf`, 전달 매니페스트, 설치 작업(`agx-s248/run-install-2757.sh`의 새 판).
- 실기기 검증: ① ARM64 앱(지금의 바탕화면)이 그대로인지, ② x64 시험 프로그램에서 `D3D11CreateDevice(HARDWARE)`·`D3D12CreateDevice`가 성공하고 어댑터 이름이 Apple AGX인지, ③ x64 D3D11 간단한 그리기. 결과는 작업 통로로 받습니다.
- 콘치님 손·결정: 없음(재시작은 규칙상 묻지 않음). 화면이 이상하면 알려 주시기만.
- 위험: 전달 DLL이 잘못되면 ARM64 앱(DWM 포함)까지 드라이버를 못 실어 바탕화면이 Basic Display로 떨어짐 [가설]. 되돌리기는 2757판 재설치.
- 선행 조건: A1-1.

**A1-3. x86(32비트) 사용자 드라이버와 OpenGL ICD**
- 할 일: P9(ARM64만)를 풀고 원본 정식 패키지처럼 `AppleAgxRenderAdmissionUmd32.dll`(SysWOW64)과 `AppleAgxOpenGL32.dll`을 넣습니다. 원본은 11-06부터 x86 OpenGL ICD를 꾸러미마다 빌드합니다 [사실, 원본 `investigation/CHANGES.csv` CHG-20261106-b678a2b9-ICDBUILD].
- 바꿀 파일: `AppleAgxRenderAdmission.inf`(원본 판) 사용, `build-driver.ps1 -Board J274`(x86 인자 포함).
- 실기기 검증: 32비트 D3D11 시험 프로그램, 32비트 OpenGL 프로그램(원본은 Counter-Strike 1.6으로 확인 [사실, 원본 README.md:43-48]).
- 콘치님 손·결정: 없음.
- 위험: 작음. x86 파일은 32비트 앱에만 영향.
- 선행 조건: 없음(A1-2와 따로 가능).

**A1-4. Vulkan·OpenGL은 Microsoft 호환 팩으로**
- 할 일: Microsoft "OpenCL, OpenGL, and Vulkan Compatibility Pack"(D3D12 위에서 세 API를 구현)을 설치해 봅니다 [사실, https://apps.microsoft.com/detail/9nqpsl29bfff]. 우리 D3D12 드라이버 위에서 도는지 봅니다.
- 바꿀 파일: 없음.
- 실기기 검증: `vulkaninfo`, OpenGL 확인 프로그램.
- 콘치님 손·결정: Store가 필요하면 네트워크가 있어야 함(네트워크는 다른 갈래). 오프라인 설치 파일을 전달 통로로 넣는 방법도 있음 [가설].
- 위험: 검색 결과에는 ARM64에서 Vulkan 런타임이 NVIDIA WoA 드라이버의 추가 파일을 요구한다는 언급이 있습니다 [사실, 검색 요약, 원문은 https://docs.nvidia.com/rtx-spark/rtx-spark-porting-guide/latest/porting/vk-gl.html]. 우리에게 그대로 될지 모릅니다.
- 선행 조건: 없음.

#### 재사용 코드와 라이선스
- 원본 드라이버·빌드 스크립트: MIT [사실, `port-j274/gpu/driver-p15/THIRD_PARTY.md`].
- Mesa: 파일 머리글별(대부분 MIT) [사실, 같은 파일].
- Microsoft 호환 팩: 배포물만 씀(소스 재사용 없음).

#### 모르는 점
- x64 묶음이 그대로 컴파일·링크되는지(원본도 모름).
- x64 게임이 장치 생성 뒤 두 번째 문제를 만나는지(원본 문서가 남긴 질문).
- 우리 VM(Windows ARM64) 안에서 x64 대상 빌드를 원본 방식(x64 호스트 도구 경로)대로 할 수 있는지 [가설: ARM64 VM은 x64 도구를 흉내 내어 돌림].

#### 대략 크기: **큼**(A1-1·A1-2), A1-3 중간, A1-4 작음.

---

### A2. 성능 (셰이더 컴파일러, 메모리, P코어 느린 읽기)

#### 지금 상태

우리 기기 측정값 [사실]:

| 항목 | 값 | 근거 |
|---|---|---|
| 맥미니 GPU 벤치마크 | **잰 적 없음** | — |
| 1장(8 MB) 캐시 없는 읽기, 효율 코어 | 5.7 ms | `HANDOFF-2026-11-16-evening.md:155` |
| 같은 읽기, 성능 코어 | 34.6 ms | 같은 줄 |
| 4개 효율 코어로 나눠 복사(지금 대체 구성) | 1.89 ms | 같은 문서 `:157` |
| 하이퍼바이저 DCP 출력 평균 적용 시간(1024장) | 1221 µs | `nwoas_scripts/logs/agxmed1g-s249-20261116-202802.1sXX3e.vuart` `present #1024` 줄 |
| 전용 GPU 메모리 | 1.0 GB | `HANDOFF-2026-11-16-evening.md:181` |

원본 J313 기록 [사실(원본), README.md:32-35, GPU_CURRENT_STATE.md(`gpu/memory-latency` 가지)]:

| 항목 | 원본 Windows | 같은 기기 macOS | 비율 |
|---|---|---|---|
| GravityMark D3D12 1920x1080 점수 | 5,396 | 7,814 | 69 % |
| GravityMark D3D12 1280x720 점수 | 6,769 | 9,946 | 68 % |
| SLC를 켜기 전·후 GravityMark HD 초당 장면 | 40.73~41.07 → 44.43 | — | +8.5 % |

원본이 찾은 원인과 버린 가설 [사실(원본)]:
- "GPU 메모리 지연이 macOS보다 길다"는 측정은 대부분 **GPU 클럭이 낮은 단계에서 잰 것**이었습니다(측정 중 396 MHz 단계 64.7%). 높은 단계에서는 macOS와 비슷했습니다(GPU_CURRENT_STATE `2026-11-08 08:30Z` 절).
- 가상머신 번호(VMID) 가설과 캐시 속성 가설은 실기기에서 효과 없음으로 끝났습니다(커밋 `1766685`, `4eb0b0e`, 2026-11-07).
- 남은 차이: 1280x720 한 장 기준 원본 22.75 ms vs Apple 약 17.2 ms. 계산 작업 +3.5 ms, 작업 사이 빈 시간 4.66 ms(장의 20%)입니다. 빈 시간에는 하이퍼바이저 GPU 주소 중개가 포함됩니다(같은 문서 `13:45Z` 절).
- 우리 드라이버 바탕(`6c65673`, 11-07)에는 11-07까지의 드라이버 개선이 들어 있습니다 [사실, `THIRD_PARTY.md` 원본 커밋]. SLC는 드라이버가 아니라 **하이퍼바이저(m1n1)** 쪽 변경입니다.

P코어 느린 읽기:
- [사실] 캐시 연결·쓰기 결합·캐시 안 씀 세 방식 모두 같은 속도였고, 느린 것은 성능 코어에서의 캐시 없는 읽기뿐이었습니다(`HANDOFF-2026-11-16-evening.md:154-155`).
- [사실] 지금 기본 구성(g252J 이후)은 하이퍼바이저가 DCP로 GPU 메모리를 바로 보여 주므로 이 복사는 대체 구성에서만 씁니다(같은 문서 `:159`).
- [가설] 앱이 GPU 메모리를 CPU로 읽는 일(되읽기, 스크린샷 등)이 성능 코어에서 돌면 같은 문제가 남을 수 있습니다.

#### 목표
- 같은 맥미니의 macOS Metal 대비 몇 %인지 숫자로 압니다.
- 원본이 찾은 개선(SLC, CPU 클럭)을 우리에게 넣고, 같은 시험으로 효과를 잽니다.

#### 단계별 계획

**A2-1. 기준 측정 만들기**
- 할 일: 원본과 같은 GravityMark 1.89 D3D12(1920x1080, 소행성 100,000개, TAA 끔, 창 모드)를 Windows에서 3번 재고, 같은 맥미니 macOS에서 Metal로 3번 잽니다. 시험 프로그램은 ARM64 판을 씁니다(x64 드라이버 전이므로).
- 바꿀 파일: 새 작업 파일(`agx-s248/jobs/`에 벤치 실행·결과 수집 .cmd).
- 실기기 검증: 점수 표. 원본은 실행 간 흔들림이 1.5~2%라고 적었습니다 [사실, 원본 README.md:165-166].
- 콘치님 손·결정: macOS로 한 번 부팅해 Metal 측정(콘치님이 macOS를 다시 쓰려면 Macintosh HD의 부팅 등록 문제가 남아 있음, `HANDOFF-2026-11-16-evening.md:365`). 벤치 프로그램 내려받기(무료). 돈 없음.
- 위험: macOS 쪽이 Tahoe라 원본(macOS 15)과 조건이 다름.
- 선행 조건: 없음.

**A2-2. SLC 켜기 효과 재기** → §C8에서 함께.

**A2-3. 성능 코어 캐시 없는 읽기 원인 찾기**
- 할 일: SLC를 켠 하이퍼바이저에서 2752판 측정(코어별 8 MB 읽기)을 다시 돌립니다. 그래도 6배면 성능 코어 쪽 메모리 속성(같은 물리 영역의 다른 매핑, `PATCHES.md:92` 가설)을 봅니다.
- 바꿀 파일: 없음(측정용 판은 이미 있음).
- 실기기 검증: 코어별 ms 표.
- 콘치님 손·결정: 없음.
- 위험: 작음.
- 선행 조건: C8-1.

**A2-4. 원본의 11-07 이후 드라이버 성능 개선 반영**
- 할 일: `gpu/memory-latency` 가지의 드라이버 변경 중 J274와 무관한 것(컴파일러, 작업 겹치기 OV 계열, 인터럽트 완료 처리)을 골라 옮깁니다. 원본은 꾸러미마다 성능 회귀 관문(GravityMark·CS 2% 기준)을 돌립니다 [사실, 원본 CHANGES.csv `CHG-20261106-e7651946-PERFGATE`].
- 바꿀 파일: `port-j274/gpu/driver-p15/drivers/apple-agx/` 새 판(P22 이후), `PATCHES.md`.
- 실기기 검증: A2-1 점수와 비교, 바탕화면·절전 회귀.
- 콘치님 손·결정: 없음.
- 위험: 원본 가지는 J313 내장 패널(IOMFB) 경로를 바꾸는 변경이 섞여 있어 우리 HDMI 경로와 충돌할 수 있음 [가설].
- 선행 조건: A2-1(기준 없이는 효과를 모름).

#### 재사용 코드와 라이선스
- 원본 드라이버·m1n1_windows: MIT [사실, `external/windows-on-m1/LICENSE`, `m1n1_windows/LICENSE`].

#### 모르는 점
- 맥미니(팬 있음)가 원본 맥북 에어(팬 없음)보다 오래 높은 클럭을 유지하는지 [가설: 그럴 것].
- 원본 `gpu/memory-latency` 가지에서 어느 변경이 J313 전용인지(커밋을 하나씩 봐야 함).

#### 대략 크기: A2-1 작음, A2-3 작음, A2-4 **큼**.

---

### A3. GPU 전원과 클럭

#### 지금 상태
- [사실] GPU 클럭 단계는 GPU 펌웨어가 정하며, 펌웨어에 넘기는 전원·성능 값 66개는 J274 13.5 ADT로 다시 만든 표를 씁니다(`PATCHES.md:26` P12, `port-j274/gpu/hv/README-hv2.md:132-135`).
- [사실] 기본 성능 단계는 Asahi `t8103-j274.dts`를 따라 3을 줍니다(`README-hv2.md:137-147`, 실행기 `agxdcp-s256-guest-test.sh:33`).
- [사실] 원본은 측정 중 GPU가 낮은 단계에 오래 머문 것을 보고 "짧게 몰리는 작업에서의 클럭 올림 속도가 macOS와 다를 수 있다"를 열린 질문으로 남겼습니다(GPU_CURRENT_STATE `08:30Z` 절).
- [사실] 우리 KMD는 클럭 단계별 사용 비율을 기록하는 장치(원본 `Wom1GpuPerf`)를 가지고 있지만, 우리 실기기에서 읽은 기록은 이 세션 문서에 없습니다.

#### 목표
- 게임 중 GPU가 가장 높은 단계(원본 P6 1278 MHz [사실, GPU_CURRENT_STATE `05:00Z` 절])에 머무는지 확인합니다.
- 쉴 때는 내려가서 전력이 줄어드는지 확인합니다.

#### 단계별 계획

**A3-1. 클럭 단계 사용 비율 읽기**
- 할 일: 바탕화면 쉬기·GravityMark 실행 중 KMD 장치 키의 성능 단계 기록을 작업 통로로 읽습니다.
- 바꿀 파일: 새 읽기 작업(`agx-s248/jobs/` .cmd). 드라이버 변경 없음 [가설: 우리 판에도 기록이 켜져 있음, 키 이름 확인 필요].
- 실기기 검증: 단계별 비율 표.
- 콘치님 손·결정: 없음.
- 위험: 작음.
- 선행 조건: A2-1과 같이.

**A3-2. 클럭 올림 매개변수 비교(필요할 때만)**
- 할 일: A3-1에서 게임 중 낮은 단계가 많으면 macOS 13.5 ADT·Asahi의 성능 올림 관련 값과 우리 표를 비교합니다.
- 바꿀 파일: `port-j274/gpu/hwdata-j274/`(값을 바꾸면 드라이버·하이퍼바이저 둘 다 다시 빌드해야 함, `PATCHES.md:56`).
- 실기기 검증: A3-1 다시.
- 콘치님 손·결정: 없음.
- 위험: 전원 값 표를 잘못 바꾸면 펌웨어가 시작을 거부함(하이퍼바이저의 입력 일치 검사, `README-hv2.md:91`). 거부는 안전한 실패입니다.
- 선행 조건: A3-1.

#### 재사용 코드와 라이선스: Asahi DT·m1n1(MIT·GPL 혼재 — DT 값은 사실 자료로만 참고) [가설: 값 참고는 라이선스 문제 없음].
#### 모르는 점: 맥미니에서 GPU 온도 상한·팬 연동(SMC가 알아서 하는지) [모름].
#### 대략 크기: **작음**.

---

### A4. 동영상 재생 가속(Apple AVD 디코더, AVE 인코더)

#### 지금 상태
- [사실] 우리 드라이버에 동영상 디코드 기능이 없습니다(D3D11/12 비디오 함수 구현이 소스에 없음 — 원본 G1 문서도 "video decode"를 범위 밖으로 둠, `external/windows-on-m1/documentation/AGX_BRINGUP.md:130`).
- [사실] Asahi는 AVD 커널 드라이버(H.264, H.265, VP9; M3부터 AV1)를 리눅스 커널 검토에 올렸고, Apple 펌웨어 대신 자체 펌웨어를 만드는 중입니다(https://www.phoronix.com/news/apple-video-decode-avd-linux, 검색 요약).
- [가설] 지금 Windows의 동영상은 CPU 소프트웨어 디코드로 돕니다. M1 CPU면 1080p는 충분할 것으로 봅니다(측정 없음).

#### 목표
- 먼저 "필요한가"를 잽니다. 1080p·4K 유튜브를 CPU로 볼 때 끊김·CPU 사용률.
- 필요하면 AVD를 Windows용으로 만듭니다(D3D11 비디오 디코드 함수).

#### 단계별 계획

**A4-1. CPU 디코드 부담 재기**
- 할 일: 로컬 동영상 파일(H.264/H.265 1080p, 4K)을 Windows 기본 플레이어로 재생하며 CPU 사용률·끊김을 기록합니다.
- 바꿀 파일: 새 측정 작업.
- 실기기 검증: CPU 사용률 표 + 콘치님 눈(끊김).
- 콘치님 손·결정: 화면 보기 1회.
- 위험: 작음.
- 선행 조건: 없음.

**A4-2. (A4-1에서 부족할 때만) AVD Windows 드라이버**
- 할 일: Asahi AVD 드라이버를 참고해 AVD 장치를 ACPI로 알리고, KMD에 디코드 엔진, UMD에 D3D11 비디오 디코드 함수를 붙입니다.
- 바꿀 파일: Mu ACPI(새 장치), 하이퍼바이저(AVD 주소 창을 게스트에 열기), 드라이버 새 부분.
- 실기기 검증: DXVA 확인 프로그램, 재생 시 CPU 사용률.
- 콘치님 손·결정: 없음.
- 위험: 큼. Asahi도 아직 정식 반영 전이고 펌웨어를 새로 쓰는 중입니다.
- 선행 조건: A4-1, A1(x64 앱 브라우저·플레이어가 x64면 x64 드라이버도 필요).

#### 재사용 코드와 라이선스: Asahi AVD 커널 드라이버는 리눅스 커널 코드라 GPL-2.0일 것 [가설]. Windows 드라이버에 그대로 넣으면 안 되고 동작 이해용으로만 써야 합니다.
#### 모르는 점: AVD 펌웨어를 Windows 쪽에서 어떻게 올릴지, AVE(인코더) 공개 자료 유무 [모름].
#### 대략 크기: A4-1 작음, A4-2 **큼**.

---

### A5. 여러 화면, USB-C 화면, HDR

#### 지금 상태
- [사실] 화면은 HDMI 하나, 하이퍼바이저가 iBoot식 DCP 통로(엔드포인트 0x23)로 출력합니다. IOMFB 전체 소유 경로는 외장 화면(`/arm-io/disp0 external=1`)을 거부해서 이 길을 골랐습니다(`HANDOFF-2026-11-16-evening.md:202`).
- [사실] 해상도 8개(1920x1080 이하, 늘려서 표시)와 모니터 재연결이 됩니다(`HANDOFF-2026-11-16-evening.md:16-17`, `:248-280`).
- [사실] 하이퍼바이저 서비스가 알리는 형식은 BGRA8888뿐이고 RGBA8888도 없습니다(CAPS 0x37f, `PATCHES.md:162`). 10비트·HDR 형식이 없습니다.
- [사실] 원본은 10비트 스왑체인을 연속 출력에 쓰면 DWM이 고장 나서 거부했습니다(원본 CHANGES.csv `CHG-20261021-D3D12-C40-10BIT-SWAPCHAIN-DWM`).
- [사실] 원본은 11-14에 J313에서 외부 DisplayPort 연결 맞추기와 모드 읽기까지 했습니다(커밋 `cda0484`, "external DisplayPort link trains, modes read").
- [사실] Asahi는 USB-C 화면 출력이 되지만 기기당 USB-C 포트 하나만 정해서 쓸 수 있고, 꽂고 빼기에 문제가 있다고 밝혔습니다(https://asahilinux.org/2026/02/progress-report-6-19/, 검색 요약).
- [사실] 우리 모니터 사양은 FHD이고, 모니터가 보고한 4K 모드를 사양으로 볼 수 없습니다(`HANDOFF-2026-11-16-evening.md:263`).

#### 목표
- 1단계: USB-C 모니터 하나를 HDMI와 함께(두 화면) 씁니다.
- 2단계: HDR은 모니터가 실제로 지원할 때만 다룹니다.

#### 단계별 계획

**A5-1. 맥미니 USB-C 화면 장치 조사(읽기만)**
- 할 일: 13.5 ADT에서 두 번째 DCP(외장용)와 USB-C 포트의 DP 경로 장치를 찾고, Asahi의 M1 DP 대체 모드 패치와 원본 EXP2442 기록을 비교합니다.
- 바꿀 파일: 없음(조사 문서만).
- 실기기 검증: 없음.
- 콘치님 손·결정: 콘치님이 쓰실 USB-C 모니터(또는 USB-C→HDMI 어댑터)가 있는지 알려 주셔야 합니다.
- 위험: 없음.
- 선행 조건: 없음.

**A5-2. 두 번째 화면을 Windows에 알리기**
- 할 일: 드라이버에 두 번째 화면 출력 대상(VidPN 소스·대상 2개)을 넣고, 하이퍼바이저에 두 번째 DCP 서비스를 둡니다.
- 바꿀 파일: `m1n1-gpu-hv2/src/display.c`·`hv_agx_scanout_service.c`(두 번째 통로), 드라이버 `render-admission/src/display.c`·`scanout_windows.c`, Mu ACPI.
- 실기기 검증: 디스플레이 설정에 화면 2개, 창을 옮겨 보기(콘치님 눈).
- 콘치님 손·결정: 모니터 연결.
- 위험: 큼. USB-C 쪽 PHY·대체 모드 설정은 Asahi도 아직 제한적입니다.
- 선행 조건: A5-1, A6-1(원본 화면 코드 정리 뒤가 덜 겹침).

**A5-3. HDR (모니터가 지원하면)**
- 할 일: 10비트·FP16 출력 형식을 하이퍼바이저 DCP 서비스와 드라이버에 더합니다.
- 바꿀 파일: 위와 같음.
- 실기기 검증: Windows HDR 설정 켜짐, 원본이 본 DWM 고장이 없는지.
- 콘치님 손·결정: 모니터 HDR 사양 확인(모니터 설명서 기준).
- 위험: 원본에서도 10비트 연속 출력이 실패함.
- 선행 조건: A5-2 불필요, A6-1 뒤.

#### 재사용 코드와 라이선스: m1n1 `display.c`(MIT), Asahi DCP 드라이버(GPL — 동작 참고만) [가설: Asahi 커널 쪽은 GPL].
#### 모르는 점: iBoot식 DCP 통로가 HDR 형식을 받는지, 맥미니 USB-C가 어느 DCP에 연결되는지 [모름].
#### 대략 크기: A5-1 작음, A5-2 **큼**, A5-3 중간.

---

### A6. 안정성: 오류 복구(TDR), 장치 재시작, 절전, DDisplay

#### 지금 상태

| 항목 | 상태 | 근거 |
|---|---|---|
| DWM 새 출력 방식(DDisplay) | UMD에 `PresentMultiplaneOverlay1`이 없어 `DisableDDisplay`=1로 피함 | [사실] `HANDOFF-2026-11-16-evening.md:138-147` |
| 실행 중 `pnputil /restart-device` | 0xC0000483. 정지 경로가 GPU 정리를 건너뜀 | [사실] 같은 문서 `:193` |
| GPU 작업 시간 초과 복구 | 원본도 못 고침: 엔진·어댑터 재설정이 모두 "멈춤 확인" 단계에서 0xC0000483 | [사실] 원본 `investigation/analysis/RC34-tdr-recovery.md` |
| 연속 두 번째 절전 펌웨어 Timeout | 1번, 이후 재현 안 됨, 원인 모름 | [사실] `HANDOFF-2026-11-16-evening.md:169` |
| 하이퍼바이저 종료 포착(BUGPROBE) | ntoskrnl 판이 달라 꺼짐 | [사실] 같은 문서 `:246` |

- [사실] 원본 RC34의 결론: 펌웨어 오류 복구는 되었지만 세 작업 큐를 되찾지 못했고, "성공"을 돌려주면 GPU가 아직 쓰는 메모리를 Windows가 회수할 수 있어 고치지 않았다고 적었습니다.
- [사실] 0xC0000483은 `STATUS_DEVICE_HARDWARE_ERROR`입니다(RC34 문서가 WDK `ntstatus.h:9836` 인용).
- [사실] 원본은 11-12~11-14에 `PresentMultiplaneOverlay`·Check3/Set3(MPO3) 경로, DDisplay 반복 원인 제거 등을 `gpu/memory-latency` 가지에 넣었습니다(커밋 `04850c6` "accept a disabled plane in PresentMultiplaneOverlay", `19f1063` "PL-1 removes the DDisplay loop", `1cadce2` 화면 줄 병합).

#### 목표
- DisableDDisplay 없이 바탕화면이 정상입니다.
- 앱 하나가 GPU를 멈추게 해도 Windows가 화면을 되찾습니다(최소한 재부팅 없이).
- 드라이버 판 교체를 Windows 재시작 없이 할 수 있습니다(개발 속도).

#### 단계별 계획

**A6-1. 원본 MPO/DDisplay 변경 옮기기**
- 할 일: 원본 `gpu/memory-latency` 가지에서 MPO·DDisplay 관련 UMD·KMD 변경을 우리 P21 위에 옮깁니다. 원본의 화면 쪽은 내장 패널 전제가 섞여 있으므로 우리 하이퍼바이저 서비스 경로(`NwScanoutPath`=1)에 맞춥니다.
- 바꿀 파일: `render-admission/umd/src/*`, `render-admission/src/scanout_windows.c`, `PATCHES.md`(P22).
- 실기기 검증: `DisableDDisplay`를 지운 상태로 부팅 → DWM 충돌 0회(`D36-crashes.cmd`), 30분 사용, 창 끌기·전체 화면 앱.
- 콘치님 손·결정: 30분 사용해 보기.
- 위험: DWM이 7초마다 죽던 예전 증상이 다시 나오면 레지스트리 값을 다시 넣어 되돌립니다(`HANDOFF-2026-11-16-evening.md:145`).
- 선행 조건: 없음.

**A6-2. 정지 경로 고치기(pnputil 재시작)**
- 할 일: `DxgkDdiStopDevice` 경로가 retire·close·전원 끄기를 건너뛰는 지점을 코드에서 찾아, 시작 경로의 역순 정리를 부릅니다.
- 바꿀 파일: `render-admission/src/lifecycle.c`, `backend_platform_windows.c`.
- 실기기 검증: `pnputil /restart-device` 10번 연속 성공, 매번 바탕화면 복귀.
- 콘치님 손·결정: 없음.
- 위험: 정지 중 하이퍼바이저 풀 창 경로의 예전 NULL 역참조(g248C에서 고침, 실기기 미검증, `HANDOFF-2026-11-16-evening.md:183-187`)를 다시 만날 수 있음.
- 선행 조건: 없음.

**A6-3. BUGPROBE를 지금 ntoskrnl에 맞추기**
- 할 일: 지금 Windows(26200.8037, `NWOAS-STATUS-2026-11-15.md:4`)의 ntoskrnl 공개 심볼로 RVA를 다시 맞춥니다.
- 바꿀 파일: 하이퍼바이저 BUGPROBE 표(파일 위치는 hv2 소스에서 확인 필요).
- 실기기 검증: 일부러 일으킨 충돌 없이, 시작 기록에 `ntos base found`가 찍히는지.
- 콘치님 손·결정: 없음.
- 위험: 작음.
- 선행 조건: 없음.

**A6-4. GPU 시간 초과 복구**
- 할 일: 원본 RC34의 남은 질문(펌웨어가 큐를 왜 놓지 않나)을 Asahi `gpu.rs` 복구 순서와 대조해 설계합니다. 원본이 "성공을 거짓으로 돌려주지 말 것"이라고 한 원칙을 지킵니다.
- 바꿀 파일: `render-admission/src/scheduler_windows.c`, `backend_platform_windows.c`, `shared/src/apple_agx_g13_queue_runtime.c`.
- 실기기 검증: 원본 TDR 연습 프로그램(`windows/icd/agx_tdr_drill.c`)으로 일부러 멈추게 한 뒤 화면 복귀.
- 콘치님 손·결정: 없음.
- 위험: 큼. 원본도 막힌 곳입니다. 실패하면 0x116 블루스크린 → 재부팅.
- 선행 조건: A6-2(정리 경로가 먼저 맞아야 함).

**A6-5. 두 번째 절전 펌웨어 Timeout 장시간 시험**
- 할 일: 절전→깨우기를 자동으로 여러 번 반복하는 작업을 돌리고, 실패 시 펌웨어 기록을 남깁니다.
- 바꿀 파일: 새 작업 파일.
- 실기기 검증: 반복 횟수와 실패 0회.
- 콘치님 손·결정: 키보드로 깨워야 한다면 콘치님 손 [가설: 예약 깨우기로 대신할 수 있는지 확인 필요].
- 위험: 작음.
- 선행 조건: 없음.

#### 재사용 코드와 라이선스: 원본(MIT). Asahi `gpu.rs` 복구 순서는 동작 참고만(GPL/MIT 이중 여부 확인 필요) [모름].
#### 모르는 점: 원본 MPO 변경이 HDMI·하이퍼바이저 서비스 경로에서 그대로 되는지.
#### 대략 크기: A6-1 **큼**, A6-2 중간, A6-3 작음, A6-4 **큼**, A6-5 작음.

---

### A7. 롤이 그래픽에서 필요로 하는 것

#### 지금 상태

| 롤 요구(Windows, Riot 공식) | 우리 지금 | 근거 |
|---|---|---|
| ARM CPU | "Not supported" | [사실] https://support.riotgames.com/en-us/league-of-legends/performance/minimum-and-recommended-system-requirements-league-of-legends |
| OS 아키텍처 x64 | Windows 11 ARM64(x64 앱은 Prism 흉내 내기) | [사실] 같은 페이지, `NWOAS-STATUS-2026-11-15.md:4` |
| DirectX 11 기능 수준 11_0 | ARM64 앱에는 11_1까지, **x64 앱에는 없음** | [사실] §A1 |
| GPU 메모리 1 GB | 전용 1.0 GB | [사실] Riot 페이지, `HANDOFF-2026-11-16-evening.md:181` |
| Windows 11이면 TPM 2.0 켜짐 | TPM 없음 | [사실] Riot 페이지, §B3 |

- [사실] 기능 수준이 모자라면 클라이언트가 "Play" 버튼을 회색으로 만든다고 Riot 페이지에 적혀 있습니다.
- [가설] 롤 게임 본체가 x64라서, x64 사용자 드라이버(A1-1·A1-2)가 없으면 롤은 GPU 가속 없이 Microsoft 소프트웨어 렌더러로 떨어지거나 실행을 거부할 것입니다.

#### 목표: Riot이 ARM용 Vanguard를 내놓는 날, 그래픽 쪽은 막히지 않게 준비해 둡니다.

#### 단계별 계획
- **A7-1** = A1-1·A1-2(x64 드라이버). 
- **A7-2. 롤과 비슷한 x64 D3D11 게임으로 미리 시험**: Vanguard가 없는 x64 D3D11 게임(무료 벤치마크 등)으로 초당 장면·끊김을 잽니다. 바꿀 파일: 없음. 실기기 검증: 초당 장면 표 + 콘치님 플레이 감각. 콘치님 손: 게임 설치·조작. 돈: 무료 게임이면 없음. 위험: 작음. 선행 조건: A7-1.
- **A7-3. 화면 출력 상한 풀기** = §C5(지금 초당 약 56장 상한).

#### 재사용 코드와 라이선스: A1과 같음.
#### 모르는 점: Prism이 롤 게임 본체를 얼마나 빠르게 돌리는지(ARM용 Vanguard가 없어 아무도 잴 수 없음).
#### 대략 크기: A1에 포함.

---

## B. 앱·게임 호환성과 보안 기능

### B1. Riot Vanguard 요구 사항과 우리 기기의 차이

#### 지금 상태

Riot 공식 문서 [사실]:
- Vanguard는 상황에 따라 TPM 2.0, Secure Boot, 메모리 무결성(HVCI/VBS), IOMMU를 요구할 수 있다고 적혀 있습니다. 게임 이름은 적혀 있지 않고, 날짜는 2026-05-31입니다(https://support.riotgames.com/en-us/riot/performance/vanguard-security-requirements).
- 테스트 서명 모드(TESTSIGNING)나 서명 검사 끄기를 바꿔 두면 Vanguard가 실행을 거부할 수 있고, 해결로 `bcdedit /set TESTSIGNING off`를 안내합니다(https://support.riotgames.com/en-us/riot/security/restore-windows-security-to-normal-state).
- 가상머신·지원하지 않는 가상화 환경은 오류(VAN 138, VAN 9100)로 거부합니다(앞선 조사 `nwoas_scripts/plan-20261115/gpu.md:154`).
- ARM용: NVIDIA RTX Spark 발표에 Vanguard가 지원 작업 목록에 있고 Riot이 롤·발로란트 지원 의사를 밝혔지만, **날짜가 없습니다**(https://overclock3d.net/?p=310458, https://www.pcgameshardware.de/RTX-Spark-Hardware-286023/News/Nvidia-hunderte-PC-Spiele-auf-ARM-Laptops-DLSS-5-Anti-Cheat-1556672/ 검색 요약). 2026-11-17 검색에서 출시 확인은 못 했습니다(제 검색 도구 범위).

| 항목 | Vanguard 요구(가능성) | 우리 지금 | 채울 수 있나 |
|---|---|---|---|
| ARM64판 Vanguard | 필요 | 없음(Riot이 안 냄) | **우리 손 밖** |
| 테스트 서명 끄기 | 요구 | 켜져 있음(WDK 시험 인증서, `HANDOFF-2026-11-16-evening.md:115`) | 정식 서명 필요(§B2, 돈) |
| TPM 2.0 | 요구(롤 Windows 11 최소 사양에도 있음) | 없음 | 하이퍼바이저가 흉내 낼 수는 있음(§B3). 받아 줄지 모름 |
| Secure Boot | 요구 가능 | 펌웨어에서 꺼서 빌드(`MacMini2020.dsc:34`) | 가능하나 UEFI 변수가 저장되지 않음(§B4) |
| 메모리 무결성(HVCI) | 요구 가능 | 없음 | **지금 구조로는 어려움**(§B5) |
| IOMMU | 요구 가능 | Windows에 알리지 않음 [가설: ACPI에 IORT/DART 설명 없음] | 모름 |
| 가상화 아래에서 실행 | 거부 | Windows 밑에 하이퍼바이저가 있음 | 하이퍼바이저를 없앨 수 없음(§C9) |

- [가설, 높은 확신] 위 표의 마지막 줄 때문에, ARM용 Vanguard가 나와도 우리 구조를 거부할 가능성이 높습니다.
- 하이퍼바이저를 부정행위 방지 프로그램에게서 숨기는 것은 Riot 약관 위반일 수 있어 이 문서는 그 방향을 계획하지 않습니다(앞선 조사 `gpu.md:166`의 판단을 그대로 따름).

#### 목표: ARM용 Vanguard가 나오면 바로 "무엇에서 막히는지"를 오류 번호로 확인할 수 있는 상태.

#### 단계별 계획

**B1-1. Riot 공지 감시**
- 할 일: 롤 ARM 지원 공지를 주기적으로 확인합니다(사람이 보거나 예약 작업).
- 바꿀 파일: 없음. 실기기 검증: 없음. 콘치님 손·결정: 예약 작업을 만들지 여부. 위험: 없음. 선행 조건: 없음.

**B1-2. ARM용이 나오면 지금 상태 그대로 한 번 설치해 오류 번호 기록**
- 할 일: 테스트 서명 켜진 채로 설치·실행해 첫 오류를 봅니다. 그다음 B2~B4 중 무엇이 필요한지 정합니다.
- 바꿀 파일: 없음.
- 실기기 검증: Vanguard 오류 번호.
- 콘치님 손·결정: Riot 계정 로그인(콘치님만), 네트워크.
- 위험: Vanguard는 커널 드라이버라 부팅에 영향을 줄 수 있음 [가설]. 시험 전 Windows 파티션 백업 권장.
- 선행 조건: Riot 출시, 네트워크.

#### 재사용 코드와 라이선스: 해당 없음.
#### 모르는 점: Vanguard ARM판이 하이퍼바이저를 어떻게 판정하는지(공개 자료 없음).
#### 대략 크기: **작음**(우리 쪽 일), 결과는 Riot에 달림.

---

### B2. 테스트 서명 드라이버 → 정식 서명

#### 지금 상태
- [사실] 우리 GPU 드라이버는 VM의 WDK 시험 인증서로 서명하고, 맥미니는 testsigning을 켜고 그 인증서를 믿게 설정되어 있습니다(`HANDOFF-2026-11-16-evening.md:115`).
- [사실] Secure Boot가 켜져 있으면 `TESTSIGNING`을 바꿀 수 없다고 Microsoft 문서에 적혀 있습니다(https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).
- [사실] Microsoft 서명(attestation signing)을 받으려면 Partner Center(하드웨어 대시보드) 계정에 EV 인증서가 하나 이상 연결되어 있어야 합니다(https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs).
- [사실, 검색 요약] EV 코드 서명 인증서 가격 예: SSL.com 1년 $349, 개인사업자용 EV 1년 $359. 일반 EV는 등록된 조직용이고, 개인사업자 EV는 등록된 개인사업자를 요구하는 것으로 보입니다(SSL.com 페이지 검색 요약, 판매 페이지 직접 확인 안 함).
- 원본도 키보드·SSD 드라이버를 "영구 시험 인증서"로 설치합니다 [사실, 원본 README.md:52-56]. 원본에도 정식 서명 사례는 없습니다 [사실, 같은 문단].

#### 목표: Secure Boot를 켠 상태에서 우리 드라이버(GPU, 앞으로의 소리·와이파이 포함)가 실립니다.

#### 단계별 계획

**B2-1. 결정만 남겨 두기(지금은 사지 않음)**
- 할 일: B1-2에서 "테스트 서명 때문에 막힘"이 확인될 때까지 인증서 구매를 미룹니다.
- 콘치님 손·결정: **돈 결정**: EV 인증서 연 약 $349~359 + (필요하면) 개인사업자 등록. Partner Center 수수료는 확인 못 함 [모름].
- 위험: 없음.

**B2-2. (구매 결정 뒤) 정식 서명 꾸러미 만들기**
- 할 일: 드라이버를 EV 인증서로 서명해 Partner Center에 올리고, Microsoft 서명본을 받습니다. 카탈로그·INF 판 검사(`verify-package-version.ps1`)를 지킵니다.
- 바꿀 파일: VM 빌드 스크립트(서명 단계).
- 실기기 검증: testsigning을 끄고 부팅 → 장치 관리자에서 GPU 드라이버 정상.
- 콘치님 손·결정: 인증서 구매·본인 확인·Partner Center 가입(전부 콘치님만 가능).
- 위험: Microsoft 서명 심사에서 거절될 수 있음 [모름]. 드라이버를 고칠 때마다 다시 올려야 해서 개발 속도가 크게 떨어짐 [가설].
- 선행 조건: B2-1 결정.

#### 재사용 코드와 라이선스: 해당 없음.
#### 모르는 점: Partner Center 개인 가입 가능 여부·수수료, Microsoft가 하이퍼바이저 위에서만 도는 드라이버를 서명해 주는지.
#### 대략 크기: 일은 **중간**, 돈과 절차가 큼.

---

### B3. TPM 2.0

#### 지금 상태
- [사실] 우리 Mu는 TPM 장치를 만들지 않습니다(`fw-audio-s256-mu`의 MacMini2020 DSC·FDF에 TPM 항목 없음, `AppleSiliconPkg.dsc.inc` 원본 판은 `Tpm2DeviceLibTcg2`만 연결 — `external/windows-on-m1/mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc:383-393`).
- [사실] Mu에는 FF-A(보안 세계와 대화하는 Arm 규격)로 TPM을 쓰는 코드가 있습니다(`fw-audio-s256-mu/Common/TIANO/SecurityPkg/Tcg/Tcg2AcpiFfa`, `Library/Tpm2DeviceLibFfa`).
- [사실, 원본 문서의 판단] M1에는 EL3(보안 세계)이 없습니다(원본 `HV-RESPONSIBILITY-INVENTORY.md` §3 "M1 has no EL3"). 그래서 보안 세계에서 도는 펌웨어 TPM(fTPM)은 쓸 수 없습니다 [가설, 높음].
- [사실] 원본 하이퍼바이저의 가상 TPM 파일은 16줄짜리 머리말뿐이고, Apple 보안 칩(SEP)에 키를 맡기려는 구상만 적혀 있습니다(`external/windows-on-m1/m1n1_windows/src/hv_vtpm.c:1-16`).
- [사실] Windows 11 설치는 TPM 검사를 건너뛰는 설치 파일로 했습니다 [가설: 설치 응답 파일·설치 방식상 — 확인은 `nwoas_autounattend/` 필요].

#### 목표
- 1단계: Windows가 "TPM 2.0 준비됨"으로 봅니다(BitLocker·Windows Hello·롤 최소 사양용).
- 2단계(불확실): Vanguard가 받아 주는지는 B1-2에서 봅니다.

#### 단계별 계획

**B3-1. 설계 문서**
- 할 일: 하이퍼바이저가 TPM 명령 창(CRB 방식 MMIO 한 페이지)을 흉내 내고, 명령 처리는 공개 TPM 구현으로 하는 구조를 씁니다. 상태 저장 위치를 정합니다(예: 비워 둔 NWSTUB 4 GB 파티션 일부, `NWOAS-STATUS-2026-11-15.md:22-23`).
- 바꿀 파일: 새 설계 문서.
- 실기기 검증: 없음.
- 콘치님 손·결정: 저장 위치(NWSTUB를 쓸지) 결정.
- 위험: 없음.
- 선행 조건: 없음.

**B3-2. 개발 중에는 맥북 쪽에서 TPM 돌리기(시험용)**
- 할 일: 맥북의 공개 소프트웨어 TPM(swtpm/libtpms)에 하이퍼바이저가 명령을 넘깁니다. 맥북 연결 모드(D6, `nwoas_scripts/plan-20261115/DECISIONS.md:10`)에서만 됩니다.
- 바꿀 파일: 하이퍼바이저(TPM 창, 프록시 명령), Mu(ACPI TPM2 표, `Tcg2Dxe`), 런처 모듈.
- 실기기 검증: Windows `tpm.msc`에서 TPM 2.0 준비됨, `Get-Tpm`.
- 콘치님 손·결정: 맥북에 swtpm 설치(무료, Homebrew).
- 위험: Windows ARM64의 TPM 드라이버가 CRB 방식을 받아 주는지 모름 [모름]. TPM에 묶인 기능(BitLocker)을 켠 뒤 TPM 상태를 잃으면 복구 키가 필요 → **BitLocker는 켜지 말 것**.
- 선행 조건: B3-1.

**B3-3. 맥북 없이 하이퍼바이저 안에서 TPM 돌리기**
- 할 일: 공개 TPM 구현과 암호 라이브러리를 하이퍼바이저에 넣습니다.
- 바꿀 파일: 하이퍼바이저 새 모듈.
- 실기기 검증: B3-2와 같음 + 재부팅 뒤 상태 유지.
- 콘치님 손·결정: 없음.
- 위험: 하이퍼바이저가 크게 커지고(보안 코드), 하드웨어 뿌리 키가 없어 "진짜 TPM"이 아님. 제조사 인증서(EK 인증서)가 없어 원격 증명은 안 됨 [가설].
- 선행 조건: B3-2, 맥북 없는 부팅 갈래.

#### 재사용 코드와 라이선스
- libtpms(BSD-3-Clause), swtpm(BSD-3-Clause) [가설: 기억 기반, 내려받을 때 LICENSE 확인].
- Microsoft TPM 2.0 참조 구현(ms-tpm-20-ref) — 라이선스 확인 필요 [모름].
- Mu `SecurityPkg` Tcg2 코드: BSD-2-Clause-Patent [가설: edk2 기본 라이선스].

#### 모르는 점
- Windows ARM64가 어떤 TPM 연결 방식(CRB/FF-A/SMC)을 받는지.
- Vanguard가 제조사 인증서 없는 TPM을 받는지.
#### 대략 크기: **큼**.

---

### B4. Secure Boot (우리 키 또는 Microsoft 키)

#### 지금 상태
- [사실] `SECURE_BOOT_ENABLE = FALSE`로 빌드합니다(`fw-audio-s256-mu/Platform/MacMini2020Pkg/MacMini2020.dsc:34`).
- [사실] UEFI 변수는 메모리에만 두는 방식입니다(`PcdEmuVariableNvModeEnable|TRUE`, `fw-audio-s256-mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc:96`). 재부팅하면 사라지므로 Secure Boot 키(PK/KEK/db)를 Windows에서 넣어도 남지 않습니다.
- [사실] 이전 기록: 하이퍼바이저 아래에서는 저장된 변수가 없어 재설정이 실행을 끝냈다는 주석(`MacMini2020.dsc:50-55`).

#### 목표: Windows가 "Secure Boot 켜짐"으로 보고, Microsoft가 서명한 부팅 관리자만 실립니다.

#### 단계별 계획

**B4-1. 키를 펌웨어 안에 넣어 빌드**
- 할 일: Microsoft Windows 부팅 키(db: Windows Production PCA 2011 / Windows UEFI CA 2023, KEK)와 우리가 만든 PK를 펌웨어 기본값으로 넣는 방법을 Mu에서 찾습니다. 변수 저장이 없어도 매 부팅 기본값으로 켜지게 합니다.
- 바꿀 파일: `MacMini2020.dsc`(SECURE_BOOT_ENABLE), 키 파일, Secure Boot 기본값 라이브러리(Mu에 있는지 확인 필요).
- 실기기 검증: `Confirm-SecureBootUEFI` = True, 부팅 정상.
- 콘치님 손·결정: 없음(키 만들기는 무료).
- 위험: **테스트 서명 드라이버(GPU)가 실리지 않게 됨**(B2 없이는 바탕화면이 Basic Display로) [사실에서 따라 나오는 가설]. 실패하면 Secure Boot를 끈 페이로드로 되돌림.
- 선행 조건: B2-2(정식 서명)이 없으면 실험으로만.

**B4-2. 변수 저장소 만들기(필요하면)**
- 할 일: UEFI 변수를 디스크(NWSTUB 등)에 저장하는 방식.
- 바꿀 파일: Mu 변수 저장 드라이버, 하이퍼바이저(저장 중개).
- 실기기 검증: Windows에서 바꾼 변수가 재부팅 뒤 남음.
- 위험: 중간. 변수 저장이 깨지면 부팅 항목이 사라질 수 있음 [가설].
- 선행 조건: 없음.

#### 재사용 코드와 라이선스: Mu/edk2 SecurityPkg(BSD-2-Clause-Patent) [가설].
#### 모르는 점: Mu에 "펌웨어에 넣은 기본 Secure Boot 키" 기능이 이 판에 있는지.
#### 대략 크기: B4-1 중간, B4-2 중간.

---

### B5. 메모리 무결성(HVCI/VBS)

#### 지금 상태
- [사실] Riot은 메모리 무결성이 CPU의 하드웨어 가상화에 기댄다고 적습니다(https://support-legendsofruneterra.riotgames.com/hc/en-us/articles/48069697996435-Error-VAN-RESTRICTION, 검색 요약).
- [가설, 높음] Windows ARM64의 메모리 무결성은 Windows 자체 하이퍼바이저가 EL2에서 돌아야 합니다. 우리는 그 자리(EL2)를 우리 하이퍼바이저가 쓰고, Windows는 EL1에서 돕니다.
- [사실, 검색 요약] M1은 하드웨어 중첩 가상화를 지원하지 않고, Apple은 M3부터 중첩 가상화를 열었습니다(https://news.hada.io/topic?id=15318, Parallels 포럼). 그래서 Windows 하이퍼바이저를 우리 하이퍼바이저 위에 올리려면 EL2 동작 전부를 소프트웨어로 흉내 내야 합니다 [가설].

#### 목표: (현실적으로는) Vanguard가 이것을 요구하는지부터 B1-2에서 확인합니다.

#### 단계별 계획
- **B5-1. 요구 여부 확인** = B1-2. 크기 작음.
- **B5-2. (요구될 때만) 중첩 가상화 흉내 내기 타당성 조사**: Windows 하이퍼바이저가 쓰는 EL2 레지스터·명령을 우리 하이퍼바이저가 소프트웨어로 처리하는 설계. 바꿀 파일: 조사 문서. 실기기 검증: 없음. 콘치님 손: 없음. 위험: 성능이 크게 떨어지고 작업량이 매우 큼 [가설]. 선행 조건: B1-2.

#### 재사용 코드와 라이선스: 리눅스 KVM의 ARM 중첩 가상화 코드(GPL, 참고만) [가설].
#### 모르는 점: Vanguard ARM판이 메모리 무결성을 항상 요구하는지.
#### 대략 크기: B5-2는 **매우 큼**(사실상 별도 프로젝트).

---

### B6. 정품 인증, Windows Update, Store, SMBIOS

#### 지금 상태
- [사실] 설치에 쓴 키는 무인 설치용 일반 Pro 키이고 정품 인증 키가 아닙니다(`autounattend.xml:160-170` 주석).
- [사실] 네트워크가 없습니다(`NWOAS-STATUS-2026-11-15.md:13`). 그래서 정품 인증·Windows Update·Store는 시험한 적이 없습니다. 네트워크 계획 5단계로 잡혀 있습니다(`nwoas_scripts/plan-20261115/network.md:142`).
- SMBIOS(펌웨어가 Windows에 알리는 기기 정보) [사실, `fw-audio-s256-mu/Silicon/Apple/AppleSiliconPkg/Drivers/SmbiosInfoDxe/SmbiosInfoDxe.c`]:

| 항목 | 값 | 줄 |
|---|---|---|
| BIOS 제조사·판 | "Apple Silicon Windows Project Dev", "Placeholder Version", "Placeholder Release Date" | `:137-139` |
| 시스템 제조사 | "Apple Inc.", 나머지 "Not Specified" | `:170-172` |
| 시스템 UUID | 소스에 고정된 한 값(이 코드를 쓰는 모든 기기가 같음) | `:151-163` |
| 모델(PCD) | "Mac Mini (2020)", "Mac9,1" | `MacMini2020.dsc:45-47` |
| 프로세서 문자열 | "Apple", "Apple Inc.", "Not Specified" | `:315-317` |
| 프로세서 현재 속도 | S142에서 3228 MHz로 고침 | `nwoas_scripts/smbios-s142/build_payload.py:59` |

- [사실, 원본] 원본은 Windows 작업 관리자의 CPU 이름이 SMBIOS Type 4 Version("Not Specified")에서 온다는 것을 레지스트리로 확인했고, "Apple M1"로 고치는 계획을 세웠습니다(원본 `investigation/analysis/CPU-DVFS-SMBIOS-PLAN.md`, `gpu/memory-latency` 가지).

#### 목표: 정품 인증·Windows Update·Store가 되고, 작업 관리자에 CPU 이름·속도가 바르게 나옵니다.

#### 단계별 계획

**B6-1. SMBIOS 정리**
- 할 일: 프로세서 이름 "Apple M1", 코어 수 8, 캐시 크기, BIOS 판·날짜를 실제 값으로, 시스템 UUID는 기기마다 다르게(예: 기기 일련번호에서 만든 값) 합니다.
- 바꿀 파일: `SmbiosInfoDxe.c`, 펌웨어 페이로드 빌드.
- 실기기 검증: 작업 관리자 CPU 이름, `wmic csproduct get uuid`.
- 콘치님 손·결정: UUID를 기기 일련번호에서 만들지(공개 저장소에 일련번호가 들어가면 안 되므로 빌드 시점에만 넣기) 결정.
- 위험: 작음. 정품 인증 전에 바꾸는 것이 좋음 [가설: 하드웨어 정보가 바뀌면 인증이 풀릴 수 있음].
- 선행 조건: 없음.

**B6-2. 정품 인증·업데이트·Store (네트워크 뒤)**
- 할 일: 콘치님의 Windows 11 Pro 정품 키로 인증, Windows Update 한 번, Store 앱 하나 설치.
- 바꿀 파일: 없음.
- 실기기 검증: `slmgr /xpr`, 업데이트 기록.
- 콘치님 손·결정: **정품 키(돈이 들 수 있음, 이미 가지고 계신지)**, 업데이트를 받을지.
- 위험: 누적 업데이트가 ntoskrnl·dwmcore를 바꿔 우리 진단 도구(BUGPROBE RVA, KD 오프셋)가 다시 어긋날 수 있음 [가설]. 기능 업데이트(새 Windows 판)는 TPM 없는 기기에 안 올 수 있음 [가설]. 업데이트 뒤 재부팅이 하이퍼바이저 다시 켜기를 요구(맥북 연결 모드).
- 선행 조건: 네트워크(다른 갈래), B6-1.

#### 재사용 코드와 라이선스: Mu(BSD-2-Clause-Patent) [가설].
#### 모르는 점: ARM64 Windows의 정품 인증 하드웨어 ID가 SMBIOS UUID에 얼마나 기대는지.
#### 대략 크기: B6-1 **작음**, B6-2 작음(네트워크 뒤).

---

## C. 최적화와 하이퍼바이저 부담

### C1. 실행 중에 하이퍼바이저로 들어가는 길 (한눈에)

| 길 | 언제 들어가나 | 우리 측정값 | 원본(J313) 측정값 | 줄이는 방법 | 근거 |
|---|---|---|---|---|---|
| 쉬는 CPU 대기(WFI) | CPU가 쉴 때마다 | 없음 | — | §C2 | [사실] `m1n1-gpu-hv2/src/hv.c:88-92`, `hv_exc.c:1951-2022` |
| CPU0 주기 작업 | 초당 5000번, 큰 잠금(bhl)을 쥐고 | 없음 | 낮추면 Windows가 수십 초씩 몰아서 진행(원본 EXP-029) | 당분간 유지 | [사실] `hv.c:24`, `README-dcp-s251.md:81`, 원본 `hv_tick_policy.c:9-17` |
| 보조 CPU 7개 주기 작업 | 각 초당 5000번 | 없음 | 원본은 100번 | §C3 | [사실] `hv.c:136-141` |
| 장치 인터럽트 전달 | 장치 인터럽트마다(AIC → 가상 GIC 목록 레지스터) | 없음 | 없음(원본도 안 잼) | 유지(M1에 GIC 없음) | [사실] `hv.c:96-98`(IMO/FMO), 원본 인벤토리 #3 |
| CPU 사이 인터럽트(IPI) 보내기 | 보낼 때마다 `ICC_SGI1R_EL1` 잡힘 | 없음 | 없음 | 유지 | [사실] `hv_exc.c:1122` |
| 인터럽트 확인·끝내기(IAR/EOI) | **잡지 않음**(하드웨어 가상 CPU 인터페이스) | — | 원본은 잡음(TALL1) | 이미 가벼움 | [사실] `hv_vgic.c:1770` (`ICH_HCR_EL2`=En·LRENPIE), `hv_exc.c:1066` |
| 타이머 전달 | 타이머가 울릴 때, **최소 간격 약 1 ms** | 없음 | — | §C4 | [사실] `hv_exc.c:350-400` |
| GPU 주소 중개 | GPU 작업·16 KiB 쪽 매핑마다 | 없음 | 쪽 갱신 4.1~9.5 µs, 프로세스 바꿀 때 160~272 µs(한 장에 2번), 초당 최대 약 3만 8천 번 | §C6 | [사실] 원본 `HV-RESPONSIBILITY-INVENTORY.md` #11 |
| 화면 출력(DCP) | DWM 출력마다(MMIO) + 5 kHz 주기 안에서 | 평균 적용 1221 µs, 상한 약 56장/초 | 원본은 매 하이퍼바이저 나갈 때 한 단계 | §C5 | [사실] vuart 기록, `README-dcp-s251.md:77,93` |
| 저장장치(NVMe) | 명령마다(가짜 NVMe) | 이 세션 문서에서 지금 경로 속도 측정값을 찾지 못함 | 중개 경로 읽기 478 MB/s vs 직접 2811~3433 MB/s | 저장장치 갈래(D2) | [사실] `nwoas_scripts/plan-20261115/port/storage.md:79-80` |
| USB(xHCI) | USB 전송으로 보이는 기록 다수(`SN XFER-*`, `SN MC` — TRB 수를 찍으므로 xHCI 중개로 봄 [가설]) | `SN MC` 1375줄(한 기록) | 원본은 잰 적 없음 | 먼저 세기(C11) | [사실] 실행 기록 줄 수, 원본 인벤토리 #16 |

### C2. 쉬는 CPU를 진짜로 쉬게 하기

#### 지금 상태
- [사실] 우리 하이퍼바이저는 `HCR_TWI`로 Windows의 대기 명령(WFI)을 잡습니다. 이유는 "winload의 조용한 멈춤을 보이게 하려는 진단"이었다고 주석에 적혀 있습니다(`m1n1-gpu-hv2/src/hv.c:88-92`).
- [사실] 잡은 뒤에는 기록만 하고 **다음 명령으로 넘깁니다**(`ctx->elr += 4`, `hv_exc.c:2015-2020`). 실제로 기다리지 않습니다.
- [사실] 원본은 같은 상황에서 하이퍼바이저가 직접 `wfi`를 실행해 진짜로 기다리고, 가상 인터럽트가 이미 있으면 기다리지 않습니다(`external/windows-on-m1/m1n1_windows/src/hv_exc.c:1862-1887`).
- [가설, 높음] 그래서 우리 Windows의 쉬는 CPU는 "대기 → 하이퍼바이저 → 돌아옴 → 다시 대기"를 끝없이 돕니다. 전력·열·팬이 늘고, 같은 클러스터의 다른 코어 성능에도 영향을 줄 수 있습니다.

#### 목표: 쉬는 CPU가 하드웨어 대기에 들어갑니다. 쉴 때 전력이 줄어듭니다.

#### 단계별 계획

**C2-1. 원본 방식으로 바꾸기**
- 할 일: `ESR_EC_WFI` 처리에서 WFE는 바로 돌려보내고, WFI는 가상 인터럽트가 없을 때 EL2에서 `wfi`를 실행한 뒤 돌려보냅니다(원본 `hv_wfx_trap_action` 규칙). 진단 기록은 처음 40번 그대로.
- 바꿀 파일: `m1n1-gpu-hv2/src/hv_exc.c`(WFI 처리), 새 패치 파일 `port-j274/gpu/hv/hv2-g254L-to-g255M.patch`(이름은 예시), `MANIFEST-hv2.sha256`, 새 실행기 사본.
- 실기기 검증: ① 바탕화면·작업 통로 정상, ② 30분 무사(0x101 CLOCK_WATCHDOG·0x124 없음), ③ 쉬는 동안 맥북에서 USB 전력 측정은 못 하므로 macOS `powermetrics` 대신 **팬 소리·만졌을 때 열**(콘치님) + 하이퍼바이저 기록에 CPU별 WFI 횟수, ④ 작업 관리자 CPU 사용률이 쉴 때 낮은지.
- 콘치님 손·결정: 팬 소리·열 느낌 확인(선택).
- 위험: 원본 기록에 WFI/WFE 처리 정책이 빠지자 긴 멈춤이 생긴 사례가 있음(`external/windows-on-m1/documentation/PLATFORM_STABILITY.md:21-35`). 타이머 FIQ가 EL2에서 대기 중인 CPU를 깨우는지가 핵심입니다. 실패하면 g254L로 되돌립니다.
- 선행 조건: 없음.

#### 재사용 코드와 라이선스: 원본 m1n1_windows `hv_exc.c`, `hv_wfx_*`(MIT) [사실, `m1n1_windows/LICENSE`].
#### 모르는 점: 쉬는 전력 차이를 숫자로 잴 방법(맥미니는 배터리가 없음) — SMC 전력 센서를 하이퍼바이저가 읽을 수 있는지 [모름].
#### 대략 크기: **작음**.

---

### C3. 보조 CPU 주기 작업 5000 → 100번/초

#### 지금 상태
- [사실] 주기 작업은 초당 5000번(`hv.c:24`). M1에는 ECV가 없어(옛 기록 `nwoas_scripts/logs/retest40-22h2.run14.log`의 "HV: No ECV supported") 보조 CPU도 같은 간격을 씁니다(`hv.c:136-141`).
- [사실] 원본은 보조 CPU를 초당 100번으로 낮췄고, 1000번으로도 초당 7000번 추가 진입과 끊김이 생겼다고 적었습니다(원본 `m1n1_windows/src/hv_tick_policy.c:37-49`). 원본 기록: FIQ가 초당 약 40,000번 → 약 11,500번(`PLATFORM_STABILITY.md:90-91`).
- 계산: 보조 7개 × (5000 − 100) = 초당 **34,300번** 하이퍼바이저 진입이 줄어듭니다 [계산].

#### 목표: 보조 CPU 7개의 주기 진입이 각 초당 100번.

#### 단계별 계획

**C3-1. 보조 CPU 간격 바꾸기**
- 할 일: `hv_secondary_tick_interval`을 ECV가 없을 때 100 Hz로. CPU0은 5000 그대로(원본이 낮추면 문제였음).
- 바꿀 파일: `m1n1-gpu-hv2/src/hv.c`, 패치·지문 파일, 실행기 사본.
- 실기기 검증: 30분 무사, 마우스·창 움직임이 몰려서 움직이지 않는지(콘치님), 하이퍼바이저 진단에 CPU별 FIQ 횟수(없으면 세는 줄 추가).
- 콘치님 손·결정: 마우스 감각 확인.
- 위험: 보조 CPU에서 주기 작업에 기대던 것(타이머 막힘 복구)이 늦어짐. 원본도 이 이유로 100번을 남겨 둠. C2와 **따로** 시험해야 원인을 가를 수 있습니다.
- 선행 조건: 없음(C2와 다른 날·다른 판으로).

#### 재사용 코드와 라이선스: 원본 `hv_tick_policy.c`(MIT).
#### 모르는 점: 우리 하이퍼바이저의 보조 CPU 주기 작업 안에 원본에 없는 우리 전용 일(USB 다리, NVMe 다리 등, `hv_exc.c:922-928`)이 있는지 — 코드 확인 필요.
#### 대략 크기: **작음**.

---

### C4. 타이머 최소 간격 약 1 ms

#### 지금 상태
- [사실] Windows 커널 단계에서 타이머 전달 최소 간격을 `CNTFRQ >> 10`(약 1 ms)으로 제한합니다. winload·UEFI 단계는 약 62 ms(`hv_exc.c:350-400`).
- [가설] Windows나 게임이 1 ms보다 짧은 타이머(예: 0.5 ms 해상도)를 쓰면 늦게 깨어납니다. 게임의 프레임 맞추기·입력 처리에 영향을 줄 수 있습니다.

#### 목표: 커널 단계에서는 제한 없이(또는 매우 짧게) 타이머를 전달합니다.

#### 단계별 계획

**C4-1. 커널 단계 제한 풀기 시험**
- 할 일: 커널 단계 최소 간격을 0(제한 없음) 또는 약 0.1 ms로 바꾼 판을 만듭니다. 주석이 말하는 "즉시 다시 울림" 잠김이 커널 단계에서는 구조적으로 없다는 판단(같은 주석)을 실기기로 확인합니다.
- 바꿀 파일: `m1n1-gpu-hv2/src/hv_exc.c`.
- 실기기 검증: 30분 무사, `timeBeginPeriod(1)`을 쓰는 시험 프로그램으로 잠자기 정확도(ms 표), 0x101 없음.
- 콘치님 손·결정: 없음.
- 위험: 중간. 타이머가 몰려 하이퍼바이저가 바빠질 수 있음.
- 선행 조건: C2·C3 뒤(한 번에 하나씩).

#### 재사용 코드와 라이선스: 우리 코드.
#### 모르는 점: 원본은 이 제한이 있는지(원본 `hv_exc.c`의 타이머 전달 코드 확인 필요).
#### 대략 크기: **작음**.

---

### C5. 화면 출력 초당 약 56장 상한

#### 지금 상태
- [사실] iBoot식 통로에는 "화면이 실제로 바뀐 순간" 신호가 없어, 하이퍼바이저가 적용 뒤 17 ms가 지나면 교체 완료로 칩니다. 완료 전 다음 출력은 BUSY라 최대 약 56장/초입니다(`port-j274/gpu/hv/README-dcp-s251.md:87-93`).
- [사실] 실측 평균 적용 시간 1221 µs(1024장, 첫 장 최대 18573 µs)(vuart 기록 `present #1024`).
- [가설] 60 Hz 모니터에서 DWM이 매 장을 맞추지 못해 가끔 장이 건너뛰어질 수 있습니다. 게임에서 체감될 수 있습니다.

#### 목표: 모니터의 실제 수직 동기에 맞춰 교체 완료를 알립니다(60장/초).

#### 단계별 계획

**C5-1. 수직 동기 신호 찾기(조사)**
- 할 일: 13.5 ADT의 DCP 관련 인터럽트와 iBoot 통로 응답 중 수직 동기 시각을 알 수 있는 것을 찾습니다. 없으면 적용 시각 + 한 장 길이(16.67 ms)를 정확한 60 Hz 박자에 맞추는 계산으로 바꿉니다.
- 바꿀 파일: 없음(조사), 이후 `hv_agx_scanout_service.c`, `display.c`.
- 실기기 검증: 화면 찢어짐(콘치님 눈), 초당 출력 수 기록, 원본 방식의 "창 빠르게 끌기 1분"(`README-dcp-s251.md:195`).
- 콘치님 손·결정: 화면 보기.
- 위험: 너무 일찍 완료를 알리면 찢어짐(같은 문서 `:208`).
- 선행 조건: 없음.

**C5-2. 간단한 중간 단계: 완료 시간 16.7 ms 이하로 시험**
- 할 일: 런처 값 `NWOAS_AGX_DCP_LATCH_US`(0~100000 µs, 같은 문서 `:92`)만 바꿔 16000 등으로 시험합니다. 하이퍼바이저를 다시 빌드할 필요가 없습니다.
- 바꿀 파일: 실행기 사본의 환경 변수 한 줄.
- 실기기 검증: 찢어짐 여부, 초당 출력 수.
- 콘치님 손·결정: 화면 보기.
- 위험: 찢어짐 → 원래 값으로.
- 선행 조건: 없음.

#### 재사용 코드와 라이선스: m1n1 `display.c`(MIT).
#### 모르는 점: iBoot 통로에 수직 동기 알림이 정말 없는지(지금 판단은 원본 코드가 iBoot 경로에서 래치를 내지 않는다는 근거, `README-dcp-s251.md:90`).
#### 대략 크기: C5-2 **작음**, C5-1 중간.

---

### C6. GPU 주소 중개를 드라이버로 옮기기

#### 지금 상태
- [사실] Windows는 GPU 메모리를 4 KB 쪽으로, Apple GPU는 16 KB 쪽으로 다뤄 하이퍼바이저가 두 표를 맞춥니다(원본 README.md:85-86). 우리 하이퍼바이저에 같은 코드가 들어 있습니다(`port-j274/gpu/hv/README-hv2.md:38`).
- [사실, 원본] 원본은 이것을 GPU 경로에서 가장 큰 하이퍼바이저 비용으로 꼽고 "드라이버 안에서 Asahi처럼 GPU 페이지 표를 직접 다루기"를 1순위로 정했습니다(원본 `HV-RESPONSIBILITY-INVENTORY.md` §2-1). 원본도 아직 하지 않았습니다.
- 원본 측정 [사실(원본), 같은 문서 #11]:

| 동작 | 시간·횟수 |
|---|---|
| 쪽 갱신(UPD_LEAF) | 4.1~9.5 µs |
| 작업 시작(JOB_BEGIN) | 3.3~3.6 µs |
| 프로세스 바꿀 때(LEASE) | 160~272 µs, 한 장에 2번 |
| 중개 시간 | 초당 59~376 ms(창 끌기 중) |
| 갱신 횟수 | 초당 최대 약 3만 8천 번, 매번 TLB 비우기 |

#### 목표: 평소 그리기·작업 제출에서 하이퍼바이저가 빠집니다. 하이퍼바이저는 부팅 때 한 번 주소 대응만 넘깁니다.

#### 단계별 계획

**C6-1. 우리 기기에서 중개 비용 먼저 재기**
- 할 일: 우리 하이퍼바이저의 중개 처리에 횟수·시간 합계(10초 창)를 세는 진단을 넣습니다(원본 EXP2250 방식).
- 바꿀 파일: `m1n1-gpu-hv2/src/hv_agx_gpuva_v5.c` 주변(진단만, 기본 꺼짐).
- 실기기 검증: 바탕화면 쉬기·창 끌기·GravityMark 각각 표.
- 콘치님 손·결정: 창 끌기 1분(또는 자동).
- 위험: 작음.
- 선행 조건: A2-1(벤치 준비).

**C6-2. 설계 문서**
- 할 일: WDDM 페이징 → 드라이버 안 Apple GPU 페이지 표(UAT)로 옮기는 설계. "게스트 주소 = 실제 주소" 계약(지금 1 GiB 예약은 1:1, `README-hv2.md:160-161`)을 쓸지, 주소 대응 표를 쓸지 정합니다.
- 바꿀 파일: 새 설계 문서.
- 실기기 검증: 없음.
- 콘치님 손·결정: 없음.
- 위험: 없음.
- 선행 조건: C6-1.

**C6-3. 구현과 실기기 시험** — 크기 큼, 위험 큼(GPU가 잘못된 메모리에 쓰면 Windows·macOS 데이터 손상 가능 [가설]). 시험은 GPU 예약 구역 안에서만 하도록 하이퍼바이저가 범위를 검사하는 중간 단계를 둡니다.

#### 재사용 코드와 라이선스: 원본 드라이버 `shared/src/apple_agx_uat.c`(MIT), Asahi `drivers/gpu/drm/asahi` UAT 처리(동작 참고, 라이선스 확인 필요).
#### 모르는 점: 우리 기기에서 실제로 얼마나 큰 비용인지(C6-1 전에는 모름).
#### 대략 크기: **큼**.

---

### C7. 저장장치와 USB

- 저장장치 직접 방식은 별도 갈래 결정(D2 "검사하는 직접 방식", `nwoas_scripts/plan-20261115/DECISIONS.md:6`)이 있으므로 여기서는 다루지 않습니다. 원본 수치: 읽기 478 → 2811~3433 MB/s [사실(원본), `port/storage.md:79`].
- USB: 한 실행 기록에 `SN MC` 줄이 1375줄 있습니다 [사실, `nwoas_scripts/logs/agxmed1g-s249-20261116-202802.1sXX3e` 기록 묶어 세기]. TRB 수를 찍는 줄이라 xHCI 중개 기록으로 봅니다 [가설]. 원본도 xHCI 훅 횟수를 잰 적이 없습니다 [사실, 원본 인벤토리 #16]. 입력 지연(게임)에 영향이 있을 수 있어 **C11의 측정 목록**에 넣습니다.

---

### C8. CPU 클럭, SLC, CPU 이름

#### 지금 상태
- [사실] 펌웨어가 부팅 직전(ReadyToBoot)에 성능 코어를 P12로 올립니다(`fw-audio-s256-mu/Silicon/Apple/AppleSiliconPkg/Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c:167`). 9월 실측: 같은 작업이 성능 코어 59 ms, 효율 코어 약 85 ms(`nwoas_scripts/cpufreq-s193/hardware-result.json`), P7 대비 성능 코어 약 1.53배(`nwoas_scripts/cpufreq-s170/README.md`).
- [사실] 실행 중 클럭을 바꾸는 장치가 없습니다(DSDT에 `_CPC` 0개 — `fw-audio-s256-mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl` grep 결과). 즉 **항상 같은 클럭**입니다.
- [사실, 원본] 원본은 하이퍼바이저가 흉내 내는 CPPC(`_CPC`)로 Windows 기본 전원 관리자(FxPPM)가 600~2988 MHz로 클럭을 바꾸게 했습니다(원본 GPU_CURRENT_STATE `00:30Z` 절, `CPU-DVFS-SMBIOS-PLAN.md`).
- [사실] 우리 하이퍼바이저 소스에서 SLC를 켜는 `mcc_enable_cache()`는 `kboot.c:2816`(리눅스식 부팅 경로)에서만 부릅니다. 원본은 "하이퍼바이저 경로에서 SLC가 꺼져 있었다"를 확인하고 켜서 GPU +8.5%를 얻었습니다(원본 GPU_CURRENT_STATE `21:50Z` 절).
- [가설, 높음] 우리 경로도 SLC가 꺼져 있습니다(실기기로 SLC 상태를 읽은 기록 없음).
- [사실] CPU 측정 기록: Cinebench 2026 ARM64 다중 코어 1888.454점(S216, `NWOAS-MORNING-2026-09-10.md:15`). 같은 기기 macOS 기준값은 없습니다.

#### 목표
- SLC를 켭니다.
- 쉴 때는 클럭을 내리고 일할 때 올립니다(전력·열).
- 작업 관리자에 CPU 이름·속도가 바르게 나옵니다(B6-1).

#### 단계별 계획

**C8-1. SLC 상태 읽고 켜기**
- 할 일: 먼저 지금 실행 중인 상태에서(또는 다음 부팅 프록시 단계에서) MCC의 SLC 설정을 읽기만 합니다. 꺼져 있으면 하이퍼바이저 시작 전 `mcc_enable_cache()`를 부르게 합니다.
- 바꿀 파일: `m1n1-gpu-hv2/src/hv.c` 또는 런처 모듈(`nwoas_scripts/layout-s231/guest_module.py`의 새 사본)과 지문.
- 실기기 검증: SLC 길 수 16/16 확인, A2-1 벤치와 Cinebench 전후 표.
- 콘치님 손·결정: 없음.
- 위험: 작음(Asahi·m1n1의 기본 동작). macOS 13.5 펌웨어 판에서 MCC 값이 다를 수 있음 [가설].
- 선행 조건: A2-1(효과를 재려면).

**C8-2. CPPC로 실행 중 클럭 바꾸기**
- 할 일: 원본 `hv_cppc.c`와 Mu `_CPC/_PSD/_OSC`를 옮깁니다. 성능 코어가 일정 클럭 이상일 때 DRAM 설정을 빠른 쪽으로 바꾸는 원본 규칙도 함께 봅니다.
- 바꿀 파일: 하이퍼바이저 새 파일, Mu DSDT, 펌웨어의 ReadyToBoot P12 고정은 CPPC가 켜지면 빼기.
- 실기기 검증: 작업 관리자 속도 변화, Cinebench, 쉬는 동안 클럭.
- 콘치님 손·결정: 없음.
- 위험: 중간. 원본 기록에서 CPPC 카운터 읽기가 초당 4천~3만 번 잡혔다가 메모리 기반으로 고쳤습니다(원본 인벤토리 #17).
- 선행 조건: C2(쉬는 CPU가 진짜 쉬어야 클럭 내리는 의미가 큼).

#### 재사용 코드와 라이선스: 원본 m1n1_windows `hv_cppc.c`(MIT), Mu(원본 포크, BSD-2-Clause-Patent) [가설]. 원본 `gpu/memory-latency` 가지에서 내려받아야 합니다.
#### 모르는 점: 맥미니(팬 있음)에서 P12 고정과 CPPC의 열 차이.
#### 대략 크기: C8-1 **작음**, C8-2 중간.

---

### C9. 하이퍼바이저 없이 갈 수 있나 (AIC 직접, 장치 직접 넘기기)

#### 지금 상태
- [사실] Windows ARM64는 Arm GIC를 기본 내장으로 지원하고, 다른 인터럽트 장치는 SoC별 "HAL 확장"으로 맞추는 구조가 있습니다(https://learn.microsoft.com/en-sg/windows-hardware/drivers/bringup/acpi-system-description-tables, 검색 요약).
- [사실] 그런데 **인터럽트 장치용 HAL 확장은 Windows 8 개발판(789x)에 들어왔다가 799x에서 빠졌고**, 이후 판에서는 내보내지 않는 함수를 패턴으로 찾아야 했다고 공개 프로젝트가 적었습니다(https://github.com/Wack0/NtHalExts README, Unlicense, ARMv7 Windows 8 대상).
- [가설, 높음] 그래서 Windows 11 ARM64에서 AIC를 직접 쓰려면 Microsoft HAL을 고쳐야 하고, 서명·업데이트 문제로 일상용이 될 수 없습니다. HAL 확장은 Microsoft 서명이 필요할 것으로 봅니다 [모름: 서명 조건은 이번에 확인 못 함].
- [사실] 원본도 같은 결론으로 하이퍼바이저가 GICv3를 흉내 냅니다(원본 README.md:83-84, `ARCHITECTURE.md:36-40`). 원본은 vGIC·PSCI·타이머·Apple 시스템 레지스터 처리를 "어떤 경우에도 하이퍼바이저에 남아야 하는 것"으로 정리했습니다(원본 인벤토리 §3).
- [사실] 우리는 이미 인터럽트 확인·끝내기를 하드웨어 가상 CPU 인터페이스로 처리합니다(§C1 표).

#### 목표: 하이퍼바이저를 없애는 대신 **얇게** 만듭니다. 평소 그리기·저장·화면 출력 경로에서 하이퍼바이저가 빠지고, 인터럽트·타이머·CPU 켜기만 남습니다.

#### 단계별 계획(묶음)

| 순서 | 할 일 | 갈래 | 크기 |
|---|---|---|---|
| 1 | 쉬는 CPU, 보조 CPU 주기, 타이머 간격 | C2·C3·C4 | 작음 |
| 2 | 진단 기능을 "실사용 판"에서 끄기(경로 기록 `HVLOG` 다수, 화면 흐름 보내기 등) | 새 | 작음 |
| 3 | 저장장치 직접 방식 | 저장장치 갈래 | 큼 |
| 4 | GPU 주소 중개를 드라이버로 | C6 | 큼 |
| 5 | DCP를 드라이버가 직접(하이퍼바이저 서비스 대신) | A5·C5 이후 | 큼 |
| 6 | CPU0 주기 작업 5000 → 1000 이하(폴링하는 것이 없어진 뒤) | 원본 인벤토리 §2-4 | 중간 |

2번 "실사용 판"의 세부:
- 할 일: 하이퍼바이저 빌드 선택지로 진단 출력(`NWOAS_HOTPATH_LOG`, `NWOAS_QUIET` 등 이미 있는 스위치)을 묶어 "조용한 판"을 만듭니다.
- 바꿀 파일: `m1n1-gpu-hv2` 빌드 변수, `tools-hv2/buildvar.sh` 호출.
- 실기기 검증: 기능 회귀 없음, 기록 크기 감소.
- 콘치님 손·결정: 없음.
- 위험: 문제 생겼을 때 기록이 적어짐 → 개발 중에는 진단 판, 일상은 조용한 판으로 나눔.
- 선행 조건: 없음.

#### 재사용 코드와 라이선스: 원본(MIT).
#### 모르는 점: Microsoft가 비 GIC 인터럽트 장치를 다시 지원할 계획이 있는지(공개 자료 없음, 제 검색 범위).
#### 대략 크기: 전체 **큼**, 1·2번은 작음.

---

### C10. 부팅 시간

#### 지금 상태
- [사실] 부팅 시간을 잰 기록을 이 세션 문서에서 찾지 못했습니다.
- [가설] 파일 생성 시각으로 본 대략값(같은 실행 묶음, 11-16):

| 시점 | 시각 | 근거 |
|---|---|---|
| 실행기 시작(이름의 시각) | 20:28:02 | `launcher-agxdcp256-dcp256a-20261116-202802.out` |
| 확인 단계 파일 | 20:28:04 | `.probe` |
| 작업 통로 세션 파일 | 20:28:10 | `.link/session.json` 생성 |
| 작업 통로 첫 사건 파일 | 20:28:31 | `.link/events.jsonl` 생성(첫 줄 "WinOS worker connected") |

  → 실행기 시작부터 Windows 작업 프로그램 연결까지 약 29초로 보이지만, `events.jsonl`이 연결 순간에 만들어지는지 확인하지 않았습니다 [가설].
- 이 값은 맥미니 전원 켜기·iBoot·D4 m1n1·맥북 USB 대기 시간을 포함하지 않습니다.

#### 목표: 전원 버튼부터 바탕화면까지를 구간별로 잽니다. 그다음 가장 긴 구간부터 줄입니다.

#### 단계별 계획
- **C10-1. 구간 기록 넣기**: 실행기·하이퍼바이저·Mu·Windows(작업 프로그램 시작, 로그인 화면) 각각에 시각 한 줄. 바꿀 파일: 실행기 사본, 하이퍼바이저 시작 줄(이미 있을 수 있음), Windows 시작 작업. 실기기 검증: 3번 재부팅 표. 콘치님 손: 전원 버튼 시각 알려 주시기(선택). 위험: 없음. 선행 조건: 없음. 크기: **작음**.
- **C10-2. 긴 구간 줄이기**: 예) DCP 넘겨받기(0.1~0.3 s 가설, `README-dcp-s251.md:85`), GPU 펌웨어 시작, 1 GiB 예약 검사(모의 5.2 ms, 같은 문서 `:84`). 크기: 결과에 따라.

#### 재사용 코드와 라이선스: 우리 코드.
#### 모르는 점: 맥북 없는 부팅에서 USB 대기가 없어지면 얼마나 줄어드는지(맥북 없는 부팅 갈래).
#### 대략 크기: **작음~중간**.

---

### C11. "네이티브처럼 느껴지는가"를 재는 목록

| 측정 | 방법 | 지금 값 | 기준(같은 기기 macOS 또는 목표) |
|---|---|---|---|
| GPU 벤치(GravityMark D3D12 1080p) | A2-1 | 없음 | macOS Metal 같은 설정 |
| CPU 다중 코어(Cinebench 2026 ARM64) | 작업 통로 | 1888.454점 (9월, `NWOAS-MORNING-2026-09-10.md:15`) | macOS 같은 판 |
| 화면 출력 초당 장수 | 하이퍼바이저 `present` 기록 | 상한 약 56장/초 | 60장/초 |
| 화면 출력 적용 시간 | 같음 | 평균 1221 µs | — |
| 타이머 정확도(1 ms 잠자기) | 시험 프로그램 | 없음 | 1 ms ± 작게 |
| 쉬는 동안 CPU별 하이퍼바이저 진입 횟수 | 하이퍼바이저 세는 줄(새로) | 없음 | 원본 FIQ 약 11,500/초(8코어 합) |
| USB 입력 지연 | USB 중개 기록 횟수 + 키 입력 반응(콘치님 감각) | 없음 | — |
| 저장장치 순차 읽기 | DiskSpd/CrystalDiskMark | 이 세션 문서에서 지금 경로 값을 찾지 못함 | 원본 직접 방식 2811~3433 MB/s |
| 부팅 시간 | C10-1 | 없음 | — |
| 쉬는 동안 팬·열 | 콘치님 감각 | 없음 | macOS 쉴 때 |

---

## 출처

### 우리 파일 (줄은 본문에 적음)
- `HANDOFF-2026-11-16-evening.md`
- `port-j274/gpu/driver-p15/PATCHES.md`, `THIRD_PARTY.md`, `README.md`
- `port-j274/gpu/driver-p15/drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp`, `mesa/d3d11/agx_d3d11_ddi.cpp`, `mesa/d3d11/agx_d3d11_ddi.h`, `mesa/scripts/build-native-asahi-state.py`, `render-admission/AppleAgxRenderAdmission.inf`, `render-admission/umd/build-umd-x64.ps1`, `render-admission/umd/forwarder/build-arm64x-forwarder.ps1`, `render-admission/scripts/repackage-driver.ps1`
- `port-j274/gpu/driver-p15/out/j274-full-20261116-1938-p21/package/`
- `port-j274/gpu/hv/README-hv2.md`, `README-dcp-s251.md`
- `m1n1-gpu-hv2/src/hv.c`, `hv_exc.c`, `hv_vgic.c`, `kboot.c`, `mcc.c`
- `nwoas_scripts/agxdcp-s256-guest-test.sh`, `nwoas_scripts/logs/agxmed1g-s249-20261116-202802.1sXX3e*`
- `nwoas_scripts/plan-20261115/{gpu,DECISIONS,network}.md`, `nwoas_scripts/plan-20261115/port/storage.md`, `nwoas_scripts/plan-20261116-gpu-week.md`
- `nwoas_scripts/cpufreq-s170/README.md`, `cpufreq-s193/hardware-result.json`, `nwoas_scripts/smbios-s142/build_payload.py`
- `fw-audio-s256-mu/Platform/MacMini2020Pkg/MacMini2020.dsc`, `fw-audio-s256-mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc`, `.../Drivers/SmbiosInfoDxe/SmbiosInfoDxe.c`, `.../Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c`
- `autounattend.xml`, `NWOAS-STATUS-2026-11-15.md`, `NWOAS-MORNING-2026-09-10.md`

### 원본 (paulsmir/windows-on-m1, MIT)
- 로컬 사본 `external/windows-on-m1`(main `6c65673`, 2026-11-07): `README.md`, `documentation/{LIMITATIONS,AGX_BRINGUP,ARCHITECTURE,PLATFORM_STABILITY,ROADMAP}.md`, `m1n1_windows/src/{hv_exc.c,hv_tick_policy.c,hv_vtpm.c,hv.c}`, `mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc`
- GitHub `integration/ad04-windows-compiler` 가지: `investigation/GPU_CURRENT_STATE.md`, `investigation/analysis/D3D12-X64-WOA-UMD-ABI.md`, `investigation/analysis/RC34-tdr-recovery.md`, `investigation/CHANGES.csv`
- GitHub `gpu/memory-latency` 가지(2026-11-16까지 갱신): `investigation/GPU_CURRENT_STATE.md`, `investigation/analysis/HV-RESPONSIBILITY-INVENTORY.md`, `investigation/analysis/CPU-DVFS-SMBIOS-PLAN.md`, 커밋 `04850c6`, `19f1063`, `1cadce2`, `cda0484`, `1766685`, `4eb0b0e`

### 웹
- Riot: https://support.riotgames.com/en-us/league-of-legends/performance/minimum-and-recommended-system-requirements-league-of-legends , https://support.riotgames.com/en-us/riot/performance/vanguard-security-requirements , https://support.riotgames.com/en-us/riot/security/restore-windows-security-to-normal-state , https://support-legendsofruneterra.riotgames.com/hc/en-us/articles/48069697996435-Error-VAN-RESTRICTION
- ARM용 부정행위 방지: https://overclock3d.net/?p=310458 , https://www.pcgameshardware.de/RTX-Spark-Hardware-286023/News/Nvidia-hunderte-PC-Spiele-auf-ARM-Laptops-DLSS-5-Anti-Cheat-1556672/
- Microsoft: https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option , https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs , https://learn.microsoft.com/en-sg/windows-hardware/drivers/bringup/acpi-system-description-tables , https://apps.microsoft.com/detail/9nqpsl29bfff
- EV 인증서 가격: https://www.ssl.com/products/software-integrity/code-signing/ev/ , https://ssl.com/certificates/iv-ev-code-signing/buy/ (검색 요약)
- HAL 확장: https://github.com/Wack0/NtHalExts
- 중첩 가상화: https://news.hada.io/topic?id=15318 , https://forum.parallels.com/threads/macos-15-sequoia-nested-virtualization-for-m3-macs.364397/
- Asahi: https://asahilinux.org/2026/02/progress-report-6-19/ , https://www.phoronix.com/news/apple-video-decode-avd-linux
- NVIDIA WoA Vulkan/GL: https://docs.nvidia.com/rtx-spark/rtx-spark-porting-guide/latest/porting/vk-gl.html
