# 와이파이 s258m(1.0.1.4) 빌드 결과와 실기 시험 순서 (2026-11-19 04:29)

## 결론
**s258m은 "Windows가 왜 같은 AP에 끊기 명령을 보내는가"를 가리기 위한 판입니다. 빌드·서명·INF 검사·단위 시험은 통과했고, 맥미니에서는 아직 돌려 보지 않았습니다 [빌드만].**
들어간 것은 세 가지입니다. (1) 검색·끊기·연결·키 명령마다 숫자와 비트만 남기는 기록(아래 4장), (2) 사양과 맞지 않던 보고 두 가지 수정(BSS 나이 정보 없음, 대역폭 20 MHz인데 속도 433 Mbps), (3) 가설을 하나씩 가르는 스위치 두 개(`ScanReportConnectedOnly`, `NoDisassocIndOnHostDisconnect`, 기본값 0 = s258l 동작). 그리고 Windows 와이파이 추적(ETW)을 켜고 끄는 작업(W17·W18), 게이트웨이 핑 작업(W19)을 만들었습니다.
이벤트 기록의 "AES-GCMP-128"은 드라이버가 GCMP로 연결한 것이 아닙니다. 코드상 CCMP가 아니면 연결과 키 설치를 모두 거절하는데 s258l에서는 둘 다 성공했기 때문입니다 [사실, 3장]. GCMP라는 표시가 어디서 왔는지는 새 기록의 `ap` 값으로 확인합니다 [가설].

## 1. WDK 열거값 [사실, WDK 10.0.26100 가상머신 헤더]

| 값 | 이름 | 출처(파일:줄) |
|---|---|---|
| 검색 계기 3 | `WDI_SCAN_TRIGGER_ROAM = 3` | `km\dot11wificxtypes.hpp:689` |
| 검색 계기 6 | `WDI_SCAN_TRIGGER_FAST_ROAM = 6` | `km\dot11wificxtypes.hpp:692` |
| 연결 상태 7 | `WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST = 7` | `km\dot11wificxtypes.hpp:621` |
| RoamNeededReason | `UINT32 RoamNeededReason; // Roaming needed reason` | `km\wlan\2.0\TlvGenerated_.hpp:1803` |
| RoamTrigger | `WDI_ROAM_TRIGGER_OTHER = 0`, `WDI_ROAM_TRIGGER_CRITICAL_BSS_TRANSITION_REQUEST = 1` | `km\dot11wificxtypes.hpp:730-734` |
| 끊기 사유 칸 | `UINT16 Disassociation80211Reason; // Reason for this host-triggered disassociation …` | `km\wlan\2.0\TlvGenerated_.hpp:1650` |
| 끊기 알림 사유 칸 | `WDI_ASSOC_STATUS DisassociationWABIReason;` | `km\wlan\2.0\TlvGenerated_.hpp:1702` |
| 로밍 명령 번호 | `#define WDI_TASK_ROAM 100` | `km\dot11wificxintf.h:127` |
| 암호 방식 | `WDI_CIPHER_ALGO_CCMP = 0x04`, `WDI_CIPHER_ALGO_GCMP = 0x08` | `km\dot11wificxtypes.hpp:498,501` |
| 같은 값(OS 쪽) | `DOT11_CIPHER_ALGO_CCMP = 0x04`, `DOT11_CIPHER_ALGO_GCMP = 0x08`, `DOT11_AUTH_ALGO_RSNA_PSK = 7` | `shared\wlantypes.h:103,106,52` |
| DS 정보 | `WDI_DS_CHANGED = 1`, `WDI_DS_UNCHANGED = 2`, `WDI_DS_UNKNOWN = 3` | `km\dot11wificxtypes.hpp:895-901` |

