# S256 — Windows 소리용 런처 모듈 + 펌웨어(SPK0) + 런처

**상태(2026-11-18 14:12, S259f):** `20261118-1412-bridge` — RX 멈춤 조기 재시작·다리 놓기·RX SRAM 배정(10.10절). 맥미니에서는 실행하지 않았습니다.

**이전 상태(2026-11-18 14:03, S259e):** `20261118-1403-fixgain` — 보호가 설정되면 이득은 늘 0x14, 정적 모드는 볼륨 천장 0x20(10.9절). 맥미니에서는 실행하지 않았습니다.

**이전 상태(2026-11-18 13:52, S259d):** `20261118-1352-events` — 사건 기록, 이득 전환 페이드, 정적·보호 같은 크기(10.8절). 맥미니에서는 실행하지 않았습니다.

**이전 상태(2026-11-18 13:31, S259c):** `20261118-1331-clock` — 재생 위치를 하드웨어 시계로 맞춤(10.7절). 맥미니에서는 실행하지 않았습니다.

**이전 상태(2026-11-18 13:20, S259b):** 실기기 P1·P2에서 감지 수치는 모델과 맞았으나(Z 4.35 Ω, 비율 1.01~1.02) 재생 중 멈춤·팝이 났습니다. 원인 대응판 `20261118-1320-worker`를 만들었습니다(10.6절). 맥미니에서는 실행하지 않았습니다.

**이전 상태(2026-11-18 오후, S259):** 스피커 열 보호(V/I 감지 + 열 모델, 10절)를 넣은 드라이버 `20261118-1256-prot`과 머리말 버전 2 런처 모듈을 만들었습니다. 빌드·서명·단위 시험·모의 실행까지만 했고 **맥미니에서는 아무것도 돌리지 않았습니다.** 기본 단계(`NWOAS_AUDIO_PROT` 없음 = `sense`)에서는 이득이 0x00 그대로라 지금과 같은 크기입니다.

**이전 상태(2026-11-18 오전):** 준비 모듈은 실기기에서 실행됐습니다(2026-11-16 20:28, `logs/agxmed1g-s249-20261116-202802.1sXX3e`: 모든 단계 정상, 앰프 0x02=0x0e, `READY`). Windows 드라이버(9절)는 **빌드·서명·오프라인 검사만 했고 맥미니에 설치하지 않았습니다.** 빌드 성공은 소리 성공이 아닙니다.
계약: `research-20261116/sound/PLAN-S256.md`. 표기: 표시 없는 문장 = 파일·빌드·검사로 확인한 사실, [가설] = 확인 안 된 추정.

## 1. 만든 파일

| 파일 | 내용 |
|---|---|
| `sound-s256/audio_module.py` | `run_guest.py -m` 모듈. Windows 시작 전에 소리 경로를 준비하고 **아무것도 재생하지 않음** |
| `fw-audio-s256/acpi/NwoasAudioJ274Ssdt.asl`, `NwoasAudioJ274.asl.inc` | SSDT: `\_SB.SPK0`, `_HID "NWAU0001"`, 메모리 자원 5개(계약 순서), 인터럽트 없음 |
| `fw-audio-s256/build.py` | `fw-agx-s247/build.py` 사본 + 소리 SSDT 한 개. S247 입력(src 2개, AGX ASL 2개)은 읽기만 하고 해시를 고정. 모듈의 `S256_WIN_IPA`/`S256_WIN_SIZE`와 SSDT 자원 4가 다르면 빌드 거절 |
| `fw-audio-s256/out/s256/` | 빌드 결과(`m1n1-payload-s256-audio.bin`, `fd-audio.fd`, `result-audio.json`, `build-audio.log`) |
| `agxdcp-s256-guest-test.sh` | `agxdcp-s254-guest-test.sh` 사본. 바뀐 것은 머리 주석 3줄, 페이로드 경로·크기·해시, 모듈 해시 한 줄, 마지막 `-m .../sound-s256/audio_module.py` 한 줄뿐 |
| `agx-s248/run-reboot-arm-dcp256.sh` | `run-reboot-arm-dcp254.sh` 사본. 런처 이름과 출력 파일 이름(`launcher-agxdcp256-…`)만 바뀜 |
| 빌드 트리 `/Users/angigyeom/Desktop/DEV/NWOAS/fw-audio-s256-mu` | `fw-s237-mu`의 APFS 복제(venv 제외) + 빌드 결과. S247처럼 재빌드·AML 확인용으로 남김 |

## 2. 모듈이 하는 일 (순서)

레지스터 순서와 값은 실기기에서 소리가 난 `sound-s255/beep_tone.py`와 같습니다. 다른 점은 2.3에 있습니다.

### 2.1 A단계 — 창 준비 (실패하면 예외 → `--strict-init`으로 게스트 진입 전 중단)

| 줄 | 하는 일 |
|---|---|
| `A1` | 창 IPA `0x340000000`+`0x40000`가 아래 어느 것과도 겹치지 않는지 검사(3절) |
| `A2` | 프록시 힙에서 0x40000 바이트(16 KiB 정렬, 물리적으로 연속) 할당. 힙 안·게스트 RAM 시작(`hv.phys_base`) 아래·어떤 추적 구역에도 안 보임·carveout 밖인지 검사 |
| `A3` | 버퍼 0으로 채움, 머리말(상태 0) 씀, 캐시 비움(`dc_civac`), `hv.map_hw(0x340000000, 버퍼, 0x40000)`, 머리말 되읽기 |

A단계를 예외로 멈추게 한 이유: SSDT가 이미 Windows에 창 주소를 알려 주므로, 매핑 없이 부팅하면 드라이버가 매핑 안 된 IPA를 읽게 됩니다. [가설] 그 경우 하이퍼바이저 쪽 처리되지 않은 데이터 중단으로 게스트가 멈출 수 있습니다.

### 2.2 B단계 — 하드웨어 (실패하면 앰프 끄고 머리말 상태 0, Windows는 그대로 부팅)

| 줄 | 하는 일 |
|---|---|
| `B0` | 계약 자원 0~3(I2C1·ADMAC·MCA 클러스터·switch1)이 실시간 ADT reg와 같고 통과 매핑(`HW`)인지, GPIO 181 주소(0x23c1002d4)가 ADT gpio 기준과 같은지 검사 |
| `B1` | `pmgr_adt_clocks_enable`: i2c1, admac-sio, dart-sio, mca-switch, mca0, mca1(mca0/1은 clock-gates 없음 — S255와 같은 무해한 오류) |
| `B2` | MCA 클럭 먹스(0x23b0400d4…) 5+min(4,n) |
| `B3` | AUDIO_P(0x23b700228) 리셋, PMGR 상태 기록 |
| `B4` | DART-sio **2번 통로만** 초기화 → IOVA `0x80000000`+0x40000 → 버퍼 PA. 페이지 표를 다시 읽어 변환 확인, 0·1번 통로 TCR/TTBR이 그대로인지 확인 |
| `B5` | ADMAC TX 채널 0: 끄기, 리셋(burst 0xc00060), 32비트 워드, 1워드 프레임. **켜지 않음**, 기술자 넣지 않음 |
| `B6` | NCO 0 = 48000×256 = 12.288 MHz, 켜기 |
| `B7` | MCA 클러스터 0: SYNCGEN·MCLK 켬, SERDES TXA 설정하되 **EN=0**, 포트 0 클럭·데이터 선택, switch1[0] = 0x102040(2026-11-18 S258 수정, TX_LSB_PAD 0) |
| `B8` | GPIO 181: 0x76a02 → 0x76a03(앰프 하드웨어 리셋 해제) |
| `B9` | 앰프: 0x05=0x50 먼저 쓰고 되읽기 → 0x02=0x0e, 설정 쓰기(0x08, 0x0a~0x0c, 0x1b~, 0x16~, 0x0d), 0x03=0x00, 0x05=0x50, 0x02=0x0e → 0x02/0x03/0x05 되읽기 |
| `B10` | 최종 확인: SERDES EN=0, DART-sio ERROR 기록 |
| `LAYOUT` / `HEADER` / `READY` 또는 `NOT READY` | 최종 배치와 머리말 값 |

B단계 실패 시(`FAIL` 줄): 앰프 0x02 ← 0x0d → 0x0e, (B0 통과 뒤면) GPIO 181 ← 0x76a02(하드웨어 종료), ADMAC 채널 0 끄기, SERDES EN=0. 머리말 상태 bit0 = 0이라 드라이버는 시작을 거절해야 합니다(계약).

**이 모듈에서 앰프 0x02에 쓰는 값은 0x0e와 0x0d뿐입니다. 0x0c(동작)는 쓰지 않습니다.** 볼륨 0x05에는 0x50만 씁니다.

### 2.3 beep_tone.py와 다른 점

| 항목 | beep_tone (S255, D4 프록시) | audio_module (S256, 하이퍼바이저 아래, 게스트 진입 직전) | 이유 |
|---|---|---|---|
| 재생 | 소리 3번 | 없음 | 계약 |
| DART-sio | `dart.initialize()` — 16개 통로 전부 리셋(0=SIO, 1=AES, 15=우회 포함), UNK1(0xf8)=0, ENABLED_STREAMS=0 | 2번 통로 TCR/TTBR만, ERROR는 FLAG가 있을 때만 지움, ENABLED_STREAMS는 2번 비트만 더함 | 아래 |
| ADMAC 객체 | DART를 넘겨 4 MiB 임시 버퍼를 매핑 | DART 없이 만들고, 고정 IOVA에 창 버퍼만 매핑 | 계약의 고정 IOVA |
| 앰프 0x02 | 0x0c(동작)까지 | 0x0e 유지 | 계약 |

