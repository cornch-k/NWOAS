# S251 — 하이퍼바이저가 화면 중개(스캔아웃 서비스)를 직접 맡기

**상태 (2026-11-16): 맥북에서 빌드와 호스트 시험만 통과했습니다. 맥미니에서는 아무것도 실행하지 않았습니다.**
빌드·호스트 시험 통과는 화면이 나온다는 뜻이 아닙니다. 판정은 맥미니 앞에서 사람이 모니터를 보고서만 할 수 있습니다.

표기: **[사실]** = 이번 작업에서 파일·줄·명령 출력으로 직접 확인. **[가설]** = 확인하지 않은 추측.

## 1. 한눈에

| 항목 | 내용 |
|---|---|
| 새 m1n1 | `out-hv2/m1n1-g251F-agx-hv2-dcpservice.bin` (+ `.elf`), 빌드 태그 `v1.0.2-1474-gbddf7f06-g251F` |
| 바탕 | g250E 소스 그대로(1 GiB 로컬 예약 g249D, 비동기 화면 전환 g250E 유지) |
| 켜는 법 | 런처 환경 변수 `NWOAS_AGX_DCP_SERVICE=1` (새 런처 `nwoas_scripts/agxdcp-s251-guest-test.sh`). 없으면 지금과 똑같이 동작 |
| 하는 일 | 드라이버(KMD)가 쓰는 화면 중개 ABI v2(브로커 페이지 +0x400)를 하이퍼바이저가 직접 처리. 화면 전환은 g250E의 iBoot 비동기 3단계(SWAP_BEGIN→SET_LAYER→END). LATCHED는 APPLIED + 17 ms(시간 기준). 화면 전원 명령 추가 |
| KMD 쪽 | 아직 소프트 브로커 그대로입니다(5단계에서 바꿔야 함, §6). 지금 KMD로는 이 기능을 쓰지 않습니다 |

## 2. 3단계 — DCP 주인은 한 명만

### 2.1 지금 DCP를 건드리는 곳 [사실]

| 순서 | 누가 | 어디서 | 하는 일 |
|---|---|---|---|
| 1 | 설치된 D4 m1n1 | 부팅 | 모드 설정 + 화면 전환 + 재움 (로그 `agxmed1g-s249-20261116-173323.hqLsk3:81-104`) |
| 2 | 체인로드된 HV2 m1n1 | `src/main.c:172` `display_init()` → `:175` `display_shutdown(DCP_SLEEP_IF_EXTERNAL)` | 모드 설정 + 재움 (같은 로그 `:309-331`) |
| 3 | HV2 `hv_init()` | `src/hv.c:67-68` | 외장 화면이면 `display_start_dcp()`로 깨운 뒤 `display_shutdown(DCP_QUIESCED)` (게스트가 다시 부팅할 수 있게 정지 상태로 둠) |
| 4 | **게스트 m1n1** (페이로드 앞 0x20c000, `loader-s215` 빌드, 소스 `m1n1-guest-s215` @ bddf7f06 + `loader-s197/guest-compat-s197.patch`) | `m1n1-guest-s215/src/main.c:172` `display_init()` (패치가 DEFER 분기를 없앰: `guest-compat-s197.patch:46`), `:175` 재움 | EL1에서 DCP MMIO를 그대로 써서 세 번째 모드 설정 + 재움 + `pmgr_reset(DISP0_CPU0)` (S244 README §R2) |
| 5 | **게스트 m1n1 kboot** | `kboot.c:1918` `dart_lock_adt("/arm-io/dart-disp0")`, `:1930` → `:1359` `dart_search()` | dart-dcp·dart-disp0 레지스터를 읽고 **L1이 가리키는 모든 L2 표를 따라 읽음** (`dart.c:679-702`) |
| 6 | Mu UEFI, Windows 기본 화면 | — | DCP를 건드리지 않고 POST 화면 버퍼(`/vram` 0xbe3f60000, 1920x1080, stride 7680)에 그림만 씀 |
| 7 | 지금 KMD(P15-P19) | `scanout_windows.c` 소프트 브로커 | DCP를 건드리지 않음. 표면을 POST 화면 버퍼로 CPU 복사 |

게스트 m1n1 화면 출력은 어느 로그에도 남지 않습니다(S244 R10). 4번이 13.5에서 실제로 성공한다는 것은 Windows 바탕화면이 보였다는 결과에서 나온 추정입니다 [가설].

### 2.2 고른 방법: "게스트 m1n1이 끝난 뒤 하이퍼바이저가 넘겨받기"

요청은 "하이퍼바이저 시작부터 DCP를 깨어 있게 유지하고 게스트가 DCP에 못 닿게 막기"였습니다. 코드를 따라가 보니 그렇게 하면 **게스트 부팅이 깨지는 길**이 있어 순서를 바꿨습니다.

- 하이퍼바이저가 DCP를 깨운 채 들고 있으면, RTKit·AFK 버퍼와 풀(952 MiB)용 DART L2 표가 **하이퍼바이저 힙**에 생깁니다 (`dart.c:495-517` `dart_get_l2()` → `memalign`) [사실].
- 게스트 kboot는 화면 예약 영역을 찾으려고 dart-dcp/dart-disp0의 **모든** L1 항목을 따라 L2 표를 읽습니다 (`kboot.c:1359` → `dart.c:688`) [사실].
- 하이퍼바이저 힙(이번 부팅 0x80614a680 부근)은 게스트에 매핑되지 않습니다 (로그 `PT[800000000:801224000]`, `PT[83db80000:be1000000]`만 RAM) [사실]. 그러면 게스트 m1n1이 그 주소를 읽다가 멈춥니다 [가설: 정확히 어떤 예외로 멈출지는 보지 않음].
- MMIO만 막으면(읽기 0) 게스트 `dcp_init()`이 펌웨어 매핑을 새로 만든 뒤 `pmgr_reset(DISP0_CPU0)` (`dcp.c:203-204`)을 불러 **하이퍼바이저가 쓰는 DCP를 리셋**합니다. PMGR 페이지는 다른 장치와 같이 쓰여 막을 수 없습니다 [사실: 코드 경로, 하드웨어 확인 아님].

