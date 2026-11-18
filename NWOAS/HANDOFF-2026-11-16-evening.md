# NWOAS 인수인계 — 2026-11-16 오후~저녁 (S246 ~ S256)

> 이 문서 하나로 다음 세션이 바로 이어갈 수 있게 썼습니다. 오전까지의 기록(13.5 스텁, m1n1 재등록 등)은 `HANDOFF-2026-11-16.md`에 있습니다.
> 표기: **[사실]** = 실기기 로그·파일·콘치님 눈으로 확인. **[가설]** = 확인하지 않은 추측. "콘치님 확인" = 콘치님이 모니터를 보거나 소리를 듣고 알려 준 것.

---

## 0. 한눈에 — 지금 무엇이 되고, 무엇이 막혀 있나

| 기능 | 상태 | 마지막 확인 |
|---|---|---|
| Windows 11 ARM 부팅(13.5 스텁 → D4 m1n1 → 하이퍼바이저 → 게스트 m1n1 + Mu → Windows) | 됨 | 20:29 (S256 펌웨어) |
| Apple GPU 드라이버(KMD + Mesa UMD)로 바탕화면 그리기 | 됨. 작업 관리자에 GPU 표시, "꽤 부드러움" | 콘치님 확인 |
| 화면 출력을 하이퍼바이저가 DCP로 직접(복사 없이) | 됨 (g252J 이후) | 콘치님 확인 |
| 모니터 끄기(신호 없음)·켜기 | 됨 | 콘치님 확인 |
| 해상도 8개 고르기(1920x1080 이하, 늘려서 표시) | 됨 | 콘치님 확인 (1280x720, 1024x768) |
| 모니터 전원·HDMI 다시 꽂으면 화면 자동 복구 | 됨 (g254L) | 콘치님 확인 2번 |
| 내장 스피커 소리 — 하이퍼바이저 전 단계에서 비프음 | 됨 (S255) | 콘치님 3번 모두 들음 |
| 내장 스피커 — Windows가 장치를 봄 | 됨, 드라이버 없음(문제 코드 28) | 20:30 |
| 내장 스피커 — Windows 드라이버 | **막힘**: 에이전트가 Anthropic 안전장치에 걸려 중단 | 20:3x |
| 와이파이 | **막힘**: 같은 안전장치 | 17:4x |
| 절전 연속 두 번째에 GPU 펌웨어 Timeout | 한 번 일어났고 이후 재현 안 됨, 원인 미확정 | 16:xx |

**막힌 두 가지(소리 드라이버, 와이파이)는 Cyber Verification Program(CVP) 승인을 기다리는 중입니다.** 콘치님이 개인 보안 연구자로 신청함. 20:3x 기준 상태는 "인증 통과(11월 16일) → Defense Access 결정 검토 중".
- **안전장치에 막힌 일은 다시 시키지도, 메인 세션이 대신 쓰지도 않습니다.** 안전장치 안내가 그렇게 하라고 되어 있고, 콘치님도 동의했습니다.
- CVP 승인은 승인 메일에 적힌 **조직 ID**에 묶입니다. 승인이 나면 먼저 그 조직 ID가 Claude Code가 로그인한 계정의 조직과 같은지 확인합니다. 같으면 이 세션이나 새 세션에서 작은 일부터 다시 시켜 봅니다.

---

## 1. 지금 켜져 있는 구성과 켜는 법

### 1.1 실행 중(20:28~)

| 항목 | 값 |
|---|---|
| 실행기 | `nwoas_scripts/agxdcp-s256-guest-test.sh` |
| 하이퍼바이저 | `port-j274/gpu/hv/out-hv2/m1n1-g254L-agx-hv2-hpdwatch.bin` (sha256 `61e2e2ef93382ef26c5ae8416e7564bed99c2fff9c2d2d371b58ade6d037fdad`, 2293760 B) |
| 펌웨어 페이로드 | `nwoas_scripts/fw-audio-s256/out/s256/m1n1-payload-s256-audio.bin` (sha256 `3cb9e2df…`, S247 AGX0 + 소리 SSDT `SPK0`) |
| 소리 준비 모듈 | `nwoas_scripts/sound-s256/audio_module.py` (sha256 `356f7c5f…`) |
| Windows GPU 드라이버 | 2757판 (P21), `port-j274/gpu/driver-p15/out/j274-full-20261116-1938-p21` |
| 실행기 출력 | `nwoas_scripts/logs/launcher-agxdcp256-dcp256a-20261116-202802.out` |
| 작업 통로(link) | `nwoas_scripts/logs/agxmed1g-s249-20261116-202802.1sXX3e.link` |
| 하이퍼바이저 진단 출력 | 같은 이름 `.vuart` |

### 1.2 재시작·판 교체 스크립트 (콘치님 규칙: Windows 재시작은 묻지 않고 함)

`.claude/settings.local.json`에 `run-install-*`, `run-reboot-arm*` 허용 규칙이 있습니다. 모두 `nwoas_scripts/`에서 `bash agx-s248/<이름> <옛 실행기 .out> <옛 .link> [태그]`로 부릅니다.

| 스크립트 | 하는 일 |
|---|---|
| `agx-s248/run-reboot-arm-dcp256.sh` | Windows 재시작(W4 작업) → 옛 실행기 끝날 때까지 대기 → USB 장치 대기 → `agxdcp-s256`으로 다시 켬 → D28(G3Armed + GPU 장치 재시작) → 상태 출력 → `ARMED DONE` |
| `run-reboot-arm-dcp254.sh` / `-dcp253.sh` / `-dcp252.sh` | 같은 일, 옛 하이퍼바이저 판 |
| `run-reboot-arm-relaunch1g.sh` | 안정 대체 구성(g249D, 하이퍼바이저 DCP 서비스 없음, 드라이버가 복사 방식 자동 선택)으로 다시 켬. USB 재열거 경쟁을 피하려고 대기를 길게 둔 판 |
| `run-reboot-arm-beep255.sh` | 재시작 중 D4 프록시 단계에서 스피커 비프음 시험 후 `agxdcp-s254`로 켬 |
| `run-install-2757.sh` | 재시작 + 파일 전달(s243 매니페스트) + I19 설치 작업. **실행기는 `agxdcp-s253`로 고쳐 둠** |
| `run-install-2757-norestart.sh` | 맥미니가 이미 프록시 화면일 때 재시작 단계 없이 켜고 설치 |