**DART 초기화 판단:** S255에서 `initialize()`는 D4에서 하이퍼바이저·게스트가 올라오기 전이라 다른 사용자가 없었습니다. S256 모듈은 게스트 진입 직전에 돌고, ADT의 SIO는 `pre-loaded = 1`이며 DART-sio 0번 통로를 씁니다(windows-side.md 1.4 표). ADMAC에 필요한 것은 2번 통로뿐이므로, SIO·AES 통로를 지우는 전체 초기화는 쓸 이유가 없어 2번만 다룹니다. 0·1번 통로 TCR·TTBR0이 그대로인지 모듈이 확인합니다.
[가설·위험] 전체 초기화가 하던 UNK1(0xf8)=0 쓰기를 하지 않습니다. 이 레지스터 뜻은 모릅니다. 2번 통로 DMA가 이 값에 영향을 받으면 드라이버 재생 때 ADMAC 오류·DART ERROR로 드러납니다. 모듈은 B4에서 원래 값을 기록합니다.

## 3. 창 IPA `0x340000000` (+0x40000)와 안전 근거

| 대상 | 범위 | 근거 | 겹침 |
|---|---|---|---|
| 게스트 저주소 RAM 창 | 0 .. 0x100000000 | `usb-s174/guest_map.py` | 없음 |
| DRAM(게스트 고주소 RAM 1:1 포함) | 0x800000000 .. 0xc00000000 | `u.ba` (실기 로그 `Physical memory: 0x83c230000 .. 0xbe1000000`) | 없음 |
| Python 추적 구역 전부(`hv.mmio_maps`) | HW `/arm-io` 범위 0x200000000..0x300000000, 0x380000000..0x780000000, VUART, PMGR, NVMe 0x700000000.., PCIe 0x6c0000000/0x681000000, USB-C 0x502280000, RAM-LOW/HIGH | 실기 로그 `agxmed1g-s249-20261116-200950.MkomCJ` 252-262, 339-377줄. 0x300000000..0x380000000 사이에는 `PT[...]` 줄이 없음 | 없음(모듈이 실행 때 다시 검사) |
| AGX 중개 페이지(C HV) | 0x300000000 + 0x1000 | `m1n1-gpu-hv2/src/hv_agx_g2.generated.h:18-19` | 없음 |
| vGIC(T8103) | dist 0xF00000000, redist 0xF10000000+0x20000×8, ITS 0xF20000000 | `m1n1-gpu-hv2/src/hv_vgic.c:74-82, 1998-2003` | 없음 |
| AGX 로컬 예약·스캔아웃 풀 | 0xa00000000 + 1 GiB | 런처 `NWOAS_AGX_LOCAL_IPA`, 실기 로그 | 없음 |
| AGX 풀 창 | 꺼짐(`NWOAS_AGX_POOL_IPA=0`, 로그 "pool window: none configured") | 런처, vuart 로그 | 해당 없음(켜지면 모듈이 검사) |
| MCC carveout | 실행 때 읽음 | `p.mcc_get_carveouts()` | 모듈이 검사 |
| 36비트 게스트 주소 한도 | < 0x1000000000 | — | 안쪽 |

- C 하이퍼바이저 소스에서 0x3xxxxxxxx 상수는 중개 페이지 하나뿐입니다(grep). 0x382f…/0x39…는 `/arm-io` 범위 안 장치입니다.
- `hv.start()`의 `pt_update()`는 추적 구역 **안의** 빈칸만 매핑 해제합니다(`m1n1/hv/__init__.py:381-402`). 창은 어떤 추적 구역에도 없으므로 모듈이 한 `map_hw`가 지워지지 않습니다. 단 나중에 누가 창 위에 추적 구역을 더하면 덮어쓰입니다(지금은 없음).
- 하이퍼바이저 매핑 속성은 `PTE_MEMATTR_UNCHANGED`라 Windows 쪽 매핑 속성(드라이버가 캐시 없이 매핑)을 따릅니다.
- Windows가 RAM 밖 고주소 메모리 자원을 받아들이는 것은 AGX0의 0x300000000 자원으로 이미 확인된 방식입니다(GPU 드라이버가 그 자원으로 동작) [사실: 상태 메모리]. 같은 범위 안 0x340000000은 실기기에서 확인 전입니다.

버퍼 PA는 프록시 힙(로그 기준 0x80cd64000..0x83cd64000, `heap_limit` 위) 안이고 게스트 RAM(0x83cd64000~) 아래라 Windows RAM과 겹치지 않습니다. C 쪽 AGX 힙은 `heap_limit` 아래를 씁니다(로그 `heap_limit=0x80cd64000`).

## 4. 펌웨어

| 항목 | 값 |
|---|---|
| 빌드 명령 | `python3 -B nwoas_scripts/fw-audio-s256/build.py --tree fw-audio-s256-mu --variant audio --out nwoas_scripts/fw-audio-s256/out/s256` (이 맥에서, S247과 같은 toolbin·venv, 약 1분 20초) |
| 페이로드 | `fw-audio-s256/out/s256/m1n1-payload-s256-audio.bin`, 33669120 바이트 |
| 페이로드 sha256 | `3cb9e2df09462323cefe62900241ae95b9b123a0f47a75149323b600e18a0a3a` |
| FD sha256 | `d92e732af6156bfe9764769fd8f7bda07f4222759290f82ef163b831186457d4` |
| 소리 SSDT AML | 322 바이트, sha256 `6621c8591f95f91bcb3ad8ef88e9f99d93d349870d030a4bf651b7a4f646ae9d`, `NWAU0001`·`SPK0`·창 주소 바이트 포함 |
| AGX SSDT AML | `15347541…` = S247과 같음 |
| iasl | 소리 SSDT 오류 0·경고 0. 경고 1개는 기존 DSDT(S247과 같음) |
| S247 대비 | guest m1n1·DTB(0x0..0x21c000) 같음, 첫 차이 0x2251AF(FD 안) |

SSDT 자원(순서 고정): 0 I2C1 0x235014000+0x4000 · 1 ADMAC 0x238200000+0x34000 · 2 MCA 클러스터 0x238400000+0x18000 · 3 MCA switch1 0x238300000+0x30000 · 4 창 0x340000000+0x40000. 0~3은 실시간 ADT(`agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin`)와 일치하는 것을 모의 실행으로 확인했고, 모듈이 부팅마다 다시 확인합니다.

## 5. 고정값(sha256)

| 파일 | sha256 |
|---|---|
| `sound-s256/audio_module.py` | `0cb7fdb5f2afc2ce272a659a8f8d39b59a8ecb50fefa980b8929bdafd2596356` (런처 `agxdcp-s256-guest-test.sh` 82행에 고정, S259 머리말 v2) |
| `agxdcp-s256-guest-test.sh` | `72b4a1ccde4463db3e53ed9f507c8ffd1b5ffddaf4bbe45909d518065cf5e843` |
| `agx-s248/run-reboot-arm-dcp256.sh` | `a2fe04558979745bbc300afc93b5c9a85d24b4aec466c17aef146abe93f735b7` |
| `fw-audio-s256/build.py` | `cd62b9f25126162421d1479d1e3e840a761e58277fa2320da082711e16a483c8` |
| `fw-audio-s256/acpi/NwoasAudioJ274.asl.inc` | `bb55894116caadde800a675b3c7d4645d50503b7c67134d902c21806d30eff02` |
| `fw-audio-s256/acpi/NwoasAudioJ274Ssdt.asl` | `830227167dea1deb66821f884d2abce5261506dc734b017d81494cc2917684c9` |

모듈을 고치면 런처의 고정값도 바꿔야 합니다. 창 주소를 바꾸면 SSDT·펌웨어를 다시 빌드해야 합니다(build.py가 불일치를 거절).

## 6. 오프라인 검사 결과

| 검사 | 결과 |
|---|---|
| 런처의 `[ "$(…" ]` 고정값 33줄(변수 대입 + 대괄호 줄만 뽑아 실행) | 33개 모두 통과. 장치 존재 검사(`[ -e "$DEV" ]` 등)는 맥미니 연결 검사라 실행하지 않음 |
| `bash -n` 런처 | 통과 |
| 모의 실행(실시간 ADT + 메모리·I2C·TAS 레지스터 흉내, 실제 m1n1 Python 클래스) | 정상 경로: 머리말 상태 1, 앰프 0x02 쓰기는 0x0e뿐, SERDES 켜짐 없음, ADMAC 시작 없음, 볼륨 최소 0x50. 볼륨 되읽기 실패 경로: 상태 0, 0x0d→0x0e, GPIO 181 낮춤. 겹치는 창(0x300000000, 0x238400000, 0x900000000, 0x80000000, 0xF10000000)은 모두 거절 |

모의 실행은 논리·이름 오류 검사일 뿐, 실제 하드웨어 응답(I2C ACK, DART 잠금, PMGR 상태)은 흉내입니다.