그래서 이렇게 했습니다.

| 시점 | DCP 주인 | 화면 |
|---|---|---|
| D4 → HV2 main → `hv_init` → 게스트 m1n1 (모드 설정 + 재움) | 지금과 같음 (한 번에 하나씩 차례로) | POST 화면 버퍼 |
| Mu, Windows 기본 화면 | 아무도 없음 (DCP 잠듦) | POST 화면 버퍼 (지금과 같음) |
| KMD가 처음 `REGISTER_POOL` 또는 `SET_POWER`를 보낼 때 | **하이퍼바이저가 넘겨받음**: `display_start_dcp()` (hv_init과 같은 호출) → 비동기 모드 설정 → POST 화면 버퍼로 화면 전환 | POST 화면 버퍼 그대로 |
| 그 뒤 하이퍼바이저가 끝날 때까지 | 하이퍼바이저만. 다시 재우지 않음 | KMD 표면 |

- 넘겨받는 순간 하이퍼바이저는 `/arm-io/dcp` reg 2개, `/arm-io/dart-dcp` reg 1개, `/arm-io/dart-disp0` reg 2개를 게스트 stage-2에서 **없는 장치**(읽기 0, 쓰기 무시, 16회까지 기록)로 바꿉니다 (`nwoas_agx_broker.c:474` `dcp_guard_install()`). 그 무렵 게스트 m1n1은 이미 없고 Mu·Windows에는 DCP 드라이버가 없으니 정상이면 아무 일도 없습니다. 혹시 누가 건드리면 로그에 `NWOAS-AGX: DCP guard:` 줄이 남습니다.
- **페이로드(게스트 m1n1, Mu)는 다시 빌드하지 않았습니다.** `hv_init()`의 정지 동작(`hv.c:67-68`)도 그대로입니다.
- 넘겨받기 전에 S250 시험 명령(0x1181-0x1185)으로 DCP를 쓴 상태면 넘겨받기를 거절합니다. 넘겨받은 뒤에는 S250 시험 명령이 모두 -16을 돌려줍니다 (`display.c` `NWSVC_CLAIMED()`).

시작부터 하이퍼바이저가 들고 있으려면 필요한 것(이번에 하지 않음): 게스트 FDT(`dtb-s221`)에서 `dcp`·`disp0`·`disp0_piodma` 별칭을 빼서 kboot 화면 예약 단계를 건너뛰게 하고(`kboot.c:1663-1665` "별칭이 없으면 바로 끝냄"), 게스트 ADT에서 `/arm-io/disp0`을 지워 게스트 `display_init()`이 바로 끝나게(`display.c:630-634` 노드 없음 → -1) 해야 합니다. 페이로드를 다시 조립해야 하고 Mu가 FDT를 쓰지 않는지 다시 확인해야 합니다.

## 3. 4단계 — 서비스

### 3.1 파일

| 파일 (`m1n1-gpu-hv2/`) | 처리 | 내용 |
|---|---|---|
| `src/hv_agx_scanout_service.c/.h` | **새 파일** (UP 이식) | UP 상태 기계 그대로 + 넘겨받기(`owner_poll`) + 전원(`power_begin/poll`). UP의 진단 콜백(EXP806-890)은 옮기지 않음. 호스트 시험에서 그대로 컴파일됨 |
| `src/hv_agx_scanout_broker.c/.h` | 수정 | J274 화면 값(1920x1080, stride 7680, 표면 0x7e9000, `NWOAS_AGX_SCANOUT_J274` 때만), `CMD_SET_POWER`(4), 전원 레지스터 +0x9c, `CAP_DISPLAY_POWER`(비트 9), `complete_power()`. 이 비트가 없으면 UP와 똑같음 |
| `src/display.c/.h` | NWOAS S251 구역 추가 (AGX 빌드만) | 넘겨받기, 비동기 모드 설정·전원, 표면 전환, 시간 기준 LATCHED, RELEASE 때 POST 화면 버퍼 복귀 |
| `src/dcp_iboot.c/.h` | 함수 추가 (AGX 빌드만) | iBoot 명령 하나를 비동기로: SET_POWER, GET_HPD, GET_TIMING_MODES, GET_COLOR_MODES, SET_MODE |
| `src/dcp.c/.h` | AGX 빌드만 | DCP IOVA 창 끝을 변수로(`nwoas_dcp_iova_end`). 평소 0x20000000(지금과 같음), 넘겨받을 때만 0x60000000(1.25 GiB, UP `dcp.c:946-950`) |
| `src/nwoas_agx_broker.c` | 수정 | 설정 확인, 플랫폼 함수, `scanout_select()`, `nwoas_agx_tick()`, DCP 가드 |
| `src/nwoas_agx.h`, `src/nwoas_agx_hooks.h`, `src/hv.c` | 수정 | 설정 플래그, `hv_tick()` 끝에서 `NWOAS_AGX_TICK()` (기본 빌드는 빈 매크로) |
| `Makefile` | 수정 | `hv_agx_scanout_service.o`, `-DNWOAS_AGX_SCANOUT_J274=1` (AGX 빌드만) |

**기본 빌드(NWOAS_AGX 없음)는 바뀌지 않았습니다** [사실]: 기본 빌드를 g250E 태그로 다시 빌드하면 sha256이 g250E 기본 빌드와 똑같습니다(`04ed18f2…2b2c37`, §7).

### 3.2 어디서 돌리는가

`hv_tick()` 맨 끝(`hv.c:495`)에서 `nwoas_agx_tick()`을 부릅니다 [사실: `hv.o`의 `hv_tick` 마지막이 `nwoas_agx_tick`으로 점프].

