# 부팅 과정과 "맥북 없이 혼자 부팅" 옮기기 — paulsmir/windows-on-m1(J313) → 우리 Mac mini(J274)

작성일: 2026-11-15. 모든 조사는 읽기만 했습니다. `external/windows-on-m1`에서는 아무것도 빌드·실행하지 않았고, `/dev/cu.*`는 열지 않았으며, 돌고 있는 게스트(Mac mini의 Windows)는 건드리지 않았습니다.

표기: **[사실]** = 파일과 줄 번호로 확인한 것. **[가설]** = 코드를 읽고 추론한 것(실기기에서 돌려 보지 않음). **[저자 주장]** = 그쪽 문서에만 있고 기록된 로그로 뒷받침되지 않은 것.
경로 앞머리: `E/` = `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1`, `N/` = `/Users/angigyeom/Desktop/DEV/NWOAS`.

---

## 1. 결론

1. **그쪽의 "혼자 부팅" 방식은 우리가 옮기려는 것과 같은 일을 이미 C로 해 두었습니다.** 지금 우리는 맥북의 Python이 m1n1에 하나하나 명령을 보내 게스트를 띄웁니다. 그쪽은 이 일을 m1n1 안의 C 코드 약 1,000줄(`E/m1n1_windows/src/hv_autonomous_*.c`, `hv_bootstrap*.c`, `hv_launch_*.c`)로 옮겨, 연결된 컴퓨터 없이 전원만 켜면 Windows까지 가게 했습니다. **[사실]** 우리 `standalone.md`의 작업을 할 때 뼈대로 쓸 만합니다.
2. **그쪽 부팅 파일을 우리 Mac mini에 그대로 쓸 수는 없습니다.** 메모리 배치, 기준값 묶음, 화면 코드, GPU 코드 네 곳이 J313(맥북 에어)에 묶여 있습니다. **[사실]**
3. **그쪽은 Tahoe(macOS 26)를 시험한 적이 없습니다.** 저장소 어디에도 "tahoe"·"mBoot-18000"이 없고, 펌웨어 표는 15.0에서 끝납니다. **[사실]**
4. **콘치님이 정하셔야 할 충돌이 하나 있습니다.** 그쪽 방식은 Asahi가 깔아 주는 13.5 버전 OS 펌웨어 시작 항목(아래 "스텁")에 기대고 있습니다. 그런데 콘치님은 9월 6일에 "Tahoe OS·시스템 펌웨어를 유지하고 13.5 스텁은 쓰지 않는다"고 지시하셨습니다(`N/NWOAS-STATUS-2026-09-06.md:173`). **[사실]**
   - 다만 13.5를 고르더라도 그쪽의 실제 화면 넘기기 코드는 우리에게 쓰이지 않습니다. 그 코드는 내장 패널만 받고, 우리 Mac mini는 외부 모니터(HDMI)이기 때문입니다(아래 3장). 13.5가 되살려 주는 것은 m1n1 자신의 HDMI 화면뿐입니다. **[사실]**
5. **옮기는 방향:** 그쪽 뼈대(묶음 형식, 단계별 실행, 단계마다 점검)를 우리 `m1n1-layout-s231` 쪽으로 가져옵니다. 반대로 그쪽 트리에 우리 것을 넣지 않습니다. 우리 트리에는 J274 장치 처리(NVMe 빠른 경로, 전원 관리 차단, Tahoe 예외 처리)가 들어 있기 때문입니다. **이것은 계획일 뿐 아무것도 만들지 않았고, 빌드가 되더라도 부팅이 된다는 뜻은 아닙니다.**

---

## 2. 그쪽(J313)에서 된 것

### 2.1 부팅 순서 [사실]

| 순서 | 무엇 | 출처 |
|---|---|---|
| 1 | iBoot(애플 부트로더) | – |
| 2 | Asahi가 만든 시작 항목(스텁) → Asahi 1단계 m1n1. 복구 모드(1TR)에서 "Finish Installation"으로 설치 | `E/documentation/INSTALL.md:179-185` |
| 3 | 그쪽 `boot.bin`. Asahi ESP의 `m1n1/boot.bin` 한 파일만 바꿉니다 | `E/documentation/INSTALL.md:179-185`, `E/scripts/install-esp.sh` |
| 4 | `boot.bin` 안의 앞 단계 m1n1(Stage 0)이 뒤 단계 m1n1(Stage 1)을 새로 띄움 | `E/m1n1_windows/src/main.c:150-164`, `hv_stage_role.c:16-24` |
| 5 | Stage 1이 하이퍼바이저(게스트를 감싸 실행하는 층)를 준비하고 Mu(UEFI 펌웨어)로 들어감 | `hv_autonomous_runtime.c:347-492` |
| 6 | Mu의 `WindowsAutoBootDxe`가 내장 디스크에서 `\EFI\BOOT\BOOTAA64.EFI`를 찾아 바로 실행(BootOrder 무시) | `E/documentation/ARCHITECTURE.md:93-98` |

