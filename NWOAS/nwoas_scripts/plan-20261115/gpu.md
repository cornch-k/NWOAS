# 갈래 문서: GPU(그래픽 가속)

작성일: 2026-11-15
범위: Mac mini M1(J274, 2020)에서 m1n1 하이퍼바이저 S231 위에 올린 Windows 11 Pro 25H2 ARM64(빌드 26200)에 Apple GPU 가속을 붙이는 일.
이 문서의 모든 판단은 조사(코드 읽기·웹 자료)에서 나왔습니다. **우리 J274에서 GPU 관련 코드를 빌드하거나 실행한 결과는 하나도 없습니다.**

표기 규칙
- **[사실]**: 파일:줄 또는 URL로 확인한 내용입니다. 다른 사람이 쓴 문서의 숫자는 "작성자 주장"으로 따로 표시합니다.
- **[가설]**: 확인하지 못한 추측입니다. 확신 정도를 함께 적습니다.

---

## 1. 결론

1. **Windows용 Apple GPU 드라이버는 이미 공개되어 있습니다.** paulsmir/windows-on-m1(MIT 라이선스)이 M1 MacBook Air(J313, 우리와 같은 칩)에서 Direct3D 12·11과 OpenGL 2.1, GPU로 그리는 바탕화면까지 됐다고 적고 있습니다[사실, 작성자 주장]. 우리 상태 문서의 "Windows용 Apple GPU 드라이버는 없다"(NWOAS-STATUS-2026-11-15.md:41)는 낡은 내용입니다[사실].
2. **그러나 지금 부팅 방식 그대로는 어떤 공개 코드로도 우리 GPU를 돌릴 수 없습니다.** GPU 안의 보조 프로세서가 쓰는 펌웨어는 부팅하는 macOS 쪽에서 실려 오는데, 지금은 macOS 26(Tahoe)판입니다. 공개된 드라이버(Asahi Linux, paulsmir)는 macOS 12.3·13.5판 펌웨어만 받습니다[사실: 드라이버 쪽 목록 / 거의 사실: 우리 쪽이 Tahoe판이라는 점 — 아래 2.3 참조].
3. **그래서 GPU 작업의 첫 관문은 코드가 아니라 "macOS 13.5판 펌웨어로 부팅하는 작은 전용 영역(스텁)"을 만들 수 있느냐입니다.** 이 관문은 "맥북 없이 macOS/Windows 듀얼부팅" 갈래와 같습니다. 같은 스텁이 화면(DCP) 문제도 함께 풀 가능성이 있습니다[가설, 중간 확신].
4. **리그 오브 레전드(LoL)는 GPU 드라이버가 완벽해도 당분간 안 됩니다.** 원인은 GPU가 아니라 Riot의 부정행위 방지 프로그램 Vanguard입니다. Vanguard는 ARM용이 널리 나오지 않았고[사실], 가상화 환경을 거부합니다[사실]. NWOAS는 하이퍼바이저 위에서 Windows를 돌리므로 정면으로 걸립니다[가설, 높은 확신].
5. 권하는 순서: **(1) 하드웨어 없이 확인할 것부터 확인 → (2) Tahoe 시스템 펌웨어 위에서 13.5 스텁이 부팅되는지 확인 → (3) GPU 펌웨어 켜기/끄기만 해 보는 최소 시험 → (4) paulsmir 드라이버 이식.** GPU 코드는 (2)가 확인되기 전에는 쓰지 않습니다.

---

## 2. 현재 상태

### 2.1 우리 Windows 화면·그래픽의 지금 모습 [사실, 작업 지시 문맥과 상태 문서]

| 항목 | 지금 상태 |
|---|---|
| 화면 출력 | 펌웨어가 넘겨 준 고정 화면(1280x720), Windows 기본 디스플레이 드라이버 |
| 화면 장치(DCP) 설정 | 맥북 쪽 python 코드(display-s190/tahoe_dcp_guest_hook.py)가 대신 처리 |
| GPU 가속 | 없음 |
| 맥북 연결 | 필수 |

### 2.2 paulsmir/windows-on-m1 — 이미 있는 Windows용 Apple GPU 드라이버

[사실] 저장소 기본 정보(GitHub API 확인)

