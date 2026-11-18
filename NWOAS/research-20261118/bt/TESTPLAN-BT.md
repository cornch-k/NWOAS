# 내장 블루투스 실기 시험 계획 (S261, 2026-11-18)

표기: **[사실]** / **[가설]** 은 계약서(`CONTRACT-BT.md`)와 같습니다. 아래는 모두 **아직 실기에서 돌리지 않은 계획**입니다. 오케스트레이터가 맥미니에서 돌리고, 단계마다 앞 단계가 통과해야 다음으로 갑니다.

## 0. 결론

- 시험은 **부팅(하이퍼바이저·ACPI) → 드라이버 전달 → 단계 중단(StopAfter) 1→8 → 전체(0) → 와이파이와 함께 → 재부팅** 순서입니다.
- 첫 드라이버 적재는 `StopAfter=1`(제어 페이지만, BAR 접근 0회)로 시작합니다. 와이파이 때처럼 어느 단계에서 SError·멈춤이 나는지 한 칸씩 좁힙니다.
- 문제가 생기면 언제든 `B9-uninstall-bt.cmd` 또는 `NWOAS_BT=0`(블루투스 장치 숨김, 와이파이는 S259와 같음)으로 돌아갑니다.

## 1. 맥북에서만 하는 확인(하드웨어 없음)

```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS
PY=m1n1_windows/.venv-hv/bin/python3
$PY -I -B nwoas_scripts/bt-s261/test_bt_hv.py        # 18개, S261 하이퍼바이저 쪽
$PY -I -B nwoas_scripts/wifi-s259/test_wifi_hv.py    # 21개, S259가 그대로인지
bash port-j274/bt/bcm4378-bthx/tests/run-tests.sh    # 677개 검사, 드라이버 핵심 논리
```

| 결과(2026-11-18 이 에이전트) | 값 |
|---|---|
| test_bt_hv.py | 18 통과 |
| test_wifi_hv.py | 21 통과 |
| run-tests.sh | 677 검사 통과(ASan/UBSan) |
| 실행기 지문 | 36개 모두 실제 파일과 같음 |

## 2. 실기 순서

공통: `L=<LOG>.link`, 작업 넣기 `python3 -I nwoas_scripts/transport-s123/queue_job.py $L <job>`. 하이퍼바이저 줄은 `grep -a -E '\[s259\]|\[s261\]' <LOG>`.

### 1단계 — S261 부팅 + 드라이버 파일 전달

```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS
NWOAS_DELIVER_MANIFEST=$PWD/nwoas_scripts/file-delivery-s243/manifests/bt-j274-20261118-1806-s261e.json \
  bash nwoas_scripts/bt-s261/run-s261.sh <지금 실행기 .out> <지금 LOG.link>     # 대기 상태면: idle -
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/file-delivery-s243/jobs/bt-j274-20261118-1806-s261e.cmd
```

| 확인 | 볼 곳 | 통과 | 실패면 |
|---|---|---|---|
| A 게스트 전 | LOG | `[s261] BOOT ADT bluetooth cal=5664 calbf=5664 bdaddr=present NWOAS_BT=1`, `[s261] BOOT BAR emulation ... bt-s261-bar-emul 0x6e0900000+0x8000` 와 `0x6e0c00000+0x400000`, `[s261] BOOT control pages HOOK` | `STOP` 문구대로. ADT 문제면 `NWOAS_BT=0` |
| B 칩 + 기능 1 | LOG | `[s259] READY ...` 다음 `[s261] READY fn1 ids=0x5f6914e4 ... window=0xe0f0e000 epcmd=0x402` | `[s261] FAIL fn1 <코드>`: 와이파이는 그대로여야 함(그 뒤 `fn1 undo`). 코드별: FN1_LAYOUT(크기), FN1_BARS(되읽기), WINDOW, PORT1_CHANGED |
| C 회귀 | B0 작업 출력 | `WIFI OK`, `USB present=` 값이 S259 때와 같음, 와이파이 연결 상태 | 같은 페이로드로 `NWOAS_BT=0` 부팅해 비교 |
| D 장치 | B0 | `DEV ACPI\NWOA5F69\0 ... problem=28`, `MEM 6E0900000-6E0907FFF`, `MEM 6E0C00000-6E0FFFFFF`, `MEM 348004000-348007FFF`, IRQ 없음 | problem 12면 NWC1 `_CRS` 확인. `BT ABSENT`면 B의 FAIL 확인 |
| E 전달 | 전달 작업 | `S243 DELIVER COMPLETE`, 7개 `READBACK MATCH` | 매니페스트 지문 확인 |

