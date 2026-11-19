# BCM4378 송신 흐름 링 멈춤 — Asahi brcmfmac·bcmdhd 실제 소스 대조 (2026-11-19)

> 2026-11-19 밤 와이파이 세션의 조사 에이전트 보고 원문입니다(읽기 전용 조사, 실기 실행 없음). 참고 소스는 임시 폴더에 받았던 것이라 지금은 없을 수 있습니다(출처 커밋은 0장).

## 0. 결론
- **두 참고 드라이버 모두 연결 중 검색 앞뒤에 흐름 링·흐름 제어·인터페이스를 건드리지 않습니다 [사실].**
  - brcmfmac는 `escan` iovar 하나만 보냅니다(cfg80211.c:1099-1150, 1153-1175). 4378에는 mpc 조정도 없습니다(feature.c:434-444. NEED_MPC는 4329만).
  - bcmdhd는 검색 전에 `WLC_SET_PASSIVE_SCAN`만 더 보냅니다(wl_cfgscan.c:2230-2232).
  - 그래서 "검색 앞뒤에 빠진 동작을 채운다"는 방향의 근거는 소스에서 찾지 못했습니다.
- **펌웨어 공유 플래그 실측값 0x78810007을 풀면 두 가지가 나옵니다 [사실].** 이 값은 실기 기록 `NwoaSharedFlags` 218건에서 모두 같았습니다.
  1. **DAR 비트(0x80000000)가 꺼져 있습니다.** 그래서 이 펌웨어에서는 brcmfmac(pcie.c:1065-1072, 1221-1224)와 bcmdhd(dhd_pcie.c:659-666, 676-680, 13841; 레지스터 정의 include/pcie_core.h:1030-1031) 모두 0x140/0x144를 씁니다.
     - 우리 기본값은 0xA20/0xA24입니다(msgbuf.cpp:16-22, DbLegacy 기본 0 diag.cpp:145).
     - 분석 문서 2장의 "초인종 같음"은 기본 설정 기준으로는 틀렸습니다.
     - 다만 둘 다 0x140/0x144로 바꾼 dbl3 시험이 실기에서 alive 0 / stuck 5였습니다(wifi-s258/runs/s271-install-20261119-221821.txt:3105). 기각된 것으로 봅니다. DbLegacy 1·2는 작업 파일만 찾았고 판정은 찾지 못했습니다.
  2. **IDLE_FLOW_RING 비트(0x80000)가 꺼져 있습니다.** bcmdhd는 이 비트가 있을 때만 흐름 링 정지·재개를 켭니다(dhd_pcie.c:13793-13798). 재개 메시지 0x15에 답이 없던 것과 들어맞습니다 [사실 + 해석].
- **기각 목록에 없고 실기 시험 기록도 없는 차이가 여러 개입니다.** 가장 앞에 둘 것은 메일박스 인터럽트 마스크입니다.
  - brcmfmac는 MAILBOXMASK를 0xFFFF로 켜 둡니다(rev≥64 선택 pcie.c:2846-2850, 값 517-523, 쓰기 1056-1063). 처리할 때마다 MAILBOXINT를 지웁니다(pcie.c:1099-1103).
  - 우리는 폴링이 기본이라 마스크가 계속 0입니다(diag.cpp:62, msgbuf.cpp:398, 992-1003).
  - 인터럽트 모드 시험 W28-irq는 장치 시작부터 실패했습니다(CM_PROB_FAILED_START, CntIsr 0, /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/wifi-s258/runs/w28-20261119-100715.txt). 실패 원인은 보지 않았습니다.
  - 그 밖의 미시험 차이: mpc(우리 0 / brcmfmac 1), hostready 순서, 색인 쓰기 폭, rx_dataoffset, RX 링 깊이, 추가 iovar. 5장에 순서대로 적었습니다.
- **Linux에서 같은 증상(BCM4378/4387, 연결 중 검색 뒤 송신 멈춤)이 보고된 사례는 찾지 못했습니다.** 검색어는 4장에 있습니다.