| 항목 | 값 |
|---|---|
| 라이선스 | MIT |
| 만든 날 | 2026-08-03 |
| 마지막 반영 | 2026-11-15 |
| 기여자 수(명) | 1 |
| 커밋 수(개) | 약 8,761 |
| GPU 소스 위치 | 브랜치 `integration/ad04-windows-compiler`, drivers/apple-agx/{windows,d3d12,mesa,shared,render-admission} |
| GPU 소스 파일 수(개) | 970 (일부는 Mesa를 그대로 가져온 것) |
| .c / .h / .cpp 크기(MB) | 약 7.7 / 2.8 / 1.2 |

[사실, 작성자 주장 — J313 MacBook Air 기준, README.md:23-35] 성능

| 시험 | Windows 점수(점) | macOS Metal 점수(점) | 비율(%) |
|---|---|---|---|
| GravityMark D3D12, 1080p | 5,396 | 7,814 | 69 |
| GravityMark D3D12, 720p | 6,769 | 9,946 | 68 |

| 시험 | 결과(FPS) |
|---|---|
| Counter-Strike 1.6 | 약 47 |

[사실] 구조(README.md:73-90)
- 앱이 부르는 D3D12/D3D11 그래픽 드라이버(사용자 영역): Asahi의 셰이더 컴파일러(Mesa, MIT)에 Windows 셰이더 형식(DXIL)을 바꿔 넣는 변환기를 붙였습니다.
- Windows 커널 드라이버(WDDM): GPU 펌웨어와 직접 통신합니다.
- m1n1 기반 하이퍼바이저: GPU 메모리 주소표를 대신 관리합니다. Windows는 4KB 단위로 메모리를 다루고 GPU 주소 변환 장치는 16KB 단위라서 둘 사이를 맞추는 중개가 필요하기 때문입니다.

[사실] 제약
- GPU 드라이버는 **두 번째 Mac이 붙은 "보조 모드"에서만** 동작합니다(README.md:25, 123). 혼자 부팅하는 모드는 기본 시스템까지만 됩니다.
- 이 제약은 우리 "맥북 연결 개발 모드"와 같은 구조라서 **개발 단계에서는 막는 요인이 아닙니다.**
- 혼자 부팅할 때는 Asahi 설치기가 만든 부팅 항목을 씁니다(README.md:118, LIMITATIONS.md:9-10). 보조 모드도 같은 13.5 스텁 항목에서 시작하는지는 문서에 없습니다[미확인].
- 다른 M1 모델은 보드 이름만 바꿔서는 안 된다고 적혀 있습니다(LIMITATIONS.md:14-16). GPU 첫 단계의 기준값 파일(config/j313-agx.json)이 J313의 장치 정보·m1n1·UEFI 식별값을 정확히 고정합니다(AGX_BRINGUP.md:13-27). J274로 옮기려면 이 기준값을 처음부터 다시 만들어야 합니다.
- 펌웨어 쪽 고정값은 "V13_5/G13"입니다(AGX_BRINGUP.md:16).
- 펌웨어를 껐다 다시 켜려면 사이사이 전원을 완전히 끄는 재부팅이 필요했다고 적혀 있습니다. 깔끔한 전원 초기화는 아직 증명되지 않았습니다(AGX_BRINGUP.md:43-46).
- 성능 차이의 원인으로 GPU 주소 변환 캐시 실패를 들고 있습니다(16KiB 페이지 하나 바꿀 때 약 600ns 대 65ns, README.md:97-104). 2026-11-07 커밋에 "guest VMID 1 does not change the per-page GPU cost"라고 적혀 있어 이 문제는 아직 풀리지 않았습니다.
- 문서끼리 맞지 않는 곳이 하나 있습니다. LIMITATIONS.md:18은 아직 "가속 GPU 드라이버 없음"이라고 적습니다. 그래서 README의 숫자는 **작성자 주장**으로 다루고, 누가 따로 검증한 값으로 보지 않습니다. (AGX_BRINGUP.md:122-133은 첫 단계에서 "하지 않을 일" 목록이라 서로 어긋나는 내용이 아닙니다.)

