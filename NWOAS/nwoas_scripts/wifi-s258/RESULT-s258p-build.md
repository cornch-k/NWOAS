# 와이파이 s258p(1.0.1.7) 빌드 결과와 실기 시험 순서 (2026-11-19 08:16)

## 결론
**Linux brcmfmac과 실제로 다른 것은 전원 절약(PM), 연결 중 검색의 채널 목록, 그리고 초기 설정 값 몇 개입니다 [사실, 소스 대조].** 흐름 링 생성 요청은 우리가 보내는 흔한 송신(우선순위 0)에 대해서는 항목마다 같은 값이라, 바꿔 볼 것이 없어 `FlowCreateMode` 스위치는 만들지 않았습니다(1장).
**s258p는 한 번의 부팅에서 후보 8가지를 차례로 시험합니다 [빌드만].** `CandidateCycle=1`이면 연결 중 120초마다 드라이버가 검색을 하고, 연결 중 검색이 하나 끝나 판정이 날 때마다 다음 후보로 바꿉니다. 판정마다 "펌웨어가 스스로 다시 읽었는가(alive)"를 후보별로 셉니다. s258o의 고침(초인종 한 번 더 → 흐름 링 다시 만들기)은 모든 후보에서 그대로 안전망으로 남습니다(2장).
**새 후보 하나는 brcmfmac이 아니라 Broadcom 공개 드라이버(bcmdhd)에서 왔습니다 [가설].** 멈춘 흐름 링에 "흐름 링 재개" 메시지(FLOW_RING_RESUME, 0x15)를 먼저 보내 봅니다. s258o 기록에서 펌웨어 쪽 쓰기 색인 사본이 호스트 값과 같았으므로(318 = 318) "펌웨어가 쓰기 색인은 받았으나 이 링을 멈춘 채 둔다"는 해석과 맞는 시험입니다. 이 펌웨어가 이 메시지를 아는지는 모릅니다.
빌드·서명·INF 검사·단위 시험 234개는 통과했고, 맥미니에서는 아직 돌리지 않았습니다 [빌드만].

## 1. Linux brcmfmac과 다른 점

결론: 흐름 링 생성과 검색 시간 값은 같고, 다른 것은 아래 "다름" 표의 6가지입니다. 이 중 PM과 채널 목록, 그리고 초기 설정 묶음을 후보로 넣었습니다.

출처: Asahi Linux 트리 `drivers/net/wireless/broadcom/brcm80211/brcmfmac/`(msgbuf.c, flowring.c, core.c, cfg80211.c, scan_param.c, common.c, feature.c, fwil.h, `include/brcmu_d11.h`)와 bcmdhd 공개 헤더 `bcmmsgbuf.h`, `dhd_msgbuf.c` 대 `port-j274/wifi/bcm4378-wificx/src`.

### 1-1. 흐름 링 생성 요청 — 같음

| 칸 | brcmfmac (`msgbuf.c brcmf_msgbuf_flowring_create_worker`) | 우리 (`msgbuf.cpp NwoaFlowringCreate`, `nwbc_core.c nb_enc_flowring_create`) | 판정 |
|---|---|---|---|
| ifidx | 0 (`work->ifidx`) | 0 | 같음 |
| tid | `brcmf_flowring_tid` = 우선순위의 FIFO 번호(`flowring.c brcmf_flowring_prio2fifo`, 우선순위 0 → 0) | 0 | 우선순위 0이면 같음 |
| 우선순위 | `core.c brcmf_netdev_start_xmit` → `cfg80211_classify8021d`(IP는 DSCP, EAPOL·기타는 0) | 송신 항목 우선순위 0 고정 | ping(DSCP 0)은 같음 |
| da | 그 FIFO로 처음 보낸 프레임의 목적지(`brcmf_msgbuf_flowring_create`의 `eh->h_dest`) | 연결한 AP 주소 | 다를 수 있음 [가설: STA 모드는 `brcmf_flowring_lookup`이 FIFO로만 찾으므로(ADDR_INDIRECT) 펌웨어도 da로 상대를 고르지 않을 것] |
| sa | 첫 프레임의 출발 주소 = 우리 MAC | 우리 MAC | 같음 |
| flow_ring_id | flowid + 2 = 2 | 2 | 같음 |
| max_items / len_item(개 / 바이트) | 512 / 48 (`msgbuf.h`) | 512 / 48 (`nwbc_core.h`) | 같음 |
| if_flags, tc, priority, int_vector | 0 | 0 | 같음 |
| 링 개수 | FIFO마다 하나(최대 4) | 하나 | 우선순위 0 송신만 있으면 같음 |