### 2.2 `boot.bin` 묶음 형식 [사실]

| 층 | 내용 | 출처 |
|---|---|---|
| 바깥 | `m1n1-stage0.bin` + "ASIBOOT0" 머리말 + xz 압축(안쪽 묶음) | `E/bootstrap_image.py:34-69` |
| 안쪽 | `m1n1-stage1.bin` + 16 KiB(0x4000) 경계에 놓인 64바이트 "ASIWINGU" 설명표(플래그, 배치 버전, 크기, CRC32 검사값) + xz 압축(Mu 펌웨어 파일) | `E/standalone_image.py:41-84` |
| 플래그 | 화면(없음/실제/가상/둘 다), 디버그(끔/UART/전체/관찰) | `E/launch_profile.py:28-40` |
| 빌드 | m1n1을 세 번 컴파일(`-DM1N1_STAGE0`, `-DM1N1_STAGE1`, 맥북 연결용 일반판). Mu는 `Platform/MacBookAirMid2020Pkg` | `E/scripts/build-standalone.sh:212-229` |

| 크기 | 값 (바이트) | 값 (MiB, 대략) | 출처 |
|---|---|---|---|
| Mu 압축 전 | 0x1d88000 | 29.5 | `E/documentation/DEVELOPMENT_HISTORY.md:289-299` |
| Mu 압축 후 | 0x102424 | 1.0 | 같은 곳 |

### 2.3 왜 m1n1을 두 단계로 나눴나 [사실 — 그쪽 기록 로그 있음]

iBoot가 처음 들어가는 m1n1에서 바로 하이퍼바이저를 돌리면 두 번째 CPU(CPU1)가 망가졌고, m1n1을 한 번 더 새로 띄운 깨끗한 상태에서 돌리니 고쳐졌다고 적혀 있습니다(`DEVELOPMENT_HISTORY.md:256-260`, 로그 `:278-301`).

### 2.4 Stage 1이 하는 일 (순서대로) [사실]

| 단계 | 하는 일 | 출처 |
|---|---|---|
| 시작 | 조용한 설정(실제 화면, 디버그 끔)이면 USB는 전원만 켜고 연결 컴퓨터를 기다리지 않음. 디버그 설정이면 3초 동안 기다림 | `hv_autonomous_boot_runtime.c:111-126`, `hv_autonomous_profile.c:47-56` |
| VALIDATE | 메모리 크기를 설정값과 실제 중 작은 쪽으로 맞춤. J313 기준값 묶음을 읽고 첫 점검 실행 | `hv_autonomous_runtime.c:347-492` |
| PLATFORM | CPU 클럭 초기화 | 같은 곳 |
| DECOMPRESS | Mu 압축 풀기, CRC 검사 | 같은 곳 |
| BOOT_DATA | 기기 정보표(ADT)·TrustCache·SEPFW·preoslog 복사. `/chosen/memory-map` 항목만 고침. 부팅 인자 작성(화면 2560x1600 고정). 보조 CPU 시작 주소 기록. 남은 "ASIRAMDK" 머리말 지움 | `:204-310` |
| STAGE2 | 하이퍼바이저 초기화, 게스트 CPU 레지스터 설정(우리가 Python으로 하는 일, `hv.c:312-330`), 메모리 매핑, 낮은 주소 별칭(0x100000 → 0x8a0100000, 1 GiB), `/arm-io` 장치 범위 그대로 통과, GPU 자원 | `:312-329` |
| PCI_NVME | `hv_pci_init(ecam, bar, vintid)` | 같은 곳 |
| XHCI | USB 컨트롤러·DART(주소 변환기) 연결 다시 설정 | 같은 곳 |
| VUART | `/arm-io/uart0`에서 가상 시리얼 포트 만들기, 다음 점검 | 같은 곳 |
| READY | 화면 넘길 준비, 다음 점검. 화면 준비가 1을 돌려주지 않으면 여기서 실패 | `:464-470` |
| ENTERED | 마지막 점검, SPRR/GXF(애플 전용 권한 설정) 지정, `hv_start(firmware_base, boot_args)` | `:129-142` |