헤더에서 RoamNeededReason은 그냥 `UINT32`입니다. Microsoft 문서(WDI_TLV_CONNECTION_SETTINGS)는 이 칸의 형식을 `WDI_ASSOC_STATUS`라고 하고, "ROAMING_NEEDED 알림으로 시작된 로밍이면 그 알림의 사유를 담는다"고 적습니다. 우리 드라이버는 ROAMING_NEEDED를 보낸 적이 없습니다. 그러니 다시 붙을 때의 7은 Windows가 스스로 정한 값이고, 숫자로는 드라이버가 끊은 뒤 보낸 끊김 알림의 사유(7)와 같습니다 [사실]. 둘이 실제로 이어지는지는 H3 시험으로 봅니다 [가설].
802.11 사유 4는 표준 사유표에서 "활동이 없어 연결 해제"입니다.

## 2. 드라이버가 Windows에 알리는 값 점검

| 항목 | s258l 값 | 사양 | 판단 | s258m |
|---|---|---|---|---|
| 링크 정보 Bandwidth | 항상 20 | "Current Bandwidth in MHz" | **틀림**: 433 Mbps(5 GHz 고정값)는 80 MHz에서만 나오는 속도라 20 MHz와 함께 참일 수 없음 | 펌웨어 `chanspec`의 폭, 속도는 펌웨어 `GET_RATE`(12번) 값. 못 읽으면 예전 값. 대역 능력도 20/40(2.4 GHz)·20/40/80(5 GHz). `LinkInfoReal`(기본 1, 0이면 s258l) |
| BSS 항목 나이 정보 | 없음 | `WDI_TLV_BSS_ENTRY_AGE_INFO`(선택): 마지막 발견 시각(KeQuerySystemTime), 0 = 이번 검색, 1 = 저장분 | 빠져 있었음 | 항목마다 마지막으로 본 시각, 0(이번 검색). `BssAgeInfo`(기본 1, 0이면 s258l) |
| 프로브 응답의 타임스탬프 8바이트 | 0 | 802.11 TSF | 펌웨어 bss_info에 TSF 칸이 없어 채울 값이 없음 | 바꾸지 않음 |
| WMMQoSEnabled | FALSE | "WMM이 협상됐으면 1" | AP가 WMM을 쓰면 틀린 값일 수 있음. 다만 데이터 경로는 WMM 없이만 시험됨 | 응답에 WMM 요소가 있는지 `NwoaWmmNegotiated`로 기록만. 보고는 `ReportWmm`(기본 0 = s258l)로 따로 켬 |
| DSInfo | UNKNOWN(3) | "이전 연결과 같은 DS인가" | UNKNOWN도 허용되는 값. 같은 DS라고 단정할 근거가 드라이버에 없음 | 바꾸지 않음 |
| ReAssociation | FALSE | "재연결 요청을 썼으면 1" | 드라이버는 매번 끊고(DISASSOC) 새로 `join`하므로 FALSE가 맞음 | 바꾸지 않음 |
| 끊기 명령 뒤 끊김 알림 | 보냄(사유 7) | OID_WDI_TASK_DISCONNECT의 알림 목록에 있음("OS 명령으로 끊긴 경우도") | 사양대로 | 기본 그대로. H3 시험용 `NoDisassocIndOnHostDisconnect=1`만 추가 |

## 3. GCMP와 CCMP [사실]

| 근거 | 내용 |
|---|---|
| 코드 | `ParseConnect`: 첫 유니캐스트 방식이 CCMP가 아니면 연결을 `STATUS_NOT_SUPPORTED`로 거절. `ParseKeys`: 키 방식이 CCMP가 아니거나 16바이트 CCMP 키가 없으면 거절. 능력 보고도 CCMP만 있음 |
| s258l 기록 | `NwoaConnectParse` 0, `NwoaKeyPairwise` 0, `NwoaKeyGroup` 0, `NwoaPtkInstalled` 1 (각 7번) |
| Windows 이벤트 | 11000·11010: `Cipher=AES-GCMP-128 CipherVal=8`. 8001(연결됨): `CipherAlgorithm=AES-CCMP` |

