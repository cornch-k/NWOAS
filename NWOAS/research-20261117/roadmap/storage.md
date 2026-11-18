# 저장장치 로드맵 — 내장 SSD · USB 저장장치 · 부팅 디스크 (2026-11-17)

> 읽기와 계획만 했습니다. 아무것도 빌드·실행하지 않았고, 맥미니·시리얼·USB 장치는 건드리지 않았습니다. 웹에서는 공개 저장소(GitHub)만 읽었습니다.
> 표기: **[사실]** = 파일:줄 또는 URL로 확인한 것. **[가설]** = 확인하지 않은 추측. 참조 프로젝트(windows-on-m1) 작성자가 남긴 기록은 "그쪽 기록에 이렇게 적혀 있다"는 뜻에서 [사실]로 표시하되, 원본 증거는 우리가 다시 보지 못했다는 점을 함께 적었습니다.
> 경로 줄임: `HV` = `m1n1-gpu-hv2/src/`(지금 쓰는 하이퍼바이저 g254L 소스), `E` = `external/windows-on-m1/`(우리 사본, main 6c65673, 2026-11-07), `P` = `nwoas_scripts/plan-20261115/`, `L` = `nwoas_scripts/layout-s231/`.
> 빌드 성공은 부팅 성공이 아닙니다. 이 문서의 어떤 단계도 아직 실제 맥미니에서 시험하지 않았습니다.

---

## 0. 결론부터

1. **지금 SSD 경로는 이미 "장치가 Windows 메모리로 직접 옮기고, 하이퍼바이저는 명령 하나하나를 검사하는" 구조입니다. 느린 이유는 복사가 아니라, 하이퍼바이저가 명령을 한 번에 하나씩만 넣고 끝날 때까지 다른 일을 막은 채 기다리기 때문입니다.** [사실: `HV/nvme.c:217`(태그 0 하나만 씀), `:248-283`(완료까지 돌며 기다림), `HV/hv_vm.c:1211-1213`(전역 잠금을 쥔 채 처리), `:1743`·`:1600`(문 두드리기 한 번에 1개, 나머지는 5 kHz 틱마다 1개)]
2. **큰 파일을 순서대로 읽고 쓰는 속도는 9월 측정에서 이미 쓰기 약 2.0 GB/s, 읽기 약 1.3 GB/s였습니다. 작은 조각을 여기저기 읽는 속도(앱 실행·검색·백신 검사 같은 일상 작업)는 한 번도 재지 않았습니다.** 체감이 느린 것은 이쪽일 가능성이 큽니다 [가설]. 지금 구성(GPU·DCP가 들어간 g254L)에서는 SSD 속도를 잰 기록이 없습니다 [사실: 11월 로그 검색 결과, 1.4절].
3. **가장 먼저 할 일은 지금 구성에서 속도를 재는 것(단계 0), 그다음은 하이퍼바이저가 SSD 명령을 기다리지 않고 여러 개를 동시에 넣게 바꾸는 것(단계 2)입니다.** Windows 쪽은 지금 그대로(Microsoft 기본 NVMe 드라이버)이고, macOS 파티션 보호 검사도 그대로 둡니다. 결정 D2("검사하는 직접 방식")의 앞 절반에 해당합니다.
4. **전원 끄기·재시작 때 하이퍼바이저가 SSD를 정리하지 않습니다.** Windows가 종료 직전에 보내는 플러시는 실제 SSD로 전달되지만, SSD 제어 칩을 정상 종료하는 명령은 나가지 않습니다 [사실: `HV/hv_psci.c:1523-1541`, `HV/nwoas_frontend/model.c:109-113`]. 작은 수정입니다(단계 1).
5. **TRIM(지운 칸을 SSD에 알려 주기)은 지금 꺼져 있습니다** [사실: `HV/nwoas_frontend/admin_payload.c:30-37`에서 지원 표시 칸을 0으로 둠]. 켜려면 Windows 구역 안의 범위만 보내는 울타리가 꼭 필요하고, macOS에 가장 위험한 단계라 백업 뒤에 합니다(단계 4).
6. **USB 저장장치 읽기 손상은 원인을 찾았고 고쳐졌습니다**(PCIe 브리지 주소 1 MiB를 메모리에서 빼기, S236·S237) [사실: `nwoas_scripts/usb-s232/HANDOFF-USB-A-CORRUPTION.md` 8~10절]. 남은 것은 USB로 쓰기 시험 한 가지입니다(단계 9).
7. 참조 프로젝트는 11월 7일 이후에도 SSD 드라이버를 계속 고쳤습니다(충돌 덤프, TRIM 울타리, 종료 처리, 사라진 완료 신호 대비). 우리 사본에는 이 내용이 없습니다 [사실: GitHub `ans/bounce-t3` 가지, 4절].

---

## 1. 지금 상태

### 1.1 SSD 읽기·쓰기가 지금 지나가는 길

| 순서 | 무엇이 일어나는가 | 근거 |
|---|---|---|
| 1 | Windows는 Microsoft 기본 NVMe 드라이버(stornvme)로, 하이퍼바이저가 흉내 내는 가짜 NVMe 장치(PCI 설정 주소 0x700000000, 레지스터 0x700100000)를 씁니다 | [사실] `HV/hv_vm.c:1219-1220`, `HV/nwoas_s229_adapter.inc:135-137`, 펌웨어 DSDT `fw-audio-s256-mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl:119-122` |
| 2 | 장치 설정·관리 명령(Identify, 큐 만들기 등)은 S229/S230부터 하이퍼바이저 C 코드가 직접 답합니다 | [사실] `HV/nwoas_s229_adapter.inc:1-2, 110-118`, `HV/nwoas_frontend/admin_state.c`, `admin_payload.c` |
| 3 | Windows가 읽기·쓰기 명령을 줄에 넣고 문을 두드리면(0x1008 쓰기) 하이퍼바이저로 넘어옵니다 | [사실] `HV/hv_vm.c:1735-1747` |
| 4 | 하이퍼바이저가 명령 64바이트를 자기 메모리로 **먼저 복사한 뒤** 검사합니다: 명령 종류(읽기·쓰기·플러시만), LBA가 디스크 안인지, 쓰기면 Windows 구역(LBA 39944960~59968629) 안인지 | [사실] `HV/hv_vm.c:1528`(복사), `:1461-1496`(검사) |
| 5 | Windows가 준 메모리 주소 목록(PRP)을 실제 주소로 바꾸며, Windows 메모리 범위 밖이거나 보안 예약 영역(MCC TZ)과 겹치면 거부합니다. 바꾼 주소 목록은 하이퍼바이저 메모리에 만듭니다 | [사실] `HV/nvme.c:590-611`, `:621-690` |
| 6 | 실제 SSD 제어 칩(ANS2)에 명령을 넣습니다. **태그(번호표)는 늘 0번 하나**이고, 끝날 때까지 최대 1초 동안 돌면서 기다립니다 | [사실] `HV/nvme.c:217`, `:245`, `:248-283`, `:15` |
| 7 | 데이터는 SSD가 Windows 메모리로 직접 옮깁니다(복사 없음) | [사실] `HV/nvme.c:621-700`(S101 무복사) |
| 8 | 끝나면 하이퍼바이저가 Windows 완료 줄에 결과를 쓰고 가상 인터럽트 900을 넣습니다 | [사실] `HV/hv_vm.c:1565-1589`, `HV/hv_exc.c:479-505` |
| 9 | 3~8 전체가 하이퍼바이저 전역 잠금(bhl)을 쥔 채 돕니다. 그동안 다른 CPU가 하이퍼바이저로 들어오면(타이머, GPU, 화면 서비스 등) 기다립니다 | [사실] `HV/hv_vm.c:1211-1213` 주석, `port-j274/gpu/hv/README-dcp-s251.md:81`(틱·화면 서비스도 같은 잠금) |
| 10 | 문 두드리기 한 번에 명령 1개만 처리합니다. 한 번에 여러 개가 쌓이면 나머지는 5 kHz 틱(0.2 ms)마다 1개씩 처리합니다 | [사실] `HV/hv_vm.c:1743`, `:1596-1601`, `HV/hv.c:24, 494` |
| 11 | Windows에 알리는 입출력 큐는 1쌍뿐입니다 | [사실] `HV/nwoas_frontend/admin_state.c:65` |
| 12 | 한 명령의 최대 크기는 1 MiB입니다(하이퍼바이저 빌드 설정 256블록, 장치 정보 MDTS 8) | [사실] `port-j274/gpu/hv/tools-hv2/buildvar.sh:15`, `nwoas_scripts/agxdcp-s256-guest-test.sh:47`, `L/module.py:8-14` |
| 13 | 두 번째 가상 디스크(NS2, 작업 통로·파일 전달용)의 명령은 하이퍼바이저가 처리하지 못하고 **맥북에 묻습니다.** 묻는 동안 모든 CPU를 한곳에 모읍니다(시간 보정 기능이 기본으로 켜져 있음) | [사실] `HV/hv_vm.c:1472-1473` → `:1443`, `HV/hv_exc.c:67, 92-93` |
| 14 | Windows 쪽 작업 통로 프로그램은 0.5초마다 NS2를 읽습니다 | [사실] `nwoas_scripts/worker-s177/nwos.c:142`(작업 통로 모듈이 이 프로그램을 NW177.EXE로 넣음, `nwoas_scripts/memory-s180/link_module.py:23`) |