- CPU 켜기: 맥북 연결 방식에서는 Python이 가로채던 전원 관리 페이지를, 혼자 부팅에서는 "C 처리 또는 안전한 그대로 통과"로 한다고 주석에 있습니다(`hv_launch_j313.c:66-70`). PSCI(CPU 켜기 요청)는 CPUSTART 레지스터에 직접 씁니다(`hv_psci.c:1164-1169`). **[사실]** 어떤 전원 관리 처리를 넣었는지 목록은 읽지 않았습니다.
- 연결 컴퓨터가 없을 때 예외(원래 맥북으로 넘어가던 일)를 C가 어떻게 처리하는지: **읽지 않았습니다.**

### 2.5 그쪽 저자가 말한 것 [저자 주장]

| 주장 | 출처 | 비고 |
|---|---|---|
| 혼자 부팅으로 설치된 Windows가 처음부터 떴다. 약 20초 멈춤이 남아 있다 | `E/documentation/RUN.md:35-39`, `LIMITATIONS.md:5-9` | 직접 확인 못 함 |
| GPU 드라이버는 아직 맥북 연결 방식이 필요하다 | `E/README.md` 표 | 직접 확인 못 함 |

- 그쪽 문서는 여러 곳이 낡았습니다. **[사실]** `ARCHITECTURE.md:12-13`은 "저장장치 드라이버가 필요 없다"고 하지만 README에는 AppleAns 드라이버가 있고, `LIMITATIONS.md:18`은 "가속 GPU 드라이버가 없다"고 하지만 `README.md:23`과 다릅니다. 그래서 그쪽 설명 글보다 기록된 로그를 믿어야 합니다.

---

## 3. 우리(J274)와 다른 점

### 3.1 J313에 묶인 부분 [사실]

| 항목 | 위치 | J274에서는 |
|---|---|---|
| 메모리·장치 배치 | `E/config/j313-guest-layout.json`, 생성 헤더 `J313_AUTONOMOUS_LAYOUT` | 시작 0x850000000, 끝 0xa00000000(8 GB 기기 기준), 화면 2560x1600, ECAM 0x690000000, BAR 창 0x400000000, xhci 0x502280000, CPU 8개 필수(`guest_layout.py:46-47`). 전부 다시 정해야 함 |
| 기준값 묶음 | `hv_launch_golden_j313.c:53-112` | 대상 "J313", 메모리 0x18f708000, ADT 크기 0x5941c+요약값, 인터럽트 경로 857, GPU 경로, 장치(DART 0x502f00000, vUART 0x235200000). 메모리는 정확히 같아야 하고 인터럽트 경로는 집합으로 비교(`hv_launch_j313.c:71-104`) |
| CPU 레지스터 기준 | `hv_launch_j313.c:78-96`, 값 `hv_launch_golden_j313.c:35-42` | HACR·MDCR·MDSCR·AMX·ACTLR을 J313 전용 마스크로, VM 키는 정확히 비교. J274 값이 없음 |
| 화면 | `dcp.c:600-604`, `display.c:837-840` | J313AP + OS 펌웨어 13.5 + `/arm-io/disp0`가 외부가 아닐 것을 요구. 외부 패널은 거부. J274는 외부 모니터라 **펌웨어를 무엇으로 하든 쓸 수 없음** |
| GPU | `hv_agx_retained_platform.c:454`, `hv.c:200-203` | board_id 0x26만 받음. GPU 인터럽트 경로 등록이 실패하면 하이퍼바이저 초기화 전체가 실패 |
| 키보드·트랙패드(SPI-HID) | `hv_autonomous_runtime.c:384-387` | 선택 사항. `/arm-io/spi3/ipd`가 없으면 건너뜀 |
| ACPI 표 | `E/mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl:124-251` | J313 주소들. 그쪽 `MacMini2020Pkg`는 281줄에 MCFG 없음(그대로 둔 원본인지는 확인 못 함). 우리 것은 567줄에 MCFG·IORT 있음 |