## 7. 실기기에서만 확인되는 것

1. 모듈 로그에 `READY`와 `B4 … page tables re-read: ok`, `B9 amp after config: 0x02=0x0e 0x03=0x00 0x05=0x50`이 나오는지. `FAIL`이면 그 줄이 원인입니다.
2. 모듈 실행 뒤 Windows 바탕화면·GPU 화면·USB·NVMe가 S254와 같이 나오는지(소리 블록 리셋·DART-sio 2번 통로 변경이 다른 장치에 영향 없는지).
3. 부팅 중·바탕화면에서 스피커에 소리(팝·잡음 포함)가 없는지 — 앰프는 소프트웨어 종료 상태여야 합니다.
4. [가설] guest m1n1·Mu·Windows가 모듈이 켜 둔 전원(AUDIO_P 등)이나 DART-sio 2번 통로를 건드리지 않는지. guest m1n1의 `pmgr_init`은 활성 장치의 부모를 켜기만 합니다(`m1n1_windows/src/pmgr.c:397` 이하). 실기기에서는 드라이버가 PMGR·DART 값을 레지스트리에 기록해 확인해야 합니다.
5. [가설] DART-sio가 잠겨 있지 않은지(잠겨 있으면 B4가 `TCR2 write did not stick`으로 실패 → 상태 0).
6. 드라이버가 창을 읽어 머리말 magic/version/bit0을 확인하는지(다른 담당).

## 8. 남은 위험

| 위험 | 내용 |
|---|---|
| DART 부분 초기화 | 2.3 끝. 재생 단계에서만 드러남 |
| 클럭이 계속 나감 | MCLK·SYNCGEN이 부팅부터 켜져 포트 0에 BCLK/FSYNC가 계속 나갑니다(S255와 같은 설정, 앰프는 종료 상태). [가설] 무해하나 전력이 조금 듭니다 |
| 드라이버 쪽 잘못된 레지스터 쓰기 | 소리 블록은 통과 매핑이라 하이퍼바이저가 막지 않습니다. 잘못된 쓰기는 SError가 될 수 있습니다(CLAUDE.md 프론티어 설명). 0x02=0x0c 조건·0x05 하한은 드라이버가 지켜야 합니다 |
| PMGR 직접 쓰기 | AUDIO_P(0x23b700228)는 하이퍼바이저가 가로채지 않습니다(가로채는 것은 SIO_BUSIF·SIO 등 UART0 부모 칸). 드라이버가 리셋하면 모듈 설정이 사라집니다 |
| 창 위 추적 구역 | 앞으로 다른 모듈이 0x340000000을 덮는 추적 구역을 더하면 매핑이 바뀝니다. 이 모듈은 마지막에 실행되며 실행 시점에 검사합니다 |

## 9. Windows 드라이버 `ACPI\NWAU0001` (2026-11-18, 실기기 미검증)

결론: 서명된 설치 패키지가 만들어졌고, 설치·상태·재생 작업과 전달 매니페스트가 준비됐습니다. **설치도 재생도 아직 하지 않았습니다.** 소리가 나는지는 실기기에서 콘치님 귀와 드라이버 카운터로만 확인됩니다.

### 9.1 파일

| 파일 | 내용 |
|---|---|
| `port-j274/sound/nwoas-audio/Source/Main/nwau_hw.cpp` | 하드웨어 계층: 자원 5개 매핑(캐시 없음), 머리말 검사, I2C1 폴링(앰프), ADMAC TX 기술자·보고, MCA TXA 직렬기 켜기/끄기, 10 ms 타이머, 레지스트리 기록 |
| `.../Source/Main/nwau_hw.h` | 위 함수 선언(`NwAuPowerUp` 추가) |
| `.../Source/Main/NwoasAudio.inx` | INF 원본(`ACPI\NWAU0001`, MEDIA 클래스, 렌더 끝점 하나, 서비스 `NwoasAudio`) |
| `.../Source/Main/adapter.cpp` | `StartDevice` → `NwAuStart`, 마이크 끝점 설치 제거, 제거·정지·언로드 → `NwAuShutdown` |
| `.../Source/Main/minwavertstream.cpp/.h` | RUN → `NwAuStreamRun`, PAUSE/STOP/소멸 → `NwAuStreamPause`, 렌더 타이머가 1 ms마다 `NwAuRenderPush`+`NwAuService`, EoS 끝 → `NwAuStreamStarved` |
| `.../Source/Main/common.cpp` | 볼륨 노드(0) → `NwAuSetVolumeLevel`, 음소거 노드(1) → `NwAuSetMute`, D0 밖 → `NwAuPowerDown`, D0 → `NwAuPowerUp` |
| `.../Source/Filters/minipairs.h` | 마이크 배열 끝점 제거(렌더만) |
| `.../Source/Utilities/kshelper.cpp` | ALL_CHANNELS 반복 버그 수정(`i < ulChannel` = 2^32−1번 → `i < MaxChannels`), 볼륨·음소거 2곳 |
| `.../Source/Utilities/hw.cpp` | Windows에 처음 알리는 볼륨 −16 dB(앰프 0x50) |
| `.../Source/Main/Main.vcxproj`, `SimpleAudioSample.rc` | 새 소스 2개, 출력 이름 `NwoasAudio.sys` |
| `.../build-audio.sh`, `.../build/build-audio.ps1` | 맥 → VM 빌드·서명·수집 한 번에 |
| `sound-s256/make_tone_wav.py`, `media/nwoas-tone-quiet.wav` | 시험 소리(48 kHz·16비트·스테레오, 440 Hz, −30 dBFS, 1.5초) |
| `sound-s256/jobs/A1-install-audio.cmd`, `A2-status.cmd`, `A3-play.cmd` | 설치 / 상태 / 재생 작업 |
| `file-delivery-s243/manifests/snd-j274-20261118-1131.json`, `jobs/snd-j274-20261118-1131.cmd` | 전달 매니페스트·전달 작업(파일 4개 → `C:\NWOAS\drop\snd-j274-20261118-1131`) |
| `sound-s256/run-install-audio.sh` | 재시작(agxdcp-s256 + 매니페스트) → 전달 → D28 → A1 → A2 [→ A3] |

### 9.2 빌드 결과 (stamp `20261118-1131`)