### 2단계 — 설치(StopAfter=1)와 단계 중단

```bash
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/bt-s261/jobs/B1-install-bt.cmd
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/bt-s261/jobs/B5-stopafter-2.cmd   # 3,4,...,8 차례로
```

판정은 모두 `reg query` 출력(`Device Parameters`)의 `NwbtStage`와 아래 값, 그리고 LOG에 하이퍼바이저 예외·SError가 없을 것.

| StopAfter | 하는 일(새로 하는 것) | 통과(NwbtStage와 값) | 실패면 의심할 것 |
|---|---|---|---|
| 1 (B1) | 제어 페이지 확인, ADT 값 읽기. **BAR 접근 없음** | `stopafter_1_ctlpage`, `NwbtCtlState=1`, `NwbtCtlIds=0x5f6914e4`, `NwbtCalLoaded=0x1620`, `NwbtBoard=atlantisb`, `NwbtResources=7` | `fail_control_page`(BT 페이지 STATE·ERROR 값), `fail_resources`(자원 길이) |
| 2 | FLR, 버스 마스터, 설정 창 6개. **아직 BAR 접근 없음** | `stopafter_2_config`, `NwbtResetResult=1`(2면 FLR 없음: 계속 진행 가능), `NwbtFn1Command=0x406`, `NwbtCfgBad=0`, LOG `[s261] reset: FLR done ok=True` | `fail_flr`, `fail_config_windows`(허용 표), 하이퍼바이저 `fn1 config write refused` 줄 |
| 3 | **첫 BAR 접근**: OTP 224바이트(8비트 읽기) | `stopafter_3_otp`, `NwbtStepping=b1`, `NwbtVendor=m`(와이파이 OTP도 V=m) | SError·하이퍼바이저 예외 → BAR 에뮬레이션(BYPASS) 범위. `fail_otp`면 `NwbtOtpHead` |
| 4 | 부트 단계·RTI 읽기(BAR2), 파일, DMA 메모리 | `stopafter_4_files`, `NwbtBootstageBefore=0`, `NwbtRtiBefore=0`, `NwbtFirmwareLen=0x578cd`, `NwbtDmaBlkPa` < `0xBFF00000` | `fail_not_fresh`(FLR이 부트 단계를 안 지움 → 칩 전원 순환 필요), `fail_files` |
| 5 | 펌웨어 부팅(칩이 DMA로 펌웨어를 읽음) | `stopafter_5_boot`, `NwbtBootstage=2`, `NwbtBootMs` < 5000, `NwbtBootstageSeen` | `fail_boot_bootstage`: 0에 머물면 DMA가 안 닿음(RID→SID/DART) 또는 MSI 필요 → B6-MsiToRam-1 |
| 6 | RTI 1→2, 문맥 주소, 폴링 시작 | `stopafter_6_rti`, `NwbtRti=2`, `NwbtCntPoll` 증가 | `fail_rti` |
| 7 | 완료 링 4개·전송 링 6개(제어 메시지) | `stopafter_7_rings`, `NwbtRingsCreated=10`, `NwbtCntBadEntry=0` | `fail_create_rings`: 제어 응답이 안 옴 → MSI 가설(B6-MsiToRam-1) |
| 8 | 보정 데이터(25조각), PTB(4조각), BD 주소, Read_Local_Version | `stopafter_8_hci`, `NwbtCalSent=25`, `NwbtCalStatus=0`, `NwbtPtbSent=4`, `NwbtBdState`=1 또는 2, `NwbtManufacturer=0xf`[가설: Broadcom 번호] | `fail_calibration_ptb`(상태 값), `fail_bd_address`(`NwbtBdState`=3이면 0xFC01 거절 → B6-SetBdaddr-0) |
| 0 | 전부 + BthMini 자식 | `ready_bthmini_child`, `DEV NWBT\BCM4378BTH\... status=OK`, `RADIO kind=Bluetooth state=On`, `NwbtBthxOpen=1`, `NwbtCntCmdTx`·`NwbtCntEvt` 증가 | 자식이 problem이면 B2의 `EVT` 줄(BTHPORT/BthMini), `NwbtCntDropFifo`·`NwbtCntBadEntry` |