### 1-2. 연결 중 검색 요청

| 항목 | brcmfmac | 우리 | 판정 |
|---|---|---|---|
| nprobes / active / passive / home 시간 | 모두 -1(펌웨어 기본) (`scan_param.c brcmf_scan_param_set_defaults`) | 모두 -1 | 같음 |
| 기본 검색 시간(ms) | 40 / 40 / 120 (`cfg80211.c brcmf_dongle_scantime`) | 40 / 40 / 120 | 같음 |
| 채널 목록 | cfg80211 요청의 채널 전부를 20 MHz chanspec으로(`brcmf_scan_param_get_prepped_struct_v1`) | 0개(펌웨어 전체 목록) | **다름** |
| SSID 위치 | 채널 목록 뒤, `channel_num` 위 16비트에 SSID 수 | `ssid_le` 칸, SSID 수 0 | 다름(둘 다 펌웨어가 받는 형식) |
| 능동/수동 | SSID가 있으면 능동, 없으면 수동 | 지시 검색은 능동 | 같음 |
| 검색 전후 처리 | `brcmf_do_escan`의 `brcmf_scan_config_mpc`는 `BRCMF_FEAT_QUIRK_NEED_MPC`(feature.c: 4329 칩만)일 때만 → 4378은 아무것도 안 함 | 아무것도 안 함 | 같음 |
| 검색 중 PM | 바꾸지 않음 | 바꾸지 않음 | 같음 |

### 1-3. 전원 절약과 초기 설정 값

| 항목 | brcmfmac (함수) | 우리 | 판정 |
|---|---|---|---|
| PM | `PM_FAST`(2) (`brcmf_config_dongle`, `brcmf_cfg80211_set_power_mgmt`, cfg80211 기본 절전 켬) | 0 | **다름** |
| pm2_sleep_ret(ms) | min(timeout, 2000) (`brcmf_cfg80211_set_power_mgmt`) | 설정 안 함 | **다름** |
| mpc | 1 (`common.c brcmf_c_preinit_dcmds`) | 0 (`Mpc` 기본) | 다름(연결 중에는 효과 없음 [가설]) |
| txbf | 1 (`brcmf_c_preinit_dcmds`) | 설정 안 함 | **다름** |
| arp_ol / arpoe / ndoe | 9 / 1 / 1 (`core.c brcmf_configure_arp_nd_offload(ifp, true)`) | 0 / 0 / 0 | **다름** |
| FAKEFRAG(frameburst) | 1 (`brcmf_config_dongle`) | 설정 안 함 | **다름** |
| roam_off, bcn_timeout | 모듈 기본 roamoff 0 → 펌웨어 로밍 켬, bcn_timeout 2 (`brcmf_dongle_roam`; roamoff=1이면 4) | roam_off 1(Windows가 로밍 결정), bcn_timeout 읽기만 | 다름(로밍은 WDI 구조상 그대로 둠) |
| ROAM_TRIGGER / DELTA | -75 / 20 | 읽기만 | 다름(로밍 꺼 둔 상태라 시험 안 함) |
| bw_cap 2.4 GHz 40 MHz, obss_coex | bw_cap 설정 + obss_coex -1 (`brcmf_cfg80211_attach`) | 설정 안 함 | 다름(5 GHz 연결에는 영향 적음 [가설]) |
| tdls_enable | TDLS 기능이 있으면 1 | 설정 안 함 | 다름(시험 안 함) |
| join_pref, SET_INFRA 1, 검색 시간 | 같은 값 | 같은 값 | 같음 |
| "assoc_listen", "ampdu_*" | 설정 안 함 | 설정 안 함 | 같음(둘 다 펌웨어 기본) |

### 1-4. 흐름 링 재개 메시지 (bcmdhd)

| 항목 | 내용 |
|---|---|
| 번호 | 요청 0x15, 완료 0x16 (bcmdhd `bcmmsgbuf.h`; Asahi brcmfmac `msgbuf.c`에는 번호만 있고 쓰지 않음) |
| 요청 형식 | 공통 머리 8바이트 + flow_ring_id(u16, 8) + reason(u16, 10) + 예약 (`tx_idle_flowring_resume_request_t`, `dhd_msgbuf.c dhd_prot_flow_ring_resume`) |
| 근거 | s258o 실행의 검색 4번 모두 펌웨어 쪽 쓰기 색인 사본이 호스트 쓰기 색인과 같았음(예: 호스트 318, 사본 0x13e = 318, 읽기 314에서 멈춤) [사실]. 이 사본의 뜻은 s258o 문서대로 가설입니다 |
| 위험 | 이 펌웨어가 메시지를 모르면 무시하거나 오류 상태로 답할 것으로 봅니다 [가설]. 펌웨어가 멈추면 그 부팅의 남은 후보는 시험되지 않습니다. 그래서 순서상 다섯 번째에 두었습니다 |