참고 소스 [사실]
- Asahi brcmfmac: `AsahiLinux/linux` 브랜치 `asahi`, 커밋 6ba0637d1e(2026-11-05). 받아 둔 곳: /private/tmp/claude-501/-Users-angigyeom--claude-spares-spare-WbL8Gm/0e6ca8b2-3573-43c1-932d-853319f124ff/scratchpad/ref/brcmfmac/
- bcmdhd: AOSP `kernel/google-modules/wlan/bcmdhd/bcm4389`, 브랜치 `android-gs-tangorpro-6.1-android16`. 받아 둔 곳: /private/tmp/claude-501/-Users-angigyeom--claude-spares-spare-WbL8Gm/0e6ca8b2-3573-43c1-932d-853319f124ff/scratchpad/ref/bcmdhd/
- bcmdhd는 Pixel용 BCM4389 트리입니다. **프로토콜을 확인하는 참고일 뿐, Apple 펌웨어가 같게 동작한다는 근거는 아닙니다.**
- 행 번호 읽는 법: 접두어 없는 `pcie.c` 등은 brcmfmac, `dhd_*`·`wl_*`·`include/*`·`Kbuild`는 bcmdhd, `msgbuf.cpp`·`wifi.cpp`·`nwbc_core.*`·`diag.cpp`·`chip.cpp`·`hw.cpp`·`nwoa.h`는 우리 소스입니다. 우리 소스 위치는 /Users/angigyeom/Desktop/DEV/NWOAS/port-j274/wifi/bcm4378-wificx/src/ 입니다.

## 1. 공유 플래그 0x78810007 비트 풀이
기준: pcie.c:220-231, include/bcmpcie.h:54-120.

| 비트 | 이름 | 상태 | 뜻 |
|---|---|---|---|
| 0xFF | 판 | 7 | v7 아이템 크기(pcie.c:493-499). 우리와 같음(nwbc_core.c:410) |
| 0x10000 | DMA_INDEX | 켜짐 | 호스트 메모리 색인 |
| 0x100000 | 2B_IDX | 꺼짐 | 4바이트 색인(실측 NwoaIdxSz 4) |
| 0x80000 | IDLE_FLOW_RING | **꺼짐** | 정지·재개 0x15~0x18 대상 아님 |
| 0x800000 | D2H_MSI_MULTI_MSG | 켜짐 | 폴링에서는 무관 |
| 0x2000000 | USE_MAILBOX | 꺼짐 | 메일박스 데이터는 제어 링 메시지로 오감(pcie.c:1846-1847, dhd_pcie.c:12805-12822) |
| 0x8000000 | TIMESTAMP_DB0 | 켜짐 | HOSTCAP_DB0_TIMESTAMP를 켜야만 쓰임(dhd_msgbuf.c:3967-3974) |
| 0x10000000 | HOSTRDY_DB1 | 켜짐 | hostready가 필요함 |
| 0x20000000 | NO_OOB_DW | 켜짐 | — |
| 0x40000000 | INBAND_DS | 켜짐 | bcmdhd만 인밴드 깨우기를 켬(Kbuild:143, dhd_msgbuf.c:3854-3859). brcmfmac·우리는 끔 |
| 0x80000000 | DAR | **꺼짐** | brcmfmac·bcmdhd는 0x140/0x144, 우리 기본은 0xA20/0xA24 |

실측값 [사실]: MaxSub 0x2b, MaxCompl 6, PCIe2 rev 0x44, host_cap 0x1407, ScanVer 0, IrqMode 대부분 0.

## 2. brcmfmac ↔ 우리 드라이버 항목별 대조

### (a) 공유 구조·host_cap·hostready
| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| 판 범위 5~7 | pcie.c:1798-1803 | nwbc_core.c:344-345 | 같음 |
| 색인 크기 결정 | pcie.c:1806-1811 | nwbc_core.c:366-367 | 같음 |
| max_rxbufpost가 0이면 255 | pcie.c:1813-1816 | nwbc_core.c:347-349 | 같음 |
| mb_via_ctl 조건 | pcie.c:1846-1847 | nwbc_core.c:368 | 같음 |
| host_cap = 판\|HOSTRDY\|(DAR면 H2D_DAR)\|DS_NO_OOB_DW | pcie.c:1850-1860 | nwbc_core.c:392-403, 실측 0x1407 | 같음 |
| host_cap2 = 0 | pcie.c:1864-1865 | msgbuf.cpp:374 | 같음 |
| host_cap을 쓰는 때 | 펌웨어 기동 직후, 링보다 먼저(pcie.c:2135, 2551→2557) | 링 기술자 뒤(msgbuf.cpp:372-375) | **다름**(순서) |
| hostready 레지스터 | DAR일 때만 0xA24, 아니면 0x144(pcie.c:1065-1072) | 기본 0xA24(msgbuf.cpp:18-20) | **다름**(기본값). dbl3로 기각 |
| hostready를 보내는 때 | 버퍼를 다 올린 뒤(msgbuf.c:1705-1715 → core.c:1392, 1411 → 1252 → pcie.c:1617-1626) | 버퍼 올리기 전(msgbuf.cpp:405-416) | **다름**(순서) |