### 3단계 — 사용 확인

| 확인 | 방법 | 통과 |
|---|---|---|
| 설정에 "Bluetooth" | 콘치님 눈(설정 → Bluetooth 및 장치) + B2 `RADIO kind=Bluetooth` | 켜기/끄기 스위치가 보임 |
| 장치 검색 | 휴대폰 등을 검색 가능 상태로 둔 뒤 `B3-scan.cmd` (이름은 앞 두 글자만 출력) | `SCAN FOUND total=` 1 이상, `NwbtCntEvt` 증가 |
| 와이파이와 함께 | `B7-wifi-coexist.cmd` (와이파이 장치 재시작) 뒤 B2·W10 | 블루투스 `NwbtDead=0` 또는 자동 재시작 뒤 `ready_bthmini_child`(`NwbtAutoRestarts`), 와이파이 다시 연결 |
| 재부팅 | W4 재시작 → `run-s261.sh` → B2 | 설치된 상태(StopAfter 없음)로 바로 `ready_bthmini_child` |

## 3. 바꿔 볼 수 있는 값(B6 작업, 장치 재시작 포함)

| 작업 | 값 | 언제 |
|---|---|---|
| `B6-MsiToRam-1.cmd` | MSI를 호스트 RAM 한 페이지로 켬 | 5·7단계에서 칩이 진행하지 않을 때(가설: 칩이 MSI를 기다림). `NwbtCntMsi`가 늘면 칩이 MSI를 보냄 |
| `B6-DoFlr-0.cmd` | FLR 안 함 | FLR 자체가 문제일 때(그 대신 첫 적재만 가능) |
| `B6-FixQuirks-0.cmd` | 이벤트 고치기 끔 | 고치기가 BthPort 초기화를 방해한다고 볼 때 |
| `B6-PokeAwake-0.cmd` | 패킷마다 SLEEP_CONTROL 쓰기 끔 | 쓰기 횟수를 줄여 볼 때 |
| `B6-SetBdaddr-0.cmd` | 칩 기본 주소 유지 | 0xFC01이 거절될 때 |
| `B6-PollIntervalMs-1.cmd` | 1 ms 폴링 | 응답이 느릴 때 |
| `B6-reset-knobs.cmd` | 위 값 전부 지움(StopAfter는 둠) | 비교가 끝난 뒤 |

## 4. 되돌리기

| 상황 | 방법 |
|---|---|
| 드라이버를 빼고 싶음 | `B9-uninstall-bt.cmd` → 재시작 |
| 장치 자체를 숨기고 싶음 | `NWOAS_BT=0 bash nwoas_scripts/bt-s261/run-s261.sh ...` (와이파이는 S259와 같음) |
| S259로 완전히 돌아가기 | `bash nwoas_scripts/wifi-s259/run-s259.sh ...` (S259 페이로드에는 BTH0가 없음. 설치된 블루투스 드라이버는 장치가 없어 그냥 쉼) |

## 5. 위험(가설)