| 근거 | 내용 |
|---|---|
| 한 실행 흐름 | `hv_tick()`은 5 kHz, 끼어들 수 있는 CPU 하나에서만, `bhl`을 쥔 채 돕니다 (`hv_exc.c` `hv_exc_fiq` 느린 경로). 브로커 MMIO 처리, 프록시 명령도 `bhl` 아래라서 DCP를 쓰는 곳이 전부 한 줄로 섭니다. HV2의 RTKit/AFK 송신에는 UP의 예약 API가 없어(S250 README §3) 이 조건이 꼭 필요합니다 |
| 할당·출력 | 같은 자리에서 NVMe 빠른 경로(`hv_vm.c:1596`)와 프록시 처리가 이미 할당·출력을 합니다. 예외 진입 직후가 아니라 이 틱의 프록시·vuart·NVMe 처리가 끝난 뒤입니다 |
| UP와 다른 점 | UP는 `hv_exc_exit()`에서 `bhl` 밖, 여러 CPU에서 돌립니다(UP `hv_exc.c:1843`). HV2는 송신 직렬화가 약해 그렇게 하지 않았습니다 |
| 한 틱의 양 | 최대 512단계 또는 100 µs. 952 MiB 풀 검사+DART 두 개 매핑은 단계 11,424개 → 호스트 시험에서 26틱(모의 5.2 ms) |
| 예외 | 넘겨받기의 `display_start_dcp()`는 한 단계 안에서 동기로 돕니다(RTKit 부팅, 엔드포인트 시작). 이때 CPU0이 한 번 멈춥니다. 부팅 때 모드 설정 시간으로 보면 0.1-0.3 s로 봅니다 [가설]. 1초 HV 감시 타이머는 이 호출 동안만 멈춥니다(`hv_wdt_suspend/resume`) |

### 3.3 LATCHED = APPLIED + 17 ms

- APPLIED는 `SWAP_END` 응답을 받은 순간입니다. 모니터가 새 그림으로 바뀐 순간이 아닙니다 (S250 README §4).
- iBoot 경로에는 화면이 실제로 바뀐 순간을 알려 주는 신호가 없습니다 (UP도 iBoot 경로에서는 LATCHED를 내지 않음: UP `display.c:742-748` `display_scanout_latch_poll()`이 전체 IOMFB 주인일 때만 동작) [사실].
- 60 Hz 한 장은 16.67 ms입니다. 새 표면이 다음 수직 동기에서 걸린다면 APPLIED 뒤 한 장 길이가 지나면 옛 표면은 더 읽히지 않습니다 [가설: DCP가 SWAP_END 뒤 첫 수직 동기에서 바꾼다는 가정]. 그래서 17 ms(올림)를 기본으로 했습니다.
- 런처 `NWOAS_AGX_DCP_LATCH_US`로 0-100000 µs를 줄 수 있습니다(0이면 17000). 화면이 찢어지면 33000(두 장)으로 올려 봅니다.
- 브로커는 LATCHED 전 다음 PRESENT를 BUSY로 돌려주므로 최대 약 56장/초입니다(0.74 ms + 17 ms).

### 3.4 그 밖의 동작

| 항목 | 동작 |
|---|---|
| 넘겨받기 순서 | `display_start_dcp()` → GET_HPD(100 ms 간격 최대 10회) → SET_POWER(1) → 100 ms → GET_TIMING_MODES(1920x1080, 60 Hz에 가장 가까운 것, 없으면 실패) → GET_COLOR_MODES(`display_choose_color_mode`와 같은 규칙) → SET_MODE(실패하면 500 ms 뒤 한 번 더) → POST 화면 버퍼로 화면 전환. `display_configure()`(외장 화면)과 같은 순서지만 기다림은 틱 마감 시간으로 |
| 풀 | 로컬 예약 2구역 전체(0x3b800000, IPA 0xa00000000)만 받음. 16 KiB마다 IPA→PA가 이어지고 2구역 안인지 확인. dart-disp0과 dart-dcp에 같은 IOVA로 256 KiB씩 매핑. 실패하면 둘 다 되돌림 |
| 표면 | 1920x1080, stride 7680, 크기 0x7e9000, BGRA8888만. RGBA는 S250에서 시험한 적이 없어 `CAP_RGBA8888`을 끔(KMD는 이 비트가 없으면 A8B8G8R8을 내놓지 않음: `scanout_windows.c:1648-1656`) |
| 캐시 | 화면 전환 전에 표면 전체 `dc civac` (S250 측정 8 MB 약 62 µs). `NWOAS_AGX_DCP_NO_CLEAN=1`이면 생략 |
| RELEASE | POST 화면 버퍼로 되돌림 → APPLIED + 150 ms → 두 DART에서 풀 해제 (UP 순서) |
| 화면 꺼짐 중 PRESENT | DCP에 보내지 않고 받아 둠: APPLIED 바로, LATCHED는 17 ms 뒤. 켤 때 마지막 표면을 다시 보여 줌 |
| vIRQ | `hv_vgic3_spi_enabled(889)`이고 같은 CPU에 아직 걸린 것이 없고 빈 LR이 있을 때만 `take_irq_edge` 후 주입 (UP와 같은 규칙, NVMe 900과 같은 방식) |
| 힙 | 설치 때 힙 여유 검사에 8 MiB 추가 |

## 4. 바뀐 ABI — KMD가 따라야 할 것

브로커 페이지 IPA 0x300000000, 화면 중개 창 +0x400 (크기 0xa0). 아래 오프셋은 창 기준입니다.

