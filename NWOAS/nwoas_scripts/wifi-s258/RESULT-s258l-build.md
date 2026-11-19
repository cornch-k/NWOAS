# 와이파이 s258l(1.0.1.3) 빌드 결과와 실기 시험 순서 (2026-11-19 03:28)

## 결론
**s258l은 Windows가 약 60초마다 묻는 통계 명령(34번 = `WDI_GET_STATISTICS`)에 "지원 안 함" 대신 실제 통계를 답하는 판입니다. 빌드·서명·INF 검사는 통과했고, 끊김이 없어지는지는 아직 모릅니다 [빌드만].**
s258k의 끊김 3번은 모두 Windows가 보낸 연결 끊기 명령(8번 = `WDI_TASK_DISCONNECT`)을 드라이버가 따른 것이었습니다 [사실]. Windows가 왜 끊기 명령을 보냈는지는 기록만으로 확정되지 않았습니다. 이번 시험에서 (1) 통계에 답하면 끊김이 멈추는지, (2) Windows 이벤트 기록에 남은 끊김 사유가 무엇인지를 함께 봅니다.

## 1. 명령 번호 [사실, WDK 10.0.26100 `km\dot11wificxintf.h`(`dot11wdi.h`도 같은 값)]

| 번호 | 이름 | s258k 답 |
|---|---|---|
| 4 | `WDI_TASK_SCAN` | 성공 |
| 6 | `WDI_TASK_CONNECT` | 성공 |
| 7 | `WDI_TASK_DOT11_RESET` | 성공 |
| 8 | `WDI_TASK_DISCONNECT` | 성공(이때 끊음) |
| 28 | `WDI_SET_PRIVACY_EXEMPTION_LIST` | 성공(아무것도 안 함) |
| 29 | `WDI_SET_ADD_CIPHER_KEYS` | 나중에 완료(키 설치) |
| 34 | `WDI_GET_STATISTICS` | **0xC00000BB(지원 안 함)** |
| 75 | `WDI_ABORT_TASK` | 지원 안 함(시작할 때 1번) |
| 85 | `WDI_SET_ADAPTER_CONFIGURATION` | 성공(아무것도 안 함) |
| 138 | `WDI_SET_LOCATION_PRIVACY` | 지원 안 함(시작할 때 1번) |

Microsoft 문서(`OID_WDI_GET_STATISTICS`)에 따르면 이 명령은 "MAC·PHY 통계를 돌려 달라"는 요청이고, 결과 TLV 두 개(`WDI_TLV_MAC_STATISTICS`, `WDI_TLV_PHY_STATISTICS`)는 둘 다 선택 항목이 아닙니다.

## 2. 끊김 앞뒤 순서 (s258k 기록 `runs/s258k-20261119-024039.txt`) [사실]

연결 4번 모두 같은 순서였습니다. 다만 4번째 연결에서는 마지막 끊기 명령이 오지 않았습니다.

| 연결 | 연결 명령(초) | 연결된 채 검색(초, 연결 뒤) | 주기 밖 통계 질의(초, 검색 뒤) | 끊기 명령(초, 통계 질의 뒤) | 연결 명령의 후보 AP 수 |
|---|---|---|---|---|---|
| 1 | 89.9 | 271.1 (181.1) | 311.3 (40.3) | 312.3 (0.9) | 4 |
| 2 | 314.6 | 495.7 (181.1) | 542.7 (47.0) | 558.7 (16.0) | 4 |
| 3 | 561.1 | 742.2 (181.1) | 788.3 (46.1) | 796.3 (8.0) | 4 |
| 4 | 798.6 | 979.7 (181.1) | 1,020.3 (40.6) | 없음(관찰 끝 1,934초까지) | 3 |

- 끊기 명령의 802.11 사유 값은 세 번 모두 4였습니다(802.11 표준에서 4 = "활동이 없어 연결 해제").
- 다시 붙는 연결 명령은 "로밍 연결"(RoamRequest=1)이고, 로밍 사유 값은 7(`WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST`)이었습니다. 7은 드라이버가 끊은 뒤 Windows에 보낸 연결 해제 알림의 값과 같습니다.

