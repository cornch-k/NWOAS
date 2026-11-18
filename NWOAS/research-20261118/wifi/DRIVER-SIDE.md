# 내장 Wi-Fi 드라이버 쪽 가정 (S258, nwoa4378.sys, 2026-11-18)

표기: **[사실]** = 코드·빌드·실기 기록으로 확인. **[가설]** = 확인 안 함. 드라이버는 **빌드(오류 0, 경고 0)와 맥북 단위 시험(127개 통과)만 했고 맥미니에서 한 번도 돌리지 않았습니다.** 상대 쪽 약속은 `CONTRACT-WIN.md`(S259)입니다.

## 0. 결론

- 드라이버는 S259 계약대로 `ACPI\NWOA4378`에 붙고, 계약의 자원 3개·제어 페이지·DMA 규칙을 그대로 따릅니다. 계약과 어긋나는 점은 찾지 못했습니다.
- 같은 INF에 `PCI\VEN_14E4&DEV_4425`도 넣어 두었습니다. 나중에 PCI 장치로 보이게 바꿔도 드라이버는 그대로 씁니다(설정 공간은 버스 인터페이스로 접근).

## 1. 드라이버가 Windows에서 기대하는 것

| 항목 | 드라이버의 가정 | 계약 | 맞음 |
|---|---|---|---|
| 하드웨어 ID | `ACPI\NWOA4378` (그 밖에 `PCI\VEN_14E4&DEV_4425&SUBSYS_4378106B&REV_03`, `PCI\VEN_14E4&DEV_4425`) | `ACPI\NWOA4378`, 인스턴스 `\0` | 예 |
| 메모리 자원 | 길이로 구별: 64 KiB = BAR0, 8 MiB 이상 = BAR2, 16 KiB = 제어 페이지. 순서는 보지 않음 | 같은 길이, 순서 무관 | 예 |
| 시작할 때 확인 | 제어 페이지 MAGIC `0x4657574E`, VERSION 1, STATE 1, BAR0·BAR2 0 아님, IDS `0x442514E4`. 하나라도 다르면 **BAR를 읽지 않고** 시작 실패(`NwoaStage=fail_config_space`) | 6장 1번 | 예 |
| 설정 공간 | 읽기: CFG_OFF(0x20)에 오프셋 → CFG_DATA(0x24) 읽기. 쓰기: CFG_OFF → CFG_DATA, 그 뒤 CFG_ERR(0x28)이 0이 아니면 횟수만 셈. BAR0 창(0x80)은 BAR0WIN(0x4C)으로 | 3장 | 예 |
| 설정 공간에 쓰는 것 | 0x80(창, 4 KiB 정렬), 0x04(BME 켜기/끄기, 메모리 비트 유지), DevCtl 하위 16비트(No Snoop·RO 끔), LinkCtl ASPM 두 비트(칩 리셋 앞뒤로 끄고 되돌림) | 허용 표 4줄 | 예 |
| 제어 페이지 CMD(0x10) | 쓰지 않음(코드에서 막음) | ACPI 전용 | 예 |
| ADT 값 | MAC(0x60/0x64), CAL_LEN(0x68), 안테나(0x6C, `X3`이어야 함), 보드(0x70~, `atlantisb`이어야 함), 보정 데이터(0x1000~). 제어 페이지가 없을 때만 패키지의 `nwoas-bcm4378-board.bin`(오프라인 ADT 덤프에서 만든 같은 값) | 3장 | 예 |
| 펌웨어 파일 | 패키지에 들어 있음(드라이버 스토어에서 읽음): `atlantisb-X3.trx`·`.clmb`·`.txcb`, NVRAM `P-atlantisb_M-RASP_V-m.txt`·`_V-u.txt`(칩 OTP의 `V`로 고름, 같은 제조사 판끼리는 내용이 같음을 빌드 때 `cmp`로 확인). Apple 파일이라 이 맥미니 전용 패키지에만 넣음 | 드라이버 몫 | 예 |
| DMA 주소 | 칩에 주는 주소 = WDF 공통 버퍼 논리 주소. 그 주소가 `[4 GiB, 48 GiB)` 밖이면 버리고 `MmAllocateContiguousMemorySpecifyCache(4 GiB ~ 48 GiB, 캐시 안 함)`로 다시 잡아 물리 주소를 씀(`DmaMode` 0). 결과는 `NwoaDmaContig`에 남김 | 2장(4 GiB 위 권장, `_DMA` 48 GiB) | 예 |
| 캐시 | 공통 버퍼·연속 메모리 모두 캐시 안 함. 링·버퍼는 모두 이 메모리 안에서 복사(패킷 DMA 없음) | `_CCA 0` | 예 |
| 인터럽트 | **기본값은 폴링**(10 ms 타이머, `UseInterrupt`=0). 1로 바꾸면 ISR이 `MAILBOXINT`(BAR0+0xC30)를 읽고 써서 지운 뒤 DPC에서 링 처리. 링이 올라가기 전에는 메일박스 마스크를 0으로 둠 | 4장(수준형, 폴링 먼저) | 예 |
| MSI | 쓰지 않음 | 없음 | 예 |
| D0 진입(D3 뒤) | 칩을 처음부터 다시 올림(EROM부터 펌웨어까지) | `_PS0` = 전원 순환 | 예 |
| D0 나감·제거 | `DISASSOC`, 플로우링 삭제, `DOWN` → 타이머 정지 → 메일박스 마스크 0 → BME 끔 → 작업 항목 비움 → CR4 정지 → DMA 메모리 해제 | 6장 6번 | 예 |
| 만지지 않는 것 | DART, apcie 포트 레지스터, GPIO, SMC, ECAM은 매핑하지 않음. 블루투스(기능 1) 건드리지 않음 | 금지 목록 | 예 |