- J274가 외부 모니터라는 근거: 우리 7월 로그에 "Display is external", "External display found"(`N/nwoas_scripts/logs/appark-chainload-1700.log:117-118`). **[사실]**
- 저자도 "보드 이름만 바꿔서는 다른 M1 기종이 동작하지 않을 것"이라고 적었습니다(`LIMITATIONS.md:16-18`). **[저자 주장]**

### 3.2 그쪽 파일을 J274에 넣으면 [가설]

| 단계 | 예상 | 근거 |
|---|---|---|
| 첫 점검 | 실패. 16 GB에서는 메모리 끝이 0xa00000000으로 잘려 크기 0x1b0000000이 되고, 기준값 0x18f708000과 다름 | `hv_autonomous_memory.c:5-23`, `hv_launch_j313.c:107-121` |
| READY | 실제 화면 설정이면 여기서도 실패 | `hv_autonomous_runtime.c:464-470`, `display.c:837-840` |
| 실패 후 | m1n1 대기 상태(proxy)로 떨어짐 | `main.c:150-164` |

- **주의 — 이것은 설계된 안전장치가 아닙니다.** 그쪽 파일에는 기종 검사가 없습니다. "대상 이름이 정확히 같아야 한다"는 점검은 대상 이름을 기준값 묶음에서 그대로 복사해 오므로 늘 통과합니다(`hv_autonomous_runtime.c:52`). J274에서 멈추는 것은 우연히 메모리 크기가 달라서일 뿐입니다. 옮길 때는 ADT의 compatible이나 board_id로 **진짜 기종 검사를 넣어야 합니다.** **[사실 — 코드 근거]** 실제로 멈추는지는 **[가설]**(돌려 보지 않음).

### 3.3 펌웨어 버전 [사실]

| 항목 | 그쪽(J313) | 우리(J274) | 출처 |
|---|---|---|---|
| OS 펌웨어(스텁) | 13.5 (iBoot-8422.141.2). 다른 버전은 도구가 거부 | Tahoe 26.x | `E/tools/generate_agx_hwdata_profile.py:125`, `E/documentation/plans/2026-08-26-j313-agx-g2-firmware-lifecycle-core.md:15` |
| 시스템 펌웨어 | Sequoia(macOS 15.7.8) | Tahoe (mBoot-18000.121.3) | `E/investigation/GPU_CURRENT_STATE.md:104`, `E/investigation/EXPERIMENTS.md:31146, 31195` |
| m1n1 펌웨어 표 | 15.0(iBoot-11881.1.1)에서 끝. Tahoe는 "모름"으로 처리됨 | Tahoe 문자열을 따로 처리 | `E/m1n1_windows/src/firmware.c:22-44`, `N/m1n1-layout-s231/src/main.c:169`, `afk.c:326` |

- `E/README.md:30`은 "macOS 15"라고만 적었고, "15.7.8"은 `GPU_CURRENT_STATE.md:104`에 있습니다.
- **우리 쪽 새 근거:** 7월 로그에서 J274가 OS 펌웨어 13.5, 시스템 펌웨어 "unknown (mBoot-18000.121.3)" 상태로 m1n1 HDMI 화면을 1920x1080으로 켰습니다(`appark-chainload-1700.log:47-48, 86-87, 117-134`). 즉 Tahoe 시스템 펌웨어 위의 13.5 스텁이 이 Mac mini에서 m1n1 화면까지는 됐습니다. 다만 **Windows 화면이 아니라 m1n1 자신의 화면**입니다. 이 방식은 콘치님 지시로 접었습니다(`NWOAS-STATUS-2026-09-06.md:152-155, 173`). **[사실]**

---

## 4. 옮기는 순서 (담당·검증 방법)

모두 제안이며 아무것도 하지 않았습니다. "빌드 통과"는 부팅 성공이 아닙니다. 실제 부팅 확인은 콘치님이 Mac mini에서만 할 수 있습니다.