### (b) 링 초기화·ring info·색인
| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| ring_info 배치: ringmem·색인 포인터 4개·호스트 주소 4쌍(0x14)·max_flowrings/max_sub(0x34)·max_compl(0x38) | pcie.c:462-475 | nwbc_core.h:187, nwbc_core.c:354-360 | 같음 |
| 색인 배열 | 한 덩어리 h2d_w[max_sub], h2d_r[max_sub], d2h_w[max_compl], d2h_r(pcie.c:1412-1470) | 배열 4개를 256B 정렬로 따로 두고 주소를 각각 씀(nwbc_core.c:418-423, msgbuf.cpp:340-346) | 다름. 동등할 것(가설) |
| 4바이트 색인 쓰기 | 아래 16비트만(pcie.c:610-617) | 32비트 전체(msgbuf.cpp:155-156) | **다름**(영향 미확인) |
| 링 기술자 max_item/len_item | 16비트 둘(pcie.c:1311-1314) | 32비트 하나(msgbuf.cpp:351-352) | 같은 바이트 |
| 깊이: 제어제출/RX게시/제어완료/TX완료/RX완료 | 64/2048/64/1024/2048(msgbuf.h:10-14, Asahi 커밋 ae8644ec4d) | 64/1024/64/1024/1024(nwbc_core.c:409) | **다름**(RX 둘) |
| 흐름 링 id = flowid+2 | pcie.c:1517, msgbuf.c:665-666 | nwbc_core.c:437 | 같음 |
| 흐름 링 색인 칸 | 공통 H2D 2칸 다음(pcie.c:1525-1528) | slot 2+i | 같음 |
| scratch 8 / ringupd 1024 | pcie.c:318-319, 1560-1599 | nwbc_core.h:199-200, msgbuf.cpp:363-371 | 같음 |
| 다운로드 전: MASK 0, MAILBOXINT 0xFFFFFFFF, PCI cfg INTMASK 0, TLCNTRL_5 | pcie.c:845-854 | 안 함(이유 chip.cpp:175-177) | **다름** |
| 리셋 뒤 MAILBOXINT 지우기 | pcie.c:2248-2250 | chip.cpp:187-189 | 같음 |
| BAR2 설정(0x4E0) 다시 쓰기 | pcie.c:813-821 | chip.cpp:224-228 | 같음 |
| PCIe 코어 레지스터 접근 경로 | BAR0+0x2000 전용 창(pcie.c:560-571, 커밋 2c723a38bf) | BAR0 창을 PCIe2에 고정(hw.cpp:340-348, msgbuf.cpp:396) | 다름. 같은 레지스터일 것(가설) |

### (c) 흐름 링 생성·삭제
| 필드·시점 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| ifidx | 인터페이스 번호, 기본 0(msgbuf.c:662) | 0(msgbuf.cpp:790) | 같음 |
| request_id | 0(msgbuf.c:663) | 0(nwbc_core.c:554) | 같음 |
| tid | prio2fifo[skb->priority]: prio 0이면 0(flowring.c:28-37, 115, 168-175) | NwoaFlowTidFifo(FlowTid), 기본 0(nwoa.h:778-782, msgbuf.cpp:791) | 같음(prio 0일 때) |
| da | 첫 프레임 목적지(msgbuf.c:716-726). STA 조회는 MAC을 보지 않음(flowring.c:81-89) | BSSID(wifi.cpp:1812) | 대체로 같음(가설) |
| sa | 첫 프레임 출발지(msgbuf.c:725) | 우리 MAC | 같음 |
| if_flags·tc·priority·int_vector | 쓰지 않음(msgbuf.c:660-673). 제어 링 칸을 돌려 쓰므로 이전 바이트가 남을 수 있음 | 0으로 채움(nwbc_core.c:553-562) | 다름(영향 미확인) |
| max_items / len_item | 512 / 48(msgbuf.c:672-673) | 512 / 48(nwbc_core.h:238-239) | 같음 |
| 생성 전 r/w 색인 0 | msgbuf.c:646, commonring.c:41-46 | msgbuf.cpp:785-787 | 같음 |
| 링 수 | STA는 fifo마다 하나, 첫 프레임 때(flowring.c:103-165, msgbuf.c:849-858) | 하나(+FlowSpare)(msgbuf.cpp:830-870) | 다름. prio 0만 쓰면 사실상 하나 |
| 만드는 때 | 첫 TX(msgbuf.c:849-856) | 연결 직후(wifi.cpp:1812) | 다름(시점) |
| 지우는 때 | DEAUTH·DEAUTH_IND·DISASSOC_IND·LINK 내려감(cfg80211.c:6559-6564), 주소 방식 변경(flowring.c:401~) | 끊김·종료(wifi.cpp:1596, 1873, driver.cpp:176) + 복구 | 같음 + 복구용 추가 |
| 삭제 요청 내용 | id, reason 0(msgbuf.c:1502-1508) | nwbc_core.c:569-578 | 같음 |