## 2. s258p에서 바꾼 것 [빌드만]

결론: 새 스위치 5개는 모두 기본값 0이며, 0이면 s258o와 똑같이 동작합니다.

| 스위치(`Device Parameters`) | 값 | 하는 일 |
|---|---|---|
| `ScanPm` | 0 / 2 | 2: 연결되면 SET_PM 2 + pm2_sleep_ret 2000 (brcmfmac 기본 절전) |
| `ScanChanList` | 0 / 1 / 2 | 연결 중 검색: 0 목록 없음(지금), 1 현재 대역의 유효 채널(펌웨어 `WLC_GET_VALID_CHANNELS`, 실패하면 고정 목록), 2 현재 채널 하나(전파가 연결 채널을 떠나지 않음) |
| `FlowResume` | 0 / 1 | 1: 검색 뒤 흐름 링이 안 읽히면 재개 메시지를 먼저 보내고 0.1초 기다린 뒤 초인종 → 다시 만들기 |
| `InitIovarSet` | 비트 묶음 | 펌웨어 초기화 때 brcmfmac 값으로: 1 txbf, 2 arp_ol/arpoe/ndoe, 4 FAKEFRAG, 8 bcn_timeout 4, 16 obss_coex -1, 32 mpc 1 |
| `CandidateCycle` | 0 / 1 | 1: 아래 후보를 차례로 돌림. `ScanFlowCheck`는 1로 고정, `TestConnScanSec`이 0이면 120 |

| 후보 번호 | 이름 | 그 후보 동안의 설정 |
|---|---|---|
| 0 | base | s258o 그대로(비교 기준) |
| 1 | pm2 | PM2 + pm2_sleep_ret 2000 |
| 2 | ch2 | 현재 채널 하나만 검색 |
| 3 | ch1 | 현재 대역 유효 채널 목록으로 검색 |
| 4 | rsm | 멈추면 재개 메시지 먼저 |
| 5 | pm2+ch1 | 1과 3 함께 |
| 6 | iov | txbf 1, arp_ol 9, arpoe 1, ndoe 1, FAKEFRAG 1, bcn_timeout 4, obss_coex -1 (켤 때 옛 값을 읽어 두고 끌 때 되돌림) |
| 7 | pm2+ch1+iov | brcmfmac에 가장 가깝게 |

- 후보 바꾸는 때: 연결 중 검색의 판정(스스로 읽음 / 멈춤 / 판정 불가)이 기록된 직후 다음 2초 진단 주기 또는 다음 검색 시작 직전. PM2·iov는 다음 후보로 넘어갈 때 되돌립니다. 그래서 각 후보는 다음 검색 전 약 60~120초 동안 켜져 있습니다.
- 검색마다 기록 `NwoaScanFlowLog`에 `k<후보 번호> b<설정 비트> n<채널 수>`가 붙습니다.
- 채널 목록이 든 검색 요청을 펌웨어가 거절하면 그 후보의 `e`를 세고 다음 후보로 넘어갑니다.

### 새 기록 (숫자만)

| 값 | 내용 |
|---|---|
| `NwoaCfgS258p` | 스위치 값(강제된 `ScanFlowCheck`, `TestConnScanSec` 포함) |
| `NwoaCand` | `cur` 지금 후보, `adv` 바꾼 횟수, `on` 지금 펌웨어에 걸린 비트, `pm` PM2 걸기/되돌리기 결과, `iov old/set/back` 항목별 읽기·설정·되돌리기 성공 비트, `vch` 유효 채널 수/읽기 결과, `rsm sent/cmplt/fix last` 재개 메시지 보낸 수·완료 받은 수·풀린 수·마지막 상태@링, `initiov` |
| `NwoaCandStats` | 후보마다 `n` 판정된 검색 수, `a` 스스로 읽음, `s` 멈춤, `i` 판정할 송신 없음, `g` 도중 끊김, `e` 검색 거절, `db`/`rs`/`rf` 초인종/재개/다시 만들기로 풀림 |
| `NwoaInitIovSt` | `InitIovarSet` 중 성공한 비트 |
| 판정 번호 9 | 재개 메시지로 풀림 (1~8은 s258o와 같음) |

