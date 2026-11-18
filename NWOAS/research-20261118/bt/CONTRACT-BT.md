# 내장 블루투스 Windows 드라이버 계약서 (S261, 2026-11-18)

표기: **[사실]** = 코드·실기 기록으로 확인한 것. **[가설]** = 확인하지 않은 것. 이 계약의 하이퍼바이저·펌웨어·드라이버는 **빌드와 가짜 하드웨어 시험·단위 시험만 했고 맥미니에서 한 번도 돌리지 않았습니다.** 장치가 장치 관리자에 보여도 블루투스가 된다는 뜻이 아닙니다.

구현: 하이퍼바이저 쪽 `nwoas_scripts/bt-s261/`(nwoas_bt_s261.py, bt_module.py, acpi/), 펌웨어 `nwoas_scripts/fw-bt-s261/`, 실행기 `nwoas_scripts/agxdcp-s261-guest-test.sh`, 드라이버 `port-j274/bt/bcm4378-bthx/`. 와이파이 계약(`research-20261118/wifi/CONTRACT-WIN.md`)은 그대로 유효합니다. 값이 바뀌면 이 문서, `nwoas_bt_s261.py` 상수, SSDT가 함께 바뀌어야 하며 `fw-bt-s261/build.py`와 `test_bt_hv.py`가 어긋남을 거절합니다.

## 0. 결론

- 블루투스(칩의 PCIe 기능 1, `14e4:5f69`)는 **ACPI 장치 `ACPI\NWOA5F69`(`\_SB.NWC1.BTH0`)** 로 Windows에 보입니다. 와이파이와 같은 방식(PCI 루트는 숨김, 설정 공간은 제어 페이지 경유)입니다.
- 와이파이 쪽은 바뀌지 않습니다: 와이파이 SSDT AML은 S259와 바이트 단위로 같고(빌드 결과 `wifi_ssdt_identical_to_s259 = true`), 기능 0 BAR·DART·버스 마스터 순서는 S259 코드 그대로입니다. S261은 그 뒤에 기능 1만 덧붙입니다. `NWOAS_BT=0`이면 S259와 똑같이 동작합니다.
- Windows 쪽 모델은 **Bluetooth extensible transport(BthX)** 입니다: 우리 드라이버 `nwbt4378.sys`가 `ACPI\NWOA5F69`의 버스 드라이버로 자식 PDO(호환 ID `MS_BTHX_BTHMINI`) 하나를 만들고, Windows 기본 `bth.inf`가 그 위에 BthMini.sys → BthPort를 올립니다. 그래서 설정의 "Bluetooth"와 장치 검색은 Windows 기본 스택이 합니다.
- 인터럽트는 쓰지 않고 **폴링**합니다(링 위치는 호스트 RAM에 있어 폴링에 하드웨어 접근이 없음).

## 1. Windows가 보는 것

| 항목 | 값 | 근거 |
|---|---|---|
| ACPI 경로 | `\_SB.NWC1.BTH0`, 부모 `NWC1` = `ACPI0004` 컨테이너(`_UID 0x12`) | `bt-s261/acpi/NwoasBtJ274.asl.inc` |
| 하드웨어 ID | `ACPI\NWOA5F69`, 인스턴스 `ACPI\NWOA5F69\0` | 같은 파일 |
| 자원 0 | 기능 1 BAR0(레지스터): CPU `0x6E0900000`, 길이 `0x8000` (버스 `0xE0900000`) | [사실: 크기] S257 실기 BAR 크기 측정 fn1 BAR0 `0x8000` mem64 |
| 자원 1 | 기능 1 BAR2(칩 메모리): CPU `0x6E0C00000`, 길이 `0x400000` (버스 `0xE0C00000`) | [사실: 크기] 같은 측정 BAR2 `0x400000` mem64 |
| 자원 2 | BT 제어 페이지: `0x348004000`, 길이 `0x4000` (와이파이 페이지 바로 뒤) | `nwoas_bt_s261.py` `BT_CTRL_IPA` |
| 인터럽트 | **없음** | 4장 |
| 자원 순서 | 위 순서지만 **길이로 구별**(32 KiB / 4 MiB / 16 KiB) | — |
| `_CCA` / `_DMA` | 0 / 장치 주소 `[0, 4 GiB)` = CPU 주소 | SSDT |
| IORT | 노드 추가 안 함 | 드라이버가 DMA 어댑터를 만들지 않음(연속 메모리 직접 할당) |
| `_STA` | `_INI` 전 0x0F, 뒤로는 기능 1이 한 번이라도 올라왔으면 0x0F, 아니면 0 | SSDT `AVLB` |
| 전원 메서드 | `_INI`·`_PS0` → BT CMD 1(칩·기능 1 준비), `_PS3` → BT CMD 2(기능 1 버스 마스터 끔). **칩 전원은 와이파이(WLN0) 소유** | SSDT |