### (d) TX_POST
기준: msgbuf.c:781-798 / nwbc_core.c:531-546, msgbuf.cpp:918-921.

| 필드 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| request_id 범위 | pktid+1, 0~2047(msgbuf.c:77, 784) | slot+1, 1~128(nwoa.h:17) | 범위만 다름 |
| ifidx | 0 | 0 | 같음 |
| flags | 802.3\|(prio&7)<<5 | 802.3\|(FlowTid&7)<<5 | 같음(prio 0) |
| seg_cnt | 1 | 1 | 같음 |
| 헤더 처리 | 14바이트를 아이템에, 데이터는 +14 / 길이 -14(msgbuf.c:765-767, 791, 357-366) | 같음 | 같음 |
| metadata | 0 | 0 | 같음 |
| rsvd | 쓰지 않음 | 0 | 다름(영향 미확인) |
| 초인종 | 몇 개씩 모아서(msgbuf.c:756, 800-806) | 프레임마다(msgbuf.cpp:165-188) | 다름 |

### (e) 완료 처리·버퍼 다시 올리기
| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| 처리 순서 | RX → TX → 제어(msgbuf.c:1432-1437) | 제어 → RX → TX(msgbuf.cpp:638-640) | 다름 |
| 완료 메시지 위치 | rx_dataoffset만큼 밀어 읽음(msgbuf.c:1404-1405) | 밀지 않음(msgbuf.cpp:619) | **다름**. 이 펌웨어는 0일 것(가설, 실측 기록 없음) |
| 읽기 색인 갱신 | 48개마다+끝(msgbuf.c:98, 1408-1415) | 16개마다+끝(msgbuf.cpp:621-624) | 같은 방식 |
| TX_STATUS | pktid 해제·outstanding 감소(msgbuf.c:915-939) | msgbuf.cpp:436-461 | 같음 |
| RX 버퍼 | max_rxbufpost까지, 32개 줄면 채움(msgbuf.c:84, 1020-1045) | min(256, max), 하나씩 즉시(msgbuf.cpp:110, 230-242) | 비슷 |
| 이벤트·ioctl 버퍼 | 각 8개, 쓰면 바로 다시(msgbuf.c:85-86, 909-911, 1155-1157) | 각 8개(nwbc_core.h:243-244, msgbuf.cpp:535, 546) | 같음 |

### (f) 인터럽트·D2H 메일박스
| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| 동작 방식 | 스레드 인터럽트(pcie.c:1120-1142) | 폴링 2/50ms(diag.cpp:52, 62) | **다름** |
| MAILBOXMASK | 0xFFFF(pcie.c:203-218, 517-523, 1056-1063) | 0(msgbuf.cpp:398) | **다름, 시험 기록 없음** |
| MAILBOXINT 지우기 | 매번(pcie.c:1099-1103) | 처음 한 번(msgbuf.cpp:399-404). MbAck=1이면 매번(1193-1206) | MbAck로 기각 |
| TCM 메일박스 읽기 | rev≥64는 int_fn0=0이라 **실행되지 않음**(pcie.c:522, 1104-1105) | 읽기만(msgbuf.cpp:1130-1146) | 사실상 같음 |
| D2H_MAILBOX_DATA(0x24) | DS_ENTER_REQ→DS_ACK, FWHALT 처리(msgbuf.c:1327-1334, pcie.c:925-944) | msgbuf.cpp:576-585 | 같음 |
| H2D 메일박스 ifidx | 0xFF(msgbuf.c:1539-1544) | 0xFF(nwbc_core.c:596) | 같음 |

분석 문서는 "TCM 메일박스와 인터럽트로 받는다"고 적었지만, 이 칩(rev≥64)의 brcmfmac는 0x24 메시지로만 받습니다.