| 순서 | 할 일 | 담당 | 검증 방법 |
|---|---|---|---|
| 0 | **스텁 OS 펌웨어 결정.** 13.5(그쪽 방식, 7월 선례, 9/6 지시와 충돌) 또는 26.x(`dualboot.md` A안). 어느 쪽이든 그쪽 화면 넘기기 코드는 못 씀. 13.5는 m1n1 HDMI 화면만 되살림 | 콘치님 | 결정 기록 |
| 1 | **J274 배치 파일 작성.** 메모리 시작(m1n1 작업 메모리 위에 고정), 메모리 끝, 낮은 주소 별칭, 화면 크기(지금 HDMI 1280x720), 장치 창(`standalone.md` 24행의 통과 범위 0x6c0000000·0x681000000·0x502280000 — 이것이 ECAM/BAR인지는 따로 확인 필요), xhci 0x502280000. `guest_layout.py`의 J313 이름·CPU 8개 고정을 고쳐 C 헤더와 Mu 설정값을 생성 | 구현(Sonnet) | 생성된 헤더·설정값을 지금 맥북 Python 실행 때의 값과 한 줄씩 대조 |
| 1a | **게스트 메모리 크기 결정.** `standalone.md` 25행(`:98`)은 "메모리 크기 −4 GiB", 제안 초안은 16 GB 끝까지. 둘 중 하나로 맞춰야 함 | 콘치님 + 설계(Opus) | 결정 기록, 지금 돌고 있는 방식의 값과 비교 |
| 2 | **Mu.** 우리 `MacMini2020Pkg`(567줄 DSDT, MCFG/IORT)를 배치에 맞는 고정 설정값으로 빌드. 펌웨어 시작 주소 0x830000000(`N/apple_silicon_platforms_mu/Platform/MacMini2020Pkg/MacMini2020.fdf:30`). 우리 트리에 없는 `WindowsAutoBootDxe` 옮기기 | 구현(Sonnet) | 빌드 통과 + 설정값 덤프 대조(빌드 수준까지만) |
| 3 | **m1n1에 뼈대 옮기기**(`N/m1n1-layout-s231`로). 설명표 읽기, 부팅 대기, 단계별 실행·점검, Stage 0/1 분리(우리 `standalone.md` 6단계 main.c 순서 변경과 맞물림) | 설계(Opus — 부팅 체인·하이퍼바이저 핵심부 변경이라 Sonnet으로 부족) | 빌드 통과, `file`로 형식 확인, 정적 검토 |
| 3a | **GPU 빼기.** 호출부만 지우면 안 됨. `hv.c:200-203`에서 GPU 경로 등록 실패가 초기화 실패로 이어지므로 `hv_init` 안에서 빼야 함. SPI-HID 호출도 뺌 | 구현(Opus) | 코드 검토 + 빌드 |
| 3b | **기준값 묶음 교체.** S231 맥북 연결 실행에서 J274 값을 받아 만듦: 인터럽트 경로, 장치 범위, **CPU 레지스터(HACR·MDCR·MDSCR·AMX·ACTLR·VM 키)와 마스크** | 구현(Sonnet) + 콘치님 실기기 실행 | 받아 둔 값과 묶음 값 대조 |
| 3c | **진짜 기종 검사 넣기**(ADT compatible 또는 board_id) | 구현(Sonnet) | J313 값을 넣었을 때 거부되는지 오프라인 확인 |
| 3d | 화면 설정은 "없음"으로 시작(외부 패널 거부를 피함). 화면은 나중 단계 | 구현(Sonnet) | 설명표 플래그 확인 |
| 4 | **묶기.** stage0 + ASIBOOT0 + xz(stage1 + ASIWINGU + xz(Mu)). 스텁의 1단계가 불러오는 ESP의 `m1n1/boot.bin`에 놓기. 그쪽 `install-esp.sh` 방식(살펴보기, 처음 한 번 백업, 한 번에 바꿔 쓰기, SHA-256 확인, 되돌리기)을 따름 | 구현(Sonnet) + 콘치님(ESP 쓰기 승인) | 오프라인으로 묶음 다시 풀어 크기·CRC 확인 |
| 5 | **실기기 시험.** 관찰 설정으로 켜고 시리얼 로그를 읽기만 하는 기록기로 받아 "PREFLIGHT PASS checkpoint=..." 줄 확인 → 콘치님이 바탕화면을 눈으로 확인. 그 전까지의 모든 결과는 빌드 수준 | 콘치님 | 로그의 점검 통과 줄, 화면 육안 확인 |