### 1.1 BAR 배치와 루트 포트 창

| 구간(버스 주소) | 크기 | 무엇 |
|---|---|---|
| `0xE0000000`–`0xE07FFFFF` | 8 MiB | 기능 0 BAR2 (S259 그대로) |
| `0xE0800000`–`0xE080FFFF` | 64 KiB | 기능 0 BAR0 (S259 그대로) |
| `0xE0900000`–`0xE0907FFF` | 32 KiB | 기능 1 BAR0 |
| `0xE0C00000`–`0xE0FFFFFF` | 4 MiB | 기능 1 BAR2 (4 MiB 정렬) |
| 루트 포트 0 창 | `0xE0000000`–`0xE0FFFFFF` (`0x20` = `0xE0F0E000`) | S259 `0xE080E000`에서 넓힘 |

- 넓힌 창도 S259 UEFI가 이미 게스트 RAM에서 빼 둔 `[0xE0000000, 0xE1000000)` 안이라 **UEFI 메모리 예약은 바꾸지 않았습니다**(같은 `MemoryInitPeiLib.c`). USB-A(FL1100) 창 `0xC0000000`대와 겹치지 않습니다.
- ACPI 생산자 구간: NWC0 = `0x6E0000000`–`0x6E08FFFFF`(S259 그대로), NWC1 = `0x6E0900000`–`0x6E0FFFFFF`, 겹치지 않음(시험으로 확인).

### 1.2 BAR 접근 방식

- 와이파이와 같습니다. `bt_module.py`가 기능 1 BAR0·BAR2 두 구간에 BYPASS 추적기(`bt-s261-bar-emul`)를 걸어, Windows의 Device-nGnRnE 접근을 하이퍼바이저가 EL2 nGnRE로 대신 합니다. 두 BAR 사이 빈 구간(`0x6E0908000`–`0x6E0BFFFFF`)과 FL1100 BAR는 건드리지 않습니다(시험 `Module.test_tracers`).
- **드라이버 규칙:** 일반 레지스터 8·32비트 읽기·쓰기만(OTP는 8비트, 나머지 32비트). `memcpy`·64비트 쌍 접근 금지. 펌웨어 이미지는 BAR가 아니라 호스트 RAM에 두고 칩이 DMA로 가져가므로 BAR 접근은 수백 번 수준입니다.

## 2. DMA 규칙

| 규칙 | 내용 |
|---|---|
| 칩 한계 | [사실: Linux 드라이버] 이 칩 블루투스는 32비트 창 `[0, 0xFFFFFE00]`만 DMA 가능 |
| 주소 | 칩에 주는 주소 = 게스트 물리 주소, **`[1 MiB, 0xBFF00000)` 안의 비캐시 연속 메모리**(`MmAllocateContiguousMemorySpecifyCache`, 범위 밖이면 버림). dart-apcie0 ADT 창(vm-base `0x100000`, vm-size `0xBFE00000`) 안 |
| 왜 되는가 [사실: 코드] / [가설: fn1] | 포트 0 DART 16개 스트림이 모두 UEFI 표 하나를 가리키고 낮은 4 GiB는 IOVA = 게스트 PA(와이파이 계약 2장). 기능 1의 요청자 ID(0x101)가 어느 스트림으로 가든 같은 표 [가설: RID→SID 표가 비어 있어도 기능 0과 같은 기본 스트림을 탐. 하이퍼바이저가 `RID2SID` 0~15 유효 비트를 읽기만 해서 `BTC_RID2SID`로 보여 줌] |
| 크기 | 한 덩어리(약 488 KiB: 문맥·링 인덱스·링 12개·페이로드) + 펌웨어 358,605바이트(부팅 뒤 해제) |
| 해제 | 버스 마스터를 끈 것이 확인될 때만. 확인 실패면 일부러 해제하지 않음(`NwbtDmaLeaked`) |