그래서 드라이버와 Windows 사이의 실제 연결과 키는 CCMP였습니다. 11000·11010의 8은 연결을 시작할 때 Windows가 적은 값이고, 협상이 끝난 뒤의 8001은 CCMP입니다.
[가설] AP의 RSN 요소에 GCMP-128(8)과 CCMP(4)가 함께 있고, Windows가 시작 이벤트에는 AP 쪽 값을 적었을 수 있습니다. s258m의 `NwoaConnDiag`에 있는 `ap<마스크>`가 `110`(8번·4번 비트)이면 이 가설이 맞고, `10`이면 틀린 것입니다. `uc`·`mc` 칸은 Windows가 연결 명령에 넣은 방식 목록이고, `NwoaKeyDiag`의 `ci`는 키마다 실제로 받은 방식입니다.

## 4. 새 기록 (레지스트리 `Device Parameters`, 숫자·비트만)

SSID·BSSID·MAC·키 값은 남기지 않습니다. 같은 SSID인지는 드라이버 메모리 안에서만 비교합니다. 시각 `@ms`는 부팅 뒤 ms(기존 기록과 같은 기준)입니다.

| 값 | 언제 | 칸 (요청 항목) |
|---|---|---|
| `NwoaScanDiag` (최근 6개) | 검색이 끝날 때마다 | (a) `t` 계기, `d` 지정 검색, `c` 연결 중, `n` 찾은 수/보고한 수, `p` 지금 AP가 결과에 있음, `s` 없어서 연결 때 비콘으로 채움(H1 스위치일 때만), `r` 지금 AP RSSI, `k` 같은 SSID 안에서 RSSI 순위(1 = 가장 셈), `ss` 같은 SSID 수, `o` 가장 센 다른 같은 SSID 항목의 RSSI/대역, `ch`·`b`·`bw` 지금 AP 채널·대역·폭, `ie` IE 바이트, `cap` 능력 칸, `fw` 그 순간 펌웨어 GET_RSSI, `st` 검색 상태 |
| `NwoaDiscDiag` (최근 4개) | 끊기 명령(8)이 올 때 | (b) `r` 802.11 사유 전체, `port` 포트 번호, `cur` 상대가 지금 AP, `c` 연결 중, `scan`·`rx`·`tx`·`txd`·`lq` 마지막 검색 완료·수신·송신 요청·송신 완료·링크 상태 알림 뒤 지난 ms(4294967295 = 없음), `rx10`·`tx10`·`txd10` 앞 약 10초 동안 프레임 수, `span` 그 창의 실제 길이(ms) |
| `NwoaConnDiag` (최근 4개) | 연결(6)·로밍(100) 명령 | (c)(d) `rr` RoamRequest, `rnr` RoamNeededReason, `rt` RoamTrigger, `h` 숨은 네트워크, `mfp`, `bt` BSS 전환, `au` 첫 인증 방식, `uc<개수>:<첫 값>/<마스크>`, `mc<첫 값>/<마스크>`, `ap<AP 쌍방향 마스크>/<그룹>`, `[후보 비트/캐시 RSSI …]` — 후보 비트: 1 지금 AP, 2 직전 연결 AP, 4 우리 검색 결과에 있음, 8 5 GHz, 10 Windows가 프레임을 줌, 8~15비트 채널 |
| `NwoaKeyDiag` (최근 8개) | 키 추가(29)·삭제 | (d) `a` 1 추가/0 삭제, `ty` 키 종류, `n` 원소 수/받아들인 수, `ci` 첫 방식/마스크, `st` 최종 상태 |
| `NwoaCfgS258m` | 2초마다 | 지금 스위치 값 5개 |
| `NwoaLinkBwMhz`, `NwoaLinkRateKbps`, `NwoaFwChanspec`, `NwoaFwRate500k`, `NwoaWmmNegotiated` | 연결 때 | 펌웨어가 알려 온 폭·속도·WMM |
| `NwoaCntScanSynth`, `NwoaCntDisassocIndSkipped` | 누적 | H1·H3 스위치가 실제로 동작한 횟수 |

## 5. 스위치 (모두 `HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA4378\0\Device Parameters`, 장치 다시 시작 또는 부팅 때 읽음)