- 5번 주의: 관찰 설정은 3초 대기는 있지만 m1n1 대기 상태로 붙잡는 것은 꺼져 있습니다(`hv_autonomous_boot_runtime.c:142-145`, 붙잡기 여부는 `accept_proxy`, `hv_autonomous_profile.c:39-41`). 그래서 이 설정으로는 중간에 멈춰 세울 수 없습니다. 멈춰 세워야 하면 붙잡기가 켜진 디버그 설정을 써야 합니다. **[사실]**

### 4.1 우리 `standalone.md` 표와의 대응

| `standalone.md` 행 | 그쪽 C의 해당 부분 | 판단 |
|---|---|---|
| 2 (다시 띄우기) | Stage 0이 Stage 1을 새로 띄움 | 바꿈 |
| 3 (시작 주소가 매번 다름, 작업 메모리+896 MiB) | 고정 배치 | 바꿈. "시작 주소 기준으로 비교" 문제(§2.3/§3.2)도 없어짐 **[사실]** |
| 6, 10-12 (HACR, MDCR, MDSCR, AMX, VM 키, ACTLR) | `hv_prepare_guest_cpu_state`. J313 값(hacr 0x317000000014000, mdcr 0xf00, mdscr 0x8000, AMX 62번 비트, actlr 0x1c00) | 재사용. 11-12행은 "모름"에서 "J313에서는 필요"로(저자 근거). J274 값은 따로 받아야 함 |
| 8 | `map_arm_io_ranges` | 재사용 |
| 13 | `map_vuart_from_adt` | 재사용 |
| 15-18, 20-21 | `prepare_boot_data` | 재사용 |
| 16 | `map_stage2` | 재사용 |
| 17 (32 MiB 묶음) | Mu를 xz로 압축(29.5 MiB → 1.0 MiB) | `standalone.md` §4.1과 `dualboot.md` §4.2의 "32 MiB/33.6 MB를 어디에 둘지" 문제에 답이 됨 |
| 14, 19 (USB0 기기표 지우기, `__OS_LOG` 지우기) | 그쪽은 memory-map만 고침 | **[가설]** 빼도 될지 모름. J274에서 확인 안 됨 |
| 25-27 (NVMe) | `hv_pci_init` | 우리 NVMe 빠른 경로는 우리 것을 유지 |
| 28/28a | 저자 주장만 있음 | 확인 안 됨 |

- `standalone.md` §3.3(연결 컴퓨터 없을 때 예외 처리): 읽은 범위에서 답이 없어 그대로 남깁니다.
- `standalone.md` §3.4 6단계: 그쪽의 부팅 대기 + 설정 플래그로 켜고 끄는 3초 창이 그대로 쓸 만한 설계입니다. 조용한 설정은 창을 아예 건너뛰므로 완전히 혼자 부팅됩니다. **[사실]**

### 4.2 우리 `dualboot.md`와의 대응

| `dualboot.md` 부분 | 내용 |
|---|---|
| A단계(스텁, 1TR, ESP에 boot.bin) | 그쪽 INSTALL.md A·B 단계와 같음. 그쪽이 쓰는 Asahi 정식 "UEFI만" 경로는 빈 공간과 새 파티션이 필요한데 우리 GPT 보호 장치가 거부(`dualboot.md` §2.4). 우리 수정 스텁 계획 유지 |
| C단계(UEFI를 2단계 파일에 넣기) | 그쪽 이중 xz 묶음 형식으로 대신할 수 있음 |
| §3.4 | 7월 "Tahoe 시스템 펌웨어 위 13.5" 로그를 B안 근거로 추가. 단 콘치님 지시가 막고 있음을 함께 적음. 그 로그는 m1n1 화면이지 Windows 화면이 아님 |
| 4단계 | 그쪽 `install-esp.sh` 방식(백업 한 번, 한 번에 바꿔 쓰기, SHA-256 확인, 되돌리기)이 맞음(`INSTALL.md:183-185`) |

---

## 5. 위험과 모르는 점