## 3. 원인 판단

| 구분 | 내용 | 근거 |
|---|---|---|
| 사실 | 끊은 쪽은 Windows(끊기 명령) | `NwoaHostDisassocLog`=`3@312253 3@558731 3@796262` |
| 사실 | 통계 질의는 매번 "지원 안 함"이었고, 끊기 명령마다 그 직전 1~16초 안에 주기 밖 통계 질의가 1번 있었음 | `NwoaCmdLog` |
| 사실 | 4번째 연결에서는 같은 실패가 계속됐지만 끊기 명령이 없었음 | `NwoaCmdLog` 1,020~1,934초 |
| 가설 1 | Windows가 검색 뒤 연결 상태를 점검하면서 통계를 묻고, 답을 못 받으면 "활동 없음"(사유 4)으로 보고 다시 연결한다. 3번 뒤 멈춘 것은 Windows 쪽 재시도 횟수 제한 때문이다 | 시간 순서와 사유 4는 맞음. 4번째에 멈춘 이유는 확인 안 됨 |
| 가설 2 | Windows WLAN 서비스의 "제한된 연결 → 자동 복구"(이벤트 4003)가 인터넷 확인 결과로 끊고 다시 붙인다(통계와 무관) | Microsoft Q&A에 같은 동작 보고. 우리 기기에서는 확인 안 됨 |
| 가설 3 | 같은 이름의 AP가 4개라 Windows가 더 나은 AP로 옮기려고 끊고 다시 붙는다. 4번째에 후보가 3개로 줄어 멈췄다 | 후보 수 변화는 사실, 옮긴 AP가 같은지 다른지는 기록 없음 |

가설 1은 이번 판으로, 가설 2는 W15(이벤트 4003·8003의 사유)로, 가설 3은 새 기록 비트(아래 4장)로 가릅니다.

## 4. s258l에서 바꾼 것 [빌드만]

| 바꾼 것 | 내용 | 끄는 값 |
|---|---|---|
| 통계 질의(34)에 답함 | 연결 중이면 AP·브로드캐스트 MAC 통계 2개, 아니면 브로드캐스트 1개, PHY 통계 1개. 값은 드라이버 계수(받은 프레임, 보낸 완료, 보내기 버림). Microsoft WDI 예제 `Wdi_Get_Statistics`와 같은 모양. 출력 칸이 모자라면 필요한 크기를 알리고 BUFFER_OVERFLOW | `StatsReply=0` (작업 `W16-statsreply-0.cmd`, 되돌리기 `-1.cmd`) |
| 검색 기록 | 정보 칸 4~7비트에 검색 계기(`WDI_SCAN_TRIGGER`: 1 수동, 2 백그라운드, 3 로밍, 4 연결) | 해당 없음 |
| 연결 기록 | 정보 칸 19비트 = 첫 후보가 직전에 붙었던 AP와 같음(BSSID 값은 남기지 않음) | 해당 없음 |
| 끊기 기록 | 정보 칸 16비트 = 끊을 상대가 지금 AP | 해당 없음 |
| 통계 계수 | `NwoaCntStatsOk` / `NwoaCntStatsFail` / `NwoaCntStatsSmall`, `NwoaStatsLastBytes`, `NwoaStatsLastStatus` | 해당 없음 |

소스: `port-j274/wifi/bcm4378-wificx/src/driver/wifi.cpp`(`StatsReply`, 명령 분기), `nwoa.h`, `diag.cpp`, `driver.cpp`. 설계 메모: `research-20261118/wifi/DRIVER-SIDE.md` s258l 장.

## 5. 빌드 [빌드만]