| 이름 | 기본 | 1(또는 2)이면 | 켜는 작업 / 끄는 작업 |
|---|---|---|---|
| `ScanReportConnectedOnly` | 0 | 1: 연결 중 검색은 지금 AP만 보고. 2: 전부 보고하고 지금 AP가 없으면 더함 | `W20-h1-scanconnonly-1.cmd` / `-0.cmd` |
| `NoDisassocIndOnHostDisconnect` | 0 | 끊기 명령 뒤 끊김 알림을 보내지 않음(사양과 다름, 시험용) | `W21-h3-nodisassoc-1.cmd` / `-0.cmd` |
| `BssAgeInfo` | 1 | 0이면 나이 정보 없음(s258l) | `W22-fixes-off.cmd` / `W22-fixes-on.cmd` |
| `LinkInfoReal` | 1 | 0이면 20 MHz·433/144 Mbps(s258l) | 위와 같은 작업 |
| `ReportWmm` | 0 | 1이면 WMM 협상 결과를 그대로 보고 | 작업 없음(필요할 때 만듦) |

W1 설치 작업은 위 다섯 값을 지워 기본값으로 시작합니다. 그래서 기본 실행은 s258l과 BSS 나이 정보·링크 폭·속도 보고가 다릅니다. 이 두 수정의 효과만 보려면 `fixoff` 실행과 비교합니다.

## 6. 빌드 [빌드만]