| 항목 | 값 | 비고 |
|---|---|---|
| `REG_ABI_VERSION` (+0x04) | 2 | 서비스가 켜졌을 때만. 꺼지면 지금처럼 v1, 능력 0 |
| `REG_CAPABILITIES` (+0x08) | `0x27f` | FIXED_J313(0) · BGRA8888(1) · REGISTERED_POOL(2) · APPLIED_RECEIPT(3) · REPEATED_PRESENT(4) · LATCHED_RECEIPT(5) · LATCHED_IRQ(6) · **DISPLAY_POWER(9)**. RGBA8888(7)·SCALED_SOURCE(8) 없음 |
| `CAP_DISPLAY_POWER` | `1u << 9` = 0x200 | 새 비트. KMD 헤더의 비트 8(SCALED_SOURCE)과 겹치지 않음 |
| `REG_POWER` (+0x9c, u32) | 쓰기: 0 끄기 / 1 켜기, 읽기: 지금 전원 | 새 레지스터. DISPLAY_POWER 비트가 없으면 접근 거절 |
| `CMD_SET_POWER` | `REG_COMMAND` (+0x98) = 4 | 새 명령 |
| `REG_POOL_SIZE` | 0x3b800000 정확히 | 로컬 예약 2구역 크기. 다르면 INVALID_POOL |
| 표면 | WIDTH 1920, HEIGHT 1080, STRIDE 7680, SURFACE_SIZE 0x7e9000, FORMAT 1(BGRA) | 다르면 INVALID_SURFACE |
| vINTID | 889 (런처 `NWOAS_AGX_SCANOUT_INTID`) | LATCHED/ERROR 때 엣지 한 번. IRQ_STATUS를 지워야 다음 엣지 |

`CMD_SET_POWER` 순서:

1. `REG_POWER` ← 0 또는 1 (u32)
2. `REG_REQUEST_SEQUENCE` ← 새 순번 (u64, 지금까지보다 커야 함)
3. `REG_COMMAND` ← 4 (u32)
4. `REG_RECEIPT_SEQUENCE` == 순번이 될 때까지 기다림. 그때 `REG_RESULT` == 0이면 성공: `REG_APPLIED_SEQUENCE` == 순번, `REG_STATE`는 명령 전 상태(UNREGISTERED/READY/ACTIVE)로 돌아옴, ACTIVE였으면 `REG_LATCHED_SEQUENCE`도 == 순번(LATCHED 인터럽트는 없음), `REG_POWER`가 새 값. 실패면 `REG_RESULT` = 10(PRESENT_FAILED), IRQ_ERROR.

받는 조건: 상태가 UNREGISTERED/READY/ACTIVE이고, ACTIVE면 LATCHED == APPLIED (PRESENT가 끝나 있음). 아니면 BUSY(3, 순번 소비 안 함). 값이 2 이상이면 INVALID_COMMAND(1, 순번 소비). 켜기는 HPD 대기 최대 1 s + 100 ms + 모드 설정이 들어가므로 마감을 2 s 이상으로 잡아야 합니다.

첫 `REGISTER_POOL`(또는 `SET_POWER`)은 넘겨받기가 들어 있어 평소보다 깁니다(§3.2). KMD의 지금 마감 2000 ms(`scanout_windows.c:5`) 안에 들어갈 것으로 보지만 [가설] 하드웨어에서 로그의 `takeover ... in N us`, `modeset ... in N us`로 확인해야 합니다.

## 5. 런처

| 파일 | 바뀐 것 |
|---|---|
| `port-j274/gpu/hv/tools-hv2/agx_hv2_module.py` | `NWOAS_AGX_DCP_SERVICE`, `NWOAS_AGX_DCP_LATCH_US`, `NWOAS_AGX_DCP_NO_CLEAN`를 읽어 설정의 `flags`(0x2, 0x4)와 마지막 필드(`dcp_latch_us`, 예전 `reserved`)에 넣음. **셋 다 없으면 보내는 512바이트가 예전 모듈과 똑같음** [사실: 같은 가짜 입력으로 두 모듈을 돌려 sha256 `13ecfe5e…14355`(MEDIATE), `401d86d5…3538`(LOG) 일치]. 켰을 때만 로그 한 줄 추가 |
| 위 모듈을 지문으로 고정한 런처 5개 | `agxlog-s247`, `agxmed-s247`, `agxmedp-s247`, `agxmedq-s248`, `agxmed1g-s249`의 모듈 지문만 `674f396b…2b22` → `7af920a6…dcdb9`로 바꿈 (다른 줄 그대로) |
| `nwoas_scripts/agxdcp-s251-guest-test.sh` (새 파일, 실행하지 않음) | `agxmed1g-s249`와 차이: HV 경로·크기(2277376)·sha256, `export NWOAS_AGX_DCP_SERVICE=1`(+설명 주석 1줄). 로그 이름은 그대로 `agxmed1g-s249-…`로 생깁니다 |

두 런처의 지문 고정 32개를 실행 없이 모두 확인했습니다(전부 일치).

HV 쪽 설정 확인(`nwoas_agx_broker.c:146-159`): 모르는 플래그, 서비스 없이 `dcp_latch_us`/NO_CLEAN, 100000 µs 초과, MEDIATE가 아니거나 vINTID 0이면 설정 전체를 거절합니다.

## 6. 5단계에서 KMD가 바꿔야 할 것

1. J274 빌드의 소프트 브로커(`AdmissionScanoutSoft*`, P15-P19)를 그대로 두되, 시작할 때 진짜 MMIO로 `REG_ABI_VERSION`/`REG_CAPABILITIES`를 먼저 읽어 **v2이고 `(caps & 0x7f) == 0x7f`이고 `caps & 0x200`이면** 소프트 브로커를 끄고(풀·POST 화면 버퍼 `MmMapIoSpaceEx`, 복사 스레드, `SoftLatchDpc`를 만들지 않음) 모든 +0x400 접근을 진짜 MMIO로 보냄. 아니면 지금처럼 소프트 브로커(g249D/g250E 하이퍼바이저와 함께 쓸 때).
2. `RegisterPool(memory.GuestIpaAddress, memory.PoolBytes)`의 풀 크기가 정확히 0x3b800000(로컬 예약 1 GiB 기준)이어야 함. 다르면 INVALID_POOL.
3. LATCHED 인터럽트는 진짜 vINTID 889로 옴. ISR(`AdmissionScanoutProcessInterrupt` → `AppleAgxScanoutConsumeInterrupt`)은 그대로 쓰고, 소프트 경로가 만들던 가짜 LATCHED는 없앰.
4. P19(화면 숨김 → 검은색 복사)를 `CMD_SET_POWER 0/1`로 바꿈 (§4 순서, 새 대기 종류: 끝나면 상태가 명령 전 상태). 꺼져 있는 동안의 PRESENT는 받아 주므로 P18(숨김 중 화면 전환 완료 알림)은 LATCHED로 그대로 돌아감.
5. KMD는 POST 화면 버퍼에 더 쓰지 않음(하이퍼바이저가 RELEASE 때 그 화면으로 되돌림).
6. 첫 REGISTER_POOL 마감 2000 ms는 그대로 두되, 실패 시 로그에 `LastBrokerResult`를 남겨 넘겨받기 실패(MAP_FAILED, 9)와 구별.