## 2. 계약에 없어서 드라이버가 정한 것

| 항목 | 정한 값 | 이유 |
|---|---|---|
| 백플레인 창 | 링을 올린 뒤에는 PCIe2 코어(`0x18001000`)에 고정. 도어벨 BAR0+`0xA20`, 메일박스 상태 BAR0+`0xC30` | [사실] S257 실기에서 통과한 방식(`doorbell=window:0xa20`) |
| 제어 페이지 접근 횟수 | 창 바꿀 때 쓰기 1번 + 되읽기 1번. EROM·OTP·코어 리셋 동안 수십~수백 번 | [가설] 한 번에 수 ms면 시작이 1~2초 늘어남 |
| 검색 | 기본은 수동 검색(듣기만, `ScanPassive`=1) — S257 `PASS_SCAN`과 같은 설정. 능동 검색은 `ScanPassive`=0 | 실기에서 통과한 설정부터 |

## 2.1 첫 실기(2026-11-18 13:29) 뒤 고친 점

- [사실] 첫 SError의 `L2C_ERR_ADR`가 `...6e0800c34`였습니다. 원인은 창을 PCIe2 코어에 둔 상태에서 펌웨어를 올리기 **전에** `MAILBOXMASK`(0xC34)에 0을 쓴 것입니다. 이 쓰기는 S257 스크립트에 없는 접근이었습니다. 그 뒤로는 칩 안 레지스터 접근이 모두 SError가 됐습니다(0x040·0x120·0x124·0xC30·0x408, 564회).
- 0.2판(`s258b`)에서 고친 것:
  - 이 쓰기를 뺐습니다. 마스크는 펌웨어가 뜬 뒤 링을 올릴 때만 씁니다(`wifi_scan.py`와 같은 자리).
  - 칩 안 레지스터 접근을 S257이 실제로 만진 주소로만 허용합니다(`hw.cpp` `BpAllowed`).
  - 창은 쓴 뒤 되읽어 맞을 때만 씁니다. 한 번이라도 거절되면 그 뒤 칩 안 접근은 모두 막습니다(`NwoaBpFault`, `NwoaBpBadAddr`).
  - 종료할 때 CR4를 멈추던 접근도 뺐습니다. 버스 마스터를 끄는 것으로 DMA는 멈춥니다.

## 3. 모르는 점(상대 쪽 실기에서 알 수 있는 것)