### (g) 초기화·연결 때 iovar·ioctl
| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| cur_etheraddr | common.c:277-299 | wifi.cpp:739 | 같음 |
| clm·txcap·cal 내려받기 | common.c:342-357 | wifi.cpp:754-763 | 같음 |
| mpc | 1(common.c:406) | 기본 0(wifi.cpp:769, diag.cpp:59) | **다름, 시험 기록 못 찾음** |
| join_pref | common.c:412 | wifi.cpp:811-812 | 같음 |
| 검색 시간 40/40(/120) | common.c:414-430, cfg80211.c:6928-6952 | wifi.cpp:836-838 | 같음 |
| txbf 1 | common.c:433 | InitIovarSet일 때(wifi.cpp:770) | 후보로 시험 |
| C_UP 순서 | config_dongle 맨 앞(cfg80211.c:8033) | PM 설정 뒤(wifi.cpp:841) | 순서 다름 |
| PM | pwr_save 기본 true → PM_FAST(cfg80211.c:6849, 8037-8038), pm2_sleep_ret(3202-3211) | 기본 0(diag.cpp:60, wifi.cpp:839) | 다름, 기각 |
| bcn_timeout·roam_off·ROAM_TRIGGER/DELTA | roamoff 기본 0(common.c:63-64, 510), cfg80211.c:6877-6923 | roam_off 1(diag.cpp:74, wifi.cpp:834) | 다름, 기각 |
| SET_INFRA 1 | cfg80211.c:1079 | wifi.cpp:845 | 같음 |
| SET_AP 0, toe 0, 초기 sup_wpa 0 | 안 보냄 | 보냄(wifi.cpp:846, 857, 867) | 다름 |
| arp_ol·arpoe·ndoe 켜기 | core.c:113-128, cfg80211.c:8052 | 기본 끔(wifi.cpp:847-856) | 후보로 시험 |
| FAKEFRAG 1 | cfg80211.c:8054 | 후보(wifi.cpp:858) | 시험됨 |
| 이벤트 마스크 | 펌웨어 값 읽고 E_IF 추가(fweh.c:440-493) | 직접 고른 목록(+DiagEvents 2)(wifi.cpp:785-809, diag.cpp:91) | 다름. 멈춤은 DiagEvents 전(s258k/l/m, nwbc_core.h:555)에도 있었음 [사실] |
| tlv/proptx | fwsignal은 BCDC에만 붙음(bcdc.c:428-434, fwsignal.c:2399-2414). PCIe는 msgbuf(pcie.c:2889, proto.c:32-36) | 안 보냄 | 같음 |
| 연결 iovar | wpaie·mgmt_ie·wpa_version·auth·wsec·mfp·sup_wpa·join, 실패 시 SET_SSID(cfg80211.c:2217-2356) | wpaie·wpa_auth·wsec·auth·mfp·sup_wpa·INFRA·join, 실패 시 SET_SSID(wifi.cpp:1729-1755) | 거의 같음 |
| 연결 직후 | assoc_info·IE·wme_ac_sta 읽기(cfg80211.c:6381, 6463, 6301) | 읽기만(wifi.cpp:1772-1818) | 같음 |

### (h) 검색
| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| 구조 판 | scan_ver 실패면 v1(feature.c:393-401, scan_param.c:420-446) | v1(실측 0, wifi.cpp:869-873) | 같음 |
| 구조 배치 | version, action 1, sync 0x1234(cfg80211.c:1135-1139, fwil_types.h:405-427, 566-576) | 같은 배치, sync 0x5257(nwbc_core.c:786-813) | 같음(sync 값만 다름) |
| 기본값 | 브로드캐스트·bss_type 2·시간 -1(scan_param.c:14-26) | wifi.cpp:907, nwbc_core.c:798-805 | 같음 |
| 채널·SSID | cfg80211 채널 목록, SSID는 채널 뒤, SSID 없으면 수동(scan_param.c:133-169) | 기본 전체 목록·수동(diag.cpp:58, wifi.cpp:911) | 다름. shape로 시험됨 |
| 검색 전 | mpc는 NEED_MPC만(4378 아님), 프로브 요청 IE(cfg80211.c:814-818, 1165, 1220) | 진단 기록(wifi.cpp:912-965) | 링 동작 없음, 같음 |
| 검색 뒤 | 중단은 시간 초과·정리 때만(cfg80211.c:869-882, 3458-3478). 정상 완료는 fw_abort false(3636) | 흐름 링 감시·복구(wifi.cpp:1034-1240) | brcmfmac는 아무것도 안 함 |
| 흐름 제어(netif 멈춤) | 큐가 1024 넘을 때만(flowring.c:21-22, 221, 259-279), 끊김 때(core.c:785) | — | 검색과 무관 |

### (i) Apple 칩 전용 분기
`grep -rniE "apple|4378|4387"`로 받아 둔 brcmfmac .c 전체를 확인했습니다(firmware.c 제외). 걸린 곳: chip.c 727/729/1313, of.c 82/89/92, pcie.c 71-73/114-116/1966/2029/2423/2428/2635-2646/3132-3133. **모두 펌웨어 이름·OTP·rambase·시드·보드 이름이고, 송신 경로 분기는 없습니다 [사실, 이 범위 안에서].**

| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| OTP 읽기(GCI 0x1120, 0x170워드) | pcie.c:2422-2427 | nwbc_core.h:95-96, chip.cpp:216-219 | 같음 |
| rambase 0x352000 | chip.c:727-728 | nwbc_core.h:94 | 같음 |
| 무작위 시드(WCC_SEED, 0xFEEDC0DE·0x100) | pcie.c:3132, 1912-1913, 1956-1971, 2026-2037 | nwbc_core.h:99-100, fw.cpp:241-257 | 같음 |
| board_type·antenna-sku·cal-blob | DT에서 읽음(of.c:82-97) | 보드 파일(nwbc_core.h:540-551) | 다른 경로 |
| WCC 벤더 ops | SAE 비밀번호·이벤트 수 213만(wcc/core.c:14-38) | — | 무관 |

## 3. bcmdhd 확인 결과

### (a) 연결 중 검색 앞뒤에 하는 일
- 흐름 링·흐름 제어는 건드리지 않습니다 [사실].
- 하는 일은 넷입니다: `WLC_SET_PASSIVE_SCAN`(wl_cfgscan.c:2230-2232), 수동 시간 조정(2238-2277), 깨움 잠금, 호스트 런타임 PM 끄기·켜기(2647, 2728, 2738, 3053).
- 흐름 링을 지우는 것은 본 범위에서 피어·인터페이스 삭제 이벤트 때입니다(dhd_common.c:5828-5836, 5900-5912).

### (b) 콘솔 명령과 덤프 iovar
- **PCIe 콘솔 입력 방법 [사실]**(dhd_pcie.c:6050-6108, include/hnd_cons.h:60-96):
  1. `cbuf_idx`에 0을 씁니다(wlc_ver_major<14면 오프셋 -4).
  2. `cbuf`에 명령 문자열을 씁니다.
  3. `vcons_in`에 길이를 씁니다.
  4. 메일박스 데이터 `H2D_HOST_CONS_INT`(0x80000000, include/bcmpcie.h:493)를 보냅니다. 이 펌웨어는 USE_MAILBOX가 꺼져 있으니 제어 링 메시지 0x23으로 갑니다(dhd_pcie.c:12805-12822).
- hnd_cons_t 오프셋: vcons_in 0, vcons_out 4, log 8(buf 8, size 12, idx 16, out_idx 20, dump_idx 24), cbuf_idx 28, cbuf 32(CBUF_LEN 128). 우리 콘솔 읽기 오프셋(nwbc_core.h:201-203)과 맞습니다.
- `"cons"` iovar(dhd_common.c:523, 2623-2626)는 호스트 쪽 iovar입니다. 위 함수를 부를 뿐 펌웨어로 가지 않습니다.
- 펌웨어로 가는 덤프 iovar(소스에서 보인 것): `"dump"`+`"pm"`(wl_cfg80211.c:14325), `"counters"`(14229), `"dump_obss"`(11286).
- 호스트 쪽 iovar(펌웨어로 안 감): `dump`(dhd_common.c:522), `dump_flowrings`(680), `dump_dongle`·`dump_ringupdblk`·`dump_txcpl`·`dump_rxlat`(dhd_pcie.c:497-586). 예를 들어 dump_dongle은 호스트가 코어 레지스터를 직접 읽습니다(dhd_pcie.c:8238-8277).
- 펌웨어 콘솔 명령 목록과 dump 하위 명령 목록은 펌웨어 안에 있어 확인할 수 없습니다.

### (c) 초기화 bus: iovar와 host_cap
- `bus:fl_prio_map`=2(LLR)를 초기화 때 보냅니다. DHD_LOSSLESS_ROAMING(Kbuild:378) 조건이고, 경로는 dhd_linux.c:11359-11365 → dhd_flowring.c:1389-1413 → 1443-1448입니다. 기본 지도는 AC 방식 {0,1,1,0,2,2,3,3}입니다(dhd_flowring.c:66, 468-469). **brcmfmac와 우리는 보내지 않습니다.**
- `bus:d3_hostwake_delay`, `bus:fr_phase_nibble`은 dhd_linux.c:11664-11678에서 보냅니다(바로 위 4개 전처리 조건 안에는 걸려 있지 않음).
- 조건부: `bus:hostwake_oob`는 BCMPCIE_OOB_HOST_WAKE(11088, 11986), `bus:intr_width`는 DYNAMIC_SWOOB_DURATION(2392). `bus:txglom*`은 BCMSDIO 전용(12415-12430, 13011-13025).
- bcmdhd host_cap(dhd_msgbuf.c:3828-3999): 판, VALID_PHASE, HOSTRDY, DS_INBAND_DW+DS_NO_OOB_DW 또는 DS_NO_OOB_DW, **EXTENDED_TRAP_DATA(항상, 3878)**, TXSTATUS_METADATA, FAST_DELETE_RING, IDMA, IFRM, DAR, **UR_FW_NO_TRAP(항상, 3937)**, SNAPSHOT, HSCB, EDL, EXT_TRAP_DBGBUF, DB0_TIMESTAMP, PKT_TXSTATUS. 트랩 버퍼 주소도 씁니다(3989-3990).
- 정의 전체는 include/bcmpcie.h:440-466, HOSTCAP2는 468-469입니다.
- hostready는 DB1 0x144에만 씁니다(dhd_pcie.c:676-680, 9220-9246). 공통 링 초기화 뒤 보냅니다(dhd_msgbuf.c:4379-4381).
- 초인종: DAR가 아니면 0x140에 0x12345678(dhd_pcie.c:12495-12532).