| 항목 | 상태 | 영향 |
|---|---|---|
| 13.5 스텁 vs Tahoe 유지 | 콘치님 결정 필요(9/6 지시와 충돌) | 13.5라도 Windows 화면 넘기기는 안 됨. m1n1 HDMI 화면만 |
| 게스트 메모리 크기(−4 GiB vs 16 GB 끝까지) | 결정 필요 | 배치 파일·기준값 묶음 전부에 영향 |
| 기종 검사 없음 | 그쪽 코드에 없음 **[사실]** | 넣지 않으면 잘못된 기종 파일이 우연에 기대어 멈춤 |
| GPU가 초기화 안에 묶임 | `hv.c:200-203` **[사실]** | 호출부만 빼면 하이퍼바이저 초기화가 실패. 코드 수정 확정 |
| J274 CPU 레지스터 값 | 없음 | S231 실행에서 받아야 점검을 통과시킬 수 있음 |
| 연결 컴퓨터 없을 때 예외 처리(`hv_exc`) | 읽지 않음 | 맥북으로 넘기던 예외가 혼자 부팅에서 어떻게 되는지 모름 |
| 전원 관리 처리 목록 | 읽지 않음 | 우리 전원 관리 차단과 맞는지 모름 |
| 그쪽 GPU 경로 등록이 J274에서 실패하는지 | 확인 안 함 | 어차피 빼야 함 |
| USB-A에서 960 KiB가 전부 0으로 읽히는 문제(`usb.md`) | 이 문서 범위 밖 | 혼자 부팅과 별개로 남음 |
| 그쪽 혼자 부팅 성공, 20초 멈춤 | **[저자 주장]** | 우리에게는 참고만 |
| 그쪽 문서가 낡음(여러 곳) | **[사실]** | 설명 글 말고 기록된 로그를 근거로 삼아야 함 |
| 그쪽 `MacMini2020Pkg`가 원본 그대로인지 | 확인 못 함 | 줄 수 281·MCFG 없음만 확인 |

---

## 6. 출처

그쪽(읽기만 함, `E/` = `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1`)
- `E/documentation/INSTALL.md:179-185, 183-185`
- `E/documentation/DEVELOPMENT_HISTORY.md:256-260, 278-301, 289-299`
- `E/documentation/ARCHITECTURE.md:12-13, 93-98`
- `E/documentation/RUN.md:35-39`, `E/documentation/LIMITATIONS.md:5-9, 16-18, 18`
- `E/README.md:23, 30`, `E/investigation/GPU_CURRENT_STATE.md:104`, `E/investigation/EXPERIMENTS.md:31146, 31195`
- `E/documentation/plans/2026-08-26-j313-agx-g2-firmware-lifecycle-core.md:15`
- `E/bootstrap_image.py:34-69`, `E/standalone_image.py:41-84`, `E/launch_profile.py:28-40`, `E/guest_layout.py:46-47, 200-216`
- `E/scripts/build-standalone.sh:212-229`, `E/scripts/install-esp.sh`, `E/tools/generate_agx_hwdata_profile.py:125`
- `E/config/j313-guest-layout.json`
- `E/m1n1_windows/src/main.c:150-164`, `hv_stage_role.c:16-24`, `hv_bootstrap.c:5-38, 123-160`
- `E/m1n1_windows/src/hv_autonomous_boot_runtime.c:111-126, 139-151, 142-145`, `hv_autonomous_profile.c:37-45, 39-41, 47-56`
- `E/m1n1_windows/src/hv_autonomous_runtime.c:52, 129-142, 204-310, 312-329, 347-492, 384-387, 464-470`, `hv_autonomous_memory.c:5-23`
- `E/m1n1_windows/src/hv_launch_j313.c:66-70, 71-104, 78-96, 107-121`, `hv_launch_golden_j313.c:35-42, 53-112`
- `E/m1n1_windows/src/hv.c:200-203, 312-330`, `hv_psci.c:1164-1169`, `hv_agx_retained_platform.c:454`
- `E/m1n1_windows/src/dcp.c:600-604`, `display.c:837-840`, `firmware.c:22-44`
- `E/mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl:124-251`, `E/mu/Platform/MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc`

우리(`N/` = `/Users/angigyeom/Desktop/DEV/NWOAS`)
- `N/NWOAS-STATUS-2026-09-06.md:152-155, 173`
- `N/nwoas_scripts/logs/appark-chainload-1700.log:47-48, 86-87, 117-118, 117-134`
- `N/m1n1-layout-s231/src/main.c:169`, `N/m1n1-layout-s231/src/afk.c:326`
- `N/apple_silicon_platforms_mu/Platform/MacMini2020Pkg/MacMini2020.fdf:30`
- `N/nwoas_scripts/plan-20261115/standalone.md:97-98`(24·25행), `dualboot.md` §2.4·§3.4·§4.2, `usb.md`