판을 바꿀 때 **Windows 재시작을 씁니다.** 실행 중 `pnputil /restart-device`는 GPU 정리를 건너뛰어 0xC0000483으로 실패합니다(§3.6).

### 1.3 작업 통로(Windows에 명령 보내기)

- `python3 -I nwoas_scripts/transport-s123/queue_job.py <link> <ascii .cmd>`로 보냅니다.
- 결과는 `<link>/job-N.log`(CP949, `iconv -f CP949 -t UTF-8`)에 남습니다. 끝났는지는 `<link>/events.jsonl`의 `"job": N, "kind": 3`으로 압니다.
- 자주 쓰는 작업:
  - `agx-s248/jobs/D28-rearm.cmd`: G3Armed 넣고 GPU 장치 재시작
  - `D36-crashes.cmd`: 최근 충돌 이벤트, dwm·explorer 시작 시각
  - `D29-read-stats.cmd`: NwSoftStats 등

---

## 2. 구성 요소 판 기록

### 2.1 하이퍼바이저(m1n1-gpu-hv2, `port-j274/gpu/hv/out-hv2/`)

빌드 방법:
- `tools-hv2/buildvar.sh <src> <새 폴더> <태그> NWOAS_AGX=1`. 같은 폴더가 있으면 거절하므로 매번 새 폴더를 씁니다.
- 경고 기준: g251F까지 45개, g252J부터 44개(hv_vgic.c의 width 미사용 경고가 없어짐).
- 호스트 시험: `tools-hv2/hosttest/run.sh nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin <출력>` → `ALL PASS`가 나와야 합니다.

| 판 | sha256 앞 16자 | 바꾼 것 | 실기기 |
|---|---|---|---|
| g247B | 2d70d7f1… | J274 Hwdata | GPU 펌웨어 부팅 |
| g248C | 684cb627… | `backing_find` NULL 보호(풀 창 경로) | **실기기 미검증** |
| g249D | 439ec003… | GPU 로컬 예약 384 MiB → 1 GiB | 안정 대체 구성 |
| g250E | 9693cc6a… | DCP iBoot 비동기 화면 전환, 시험 프록시 명령 0x1180~0x1185 | 패턴 시험 통과 |
| g251F | 4f5c4976… | 하이퍼바이저 화면 서비스(DCP 넘겨받기) | 켜기 실패 |
| g252G | f990dbec… | 켜기·모드 설정 시간 한도 5 s, 실패 원인 출력 | `reply=0` 확인 |
| g252H | c3399ce3… | 빈 응답을 성공으로 | 동작, 45초 뒤 0x124 |
| g252I | 4dbd40ab… | 화면 인터럽트 미루기 + 진단 | 우선순위 0x00 발견 |
| g252J | 004c474a… | **vGIC 우선순위 읽기 버그 수정** | 30분+ 무사 |
| g253K | 8417ce16… | 축소 화면(CAP_SCALED_SOURCE), 모니터 모드 목록 출력 | 해상도 바꾸기 성공 |
| **g254L** | **61e2e2ef…** | 모니터 재연결 감시, `[vgic-prio]` 출력 4번 제한 | 재연결 자동 복구 성공 |

패치 파일(`port-j274/gpu/hv/`, 공개 저장소에도 같은 파일):
- `hv2-g248C-poolguard.patch`
- `hv2-g249D-local1g.patch`
- `hv2-g249D-to-g252J.patch`
- `hv2-g252J-to-g253K.patch`
- `hv2-g253K-to-g254L.patch`

지문 목록은 `MANIFEST-hv2.sha256`에 있습니다.

### 2.2 Windows GPU 드라이버(`port-j274/gpu/driver-p15/`, 기록 `PATCHES.md`)

| 판 | 패치 | 요지 |
|---|---|---|
| 2745 | P15 디버그 | 처음으로 GPU 드라이버로 바탕화면 |
| 2746~2752 | P16 | 화면 복사를 스레드로, 여러 CPU로 나누기, 효율 코어에 고정 |
| 2753~2755 | P17~P19 | 절전 대응 |
| 2756 | P20 | 하이퍼바이저 화면 서비스 자동 선택, 화면 전원 |
| **2757** | **P21** | 해상도 8개(축소 화면 기능이 있을 때만) |