| 위험 | 내용 | 대책 |
|---|---|---|
| MSI 없이 칩이 멈춤 | Linux는 MSI만 씀. 칩이 MSI 쓰기 완료를 기다릴 수 있음 | `MsiToRam=1` |
| 기능 1 DMA가 다른 스트림 | RID 0x101이 표 밖 스트림으로 가면 부트 단계 2가 안 옴 | `NwbtCtlRid2Sid` 값과 5단계 결과로 판단, 하이퍼바이저에 RID→SID 쓰기 추가 검토 |
| 와이파이 칩 리셋이 블루투스를 멈춤 | 와이파이 드라이버의 감시 타이머 리셋 | 1초마다 RTI 확인 → 자동 재시작(부팅당 3번) |
| 와이파이 D3가 칩 전원을 끔 | 와이파이 장치를 끄면 블루투스도 꺼짐 | 계약 5장. 와이파이 장치를 사용 안 함으로 두지 말 것 |
| HOOK 시간 | 보정 데이터 1,416번 + 시작 때 수십 번 제어 페이지 접근(각 수 ms) | 시작이 수 초 늘어남. 문제면 패키지 파일로 바꾸는 판 |
| BthPort가 이 컨트롤러의 명령을 거절 | 4378의 알려진 문제 3가지는 고치기로 막음(MWS 명령 숨김, LE Coded PHY 숨김, 확장 광고 PHY 정리) | `FixQuirks`로 끄고 켜며 비교 |

## 6. 산출물(빌드만 함, 실기 미시험)

| 파일 | 크기(바이트) | sha256 |
|---|---|---|
| `nwoas_scripts/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin` | 33,669,120 | `26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92` |
| BT SSDT AML (`NwoasBtJ274Ssdt.aml`) | 700 | `068422958f1bee4ef2a07e8a415931a2b6c2eb3b949dc9716b9657d63ed1c0d7` |
| 와이파이 SSDT AML (S259와 같음) | 701 | `bcdcbb03b167eac4a845a8efcb4ba05a0a6f9caa46732b76fd63364221643254` |
| `port-j274/bt/bcm4378-bthx/out/20261118-1806-s261e/package/nwbt4378.sys` | 54,168 | `2e0448fa369c3c99a99c48afdb357ab53b1065283763d5efef25cf1bda74f177` |
| 같은 폴더 `nwbt4378.inf` | 2,195 | `6eaad0eb4f04fc1532101f5a745cd7c3800047cf2861ad0d1e0d00143051aa3e` |
| 같은 폴더 `nwbt4378.cat` | 4,333 | `0562a5d9fae1a425159ced3baf0f2793c594721f3d870c821b525764ccac55f4` |
| 펌웨어 `bt4378b1-atlantisb-m.bin` / `-u.bin` (Apple 파일, 비공개 패키지만) | 358,605 / 358,605 | `11d6aa02…7443f` / `dc046e4d…a7b81` |
| PTB `-m.ptb` = `-u.ptb` (내용 같음) | 786 | `75a2726d…9e0a1` |
| 전달 매니페스트 `file-delivery-s243/manifests/bt-j274-20261118-1806-s261e.json` | — | `4fd4505eee3e119e1b235930ef6a9fe10f843ab6cef07d868771488a7922aa69` |

드라이버 빌드: VS 2022 + WDK 26100, Release|ARM64, 경고 0·오류 0, 서명 인증서 `13338B38…DBFF`(GPU·소리·와이파이와 같음), infverif 통과, 카탈로그 포함 확인.

## 7. 1차 실기(18:08–18:44) 뒤 고친 판 s261g (하이퍼바이저 S261b + 드라이버 1.0.0.3)

### 7.1 BthMini Code 10 (0xC0000182)
- [사실: 실기] 전송 쪽 StopAfter 2–8 통과, SError 0. `NwbtBthxVersionSet=1`, `NwbtCntCmdTx=0`, `NwbtCntReadDone=0`, `NwbtCntEvt=0x21`(= 우리 초기화 응답 33개) → BthMini는 기능 값을 받은 뒤 HCI를 하나도 보내지 않고 실패.
- [사실: 문서] `_BTHX_CAPABILITIES`: ScoSupport는 ScoSupportHCIBypass, MaxScoChannels는 1이어야 함. s261e는 None(0)/0.
- [가설] 이것이 원인. 2 / 1로 고침(레지스트리 `ScoSupport`·`MaxScoChannels`). BthMini의 모든 IOCTL을 `NwbtIoLog`·`NwbtIoLogLast`·`NwbtIo*`에 기록.