## 7. 빌드·호스트 시험 (맥북, 2026-11-16)

명령: `bash port-j274/gpu/hv/tools-hv2/buildvar.sh $PWD/m1n1-gpu-hv2 <새 폴더> v1.0.2-1474-gbddf7f06-g251F [NWOAS_AGX=1]`

| 빌드 | rc | 경고 수 | 오류 | 크기 (바이트) | sha256 |
|---|---|---|---|---|---|
| g251F AGX `.bin` | 0 | 45 | 0 | 2,277,376 | `4f5c49767eb86b189bc9dc471789c9d2faf35f7ba841661cab7b76832a5fc939` |
| g251F AGX `.elf` | — | — | — | 3,547,048 | `4cf2d0c81bd804088cd8c6dc5f6bb3ced1c09377c4e5b507356b6644e958c2bc` |
| g251F 기본 `.bin` | 0 | 45 | 0 | 2,179,072 | `04ba3d4e7c12ecad000cd2b8e1523ac3e788bca167f8a8c127f91b99c1bff005` |
| 기본, g250E 태그로 다시 빌드 | 0 | 45 | 0 | 2,179,072 | `04ed18f2ea2c16ed6cab32ce1406900808c90bbb1c436669ddb80889268b2c37` (= g250E 기본) |

경고 45개는 파일·내용이 g250E와 같습니다(줄 번호 무시하고 비교, Makefile 줄 번호만 다름) [사실]. 빌드 로그: `out-hv2/build-g251F-agx.log`, `out-hv2/build-g251F-default.log`.

호스트 시험: `tools-hv2/hosttest/run.sh nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin` → rc 0, `ALL PASS: 0 failure(s)`, PASS 60개(기존 31 + 새 29). 결과: `out-hv2/hosttest-g251F-adt135.txt`.

새 시험(`hosttest/test_scanout_svc.c`)은 **KMD의 실제 클라이언트 코드**(`driver-p15/.../shared/src/apple_agx_scanout.c`, J274 빌드)가 하이퍼바이저 브로커+서비스와 MMIO 함수로 이야기하게 하고, DCP 쪽만 가짜로 둡니다. 확인한 것:

| 시험 | 결과 |
|---|---|
| 서비스 꺼짐: KMD Qualify → MissingCapabilities, +0x9c·명령 4 거절 (지금과 같음) | PASS |
| 서비스 켜짐: Qualify OK, v2, caps 0x27f | PASS |
| RegisterPool(2구역 전체): 넘겨받기 먼저, 두 DART에 3808 × 256 KiB, 26틱(모의 5.2 ms) | PASS |
| PresentApplied: 1920x1080 / 7680 / BGRA, IOVA 0x10000000 = PA 0xa00000000 | PASS |
| LATCHED 전 두 번째 PRESENT → BUSY, APPLIED 17,000 µs 뒤 LATCHED | PASS |
| QueuePresent → vIRQ 엣지 1번 → ConsumeInterrupt LATCHED, 지운 뒤 다음 래치까지 엣지 없음 | PASS |
| 2560x1600 표면, RGBA 표면 → INVALID_SURFACE | PASS |
| SET_POWER 0 → 상태 ACTIVE, APPLIED == LATCHED, 꺼진 동안 PRESENT 받음, SET_POWER 1, 값 2 → INVALID_COMMAND | PASS |
| Release: POST 화면 버퍼 복귀 후 두 DART 해제, IOVA 반납 | PASS |
| dart-dcp 매핑 실패 → MAP_FAILED, 매핑한 만큼 되돌림 / 넘겨받기 실패 → MAP_FAILED, 매핑 없음 / 풀 크기 다름 → INVALID_POOL, DCP 건드리지 않음 | PASS |

가짜 DCP로 상태 기계만 본 것입니다. 실제 DCP 응답, 화면 표시, 시간은 시험하지 못했습니다.

## 8. 맥미니 시험 계획 (콘치님 승인 후에만)

**준비**: 5단계 KMD(§6)가 아직 없으면 이 HV로 Windows를 부팅해도 KMD는 소프트 브로커를 씁니다. 그때 확인할 수 있는 것은 "켜 두어도 지금과 똑같이 부팅되는가"뿐입니다(1단계).