빌드:
- Parallels VM "Windows 11"에서 합니다. 스크립트는 scratchpad의 `build-p21.sh`이고, PackageBuild·stamp·실행 이름을 매번 새로 바꿔야 합니다. 08b 단계가 기존 실행 폴더를 거절하기 때문입니다.
- 서명은 VM의 `WDKTestCert SYSTEM,134392251612905395`(SHA1 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF`)로 합니다. 맥미니가 이미 신뢰하는 인증서이고, testsigning과 인증서 저장소는 콘치님이 설정했습니다.
- 설치 전에 G3Armed를 넣어야 설치 직후 첫 시작이 성공합니다(§3.6).

### 2.3 공개 저장소(`~/Desktop/DEV/NWOAS-publish`) — 오늘 커밋, **푸시 안 함**

| 커밋 | 내용 |
|---|---|
| df3aab2 | S246 GPU 펌웨어 G0/G1 |
| 3423c95, 96b1db4 | S247 1·2단계 |
| f35370c | S248 GPU 바탕화면, 복사 스레드 |
| abd8f6a | S249 절전, 1 GiB, 빠른 복사 |
| a805497 | S252 하이퍼바이저 DCP 화면, vGIC 우선순위 버그 |
| 53ec930 | S253 해상도 선택 |
| 0f316bb | S254 모니터 재연결 |

- 커밋 메시지에 Claude 언급을 넣지 않습니다(전역 규칙).
- 푸시는 콘치님 확인 뒤에만 합니다.
- 소리(S255·S256)는 아직 커밋하지 않았습니다.

---

## 3. 문제별 상세 기록 — 증상, 시도한 가설, 원인, 해결, 왜 통했나

### 3.1 GPU 드라이버를 붙였더니 화면이 검고 DWM이 7초마다 죽음 (S248, 14시대)

- **증상**: 드라이버 시작은 정상인데 화면이 검음. DWM이 약 7초마다 0x889800d0으로 종료.
- **조사**:
  - Microsoft 공식 심볼 서버에서 dwmcore.dll(10.0.26100.7920)과 PDB를 받아 역어셈블했습니다.
  - 흐름: `CDDisplayRenderTarget::Present` → `COverlayContext::Present` → UMD `PresentMultiplaneOverlay1`이 E_NOTIMPL을 돌려줌 → `CD3DDevice::GetPresentError`가 0x889800d0을 골라 즉시 종료.
- **원인**: Windows 11의 새 출력 방식(DDisplay)이 우리 UMD에 없는 함수를 부름.
- **해결**: `HKLM\SOFTWARE\Microsoft\Windows\Dwm` `DisableDDisplay`=1. DWM이 예전 출력 방식으로 돌아가 충돌 0회.
- **왜 통했나**: 예전 방식은 그 함수를 부르지 않습니다. 원본 windows-on-m1도 EXP951~1782 내내 이 값을 썼습니다.
- **남은 일**: UMD에 `PresentMultiplaneOverlay1`을 구현하면 이 레지스트리 값 없이도 됩니다(우선순위 낮음).

### 3.2 화면이 끊김(체감 30프레임 미만) (S248~S249, 15시대)

| 가설 | 시험 | 결과 |
|---|---|---|
| 화면 복사(1장 8 MB)를 인터럽트 수준에서 함 | 2746 측정 | [사실] PRESENT가 IRQL 10에서 불림 → 2747부터 별도 PASSIVE 스레드 |
| GPU 메모리를 캐시 없이 읽어서 느림 | 2747~2748, 캐시 연결/WC/캐시 안 씀 비교 | **틀림**: 세 가지 모두 8 MB 읽기 5.66~5.69 ms. Windows가 이 영역의 캐시 요청을 반영하지 않음 |
| 복사 스레드가 어느 CPU에 올라가느냐 | 2750~2752 시작 시 CPU별 측정 | **맞음**: 효율 코어(CPU0~3) 5.7 ms, 성능 코어(CPU4~7) 34.6 ms. 캐시 없는 읽기만 성능 코어에서 6배 느림 |

- **해결**: 시작 때 CPU별로 재고, 가장 빠른 CPU의 1.5배 안인 CPU(0~3)에만 복사 스레드와 도우미 스레드를 묶어 4분할 → **1장 1.89 ms**. 콘치님 "더 부드러워짐".
- **참고**: Windows는 8코어 모두 EfficiencyClass 0으로 압니다(ACPI에 코어 종류 정보 없음).
- **나중에**: g252J 이후에는 하이퍼바이저가 DCP로 GPU 메모리를 바로 보여 주므로 이 복사는 대체 구성(g249D)에서만 씁니다.

### 3.3 절전(모던 스탠바이) 문제들 (S249, 16시대)

| 증상 | 원인 [사실] | 해결 | 왜 통했나 |
|---|---|---|---|
| 절전 뒤 작업 표시줄(explorer) 무한 충돌 887a0005 | GPU 재설정 뒤 `PagingRecordsUnsubmitted`가 148건 남아 CPU 복사가 STATUS_DEVICE_BUSY로 실패 | P17(2753): 재설정 성공 시 0으로 | 남은 수가 잘못된 값이므로 지워야 다음 페이징이 진행됨 |
| 2753에서 650건 남음 | [가설] VidMm이 만들고 제출하지 않은 페이징 버퍼를 알리지 않음 | P18(2754): 큐·작업자·DPC가 모두 비고 500 ms 지나면 버림 | 실제로 처리할 일이 없을 때만 버리므로 안전 |
| 절전 중 VidSch가 어댑터 재설정 | 화면 숨김 중 받은 화면 전환 3건을 완료로 알리지 않음 | P18(2754): 다음 수직 틱에 완료 통지 | VidSch는 전환 완료를 기다리다 시간 초과로 재설정함 |
| 절전 눌러도 마지막 화면이 그대로 | 복사 방식에는 화면 끄기 수단이 없음 | P19(2755): 숨김 시 HDMI 화면을 검게, 보임 시 다시 복사 | 임시 처리였고, 진짜 해결은 §3.8의 DCP 화면 끄기 |
| 2754 연속 두 번째 절전 뒤 GPU 펌웨어 작업 4 ms Timeout → RTKit 드레인 실패 → 재설정 3회 실패, 잠금화면 멈춤 | **원인 미확정** | g249D(1 GiB) 이후 재현 안 됨 | [가설] 메모리 압박과 관련. 다시 나오면 펌웨어 로그부터 |

콘치님 질문 "절전도 수동으로 만들어야 하나?"에 대한 답:
- Windows는 화면 끄기·켜기를 드라이버의 `SetVidPnSourceVisibility` 같은 함수로 알려 줍니다.
- 실제로 모니터 신호를 끄는 것은 각 그래픽 드라이버가 할 일입니다. 그래서 DCP 경로가 필요했습니다.

### 3.4 전용 GPU 메모리 287/320 MB로 꽉 참 (g249D)

- **원인 [사실]**: 실행기가 풀 창 없이(pool=0) 돌아서 Mesa 버퍼까지 구역 2(로컬 예약)에 들어감. 원본은 384 MiB를 풀 창을 전제로 정했음.
- 콘치님 질문 "통합메모리인데 VRAM을 유동적으로 못 하나?":
  - Windows WDDM에서는 "전용"이 드라이버가 알린 구역 크기이고, 공유 메모리는 별도입니다.
  - 풀 창(g248C)을 쓰면 공유 쪽으로 넓힐 수 있지만 실기기 검증 전이라 1 GiB 예약을 택했습니다.
- **해결**: `HV_AGX_LOCAL_BYTES` 0x18000000 → 0x40000000. 실기기에서 1.0 GB로 표시(콘치님 확인). 구역 2 = 952 MiB = 0x3b800000.

### 3.5 풀 창 경로에서 하이퍼바이저가 죽음 (g248C)

- [사실] GPU 장치를 끈 직후 PC 0x580a4(`backing_find`)에서 NULL 역참조.
- 흐름: AGX_RR_CLOSE가 `gpuva_v5` 전체를 0으로 지운 뒤, 풀 창 명령이 `hv_agx_retained_platform_page_busy` → `backing_find`를 부름.
- 수정: 색인 표가 NULL이면 NULL 반환. **실기기 미검증**이고, 풀 창 구성(`agxmedq-s248`)은 아직 안 씀.

### 3.6 드라이버 판 교체 실패들

| 증상 | 원인 | 해결 |
|---|---|---|
| 실행 중 `pnputil /restart-device` → 0xC0000483 | 정지 경로가 GPU 정리(retire·close·전원 끄기)를 건너뛰고 다음 시작으로 넘어감 [사실] | 판 교체는 Windows 재시작. 실패한 상태에서 다시 시작하는 것은 정상. 정지 경로 고치기는 남은 일 |
| 2746 설치 직후 첫 시작 실패 | G3Armed(1회용 레지스트리) 없이 시작됨 | 설치 작업(I-job)에서 pnputil 전에 G3Armed를 넣음 |
| 08b 단계(Mesa 런타임)가 기존 실행 폴더 거절 | — | 빌드마다 새 실행 이름(j274g3t, g3u…) |
| scheduler_windows.c 컴파일러 내부 오류 1회 | 일시적 | 다시 빌드 |
| `ZwWaitForSingleObject` 선언 없음 | — | `ObReferenceObjectByHandle` + `KeWaitForSingleObject` |

### 3.7 DCP(화면 장치)를 하이퍼바이저가 직접 다루기 — 왜, 어떻게 (S250~S251)

- **왜**: 복사 방식은 화면마다 8 MB를 CPU로 복사하고, 모니터를 끌 수 없습니다.
- **IOMFB 경로(macOS 방식) [사실]**: IOMFB 전체 소유 경로는 외장 화면(`/arm-io/disp0 external=1`)을 거부합니다. 그래서 iBoot가 쓰는 간단한 EPIC 통로(엔드포인트 0x23)를 비동기로 쓰기로 했습니다.
- **S250(g250E, 17:31) 패턴 시험**: 하이퍼바이저 없이 m1n1만으로 했습니다(`tools-hv2/dcp_swap_test.py`).
  - 120번 화면 전환 모두 APPLIED, 평균 0.74 ms. 빨강|파랑 ↔ 초록|자홍이 번갈아 나옴(콘치님 확인).
  - 화면 끄기는 검게 됐습니다.
  - **켜기 뒤에는 계속 검었습니다.** [가설] 외장 HDMI는 켠 뒤 모드를 다시 설정해야 함 → 서비스는 켤 때마다 전체 모드 설정을 하도록 설계했고, g252H 이후 실기기에서 맞았습니다.
- **S251 설계(g251F)**:
  - DCP 주인은 한 명이어야 합니다. 게스트 m1n1이 부팅 중 DCP를 만지므로 하이퍼바이저는 **처음 REGISTER_POOL/SET_POWER 때 넘겨받습니다**(lazy takeover).
  - 넘겨받은 뒤에는 게스트가 DCP/DART 영역에 접근하면 없는 장치처럼 보이게 했습니다(DCP guard).
  - LATCHED는 APPLIED + 17 ms(진짜 래치 신호가 iBoot 통로에 없음), 인터럽트는 vINTID 889, CAPS 0x27f(CAP_DISPLAY_POWER 1<<9).
  - 드라이버 P20(2756): 시작 때 CAPS를 읽어 하이퍼바이저 서비스면 그걸 쓰고, 아니면 복사 방식을 씁니다(`NwScanoutPath` 1/2).
- **g251F 실기기: 화면 켜기(SET_POWER 1) 실패**

  | 가설 | 시험 | 결과 |
  |---|---|---|
  | 응답이 500 ms 안에 안 와서 시간 초과 | g252G: 5 s로 늘리고 실패 시 마지막 응답 값을 출력 | **틀림**: 기록에 `reply=0`, 시간 문제가 아님 |
  | DCP가 결과 0인 빈 응답을 주는데 우리가 실패로 봄 | g252H: `poll != COMPLETE \|\| result != 0`일 때만 실패 | **맞음**: 넘겨받기, 출력, 끄기·켜기 모두 동작 |

### 3.8 g252H에서 약 45초 뒤 Windows가 멈추고 재부팅 (0x124) — 가장 중요한 버그 (S252)

- **증상**: 화면 서비스가 잘 돌다가 약 45초 뒤 모니터 "신호 없음" → 재부팅. 하이퍼바이저 기록 마지막은 `present #4`, `vINTID 889 injected (n=4)`, MC 줄.
- **조사 1 — 무엇으로 멈췄나**:
  - 미니덤프(`scratchpad/dump/mini.dmp`)의 WHEA 기록은 0x124, SEI(Arm SError) 1건이었습니다. ESR `0xbf40100b`(EC 0x2f, IDS=1), FAR `0x7ffd5fddddd0`.
  - 하이퍼바이저의 SError 처리(`hv_exc_serr` → Python)는 불리지 않았습니다(호스트 기록에 "Guest exception" 없음). 그러니 **게스트(Windows)가 직접 받은 SError**입니다.
- **처음 가설 [틀림]**: "하이퍼바이저가 화면 장치 메모리 매핑을 바꾸는 시점(DCP/DART 접근, 전원 끄기 중 접근)과 관련". 메모리 접근 오류였다면 EL2로 와서 Python에 찍혔어야 합니다. 그런데 안 찍혔습니다.
- **조사 2 — 같은 ESR이 예전에 있었나**:
  - `NWOAS-STATUS-2026-09-06.md` S6-D46~D50에서 **똑같은 ESR `0xbf40100b`**를 찾았습니다.
  - 그때 원인: 하이퍼바이저가 만든 인터럽트(698)를 강제 우선순위 0x00으로 넣어, 타이머(18, 0x80) 처리 중에 끼어들게 했고 두 LR이 동시에 ACTIVE였습니다. 이것이 Apple vGIC의 잘못된 상태로 SEI를 낸 것으로 봤고, 우선순위를 게스트 설정값으로 바꾸자 사라졌습니다.
- **g252I(진단 + 예방)**:
  - 다른 LR이 ACTIVE면 889 주입을 다음 틱으로 미루고(최대 50 ms), 첫 주입 때 우선순위를 출력하게 했습니다.
  - 결과 [사실]: `scanout prio=0x0 timer18 prio=0x20 VPMR=0xf0`. 그리고 미룬 경우가 실제로 생겼습니다(INTID 0 IPI, 0x20 처리 중). 138초까지 멈춤 없음.
  - → 889가 **0x00(가장 높음)** 으로 들어가고 있었습니다. 9월과 같은 조건입니다.
- **조사 3 — 왜 0x00인가 (진짜 원인)**:
  - `hv_vgic.c` 분배기 MMIO 쓰기는 GICD_IPRIORITYR 바이트 n을 **INTID n 자리**(배열 바이트 n)에 저장합니다.
  - 그런데 `hv_vgic3_get_priority()`는 SPI를 **`(intd - 32)` 자리**에서 읽었습니다.
  - 그래서 모든 SPI가 32번 앞 INTID의 우선순위를 받았습니다. 889는 아무도 안 쓴 857번 자리의 0x00을 받은 것입니다.
  - 9월에 698이 0x80으로 "측정"된 것도 사실은 666번 자리 값이었습니다.
  - 또 바이트 단위 쓰기가 4바이트 전체를 그 값으로 덮어써, 옆 INTID 세 개를 0으로 만들고 값도 엉뚱한 칸에 넣었습니다.
  - **이 함수는 하드웨어 인터럽트를 Windows로 넘기는 일반 경로(`hv_exc.c` AIC→vGIC), USB(698), 900번 경로에서도 씁니다.** 즉 그동안 모든 장치 인터럽트가 엉뚱한 우선순위로 들어가고 있었습니다.
- **해결 g252J**:
  - 읽기를 `intd/4`, `intd%4`로 고쳤습니다.
  - 분배기 우선순위 읽기·쓰기를 접근 폭(바이트 수 = 1 << width)만큼 바이트 단위로 처리하게 했습니다.
  - 결과 [사실]: Windows가 889에 쓴 값 0x50(바이트 쓰기)이 그대로 쓰입니다. 30분 넘게 멈춤 없고, 화면 끄기·켜기 정상(콘치님 확인).
  - g252I의 미루기는 남겨 두었습니다. 0x50은 IPI(0x20)보다 낮아 끼어들지 않으므로 실질적으로는 일어나지 않습니다.
- **왜 통했나**: Windows가 정한 우선순위로 들어가면, 더 급한 처리(IPI·타이머) 중에는 하드웨어가 889를 알리지 않습니다. 그래서 하이퍼바이저가 강제로 만든 "두 LR 동시 ACTIVE" 상태가 생기지 않습니다.
- **참고**: 하이퍼바이저의 종료 포착 장치(BUGPROBE)는 `ntos base not found after 16 kernel samples`로 꺼져 있었습니다. 지금 ntoskrnl 판과 RVA가 맞지 않습니다. 고치면 다음 0x124 때 LR 상태를 바로 얻을 수 있습니다(남은 일).

### 3.9 해상도 고르기 (S253, g253K + 드라이버 2757 P21)

- 원본 드라이버에는 맥북용 "축소 화면" 기능(rc106)이 이미 있었습니다. 작은 표면을 DCP가 패널 크기로 늘려 보여 주고, 브로커가 `CAP_SCALED_SOURCE`(bit 8)를 알려야 쓰입니다. J274 판(P15)이 표를 1개로 막아 두었을 뿐입니다.
- **하이퍼바이저(에이전트)**:
  - CAP 비트 추가(CAPS 0x37f), DEST_ORIGIN 0xa0 / DEST_SIZE 0xa4(창 0xa8)를 추가했습니다.
  - iBoot 화면 전환의 src = (0,0,w,h), dst = 드라이버가 준 사각형입니다.
  - 넘겨받을 때 모니터 모드 목록을 출력합니다.
  - 사각형 밖 색: iBoot 통로에 배경색 칸이 없어 레이어 하나만 썼습니다. [가설이었는데 사실로 확인] 밖은 검게 나옴.
- **드라이버(에이전트)**:
  - J274 표에 1920x1080, 1680x1050, 1600x900, 1440x900, 1280x800, 1280x720, 1024x768, 800x600을 넣었습니다.
  - 복사 방식에서는 CAPS 0x7f라 1개만 남습니다.
  - 신호 정보는 1080p60 기준으로 고쳤습니다.
- **실기기**: 목록 8개 확인. 1280x720 → `panel 1920x1080 at 0,0` 화면 가득. 1024x768 → `panel 1440x1080 at 240,0` 양옆 필러박스(콘치님 확인).
- **제 실수 2개**:
  1. 첫 설치 때 제가 만든 실행기의 하이퍼바이저 **파일 크기 확인값**을 옛 판 값(2277376)으로 두어 실행기가 5초 만에 멈췄습니다(Windows 영향 없음). → 이후 새 실행기는 `[ ... ]` 검사 줄만 뽑아 미리 돌려 봅니다.
  2. DCP가 보고한 모드 33개에 3840x2160@60이 있다고 "이 모니터는 4K 지원"이라고 단정했습니다. 실제 모니터 사양은 FHD입니다. → "장치가 보고한 값"과 "사양"을 구분하는 규칙을 전역 CLAUDE.md에 넣었습니다. [가설] 4K 입력을 받아 줄여 보여 주는 기능을 목록에 올리는 HDMI 모니터일 수 있음.

### 3.10 모니터가 꺼졌다 켜진 뒤 "신호 없음"이 계속됨 (S254, g254L)

- **증상(19:5x)**: 콘치님이 포터블 모니터 충전기(65W 3포트)에 휴대폰 케이블을 더 꽂음 → 전원이 다시 맞춰지며 모니터가 꺼졌다 켜짐(로고 뜸) → 신호 없음이 계속됨.
- **관찰 [사실]**: 하이퍼바이저는 화면이 켜진 것으로 알고 있었습니다. Windows는 정상이었습니다(dwm·explorer 실행, 새 충돌 없음).
  - 콘치님께 확인: "한동안 바탕화면이 보이다가 꺼졌다" + 충전기 사정.
- **원인**: 모니터가 다시 켜지면 HDMI 연결을 처음부터 다시 맞춰야 하는데, 하이퍼바이저에 연결 변화 처리가 없었습니다(S251 문서 §9 위험 3이 실제로 일어남).
  - iBoot 통로에는 연결 변화 알림이 없고, ADT `dp2hdmi-gpio`에는 reset·force-dpfu 핀만 있어 HPD 신호선을 직접 받을 수 없습니다.
- **원인 확인 [사실]**: Windows가 쉬는 시간이 차서 화면 끄기를 요청한 뒤 콘치님이 키를 누름 → 켜기 과정의 전체 모드 설정 → 화면이 돌아옴.
- **해결 g254L**:
  - 화면이 켜져 있고 통로가 비어 있으면 1초마다 GET_HPD를 보냅니다.
  - HPD 0 → 1, 또는 해상도 개수가 바뀌면 하이퍼바이저가 켜기 과정(HPD → 켜기 → 100 ms → 모드 → 모드 설정 → 현재 화면 다시 보내기)을 스스로 다시 합니다.
  - 그동안 들어온 화면 출력은 화면 꺼짐 때처럼 처리합니다.
  - 출력·끄기/켜기 요청이 GET_HPD와 겹치면 최대 20 ms 기다립니다. Windows의 켜기 요청이 겹치면 그 과정에 합류합니다.
- **실기기 [사실]**: 2번 뽑았다 꽂음 → `monitor disconnected (hpd 0, modes 33/0)` → `reconnected` → `reconnect modeset done in 315055 us`(#1), 314978 us(#2). 키 입력 없이 복구(콘치님 확인).
- **콘치님 질문 "모든 모니터가 이렇게 물어보나?"**: 아닙니다. 보통은 HPD 변화가 인터럽트로 알려지고 macOS는 DCP 펌웨어가 IOMFB로 알려 줍니다. 우리는 알림이 없는 iBoot 통로를 쓰기 때문에 물어보는 방식입니다.
- **한계**: 1초보다 짧은 끊김은 해상도 개수가 같으면 놓칩니다.

### 3.11 내장 스피커 (S255~S256)

- **콘치님 결정**: 연결 장치는 내장 스피커뿐이고, 내장 스피커를 먼저 함.
- **조사(에이전트 2개, 문서 `research-20261116/sound/hw-path.md`, `windows-side.md`)**:
  - 경로: 메모리 → DART-sio 스트림 2 → ADMAC TX 채널(클러스터 n → 4n) → MCA 클러스터 → I2S → TAS5770L(I2C1 0x235014000, 주소 0x31) → 스피커.
  - SIO 펌웨어 없이 CPU가 직접 다룰 수 있습니다.
  - TAS5770L은 비공개 부품이지만 TAS2770 레지스터와 같습니다. 0x02 동작/음소거/종료, 0x03 이득, 0x05 디지털 볼륨(−0.5 dB 단위, **리셋 값 0 = 최대**).
  - 앰프 리셋 GPIO 181(0x23c1002d4).
  - 안전: Asahi는 speakersafetyd가 없으면 커널이 볼륨을 사실상 무음으로 묶습니다. J274 열 모델로 연속 0.94 W, 이득 21 dB일 때 최소 감쇠 −17.7 dB.
  - MCA 클러스터 번호가 엇갈렸습니다: ADT는 1, m1n1 실험·Asahi DT는 0. 실기기에서는 **0으로 동작**했습니다.
- **S255 비프음(20:09)**:
  - `nwoas_scripts/sound-s255/beep_tone.py`를 D4 프록시에서(하이퍼바이저 올리기 전) 돌렸습니다. 바탕은 m1n1 `proxyclient/experiments/speaker_amp.py`(MIT)이고 안전 장치를 더했습니다.
    - 볼륨을 0x50(−40 dB)으로 **먼저** 쓰고 되읽어 다르면 멈춤
    - 이득 0x00
    - 440 Hz 0.7초 소리를 −20/−10/−4 dBFS로 3번
    - finally에서 음소거 → 종료
  - 처음 설계는 볼륨 레지스터를 단계마다 바꾸는 것이었는데, DMA 큐가 앞서 있어 실제 재생 시점과 어긋날 수 있어서 볼륨은 고정하고 데이터 크기로 나눴습니다.
  - `DARTRegs`가 없어진 m1n1에 맞춰 `DART.from_adt`로 바꿨습니다.
  - 결과: 되읽기 0x50, 끝난 뒤 0x02=0xe. **콘치님 3번 모두 들음**(−60 dB도 들림).
  - MCA 클럭 먹스는 이미 5+n으로 되어 있었습니다(바꿀 필요 없음). `mca0`·`mca1`에 clock-gates가 없다는 pmgr 오류 줄은 무해합니다.
- **S256 런처 준비 모듈 + 펌웨어 SSDT(에이전트)**:
  - 계약: `research-20261116/sound/PLAN-S256.md`. ACPI `SPK0` `_HID "NWAU0001"`, 메모리 자원 5개(I2C1, ADMAC, MCA 클러스터, MCA switch1, 소리 창).
  - 소리 창은 게스트 IPA `0x340000000`+0x40000 → PA `0x80c630000`(프록시 힙, 게스트 RAM 밖)이고, 고리 버퍼는 창+0x1000, IOVA `0x80001000`.
  - 창 머리말: magic 'NWAU', ring_iova, 48 kHz, 형식 1(모노 24비트를 32비트 상위 정렬), ADMAC 0, 클러스터 0, 앰프 0x31, 볼륨 상한 0x30, 상태 bit0.
  - `dart.initialize()`는 쓰지 않고 2번 스트림만 초기화했습니다(SIO 0번·AES 1번 보호). 0·1번 값이 그대로인지도 확인합니다.
  - 실기기(20:28) [사실]: 모든 단계 정상, 앰프는 0x0e로 대기, `READY: hardware prepared, nothing playing`. DART-sio ERROR=0xc010400은 모듈 전부터 있던 값입니다.
  - Windows: `ACPI\NWAU0001\0`이 보임, 드라이버 없음(문제 28).
- **Windows 드라이버(에이전트) — 중단**:
  - 완성: `port-j274/sound/nwoas-audio/`
    - Microsoft `audio/simpleaudiosample` 사본(커밋 d49bb7d72f36, MS-PL)
    - `Source/Main/nwau_core.c/.h`: 머리말·자원 검사, 16비트 스테레오 → 모노 24-in-32 변환, 볼륨 → 0x05(상한 강제, 0xC9 음소거), 고리·기술자 계산
    - `tests/` 60116개 통과
  - 막힘: 하드웨어를 다루는 `nwau_hw.cpp`(자원 매핑, I2C·ADMAC·MCA, 감시 타이머)를 쓰던 중 Anthropic 안전 분류기에 걸림.
  - 남은 것: `nwau_hw.cpp`, 샘플 고치기(StartDevice, 마이크 빼기, 스트림 타이머, 볼륨 연결, `kshelper.cpp` ALL_CHANNELS 반복 버그, vcxproj, INF `ACPI\NWAU0001`), `build-audio.sh`, VM 빌드·서명, 전달 매니페스트, `nwoas_scripts/sound-s256/jobs/A1-install-audio.cmd`·`A2-status.cmd`, README.
  - 에이전트가 남긴 주의 [가설]:
    1. 런처가 머리말·고리를 캐시를 거쳐 쓰고 clean+invalidate를 안 하면, 드라이버가 머리말을 못 읽거나 나중에 밀려 나온 캐시가 고리를 덮을 수 있습니다. 모듈이 실제로 어떻게 쓰는지 확인 필요.
    2. Python `map_hw`의 메모리 속성.

### 3.12 와이파이 (17:4x)

- **콘치님 결정**: 내장 와이파이(BCM4378 추정) 우선. USB 유선 랜은 우선순위 아님.
- 에이전트 A(펌웨어 올리기 스크립트), B(WiFiCx 드라이버 뼈대) 둘 다 안전장치(cyber)에 막혀 중단. 다시 시도하지 않았습니다.
- B가 남긴 것:
  - `port-j274/wifi/bcm4378-wificx/src/` 쓰다 만 소스 4개
  - scratchpad `wifi-drv-src`(OpenBSD bwfm·brcmfmac 참고)
  - `/Volumes/media/nwoas/wifi-build` 찌꺼기는 지웠습니다.
- 계획 문서: `nwoas_scripts/plan-20261115/wifi.md`.

### 3.13 기타 사건

| 사건 | 원인 | 처리 |
|---|---|---|
| DCP 시험 뒤 다시 켜기 실패(Running Proxy에서 멈춤) | 맥미니 재부팅 직후 USB 재열거와 실행기 시작이 겹침 | `run-reboot-arm-relaunch1g.sh`에서 대기를 늘림 |
| 콘치님이 "기록 쌓는 줄 알고 창 들고 하다가 멈춤" | 안내가 모호했음 | 시험 때는 무엇을 해야 하는지 명확히 말함 |
| 낮잠 중인데 기록을 읽음 | 규칙 위반 | 전역 CLAUDE.md에 "잔다 하면 한두 줄만" 규칙 |
| 영어 상태 메시지 1번 | 규칙 위반 | 사과, 이후 한국어만 |
| 자동 모드 분류기가 첫 1 GiB 재시작을 거부 | — | 콘치님 "진행해" + 허용 규칙 추가 |
| 기록 감시(Monitor)가 끝나도 그 안의 `tail -F`가 남음 | Monitor 스크립트의 `kill %1`이 일부 경우 안 먹음 | 20:3x에 5개 정리. **세션 끝낼 때 `ps`로 `tail -n` 프로세스 전부 확인** |

---

## 4. 다음에 할 일 (순서)

1. **CVP 승인 확인.** 승인 메일의 조직 ID가 Claude Code 계정 조직과 같은지 봅니다. 같으면:
   - **소리 드라이버** 남은 부분(§3.11 "남은 것")을 Opus 5.5 에이전트로 이어서 하고, 먼저 작은 일(`nwau_hw.cpp` 하나)로 다시 막히는지 봅니다.
   - 설치·시험 순서:
     1. VM 빌드
     2. `A1-install-audio.cmd`
     3. 장치 상태 OK, 소리 설정에 "스피커" 확인
     4. 작업 통로로 WAV 재생(처음 볼륨 −40 dB, 상한 −24 dB)
     5. 콘치님 귀로 확인
     6. 긴 재생·볼륨·절전
   - **와이파이**: `nwoas_scripts/plan-20261115/wifi.md` 순서(PCIe 노출 → 펌웨어 올리기 → 검색 → 접속).
2. 승인 전에도 할 수 있는 것(안전장치와 무관한 일):
   - 소리 준비 모듈의 캐시 처리 확인(§3.11 주의 1).
   - 공개 저장소에 S255·S256(비프음 스크립트, 준비 모듈, 펌웨어 SSDT, 문서) 커밋.
   - BUGPROBE를 지금 ntoskrnl에 맞추기(§3.8 참고). 다음 0x124 때 LR 상태를 바로 얻기 위해서입니다.
   - 판 교체 시 GPU 정리 누락(`pnputil /restart-device` 0xC0000483) 원인 찾기.
   - 2754 두 번째 절전 펌웨어 Timeout이 다시 나오는지 오래 시험.
   - UMD `PresentMultiplaneOverlay1` 구현(DisableDDisplay 없이).
3. 콘치님 결정이 필요한 것:
   - 공개 저장소 **푸시**(오늘 커밋 7개 이상 쌓임)
   - 부팅마다 G3Armed를 넣는 예약 작업(`agx-s248/jobs/T1-arm-task.cmd`, Windows 설정을 바꾸는 일)
   - Macintosh HD의 D4 m1n1 등록 지우기(macOS를 다시 쓰려면)

---

## 5. 지켜야 할 콘치님 규칙 (이번 세션에서 쓰인 것)

- 모든 글은 한국어 합쇼체, "콘치님", 결론부터, 수치는 표, 내가 만든 용어 금지.
- wrap_up에는 요약만 넣고 질문·버튼은 넣지 않습니다.
- 서브에이전트는 Opus 5.5만 씁니다(Fable 5.2는 조언자로 가능). 독립 갈래는 동시에 띄웁니다.
- Windows 재시작(`run-install-*`, `run-reboot-arm*`)은 묻지 않습니다. 과금·개인 계정·지우기는 묻습니다.
- 안전장치에 막힌 에이전트 일은 다시 시키지도, 대신 하지도 않습니다. 콘치님께 알립니다.
- 콘치님이 잔다·쉰다 하면 한두 줄만 남깁니다.
- 장치가 보고한 값은 사양처럼 말하지 않습니다(4K 사건).
- 세션을 끝낼 때는 이 세션이 띄운 백그라운드 작업(`tail -F`, 대기 루프, 에이전트가 남긴 것)을 프로세스 목록 전체에서 찾아 정리합니다. 단, 지금 Windows를 돌리고 있는 실행기(`agxdcp-s256-guest-test.sh`, `caffeinate`, `run-reboot-arm-dcp256.sh`)는 Windows를 끄는 것이므로 콘치님 확인 뒤에만 끕니다.

---

## 6. 파일 지도

| 무엇 | 어디 |
|---|---|
| 하이퍼바이저 소스 | `m1n1-gpu-hv2/src/` (g254L 상태). 주요 파일: `display.c` S251~S254 구간, `nwoas_agx_broker.c`, `hv_agx_scanout_broker.c`, `hv_vgic.c` |
| 하이퍼바이저 결과물·문서 | `port-j274/gpu/hv/out-hv2/`, `README-hv2.md`, `README-dcp-s250.md`, `README-dcp-s251.md`(§9~§12에 실기기 결과) |
| GPU 드라이버 소스·기록 | `port-j274/gpu/driver-p15/` (`PATCHES.md` P1~P21) |
| 실행기 | `nwoas_scripts/agxdcp-s25{1,2,3,4,6}-guest-test.sh`, `agxmed1g-s249-guest-test.sh`(안정 대체) |
| 재시작·설치 스크립트, 작업 파일, 실행 기록 | `nwoas_scripts/agx-s248/` (`jobs/`, `runs/`) |
| 소리 | `research-20261116/sound/`(조사·계약), `nwoas_scripts/sound-s255/`(비프음), `nwoas_scripts/sound-s256/`(준비 모듈), `nwoas_scripts/fw-audio-s256/`(펌웨어), `fw-audio-s256-mu/`(빌드 트리), `port-j274/sound/nwoas-audio/`(드라이버, 미완성) |
| 와이파이 | `port-j274/wifi/bcm4378-wificx/src/`, `nwoas_scripts/plan-20261115/wifi.md` |
| 오후 결과 요약 | `research-20261116/RESULT-20261116-afternoon.md` |
| 실기기 로그 | `nwoas_scripts/logs/` (`*.vuart` = 하이퍼바이저 출력, `launcher-*.out`) |
| 프로젝트 메모리 | `~/.claude/projects/-Users-angigyeom-Desktop-DEV-NWOAS/memory/` (`nwoas-state-2026-11-15.md`에 시각별 갱신, `project-network-wifi-first.md`에 와이파이·소리 막힘) |
| scratchpad(재부팅하면 사라질 수 있음) | `build-p16.sh`, `build-p21.sh`(VM 빌드), `readstats4.sh`, `dump/mini.dmp`(g252H 0x124 덤프), `sym/`, `wifi-drv-src/`, `adtdump.py`(ADT 노드 출력: `python3 -I adtdump.py <adt.bin> <이름 일부...>`) |