[사실] GPU 외에 다른 갈래에도 쓸 만한 것(README.md:20)
- 실기기 Apple SSD 드라이버(AppleAns, 작성자 주장 3.4 GB/s): 우리 저장장치 흉내 내기를 없애는 데 참고가 됩니다.
- 키보드·트랙패드 입력(SPI-HID), Asahi 스텁 기반 혼자 부팅: 맥북 없는 부팅 갈래에 참고가 됩니다.

[사실] 우리 문서 쪽 상태
- NWOAS-STATUS-2026-11-15.md:41은 "Windows용 Apple GPU 드라이버는 없다"고 적고 있습니다. 낡은 내용입니다.
- NWOAS 최상위 문서와 layout-s231 문서 어디에도 paulsmir가 나오지 않습니다(grep 확인).

### 2.3 펌웨어 판(版) 문제 — 이 갈래의 핵심 걸림돌

[사실] Asahi Linux GPU 커널 드라이버(drm/asahi, Rust)가 받는 GPU·펌웨어 조합
- 라이선스: `GPL-2.0-only OR MIT`(AsahiLinux/linux `asahi` 브랜치 drivers/gpu/drm/asahi/driver.rs:1). MIT로도 쓸 수 있으므로 펌웨어 통신 방식을 그대로 참고할 수 있습니다.
- 받는 조합(driver.rs:182-205):

| GPU 세대 | 받는 펌웨어 판 |
|---|---|
| G13(M1) | macOS 12.3, 13.5 |
| G14(M2) | macOS 12.4, 13.5 |
| 그 밖 | "Unsupported GPU/firmware combination"으로 거부 |

[사실] 우리 m1n1 트리가 아는 판
- GPU 펌웨어 구조 목록: V13_5까지(m1n1-layout-s231/proxyclient/m1n1/constructutils.py:803-804).
- OS 펌웨어 판 목록: V15_0(`iBoot-11881.1.1`)까지(m1n1-layout-s231/src/firmware.c:23-43).
- 지금 부팅에서 쓰는 OS 쪽 iBoot 판은 `mBoot-18000.121.3`이고, C 화면 코드는 이 Tahoe 메시지 형식을 거부합니다(m1n1-layout-s231/src/afk.c:323-328).