## 3. BT 제어 페이지 (자원 2, 32비트 읽기·쓰기만)

모든 접근이 하이퍼바이저(맥북 Python HOOK)로 넘어가 한 번에 수 ms [가설: 값]. 부팅 때와 상태 기록 때만 씁니다.

| 오프셋 | 이름 | R/W | 뜻 |
|---|---|---|---|
| `0x00` | MAGIC | R | `0x5442574E` ('NWBT') |
| `0x04` | VERSION | R | 1 |
| `0x08` | STATE | R | 0 꺼짐, **1 켜짐**, 2 조용함(버스 마스터 끔, `_PS3`), 3 실패 |
| `0x0C` | ERROR | R | `nwoas_bt_s261.py` `BT_ERRORS` (1 DISABLED, 2 CHIP_DOWN, 3 NO_FN1, 4 FN1_BARS, 6 WINDOW, 11 WIFI_DISABLED …) |
| `0x10` | CMD | W | **ACPI 전용**: 1 준비(칩이 꺼져 있으면 S259 전원 켜기, 와이파이가 실패한 칩은 다시 켜지 않음), 2 조용함 |
| `0x14`/`0x18` | SEQ / AVAIL | R | 끝난 명령 수 / 기능 1이 한 번이라도 올라옴 |
| `0x1C` | IDS | R | `0x5F6914E4` |
| `0x20`/`0x24`/`0x28` | CFG_OFF / CFG_DATA / CFG_ERR | R/W | 기능 1 설정 공간(아래 허용 표). 0 성공, 1 켜짐 아님, 2 허용 안 됨, 3 오프셋, 4 예외 |
| `0x2C`/`0x30` | BAR0 / BAR2 | R | 켜짐일 때 `0xE0900000` / `0xE0C00000` |
| `0x34` | CHIP_STATE | R | 와이파이 페이지의 STATE(칩 전원) |
| `0x38`/`0x3C` | DEVCTL / EPCMD | R | 기능 1 DevCtl, 명령 레지스터 |
| `0x40` | RESET | W/R | 쓰기 1 = 기능 1 FLR(버스 마스터 끔 → Initiate FLR → 100 ms → BAR·DevCtl 복원 → 명령 `0x0402`). 읽기 = 결과 1 성공, 2 FLR 능력 없음, 3 복원 불일치, 4 켜짐 아님, 5 예외 |
| `0x44` | RESET_COUNT | R | |
| `0x48` | EPOCH | R | 칩 전원 켜기 성공 횟수(전원 순환을 드라이버가 알아챔) |
| `0x4C` | RID2SID | R | 포트 0 RID→SID 표 유효 비트(읽기만 함) |
| `0x54`/`0x58`/`0x5C` | WINDOW / DEVCAP / MSICAP | R | 루트 포트 `0x20`, 기능 1 DevCap, MSI 능력 오프셋 |
| `0x60`/`0x64` | BDADDR | R | ADT `/arm-io/bluetooth` `local-mac-address` 6바이트(**표시 순서**; HCI에는 뒤집어 보냄). 기록에 남기지 않음 |
| `0x68`/`0x6C` | CAL_LEN / CALBF_LEN | R | ADT `bluetooth-taurus-calibration` / `-bf` 길이(실기 ADT 덤프 둘 다 5,664) |
| `0x70`–`0x7F` | BOARD | R | `"atlantisb"` |
| `0x1000`–`0x27FF` | CAL | R | 보통 보정 데이터 |
| `0x2800`–`0x3FFF` | CALBF | R | 빔포밍 보정 데이터(B1이 쓰는 것) |

