# 와이파이 송신 흐름 링 멈춤 — 코드 대조 분석 (2026-11-19 저녁)

> 구현 에이전트의 읽기 전용 분석을 지휘자가 저장한 것입니다. Linux brcmfmac 소스는 로컬에 없어, brcmfmac 쪽 내용은 **기억에 기댄 것**입니다(행 번호 없음). 빌드·설치·실기 실행은 하지 않았습니다.

## 0. 결론
- **펌웨어는 멈출 때 호스트에 아무 알림도 보내지 않습니다 [사실, s258r 실기 기록 `runs/s258r-shapes-20261119-113624.txt`, 멈춤 4~7번 포함].** 모르는 D2H 메시지 0, 일반·링 상태 메시지 0, D2H 메일박스 메시지 0, TCM D2H 메일박스 값 0, 0이 아닌 송신 상태 0, 흐름 링 재개(0x15) 응답 없음. → 인계 문서 4.4의 1번 가설("무시하는 D2H 메시지")은 성립하지 않습니다.
- 흐름 링 생성 방식이 brcmfmac과 다른 점: 링 **하나**, **tid 항상 0**, TX_POST **prio 항상 0**, 폴링 중 **MAILBOXINT를 지우지 않음**(`int=1`이 계속 남음 [사실]).

## 1. D2H 메시지·펌웨어 이벤트 대조
분기 `port-j274/wifi/bcm4378-wificx/src/driver/msgbuf.cpp:482-576`(`Dispatch`), 번호 정의 `src/core/nwbc_core.h:269-293`.

| D2H 종류 | 우리 처리 (file:line) | brcmfmac (기억) | s258r 실기 횟수 [사실] |
|---|---|---|---|
| GEN_STATUS 0x01 | 세기만 `msgbuf.cpp:561-564` | 오류 기록만 | 0 |
| RING_STATUS 0x02 | 세고 기록 `msgbuf.cpp:565-569` | 경고 기록만 | 0 |
| FLOW_RING_CREATE_CMPLT 0x04 | `msgbuf.cpp:534-539` | 링 OPEN / 실패면 지움 | 다시 만들 때마다 |
| FLOW_RING_DELETE_CMPLT 0x06 | `msgbuf.cpp:540-545` | 링 해제 | 다시 만들 때마다 |
| IOCTLPTR_REQ_ACK 0x0A / IOCTL_CMPLT 0x0C | `msgbuf.cpp:489-514` | 같음 | — |
| WL_EVENT 0x0E | `msgbuf.cpp:515-525` | fweh 큐 | — |
| TX_STATUS 0x10 | `msgbuf.cpp:530-533`, `412-432` | 해제·흐름 재개 | 0이 아닌 상태 0 |
| RX_CMPLT 0x12 | `msgbuf.cpp:526-529` | 같음 | — |
| D2H_MAILBOX_DATA 0x24 | FWHALT, DS_ENTER_REQ→ACK `msgbuf.cpp:546-555` | TCM 메일박스 + 인터럽트로 받고 읽은 뒤 0으로 지움 | 0 |
| FLOW_RING_RESUME_CMPLT 0x16 | 세기만 `msgbuf.cpp:556-560` | 안 씀 | 0 |
| 그 밖 | `CntUnknown` `msgbuf.cpp:570-572` | "Unsupported msgtype" | 0 |

TCM D2H 메일박스는 `msgbuf.cpp:1056-1075`(`NwoaMbPollTick`)에서 읽기만 함, 실기 `dtoh=0` [사실].

펌웨어 이벤트: 동작하는 것 `events.cpp:115-125`(0, 3, 5/6, 7, 9, 11/12, 16, 19, 25, 54, 69), 나머지는 세기만(`136-150`), 124는 내용 기록(`151-193`).

| 이벤트 | 언제 | 횟수 [사실] |
|---|---|---|
| 74 FIFO_CREDIT_MAP | 연결 때 | 2 |
| 127 BCMC_CREDIT_SUPPORT | 연결 때 | 2 |
| 40, 38, 1, 87, 88, 117 | 연결 때 | 각 1~3 |
| 124 | 검색마다 | 수백 |
| 검색 뒤 흐름 제어·링 관련 이벤트 | — | 없음 |

brcmfmac에서 74·127은 fwsignal이 처리하며, fwsignal은 BCDC(SDIO/USB)에서만 붙고 PCIe msgbuf에는 안 붙는다고 기억 [가설: 기억].

## 2. 흐름 링 생성 값·흐름 제어·초인종·색인