[사실] OS 쪽 저장 영역(컨테이너)에 iBoot2와 펌웨어가 들어 있다고 Asahi 문서가 적습니다(https://asahilinux.org/docs/platform/open-os-interop/).

판단: 위 세 가지를 합치면 **"지금 GPU 펌웨어는 Tahoe판이다"는 거의 사실**입니다. 남은 확인은 하드웨어 실행 없이 m1n1 부팅 로그의 "OS FW version:" 줄을 보는 것뿐입니다. Tahoe판은 받는 목록(12.3, 13.5)에 없으므로 Asahi·m1n1·paulsmir 어느 코드로도 지금 그대로는 GPU를 돌릴 수 없습니다. Tahoe판 GPU 펌웨어 구조를 처음부터 분석하는 길은 몇 달짜리 일입니다[가설, 높은 확신].

[사실] 우리 m1n1 부팅 항목("D4")은 1TR(복구 환경)에서 `kmutil configure-boot`로 등록했습니다(layout-s231/STANDALONE-CENSUS-20261115.md:324). 그것이 macOS 26 Macintosh HD 쪽이라는 점은 같은 문서 :353의 맥락에서 추론한 것입니다[가설, 높은 확신].

### 2.4 13.5 스텁이 두 문제를 같이 풀 가능성 [가설, 중간 확신]

계획 중인 듀얼부팅용 작은 영역 NWSTUB(4GB, STANDALONE-CENSUS-20261115.md:43, 347-357)을 Asahi 방식으로 13.5 펌웨어를 넣어 만들면 다음 두 가지가 함께 생깁니다.
- Asahi와 paulsmir가 받는 GPU 펌웨어.
- 우리 m1n1의 C 화면 코드가 이미 다룰 줄 아는 DCP 펌웨어. 혼자 부팅할 때의 화면 문제도 풀릴 수 있습니다.

**확인되지 않은 점**: macOS 26 시스템 펌웨어가 깔린 Mac mini에서 13.5 OS 펌웨어 스텁이 부팅되는지는 근거를 찾지 못했습니다. 이것이 이 갈래의 첫 실제 작업입니다.

### 2.5 Asahi Linux 쪽 참고 상태 [사실]

| 항목 | 상태 | 출처 |
|---|---|---|
| GPU 구동 방식 | GPU 안의 ARM64 보조 프로세서(ASC)가 Apple RTKit 펌웨어를 돌리고, 드라이버는 대부분 이 펌웨어와 대화 | https://asahilinux.org/docs/hw/soc/agx/ |
| OpenGL 4.6 / ES 3.2 | 적합성 인증 통과(2024-02) | https://appleinsider.com/articles/24/02/14/asahi-linux-conformant-to-opengl-46-opengl-es-32-on-apple-silicon-macs |
| Vulkan 1.3 (Honeykrisp) | 적합성 인증 통과 | https://asahilinux.org/2024/06/vk13-on-the-m1-in-1-month/ |
| Vulkan 1.4 | 적합성 인증 통과 | https://rosenzweig.io/blog/vulkan-14-sur-asahi-linux.html |
| OpenCL 3.0 | 적합성 인증 통과 | https://docs.mesa3d.org/drivers/asahi.html |
| M3 대응 | macOS 14.8.3 펌웨어 방식을 목표 | https://linuxiac.com/asahi-linux-gets-closer-to-full-m3-support-on-apple-silicon-macs/ |

분석 방법도 참고가 됩니다. macOS를 m1n1 하이퍼바이저 위에 올려 놓고 python 추적 코드로 GPU 펌웨어와 주고받는 공유 구조와 레지스터 접근을 기록했습니다. 같은 코드가 우리 트리 proxyclient/m1n1/fw/agx/(channels.py, cmdqueue.py, initdata.py, microsequence.py, handoff.py)에 있습니다. 셰이더 명령어 체계는 Metal 셰이더 바이너리를 풀어서 알아냈습니다(https://rosenzweig.io/blog/asahi-gpu-part-3.html, https://rust-for-linux.com/apple-agx-gpu-driver). Tahoe판 펌웨어를 직접 분석하는 길을 고르면 이 방법을 그대로 쓰게 됩니다.

### 2.6 Windows ARM에서 그래픽 API를 겹쳐 쓰는 방법

| 방법 | 하는 일 | 필요한 것 | 판정 |
|---|---|---|---|
| Mesa Dozen | D3D12 위에 Vulkan을 얹음 | 진짜 D3D12 드라이버 | [사실] 밑에 드라이버가 없으면 쓸모없음 |
| GLOn12 / CLOn12 (Microsoft 호환 팩) | D3D12 위에 OpenGL/OpenCL을 얹음 | 진짜 D3D12 드라이버 | [사실] 위와 같음 |
| DXVK / vkd3d-proton | Vulkan 위에 D3D9~12를 얹음 | 진짜 Vulkan 드라이버 + 커널 드라이버 | [사실] 방향이 반대. 그래도 Windows 바탕화면 합성(DWM)은 진짜 D3D 드라이버가 필요[가설, WDDM 구조 근거] |

판단: **D3D11/D3D12를 직접 만드는 것이 가장 값어치가 큽니다.** 그 위에 Dozen과 GLOn12로 Vulkan과 OpenGL이 따라옵니다. paulsmir도 이 길을 골랐습니다.

x86/x64 게임과의 관계 [사실]
- Windows ARM의 x86/x64 흉내 내기는 사용자 영역만 다룹니다. 커널 드라이버는 반드시 ARM64여야 합니다(https://learn.microsoft.com/windows/arm/apps-on-arm-x86-emulation).
- x64 앱이 GPU를 쓰려면 그래픽 드라이버의 사용자 영역 부분이 x64 앱 안에 실려야 합니다. Snapdragon 850 드라이버는 이것이 안 돼서 x64 앱이 가속 없이 돌았습니다(https://chipsandcheese.com/p/state-of-windows-on-arm64-a-high-level-perspective).
- Vulkan·OpenGL 로더는 ARM64X(ARM64와 x64 겸용) 바이너리입니다(https://docs.nvidia.com/rtx-spark/rtx-spark-porting-guide/0.1.0/porting/vk-gl.html).
- paulsmir의 x64 쪽 진행(investigation/analysis/D3D12-X64-WOA-UMD-ABI.md, 2026-10-20, 작성자 기록):
  - 서명된 패키지에 x64·Arm64EC 이미지가 없습니다.
  - x64 앱이 `D3D12CreateDevice`를 부르면 `DXGI_ERROR_UNSUPPORTED`를 받았습니다.
  - 내용 없이 요청만 넘기는 Arm64X 껍데기는 J313에서 x64와 ARM64 양쪽에 실리는 것이 확인됐습니다.
  - "A real x64 UMD and signed package remain open"(진짜 x64 드라이버와 서명 패키지는 아직 남은 일).
  - 즉 **x64 드라이버는 진행 중이고 미완성**입니다. LoL 같은 x64 게임에는 이것이 필요합니다.

### 2.7 리그 오브 레전드(LoL)

[사실]
- LoL은 지금 Windows on Arm에서 돌지 않습니다. 원인은 Vanguard의 ARM 미지원입니다(https://isitonarm.com/app/league-of-legends/, 2026-11-05 확인).
- 2026-06-01 무렵 Microsoft·NVIDIA가 RTX Spark용 ARM 네이티브 부정행위 방지 지원을 발표했고, Riot은 LoL·Valorant가 그 플랫폼을 지원할 것이라고 했습니다. 날짜는 없습니다(https://www.tomshardware.com/pc-components/cpus/nvidia-says-rtx-spark-chip-will-support-all-major-anti-cheat-and-drm-technologies-fortnite-valorant-denuvo-and-more-to-work-natively-with-windows-on-arm, https://pcgamer.com/hardware/gaming-laptops/nvidias-working-with-all-the-anti-cheat-vendors-to-make-competive-games-work-on-rtx-spark-and-woa-just-when-gaming-on-linux-was-looking-good). 2026-10 기준 웹 검색으로는 발표만 있고 출시는 확인되지 않았습니다. 게임 본체는 그래도 x64 흉내 내기(Prism)로 돕니다.
- Windows 11에서 LoL은 TPM 2.0과 DirectX 11 기능 수준 11_0을 요구합니다(https://support.riotgames.com/en-us/league-of-legends/performance/minimum-and-recommended-system-requirements-league-of-legends). Secure Boot는 요구하지 않는다고 Riot이 밝혔습니다(https://www.pcgamer.com/games/moba/we-have-not-confirmed-any-instance-of-vanguard-bricking-anyones-hardware-following-its-league-of-legends-rollout-riot-says-but-there-are-definitely-problems-for-some-players).
- Vanguard는 가상머신과 "지원하지 않는 가상화 환경"을 거부합니다(오류 VAN 138: https://support.riotgames.com/en-us/riot/performance/error-van-138, VAN 9100: https://support.riotgames.com/en-us/riot/client/error-van-9100).

[가설, 높은 확신] NWOAS는 거부 조건을 한꺼번에 가지고 있습니다.
- Windows 밑에 하이퍼바이저가 있고, 인터럽트 장치(GIC)를 흉내 내고, 메모리 접근을 가로채고, 가짜 NVMe 장치를 보여 줍니다.
- 테스트 서명 드라이버를 쓰므로 테스트 서명 모드입니다. Vanguard가 이 모드 자체를 문제 삼을 가능성도 있습니다(조사 안 함).
- 지금 TPM 2.0이 없습니다. UEFI(Mu)에 소프트웨어 TPM을 넣어도 Riot이 거부할 수 있습니다.

[가설] GPU 드라이버가 완벽해도 LoL은 다음이 모두 갖춰질 때까지 막힙니다.
- Riot이 ARM64 Vanguard를 RTX Spark 밖으로도 널리 내놓을 것.
- Vanguard가 이 플랫폼을 받아 줄 것.
- 하이퍼바이저가 드러나지 않거나 없어질 것. 그런데 M1의 인터럽트 장치(AIC) 위에서 Windows가 흉내 낸 GIC를 필요로 하는 동안에는 하이퍼바이저를 없앨 수 없습니다.

하이퍼바이저를 부정행위 방지 프로그램에게서 숨기는 것은 Riot 이용 약관 위반일 수 있습니다. 이 부분은 더 조사하지 않았고, 이 문서는 그런 방향을 권하지 않습니다.

---

## 3. 해야 할 일

순서대로 진행합니다. 한 단계가 검증을 통과하기 전에는 다음 단계 구현에 들어가지 않습니다(프로젝트 작업 규칙).

### 3.1 단계표

| 단계 | 할 일 | 담당 | 하드웨어 필요 | 검증 방법(통과 기준) |
|---|---|---|---|---|
| G-0a | 상태 문서 고치기: NWOAS-STATUS-2026-11-15.md:41의 "드라이버 없음"을 paulsmir 존재와 제약으로 바꾸기 | Claude(콘치님 승인 후) | 아니요 | 문서에 출처 URL과 "작성자 주장" 표시가 있는지 확인 |
| G-0b | 최근 m1n1 부팅 로그에서 "OS FW version:" 줄 찾기 | Claude(기존 로그 읽기만) | 아니요(이미 있는 로그) | 줄이 나오고 값이 Tahoe판(`mBoot-18000.121.3` 대응)인지 확인. 로그가 없으면 다음 부팅 때 콘치님이 이 줄을 캡처 |
| G-0c | 지금 장치 정보(ADT)의 GPU 노드 덤프 확보(주소, 인터럽트 9개, 펌웨어 메모리 영역). `apple,firmware-compat`는 ADT 값이 아니라 m1n1이 Linux용으로 만들어 넣는 값이므로(src/kboot_gpu.c:673-691) 찾지 않음 | 콘치님(실기기 부팅 때 덤프) + Claude(분석) | 예(읽기만, 그리기 작업 없음) | GPU 노드 이름·주소·인터럽트 수를 J313 기준값(config/j313-agx.json)과 표로 비교 |
| G-1 | Tahoe 시스템 펌웨어 위에서 13.5 스텁이 부팅되는지 조사·시험. 먼저 Asahi 설치기의 macOS 26 호환 여부를 문서·이슈로 확인 | Claude(조사) → 콘치님(실기기 시험) | 예 | 스텁에서 m1n1이 뜨고 로그의 "OS FW version:"이 13.5로 나오는지. **듀얼부팅 갈래와 함께 진행** |
| G-2 | GPU 펌웨어 켜기/끄기 최소 시험(paulsmir의 첫 관문, AGX_BRINGUP.md:29-46): 켜고 → 1초 안에 관리 응답 받고 → 끄고 → 전원 완전 재부팅, 10번 반복 | Claude(J274용 기준값 작성) → 콘치님(실기기) | 예(13.5 스텁 부팅 뒤) | 10번 모두 1초 안 응답. **이것이 GPU 진행 여부를 가르는 진짜 관문.** 화면에는 아무것도 안 나옴 |
| G-3 | 하이퍼바이저에 GPU 주소표 중개 기능 넣기(paulsmir HV 변경을 S231에 합치기), J274용 ACPI(Mu) GPU 설명 작성 | Claude(Opus — 하이퍼바이저·메모리 변환 핵심부) | 예 | 빌드 성공은 통과 아님. 실기기에서 GPU 커널 드라이버가 장치를 찾고 펌웨어를 올리는 로그가 나와야 통과 |
| G-4 | paulsmir WDDM 커널 드라이버 + D3D12/D3D11 사용자 영역 드라이버를 J274용으로 빌드·테스트 서명 | Claude | 예 | 실기기에서 장치 관리자에 드라이버가 정상, dxdiag에 D3D 기능 수준 표시, 간단한 D3D 시험 프로그램 실행 |
| G-5 | 바탕화면 합성이 GPU로 도는지, 1시간 이상 안정성, 시간 초과 복구 | 콘치님(사용) + Claude(로그 분석) | 예 | 화면 멈춤·SError 없이 1시간, 의도적 GPU 시간 초과 후 복구 |
| G-6 | (선택) x64용 사용자 영역 드라이버(Arm64X) — x64 게임용 | Claude | 예 | x64 시험 프로그램의 `D3D12CreateDevice`/`D3D11CreateDevice` 성공 |

### 3.2 다른 길: 화면만 먼저 좋게(M-D)

| 항목 | 내용 |
|---|---|
| 하는 일 | DCP를 쓰는 Windows 화면 전용 드라이버로 기본 디스플레이 드라이버를 바꿔, 모니터 본래 해상도와 HDMI 연결/분리 감지 지원 |
| 보이는 효과 | 콘치님 눈에 바로 보임(해상도) |
| 한계 | GPU 가속이 아님. Tahoe판 대 13.5판 DCP 펌웨어 문제를 먼저 풀어야 함(G-1과 같은 전제) |
| 참고 | paulsmir도 화면 출력은 GPU와 따로 둠(AGX_BRINGUP.md:150-151). J274의 HDMI가 주 DCP에 붙어 있다는 점은 가설 |

### 3.3 공수 짐작 [전부 가설]

| 길 | 전제 | 공수(사람-개월) | 근거·비고 |
|---|---|---|---|
| paulsmir 스택을 J274로 이식, GPU 바탕화면까지 | 13.5 스텁 부팅 성공 | 2~4 (AI 도움 전제) | paulsmir는 저장소 생성부터 D3D12 FL12_1까지 약 3.2개월(수년간의 Asahi 성과 위에서). 원래 짐작 1~3에서, J274 기준값을 처음부터 다시 만들어야 한다는 점(LIMITATIONS.md:14-16, AGX_BRINGUP.md:13-27)을 반영해 올려 잡음 |
| 위에 더해 x64 드라이버·안정성·전력 관리 | 위와 같음 | 추가 4~12 | paulsmir도 x64 드라이버 미완성(D3D12-X64-WOA-UMD-ABI.md). LoL은 이와 별개로 Vanguard에 막힘 |
| Tahoe판 GPU 펌웨어를 직접 분석해 처음부터 작성 | 스텁 없이 지금 부팅 유지 | 12~24 이상 | Asahi 방식의 추적 분석을 새 펌웨어판에 다시 해야 함 |

모델 배분: G-3 이후(하이퍼바이저 메모리 중개, 인터럽트, 커널 드라이버)는 프로젝트 규칙상 시스템 핵심부 변경이므로 Opus를 씁니다. 콘치님 지시(2026-11-15)에 따라 이 갈래의 모든 작업은 Opus로 진행합니다.

---

## 4. 위험과 모르는 점

| 구분 | 내용 | 영향 | 상태 |
|---|---|---|---|
| 모름 | Tahoe 시스템 펌웨어 위에서 13.5 OS 펌웨어 스텁이 부팅되는가 | 안 되면 공개 코드 재사용 길이 통째로 막히고 처음부터 작성 길(12~24개월 이상)만 남음 | 근거 없음, G-1에서 확인 |
| 모름 | paulsmir 보조 모드도 13.5 스텁 부팅 항목에서 시작하는가 | 우리 맥북 연결 모드를 그대로 쓸 수 있는지에 영향 | 문서에 없음 |
| 모름 | paulsmir README 성능 숫자가 맞는가 | 기대치 설정 | 작성자 주장. LIMITATIONS.md:18과 서로 어긋남. 아무도 따로 검증하지 않음 |
| 위험 | J274와 J313의 차이(장치 정보, HDMI, 전원 구성) | 이식 공수 증가 | J313 기준값이 정확히 고정되어 있어 재작성 필요(사실) |
| 위험 | GPU 펌웨어를 다시 켤 때 전원 완전 재부팅이 필요 | 개발 반복 속도가 느려짐, 시간 초과 복구 어려움 | paulsmir 기록(AGX_BRINGUP.md:43-46) |
| 위험 | 게스트가 GPU 관련 레지스터에 잘못 쓰면 하이퍼바이저에서 SError | 맥이 멈춤. 지금 8단계 문제와 같은 종류 | 가설 |
| 위험 | 성능 손실(주소 변환 캐시 실패) | macOS 대비 약 68~69%(작성자 주장) 수준에 머무름 | paulsmir도 미해결(2026-11-07 커밋) |
| 위험 | LoL은 Vanguard 때문에 GPU와 무관하게 막힘 | "게임" 목표 중 LoL은 당분간 달성 불가 | 사실(ARM 미지원·가상화 거부) + 가설(NWOAS가 걸림) |
| 모름 | Vanguard가 테스트 서명 모드를 문제 삼는가 | LoL 차단 요인 추가 | 조사 안 함 |
| 모름 | 하이퍼바이저를 숨기는 것이 Riot 약관 위반인가 | 법적·계정 위험 | 조사 안 함, 권하지 않음 |
| 모름 | 13.5 스텁이 화면(DCP) 문제도 풀어 주는가 | 듀얼부팅 갈래와 화면 갈래 공수 | 가설, 실기기 확인 필요 |

**하드웨어에서 확인한 결과는 하나도 없습니다.** 위 단계 중 G-0a·G-0b를 뺀 나머지는 모두 콘치님 실기기에서만 검증할 수 있습니다. 빌드가 되더라도 그것은 부팅이나 동작 성공이 아닙니다.

---

## 5. 출처

### 우리 프로젝트 파일
- /Users/angigyeom/Desktop/DEV/NWOAS/NWOAS-STATUS-2026-11-15.md:41
- /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/layout-s231/STANDALONE-CENSUS-20261115.md:21, 43, 149, 324, 347-357
- /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/afk.c:323-328
- /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/firmware.c:23-43, 137
- /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/kboot_gpu.c:673-691
- /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/proxyclient/m1n1/constructutils.py:800-804
- /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/proxyclient/m1n1/fw/agx/

### paulsmir/windows-on-m1 (https://github.com/paulsmir/windows-on-m1)
- README.md:20, 23-35, 73-90, 97-104, 113, 118, 123
- documentation/LIMITATIONS.md:9-10, 14-16, 18
- AGX_BRINGUP.md:13-27, 29-46, 122-133, 150-151
- investigation/analysis/D3D12-X64-WOA-UMD-ABI.md (2026-10-20)
- 브랜치 `integration/ad04-windows-compiler` 커밋 기록(2026-11-07)

### Asahi Linux·Mesa
- AsahiLinux/linux `asahi` 브랜치 drivers/gpu/drm/asahi/driver.rs:1, 182-205
- https://asahilinux.org/docs/hw/soc/agx/
- https://asahilinux.org/docs/platform/open-os-interop/
- https://asahilinux.org/docs/platform/feature-support/m1/
- https://asahilinux.org/2024/06/vk13-on-the-m1-in-1-month/
- https://rosenzweig.io/blog/vulkan-14-sur-asahi-linux.html
- https://rosenzweig.io/blog/asahi-gpu-part-3.html
- https://rust-for-linux.com/apple-agx-gpu-driver
- https://docs.mesa3d.org/drivers/asahi.html
- https://appleinsider.com/articles/24/02/14/asahi-linux-conformant-to-opengl-46-opengl-es-32-on-apple-silicon-macs
- https://linuxiac.com/asahi-linux-gets-closer-to-full-m3-support-on-apple-silicon-macs/

### Windows on ARM
- https://learn.microsoft.com/windows/arm/apps-on-arm-x86-emulation
- https://chipsandcheese.com/p/state-of-windows-on-arm64-a-high-level-perspective
- https://docs.nvidia.com/rtx-spark/rtx-spark-porting-guide/0.1.0/porting/vk-gl.html

### 리그 오브 레전드·Vanguard
- https://isitonarm.com/app/league-of-legends/
- https://www.tomshardware.com/pc-components/cpus/nvidia-says-rtx-spark-chip-will-support-all-major-anti-cheat-and-drm-technologies-fortnite-valorant-denuvo-and-more-to-work-natively-with-windows-on-arm
- https://pcgamer.com/hardware/gaming-laptops/nvidias-working-with-all-the-anti-cheat-vendors-to-make-competive-games-work-on-rtx-spark-and-woa-just-when-gaming-on-linux-was-looking-good
- https://support.riotgames.com/en-us/league-of-legends/performance/minimum-and-recommended-system-requirements-league-of-legends
- https://www.pcgamer.com/games/moba/we-have-not-confirmed-any-instance-of-vanguard-bricking-anyones-hardware-following-its-league-of-legends-rollout-riot-says-but-there-are-definitely-problems-for-some-players
- https://support.riotgames.com/en-us/riot/performance/error-van-138
- https://support.riotgames.com/en-us/riot/client/error-van-9100