| 항목 | 값 |
|---|---|
| 출력 | `port-j274/wifi/bcm4378-wificx/out/20261119-0429-s258m/package` |
| 단위 시험(개) | 145 통과 / 0 실패 (s258l 142 + RSN 쌍방향 목록 3) |
| MSBuild 경고 / 오류(개) | 0 / 0 |
| INF | `DriverVer = 11/19/2026,1.0.1.4` (s258l과 이 줄만 다름), infverif 0, inf2cat 오류·경고 없음 |
| 서명 | .sys·.cat 모두 시험 인증서 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF`, .sys가 .cat에 들어 있음(가상머신에서 루트를 믿지 않는다는 표시는 s258l과 같음) |
| nwoa4378.sys 크기(바이트) | 151,448 (s258l 143,768) |

빌드 뒤 `nwoa.h` 주석 한 줄(로밍 명령 번호 133 → 100)만 고쳤습니다. 코드는 바뀌지 않았습니다.

| 파일 | SHA-256 |
|---|---|
| nwoa4378.sys | `5433de301ee088008545aaf050a09ab6ac243e46117f80291c1071061e8a1472` |
| nwoa4378.cat | `ce6d70a12ba9e446704ec4373ad14f267a6cf656c25bf1b6050e36aeca992593` |
| nwoa4378.inf | `3e3a6fa92f5d98863a89ffc60038e940f0728f460d44cc8efd4d29986f46dda5` |
| 펌웨어 5개·보드 파일 | s258l과 같음(`7230e9ea…` trx 등) |
| 매니페스트 `file-delivery-s243/manifests/wifi-j274-20261119-0429-s258m.json` (파일 12개 = 패키지 9개 + ps1 3개) | `10c54c15c2d939bb5ec7fdf2454c4d10d0b8d14f96d45e364da38915b830172f` |
| 전달 작업 1 `file-delivery-s243/jobs/wifi-j274-20261119-0429-s258m.cmd` (패키지, 3,620바이트) | `b67946fea132694e31da84a34538ac205e5ed70f5bf5f63ceab3b8ec057cc0eb` |
| 전달 작업 2 `…-s258m-scripts.cmd` (W17·W18·W19 ps1, 2,020바이트) | `7f33174a09478c1bae4645a1f9db2a78069eb18593010c7ad22fdfa4022ff11f` |

## 7. 작업 파일 (`wifi-s258/jobs`, 모두 ASCII, 4,032바이트 이하)

| 파일 | 크기(바이트) | 하는 일 | SHA-256 |
|---|---|---|---|
| `W1-install-wifi-s258m.cmd` | 3,759 | 제자리 덮어 설치(W9 아님), 스위치 값 지움 | `78558d6d7370419ef9ad9a2805802e50ec25f43f2334b008f6e5634c7a244585` |
| `W17-etw-wlan-start.cmd` + `src/W17-etw-wlan-start.ps1` | 459 + 3,260 | logman 세션 `NwoasWlan`, 순환 192 MB, `C:\NWOAS\wifitrace\wlan.etl`, 패킷 캡처 없음 | `af30fbee12e137e1a9b4271db235b91eb993436752281d39e058672c9a5c5192` / `a474a523b140584148d6bed5456b8b82a8a05f53fffe082b7f1ea0e8f977d7fb` |
| `W18-etw-wlan-stop.cmd` + `src/W18-etw-wlan-stop.ps1` | 412 + 4,045 | 세션 정지 → `netsh trace convert`(실패하면 tracerpt) → 끊김·로밍 관련 줄만, 가린 뒤 약 30 KB | `898a4556c9325ea3c4505e0b188c924f21bea7e2e403e2dbad08cbe0c2bf43a7` / `ff976ab9615da768a8c25dbf7f337cd655e85b74438158e751cfa01a2f6ab374` |
| `W19-ping-start.cmd` / `W19-ping-stop.cmd` + `src/W19-ping.ps1` | 734 / 575 + 1,898 | BCM4378 어댑터의 기본 게이트웨이에 1초마다 핑(최대 1,500초). WMI로 따로 띄워 작업과 분리. 상태가 바뀔 때·60초마다·끝에만 한 줄. 주소는 출력 안 함 | `cbd191720f37d4e179c4d7104c5268133d42bfb09894e51978e595fcb36d9887` / `adc5a3b646f28b04c0db6366ac4f0b2983de87279f77ad86af4707b58de57831` / `a3a8b46ea29c10e83675a99342b88ec819eff0940fb792c39490c7067a0aa806` |
| `W20-h1-scanconnonly-1/-0.cmd` | 607 / 568 | H1 스위치 켬/끔 + 장치 다시 시작 | `8721eb5903e6e5808c9d6a9597397812fe557673c4002108286740f0b55d6ea7` / `1f736b115873da038c7795e51e73f979b5a22684ce353363546f3fc82df41785` |
| `W21-h3-nodisassoc-1/-0.cmd` | 617 / 576 | H3 스위치 켬/끔 + 장치 다시 시작 | `82613501eec74a3eb27803de44277c7b4b49f99926e52123c806f3b286133f06` / `23c5ac44921264101fa02b409778b300bd47d40d95730449d5e2a493a2a94632` |
| `W22-fixes-off/-on.cmd` | 690 / 643 | BssAgeInfo·LinkInfoReal 끔/기본값 | `e2987285958d3e0e081f96cdb2ac37707f5ff375519f30bdc6434ba88effd021` / `ee36026eb6d4bb849797ce6219389607f61c7d69c20505cf59d6a0139ba7ca9a` |
| `wifi-s258/run-s258m.sh` | 5856 | 아래 8장 | `df0c8c15fcd1c35613875d19f501b640bfd17c72bef5ed8c4ead22eb7ba1a150` |

W18이 가리는 것: MAC 모양, 12자리 16진수, 공백으로 이은 16진 바이트 6개 이상, GUID, IPv4, `ssid`·`profile`·`bssid`·`peer`·`mac`·`address`·`name` 뒤의 값, 그리고 이 기기에 저장된 와이파이 프로필의 이름·SSID·SSID 16진값(프로필 XML에서 읽어 글자 그대로 바꿈, 그 값은 출력 안 함). 같은 문장(숫자 무시)은 3번까지만 출력하고 그 뒤는 `W18 REPEAT <횟수>`로 셉니다. 끊김·로밍 낱말 줄(S)을 먼저, 신호·검색 낱말 줄(W)을 나중에 냅니다.

### ETW 공급자 확인 [사실, Parallels Windows 11 Pro 26200(25H2) ARM64에서 SYSTEM으로 실행]

| 공급자 | 있음/없음 |
|---|---|
| Microsoft-Windows-WLAN-AutoConfig `{9580D7DD-…}` | 있음 |
| Microsoft-Windows-NWiFi `{0BD3506A-…}` | 있음 |
| Microsoft-Windows-WiFiNetworkManager `{E5C16D49-…}` | 있음 |
| Microsoft-Windows-WLAN-Driver `{DAA6A96B-…}` | 있음 |
| Microsoft-Windows-VWiFi `{314B2B0D-…}` | 있음 |
| WiFiCx·WDI 이름의 공급자 | `logman query providers` 목록에 없음 (`Kernel-WDI`·`Diagnosis-WDI`는 진단 기반 시설이라 와이파이와 무관) |
| WDI Driver WPP `{21BA7B61-05F8-41F1-9048-C09493DCFE38}` 등 Microsoft 문서의 OS 쪽 WPP GUID 8개 | 추가함. WPP는 TMF가 있어야 글로 풀리므로 W18에는 대개 개수만 나옵니다 |
| WlanSvc 프로세스가 등록한 공급자 전부 | 맥미니에서 `logman query providers -pid`로 찾아 넣음(가상머신은 WlanSvc가 멈춰 있어 0개) |
| `netsh trace` 시나리오 wlan·wireless_dbg | 가상머신에는 없음. 맥미니 결과는 `W17 SCEN` 줄로 나옴 |

W17·W18·W19는 가상머신에서 SYSTEM으로 실제로 돌려 문법·변환·가림을 시험했습니다(가짜 MAC·SSID·GUID·IP·프로필이 모두 가려짐). 가상머신에는 와이파이가 없어 실제 와이파이 줄은 0개였습니다. 시험 파일은 지웠습니다.

## 8. 실기 시험 순서 (콘치님이 실행)

실행마다 Windows를 다시 시작합니다. 끊김이 "부팅마다 3번 뒤 멈춤"이라서, 한 부팅에서 앞 시험이 3번을 써 버리면 다음 시험을 가를 수 없기 때문입니다. 실행 하나에 약 25~30분 걸립니다.

| 순서 | 실행 | 명령 | 그 부팅의 설정 |
|---|---|---|---|
| 1 | 기본 | `bash wifi-s258/run-s258m.sh base <지금 실행기 .out> <지금 LOG.link> 1200` | 전달 2개 → W1 설치 → W17 → 300초마다 W2 → 첫 끊김을 본 W2 뒤 W18 → W15. 스위치 둘 다 0 |
| 2 | H1 | `bash wifi-s258/run-s258m.sh h1 <지금 .out> <지금 .link> 1200` | 다시 시작 전에 W20-1, 끝에 W20-0 |
| 3 | H3 | `bash wifi-s258/run-s258m.sh h3 <지금 .out> <지금 .link> 1200` | 다시 시작 전에 W21-1, 끝에 W21-0 |
| 4 | H2 | `bash wifi-s258/run-s258m.sh h2 <지금 .out> <지금 .link> 1200` | W17 뒤 W19 핑 시작, W18 뒤 W19 정지 |
| (선택) | 수정 끔 | `bash wifi-s258/run-s258m.sh fixoff <지금 .out> <지금 .link> 1200` | BssAgeInfo=0·LinkInfoReal=0, 끝에 되돌림 |

h1·h3·h2·fixoff는 s258m이 깔려 있어야 하고, 시작할 때 W2로 `1.0.1.4`를 확인합니다. 결과는 `wifi-s258/runs/s258m-<실행>-<시각>.txt`입니다. 실행 하나가 끝나면 그 결과의 마지막 줄 `S258M … DONE link=…`의 link와 새 실행기 .out(`logs/launcher-agxdcp259-wifi-…out`)을 다음 실행에 넣습니다.

## 9. 결과 읽는 법

| 실행 | 볼 값 | 이렇게 나오면 |
|---|---|---|
| 기본 | `NwoaScanDiag`의 `t3`(로밍 검색, 연결 뒤 약 180초) | `p0`: 지금 AP가 그 검색 결과에 없음 → Windows가 "지금 AP가 사라졌다"고 볼 수 있음(가설 1의 하위 질문). `p1 k1`: 지금 AP가 가장 셈. `p1 k2` 이상이고 `o`가 더 셈: 더 나은 같은 SSID AP가 있음 |
| 기본 | `NwoaDiscDiag` | `rx10=0`이거나 `rx`가 수십 초 이상: 끊기 전 수신이 없었음 → 활동 없음(가설 2)과 맞음. `rx`가 수백 ms 이하로 계속 받고 있었으면 가설 2와 맞지 않음. `scan`은 검색 완료 뒤 몇 ms 뒤에 끊었는지 |
| 기본 | `NwoaConnDiag` 끊긴 뒤 연결 | s258l과 같다면 `rr1 rnr7 rt0`. 끊긴 뒤라 후보 비트 1(지금 AP)은 꺼져 있고, 첫 후보에 2(직전 연결 AP)가 켜져 있으면 같은 AP로 다시 붙는 것. 후보마다 `/` 뒤 RSSI로 Windows가 고른 순서와 신호 세기를 비교. `ap110`이면 AP가 GCMP·CCMP 둘 다 알림(3장 가설) |
| 기본 | `NwoaKeyDiag` | `ci4/10`이 아니면 3장 결론을 다시 봐야 함 |
| 기본 | W18 `S` 줄 | Windows가 남긴 끊김·로밍 판단 문장. `W18 LINES strong=0`이면 이 공급자들로는 판단 문장이 안 나온다는 뜻(WPP 해석 불가 포함) |
| 기본 | `NwoaHostDisassocLog`의 `3@` 개수 | s258l처럼 3개면 BSS 나이·링크 폭 수정은 효과 없음. 0이면 `fixoff`로 다시 생기는지 확인 |
| H1 | `3@` 개수, `NwoaCntScanSynth` | 0개가 되면: 연결 중 검색 결과의 다른 같은 SSID AP(또는 지금 AP가 빠진 것)가 끊기 판단에 쓰임(가설 1 맞음). 그대로 3개면 가설 1 틀림 |
| H3 | `3@` 개수, `NwoaCntDisassocIndSkipped`, 끊긴 뒤 `NwoaConnDiag` | 끊김 수는 같고 다시 붙는 연결이 `rr0`이거나 `rnr`이 7이 아니면: 다시 붙기는 우리 끊김 알림에 대한 반응(가설 3). 아무것도 안 바뀌면 가설 3 틀림. 다시 붙지 않고 끊긴 채로 있으면 끝의 W21-0(장치 다시 시작)이 되돌립니다 |
| H2 | `3@` 개수, W19 `CHG`·`BEAT`·`END` | 1초마다 핑으로 데이터를 흘렸는데도 끊기면 활동 없음 판단(가설 2) 틀림. 끊김이 없어지면 가설 2 맞음. `CHG … TimedOut`의 `t_ms`와 `3@<ms>`를 비교하면 핑 실패가 끊김 앞인지 뒤인지 보임 |

W18의 시각은 Windows 시계(`2026/11/19-04:10:12.345`)입니다. 드라이버 `@ms`와 맞출 때는 W17·W18의 `NOW … UP_MS …` 줄로 바꿉니다(몇 ms 어긋날 수 있음).

## 10. 확인 안 된 것
- 맥미니에서는 아무것도 돌리지 않았습니다. 새 진단 코드가 실기에서 끊김·속도에 영향을 주지 않는다는 것도 확인 전입니다 [빌드만].
- 펌웨어 `chanspec`·`GET_RATE`(12번)이 이 칩에서 값을 돌려주는지는 모릅니다. 못 읽으면 `NwoaLinkBwMhz`·`NwoaLinkRateKbps`가 0이고 예전 값으로 보고합니다.
- `logman query providers -pid 4`가 커널 공급자를 보여 주는지는 가상머신에서 0개라 확인하지 못했습니다(맥미니 결과는 `W17 KERNEL_PROVIDERS … of …`).
- "부팅마다 3번"이 부팅 단위인지 장치 다시 시작 단위인지 모릅니다. 그래서 실행마다 다시 시작합니다.