| 항목 | 우리 (file:line) [사실] | brcmfmac (기억) | 같은가 |
|---|---|---|---|
| 생성 메시지 배치 | `nwbc_core.c:548-566` | `msgbuf_tx_flowring_create_req` | 같음 |
| 링 수 | 하나(`L.flow[0]`, id 2) `msgbuf.cpp:312-314`, 칸 4개 `nwbc_core.h:237` | STA는 AC(fifo)별 하나씩, 첫 프레임 때 | **다름** |
| tid | **항상 0** `msgbuf.cpp:774-775` | 그 링의 fifo 번호 | 다를 수 있음 |
| da | 연결 BSSID `wifi.cpp:1757` | 첫 프레임 목적지 | 대체로 같음 |
| max_items / len_item | 512 / 48 `nwbc_core.h:238-239` | 512 / 48 | 같음 |
| TX_POST 플래그 | 802.3 \| **prio 0 고정** `msgbuf.cpp:856`, `nwbc_core.c:531-546` | 802.3 \| `skb->priority` | **다름** |
| 초인종 | 항목→장벽→h2d_w 색인→장벽→`H2D_MAILBOX_0`(0xA20)에 1 `msgbuf.cpp:13-17`, `146-169` | write_wptr + ring_bell | 같음 |
| 색인 위치 | 호스트 DMA 색인, idx_sz 4 `msgbuf.cpp:315-321`, `121-143` | DMA_INDEX면 호스트 버퍼 | 같음 |
| host_cap | 0x1407 `nwbc_core.c:392-403` | 같은 조합 | 같음 |
| D2H 알림 | **폴링**(2/50 ms) `diag.cpp:52,55`, `msgbuf.cpp:935-968`, MAILBOXMASK 0, **MAILBOXINT는 링 올릴 때 한 번만 지움** `msgbuf.cpp:375-379` | 인터럽트, 처리마다 지움 | **다름** |
| 다시 만들기 | 지우기→만들기 `msgbuf.cpp:1049-1055`, `wifi.cpp:1105-1150` | 없음 | — |

## 3. 원인 가설 순위

### H1 [가설, 1순위] 채널을 벗어나는 동안 있던 흐름 링 객체가 펌웨어 안에서 "가져오기 막힘" 상태로 남는다. 링 하나·tid 0 구성이 그 경로를 밟게 할 수 있다
- 근거 [사실]: 펌웨어 읽기 색인만 멈춤, 쓰기 색인 사본은 맞음, 초인종·재개 메시지 효과 없음, 지우고 다시 만들면 즉시 풀림, 제어·수신 링 정상, 지금 채널만 검색은 멈추지 않음, 펌웨어 판·PM·Linux iovar·검색 중 송신 보류와 무관, 알림 없음.
- 시험: 1.0.1.10에 `FlowTid=N`(생성 tid와 TX_POST prio를 같은 값으로), `TestConnScanSec=120`, `ScanFlowCheck=1`로 10분씩, `NwoaScanFlow` alive/stuck. tid≠0(1, 3, 6)에서 alive 3/3이면 관련 있음.
- 변경(적용 안 함): Cfg `FlowTid` 추가(`nwoa.h`, `diag.cpp`), `msgbuf.cpp:774-775`와 `:856`을 그 값으로. 원인이면 다음은 brcmfmac식 AC별 링.
- 함께: `FlowSpare=1` — 두 번째 링(id 3)을 비워 두었다가 첫 링이 멈추면 다음 프레임을 그쪽에 올려 "걸려 있던 링만 막히는지 / 모든 링이 막히는지" 가름.

### H2 [가설, 2순위] 폴링 중 MAILBOXINT를 지우지 않는다
- 근거 [사실]: `int=1` 계속, `intnz` 2초마다 증가(16→482). brcmfmac은 처리마다 지움.
- 약점: 제어·수신 링은 같은 조건에서 정상.
- 시험: `MbAck=1` — `NwoaEvtPollTimer`(`msgbuf.cpp:935`)에서 MAILBOXINT가 0이 아니면 같은 값을 써서 지움(인터럽트는 안 켬).

### H3 [가설, 낮음] 채널 이탈 때의 AP 쪽 절전 상태
- 링을 다시 만들면 바로 풀리므로 약함 [사실]. 펌웨어 `counters`의 txnull·txframe 전후 비교로 확인 가능.

### H4 [가설, 낮음] 하이퍼바이저 BYPASS 경로 타이밍
- 같은 초인종을 쓰는 제어 제출 링 정상 [사실]. 필요하면 HV에서 0xA20 쓰기 수를 `NwoaCntDbell`과 비교.

## 4. 다음 순서
1. 빌드 1.0.1.10에 `FlowTid`, `FlowSpare`, `MbAck`(모두 기본 0 = s258r과 같음).
2. 스위치 하나씩 10분: `FlowTid=3` → `FlowTid=1` → `MbAck=1` → `FlowSpare=1`.
3. `NwoaScanFlow` alive/stuck/refresh 증가량으로 판정.

## 5. 확인하지 못한 것
- brcmfmac 소스 직접 확인(prio2fifo의 prio 0 값, fwsignal이 PCIe에 안 붙는지, da 정하는 법).
- 이벤트 87, 88, 117의 이름.
- Linux에서 이 칩·이 펌웨어로 연결 중 검색 때 멈춤이 없는지.