### 7.2 Windows 멈춤: fn1 BAR2+0x20045C 동기 중단 5,453회
- [사실: 하이퍼바이저 기록 `logs/agxmed1g-s249-20261118-180900.1hpvLx`] 순서: B5-stopafter-0 재시작(FLR 성공, 드라이버 정상) → **와이파이 드라이버가 버스 마스터를 끄고 WLN0 `_PS3`**(`config write off=0x4 val=0x2`, `CMD 0x2`) → 칩 전원 꺼짐 → `CMD 0x1`로 다시 켜짐(epoch 2, fn1 BAR 다시 배정) → 와이파이 드라이버의 칩 리셋 앞뒤 LinkCtl 쓰기(`off=0xbc` 두 번) → 바로 다음 줄부터 `FAR 0x6e0e0045c` 중단 반복.
- [사실: 코드] 중단 난 명령(`ldr w8,[x8]; str w8,[x19,#0x6b8]; cmp w8,#2`)은 블루투스 드라이버의 1초 상태 확인(RTI 읽기)입니다. 이 확인은 제어 페이지 STATE를 먼저 보지만, 그 읽기가 와이파이의 전원 끄기·켜기 HOOK 처리 뒤로 밀려 STATE가 이미 다시 1이었던 것으로 보입니다 → 새로 켜진 칩(fn1 설정 창 0x84 등이 초기값)에 RTI를 읽어 중단.
- 원인 정리: (a) 칩 전원 순환(와이파이 장치 D3→D0)이 맞음. (b) 아님(같은 오프셋을 그 전 10분 동안 정상으로 읽음). (c) 중단은 BYPASS 대신 접근이 버스 오류를 게스트로 넘긴 것이고, Windows가 같은 명령을 다시 실행해 그 CPU가 묶임 [가설: 반복 이유]. `0xBC`는 **기능 0(와이파이) LinkCtl을 와이파이 드라이버가** 와이파이 페이지로 쓴 것(허용 표상 ASPM 두 비트만)이고, 칩 리셋의 앞뒤 표시일 뿐 중단의 원인은 아님.
- [모름] 와이파이 장치가 18:2x에 왜 D3로 갔는지(작업이 와이파이를 재시작했는지, Windows가 내렸는지). 오케스트레이터 쪽 작업 기록·Windows 시스템 로그로 확인 필요.

### 7.3 고친 것 (닫힌 쪽으로 실패)
- **하이퍼바이저(S261b):** fn1 BAR 두 구간 위에 HOOK "울타리"(`bt-s261-bar-fence`): 읽기는 모두 1(`0xFFFFFFFF`), 쓰기는 버림. 부팅 때 닫혀 있고, **칩 전원 끄기 직전·전원 켜기·와이파이 칩 리셋 표시(fn0 0xBC 쓰기)·`_PS3`** 때마다 닫고 GEN(세대)을 올림. 닫을 때 그 자리에서 페이지 표 갱신(tlbi)까지 하므로 다른 CPU의 다음 접근도 울타리로 감. 드라이버가 `ARM`(0x84)에 현재 GEN을 쓰면, 하이퍼바이저가 fn1 BAR·디코드를 다시 확인(지워졌으면 다시 씀)한 뒤에만 엶. `NWOAS_BT_FENCE_WIFI_RESET=0`이면 0xBC 때는 닫지 않음.
- **드라이버(1.0.0.3):** 시작 때 GEN 읽기 → ARM, 결과 1이 아니면 BAR를 하나도 건드리지 않고 `fail_arm`. 1초 확인은 GEN을 먼저 보고 바뀌었으면 BAR 접근 없이 죽음 처리(`NwbtDeadReason=1`). RTI·부트 단계 읽기가 `0xFFFFFFFF`면 즉시 중단(같은 레지스터를 다시 읽지 않음). 죽음 뒤로는 폴링·도어벨·상태 확인 모두 멈추고, 장치 자동 재시작(부팅당 3번)은 새 GEN으로 다시 ARM.
- 시험: `test_bt_hv.py` 22개(울타리 4개 추가: 부팅 때 닫힘·ARM, 전원 끄기 전에 닫힘, 0xBC 때 닫힘, ARM이 지워진 BAR를 되살림), `test_wifi_hv.py` 21개, 핵심 677개 모두 통과. 실행기 지문 36개 일치. 페이로드·SSDT는 바뀌지 않음.