### (d) 흐름 링·인터페이스 상태 정의
- 호스트 쪽 FLOW_RING_STATUS: OPEN 0 ~ STA_FREEING 7(dhd_flowring.h:49-59).
- 정지·재개 메시지 0x15~0x18(include/bcmmsgbuf.h:245-248), 구조체는 1491-1516. IDLE_FLOW_RING 비트가 있을 때만 씁니다.
- 메시지 종류 전체: 0x01~0x33, 내부용 0x40~0x50, 0x80~0x83(include/bcmmsgbuf.h:221-304). D2H 처리표는 dhd_msgbuf.c:990-1037, 상태 코드 1~24는 include/bcmmsgbuf.h:741-764.
- proptxstatus(suppress·credit): Kbuild의 PROP_TXSTATUS는 두 곳 모두 SDIO 조건 안입니다(Kbuild:776-780, 846-849). 이벤트 74 FIFO_CREDIT_MAP, 117 ALLOW_CREDIT_BORROW, 127 BCMC_CREDIT_SUPPORT(include/bcmevent.h:186, 233, 241)는 PROP_TXSTATUS일 때만 처리합니다(dhd_common.c:5644-5662). **PCIe 빌드에서는 두 호스트 드라이버 모두 이 이벤트를 무시합니다 [사실].**
- 이벤트 이름(bcm4389 기준): 38 ASSOC_START, 40 RADIO, 87 ASSOC_REQ_IE, 88 ASSOC_RESP_IE, 124 CCA_CHAN_QUAL(include/bcmevent.h:151-238).
- TX_POST에서 bcmdhd는 단계 비트·면제 비트·ext_flags를 더 씁니다(dhd_msgbuf.c:8654-8705). 단계 비트는 VALID_PHASE일 때만 쓰입니다(3838-3842).

## 4. Linux 보고 사례 검색
**같은 증상은 찾지 못했습니다.**

GitHub 전체에서 쓴 검색어: `brcmfmac 4378 scan tx stall`, `brcmfmac flowring stall scan`, `brcmfmac tx stops after scan`, `BCM4378 wifi stops after scan`, `BCM4387 tx stall`, `brcmfmac flowring stuck`, `brcmfmac 4387 throughput drops scan`, `asahi wifi drops background scan`, `brcmfmac flowring`, `brcmfmac txstatus scan`, `brcmfmac scan connected traffic stops`, `bcm4378 wifi`, `bcm4387 wifi drops`, `msgbuf flowring stuck`, `brcmfmac tx timeout scan`, `BCM4378 tx`.

AsahiLinux/linux·docs·asahi-installer·m1n1 저장소 안에서 쓴 검색어: `brcmfmac wifi`, `wifi stops`, `wifi disconnect`, `wifi stall`, `wifi scan`, `wlan0 stops`, `brcmfmac`, `wifi slow after scan`, `wifi traffic stops`. 처음에 한도 초과로 실패한 것은 다시 돌렸습니다. m1n1 저장소의 `wlan0 stops`·`brcmfmac`는 다시 돌리지 않았습니다.

웹에서 쓴 검색어: `brcmfmac BCM4378 tx stall after scan flowring`, `Asahi Linux wifi stops transmitting after background scan BCM4387`, `brcmfmac "flowring" firmware stops reading after scan msgbuf`, `t2linux BCM4364 OR BCM4377 brcmfmac wifi traffic stops after scan connected`.