## 3. 빌드 [빌드만]

| 항목 | 값 |
|---|---|
| 출력 | `port-j274/wifi/bcm4378-wificx/out/20261119-0816-s258p/package` |
| 단위 시험(개) | 234 통과 / 0 실패 (s258o 198 + 채널 인코딩·검색 요청·재개 메시지·후보 표 36) |
| MSBuild 경고 / 오류(개) | 0 / 0 |
| INF | `DriverVer = 11/19/2026,1.0.1.7` (s258o와 이 줄만 다름), infverif 통과, inf2cat 오류·경고 없음 |
| 서명 | .sys·.cat 모두 시험 인증서 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF`, .sys가 .cat에 들어 있음 |
| nwoa4378.sys 크기(바이트) | 171,928 (s258o 167,320) |

| 파일 | SHA-256 |
|---|---|
| nwoa4378.sys | `547149a6e9ae6bf95e6fd24e460baef64ae5170c9f7f87d6719f5d5008a429b4` |
| nwoa4378.cat | `7aee1f045e4d8fc47f6885b67b6a8970c57a966892cb0c1a7d36b3b0997d8e47` |
| nwoa4378.inf | `72511eaf25bd6eaf09ac081082df63fd5bcc0b1bb18d82adcbd38e092910ae52` |
| 펌웨어 5개·보드 파일·W17/W18/W19 ps1 | s258o와 같음 |
| 매니페스트 `file-delivery-s243/manifests/wifi-j274-20261119-0816-s258p.json` (12개, 실제 파일과 대조 통과) | `6ec4fd22df4b9fedab35e37280949008a731f677c7095f0bdf49d165b34c040a` |
| 전달 작업 1 `…/jobs/wifi-j274-20261119-0816-s258p.cmd` (3,620바이트) | `4221d7f6e8d4c669bd1363db11a2a4ad7e80855c11906fdbc0d5e8bc2d9256f6` |
| 전달 작업 2 `…-s258p-scripts.cmd` (2,020바이트) | `08b2ffeea29358170ba445c0d48deb93e24ef672e28df49850d327f68367129b` |

| 작업(`wifi-s258/jobs`, ASCII·CRLF) | 크기(바이트) | 하는 일 | SHA-256 |
|---|---|---|---|
| `W1-install-wifi-s258p.cmd` | 1,788 | 제자리 덮어 설치(W9 아님), 시험·스위치 값 40개 지움 | `4fdeb4f5e37278a8d142050529303d11d08c41ffeac4334fcb60f4139175cc49` |
| `W26-cycle-on.cmd` | 821 | `CandidateCycle=1`, `TestConnScanSec=120`, 장치 다시 시작 | `5a7797d79acecc2df6cea1240078b1948ebad678f754638f7a916faf6a1bc270` |
| `W26-cycle-off.cmd` | 523 | 두 값 지움(다음 장치 시작부터) | `be27112c145ab5ad5eaea600824b0bd3da4d9340f04fd7305a2535505e804ea0` |
| `W17-…-s258p.cmd` / `W18-…-s258p.cmd` | 471 / 439 | s258p 폴더의 ps1 실행 | `18c3a87f…` / `72bdb6d7…` |
| `W19-ping-start-s258p.cmd` | 773 | 핑 2400초(1800초 관찰을 덮음) | `033e6f2e…` |
| `wifi-s258/run-s258p.sh` | 9,749 | 4장 | `c23cb03e221ef1a04d582c9b59f9a5e1e49b23c3a23c388ee699588cacfc3532` |

## 4. 실기 시험 (콘치님이 실행)

결론: 한 번 실행(약 40분)으로 후보 8가지가 각각 1~2번씩 시험됩니다.

| 순서 | 명령 (`nwoas_scripts`에서) | 그 부팅의 설정 |
|---|---|---|
| 1 | `bash wifi-s258/run-s258p.sh cycle <지금 실행기 .out> <지금 LOG.link> 1800` | 전달 2개 → W1(모두 기본값) → W26(후보 순환 + 120초 검색) → W17 → W19 핑 → 300초마다 W2 → W18 → W19 끝 → W15 → W26 끄기 → CHECK |
| 2 (필요하면) | `bash wifi-s258/run-s258p.sh recycle <새 .out> <새 .link> 1800` | 이미 s258p가 깔린 상태에서 같은 순환을 한 번 더(후보당 판정 수를 늘림) |

- 지금 링크는 `runs/s258o-scans-20261119-073237.txt` 끝줄(`S258O scans DONE link=…`)에 있습니다.
- 결과는 `wifi-s258/runs/s258p-<모드>-<시각>.txt`, 끝줄 `S258P … DONE link=…`의 link를 다음 실행에 넣습니다.
- 검색 간격 120초: 연결 120초 뒤 첫 검색, Windows 자체 검색(연결 약 180초 뒤)도 그때 후보로 셉니다. 1800초 관찰이면 연결 중 검색이 약 14~15번입니다.

### 결과 읽는 법

끝의 `CHECK TABLE` 줄이 후보마다 `scans alive stuck idle gone err dbfix rsmfix refresh -> 읽기`를 보여 줍니다.

| CHECK TABLE의 읽기 | 뜻 |
|---|---|
| `base`가 `stuck every time`, 다른 후보가 `firmware resumed by itself` | 그 후보의 설정이 원인과 관련 있음. 그 설정을 기본값으로 하는 s258q를 만들고 `scans` 방식으로 20분 확인 |
| `ch2`만 스스로 읽음 | 전파가 연결 채널을 떠나는 것이 계기. 채널 목록을 줄이거나 검색을 나누는 쪽으로 감 |
| `pm2`(와 그 조합)만 스스로 읽음 | PM0에서만 생기는 펌웨어 경로. `ScanPm=2` 기본으로 감 |
| `rsm`에서 `FLOW_RING_RESUME freed it` | 펌웨어가 이 링을 "멈춤" 상태로 두고 있었고 재개 메시지로 풀림 → 다시 만들기 대신 재개 메시지를 쓰면 됨. `NwoaCand`의 `rsm cmplt`(완료 받은 수)와 `last` 상태도 같이 봄 |
| `rsm`이 `refresh`로만 풀림, `rsm cmplt0` | 이 펌웨어는 재개 메시지를 처리하지 않음 |
| `iov`(와 조합)만 스스로 읽음 | 초기 설정 차이가 원인. `NwoaCand`의 `iov set` 비트로 어느 값이 실제로 들어갔는지 확인한 뒤 `InitIovarSet`로 하나씩 가름 |
| 모든 후보가 `stuck every time` | Linux와 다른 설정 때문은 아님(이 8가지 안에서는). 펌웨어 동작으로 보고 s258o의 다시 만들기를 유지하면서, 검색 중 핑 손실(검색 3.5초 동안 송신 완료 0)을 줄이는 쪽으로 감 |
| `err` > 0 | 채널 목록이 든 검색 요청을 펌웨어가 거절. `NwoaEscanRequest` 값과 `NwoaCand`의 `vch`로 확인 |
| 펌웨어 멈춤(Wi-Fi가 그 뒤 죽음) | `NwoaCand`의 `cur`가 그때의 후보. `rsm`이면 재개 메시지 탓일 가능성 |

- 한 후보의 판정이 1~2번뿐이라 "한 번 스스로 읽음"은 우연일 수 있습니다. 두드러진 후보가 나오면 그 후보만 정적 스위치(`ScanPm`, `ScanChanList`, `FlowResume`, `InitIovarSet`)로 켜고 `TestConnScanSec=120`으로 20분 더 확인하는 것이 다음 순서입니다.
- 이 실행의 PASS/NOT PASS 줄은 s258o 기준(검색 8번 이상, 멈춤 0, 끊김 0, 핑 손실 1% 미만, v4=Internet)을 그대로 씁니다. 후보를 가르는 것이 목적이므로 NOT PASS여도 표가 채워졌으면 실행은 쓸모가 있습니다.

## 5. 확인 안 된 것
- 맥미니에서는 아무것도 돌리지 않았습니다. 후보 설정이 실제로 펌웨어에 들어가는지(`NwoaCand`의 `pm`, `iov set`), 채널 목록 검색을 펌웨어가 받는지는 실기에서만 압니다 [빌드만].
- 재개 메시지 형식은 bcmdhd 공개 소스 기준이고, Apple 펌웨어 18.20.511이 이 메시지를 아는지는 모릅니다 [가설].
- STA 모드 흐름 링에서 펌웨어가 da를 쓰지 않는다는 것은 brcmfmac 호스트 쪽 코드로 미루어 본 가설입니다.
- 실행 중 txbf·FAKEFRAG를 바꾸는 것은 펌웨어가 연결 중에 받지 않을 수 있습니다(받지 않으면 `iov set` 비트가 빠짐).