### 7.4 순서(맥미니가 S261로 돌고 있을 때; 지금은 NWOAS_BT=0 부팅이므로 1번부터)

```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS
# 블루투스 드라이버가 들어 있는 채로 S261b(울타리)로 부팅: 옛 s261e는 ARM을 모르므로 BAR가 모두 1로 보여 시작 실패(안전)
NWOAS_DELIVER_MANIFEST=$PWD/nwoas_scripts/file-delivery-s243/manifests/bt-j274-20261118-1849-s261g.json \
  bash nwoas_scripts/bt-s261/run-s261.sh <지금 .out> <지금 LOG.link>
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/bt-s261/jobs/B9-uninstall-bt.cmd
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/file-delivery-s243/jobs/bt-j274-20261118-1849-s261g.cmd
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/bt-s261/jobs/B1-install-bt-s261g.cmd   # StopAfter=1
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/bt-s261/jobs/B5-stopafter-0.cmd
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/bt-s261/jobs/B10-ioctl-trace.cmd
python3 -I nwoas_scripts/transport-s123/queue_job.py $L nwoas_scripts/bt-s261/jobs/B7-wifi-coexist.cmd        # 이번 멈춤의 재현 시험
```

| 확인 | 통과 |
|---|---|
| LOG | `[s261] BOOT fn1 BAR fence HOOK installed (closed …)`, 드라이버 시작마다 `[s261] arm gen=N … ok=True`, `fence OFF (arm gen N)` |
| B10 | `NwbtArmResult=1`, `NwbtCapsScoSupport=2`, `NwbtCapsMaxScoChannels=1`, `NwbtIoLog`에 `W… t1 c03/3`(HCI_Reset), `R104 … t4 e/c03`, 자식 status=OK |
| B7 (와이파이 재시작) | LOG `fence ON (chip power down …)`, Windows가 계속 응답, 중단(DABORT) 0회, 블루투스 `NwbtDeadReason=1` 뒤 자동 재시작으로 다시 `ready_bthmini_child`, 와이파이 다시 연결 |
| 그다음 | B2 `RADIO kind=Bluetooth`, B3 검색 |

| s261g 파일 | 크기(바이트) | sha256 |
|---|---|---|
| nwbt4378.sys (1.0.0.3) | 57,240 | `5f7dbff0fb7b32a1c0d64bdbce2a2859a4cbfe53fca6d028711cedb854d29a7b` |
| nwbt4378.inf | 2,195 | `2964f295e25076a7fcc0be07ae732c18918a82f6078e5f84c340ce0e7ba1b844` |
| nwbt4378.cat | 4,333 | `352a45b76bf04c7ce58864cec0024653b1992fc4c236d299ffdeaabcf266817a` |
| 매니페스트 bt-j274-20261118-1849-s261g.json | — | `3957667eeeaf800f13a0a12d000cde025971afae7ec0596f7e59e4f8586fcbc1` |
| bt-s261/bt_module.py | — | `25dc374f5def0262b3f0eb28bcbb1ce75a7554a6f3932ae60a67e39a97ccae56` |
| bt-s261/nwoas_bt_s261.py | — | `79c6522cecf5a500997411a9b98f3a13970a05fc740079d66ec77c06a0e2104c` |