| 항목 | 드라이버 쪽에서 보이는 곳 |
|---|---|
| 제어 페이지 설정 공간 쓰기가 실제로 먹는가 | `NwoaPciCommand`, `NwoaDevCtlAfter`, `NwoaChipId`(창이 바뀌어야 읽힘) |
| DMA가 실제로 RAM에 닿는가 | `NwoaStage=fail_first_ioctl`이면 링·DMA 길이 막힌 것(S257의 `IOCTL_NO_REPLY`와 같은 뜻) |
| 인터럽트 695가 들어오는가 | `UseInterrupt`=1로 바꾼 뒤 `NwoaCntIsr` |

## s258k (2026-11-19) — 약 250초마다 끊겼다 다시 붙는 문제

결론: s258j 기록만으로는 끊는 쪽을 하나로 정할 수 없어, 가려낼 기록을 넣고 근거가 있는 고침 세 가지를 함께 넣었다. 하드웨어 확인 전이다.

- 코드로 확인한 것: 펌웨어 이벤트 `11/0/8`(E_DISASSOC, 사유 8 "떠남")은 우리 쪽 `NB_C_DISASSOC`로 나는 모양이고, 2.4초 뒤 이벤트 끝의 `0/0/0`(E_SET_SSID)은 호스트가 낸 join의 완료다. 즉 다시 붙는 것은 Windows가 낸 WDI_TASK_CONNECT 처리(JobConnect)다.
- 가려지지 않은 것: (가) Windows가 연결된 채 CONNECT를 보내 JobConnect가 먼저 끊었는지, (나) 펌웨어가 스스로 끊고(그때 `NwoaLostReasonWdi`=0xE가 남음) 우리가 알린 뒤 Windows가 다시 CONNECT를 보냈는지.
- 넣은 기록(레지스트리, SSID·BSSID·키 값 없음): `NwoaCmdLog`(명령 id/연결 여부/M3 상태/정보@ms), `NwoaCmdHist`(명령 id별 횟수), `NwoaCmdBeforeLoss`(마지막 끊김 직전 명령 10개), `NwoaHostDisassocLog`(우리가 끊은 출처@ms: 1 CONNECT, 2 ROAM, 3 DISCONNECT, 4 RESET, 5 무선 끔, 6 연결 실패 정리, 7 종료), `NwoaLossLog`(펌웨어 끊김 이벤트 종류/사유/그때 연결 중이었나/우리 끊기 뒤 ms/RSSI@ms), `NwoaRssiLast/Min/Max`, `NwoaCntLinkStateInd`, `NwoaCntRoamStay/Move`, `NwoaCntConnWhileConn`, `NwoaCntD0Entry/Exit`, 펌웨어 기본값 `NwoaFwRoamOffWas`, `NwoaFwBcnTimeout`, `NwoaFwRoamTrigger`, `NwoaFwRoamDelta`, `NwoaFwRoamScanPeriod`.
- 고침 1: 링크 품질 알림. Microsoft 문서는 링크 품질이 5 넘게 바뀌면 LINK_STATE_CHANGE를 보내라고 하는데 s258j는 연결 순간 한 번만 보냈다. 2초마다 RSSI를 읽어 5 이상 바뀌면 보낸다(`LinkQualityUpdates`, 기본 1).
- 고침 2: WDI_TASK_ROAM 처리. s258j는 NOT_SUPPORTED였다. 지금 AP로의 로밍이면 끊지 않고 완료만 보내고(문서: "로밍하지 않고 현재 AP에 머물 수 있으며 그때는 작업만 완료"), 다른 AP면 옛 AP 해제 알림 + join + ROAM_COMPLETE(`RoamStay`, 기본 1).
- 고침 3: 펌웨어 `roam_off` 1. WDI에서는 로밍을 호스트가 정하므로 펌웨어가 혼자 BSSID를 옮기지 않게 한다(`FwRoamOff`, 기본 1).
- 이 셋이 250초 주기를 없앤다는 것은 가설이다. 다음 기록으로 (가)/(나)를 가린 뒤 원인에 맞게 다시 고친다.