| 단계 | 할 일 | 모니터에서 보여야 하는 것 (사람이 판정) | 로그에서 함께 볼 것 |
|---|---|---|---|
| 1 | `agxdcp-s251-guest-test.sh` 실행, 지금 KMD(P19) 그대로 | 지금과 똑같이 부팅 로고 → Windows 로그인 → 바탕화면 (GPU 가속 화면은 지금처럼 소프트 복사) | `NWOAS-AGX: DCP scanout service armed`, **`takeover` 줄이 없어야 함**(지금 KMD는 진짜 브로커를 쓰지 않음) |
| 2 | 5단계 KMD 설치 후 재부팅 | 드라이버가 시작될 때 화면이 잠깐 깜빡일 수 있음. 그 뒤 Windows 바탕화면이 **계속** 보이고 창을 움직이면 따라 움직임 | `takeover: display_start_dcp()=0 in N us`, `modeset 1920x1080 60.00 Hz`, `DCP owned by the hypervisor`, `DCP guard: 5 guest ranges now absent`, `present #1..#4`, `scanout vINTID 889 injected` |
| 3 | 창을 빠르게 끌기 1분 | 찢어짐(위아래가 어긋난 줄)이 보이는지 사람이 봄. 보이면 실패로 적고 `NWOAS_AGX_DCP_LATCH_US=33000`으로 다시 | `present #1024`마다 평균·최대 µs, `failures 0` |
| 4 | 화면 끄기(절전 설정 "화면 끄기 1분") → 마우스로 깨우기 | 꺼질 때 모니터가 검거나 신호 없음, 깨우면 **다시 바탕화면** | `display power off requested/done`, `display power on ... done in N us` |
| 5 | 드라이버 다시 시작 또는 Windows 종료 | 종료 화면까지 보임 | `pool released, POST framebuffer shown again` |

로그만으로 통과라고 하지 않습니다. 2·4번은 모니터에 바탕화면이 보여야 통과입니다. 로그에 `DCP guard: guest ...` 줄이 나오면 누가 DCP를 건드린 것이므로 그 PC와 함께 기록합니다.

멈춰야 할 때: 2번에서 모니터가 검은 채 30초 넘게 그대로이거나 하이퍼바이저 로그가 멈추면 **맥미니 전원선을 10초 뽑았다 다시 꽂습니다**(§9 위험 1).

## 9. 위험

| # | 위험 | 대응 |
|---|---|---|
| 1 | DCP 주인이 둘이 되면 DCP 펌웨어가 죽고 전원을 완전히 끊어야만 돌아옴 [가설] | 넘겨받기는 게스트 m1n1이 끝난 뒤에만(§2.2), 넘겨받은 뒤 게스트 DCP/DART 접근은 가드가 막음, S250 시험 명령도 막음. 그래도 Mu·Windows 쪽에 DCP를 건드리는 것이 있는지는 하드웨어 로그(가드 줄)로만 확인됨 |
| 2 | 시간 기준 LATCHED: DCP가 실제로 바꾸기 전에 KMD가 옛 표면을 다시 쓰면 찢어짐 | 기본 17 ms, 런처로 늘릴 수 있음(§3.3). 진짜 래치 신호는 iBoot 경로에 없음 |
| 3 | HDMI 연결이 끊겼다 다시 붙음(모니터 끔·케이블) | 하이퍼바이저는 핫플러그 알림을 처리하지 않음. 다시 붙은 뒤 화면이 검으면 KMD가 SET_POWER 1(모드 다시 설정)을 보내야 함. 켜기 중 HPD가 1 s 안에 안 오면 실패(PRESENT_FAILED) |
| 4 | 넘겨받기 때 CPU0이 한 번 길게 멈춤(동기 `display_start_dcp`) | 0.1-0.3 s로 봄 [가설]. 로그 `takeover ... in N us`로 확인. 1 s를 넘으면 Windows 쪽 시간 초과가 생길 수 있음 |
| 5 | 켜기(SET_POWER 1) 뒤에도 검은 화면 (S250 §9 3번과 같은 일) | 이번 켜기는 모드를 다시 설정하고 표면을 다시 보냄. S250에서 `--reconfigure-after-power`는 아직 시험하지 않았으므로 이것도 처음입니다 |
| 6 | 1.25 GiB IOVA 창이 iBoot 매핑과 겹침 | `dart_map()`이 이미 있는 페이지를 거절 → REGISTER가 MAP_FAILED로 실패(겹쳐 쓰지 않음) |
| 7 | Windows나 Mu가 DISP0 전원 영역을 끔 | 확인 못 함 [가설: 그런 드라이버 없음] |
| 8 | 하이퍼바이저가 끝날 때 DCP를 재우지 않음 | 프록시 재부팅(`reboot.py`) 뒤 D4가 다시 처음부터 설정하므로 문제 없을 것으로 봄 [가설] |

## 10. 실기기 결과와 수정 (2026-11-16 저녁)

| 판 | sha256 앞 16자 | 바꾼 것 | 실기기 결과 |
|---|---|---|---|
| g251F | 4f5c4976… | 처음 판 | 넘겨받기 OK, 화면 켜기(SET_POWER 1)가 500 ms 안에 안 끝나 실패 |
| g252G | f990dbec… | 켜기·모드 설정 시간 한도 5 s, 실패 때 마지막 응답 값 출력 | 실패 줄에 `reply=0`: 시간 문제가 아니라 빈 응답을 실패로 본 것 |
| g252H | c3399ce3… | DCP가 빈 응답(결과 0)을 주면 성공으로 봄 | 넘겨받기·출력 4회·끄기/켜기 모두 OK. 약 45초 뒤 Windows가 0x124로 멈춤 |
| g252I | 4dbd40ab… | 화면 인터럽트(vINTID 889)를 같은 CPU에 처리 중(ACTIVE)인 인터럽트가 있으면 다음 틱(200 µs)으로 미룸, 최대 50 ms. 우선순위·미룬 횟수 출력 | 889 우선순위가 **0x00**(타이머 0x20)으로 찍힘. 처음 2분에 미룬 경우 8번 이상(모두 INTID 0, 0x20 처리 중). 45초 넘어 138초까지 멈춤 없음 |
| g252J | 004c474a… | 가상 인터럽트 장치 우선순위 버그 수정(아래). g252I의 미루기는 그대로 둠 | 30분 넘게 멈춤 없음. Windows가 쉬는 동안 화면 끄기 → 모니터 **신호 없음**, 키 입력 → 다시 켜짐(0.11~0.18 s, 콘치님 확인) |

### g252H 멈춤 원인 분석