| 항목 | 값 |
|---|---|
| 출력 | `port-j274/wifi/bcm4378-wificx/out/20261119-0328-s258l/package` |
| 단위 시험(개) | 142 통과 / 0 실패 |
| MSBuild 경고 / 오류(개) | 0 / 0 |
| INF | `DriverVer = 11/19/2026,1.0.1.3`, infverif 0, inf2cat 오류·경고 없음 |
| 서명 | .sys·.cat 모두 시험 인증서 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF`, .sys가 .cat에 들어 있음(가상머신에서는 루트를 믿지 않는다는 표시가 s258k와 같게 나옴) |
| nwoa4378.sys 크기(바이트) | 143,768 (s258k 141,720) |

| 파일 | SHA-256 |
|---|---|
| nwoa4378.sys | `3693b856cdb9125cb40b8788d0f58735b7232082029376dd5d5bfdc2b62759d4` |
| nwoa4378.cat | `6df04fee6a8f29bab405fe9319467eb59fc5b3fc29f146ab285f99f1332bffa9` |
| nwoa4378.inf | `5f6251724c9666587f087f3c25122d950791d55f8b6053087f5ab99ae998840e` |
| 펌웨어 5개·보드 파일 | s258k와 같음(`7230e9ea…` trx 등) |
| 전달 매니페스트 `file-delivery-s243/manifests/wifi-j274-20261119-0328-s258l.json` | `b97a50c03173d5352d2877da9faaf87fa4b446fecb390d410fa935a2617f26c8` |
| 전달 작업 `file-delivery-s243/jobs/wifi-j274-20261119-0328-s258l.cmd` (3,620바이트) | `122cc6ad6accbdc5fb8bef15c74e494d770a721fd6a7b859436196dc4faad5e1` |
| 설치 작업 `wifi-s258/jobs/W1-install-wifi-s258l.cmd` | `c16eea1198a1e9610de17fd4bdc38a3194c6ebf90f40f8e829fc6a627ca520a6` |
| 이벤트 작업 `wifi-s258/jobs/W15-wlan-events.cmd` (3,184바이트) | `423ec08e67b681701139eca3178cd3a82dfc043acf86ac7772ff1a71510de7a1` |

W15는 가상머신에서 SYSTEM으로 문법 시험만 했습니다(그 가상머신에는 와이파이가 없어 이벤트 0개). 맥미니에서는 돌려 보지 않았습니다.

## 6. 실기 시험 순서 (콘치님이 실행)

설치는 W1 제자리 덮어 설치입니다. W9(제거)는 쓰지 않습니다(저장된 와이파이 연결 정보가 지워짐).

| 순서 | 할 일 | 명령 / 작업 | 확인 |
|---|---|---|---|
| 1 | 전달 매니페스트로 Windows 다시 시작 | `NWOAS_AUDIO_PROT=moderate NWOAS_DELIVER_MANIFEST=…/file-delivery-s243/manifests/wifi-j274-20261119-0328-s258l.json bash wifi-s259/run-s259.sh <지금 실행기 .out> <지금 LOG.link>` | `S259 DONE link=…` |
| 2 | 파일 전달 | `file-delivery-s243/jobs/wifi-j274-20261119-0328-s258l.cmd` | `S243 READBACK MATCH` 9개, `S243 DELIVER COMPLETE` |
| 3 | 제자리 설치 | `wifi-s258/jobs/W1-install-wifi-s258l.cmd` | `PNPUTIL EXIT 0`, DriverVersion `1.0.1.3`, status OK |
| 4 | 30분 이상 관찰 | 5분마다 `wifi-s258/jobs/W2-status.cmd` | 아래 합격 기준 |
| 5 | Windows 이벤트 | `wifi-s258/jobs/W15-wlan-events.cmd` | `W15 COUNT3H`·`W15 EV` 줄 |

1~5를 한 번에 하는 스크립트: `bash wifi-s258/run-s258l.sh <지금 실행기 .out> <지금 LOG.link> 1800` (s258k 때 쓴 `run-s258k.sh`와 같고, 끝에 W15만 더함. 결과 `wifi-s258/runs/s258l-<시각>.txt`).

선택(합격했을 때 원인 확정용): `W16-statsreply-0.cmd`로 통계 답을 끄고 다시 20분 관찰 → 끊김이 다시 생기면 통계 미응답이 원인이라는 것이 확정됩니다. 끝나면 `W16-statsreply-1.cmd`로 되돌립니다.

## 7. 합격 기준

| 항목 | 합격 | s258k 값 |
|---|---|---|
| `NwoaHostDisassocLog`(연결 뒤 30분 안의 `3@…` 개수) | 0 | 3 |
| `NwoaCmdLog`의 34번 줄 | `34/1/0/<바이트>` (상태 0) | `34/1/c00000bb/0` |
| `NwoaCntStatsOk` | 30분에 약 30 이상, 계속 증가 | 없음 |
| `NwoaCntStatsFail` / `NwoaCntStatsSmall` | 0 / 0 | 없음 |
| W15 `COUNT3H 8003`(설치 뒤 새로 생긴 연결 끊김 이벤트) | 0 | 확인 안 함 |
| W15 `COUNT3H 4003`(제한된 연결 자동 복구) | 0 | 확인 안 함 |
| 장치 상태 | OK, 자동 재연결 | OK |

판정:
- 34가 상태 0인데도 끊기 명령(`3@…`)이 생기면 가설 1은 틀린 것입니다. 그때는 W15의 8003 사유 문장과 4003 유무로 가설 2를, 끊기 뒤 연결 명령의 19비트(`6/0/0/…`의 0x80000)로 가설 3(같은 AP로 다시 붙는지)을 가립니다.
- `NwoaCntStatsSmall`이 0보다 크면 Windows가 준 출력 칸이 작은 것이라 크기 처리를 다시 봅니다.
- 장치에 문제가 생기면 재설치 없이 `W16-statsreply-0.cmd`로 s258k 동작으로 돌아갈 수 있습니다.

## 8. W15가 출력하는 것과 가리는 것

- `W15 NOW … UP_MS …`: 부팅 뒤 시간. 이벤트마다 `t_ms=`(부팅 뒤 ms)를 붙여 드라이버 기록의 `@ms`와 맞춰 볼 수 있습니다(드라이버 시각은 `KeQueryInterruptTime` 기준이라 몇 ms 어긋날 수 있음).
- `W15 COUNT3H <id> <개수>`: 최근 3시간의 4003, 8001(연결), 8002(연결 실패), 8003(끊김), 11004·11005(보안 중지·시작), 11010, 12013 개수.
- `W15 EV …` 최근 40개(오래된 것부터): ` D `는 이벤트 데이터 이름=값(끊김 사유 코드 포함), ` M `는 이벤트 문장(420자까지).
- 가림: 이름에 SSID·Profile·Mac·Peer·Addr·Guid·Bss·Name이 들어간 값은 `<m>`으로 바꾸고, 같은 값이 문장에 나오면 문장에서도 바꿉니다. MAC 모양 문자열은 모두 `<mac>`. 와이파이 암호·키는 이 기록에 들어 있지 않습니다.
- `netsh wlan show interfaces`는 SYSTEM에서 위치 권한 오류가 나서 쓰지 않았습니다. 대신 `Get-NetAdapter`(상태·속도), `Get-NetAdapterStatistics`(바이트·패킷·버림·오류), `Get-NetConnectionProfile`(IPv4·IPv6 인터넷 연결 판정 = Windows 인터넷 확인 결과), WMI `MSNdis_*`를 출력합니다. 가상머신 시험에서 `MSNdis_80211_ReceivedSignalStrength`는 "지원 안 함"이 나왔으므로 맥미니에서도 안 나올 수 있습니다(신호 세기는 드라이버 `NwoaRssiMin/Max`로 봅니다).
- 마지막에 드라이버 레지스트리(`NwoaDriverBuild`, `NwoaHostDisassocLog`, `NwoaCmdBeforeLoss`, `NwoaCmdHist`, `NwoaLossLog`, 통계 계수, RSSI)를 붙입니다.