| 항목 | 값 |
|---|---|
| 명령 | `bash port-j274/sound/nwoas-audio/build-audio.sh` (단위 시험 → rsync → `prlctl exec "Windows 11"` → 결과 복사) |
| 단위 시험 | 60116개 검사, 실패 0 |
| MSBuild | Release·ARM64, `/W4 /WX`, 오류 0·경고 0 |
| infverif `/w` | INF is VALID |
| 서명 | `.sys`·`.cat` 모두 `WDKTestCert SYSTEM,134392251612905395`(SHA1 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF`, GPU 패키지와 같은 인증서). `.sys`는 `.cat` 안에 있음. `verify /pa`는 테스트 루트라 "신뢰하지 않는 루트"로 실패(GPU와 같음) |
| PE | AA64 (ARM64), 가져오는 모듈 HAL·portcls·WDFLDR·ntoskrnl |
| 패키지 | `port-j274/sound/nwoas-audio/out/20261118-1131/package/` (공유 폴더 `/Volumes/media/nwoas/audio-build/out/20261118-1131/`) |

| 파일 | 바이트 | sha256 |
|---|---|---|
| `NwoasAudio.sys` | 80736 | `dab60f39fd4cc372b3eeb39de725019239c786c9cadb5c6cbe12e6fd297232ff` |
| `NwoasAudio.inf` | 3585 | `b3edb2ac5dd33fca17013b4b6261f6f97ec1a1253f82d6e4f7cb8253b6cafe60` |
| `nwoasaudio.cat` | 2603 | `cafaf2e1d925e95a65753599f848a876d6e0750e1ea57d5fb9853955a51e7ac5` |
| `nwoas-tone-quiet.wav` | 364844 | `ff995663f61f8cf0f2eb8898858575dc7766feb48327fa66edb344ae4e3ad096` |

### 9.3 드라이버가 하는 일

| 때 | 순서 |
|---|---|
| 장치 시작 | 메모리 자원이 정확히 5개이고 계약 주소·크기인지 → 5개를 캐시 없이 매핑 → 머리말 12워드를 32비트 읽기로 읽고 magic·version·상태 bit0·고리·IOVA·속도·형식·채널·클러스터·앰프 주소·볼륨 상한 검사 → **여기까지는 하드웨어 레지스터를 건드리지 않음** → ADMAC 채널 끄기, 직렬기 끄기, 앰프 0x02 읽어 0x0e가 아니면 0x0d→0x0e, 그래도 0x0e가 아니면 시작 실패 → 볼륨 0x05 쓰기 |
| 재생 시작(RUN) | MCA MCLK·SYNCGEN이 켜져 있는지(꺼져 있으면 거절) → 앰프 설정(S256 모듈 B9와 같은 바이트, 0x03=0x00, 0x05) 되읽기 → ADMAC 채널 리셋 → 0으로 채운 기술자 3개(약 64 ms) 넣고 채널 켜기 → 직렬기 켜기 → 0x02=0x0c → 되읽기 |
| 재생 중 | 렌더 타이머 1 ms: Windows 버퍼의 지난 만큼을 모노 24비트(32비트 상위 정렬)로 바꿔 고리에 씀, 보고를 읽고 기술자 보충(최대 4개, 2개 밑이면 0으로 채움). 드라이버 자체 타이머 10 ms도 같은 일을 함 |
| 멈춤 | 정지·일시정지·스트림 소멸·절전(D0 밖)·장치 제거·언로드·고갈(대기 기술자 0)·Windows 데이터 300 ms 없음·EoS 끝: 앰프 0x0d → 0x0e, ADMAC 끄기, 직렬기 끄기 |

안전 규칙은 앰프에 쓰는 유일한 함수 `AmpWriteGated` 한 곳에서 검사합니다: 0x05 < max(머리말 상한, 0x28) 거절, 0x03 ≠ 0x00 거절, 0x02=0x0c는 직렬기 EN 되읽기·DMA 켜짐·대기 기술자 1개 이상일 때만, 0x02에는 0x0c/0x0d/0x0e만, 0x02~0x05에 걸치는 여러 바이트 쓰기와 0x00(page)·0x7f(book) 쓰기 거절.
앰프가 이미 0x0e로 되읽히는 경우(장치 시작, 쉬고 있을 때의 절전)에는 0x0d를 건너뜁니다. 꺼진 앰프를 잠깐 음소거 상태로 켜지 않기 위해서입니다. 재생 중이었거나 되읽기가 0x0e가 아니면 항상 0x0d → 0x0e입니다.

### 9.4 레지스트리 (`HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWAU0001\0\Device Parameters`)

| 값 | 뜻 |
|---|---|
| `NwAudioStartStatus` | 0 = 정상. 0x101 자원 개수, 0x110+n 자원 n 불일치, 0x201~0x20b 머리말(0x203 = 런처 준비 안 됨), 0x2f0 고리 값, 0x300+n 매핑 실패, 0x310 메모리, 0x320 앰프가 0x0e가 아님 |
| `NwAudioHeader`(이진 48바이트), `NwAudioWindowIpa` | 드라이버가 읽은 머리말, 창 주소 |
| `NwAudioState` | 0 시작 안 됨, 1 대기, 2 재생, 3 고장으로 멈춤, 4 절전 |
| `NwAudioLastRunStatus` | 0 정상, 0x401 MCA 클럭 꺼짐, 0x402 앰프 설정 되읽기, 0x403 기술자 못 넣음, 0x404 0x0c 거절/I2C, 0x405 0x0c 되읽기 실패 |
| `NwAudioLastStopReason` | 1 스트림, 2 고갈(DMA가 다 비움), 3 Windows 데이터 없음, 4 절전, 5 종료·제거, 6 앰프 확인 실패 |
| 카운터 | `Starts`, `Stops`, `StartFails`, `DescSubmitted`, `DescCompleted`, `StrayReports`, `ReportFaults`, `RingErrors`, `PadChunks`, `PadBytes`, `PushBytes`, `DropBytes`, `DmaRanDry`, `OsStarved`, `I2cErrors`, `AmpVerifyFails`, `AmpState`(마지막 0x02), `VolReg`(마지막 0x05), `MaxServiceGapUs`, `OfUf`, `Switch1`(앞에 `NwAudio`가 붙음) |

카운터는 장치 시작·재생 시작·멈춤·절전·종료 때와 고장 직후(작업 항목)에만 기록됩니다. 재생 중 값은 다음 멈춤 뒤에 보입니다.

### 9.5 설치·시험 방법 (오케스트레이터가 실행)

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
# 지금 도는 런처의 .out 과 LOG.link 를 넘김. Windows를 재시작해 agxdcp-s256(소리 모듈 포함)으로 다시 올림
bash sound-s256/run-install-audio.sh <현재 launcher .out> <현재 LOG.link>          # 설치 + 상태
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/A3-play.cmd   # 그다음 재생(또는 위 명령 끝에 play)
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/A2-status.cmd # 재생 뒤 카운터
```

| 단계 | 기대 |
|---|---|
| 런처 로그 | `[S256-audio] READY` |
| 전달 | `S243 READBACK MATCH` 4줄 |
| A1 | `DEV OK problem=0`, `NwAudioStartStatus 0x0`, `NwAudioAmpState 0xe`, `NwAudioVolReg` ≥ 0x30 |
| A2 | `EP OK ... Speakers` 같은 소리 끝점 한 줄 |
| A3 | `PLAYSYNC OK` 약 1900 ms, AFTER에서 `Starts`·`DescSubmitted`·`PushBytes` 증가, `AmpState=e`(멈춘 뒤). 소리는 콘치님 귀로만 확인 |
| 되돌리기 | `pnputil /delete-driver oemNN.inf /uninstall`(oemNN은 A1 출력) 또는 S254 런처로 부팅(장치 자체가 없어짐) |

### 9.6 남은 위험 [가설 포함]

| 위험 | 내용 |
|---|---|
| 캐시 일관성 | 런처는 머리말·0으로 채운 고리를 프록시(EL2, 캐시 있음)로 쓴 뒤 `dc_civac`로 메모리까지 밀어냈고(A3·최종 머리말), 그 뒤로는 그 버퍼를 쓰지 않습니다(코드 확인). 하이퍼바이저 매핑 속성은 `PTE_MEMATTR_UNCHANGED`라 Windows 쪽 속성을 따르고, 드라이버는 `PAGE_NOCACHE`(장치 메모리)로 매핑해 32비트 단위로만 읽고 씁니다. 그래서 머리말 읽기와 고리 쓰기는 메모리로 바로 갑니다. 남는 경우 [가설]: m1n1의 캐시 있는 매핑으로 투기적 읽기가 고리 줄을 (깨끗한 상태로) 캐시에 올리고, ADMAC이 캐시를 엿보는(coherent) 장치라면 예전 0을 읽어 소리가 안 나거나 끊길 수 있습니다. 큰 소리 쪽 위험은 아닙니다. MSVC에 `dc civac` 내장 함수가 없어 드라이버에서 캐시 정리는 하지 않았습니다(필요하면 ARM64 어셈블리 파일 하나 추가) |
| 시계 차이 | 스트림 위치는 Windows 시계(QPC)로, 소비는 NCO(48 kHz)로 갑니다. S243 전달 때 게스트 시계가 약 10배 느리게 간 적이 있습니다. 그런 일이 생기면 데이터가 늦어 고갈 → 앰프 끔(`DmaRanDry`)으로 끝납니다. 다음 단계는 ADMAC 보고로 위치를 알리는 것 |
| 세션 0 재생 | A3는 작업 통로(SYSTEM, 세션 0)에서 `Media.SoundPlayer`로 재생합니다. 세션 0 프로세스의 재생이 막히면 `PLAYSYNC FAIL`이나 카운터 변화 없음으로 드러납니다 |
| I2C 오류 비트 | 쓰기 뒤 SMSTA의 MTN/MTO/MTA를 오류로 봅니다(i2c.py 이름 기준). 정상 전송에서도 켜진다면 `I2cErrors`가 늘고 재생이 거절됩니다(안전한 쪽) |
| ADMAC 보고 bit26 | admac.py의 "memory access fault?" 추정 비트를 `ReportFaults`로 셉니다. 뜻은 확인 안 됨 |
| DART 부분 초기화 | 8절 그대로. 재생 때 `ReportFaults`·`RingErrors`로 드러날 수 있음 |
| 잘못된 레지스터 쓰기 | 소리 블록은 통과 매핑이라 하이퍼바이저가 막지 않습니다. 드라이버는 머리말 bit0(런처가 전원을 켬)을 확인한 뒤에만 레지스터를 건드립니다 |


## 10. 스피커 열 보호 (2026-11-18 S259, 실기기 미검증)

결론: 앰프의 V/I 감지를 받아 speakersafetyd(J274)와 같은 수치의 열 모델을 드라이버 안에서 돌리고, 모델이 정상일 때만 이득을 21 dB로 올립니다. 감지가 없거나 늦거나 이상하면 그 재생은 지금의 고정 상한(이득 0x00, 볼륨 ≥ 0x0c = 풀스케일 +5 dBV)으로 돌아갑니다. 계약은 `research-20261116/sound/PLAN-S256.md` "머리말 버전 2"·"V/I 감지와 열 보호", 하드웨어 근거는 `hw-path.md` 7절입니다.

### 10.1 바뀐 파일

| 파일 | 내용 |
|---|---|
| `port-j274/sound/nwoas-audio/Source/Main/nwau_core.h/.c` | 머리말 v2 검사, 앰프 쓰기 게이트(`nwau_amp_write_allowed`), 정수 연산 exp·10^x·제곱근, 열 모델(`nwau_ts_*`), 감지 판정 |
| `.../Source/Main/nwau_hw.cpp` | RX 수집(ADMAC RX 채널, MCA RXB), 모델 연결, 이득 0x00↔0x14 순서, 볼륨 천천히 올리기, 레지스트리 값·열 상태 저장/복원 |
| `.../Source/Main/nwau_hw.h` | 정지 사유 9(열 한계) |
| `.../tests/test_nwau_core.c`, `tests/test_thermal.inc` | 머리말 v2, 게이트 전수, 계단 응답, 식힘, 되돌림 경로, 상한, 20분 풀스케일 사인 폐루프 |
| `sound-s256/audio_module.py` | 머리말 v2, S1~S4(감지 경로 준비), `NWOAS_AUDIO_PROT` 단계 |
| `agxdcp-s256-guest-test.sh` | 모듈 지문 한 줄 |
| `sound-s256/jobs/prot-tone.ps1`, `P1-sense.cmd`, `P2-moderate.cmd`, `P3a-full-quiet.cmd`, `P3b-full-tone.cmd`, `A1-install-audio-prot.cmd` | 단계 시험(끝점 볼륨 0 dB로 맞추고 1 kHz 사인 재생, 1초마다 레지스트리 출력) |
| `file-delivery-s243/manifests/snd-j274-20261118-1256-prot.json`, `jobs/snd-j274-20261118-1256-prot.cmd` | 전달(파일 4개 → `C:\NWOAS\drop\snd-j274-20261118-1256-prot`) |

### 10.2 빌드 결과 (stamp `20261118-1256-prot`)

| 항목 | 값 |
|---|---|
| 단위 시험 | 243598개 검사, 실패 0 |
| MSBuild | Release·ARM64 `/W4 /WX`, 경고 0·오류 0. infverif 통과, 서명 인증서 13338B38…(이전과 같음) |
| 모의 실행 | 런처 모듈: B단계 실패 경로의 머리말(v2, 상태 0), S1~S4를 실제 m1n1 ADMAC·MCA 클래스와 가짜 메모리로 실행 → RXB CONF 0x1b44f, 어댑터 B 0x205050, RX 버스폭 0x11, 앰프 0x0e..0x10 = 0x21/0x42/0x40 |

| 파일 | 바이트 | sha256 |
|---|---|---|
| `NwoasAudio.sys` | 91592 | `a7603464fdd9983bde678fc048f48cad02523d6d7ab530a3068ea214fcb31dc4` |
| `NwoasAudio.inf` | 3585 | `56c918cf86dca9c538c2ff4644507035587564d9411c173799cb92c0dedda21f` |
| `nwoasaudio.cat` | 2603 | `7567c8125cf500a639ec9f4a1b76813d7690952c50cb323f87bc7e2bebc8b9cb` |
| `prot-tone.ps1` | 6409 | `31f80ef3d8dfdd00ac3e99ef429199090cc01a56ef4c2285255e08deb713539f` |

### 10.3 단위 시험이 보인 것 (모델 안에서의 사실, 실기기 아님)

| 시험 | 결과 |
|---|---|
| 0.49 W 계단 25분 | 코일 94.84 °C(정상상태 94.91), 표본 단위 식과 최대 차이 0.0013 °C |
| 풀스케일 1 kHz 사인 20분, 켜짐 가정(119 °C)에서 | 10묶음 뒤 정상, 코일 최대 134.7 °C, 가장 큰 출력 +17 dBV, 끝 볼륨 −13.5 dB(+7.5 dBV), 되돌림 0 |
| 같은 시험, 2시간 꺼진 뒤 | 처음엔 +21 dBV까지, 코일 최대 134.7 °C, 되돌림 0 |
| V 감지 0(죽음) | 6묶음(128 ms) 뒤 되돌림, 그동안 모델은 보낸 신호 전력으로 가열 |
| 제어 없는 19.6 W | 0.21 s 뒤 180 °C 넘어 앰프 끔 신호 |
| 게이트 전수(이득 그림자 256 × 볼륨 256 × 정상/아님) | 21 dBV 넘는 조합 없음, 정상 아닐 때 +5 dBV 넘는 조합 없음 |

### 10.4 단계별 실기기 시험 (오케스트레이터가 실행)

공통 명령(첫 번째는 설치 포함, 다음 단계도 같은 명령으로 Windows를 다시 올림 — 단계는 런처 시작 때 머리말에 들어감):

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
NWOAS_AUDIO_PROT=sense NWAU_PKG=snd-j274-20261118-1320-worker NWAU_A1=A1-install-audio-worker.cmd \
  bash sound-s256/run-install-audio.sh <현재 launcher .out> <현재 LOG.link>
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/P1-sense.cmd
```

모든 단계의 런처 로그: `[S256-audio] S1 … S4 …`, `HEADER … version=2 … status=0x3 … stage=<단계>`, `READY`. `SENSE FAIL`이면 상태 0x1(감지 없이 정적)이라 그 단계는 의미가 없습니다.
아래 기대값은 [가설: 이득 dB = 풀스케일 사인 dBV, 임피던스 4.6 Ω]으로 계산한 값입니다. P1이 이 가설 자체를 재는 시험입니다.

**P1 — 감지만** (`NWOAS_AUDIO_PROT=sense`, `P1-sense.cmd`: 1 kHz −10 dBFS 20초, 끝점 0 dB → 볼륨 0x0c, 이득 0x00, 기대 0.56 Vrms·약 69 mW)

| 레지스트리(`Device Parameters`, 재생 중 T+5~T+20 줄) | 기대 | 다음 단계로 가는 조건 |
|---|---|---|
| `Sense` / `StartStatus` | 1 / 0 | 같아야 함 |
| `ProtState` | 2(정상)로 바뀜, `ProtVerified` 10 | 2 |
| `GainReg` / `VolReg` / `ProtRaises` | 0 / 12 / 0 | 이득이 오르면 안 됨 |
| `SenseVrmsMv` | 약 560 | 400~790(±3 dB) |
| `SenseRatioPermille` | 약 1000 | 500~2000(정상 판정 조건과 같음) |
| `SenseIrmsMa` | 약 120 | 0이 아님 |
| `SenseZMilliohm` | 약 4600 | 3000~8000 |
| `SenseRMilliohm` | Z 이하, 약 4000~4600 | 1000 이상 |
| `SensePowerUw` | 약 69000 | 양수, 30000~140000 |
| `SenseSat`, `SenseBad*`, `ProtTrips`, `RxRingErrors`, `RxReportFaults`, `RxStrayReports` | 0 | 모두 0 |
| `RxDescCompleted` | 초당 약 47 증가 | 증가 |
| `CoilTempMilliC` / `MagnetTempMilliC` | 119000에서 약 90000으로 / 89800에서 약 86700으로 | 내려감 |
| `SenseRTempMilliC` | 25000 근처 | 기록만(Rref 확인용) |
| `AmpTdm4` / `RxConf` | 4211233(0x404221) / 111695(0x1b44f) | 같아야 함 |
| 재생 뒤 `ProtState` / `GainReg` / `AmpState` / `LastStopReason` | 0 / 0 / 14 / 1 | 같아야 함 |

P1이 어긋날 때의 뜻: `ProtTripReason` 1(STALE)·`RxDescCompleted` 0 → RX DMA가 안 돎(`RxCarveout`·`RxOfUf` 확인), 2(UNWRITTEN) → 완료는 오는데 메모리에 안 씀, 5(IMPEDANCE)·Z 약 3000 → I/V 반쪽이 바뀜, Z 매우 큼 → I 없음, 4(NEG_POWER) → I나 V 부호 반대, 6(DEAD)·Vrms 정상 → 이득 가설이 8 dB 넘게 틀림, 값이 무작위 → 앰프 0x0e 가장자리/오프셋(0x20 또는 BITSTART 1) 문제. 이 경우 P2로 가지 않습니다.

**P2 — 보호 켬, 중간** (`NWOAS_AUDIO_PROT=moderate`, `P2-moderate.cmd`: −10 dBFS 30초, 기대 0.89 Vrms·약 0.17 W, P1보다 4 dB 큼)

| 값 | 기대 |
|---|---|
| `ProtState` 2, `ProtRaises` 1, `GainReg` 20(0x14) | 0.3초 안 |
| `VolReg` | 32(0x20)에서 24(0x18)로 내려와 그대로 |
| `ProtFloor` | 0(모델 120 °C 밑) |
| `SenseVrmsMv` / `SensePowerUw` / `SenseRatioPermille` | 약 890 / 약 173000 / 약 1000 |
| `MaxCoilTempMilliC` | 119000 이하 |
| `ProtTrips` | 0 |
| 재생 뒤 `ProtDrops` 1, `GainReg` 0 | |

**P3 — 보호 켬, 최대** (`NWOAS_AUDIO_PROT=full`). 먼저 `P3a-full-quiet.cmd`(−20 dBFS 20초, 기대 1.12 Vrms·0.27 W): `VolReg`가 32에서 0까지 0.5 dB씩(약 0.3초) 내려와 0, `ProtFloor` 0, `ProtTrips` 0. 그다음 `P3b-full-tone.cmd`(−10 dBFS 60초, 볼륨 0이면 3.55 Vrms·2.74 W):

| 값 | 기대 |
|---|---|
| `CoilTempMilliC` | 1초 안에 120000을 넘고, 그 뒤 120000~140000 |
| `ProtFloor` = `VolReg` | 0에서 늘어남(코일 130 °C면 18, 140 °C면 36) |
| `MaxCoilTempMilliC` | 140000 미만 |
| `ProtTrips` / `SenseSat` | 0 / 0 |
| 멈출 조건 | 되돌림, 140000 이상, `SenseZMilliohm`이 크게 변함, 콘치님이 이상한 소리를 들음 |

P3b 다음은 음악 재생(끝점 0 dB)으로 귀 확인입니다.

### 10.5 남은 위험 [가설 포함]

| 위험 | 내용 |
|---|---|
| 이득 dB 해석 | 기대 V 계산은 "11 + 0.5·이득 dB = 풀스케일 사인 dBV"라는 가설. 8 dB 넘게 틀리면 정상 판정이 안 나 정적 모드로 남음(안전 쪽). P1 비율로 확인 |
| 같은 클러스터 RX | Asahi는 감지에 따로 클러스터를 쓰고 RXB를 씀. NWOAS는 클러스터 0 RXB를 TX와 같은 SYNC로. 안 되면 STALE/UNWRITTEN으로 드러남 |
| 앰프 TX 가장자리·오프셋 | 0x0e = 0x21, RXB BITSTART 0은 가설. 1비트 어긋나면 V·I가 같이 2배/절반 → V² 비가 ±6 dB → 정상 판정 범위(±3 dB) 밖이라 정적 모드로 남음(단위 시험 확인). 비율은 P1에서 직접 봄 |
| SRAM 배정 | ADMAC RX 채널의 SRAM은 리셋값 그대로. Linux는 채널마다 나눠 줌. RX가 안 돌면 STALE |
| 감지 중단 직후 | 정상 상태에서 V 감지만 0이 되면 되돌림까지 약 130 ms. 그동안 실제 코일은 모델보다 뜨거울 수 있음(풀스케일 27 W면 수 °C~수십 °C). Asahi도 비슷한 지연(250 ms 잠금)이 있음 |
| 저항 온도 | Rref 4.6 Ω @25 °C는 공칭값. 보고만 하고 제한에 안 씀 |
| 게스트 시계 | 멈춘 동안의 식힘은 KeQueryInterruptTime 기준. 시계가 느리면 덜 식힌 것으로 봄(안전 쪽) |


### 10.6 S259b — 재생 중 멈춤·팝 대응 (`20261118-1320-worker`, 실기기 미검증)

**실기기 결과(오케스트레이터, 13:00~13:05, `1256-prot`):** P1 감지 수치는 가설과 맞음(Vrms 569 mV, 비율 1014~1022 ‰, Z 4.35 Ω). 문제: 재생 중 RX 멈춤 → STALE, 이어서 DmaRanDry로 재생 정지, 팝. A2: `MaxServiceGapUs` 86 ms, `PadChunks` 159, TX `OfUf` 0xffff. P1의 VolReg 12→39는 콘치님이 손으로 볼륨을 내린 것이고, 그 약 1.5초 뒤 정지.

**원인 [코드로 확인한 사실 + 가설]:**

| 항목 | 내용 |
|---|---|
| 사실 | 이전 판은 TX 기술자 4개 × 21.3 ms = 85.3 ms만 큐에 넣음. 측정 최대 간격 86.06 ms = 이 한계를 막 넘은 값 → TX 고갈(DmaRanDry), RX 기술자도 4개라 같은 간격에서 바닥 |
| 사실 | 게스트 NVMe는 파이썬 하이퍼바이저가 흉내 냄(`nvme-s139/controller.py`, SSD 입출력을 동기 실행). m1n1은 `time_stealing = true`라 프록시 예외마다 모든 CPU를 멈춤(`hv_exc.c` `hv_rendezvous`). ADMAC·NCO는 실제 시간으로 계속 소비 |
| 사실 | `1256-prot`는 재생 중 1초마다 레지스트리 값 70여 개를 씀 → SYSTEM 하이브가 계속 더러워져 몇 초마다 디스크로 씀. Windows도 볼륨을 바꾸면 그 값을 레지스트리에 저장 |
| 가설 | 디스크 쓰기 → NVMe 트랩 → 게스트 전체 정지 → 85 ms 넘는 공백 → 팝·TX 고갈·RX 멈춤. 볼륨 변경 1.5초 뒤 정지가 이와 맞음. 새 판의 실시간 카운터로 확인 |
| 사실 | 이전 판은 Windows 볼륨 변경·볼륨 램프·고장 처리의 I2C를 스핀락을 잡은 채 실행(렌더 DPC가 그동안 기다림) |

**고친 것:**

| 무엇 | 어떻게 |
|---|---|
| 디스크 쓰기 | 재생 중 1초 기록은 휘발성 하위 키 `Device Parameters\Live`에만(디스크로 안 감). 비휘발 기록은 장치 시작·정지·고장·종료 때만. 재생 시작 때도 Live만 |
| 버퍼 | TX·RX 기술자를 8 KiB(2048 프레임, 42.7 ms)로 → 4개 = 171 ms까지 버팀(이전 85 ms). 무음 선채움 2개(85 ms) |
| I2C | 앰프 I2C는 전부 PASSIVE에서 `AmpMutex`로, 스핀락 없이. 렌더·타이머 DPC는 앰프 작업자(작업 항목)만 깨움. DPC에서 본 고장은 `FAULTING` 상태로 무음을 계속 넣고 작업자가 앰프를 끈 뒤 DMA를 멈춤 |
| RX 멈춤 | 150 ms 동안 감지 묶음이 없으면(STALE) 또는 묶음이 안 쓰였으면(UNWRITTEN): RX 채널·RXB를 다시 시작하고 처음부터 다시 판정(재생마다 최대 5번). 그동안은 정적 모드 + 보낸 신호로 모델을 계속 돌림(4.0 Ω 기준, 실측 4.35 Ω보다 보수적) |
| 볼륨 채널 | 두 채널 값 중 큰 쪽(마지막 채널이 이기던 방식 대신) |
| 측정 | ADMAC 24 MHz 카운터로 실제 시간 측정(게스트 시계는 하이퍼바이저가 멈춘 시간을 빼서 못 봄) |

**새 레지스트리 값(`Device Parameters`와 `Device Parameters\Live`):**

| 값 | 뜻 |
|---|---|
| `MaxRealGapUs` / `MaxServiceGapUs` / `MaxStolenUs` | 서비스 사이 최대 간격(실제 시간 / 게스트 시간 / 그 차이 = 하이퍼바이저가 멈춘 시간) |
| `RealGaps40ms` / `RealGaps80ms` | 실제 시간 40 / 80 ms 넘은 간격 수 |
| `MaxGapRenderUs` / `MaxGapTimerUs` | 출처별 최대 간격(렌더 타이머 1 ms / 드라이버 타이머 10 ms) |
| `MaxServiceUs` / `MaxSenseUs` | 서비스 한 번 / 그중 RX + 모델 처리 시간(스핀락 잡은 시간) |
| `I2cOps` / `I2cMaxUs` / `I2cTotalMs` | I2C 횟수, 한 번 최대, 합계 |
| `AmpWorkRuns` / `MaxAmpWorkUs` | 앰프 작업자 실행 횟수·최대 시간 |
| `RxRecoveries` / `BlindMs` | RX 재시작 횟수 / 보낸 신호로만 모델을 돌린 시간 |
| `VolSets` / `LastVolLevel` / `LastVolChannel` / `GateRefusals` | Windows 볼륨 쓰기 횟수·마지막 값·채널 / 게이트가 거절한 쓰기 |
| `StreamRealMs` / `StreamGuestMs` | 마지막 재생의 실제·게스트 길이(차이 = 멈춘 시간, 카운터 24 MHz 가정 확인용) |

**시험 명령** (P1부터 다시. 기대값은 10.4와 같고, 묶음이 2배라 `RxDescCompleted`는 초당 약 23, 정상 판정은 6묶음 = 0.26초):

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
NWOAS_AUDIO_PROT=sense NWAU_PKG=snd-j274-20261118-1320-worker NWAU_A1=A1-install-audio-worker.cmd \
  bash sound-s256/run-install-audio.sh <현재 launcher .out> <현재 LOG.link>
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/P1-sense.cmd
# P2: 같은 첫 명령을 NWOAS_AUDIO_PROT=moderate로 다시(Windows 재시작) → P2-moderate.cmd
# P3: NWOAS_AUDIO_PROT=full → P3a-full-quiet.cmd → P3b-full-tone.cmd
python3 -I transport-s123/queue_job.py <LOG.link> sound-s256/jobs/A2-status.cmd   # 단계 뒤 비휘발 값
```

판정에 더 볼 것: `PadChunks` 증가 없음, `DmaRanDry` 0, `MaxRealGapUs` < 171000. 공백이 생기면 `MaxStolenUs`가 크면 하이퍼바이저 정지(디스크 등), 작으면 게스트 안의 지연입니다. `RxRecoveries`가 늘면 RX가 멈췄다가 다시 시작된 것(그동안 정적 모드).

| 파일 (`out/20261118-1320-worker/package`) | 바이트 | sha256 |
|---|---|---|
| `NwoasAudio.sys` | 97264 | `162e71c234f284ac735f69a9828469e510c90fba59cf4ef5716671dd969058a1` |
| `NwoasAudio.inf` | 3584 | `05b1bf969a1d0d5b1f8c772e4a3287524b64df402ee65f953919978b19abd7c3` |
| `nwoasaudio.cat` | 2603 | `f24d4b23d319b66492e35948e3ef4d7800c9baf8908ef0ce116cdac82dd72a9c` |
| `prot-tone.ps1` | 7160 | `56b0208a48c4eebfc03b122f8c6aaccf335b76c3e19a500a684f9c47e016745d` |

빌드: 단위 시험 152196개 실패 0, MSBuild 경고 0·오류 0, infverif 통과. 런처 모듈·지문은 바뀌지 않음(`0cb7fdb5…`).

남은 위험 [가설]: 171 ms보다 긴 하이퍼바이저 정지는 여전히 TX 고갈 → 안전 정지(앰프 끔). 고갈 뒤 직렬기가 마지막 값을 붙들고 있는지(직류)는 모름 — 그래서 고갈은 계속 정지로 처리. 지연은 시작 무음 85 ms + 큐 최대 171 ms.


### 10.7 S259c — 팝·정지의 실제 원인과 수정 (`20261118-1331-clock`, 실기기 미검증)

**실기기 결과(13:25, `1320-worker`, P2):** 보호는 정상(이득 0x14, Z 4.28 Ω, 0.26초에 정상 판정). 하지만 팝이 있었고(볼륨을 안 만져도) 11.6초에 정지(DmaRanDry). 실제 간격 80 ms 초과 0, 렌더 간격 최대 5.1 ms, `PadChunks`가 초당 약 3씩 꾸준히 늘었고, `StreamRealMs` 11605 대 `StreamGuestMs` 11512(0.8 %).

**원인 [코드와 카운터로 판단]:**

| 현상 | 원인 |
|---|---|
| 팝 | Windows 데이터를 링으로 넘기는 양(`UpdatePosition` → `ReadBytes`)이 QPC(게스트 시간)로 정해짐. 게스트 시간은 하이퍼바이저가 멈춘 시간만큼 느림(0.8 %). ADMAC은 실제 시간(NCO)으로 소비 → 공급이 초당 약 8 ms 모자람 → 큐가 무음 채움 문턱(대기 2개 미만)에 붙어 있음 → 몇백 바이트짜리 무음을 소리 중간에 자꾸 끼워 넣음(`PadChunks` 초당 약 3) |
| 11.6초 정지 | 큐가 문턱(85 ms)에 붙어 있는 상태에서 타이머가 약 80 ms 늦게 옴(`MaxServiceGapUs` 79.5 ms, 실제와 게스트 차이 약 20 µs → 하이퍼바이저 정지가 아니라 게스트 안의 DPC 지연. `MaxGapTimerUs` 88.9 ms도 같은 종류). 렌더 간격 5.1 ms가 이 공백을 못 본 것은 그다음 렌더 호출이 이미 고장 상태라 재지 않았기 때문 |
| 앰프 쓰기 | 평상시 재생 중에는 없음(코드: 정책이 바뀔 때만 쓰기). `I2cOps` 226 = 재생 시작 설정 + 콘치님 슬라이더 216번 |
| `vol6`은 깨끗했던 이유 [가설] | 1.5초 재생이라 0.8 %면 12 ms만 모자라 처음 버퍼로 견딤 |

**수정:**

| 무엇 | 어떻게 |
|---|---|
| 재생 위치 | 재생 중에는 ADMAC 24 MHz 카운터(실제 시간)로 위치를 진행 → 공급 = 소비. 카운터가 QPC의 절반보다 느리면 무시하고 QPC 사용, 한 번에 0.5초 넘게는 건너뛰지 않음 |
| 처음 큐 | 무음 3묶음(128 ms)으로 시작 → 평상시 큐가 약 128 ms에 머묾. 80 ms 지연을 견딤 |
| 고갈 | 고갈되면 무음을 다시 넣고 계속 재생. 한 재생에서 3번째면 정지(안전 장치 유지) |
| 카운터 | `MinQueueUs`(0.5초 뒤 큐 최소, µs), `MinInflight`, `DryRecoveries`; 시험 스크립트 FINAL에 `PadChunks`·`PadBytes`·`DropBytes`·`PushBytes`도 |

**기대(P2 재시험):** `PadChunks`가 시작 뒤 늘지 않음, `MinQueueUs` 약 80000 이상, `MinInflight` 2 이상, `DmaRanDry` 0, `DropBytes` 0.

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
NWOAS_AUDIO_PROT=moderate NWAU_PKG=snd-j274-20261118-1331-clock NWAU_A1=A1-install-audio-clock.cmd \
  bash sound-s256/run-install-audio.sh <현재 launcher .out> <현재 LOG.link>
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/P2-moderate.cmd
```

| 파일 (`out/20261118-1331-clock/package`) | 바이트 | sha256 |
|---|---|---|
| `NwoasAudio.sys` | 97776 | `bf3be9578d9efa8b1d36464163c6d09f89a4fb2a5385b0db3c39cadcf7524a02` |
| `NwoasAudio.inf` | 3585 | `3f0a0db16f4f3be5bc22e4c9a0144fd53915952e7ee93fb4cadc1b4a8b301fc2` |
| `nwoasaudio.cat` | 2603 | `7eb27357942fca55f0ba1fdd3c638572ad993205a5f9e465431c24036c83eea5` |
| `prot-tone.ps1` | 7377 | `81e0e6cd2a5293e249682deab1d5549e2e6c9c24de4c15c0468e32c09942e909` |

남은 위험 [가설]: 하이퍼바이저가 길게 멈추면 위치가 그만큼 건너뛰어, Windows가 아직 안 쓴 버퍼 부분(이전 소리)을 읽을 수 있음(0.5초 한도). 고갈 중 직렬기 출력이 무엇인지는 모름(최대 2번까지 계속 재생). 이득 0x00→0x14 전환은 재생 중 한 번이며 소리 크기는 같게 맞추지만, 전환 순간 짧은 딸깍 소리가 날 수 있음.


### 10.8 S259d — 사건 기록과 남은 팝 (`20261118-1352-events`, 실기기 미검증)

**실기기 결과(13:46, `1331-clock`, P2):** 30초 끝까지 재생, `PadChunks` 0, `MinQueueUs` 125333, 팝은 많이 줄었지만 남음. `DmaRanDry` 1, `DropBytes` 15616, 보호 되돌림 2번.

**카운터로 본 원인:**

| 현상 | 원인 |
|---|---|
| 고갈 1, 버림 15616 B, 되돌림 1 | T+3~4초에 서비스 간격 385 ms(게스트 384.6 / 실제 385.0 → 하이퍼바이저 정지 아님, 게스트 안에서 타이머 DPC가 385 ms 안 돎). 큐 128 ms를 넘어 고갈, RX도 멈춰 STALE, 다시 돌아온 뒤 위치가 실제 시간만큼 건너뛰어 링 여유(341 ms)를 넘은 끝부분을 버림 |
| 되돌림 2 | 역시 STALE(`SenseBadStale` 2). 볼륨 값 자체가 아니라 감지 묶음이 150 ms 안 온 것 |
| 되돌린 뒤 정상이 안 됨 | 콘치님이 볼륨을 낮춰 소리가 75 mV → 판정 문턱 0.2 V 밑이라 재판정 불가. 그동안 정적 매핑이 10 dB 작아 크기가 툭 떨어짐 |
| 지연과 볼륨 조작 [가설] | 긴 지연이 이번·이전 두 번 모두 볼륨 조작 직후. 볼륨 화면(Windows 볼륨 표시) 그리기 → GPU 드라이버 쪽 긴 DPC일 수 있음. 사건 기록의 VOL과 STALL 시각으로 확인 |

**수정:**

| 무엇 | 어떻게 |
|---|---|
| 사건 기록 | 최근 128개, ADMAC 시계 ms(재생 시작 기준)와 게스트 ms. START, STOP, PAD, DROP, DRY, DRYREC, GAIN, VOL(100 ms 안은 합침), TRIP(이유), VERIFIED, RXREC, STALL(40 ms 넘는 간격), FADE. `Device Parameters[\Live]\NwAudioEvents`, `prot-tone.ps1`가 끝에 `EVT` 줄로 출력 |
| 버림 | 링이 다 못 받을 때(고갈 뒤에만 생김) 가장 오래된 부분을 한 번에 버리고 최신 소리를 이어 감(끝부분을 자르던 방식 대신) |
| 같은 크기 | 보호가 설정된 경우 정적 모드도 보호 모드와 같은 크기(이득 차 20단계만큼 덜 감쇠), 정적 상한 0x0c 아래로는 안 감 → 되돌림·무장 때 크기가 안 바뀜(상한에 걸릴 때만 작아짐) |
| 이득 전환 | 0x05를 20 dB 내렸다가(약 5 ms) 이득을 바꾸고 다시 올림(딸깍 대신 짧게 작아짐) |
| 큰 볼륨 변화 | 2 dB씩 0.5 ms 간격으로 나눠 씀 |
| 게이트 | 지금 확인된 값보다 조용한 0x05는 언제나 허용(페이드용, 단위 시험) |
| 판정 문턱 | 0.2 V → 0.1 V(작은 볼륨에서도 재판정) |

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
NWOAS_AUDIO_PROT=moderate NWAU_PKG=snd-j274-20261118-1352-events NWAU_A1=A1-install-audio-events.cmd \
  bash sound-s256/run-install-audio.sh <현재 launcher .out> <현재 LOG.link>
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/P2-moderate.cmd
```

| 파일 (`out/20261118-1352-events/package`) | 바이트 | sha256 |
|---|---|---|
| `NwoasAudio.sys` | 99312 | `f1264e2c7c45827c6f9fbe40b62b84e7a859163a5f1e00789875d249d3e4ce0b` |
| `NwoasAudio.inf` | 3585 | `243997e598048d2ab28ef3b6513cf12e14f37110a656d0423cfe1aa1ca152a2c` |
| `nwoasaudio.cat` | 2603 | `f81c485fca2fcb7d71755f753161d3c9a4eeeab09967370710a98800e797e096` |
| `prot-tone.ps1` | 9184 | `398bf1d0014651c90ac3e9586137340fbbe3e4905f3ac1b458cd6de6c73ce956` |

남은 위험 [가설]: 385 ms 같은 게스트 DPC 지연은 드라이버 큐(128~171 ms)로 못 견딤 — 그 원인(GPU 드라이버 등)을 찾아야 함. 페이드는 약 10 ms 짧게 작아짐. 정적 매핑이 바뀌어 보호가 설정된 경우 Windows 볼륨 위쪽 10 dB 구간은 정적 모드에서 상한에 걸려 차이가 없음.


### 10.9 S259e — 이득 전환 없앰 (`20261118-1403-fixgain`, 실기기 미검증)

**실기기 결과(14:00, `1352-events`, P2):** 모든 카운터 깨끗(Pad 0, Drop 0, Dry 0, `MaxServiceGapUs` 5862, `MinQueueUs` 126000, STALL 없음). 팝은 시작 때 한 번뿐. EVT: VERIFIED 387 ms → 페이드 → GAIN 0x00→0x14 393 ms → 페이드 복귀 404 ms. 아날로그 이득 전환이 5 ms 페이드로도 들리는 것으로 판단.

**바꾼 것:**

| 무엇 | 어떻게 |
|---|---|
| 이득 | 보호가 설정되면(머리말 v2, 감지 준비, `prot_gain` 0x14) 앰프가 꺼져 있는 동안(재생 시작 설정) 0x03 = 0x14로 두고 재생 내내 바꾸지 않음. 정적(미검증) 모드 = 볼륨 천장 0x0c + 0x14 = 0x20(21 dB에서 −16 dB = 11 dB에서 −6 dB와 같은 +5 dBV). 검증되면 천장만 올라감(10 ms에 0.5 dB씩), 되돌림은 천장만 내림(2 dB씩 0.5 ms 간격). 절전·드라이버 내림 때만 앰프 꺼진 상태에서 0x00으로 돌려놓음 |
| 켜기 | 볼륨 0xc9(−100 dB) → 무음이 큐에 있는 채로 0x02 동작 → 2 ms → 볼륨을 목표까지 22단계씩 0.6 ms 간격으로 올림 |
| 끄기 | 볼륨을 0xc9까지 약 5 ms에 내림 → 0x0d → 0x0e |
| 게이트 | 0x03 = 0x14는 머리말이 보호를 설정했고 확인된 볼륨이 이득 0x14 기준 하한(미검증 0x20, 검증 시 모델 하한)을 이미 만족할 때만. 0x00은 언제나(조용해짐). 0x02에 0x01(음소거·감지 켬) 허용. 단위 시험: 허용되는 이득 쓰기 뒤 풀스케일이 미검증 +5 dBV, 검증 시 모델 하한을 넘지 않음 |

보호가 설정되지 않은 경우(머리말 v1, 감지 준비 실패, `NWOAS_AUDIO_PROT=sense`)는 이전처럼 이득 0x00, 볼륨 ≥ 0x0c입니다.

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
NWOAS_AUDIO_PROT=moderate NWAU_PKG=snd-j274-20261118-1403-fixgain NWAU_A1=A1-install-audio-fixgain.cmd \
  bash sound-s256/run-install-audio.sh <현재 launcher .out> <현재 LOG.link>
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/P2-moderate.cmd
```

기대 EVT: START 뒤 GAIN 0x00→0x14가 재생 시작 설정 중(소리 나기 전) 한 번, VOL 201→32(켜기 램프), VERIFIED 뒤 VOL 32→24(천장 올림), STOP 때 VOL →201. 재생 중 GAIN 없음.

| 파일 (`out/20261118-1403-fixgain/package`) | 바이트 | sha256 |
|---|---|---|
| `NwoasAudio.sys` | 98800 | `e85bb6497053727da02899e7ed42cf4e364c661159a1877f186527fd99efbe8e` |
| `NwoasAudio.inf` | 3584 | `9131c34d3eb13ed8606f73eb1691f80cde68acd3f84b40f2cbd0fc74274eb290` |
| `nwoasaudio.cat` | 2603 | `0467b7c0b229ab67704de1111c95d10e9c6022e035b8bdd00b33ab4c59463cf1` |
| `prot-tone.ps1` | 9184 | `398bf1d0014651c90ac3e9586137340fbbe3e4905f3ac1b458cd6de6c73ce956`(그대로) |

남은 위험 [가설]: 이득 21 dB에서 같은 출력이면 앰프 잡음이 10 dB 높을 수 있음(조용할 때 쉬 소리). 앰프 켜기 자체의 팝은 디지털 볼륨으로 못 막을 수 있음.


### 10.10 S259f — RX 멈춤과 천장 내려감 (`20261118-1412-bridge`, 실기기 미검증)

**실기기 결과(14:08, `1403-fixgain`, P2):** 팝 없음. 남은 것: 2906 ms·28681 ms에 TRIP STALE + RXREC, 그때마다 천장 24→32가 약 0.3초(들리는 작아짐). 1246 ms에 STALL 80 ms(게스트 = 실제) 한 번.

**원인 [카운터로 판단]:** 두 STALE 근처에 STALL이 없음 → 서비스는 정상으로 돌았음. `RxDescSubmitted` 725 대 `SenseBlocks` 713 → RX를 시작할 때마다 4개씩 넣은 기술자가 남은 채로 RX가 스스로 멈춤(기술자가 모자라서가 아님, DPC 지연 탓도 아님). 그래서 RX 기술자를 더 깊게 하는 것으로는 안 고쳐짐(하드웨어 깊이도 4). [가설] RX 채널 SRAM 배정이 리셋값 0(`RxCarveout` 0) — Linux는 채널마다 2 KiB를 줌.

**수정:**

| 무엇 | 어떻게 |
|---|---|
| RX SRAM | RX 리셋 때 `RX_SRAM_SIZE`(0x98)가 0x800 이상이면 채널 3에 기준 0·크기 0x800 배정(TX 쪽은 그대로) [가설] |
| 조기 재시작 | 정상 상태에서 감지 묶음이 100 ms 없으면 RX를 다시 시작(보호 상태는 그대로, 다음 묶음은 판정 안 함) |
| 다리 놓기 | 정상이고 모델이 110 °C 밑이면 감지 공백 0.5초까지는 되돌리지 않고, 보낸 신호(4.0 Ω)로 모델을 계속 돌림(천장 그대로). 0.5초를 넘거나 뜨거우면 이전처럼 되돌림 |
| 기록 | 멈췄을 때 RX 레지스터(`RxStallDesc`·`Report`·`Status`·`Residue`·`Serdes`·`McaInt`·`Inflight`), `RxSramSize`, `RxCarveoutSet`, `RxQuietRestarts`, `ProtBridges`; EVT에 BRIDGE·RXQUIET |

기대: TRIP 없음(또는 0.5초 넘는 공백만), EVT에 BRIDGE·RXQUIET가 있으면 천장(VOL) 변화 없이 지나감. `RxCarveoutSet`이 0x8000000이면 배정 성공, 그 뒤로 RXQUIET가 사라지면 SRAM이 원인이었다는 뜻.

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
NWOAS_AUDIO_PROT=moderate NWAU_PKG=snd-j274-20261118-1412-bridge NWAU_A1=A1-install-audio-bridge.cmd \
  bash sound-s256/run-install-audio.sh <현재 launcher .out> <현재 LOG.link>
python3 -I transport-s123/queue_job.py <새 LOG.link> sound-s256/jobs/P2-moderate.cmd
```

| 파일 (`out/20261118-1412-bridge/package`) | 바이트 | sha256 |
|---|---|---|
| `NwoasAudio.sys` | 100368 | `95f269473363675feb2bbaf0408e1075cdc5855b095333940a8786875c7395e9` |
| `NwoasAudio.inf` | 3585 | `c2a69f6c30b72457afc525ef63c3d5fb8a3c7e8657bb572106aa707a9598ca5f` |
| `nwoasaudio.cat` | 2603 | `920d0f721e95291f98f3984df31e226c46f16dd91374e80eb835ed45a414481b` |
| `prot-tone.ps1` | 9702 | `0a2835ac74bb1095c6788779b4d693e93c78753492a745077cf480477df8daaf` |

빌드: 단위 시험 152220개 실패 0(다리 놓기: 0.5초 안 유지, 넘으면 되돌림, 110 °C 넘으면 바로 되돌림, 미검증 상태는 다리 없음), MSBuild 경고 0·오류 0.

남은 위험 [가설]: 다리 동안(최대 0.5초)은 측정 대신 4.0 Ω 추정으로 모델을 돌림(실측 4.3~4.5 Ω보다 보수적). SRAM 배정 값이 틀리면 RX가 아예 안 돌 수 있음 → STALE로 정적 모드(안전 쪽), `RxCarveoutSet`·`RxDescCompleted`로 바로 보임.