증상이 비슷한 이슈(검색과의 관련은 확인 안 됨):
- [AsahiLinux/linux#160](https://github.com/AsahiLinux/linux/issues/160): BCM4387. 집에서만 몇 초 뒤 멈추고 다시 연결해야 함. 5500MHz, 댓글 0.
- [#133](https://github.com/AsahiLinux/linux/issues/133): BCM4387. 몇 시간 뒤 가끔 멈춤.
- [#486](https://github.com/AsahiLinux/linux/issues/486): 2.4GHz에서 블루투스와 함께 쓸 때 끊김.
- [#491](https://github.com/AsahiLinux/linux/issues/491): OWE 연결 실패 뒤 ioctl 시간 초과와 commonring 자리 없음. 제어 링 문제라 다른 증상.
- [#453](https://github.com/AsahiLinux/linux/issues/453), [#603](https://github.com/AsahiLinux/linux/issues/603): BCM4388 펌웨어 응답 시간 초과.
- [#439](https://github.com/AsahiLinux/linux/issues/439): 절전 뒤 연결 안 됨.
- 상류 커밋 0fa24196(흐름 링 삭제와 txflow 경쟁 수정)은 호스트 쪽 경쟁이라 다른 문제입니다.

## 5. 우리 드라이버가 실제로 다른 점 (관련 가능성 순, 모두 가설)
1. **MAILBOXMASK가 늘 0이고 폴링만 함.** brcmfmac는 0xFFFF입니다(pcie.c:517-523, 1058-1060), 우리는 0입니다(msgbuf.cpp:398). **시험 기록 없음**: W28-irq는 시작 실패였고 원인은 보지 않았습니다. 폴링은 두고 마스크만 켜는 시험이 가장 싸지만 두 가지 위험이 있습니다. ISR 없이 레벨 인터럽트가 폭주할 수 있고(msgbuf.cpp:953-960 대비 코드), 마스크 쓰기에 SError 이력이 있습니다(chip.cpp:175-177). 기각된 MbAck는 "지우기"만 시험한 것입니다.
2. **mpc 0 대 1**(diag.cpp:59, wifi.cpp:769 / common.c:406). nwoas_scripts에서 Mpc=1 작업을 찾지 못했고, InitIovarSet은 0만 보였으며, 후보 표(wifi.cpp:129-132)에도 mpc가 없습니다. 연결 중 영향은 소스로 확인하지 못했습니다.
3. **hostready·버퍼·host_cap 순서**(msgbuf.cpp:372-375, 405-416 / pcie.c:1862, msgbuf.c:1705-1715 → pcie.c:1625). 시험 기록 없음.
4. **4바이트 색인을 32비트로 씀**(msgbuf.cpp:155-156 / pcie.c:610-617). 시험 기록 없음. 펌웨어가 위쪽 16비트를 보는지는 모릅니다.
5. **완료 메시지에 rx_dataoffset을 적용하지 않음**(msgbuf.cpp:619 / msgbuf.c:1404-1405). 값이 0이면 차이가 없습니다. 값을 기록하면 확정할 수 있습니다.
6. **RX 링 깊이 1024, RX 버퍼 최대 256**(nwbc_core.c:409, msgbuf.cpp:110 / msgbuf.h:11-13). 수신은 정상이라 관련이 약합니다.
7. **초기화 때 SET_AP 0·toe 0·sup_wpa 0을 더 보냄**(wifi.cpp:846, 857, 867). 약함.
8. **다운로드 전 레지스터 초기화 생략**(pcie.c:845-854). 펌웨어 기동 쪽 문제라 약함.
9. **생성 요청·TX_POST의 예약 필드를 0으로 채움, 프레임마다 초인종, pktid 1~128.** 약함.
10. **흐름 링 da를 BSSID로 고정하고 연결 직후 만듦**(wifi.cpp:1812 / msgbuf.c:716-726). 대체로 같은 값입니다.
11. 이미 시험해 기각된 것: 초인종·hostready 레지스터(dbl3: alive 0/stuck 5), PM(brcmfmac 기본 PM 2), roam_off, 검색 형태.

bcmdhd에만 있는 것(fl_prio_map, EXTENDED_TRAP_DATA·UR_FW_NO_TRAP, 인밴드 깨우기, fr_phase_nibble, SET_PASSIVE_SCAN)은 brcmfmac가 이 칩에서 쓰이므로 필수는 아닐 것으로 봅니다(가설). 1~5로 풀리지 않으면 `bus:fl_prio_map`이 다음 후보입니다.

## 6. 확인하지 못한 것
- 펌웨어 콘솔 명령 목록과 dump 하위 명령.
- rx_dataoffset 실측값.
- 펌웨어가 MAILBOXMASK나 색인 위쪽 16비트를 쓰는지.
- W28-irq가 시작에 실패한 원인.
- DbLegacy 1·2 단독 시험의 판정.
- Linux에서 이 칩·이 펌웨어로 연결 중 검색을 해도 멈추지 않는지. 보고가 없다는 것이 확인은 아닙니다.