- 이 SSD 처리 코드는 S231 하이퍼바이저와 **바이트 단위로 같습니다**(`nvme.c`, `nwoas_s229_adapter.inc`, `hv_vm.c`의 SSD 구간을 비교함) [사실: 이번 조사에서 `diff`]. 그래서 9월 S158~S230 측정값은 같은 SSD 코드의 숫자이지만, 지금은 GPU·화면 서비스가 같은 잠금을 함께 쓰므로 그대로 같다고 볼 수는 없습니다 [가설].
- SSD 제어 칩은 이미 64개 동시 명령을 받도록 설정돼 있습니다(대기 명령 수, NVMMU 칸 수, 입출력 큐 크기). 쓰지 않을 뿐입니다 [사실: `HV/nvme.c:18, 361-363, 387, 398`]. Asahi Linux도 이 칩(T8103)에 64개를 씁니다 [사실: https://github.com/torvalds/linux/blob/master/drivers/nvme/host/apple.c (줄 1726-1730)].

### 1.2 디스크 배치와 macOS 보호 장치

GPT(2026-11-15 읽기 전용으로 읽은 것, `nwoas_scripts/ans2-s87/run-20261115-180239/lba-2.bin`을 이번에 해석) [사실]:

| 칸(1부터) | 이름 | LBA 범위(4 KiB 블록) | 크기(GB) | Windows 쓰기 |
|---|---|---|---|---|
| 1 | iBootSystemContainer | 6 ~ 128005 | 0.5 | 거부 |
| 2 | macOS APFS | 128006 ~ 38968337 | 159.1 | 거부 |
| 3 | NWSTUB 자리 → 지금은 13.5 스텁 APFS | 38968576 ~ 39944703 | 4.0 | 거부 |
| 4 | EFI 시스템 | 39944960 ~ 40076031 | 0.5 | 허용 |
| 5 | Microsoft 예약 | 40076032 ~ 40080127 | 0.0 | 허용 |
| 6 | Windows(C:) | 40080128 ~ 59968511 | 81.5 | 허용 |
| 7 | RecoveryOSContainer | 59968630 ~ 61279338 | 5.4 | 거부 |

- 칸 3은 이후 13.5 스텁 APFS 항목으로 바뀌었고 보호 검사도 그 항목을 받아들이게 고쳐졌습니다 [사실: `L/guest_module.py:178-188`].

| 보호 장치 | 무엇을 막나 | 근거 |
|---|---|---|
| 하이퍼바이저 C, 빠른 처리부 | Windows 구역 밖 쓰기를 "읽기 전용 범위" 오류로 돌려줌(GPT 블록 포함) | [사실] `HV/hv_vm.c:1222-1223, 1494-1496` |
| 하이퍼바이저 C, 실제 쓰기 함수 | 같은 구역 + GPT 블록(0~5, 61279339~61279343)만 허용. 거부하면 로그 한 줄 | [사실] `HV/nvme.c:536-576` |
| 하이퍼바이저 C, 메모리 주소 검사 | Windows 메모리 밖·TZ 예약 영역으로의 DMA 거부 | [사실] `HV/nvme.c:590-611`, `HV/mcc.c:179-216` |
| 맥북 Python, 시작 때 | 디스크 GUID 898f172d…와 "EFI PART" 확인, 보호 GPT 항목 4개를 바이트로 비교, GPT 쓰기는 GptGuard만 | [사실] `L/guest_module.py:161, 173-195` |
| Windows에게 보이는 것 | 디스크 전체(61279344블록)가 보이고, macOS 파티션은 "알 수 없는 파티션"으로 보임 | [사실] `HV/nwoas_frontend/admin_payload.c:27-28`; 보이는 모양은 [가설](화면으로 확인 안 함) |

빈틈 [사실, 코드에서 바로 보임]:
- 주소 검사가 빼는 예약 영역은 MCC TZ뿐입니다. 기기 설명표의 다른 예약 영역(`/chosen/carveout-memory-map`, `port-j274/README.md` L-8)과 하이퍼바이저가 쥔 GPU·화면 메모리는 따로 빼지 않습니다 [사실: `HV/nvme.c:607-609`]. 정상 Windows는 그 주소를 쓰지 않으므로 지금 문제는 아닙니다 [가설].
- 디스크 GUID·보호 항목 검사는 맥북 Python에만 있습니다. 맥북 없이 켜려면 C로 옮겨야 합니다 [사실: `P/standalone.md:98, 201`].

### 1.3 왜 느린가 — 코드 근거

| 번호 | 원인 | 코드 근거 [사실] | 영향 [가설] |
|---|---|---|---|
| A | 실제 SSD에 한 번에 명령 1개만 넣고 끝날 때까지 기다림 | `HV/nvme.c:217, 245, 248-283` | Windows가 32개를 동시에 넣어도 1개씩 줄을 섬. 작은 무작위 읽기에서 가장 크게 손해. 참조 J313 실측으로 4 KiB 읽기는 1개씩이면 약 1만 회/초, 32개 동시면 약 8.7만~9.4만 회/초(약 8~9배 차이) — 그쪽 기록 `E/investigation/EXPERIMENTS.md:60271-60283`, `:60555-60560` |
| B | 기다리는 동안 하이퍼바이저 전역 잠금을 쥠 | `HV/hv_vm.c:1211-1213`; 틱·화면 서비스도 같은 잠금 `README-dcp-s251.md:81` | 1 MiB 명령 하나에 약 0.5~0.8 ms(1.4절 계산) 동안 다른 CPU의 하이퍼바이저 일(타이머·GPU·화면)이 밀림. 끊김·버벅임의 한 원인일 수 있음 |
| C | 문 두드리기 한 번에 1개, 남은 것은 0.2 ms마다 1개 | `HV/hv_vm.c:1743`, `:1596-1601`; 9월 검토 `nwoas_scripts/claude-s156/nvme-watchdog-review.md:8` | 쌓인 명령은 초당 약 5000개가 상한 |
| D | 작업 통로가 0.5초마다 맥북에 묻고, 그때마다 모든 CPU가 멈춤 | `worker-s177/nwos.c:142`, `HV/hv_vm.c:1443`, `HV/hv_exc.c:67, 92-93`; NS2 한 번 최대 10.6~15.9 ms 측정 `NWOAS-STATUS-2026-09-10-SESSION.md:13, 46` | 초당 두 번 전체 멈춤. 체감 끊김 원인일 수 있음 |
| E | 쓰기에 "즉시 기록(FUA)" 표시가 있거나 캐시를 끈 경우 쓰기마다 플러시를 하나 더 기다림 | `HV/hv_vm.c:1504` | 쓰기가 많은 작업에서 추가 지연 |
| F | 모든 페이지에 DMA 전 캐시 정리 | `HV/nvme.c:665-674` | 1 MiB당 1만6천 줄 정리. 크기 미측정 |
| G | TRIM 꺼짐 | `HV/nwoas_frontend/admin_payload.c:30-37`(지원 표시 칸 520 미설정) | 오래 쓰면 SSD 쓰기가 느려질 수 있음 |
| H | (해결됨) g252J 전까지 vGIC 우선순위 읽기 버그가 NVMe 인터럽트 900에도 걸려 있었음 | `HANDOFF-2026-11-16-evening.md:239`(§3.8) | 9월 숫자는 이 버그가 있던 상태의 값 |

- **체감이 SSD 때문인지 자체가 아직 확인되지 않았습니다.** GPU·CPU·작업 통로 멈춤(D)이 섞여 있을 수 있어 단계 0에서 함께 잽니다 [가설].

### 1.4 트리에 있는 측정값 (모두 9월, 지금 하이퍼바이저가 아님)

MB/s는 기록된 바이트 수와 시간으로 이번에 계산했습니다(1 MB = 10^6 바이트).

| 시점·판 | 시험 | 결과 | MB/s(계산) | 근거 |
|---|---|---|---|---|
| S97 (맥북 Python 중계) | 4 KiB 한 번 / 64 KiB 한 번 | 1.0 ms / 6.0 ms | 4.2 / 10.6 | [사실] `NWOAS-STATUS-2026-09-06.md:918` |
| S98 | DISM 설치 쓰기 | — | 약 3 | [사실] 같은 파일 `:928` |
| S101 (무복사 시작) | 설치 쓰기, 명령당 맥북 쪽 1.3 ms | — | 약 12 | [사실] `NWOAS-STATUS-2026-09-07-SESSION.md:24` |
| S129 | 바탕화면 부팅 중 | 약 52 회/초, 명령당 맥북 3.7 ms + 게스트 15.4 ms | — | [사실] `NWOAS-STATUS-2026-09-09-SESSION.md:133` |
| S140 | 8스레드 64 KiB 버퍼 없는 읽기, 64 MiB | 3,497,229 µs | 19.2 | [사실] 같은 파일 `:331-334` |
| S158 (하이퍼바이저 안 처리) | 같은 시험 | 약 196~200 ms | 336~342 | [사실] `NWOAS-STATUS-2026-09-10-SESSION.md:7` |
| S159 (틱에서만 처리, 실패한 시도) | 같은 시험 | 388~435 ms | 154~173 | [사실] 같은 곳 |
| S160a | 같은 시험, 반복 | 118.7~134.7 ms | 498~565 | [사실] 같은 파일 `:13` |
| S163 gap50 | 같은 시험, 5분 부하 1979회 | 최소 130.3 / 중앙 138.5 / 최대 1181 ms | 515 / 485 / 57 | [사실] `nwoas_scripts/nvme-s163/active-50-result.json` |
| S193~S230 (12회) | IOTEST: 1 MiB씩 256 MiB 즉시 기록 쓰기 → 버퍼 없이 읽기 | 쓰기 130.4~152.8 ms, 읽기 200.8~231.6 ms | 쓰기 1757~2058(중앙 1979), 읽기 1159~1337(중앙 1295) | [사실] `nwoas_scripts/logs/*/job-1.log`의 `S195 SUMMARY` 12줄, 예: `nwoas_scripts/storage-s195/hardware-s193-gap50.json` |
| S160 / S160b | 명령 하나 최장 시간 | 하이퍼바이저 안 6.10 / 7.67 ms, 맥북 묻기 10.6 / 15.9 ms | — | [사실] `NWOAS-STATUS-2026-09-10-SESSION.md:13, 46` |
| S231 WinPE (11-15) | USB-A → SSD `robocopy /J` | — | 133 | [사실] `L/USB-READ-CORRUPTION-20261115.md` 표 |

없는 숫자 [사실: 트리·11월 로그 검색에서 찾지 못함]:
- 지금 구성(g254L + S256)의 SSD 속도.
- 4 KiB 무작위 읽기·쓰기, 동시 명령 수를 바꾼 시험.
- Windows 부팅 시간 중 SSD가 차지하는 몫.

참조 J313(맥북 에어) 숫자 — 그쪽 기록, 원본 증거는 우리 사본에 없음 [사실: 기록이 이렇게 적혀 있음]:

| 방식 | 순차 읽기 1M 8개 동시 (MB/s) | 순차 쓰기 1M (MB/s) | 4 KiB 읽기 32개 동시 (회/초) | 근거 |
|---|---|---|---|---|
| 하이퍼바이저가 흉내 내는 NVMe, 한 번에 하나(우리와 비슷한 방식) | 478 | 927 | 12,835 | `E/investigation/EXPERIMENTS.md:60293-60300` |
| 하이퍼바이저가 명령마다 검사, 동시 최대 17개 이상(브로커 방식) | 순차 시험 없음. 64 MiB 쓰기+플러시 122 ms, 파일 복사 읽기 약 850 | — | — | `EXPERIMENTS.md:60104-60105`, `ANS_CURRENT_STATE.md:58` |
| Windows 드라이버가 직접(검사 없음) | 2811~3433 | 2147~2381 | 85,061~94,453 | `EXPERIMENTS.md:60271-60283, 60529-60534, 60555-60560` |

- 우리 9월 IOTEST의 1 MiB 한 번에 하나 읽기(약 1.3 GB/s)는 그쪽 직접 방식의 "1개씩 읽기" 1564 MB/s(`EXPERIMENTS.md:60278`)와 크게 다르지 않습니다. 순차 대용량은 이미 하드웨어 한계 근처이고, 차이는 **동시 처리**에서 납니다 [가설, 두 기기·조건이 다름].

### 1.5 끄기·재시작·플러시·TRIM·절전

| 항목 | 지금 | 근거 |
|---|---|---|
| Windows 종료 때 플러시 | Windows가 가짜 장치에 종료 요청(CC.SHN)을 쓰면 하이퍼바이저가 실제 SSD에 FLUSH를 보낸 뒤 "종료 완료"로 답함 | [사실] `HV/nwoas_frontend/model.c:109-113`, 어댑터 `HV/nwoas_s229_adapter.inc:75-84` |
| 실제 SSD 제어 칩 정상 종료(큐 삭제·종료 요청·펌웨어 재우기) | **하지 않음.** PSCI 재시작은 `reboot()`만, 끄기는 콘솔 비우고 `reboot()` | [사실] `HV/hv_psci.c:1523-1529, 1538-1541`, `HV/utils.c:129-133` |
| PSCI "끄기" | 실제로는 재부팅(TODO 주석) | [사실] `HV/hv_psci.c:1524-1527` |
| 쓰기 캐시 표시 | 켜짐(Windows가 플러시를 직접 보냄) | [사실] `HV/nwoas_s229_adapter.inc:8, 114`, `admin_payload.c:37` |
| TRIM | 꺼짐 | [사실] 1.3절 G |
| 절전(모던 스탠바이) | GPU 쪽 문제만 기록돼 있고, SSD 쪽 전원 명령 처리는 조사한 기록이 없음 | [사실] `HANDOFF-2026-11-16-evening.md:161-171` |
| 최대 절전·충돌 덤프 | 둘 다 Windows가 인터럽트 없이 돌면서 SSD에 씀. 지금 경로에서는 쌓인 명령이 0.2 ms마다 1개씩만 풀려 느리거나 실패할 수 있다는 9월 검토가 있음 | [사실] `nwoas_scripts/claude-s156/nvme-watchdog-review.md:10`; 실제 실패 여부는 [가설] |

- Asahi Linux는 끌 때와 잠들 때 모두 "NVMe 종료 → 펌웨어 종료 → 보조 프로세서 정지"를 합니다 [사실: https://github.com/torvalds/linux/blob/master/drivers/nvme/host/apple.c (줄 1682-1715)]. 정리 없이 꺼도 데이터가 안전한지는 확인된 바 없습니다 [가설].

### 1.6 USB 저장장치 읽기 손상 — 해결됨

| 항목 | 내용 | 근거 |
|---|---|---|
| 증상 | USB-A(FL1100)에서 캐시를 거쳐 크게 읽으면 64 KiB 조각 몇 개가 0으로 옴 | [사실] `P/usb.md` 결론 |
| 원인 | Windows가 주소 0xC0000000~0xC00FFFFF(PCIe 브리지 창과 겹치는 1 MiB)를 메모리로 써서, USB 장치의 DMA가 RAM에 닿지 않음 | [사실] `nwoas_scripts/usb-s232/HANDOFF-USB-A-CORRUPTION.md` 7절 |
| 고침 | (1) 설치된 Windows BCD `badmemorylist`(S236), (2) 펌웨어가 그 1 MiB를 예약(S237) | [사실] 같은 문서 8절, `nwoas_scripts/usb-s236/README.md`, `fw-audio-s256-mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c:415-441` |
| 검증 | 고친 뒤 8회 중 8회 정상(고치기 전 9회 중 9회 손상) | [사실] HANDOFF 8절 표 |
| 남은 것 | USB로 **쓰기** 시험 안 함. 브리지 창이 옮겨지면 예약도 따라가야 함 | [사실] HANDOFF 10절 |

- 지금 펌웨어 페이로드(S256)의 빌드 트리에 S237 예약 코드가 들어 있습니다 [사실: 위 `MemoryInitPeiLib.c:415`]. 실행 때 "S237 reserved" 줄이 찍혔는지는 이번에 로그로 확인하지 않았습니다.

### 1.7 부팅 디스크

| 단계 | 무엇이 SSD를 읽나 | 근거 |
|---|---|---|
| iBoot | Apple 펌웨어가 칸 3 스텁(13.5)에서 m1n1을 읽음 | [사실] `P/DECISIONS.md:5`(D1), `HANDOFF-2026-11-16-evening.md:12` |
| 첫 m1n1(D4) | SSD를 쓰지 않음. 옛 SSD 초기화 코드가 Tahoe에서 레지스터 0x27bce4008 접근으로 죽음 | [사실] `research-20261116/RESULT-20261116-morning.md:50` |
| 하이퍼바이저 | 맥북 Python이 `nvme_init`을 부르고 보호 검사 뒤 가짜 장치를 켬 | [사실] `L/guest_module.py:106-108, 161-195` |
| UEFI(Mu) | 표준 NvmExpressDxe가 가짜 NVMe를 읽어 Windows 부트 관리자를 찾음 | [사실] `fw-audio-s256-mu/Platform/MacMini2020Pkg/MacMini2020.fdf:162, 167`, `fw-audio-s256-mu/Silicon/Apple/AppleSiliconPkg/Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c:618-629` |
| Windows | stornvme | 1.1절 |

- UEFI 단계도 하이퍼바이저 빠른 처리부를 쓰는지는 로그로 확인하지 않았습니다. S229 코드가 큐가 생기면 곧바로 켜는 구조라 쓸 가능성이 큽니다 [가설: `HV/nwoas_s229_adapter.inc:87-108`].

---

## 2. 목표 상태 — 저장장치에서 "네이티브"의 뜻

| 항목 | 지금 | 목표 |
|---|---|---|
| 동시 명령 | 실제 SSD에 1개 | 실제 SSD에 최대 64개 |
| 데이터 이동 | 장치가 Windows 메모리로 직접(이미 됨) | 그대로 |
| 명령 검사 | 명령마다 하이퍼바이저가 복사 후 검사(이미 됨) | 그대로, 검사 범위를 넓힘(1.2절 빈틈) |
| 하이퍼바이저가 기다리는가 | 끝날 때까지 전역 잠금을 쥐고 기다림 | 넣자마자 Windows로 돌아감. 완료는 나중에 거둠 |
| macOS 파티션 | 쓰기 거부 | 그대로. TRIM도 Windows 구역 안만 |
| 끄기·재시작 | 플러시만, 칩 정상 종료 없음 | 플러시 + 칩 정상 종료 |
| TRIM | 없음 | Windows 구역 안만 |
| 맥북 의존 | SSD 켜기·보호 검사가 맥북 Python | C에서 혼자 |
| 작업 통로 | 0.5초마다 전체 CPU 멈춤 | 평소에는 멈춤 없음 |
| 속도 기대치 | 미측정 | 참조 J313 직접 방식 숫자(1.4절)가 하드웨어 상한. 우리 목표로 약속하지 않음 [가설] |

D2("검사하는 직접 방식")와의 관계: D2는 "데이터는 장치가 직접, 명령을 넣는 순간에만 하이퍼바이저가 검사"입니다(`P/DECISIONS.md:6`). **지금 경로는 이 중 "직접 옮기기"와 "넣을 때 검사"를 이미 하고 있고, 빠진 것은 "넣은 뒤 기다리지 않기"입니다** [사실: 1.1절 4~7행]. 그래서 이 문서는 D2를 두 덩어리로 나눕니다.
- D2 앞 절반(단계 2): 지금 가짜 NVMe 장치를 그대로 두고 하이퍼바이저를 기다리지 않게 바꿈. Windows 드라이버·서명·ACPI 변경 없음.
- D2 뒤 절반(단계 7): 참조 프로젝트의 Apple 전용 Windows 드라이버(AppleAns)로 바꿈. 단계 2를 잰 뒤 더 할 가치가 있는지 메인 세션이 정합니다. **이 나눔은 이 조사의 제안이며 D2를 바꾸는 결정이 아닙니다.**

Apple 하드웨어가 원래 이 구조를 염두에 두고 만들어졌다는 점도 참고가 됩니다. Asahi 드라이버 주석에 따르면 이 칩의 내장 주소 변환기(NVMMU)는 "아래 단계 드라이버가 명령을 쓰고, 위 단계 코드가 주소를 검사한 사본으로 변환표를 채우는" 방식을 위해 설계됐습니다 [사실: https://github.com/torvalds/linux/blob/master/drivers/nvme/host/apple.c (줄 88-106)].

---

## 3. 단계별 계획

공통 규칙:
- 새 하이퍼바이저는 매번 새 폴더로 빌드합니다(`tools-hv2/buildvar.sh`가 같은 폴더를 거절) [사실: `HANDOFF-2026-11-16-evening.md:76`]. 지금 쓰는 g254L·S256 구성은 되돌아갈 자리로 그대로 둡니다.
- 하이퍼바이저 SSD 코드 변경은 시스템 핵심부이므로 Opus 이상으로 합니다(프로젝트 규칙).
- Windows 재시작은 허용된 스크립트(`agx-s248/run-reboot-arm-*.sh`)로 하며 콘치님께 따로 묻지 않습니다 [사실: `HANDOFF-2026-11-16-evening.md:45-56`]. 맥미니가 멈추면 전원을 다시 넣는 것은 콘치님 손이 필요합니다.
- 모든 단계 끝에 "보호 파티션 읽기 해시"(단계 5a)를 전후로 비교합니다. macOS가 켜지지 않는 동안에는 칸 1·2·3·7의 내용이 바뀌면 안 됩니다 [가설: iBoot가 칸 1(iBootSystemContainer)에 쓸 수 있으므로 칸 1이 바뀌면 먼저 그쪽을 의심].

### 단계 0. 지금 구성에서 SSD 속도 재기 (기준선)

- **할 일**
  1. 순차: 기존 `IOTEST.EXE`(256 MiB, 1 MiB씩, 즉시 기록 쓰기 후 버퍼 없이 읽고 모든 값 비교) [사실: `nwoas_scripts/storage-s195/README.md`]. 파일 이름을 바꿔 가며 3회.
  2. 병렬: 기존 `DISKREAD.EXE`(8스레드, 64 KiB) [사실: `NWOAS-STATUS-2026-09-09-SESSION.md:331-333`] 3회.
  3. 무작위: 4 KiB 읽기·쓰기를 동시 1·8·32개로. 도구는 (가) storage-s195처럼 우리가 만드는 작은 프로그램, 또는 (나) Microsoft DiskSpd(MIT 공개 도구). 어느 쪽이든 C:의 시험 파일 하나에만 씁니다.
  4. 같은 시간에 하이퍼바이저 카운터를 읽습니다: 명령 수·오류 수(질의 3), 가장 긴 명령 시간(5·6), 한 번에 쌓인 최대 명령 수·완료 확인 횟수(7) [사실: `HV/hv_vm.c:1633-1643`]. 질의 경로는 `native-s230/query_module.py`(지금 실행기에 들어 있음, `agxdcp-s256-guest-test.sh:59, 127`).
  5. 작업 통로가 멈춤을 만드는지 보려고, 측정 동안 작업 통로 질의 횟수와 측정값의 튐(최대값)을 같이 기록합니다.
- **바꿀 파일/새로 만들 것**: 새 폴더 하나(예: `nwoas_scripts/storage-bench-20261117/`)에 작업 파일(.cmd)과 무작위 시험 도구. 하이퍼바이저·펌웨어는 그대로.
- **실기기 검증**: 결과를 9월 표(1.4절)와 같은 단위로 나란히 놓습니다. IOTEST는 모든 값 일치(`S195 PASS`)여야 숫자를 씁니다.
- **콘치님 손**: 없음. 측정 중에는 맥미니로 다른 일을 하지 않아 주시면 숫자가 깨끗합니다.
- **위험**: Windows C:에 시험 파일(256 MiB 이상)이 남습니다. macOS 쪽은 쓰기 구역 검사가 그대로라 영향 없음 [사실: 1.2절].
- **선행 조건**: 없음.

### 단계 1. 끄기·재시작 때 SSD 제어 칩을 정상 종료

- **할 일**: PSCI 재시작·끄기 두 경로에서 `reboot()` 전에, SSD가 켜져 있고 진행 중 명령이 없으면 `nvme_flush(1)` → `nvme_shutdown()`(큐 삭제, 종료 요청, 펌웨어 재우기, 칩 초기화)을 부릅니다 [사실: 함수는 이미 있음 `HV/nvme.c:483-521, 523-534`]. 진행 중 명령이 있으면 짧게 기다리고, 시간이 넘으면 정리 없이 재부팅하되 로그를 남깁니다. `P/standalone.md:129`가 같은 수정을 이미 적어 두었습니다.
- **바꿀 파일**: 새 판 폴더의 `src/hv_psci.c`(1523-1541 근처). 필요하면 `src/nvme.c`의 `rtkit_sleep`을 Asahi처럼 펌웨어 종료로 바꿀지 검토(별도 판).
- **실기기 검증**: 하이퍼바이저 출력에 `nvme: shutdown done` 줄(`HV/nvme.c:520`)이 재시작마다 찍힘 → 다음 부팅에서 `nvme: initialized` 정상 → 재시작 10회 반복 뒤 IOTEST 통과, `chkdsk C: /scan` 오류 0, 보호 파티션 해시 동일.
- **콘치님 손**: 없음(재시작 스크립트). 재시작 중 멈추면 전원 다시 넣기.
- **위험**: 종료 처리에서 멈추면 재시작이 끝나지 않음 → 전원 필요. 데이터 위험은 지금보다 줄어듦 [가설]. macOS 영향 없음.
- **선행 조건**: 단계 0(비교 기준).

### 단계 2. 하이퍼바이저가 SSD 명령을 기다리지 않게 — 여러 명령 동시에 (D2 앞 절반)

- **할 일**
  1. 실제 SSD에 태그 0~63을 씁니다. 태그마다 명령 칸·변환표 칸·주소 목록 페이지를 따로 둡니다(지금은 주소 목록 1장, 주소 배열 1개를 모두 같이 씀 [사실: `HV/nvme.c:559, 616`]).
  2. 문 두드리기(0x1008)에서 쌓인 명령을 **전부** 꺼냅니다. 각 명령은 지금과 똑같이 "복사 → 검사(종류·LBA·쓰기 구역·주소 목록) → 빈 태그에 넣기" 순서로 하고, 넣자마자 다음 명령으로 넘어가 Windows로 돌아갑니다. 빈 태그가 없으면 줄에 남겨 둡니다.
  3. 완료 거두기: 모든 하이퍼바이저 진입의 끝과 5 kHz 틱에서 실제 SSD 완료 줄을 훑어, 끝난 명령의 변환표 칸을 지우고(TCB 무효화), Windows 완료 줄에 결과를 쓰고 인터럽트 900을 넣습니다. 물리 인터럽트 590을 하이퍼바이저가 직접 받는 것은 그다음 개선입니다.
  4. SSD 펌웨어 메시지 처리(syslog 등)를 틱으로 옮깁니다. 지금은 명령을 기다리는 동안에만 처리합니다 [사실: `HV/nvme.c:240-252`의 `nvme_poll_syslog`].
  5. 플러시 순서: 플러시는 앞서 넣은 쓰기가 모두 끝난 뒤 넣습니다. FUA 쓰기는 그 명령이 끝난 뒤에 따로 플러시를 기다리지 않고 SSD의 FUA 처리에 맡길지, 지금처럼 플러시를 붙일지는 시험으로 정합니다 [가설: ANS가 FUA를 어떻게 처리하는지 모름].
  6. 사라진 완료 대비: 명령마다 시간을 재서, 한도를 넘으면 SSD를 "고장" 상태로 두고 그 태그를 다시 쓰지 않습니다(지금도 실패 뒤 재사용 금지 원칙이 있음 [사실: `HV/hv_vm.c:1552-1558`]). 참조 프로젝트도 "첫 완료가 가끔 사라짐"을 겪었고 주기 검사로 거두는 장치를 넣었습니다 [사실: `E/investigation/ANS_DIRECT_PLAN.md:117-121`, https://github.com/paulsmir/windows-on-m1/commit/15e6d6c28c].
  7. 되돌리는 스위치: 프록시 명령이나 빌드 설정 하나로 지금의 "한 번에 하나" 방식으로 즉시 돌아갈 수 있게 둡니다.
  8. Windows 쪽은 바꾸지 않습니다(같은 가짜 장치, 같은 크기, 같은 stornvme). 처음에는 동시 8개로 시작해 32·64로 늘립니다.
- **바꿀 파일/새로 만들 것**: 새 판 폴더(예: `m1n1-ssd-<판>/`, g254L 복사본)의 `src/nvme.c`(태그별 제출·거두기 함수 새로), `src/hv_vm.c`(1461-1601, 1735-1767), `src/nwoas_s229_adapter.inc`(초기화·플러시 때 진행 중 명령 비우기), 호스트 시험(`port-j274/gpu/hv/tools-hv2/hosttest/`에 SSD 시험 추가: 태그 재사용, 순서, 쓰기 구역 밖 거부, 주소 목록 겹침).
- **실기기 검증**
  - 속도: 단계 0과 같은 시험, 같은 표.
  - 자료: IOTEST 3회 모두 통과, 큰 파일 복사 뒤 SHA256 비교, `chkdsk C: /scan` 오류 0, Windows 디스크 오류 이벤트(51·153) 0건.
  - 안정: 30분 부하(DISKREAD 반복 + CPU 부하)에서 0x133 없음. 9월에 같은 부하에서 0x133이 났던 기록이 있어 꼭 봐야 합니다 [사실: `NWOAS-STATUS-2026-09-10-SESSION.md:13, 46`].
  - 하이퍼바이저 카운터: 동시 진행 최대치, 거부 수, 시간 초과 수.
  - macOS: 보호 파티션 해시 동일, 하이퍼바이저의 쓰기 거부 줄(`HV/nvme.c:566`)이 정상 사용 중 0건.
- **콘치님 손**: 없음(재시작 스크립트). 멈추면 전원. 첫 시험 전에 Windows 안의 중요한 파일을 따로 보관해 두시길 권합니다(Windows 파티션 안 손상 가능성).
- **위험**
  - Windows 파티션 안 자료 손상(완료 순서, 태그 재사용, 주소 목록 페이지를 일찍 다시 쓰는 버그) [가설]. 시험 파일로 먼저 확인합니다.
  - macOS: 검사 코드는 그대로이고 "복사 → 검사 → 넣기" 순서를 지키면 구역 밖 쓰기는 장치에 닿지 않습니다 [가설: 코드 구조상]. 검사보다 먼저 장치에 넣는 실수가 가장 위험하므로 호스트 시험으로 막습니다.
  - 넣은 명령이 끝나기 전에 Windows가 장치를 초기화하면, 늦게 끝난 DMA가 이미 다른 용도로 쓰이는 메모리에 쓸 수 있습니다. 초기화 요청 때는 진행 중 명령이 다 끝날 때까지 기다려야 합니다 [가설].
- **선행 조건**: 단계 0, 단계 1.

### 단계 3. 작업 통로가 CPU 전체를 멈추지 않게

- **할 일**: 단계 0에서 작업 통로 멈춤의 영향이 보이면 다음 중 하나를 고릅니다.
  - (가) Windows 작업 통로 프로그램이 일이 없을 때는 묻는 간격을 늘림(예: 0.5초 → 2~5초, 일이 오면 다시 짧게) [사실: 지금 간격 `worker-s177/nwos.c:142`].
  - (나) NS2 질의에서만 CPU 모으기를 하지 않음(시간 보정 끄기의 영향은 모름 [가설]).
  - (다) 맥북 없는 부팅(단계 8)에서는 NS2를 아예 없앰(`P/standalone.md` 3.4절 3단계).
- **바꿀 파일**: (가) `worker-s177/nwos.c`의 새 판과 작업 통로 모듈의 파일 목록, (나) 새 판 `src/hv_exc.c`·`src/hv_vm.c:1443` 근처.
- **실기기 검증**: 측정값의 최대 튐이 줄어드는지, 작업 통로 응답 시간이 견딜 만한지.
- **콘치님 손**: 없음.
- **위험**: (가) 작업 응답이 느려짐. (나) Windows 시간이 튈 수 있음 [가설]. macOS 영향 없음.
- **선행 조건**: 단계 0. 단계 2와 독립.

### 단계 4. TRIM — Windows 구역 안에서만

- **할 일**
  1. 가짜 장치 정보에 "Dataset Management 지원"을 표시합니다(Identify의 ONCS 칸, 지금 0 [사실: `HV/nwoas_frontend/admin_payload.c:30-37`]).
  2. 하이퍼바이저가 DSM(opcode 9) 명령의 범위 목록을 자기 메모리로 복사한 뒤, **범위 하나하나가 Windows 구역(39944960~59968629) 안에 완전히 들어 있을 때만** 실제 SSD에 보냅니다. 걸치거나 밖인 범위는 버리고, 명령은 성공으로 답합니다. 참조 드라이버가 같은 규칙입니다 [사실: https://github.com/paulsmir/windows-on-m1/blob/ans/bounce-t3/drivers/storage/appleans/common/ans_trim.c (줄 98-120)].
  3. 처음에는 "기록만" 판으로 Windows가 보내는 범위를 세기만 하고 보내지 않습니다. 범위가 모두 구역 안인지 확인한 뒤 보내는 판으로 바꿉니다(참조의 관찰 모드와 같은 순서 [사실: 같은 저장소 `ans_trim.h:9-13`]).
- **바꿀 파일**: 새 판의 `src/nwoas_frontend/admin_payload.c`, `src/hv_vm.c`(명령 종류 검사 1483), `src/nvme.c`(DSM 보내기 함수), 호스트 시험(범위 경계·겹침·개수 한도).
- **실기기 검증**: 기록 판에서 `defrag C: /L`(TRIM 다시 보내기) → 하이퍼바이저 카운터에 구역 밖 범위 0. 보내는 판에서 같은 명령 → IOTEST·파일 해시·chkdsk 통과, 보호 파티션 해시 동일.
- **콘치님 손**: **macOS 전체 백업 결정이 먼저 필요합니다.** TRIM은 자료를 지우는 명령이라 울타리 코드 버그가 곧 macOS 손실입니다.
- **위험**: 이 문서에서 macOS에 가장 위험한 단계입니다. 참조 프로젝트도 TRIM을 처음 켠 뒤 재시작이 멈춘 적이 있고 원인을 다 밝히지 못한 채 한 번 되돌렸습니다 [사실: https://github.com/paulsmir/windows-on-m1/commit/0eb7f09ba9, https://github.com/paulsmir/windows-on-m1/commit/d718d37f2e, 이후 0.4.7로 다시 켬 https://github.com/paulsmir/windows-on-m1/commit/68195c2deb].
- **선행 조건**: 단계 2(같은 제출 경로 위에서), 단계 5a(해시 도구), macOS 백업.

### 단계 5. 보호 장치 점검·강화

- **할 일**
  - 5a. 보호 파티션 읽기 해시 도구: Windows에서 실제 디스크를 **읽기 전용**으로 열어 GPT 블록과 칸 1·2·3·7 구간을 해시합니다. 159 GB 전체는 1.3 GB/s 기준 약 2분 [가설: 계산]. 빠른 판은 각 파티션 앞·뒤 1 MiB만(`P/port/storage.md:151`의 방식).
  - 5b. 하이퍼바이저 DMA 허용 범위에서 기기 설명표 예약 영역, GPU 로컬 예약, 화면 메모리 등 하이퍼바이저가 쥔 곳을 뺍니다. 참조의 검사 함수가 같은 일을 합니다 [사실: `E/m1n1_windows/src/hv_ans_memory.c:68-91, 137-155`].
  - 5c. 디스크 GUID·"EFI PART"·보호 GPT 항목 비교를 C로도 만듭니다(맥북 없는 부팅용) [사실: 지금은 `L/guest_module.py:161, 173-195`에만].
  - 5d. Windows가 macOS 파티션을 "알 수 없는 파티션"으로 보는 문제를 검토합니다. 참조 쪽 기록에 "Windows가 쓸 수 있다고 믿는 볼륨의 쓰기를 거부하면 Windows가 멈춘다"는 교훈이 있습니다 [사실: `P/port/storage.md:169`가 인용]. 선택지: 그대로 두고 쓰기 거부, 또는 하이퍼바이저가 보호 파티션 항목을 숨긴 GPT 모양을 보여 주기 [가설, 큼, Windows 부팅 정보가 바뀔 수 있음]. 지금은 결정만 합니다.
- **바꿀 파일**: 5a 새 Windows 도구, 5b·5c 새 판 `src/nvme.c`·새 C 파일, 5d 문서만.
- **실기기 검증**: 5a 같은 상태에서 두 번 돌려 같은 해시. 5b 정상 사용·IOTEST 중 거부 0건(범위를 너무 좁히면 Windows 디스크 오류가 나므로 꼭 확인). 5c 일부러 틀린 GUID 값으로 빌드한 시험 판이 SSD를 켜지 않는지 호스트 시험.
- **콘치님 손**: 없음.
- **위험**: 5b가 너무 엄격하면 정상 명령 거부 → Windows 디스크 오류. 5a는 읽기만 하므로 위험 낮음.
- **선행 조건**: 5a는 아무 때나(단계 2·4 전에 있는 것이 좋음).

### 단계 6. 절전·최대 절전·충돌 덤프 확인

- **할 일**: (1) Windows 설정을 읽기만 합니다: `powercfg /a`, 빠른 시작 켜짐 여부, 최대 절전 파일 유무. (2) 절전 중 stornvme가 가짜 장치에 보내는 관리 명령(전원 상태 Set Features 등)을 하이퍼바이저 카운터로 기록. (3) 단계 2 뒤, 충돌 덤프가 끝까지 써지는지 수동 버그체크로 시험.
- **바꿀 파일**: (2)에 필요하면 `src/nwoas_frontend/admin_state.c`.
- **실기기 검증**: 절전 3회 후 디스크 오류 0, IOTEST 통과. 버그체크 뒤 다음 부팅에 덤프 파일 생김.
- **콘치님 손**: 빠른 시작·최대 절전을 끄거나, 수동 버그체크용 레지스트리 값을 넣는 것은 Windows 설정 변경이라 승인이 필요합니다. 절전 버튼·키보드 입력이 필요할 수 있습니다.
- **위험**: 최대 절전 파일 쓰기 중 멈추면 다음 부팅이 그 파일로 이어 붙으려 할 수 있어, 확인 전에는 빠른 시작을 끄는 편이 안전합니다 [가설].
- **선행 조건**: 단계 2(덤프 시험), (1)은 아무 때나.

### 단계 7. Apple 전용 Windows SSD 드라이버 (D2 뒤 절반) — 단계 2 측정 뒤 결정

- **할 일**: 참조 프로젝트의 AppleAns(StorPort 드라이버, MIT)를 J274로 옮깁니다. 하이퍼바이저는 SSD 펌웨어(RTKit)와 내장 주소 변환표(NVMMU/TCB)의 주인으로 남고, Windows 드라이버가 명령을 쓰면 하이퍼바이저가 넣는 순간 복사·검사합니다. 참조의 "브로커" 방식이 바로 이 구조이고, 쓰기 구역을 건 채 J313에서 설치된 Windows 부팅까지 됐다는 기록이 있습니다 [사실: `E/investigation/EXPERIMENTS.md:60093-60118`, 코드 `E/m1n1_windows/src/hv_ans_service.c:191-239`, `hv_ans_broker.c:163-189`, `hv_ans_prp.c:5-61`].
  - J274에 맞출 것: 주소 변환 상수(그쪽은 8 GB 배치에 고정 [사실: `E/drivers/storage/appleans/common/ans_direct.h:63-67`]), Windows 인터럽트 번호(우리는 889를 화면, 900을 가짜 NVMe에 씀 [사실: `fw-audio-s256-mu/Platform/MacMini2020Pkg/AcpiTables/J274AppleAgxAbiAdmission.asl.inc:70, 95`, `DSDT.asl:122`]; 그쪽도 처음 889가 GPU와 겹쳐 0x7B를 겪음 [사실: `P/port/storage.md:46`]), Mu의 부팅 넘기기 관문(`E/mu/Silicon/Apple/AppleSiliconPkg/Library/AppleAnsExitBootServicesGateLib/`), 서명(지금 GPU 드라이버에 쓰는 테스트 인증서 [사실: `HANDOFF-2026-11-16-evening.md:115`]), 두 방식 오가기 장치(`ans-dual-boot.ps1`), 한 명령 최대 128 KiB(그쪽 ABI [사실: `E/m1n1_windows/src/hv_ans_broker_abi.h:22`]).
  - 그쪽 11월 7일 이후 고친 것도 함께 가져옵니다: 충돌 덤프 모드, 종료 순서(`SRB_FUNCTION_SHUTDOWN`은 입출력 끝이 아니라 순서 있는 플러시), 사라진 완료 거두기, TRIM 울타리, 정렬 안 된 주소 처리 [사실: GitHub `ans/bounce-t3` 가지 커밋 목록, 4절].
  - **하지 않아도 될 수 있는 이유** [가설]: 단계 2와 이 단계 모두 "명령마다 하이퍼바이저 진입 1~2번"이라 하이퍼바이저 비용이 비슷합니다. 단계 2로 충분히 빨라지면 이 단계의 이득은 가짜 장치 코드를 없애는 정도입니다. 반대로 Microsoft 기본 드라이버(덤프·최대 절전 지원이 검증된)를 잃습니다.
- **바꿀 파일/새로 만들 것**: `port-j274/storage/appleans/`(새, 참조 드라이버 복사 + J274 값), Mu의 ACPI 표(새 SSDT, `APPL0003`), 하이퍼바이저 새 판(브로커 서비스 이식, 기존 가짜 NVMe는 복구 경로로 유지), Windows 쪽 설치·전환 스크립트.
- **실기기 검증**: `P/port/storage.md:147-152`의 P1~P6 사다리 그대로(WinPE에서 장치만 보이기 → Identify → macOS 쪽 읽기만 해서 해시 비교 → 시험 파티션에만 쓰기 → C: 부팅 → 반복·장시간).
- **콘치님 손**: 새 부팅 드라이버 설치(Windows 설정 변경, 승인 필요), macOS 백업, WinPE USB로 부팅하는 단계에서 USB 꽂기.
- **위험**: 부팅 드라이버를 바꾸므로 0x7B(부팅 장치 없음)로 Windows가 안 켜질 수 있습니다. 그쪽 기록에 직접 부팅 한 번 뒤 stornvme가 꺼지는 문제가 있어, 전환 장치를 먼저 깔아야 합니다 [사실: `P/port/storage.md:151`]. 복구 경로는 지금 방식 그대로.
- **선행 조건**: 단계 2·5, 메인 세션 결정.

### 단계 8. 맥북 없는 부팅에서 SSD 준비

- **할 일**: 맥북 Python이 하던 SSD 준비(`nvme_init`, 주소 지도 등록, 메모리 크기 줄이기, GPT·GUID·보호 항목 확인, 가짜 장치 켜기)를 하이퍼바이저 C로 옮깁니다 [사실: `P/standalone.md:98-100`]. NS2 없이 켤 수 있게 바꿉니다(지금은 NS2 크기가 0이면 켜기를 거부 [사실: `HV/nwoas_s229_adapter.inc:112`]). 끄기 정리(단계 1)가 먼저 있어야 합니다(`P/standalone.md:201`).
- **바꿀 파일**: `P/standalone.md` 3.1절 표의 `nwoas_boot.c`·`hv_psci.c`·`nwoas_s229_adapter.inc` 행.
- **실기기 검증**: `P/standalone.md` 3.4절 3·5단계(케이블 뽑고 10분 버티기, C 실행기로 바탕화면).
- **콘치님 손**: 케이블 뽑기·꽂기(물리).
- **위험**: GUID 검사가 C에 없으면 엉뚱한 디스크에 쓸 수 있음 → 5c가 먼저 [사실: `P/standalone.md:201`].
- **선행 조건**: 단계 1, 5c. 맥북 없는 부팅 갈래 전체(M8)의 일부입니다.

### 단계 9. USB 저장장치 — 남은 확인

- **할 일**: (1) 하이퍼바이저 출력에서 `HVLOG: S237 reserved PCIe bridge window` 줄이 지금 펌웨어에서도 찍히는지 확인(로그 읽기만). (2) USB 메모리에 큰 파일을 **쓰고**, 맥북에서 해시 비교(지금까지 읽기만 시험 [사실: HANDOFF 10절]).
- **바꿀 파일**: 작업 파일 하나.
- **실기기 검증**: 해시 일치 3회.
- **콘치님 손**: USB 메모리 꽂기, 시험 뒤 맥북에 꽂아 주기(물리).
- **위험**: USB 메모리 내용만. 내장 SSD·macOS 무관.
- **선행 조건**: 없음.

---

## 4. 참고할 수 있는 기존 코드

| 무엇 | 어디 | 라이선스 | 쓸 곳 |
|---|---|---|---|
| m1n1 SSD 초기화·종료·명령(우리 판) | `HV/nvme.c` | MIT [사실: `HV/nvme.c:1`] | 단계 1·2의 바탕 |
| 우리 가짜 NVMe 장치(C) | `HV/nwoas_frontend/`, `HV/nwoas_s229_adapter.inc` | 우리 코드(머리말 표시 없음, m1n1 트리 안) | 단계 2·4·6 |
| 참조 하이퍼바이저 SSD 브로커(명령마다 검사, 64태그) | `E/m1n1_windows/src/hv_ans_service.c`, `hv_ans_broker.c`, `hv_ans_prp.c`, `hv_ans_memory.c`, `ans_queue.c` | MIT [사실: `E/LICENSE`, `hv_ans_memory.c:1`] | 단계 2(검사 함수·주소 목록 처리 참고), 단계 5b, 단계 7 |
| 참조 SSD 직접 시작·종료(제한 시간 펌웨어 부팅 등) | `E/m1n1_windows/src/nvme.c:309-427`, `hv_ans_platform.c:199-219` | MIT | 단계 1·7 |
| 참조 Windows 드라이버 AppleAns | `E/drivers/storage/appleans/`(우리 사본은 11-07판). 최신은 https://github.com/paulsmir/windows-on-m1/tree/ans/bounce-t3/drivers/storage/appleans | MIT [사실: GitHub API `license: MIT`] | 단계 7, TRIM 울타리(`common/ans_trim.c`)는 단계 4 참고 |
| 참조 Mu 부팅 넘기기 관문 | `E/mu/Silicon/Apple/AppleSiliconPkg/Library/AppleAnsExitBootServicesGateLib/` | BSD-2-Clause-Patent [사실: 같은 폴더 `AppleAnsInstalledExitBootServicesGateLib.c:3`] | 단계 7 |
| Asahi Linux 애플 SSD 드라이버 | https://github.com/torvalds/linux/blob/master/drivers/nvme/host/apple.c | GPL-2.0 [사실: 파일 1줄] | **동작만 읽고 코드는 복사하지 않습니다**(우리 m1n1·Windows 코드와 라이선스가 다름). 태그 64개, 종료·잠들기 순서, NVMMU 설명 |
| 9월 측정 도구 | `nwoas_scripts/storage-s195/`(IOTEST), `cpufreq-s140/`·DISKREAD | 우리 코드 | 단계 0 |
| 9월 지연 카운터·질의 | `HV/hv_vm.c:1374-1391, 1633-1643`, `nwoas_scripts/target-query-s163/`, `native-s230/query_module.py` | 우리 코드 | 단계 0·2 |
| Microsoft DiskSpd | https://github.com/microsoft/diskspd | MIT [가설: 이번에 저장소를 열어 확인하지 않음] | 단계 0 무작위 시험(직접 만드는 도구 대신) |

우리 사본과 최신 참조의 차이 [사실: GitHub API로 `main...ans/bounce-t3` 비교, 250개 이상 커밋 앞섬]: 충돌 덤프 모드(https://github.com/paulsmir/windows-on-m1/commit/31e0d4147d), 종료 처리(https://github.com/paulsmir/windows-on-m1/commit/1f7129fde5), TRIM과 울타리(https://github.com/paulsmir/windows-on-m1/commit/f9f03b9a7d, https://github.com/paulsmir/windows-on-m1/commit/a85a64e246), 사라진 완료 거두기(https://github.com/paulsmir/windows-on-m1/commit/15e6d6c28c), 정렬 안 된 주소 우회 버퍼(https://github.com/paulsmir/windows-on-m1/commit/e053a11ad6). 단계 7을 하게 되면 사본을 이 가지로 새로 받아야 합니다.

---

## 5. 모르는 점과 먼저 확인할 것

| 번호 | 모르는 점 | 왜 중요한가 | 언제 알 수 있나 |
|---|---|---|---|
| U1 | 지금 구성의 실제 SSD 속도, 특히 4 KiB 무작위·동시 명령 | 계획 전체의 우선순위가 이것으로 정해짐 | 단계 0 |
| U2 | 체감 느림이 SSD 때문인지(CPU·GPU·작업 통로 멈춤일 수도) | SSD를 고쳐도 체감이 안 바뀔 수 있음 | 단계 0의 튐 기록 |
| U3 | Windows가 실제로 한 번에 몇 개 명령을 쌓는지 | 동시 처리 이득의 크기 | 단계 0 카운터(질의 7) |
| U4 | 지금 SSD 펌웨어(13.5 스텁으로 켠 상태)가 여러 태그 동시 처리를 문제없이 하는지 | Asahi·참조는 다른 펌웨어 판에서 확인 | 단계 2 첫 시험 |
| U5 | 사라진 완료 신호가 우리에게도 생기는지 | 생기면 멈춤 → 시간 초과 처리 필요 | 단계 2 장시간 시험 |
| U6 | ANS가 FUA·플러시를 어떻게 처리하는지 | 쓰기 성능과 전원 차단 때 자료 안전 | 단계 2 |
| U7 | 정상 종료 없이 꺼도 자료가 안전했는지 | 지금까지 손상 기록은 찾지 못함. 그래도 단계 1은 값이 싸므로 함 | 단계 1 |
| U8 | 하이퍼바이저가 물리 인터럽트 590을 직접 받을 수 있는지 | 완료를 틱 없이 바로 거두려면 필요 | 단계 2 뒤 개선 |
| U9 | TRIM이 이 맥미니 SSD에서 실제로 되는지 | 참조 J313에서는 됨(그쪽 기록 https://github.com/paulsmir/windows-on-m1/commit/42dbf34284) | 단계 4 기록 판 |
| U10 | 절전·최대 절전 때 stornvme가 무엇을 보내는지, 빠른 시작이 켜져 있는지 | 깨어날 때 디스크 오류 위험 | 단계 6 |
| U11 | 쓰기를 거부당한 Windows가 멈추는 경우가 실제로 있는지(5d) | macOS 파티션 숨기기의 필요성 | 단계 5d 검토 |
| U12 | UEFI 단계도 하이퍼바이저 빠른 처리부를 쓰는지 | 단계 2가 UEFI(인터럽트 없이 도는)에서도 맞아야 함 | 단계 2 첫 부팅 로그 |
| U13 | macOS를 다시 켤 수 있는지 | 지금 Macintosh HD에 D4가 있어 macOS가 안 켜지는 상태로 기록됨 → "macOS가 멀쩡한지"를 macOS로 확인할 수 없음. 그래서 단계 5a 해시가 유일한 확인 수단 | `HANDOFF-2026-11-16-evening.md:365`, 메모리 `nwoas-state-2026-11-15.md` |

먼저 확인할 것(순서): U1·U2·U3(단계 0 한 번에) → U12(단계 2 설계 전에 지금 로그로) → U13(콘치님께 macOS 백업 여부 확인, 단계 4·7 전).

---

## 6. 대략 크기

| 단계 | 크기 | 주 작업 |
|---|---|---|
| 0. 속도 재기 | 작음 | 작업 파일, 작은 측정 도구 |
| 1. 끄기 정리 | 작음 | `hv_psci.c` 두 함수 |
| 2. 기다리지 않는 SSD 경로 | 중간~큼 | 하이퍼바이저 핵심부, 호스트 시험, 장시간 시험 |
| 3. 작업 통로 멈춤 줄이기 | 작음 | 작업 통로 프로그램 간격 또는 하이퍼바이저 한 곳 |
| 4. TRIM 울타리 | 중간 | 장치 정보, 범위 검사, 관찰 판 → 보내는 판 |
| 5. 보호 강화 | 중간 | 해시 도구, DMA 범위, C GUID 검사 (5d 검토만) |
| 6. 절전·덤프 확인 | 작음~중간 | 대부분 확인, 필요 시 관리 명령 응답 |
| 7. Apple 전용 드라이버 | 큼 | 드라이버 이식·ACPI·Mu·하이퍼바이저 브로커·서명·전환 |
| 8. 맥북 없는 SSD 준비 | 중간 | 맥북 없는 부팅 갈래의 일부 |
| 9. USB 남은 확인 | 작음 | 로그 확인, 쓰기 시험 |

---

## 7. 출처 (이번에 직접 연 것)

로컬(`/Users/angigyeom/Desktop/DEV/NWOAS/` 아래):
- `HANDOFF-2026-11-16-evening.md`(§0~2, §3.3, §3.8, §4~6)
- `nwoas_scripts/plan-20261115/DECISIONS.md`, `standalone.md`, `usb.md`, `port/storage.md`
- `port-j274/GAP-LIST.md`, `port-j274/README.md`, `port-j274/gpu/hv/README-dcp-s251.md`, `port-j274/gpu/hv/tools-hv2/buildvar.sh`
- `nwoas_scripts/ANS2-NEXT-2026-09-07.md`, `nwoas_scripts/S99-UEFI-NVME-PLAN.md`
- `nwoas_scripts/storage-s195/README.md`, `hardware-s193-gap50.json`; `nwoas_scripts/nvme-s163/README.md`, `active-50-result.json`; `nwoas_scripts/nvme-s1*/README.md` 첫 줄들; `nwoas_scripts/claude-s156/nvme-watchdog-review.md`
- `nwoas_scripts/layout-s231/USB-READ-CORRUPTION-20261115.md`, `guest_module.py`, `module.py`; `nwoas_scripts/usb-s232/HANDOFF-USB-A-CORRUPTION.md` 6~10절; `nwoas_scripts/usb-s236/README.md`
- `nwoas_scripts/agxdcp-s256-guest-test.sh`, `nwoas_scripts/worker-s177/nwos.c`, `nwoas_scripts/memory-s180/link_module.py`
- `NWOAS-STATUS-2026-09-06.md`, `-09-07-SESSION.md`, `-09-09-SESSION.md`, `-09-10-SESSION.md`의 해당 줄
- `research-20261116/RESULT-20261116-morning.md:50`
- `m1n1-gpu-hv2/src/`: `nvme.c`, `hv_vm.c`(1195-1767), `nwoas_s229_adapter.inc`, `nwoas_frontend/{model.c,admin_state.c,admin_payload.c}`, `hv_psci.c`(1505-1700), `hv.c`(24, 215-255, 470-496), `hv_exc.c`(67, 81-170, 452-505), `mcc.c`(179-216), `utils.c`(129-133)
- `fw-audio-s256-mu/` 의 `MacMini2020.fdf`, `DSDT.asl`, `J274AppleAgxAbiAdmission.asl.inc`, `NwoasHideHighRamDxe.c`, `MemoryInitPeiLib.c`
- `external/windows-on-m1/`: `LICENSE`, `documentation/LIMITATIONS.md`, `investigation/ANS_DIRECT_PLAN.md`, `ANS_CURRENT_STATE.md`, `EXPERIMENTS.md`(59790-60562 일부), `m1n1_windows/src/{hv_ans_memory.c,hv_ans_prp.c,hv_ans_broker.c,hv_ans_service.c,hv_ans_broker_abi.h}`, `drivers/storage/appleans/common/ans_direct.h`, `mu/.../AppleAnsExitBootServicesGateLib/`
- `nwoas_scripts/ans2-s87/run-20261115-180239/lba-2.bin`(GPT 해석만)

웹(공개 저장소, 읽기만):
- https://github.com/paulsmir/windows-on-m1 (저장소 정보·가지·커밋 목록, `ans/bounce-t3`의 `drivers/storage/appleans/common/ans_trim.{c,h}`, `windows/NATIVE_PATH.md`, `windows/direct.c`, `windows/driver.c`)
- https://github.com/torvalds/linux/blob/master/drivers/nvme/host/apple.c