기능 1 설정 공간 쓰기 허용 표(그 밖은 거절):

| 오프셋 | 허용 |
|---|---|
| `0x70`·`0x74`·`0x78`·`0x80`·`0x84` (BAR 창) | 4 KiB 정렬, `0x18000000`–`0x1FFFF000` |
| `0x88` (서브시스템 제어) | 비트 16·19만 바뀜 |
| `0x04` (명령) | BME·INTx 끔 비트만, 메모리 비트는 항상 켬 |
| DevCtl (`cap+8`) | 하위 16비트, No Snoop·RO·FLR 비트는 항상 0 |
| LinkCtl (`cap+0x10`) | ASPM 두 비트 |
| MSI (`msi`, `+4`, `+8`, 데이터) | 켜기 비트만(벡터 1개), 주소는 `[1 MiB, 0xBFF00000)` 안 4바이트 정렬, 상위 0. 주소가 그 밖이면 켜기 거절 |

### 3.1 BAR 울타리 (S261b, 2026-11-18 실기 멈춤 뒤)

- **결론: 드라이버는 시작할 때마다 `ARM`해야 BAR에 닿을 수 있습니다.** 그 전과, 칩이 사라질 수 있는 사건 뒤로는 fn1 BAR가 모두 1로 읽히고 쓰기는 버려집니다(하이퍼바이저 HOOK).
| 오프셋 | 이름 | R/W | 뜻 |
|---|---|---|---|
| `0x80` | GEN | R | 울타리 세대. 부팅·칩 전원 끄기/켜기·와이파이 칩 리셋 표시(fn0 0xBC 쓰기)·`_PS3` 때마다 닫히며 1 증가 |
| `0x84` | ARM | W/R | 쓰기 = 지금 GEN → fn1 BAR·디코드를 확인(지워졌으면 다시 씀)한 뒤 울타리를 엶. 읽기 = 1 성공, 2 GEN 다름, 3 fn1 켜짐 아님, 4 BAR 복원 실패 |
- 드라이버 규칙: ARM 결과 1 전에는 BAR 접근 금지. 1초마다 GEN을 보고 바뀌면 BAR 접근 없이 죽음 처리. 레지스터 값 `0xFFFFFFFF`는 울타리/칩 없음으로 보고 다시 읽지 않음.

## 4. 인터럽트

- **결론: 쓰지 않습니다.** [사실: Linux 드라이버 주석] 이 칩은 INTx를 쓰면 마스크·상태 레지스터 위치를 몰라 인터럽트가 멈추지 않고, Linux는 MSI만 씁니다. 여기는 MSI를 받을 ITS가 없습니다. 그래서 기능 1 명령 레지스터에서 INTx를 끄고(와이파이가 쓰는 포트 0 묶음 줄 695를 오염시키지 않음), 드라이버가 2 ms마다 호스트 RAM의 링 위치를 봅니다.
- [가설] MSI가 꺼져 있어도 펌웨어가 링을 계속 진행시킴. 아니면 `MsiToRam=1`로 MSI 주소를 호스트 RAM 한 페이지로 줘서 칩의 MSI 쓰기를 평범한 메모리 쓰기로 받습니다(`NwbtCntMsi`로 횟수 확인).

## 5. 전원·순서