- [사실] Windows 미니덤프의 WHEA 기록은 SEI(Arm SError) 한 건, ESR `0xbf40100b`, FAR `0x7ffd5fddddd0`.
- [사실] 이 ESR은 2026-09-07 S6-D49와 같은 값입니다. 그때 원인은 하이퍼바이저가 만든 인터럽트(698)를 강제 우선순위 0x00으로 넣어 타이머(18) 처리 중에 끼어들게 한 것으로, 두 LR이 동시에 ACTIVE였습니다(`NWOAS-STATUS-2026-09-06.md` S6-D46~D50).
- [사실] 하이퍼바이저 쪽 SError 처리(`hv_exc_serr` → 파이썬)는 이번에 불리지 않았습니다(호스트 기록에 "Guest exception" 없음). 즉 SError는 게스트가 직접 받은 것입니다.
- [사실] g252H의 889 주입은 우선순위는 게스트 설정값을 쓰지만, 다른 LR이 ACTIVE인지는 보지 않았습니다.
- [가설] 889가 다른 인터럽트 처리 중에 끼어들어 두 LR이 동시에 ACTIVE가 되면서 Apple vGIC가 SEI를 냈다. g252I의 로그(`S252I scanout irq deferred`, 우선순위 줄)와 멈춤이 없어지는지로 확인합니다.

### 진짜 원인: 가상 인터럽트 장치의 우선순위 읽기 위치 버그 (g252J에서 수정)

- [사실] `hv_vgic.c` 분배기 MMIO 처리는 GICD_IPRIORITYR 바이트 n을 INTID n 자리(배열 바이트 n)에 저장합니다. 그런데 `hv_vgic3_get_priority()`는 SPI를 `(intd - 32)` 자리에서 읽었습니다. 그래서 모든 SPI가 **32번 앞 INTID의 우선순위**를 받았습니다. 889번은 아무도 쓰지 않은 857번 자리의 0x00을 받았습니다.
- [사실] 이 함수는 화면 인터럽트만이 아니라 하드웨어 인터럽트를 Windows에 넘기는 일반 경로(`hv_exc.c` AIC→vGIC), USB(698)·900번 경로에서도 씁니다. 9월에 698이 0x80으로 "측정"된 것도 실제로는 666번 자리 값이었습니다.
- [사실] 바이트 단위 쓰기는 낱말(4바이트) 전체를 그 값으로 덮어써, 옆 INTID 세 개를 0으로 만들고 값도 엉뚱한 칸에 넣었습니다. g252J는 접근한 바이트만 고쳐 씁니다(읽기도 같은 방식).
- [가설→확인 중] 0x00으로 들어간 889가 IPI(0, 0x20) 처리 중에 끼어들어 두 LR이 동시에 ACTIVE → Apple vGIC SEI → Windows 0x124. g252J에서 `[vgic-prio] guest wrote INTID 889 priority ...`와 `S252I scanout prio=` 값이 같고, 멈춤이 없어야 확인입니다.

## 11. 축소 화면(S253, g253K)

Windows 드라이버가 1920x1080보다 작은 화면(예: 1280x720)을 내보내면, DCP가 그것을 패널 안의 사각형으로 늘려 보여 주게 했습니다. 모니터로 나가는 신호는 넘겨받을 때 고른 1920x1080 60 Hz 그대로입니다. 드라이버 쪽 모드 표는 다른 작업에서 엽니다.

### 바뀐 것

| 파일 | 내용 |
|---|---|
| `hv_agx_scanout_broker.h/.c` | 능력 비트 `CAP_SCALED_SOURCE`(1<<8), 레지스터 `DEST_ORIGIN`(+0xa0, X \| Y<<16)·`DEST_SIZE`(+0xa4, W \| H<<16), 창 크기 0xa0 → 0xa8. 두 레지스터는 처음에 0 / 1920x1080이고, 능력 비트가 없으면 읽기·쓰기 모두 거부(옛 창 밖과 같음). PRESENT 요청에 목적지 사각형을 실어 보냄 |
| 같은 파일, 검사 | 작은 원본: 너비 16의 배수, 높이 짝수, 너비·높이 0 아님·1920x1080 이하, stride = 너비x4, 크기 = stride x 높이(또는 64 KiB로 올림), 목적지는 비어 있지 않고 패널 안, 원본보다 작지 않음(늘리기만). 원본이 1920x1080이면 목적지는 0,0 1920x1080만. 어긋나면 `INVALID_SURFACE`(종결) |
| `nwoas_agx_broker.c` | 서비스를 켜면 능력 0x27f → **0x37f**. 목적지 사각형을 `display.c`로 넘김 |
| `display.c` (S251 구간) | `nwsvc_surface`에 목적지 사각형. 레이어의 src = (0,0,원본 W,H), dst = 목적지 사각형. 화면 다시 켜기(SET_POWER 1)의 재설정도 `nwsvc.active`의 사각형 그대로 다시 보임. 크기가 바뀔 때만 `display: NWOAS-DCP-S253 scanout source WxH stride S -> panel WxH at X,Y` 한 줄(최대 64줄). 넘겨받을 때 한 번, 모니터가 알려 준 타이밍 모드 전부(유효 여부·WxH·Hz)와 색 모드 전부를 한 줄에 4개씩 출력(타이밍 48줄·색 16줄까지) |
| `hv_agx_scanout_service.c/.h` | 바뀐 것 없음(요청을 그대로 넘김) |

서비스를 켜지 않은 판(능력 0)과, 1920x1080만 보내는 드라이버의 PRESENT는 DCP에 보내는 명령이 g252J와 같습니다(목적지 기본값이 전체 패널).

### 사각형 밖 검은색