| 상황 | 일어나는 일 |
|---|---|
| 부팅(ACPI) | WLN0·BTH0 `_INI` 중 먼저 오는 쪽이 칩을 켬(S259 순서 + 기능 1). 다른 쪽은 "이미 켜짐" |
| 블루투스 D0 | `_PS0` → 준비(조용함이면 켜짐으로). 드라이버가 FLR 후 버스 마스터를 직접 켬 |
| 블루투스 D3·사용 안 함 | 드라이버가 버스 마스터 끔 → `_PS3` → 하이퍼바이저도 끔(MSI 포함). 칩 전원 그대로 |
| 와이파이 D3(`_PS3`) | **칩 전원이 꺼져 블루투스도 죽음.** BT STATE 0, 다음 준비 때 칩 전체를 다시 올림(EPOCH+1) |
| 와이파이 드라이버의 칩 리셋 | [가설] 와이파이 드라이버의 감시 타이머 리셋이 블루투스 펌웨어를 멈출 수 있음. 드라이버가 1초마다 RTI 상태를 보고 2가 아니면 장치를 다시 시작(부팅당 최대 3번, `AutoRestart`) |

## 6. 드라이버가 할 일 / 하면 안 되는 일

해야 할 일(순서, Linux `hci_bcm4377` 탐색 순서와 같음):
1. MAGIC·VERSION·STATE(=1)·BAR·IDS 확인, 다르면 BAR를 건드리지 않고 실패.
2. RESET(FLR) → 100 ms → 버스 마스터 켬 → 설정 창 `0x80=0x18002000`, `0x70=0x1810A000`, `0x74=0x18011000`, `0x78=0x18107000`, `0x84=0x19000000`, `0x88 |= 1<<16`.
3. OTP(BAR0 `0x4120`부터 `0xE0`바이트)에서 스테핑(`s`)·안테나 업체(`V`) → 파일 `bt4378b1-atlantisb-<m|u>.bin/.ptb`.
4. 부트 단계 0·RTI 0 확인 → 호스트 창(BAR0 `0x590`–`0x598`) → 펌웨어 주소·크기(BAR2 `0x200478`–`0x200480`) → 도어벨(BAR0 `0x140`) → 부트 단계 2(5초).
5. RTI 1 → RTI 창 + 문맥 주소(BAR2 `0x200494`–`0x20049C`, `0x20048C`/`0x200450`) → RTI 2.
6. 완료 링 1–4, 전송 링 1–6 생성(제어 메시지, 각각 응답 확인).
7. HCI: 빔포밍 보정 데이터(0xFD97, 0xE6바이트 조각) → PTB(0xFE0D, 0xCF바이트 조각) → Read_BD_ADDR, 기본값(93:76:00:xx:78:43)이면 ADT 주소를 0xFC01로 → Read_Local_Version.
8. 자식 PDO 생성 → BthMini.

BthX 기능 값(QUERY_CAPABILITIES): MaxAclTransferInSize 1021, **ScoSupport = ScoSupportHCIBypass(2), MaxScoChannels = 1**(문서상 필수값, s261e는 0/0이라 BthMini가 0xC0000182로 시작 실패 — s261f에서 고침), 유휴·깨우기 FALSE.

하면 안 되는 일: 제어 페이지 CMD(0x10) 쓰기, DART·apcie 포트·SMC·GPIO·ECAM 직접 매핑, 기능 0 설정 공간 접근, INTx 켜기, `0xFFFFF000`이나 `[1 MiB, 0xBFF00000)` 밖 주소를 칩에 주기, 버스 마스터가 켜진 채 DMA 메모리 해제.

## 7. 모르는 점

| 항목 | 언제 알 수 있나 |
|---|---|
| 기능 1 BAR 배정·창 넓히기가 실기에서 통과하는가(기능 0·USB-A 영향 없음 포함) | 실기 1단계 `[s261] READY fn1`, B0 |
| 기능 1이 FLR을 지원하는가, FLR 뒤 BAR가 복원되는가 | B5 `StopAfter=2`, `NwbtResetResult` |
| MSI 없이 펌웨어가 부팅·링 진행을 하는가 | B5 `StopAfter=5/6/7` |
| 기능 1 DMA가 실제 RAM에 닿는가(RID→SID) | `StopAfter=5`(부트 단계 2 = 칩이 펌웨어를 DMA로 읽음) |
| 와이파이 드라이버의 칩 리셋이 블루투스를 멈추는가 | B7 |
| BthMini가 이 전송을 받아 라디오를 만드는가 | B1/B2 `RADIO kind=Bluetooth` |