- [사실] 우리 경로(iBoot `SWAP_SET_LAYER`)의 명령에는 배경색 칸이 없습니다(`dcp_iboot.c` `swap_set_layer_cmd`: 레이어, src, dst만). 위쪽 windows-on-m1은 IOMFB 경로의 swap 배경색으로 검게 칠했는데, 그 경로는 우리에게 없습니다.
- 선택: 레이어 하나만 씁니다. 바꾼 것은 dst 사각형뿐이라, 이미 실기기에서 된 명령 모양을 그대로 둡니다. 검은 작은 버퍼로 두 번째 레이어를 까는 방법은, iBoot가 layer_id 1을 받아 주는지 아무 근거가 없고 거부되면 모든 출력이 실패하므로 넣지 않았습니다.
- [가설] DCP는 프레임마다 레이어를 메모리에서 다시 읽어 합성하므로, 레이어가 덮지 않은 곳에 예전 화면이 남지는 않고 배경색(기본 검정으로 짐작)이 나옵니다. 검정이 아니면(쓰레기 화면·다른 색) 그때 두 번째 레이어 방법을 시험합니다.

### 시험·빌드 (맥북, 2026-11-16)

- 호스트 시험 `tools-hv2/hosttest/run.sh`: `ALL PASS: 0 failure(s)` (89개 PASS, 경고 0). 새 시험: KMD 클라이언트로 1280x720 늘리기 → dst 0,0 1920x1080, 1440x900 비율 맞춤 → dst 96,0 1728x1080(레지스터 다시 읽기 포함), 800x600 가운데 1:1(크기 그대로·64 KiB 올림 둘 다), 잘못된 모양 16가지 `INVALID_SURFACE`, 그 뒤 1920x1080 그대로, 능력 비트가 없으면 예전과 같이 작은 원본 거부.
- 빌드 `buildvar.sh … g253K NWOAS_AGX=1`: rc=0, 경고 44(g252J와 같음, 바뀐 파일에서는 0), 오류 0.

| 파일 | sha256 |
|---|---|
| `out-hv2/m1n1-g253K-agx-hv2-scaled.bin` (2293760바이트) | `8417ce167d2ccb3a523d1ccb4047fe70df249a81da233a152617d8478973bc7e` |
| `out-hv2/m1n1-g253K-agx-hv2-scaled.elf` | `245fcbba2bb88167f26c54dff3b9ecbad88e7e7a6a995e8b8c52204fb6c0527f` |

빌드 성공일 뿐, 실기기에서 작은 화면이 제대로 나오는지는 아직 모릅니다.

### 실기기에서만 알 수 있는 것

1. 넘겨받을 때의 `S253 monitor timing modes` / `color modes` 줄: 모니터가 실제로 받는 모드.
2. 작은 원본에서 DCP가 swap을 받아 주는가(`present FAILED`가 없어야 함). [가설] DCP 크기 조절기는 이 정도 늘리기(최대 1.8배, 800x600 → 1440x1080)를 받아 준다.
3. 사각형 밖이 검은가(위 [가설]).
4. 늘린 화질(흐림·계단)과 가운데 위치가 맞는가. `scanout source … -> panel … at X,Y` 줄과 화면을 맞대어 봄.
5. 작은 모드에서 화면 끄기/켜기 뒤에도 같은 사각형으로 돌아오는가.

### 11.1 실기기 결과 (2026-11-16 19:4x, g253K + 드라이버 2757)

| 시험 | 하이퍼바이저 기록 | 화면 (콘치님 확인) |
|---|---|---|
| Windows 해상도 목록 | — | 1920x1080, 1680x1050, 1600x900, 1440x900, 1280x800, 1280x720, 1024x768, 800x600 (8개) |
| 1280x720 | `source 1280x720 stride 5120 -> panel 1920x1080 at 0,0` | 화면 가득 정상 |
| 1024x768 | `source 1024x768 stride 4096 -> panel 1440x1080 at 240,0` | 양옆 필러박스, 정상 |

- [사실] 레이어 하나만 써도 사각형 밖이 필러박스로 보였습니다(§11의 가설 확인).
- [사실] DCP가 보고한 모니터 모드 33개에 3840x2160이 들어 있었지만 모니터 사양은 FHD입니다. 이 목록은 "DCP가 보고한 값"일 뿐 모니터 사양이 아닙니다.

## 12. 모니터 재연결 (S254, g254L)

- [사실] 2026-11-16 19:5x: 포터블 모니터의 전원(USB-C 충전기)이 다시 맞춰지며 모니터가 꺼졌다 켜진 뒤 "신호 없음"이 계속됨. 하이퍼바이저는 화면이 켜진 것으로 알고 있었음(§9 위험 3이 실제로 일어남). Windows가 화면을 껐다 켜자(전체 모드 설정) 되살아남.
- [사실] iBoot 통로에는 연결 변화 알림이 없고, ADT의 `dp2hdmi-gpio`에는 reset·force-dpfu만 있어 연결 신호선을 직접 받을 수 없음.
- 수정 `hv2-g253K-to-g254L.patch`: 화면이 켜져 있고 통로가 비어 있으면 1초마다 GET_HPD. HPD 0 → 1(또는 해상도 개수 변화)이면 하이퍼바이저가 켜기 과정(HPD → 켜기 → 모드 → 모드 설정 → 현재 화면 다시 보내기)을 스스로 다시 함. 그동안 들어온 화면 출력은 화면 꺼짐 때처럼 처리했다가 마지막에 보여 줌. 출력·끄기/켜기 요청이 GET_HPD와 겹치면 최대 20 ms 기다림. Windows의 켜기 요청이 겹치면 그 과정에 합류.
- `[vgic-prio]` 기록 줄은 처음 4번만.

| 판 | sha256 앞 16자 | 시험 | 결과 |
|---|---|---|---|
| g254L | 61e2e2ef… | 모니터 전원/HDMI 뽑았다 꽂기 2번 | `monitor disconnected (hpd 0, modes 33/0)` → `reconnected` → `reconnect modeset done in 315055 us (#1)`, `#2` 314978 us. 키 입력 없이 바탕화면 돌아옴(콘치님 확인) |

- [한계] 1초보다 짧게 끊겼다 붙어 HPD 0을 못 본 경우는 해상도 개수가 같으면 놓침.
