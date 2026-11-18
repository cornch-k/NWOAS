# 주변장치·포트 로드맵 (GPU·SSD·와이파이·소리 드라이버 제외) — 2026-11-17

> 읽기와 계획만 했습니다. 아무것도 빌드·실행하지 않았고, 시리얼·USB·맥미니는 건드리지 않았습니다.
> **빌드 성공은 부팅 성공이 아니고, 이 문서의 계획은 한 줄도 실기기에서 시험되지 않았습니다.**
> 표기: **[사실]** = 파일:줄, 로그, ADT 덤프, URL로 확인. **[가설]** = 확인하지 않은 추측.
> 경로는 `/Users/angigyeom/Desktop/DEV/NWOAS/` 기준입니다. 자주 쓰는 줄임:
> - `HOU` = `nwoas_scripts/usb-s232/HANDOFF-USB-A-CORRUPTION.md` (USB-A 손상 추적 기록)
> - `DSDT` = `fw-audio-s256-mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl` (지금 쓰는 S256 펌웨어의 ACPI 표)
> - `ADT` = `nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin` (11-16 맥미니에서 읽은 하드웨어 목록). 읽을 때 scratchpad의 `adtdump.py`를 썼습니다.
> - `DTS` = `nwoas_scripts/dtb-s221/recovered-j274.dts`
> - `WOM` = `external/windows-on-m1` (paulsmir, MIT, 커밋 6c65673, 2026-11-07)
> - `HV` = `m1n1-gpu-hv2/src` (지금 쓰는 하이퍼바이저 소스, g254L)

---

## 0. 한눈에

| 장치 | 지금 상태 | 목표 | 크기 | 순위 |
|---|---|---|---|---|
| USB-A (FL1100) | 키보드·마우스·USB 메모리 됨. 읽기 손상은 원인 찾고 펌웨어에서 고침(6/6 통과) | 꽂았다 뽑기·쓰기·절전까지 믿고 쓰기 | 작음~중간 | 1 |
| 모든 PCIe 장치가 함께 쓰는 기반 | USB-A 한 장치에 맞춘 개별 처리. 링크 올리기·주소 창은 맥북 Python이 함 | 이더넷·와이파이를 붙일 수 있는 공통 틀, 맥북 없이도 동작 | 중간 | 2 |
| UEFI 단계 키보드·마우스 (맥북 없이) | UEFI가 FL1100을 붙잡는 기록은 있음. 맥북 없이는 PCIe가 안 올라옴 | 맥북 없이 부팅 메뉴·복구 화면 조작 | 중간 | 3 |
| 내장 유선 이더넷 (BCM57762) | UEFI가 장치를 찾음. Windows에는 숨김. ARM64 드라이버 못 찾음 | Windows에서 1 Gbps 유선 연결 | 큼 | 4 |
| 전원 버튼 | 누르면 Windows가 모름(연결 경로 없음) | 짧게 누르면 Windows 종료/절전 | 중간 | 5 |
| USB-C (2개, Thunderbolt 포트) | 하나는 맥북 연결용. 다른 하나는 Windows에 보이지만 9월 이후 장치 시험 없음, USB 2.0 속도 | 두 포트 모두 USB 장치용, 나중에 USB 3 | 큼 (Thunderbolt는 매우 큼) | 6 |
| 앞 상태 LED | 무엇이 켜고 끄는지 모름 | 켜짐/꺼짐 표시 | 작음(모르는 것이 많음) | 7 |
| HDMI 소리·이어폰 단자·내장 마이크 | **막힘(안전장치)** | — | — | — |
| 와이파이·블루투스 | **막힘(안전장치)** | — | — | — |

---

## 1. USB-A (Fresco FL1100)

### 1.1 지금 상태

| 항목 | 내용 | 근거 |
|---|---|---|
| 칩 | Fresco Logic FL1100, PCIe 포트 1 뒤(버스 2) | [사실] ADT `/arm-io/apcie/pci-bridge1/pcie-xhci` compatible `usb-xhci,FL1100`; `logs/retest40-22h2.run14.log:2089` `VID 0x1B73 DID 0x1100` |
| 연결 방식 | PCIe 버스로 보이지 않고, ACPI 일반 장치 `SCB0/XHC0`(PNP0D10)로 보임. 인터럽트 698(포트 1 묶음 줄), `_INI`에서 메모리·DMA를 켬 | [사실] `DSDT:345-425`, `:414`, `:425` |
| 64비트 DMA | 하이퍼바이저가 AC64 비트를 가려 Windows가 32비트 주소만 씀 | [사실] `HV/hv_vm.c:102` `NWOAS_FORCE_FL32 1`; `HOU:23-25` |
| 키보드·마우스 | 됨 | [사실] 작업 지시의 현재 상태, `HOU:238-239` (3434:D030 수신기) |
| 큰 파일 읽기 손상 | **원인 확정**: 게스트 RAM 0xC0000000~0xC00FFFFF가 FL1100 위 PCIe 다리(브리지)의 주소 창과 겹쳐, 그 주소로 가는 DMA가 RAM에 닿지 않음 | [사실] `HOU:193-202`, 브리지 창 측정값 `HOU:245-250` |
| 해결 1 (Windows 설정) | BCD `badmemorylist`로 그 1 MiB 사용 금지 → 6/6 통과 | [사실] `HOU:212-234` |
| 해결 2 (펌웨어, 지금 쓰는 것) | UEFI가 그 1 MiB를 메모리 지도에서 뺌(S237). 실기기 6/6 통과 후 BCD 항목 제거 | [사실] `nwoas_scripts/fw-s237/README.md:1-25`, `HANDOFF-2026-11-16.md:76` 커밋 `4419ea5`; 지금 부팅 기록 `logs/agxmed1g-s249-20261116-202802.1sXX3e.vuart:17` `S237 reserved PCIe bridge window base=C0000000 bytes=100000` |
| S232 (No Snoop·Relaxed Ordering 끄기) | **빌드하고 실기기에서 시험함.** DEVCTL 0x2810→0x2000 확인. 두 번째 파일 손상 그대로 → 원인 아님 | [사실] `HOU:63-65`, `HOU:75` |
| S233b (전송 고리 전체 캐시 정리) | 시험함, 손상 그대로 → 원인 아님 | [사실] `HOU:67`, `:75` |
| S234 (캐시 명령 가로채기) | 시험함(2/3 손상) → 원인 아님, 실험용 | [사실] `HOU:111-136` |
| S235 (관측 전용) | 원인 위치를 찾아낸 시험 | [사실] `HOU:138-210` |
| 지금 하이퍼바이저에 남은 USB-A 처리 | S233 계열 캐시 정리 코드가 그대로 들어 있음 | [사실] `HV/hv_vm.c:3046` "S233: counters for every FL1100 cache-maintenance path" |
| 꺼 둔 Windows 설정 | USB3 절전(LPM)과 선택적 절전이 전원 계획에서 꺼져 있음 | [사실] `HOU:235-236` |

측정값

| 시험 | 값 | 근거 |
|---|---|---|
| robocopy /J USB→SSD 속도 (MB/s) | 133 | [사실] `HOU:55`, `nwoas_scripts/plan-20261115/usb.md:16` |
| PCIe 포트 1 최대 링크 속도 (세대) | 2 (ADT 값) | [사실] ADT `pci-bridge1/maximum-link-speed = 2` |
| 고친 뒤 확인 (회차 / 손상) | 6 / 0 (BCD 판), 6 / 0 (S237 판) | [사실] `HOU:221-228`, 커밋 `4419ea5` |
| 고치기 전 (회차 / 손상) | 9 / 9 (S232·S233b), 14 / 11 (S234·S235) | [사실] `HOU:232-233` |

- [가설] PCIe 2세대 1차선의 이론 상한은 약 500 MB/s이고, FL1100은 USB 3.0(5 Gb/s) 컨트롤러이므로 133 MB/s는 상한보다 낮습니다. 무엇이 묶는지(하이퍼바이저의 문 두드리기(doorbell)마다 하는 캐시 정리, USB 메모리 자체 속도, 32비트 DMA로 생기는 중간 복사)는 재 보지 않았습니다.
- [사실] 9월에 본 "같은 파일 앞쪽 페이지가 1 MiB 간격으로 들어오는" 손상은 이번 원인으로 설명되지 않았고 다시 나오지도 않았습니다(`HOU:279-280`).
- [사실] USB-A 꽂았다 뽑기는 한 번도 시험 기록이 없습니다. 9월 기록은 "hotplug 주장 없음"이라고 적었습니다(`NWOAS-STATUS-2026-09-10-SESSION.md:399`, `:496`).
- [사실] USB 메모리 쪽으로 **쓰기**(bulk OUT)는 시험하지 않았습니다(`HOU:269-270`).

### 1.2 목표

1. 큰 파일 읽기·쓰기, 꽂았다 뽑기, 허브, Windows 재시작을 반복해도 손상·멈춤이 없습니다.
2. 다른 PCIe 장치를 켜서 주소 창이 옮겨 가도 손상이 다시 생기지 않습니다.
3. 필요 없어진 실험 코드를 빼서 하이퍼바이저를 가볍게 합니다(속도도 함께 재 봄).

### 1.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 쓰기 손상 시험: SSD의 큰 파일을 USB 메모리로 복사하고 맥북에서 해시 비교 | 새 작업 파일 `nwoas_scripts/usb-s2xx/W1-write.cmd` (기존 `U3-fresh-copy.cmd` 본뜸) | 3회 모두 해시 일치 | USB 메모리를 맥북에 옮겨 꽂기 1번 | 낮음 (메모리 내용만 바뀜) | 없음 |
| 2 | 꽂았다 뽑기 시험: 키보드 수신기·USB 메모리를 Windows 실행 중 뽑고 다시 꽂기, 허브 경유 포함 | 새 작업 파일(장치 목록·이벤트 로그 수집) | 장치 관리자 다시 나타남, `USBXHCI` 오류 이벤트 0, 하이퍼바이저 기록의 PORTSC 변화 | **필요** (손으로 뽑고 꽂기, 5~10번) | 중간: Windows가 컨트롤러를 리셋하면 HV 우회 경로(인터럽트 698 주입)가 꼬일 수 있음 [가설] | 없음 |
| 3 | USB3 절전(LPM)·선택적 절전 다시 켜고 U3 시험 3회 | Windows 전원 계획 작업 파일(되돌리는 작업도 함께) | 손상 0, 장치 사라짐 0 | 없음 (Windows 설정 변경이라 승인만) | 낮음, 되돌릴 수 있음. 예전에 "드라이브 E:가 사라짐" 1번 있었음 [사실] `HOU:64` | 1단계 |
| 4 | 주소 창 예약을 1 MiB에서 **PCIe 32비트 창 전체**(0xC0000000~0xFFFFFFFF)로 넓힘. 8장 1단계와 같은 일 | `Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c`(S237 자리), 펌웨어 재빌드 | U3 3회 + truncatememory 3회 모두 정상(`HOU:105-107` 기준), Windows 메모리 줄어든 양 기록 | 없음 | 중간: 32비트 DMA가 쓸 수 있는 RAM이 4 GiB→3 GiB로 줄어 중간 복사 자리가 옮겨 감 [가설] | 8장 1단계 |
| 5 | 실험 코드 정리: S233b 전송 고리 전체 정리·S232 DEVCTL 처리를 끈 판과 켠 판을 같은 시험으로 비교. 꺼도 정상이면 끔 | `HV/hv_vm.c` (FL1100 정리 경로 ~3020-3340) | U3 6회 정상, robocopy 속도 비교 표 | 없음 | 중간: 7월에 이 정리 없이 이벤트를 못 받던 기록이 있음 [사실] `HV/hv_vm.c:47-58` 주석. 그때는 원인이 달랐을 수 있음 [가설] | 4단계 |
| 6 | 장시간 시험: 큰 파일 여러 개를 1시간 반복 복사 | 작업 파일 | 손상 0, 장치 리셋 0 | 없음 | 낮음 | 4·5단계 |

### 1.4 재사용 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| U3 시험·자동 반복 스크립트 | `nwoas_scripts/usb-s232/U3-fresh-copy.cmd`, `run-u3-cycles2.sh`, `usb-s236/validate.sh` | 우리 코드 |
| 관측용 코드(LBA별 해시 비교) | `m1n1-usb-s235*/src/nwoas_s235_probe.inc` | m1n1 바탕, MIT |
| UEFI 메모리 예약 | `nwoas_scripts/fw-s237/` (`ReserveMemoryRegion`) | Project Mu/edk2 바탕, BSD-2-Clause-Patent |

### 1.5 모르는 점

- [가설] 133 MB/s를 묶는 것이 무엇인지 모릅니다.
- [가설] 9월의 다른 모양 손상이 지금도 숨어 있는지 모릅니다.
- [사실] 사람이 키보드·트랙패드를 오래 써 본 결과는 기록이 없습니다(`HOU:239` "not checked by a person").

### 1.6 크기: 작음~중간 (1~3단계 작음, 4·5단계 중간)

---

## 2. USB-C / Thunderbolt 포트 (뒷면 2개)

### 2.1 지금 상태

맥미니 USB-C 두 포트는 각각 DWC3 USB 컨트롤러 + Apple Type-C PHY(ATC) + 포트 관리 칩 CD3217(I2C) + Thunderbolt 블록(ACIO)으로 이루어져 있습니다.

| 항목 | 포트 0 (`usb-drd0`) | 포트 1 (`usb-drd1`) | 근거 |
|---|---|---|---|
| xHCI 주소 | 0x382280000 | 0x502280000 | [사실] ADT `usb-drd0/1` reg |
| 인터럽트(AIC) | 777~780, 736 | 857~860, 816 | [사실] ADT `usb-drd0/1` interrupts |
| 포트 관리 칩 | `hpm0` CD3217, I2C 주소 0x38 | `hpm1` CD3217, I2C 주소 0x3f | [사실] ADT `/arm-io/i2c0/hpmBusManager/hpm0,1`; 0x3f는 `NWOAS-STATUS-2026-09-06.md:724` |
| Thunderbolt 블록 | `acio0` | `acio1` | [사실] ADT |
| 지금 쓰임 | **맥북 연결 케이블**(하이퍼바이저 USB 통로). "뒷면 USB-C 중 전원선에 가까운 쪽" | Windows에 `XHC1`(PNP0D15, 인터럽트 857)로 보임 | [사실] `HANDOFF-2026-11-16.md:140`; `DSDT:136-166` |

포트 1 기록 (9월)

| 시험 | 결과 | 근거 |
|---|---|---|
| D67 PHY 재설정만 | 연결 감지 안 됨 | [사실] `NWOAS-STATUS-2026-09-06.md:718` |
| D69 CD3217에 전원 상태 0 명령(SSPS) | 연결 감지됨 | [사실] 같은 문서 `:725` |
| D70 | USB-A 키보드까지 멈춤 (회귀) | [사실] `:728` |
| D71 C 코드로 HPM 자동 초기화 | 연결 감지, 입력은 안 됨 | [사실] `:736` |
| D72 Windows가 가린 857을 존중 | USB-A 키보드 회귀 해소 | [사실] `:738` |
| D76~D78 | 게스트 낮은 주소를 쓰는 DMA가 엉뚱한 실제 주소로 감 / DART 주소 간격 버그(0x8000→0x80000) | [사실] `:745-750` |
| D83 낮은 주소 전송 버퍼를 하이퍼바이저가 고쳐 씀 | **Magic Trackpad 커서 정상 동작** (콘치님 확인) | [사실] `:760-764` |
| 같은 세션 중 | 트랙패드가 도중에 멈춤, USB-A 키보드는 계속 됨 | [사실] `:922` |
| S175 (고쳐 쓰기 대신 DART 주소 변환) | 설정만 성공, 그때 두 입력 장치가 모두 USB-A에 꽂혀 있어 USB-C 전송은 확인 안 됨 | [사실] `NWOAS-STATUS-2026-09-10-SESSION.md:285`, `nwoas_scripts/usb-s175/README.md:3,104` |
| 9월 10일 이후 | USB-C에 장치를 꽂은 시험 기록 없음 | [사실] 검색 결과 없음 |

- [사실] 하이퍼바이저에는 "857 인터럽트 줄이 실제로 울리지 않아 xHCI 상태를 보고 대신 넣어 주는" 우회 코드가 남아 있습니다(`HV/hv_exc.c:414-430`). 주석은 Asahi `dwc3-apple.c`의 같은 증상 설명을 근거로 듭니다.
- [사실] windows-on-m1(J313 맥북 에어)도 같은 구조입니다. 맥북 연결용 USB 컨트롤러는 m1n1이 갖고, 다른 컨트롤러를 Windows에 넘깁니다(`WOM/documentation/ARCHITECTURE.md:61-70`). 그쪽 UEFI 드라이버는 iBoot가 해 둔 상태 그대로 **USB 2.0 속도**로만 씁니다(`WOM/mu/Silicon/Apple/AppleSiliconPkg/Drivers/AppleUsbTypeCBringupDxe/AppleUsbTypeCBringupDxe.c:47-50`).
- [사실] 같은 UEFI 드라이버가 우리 펌웨어에도 들어 있습니다(`fw-audio-s256-mu/Platform/MacMini2020Pkg/MacMini2020.fdf:193`).
- [사실] `NWOAS_USBA_NEXT_STEPS.md:2`의 "USB-C 길은 Apple 설계상 막혔다"는 9월 D69~D83 결과로 맞지 않게 되었습니다. 같은 문서의 "USB-A는 ASMedia ASM2214A" 역시 ADT(`usb-xhci,FL1100`)와 다릅니다.

### 2.2 Windows가 USB-C를 쓰려면 필요한 것

| 층 | 하는 일 | 지금 누가 하나 | 근거 |
|---|---|---|---|
| 포트 관리 칩 CD3217 | 전원 상태를 켜서 꽂힘을 감지, 방향·역할(호스트) 결정 | 하이퍼바이저 C(`HV/usb.c` `hpm_init`, `tps6598x.c`) | [사실] D71 기록, `HV/usb.c:234` |
| ATC PHY | USB 2.0만: iBoot 상태 그대로. USB 3: 퓨즈 값으로 만든 조정값이 필요 | iBoot / 없음 | [사실] `AppleUsbTypeCBringupDxe.c:47-50` |
| DWC3 컨트롤러 | xHCI 호스트 모드로 두기 | UEFI `AppleUsbTypeCBringupDxe` | [사실] 위 FDF |
| DART(주소 변환) | 우회 또는 게스트 주소 그대로 매핑 | 하이퍼바이저 + 맥북 Python(`usb-s174/guest_map.py:18-19`) | [사실] |
| 인터럽트 | 줄이 안 울려 하이퍼바이저가 대신 넣음 | `HV/hv_exc.c:414-` | [사실] |
| Windows 드라이버 | 내장 `USBXHCI` | Windows | [사실] `WOM/documentation/ARCHITECTURE.md:12` |
| 꽂았다 뽑기 | 꽂을 때마다 CD3217 인터럽트 → PHY 다시 설정 | 없음 | [사실] Asahi 참고 기록 `nwoas_scripts/usb-s165/ASAHI-REFERENCE-20260910.md`(9월 저장), `NWOAS-STATUS-2026-09-10-SESSION.md:220` |
| Thunderbolt/USB4 | ACIO 펌웨어·터널 | 없음. Linux에서도 아직 검토 중인 패치 | [사실] https://www.phoronix.com/news/Linux-USB4-Apple-Silicon |

- [사실] Linux 쪽은 M1 USB 3 지원이 6.19에 들어갔다는 보도가 있습니다(https://scribe.disroot.org/comment/8241087 — Phoronix 기사 인용, 2차 출처). Thunderbolt는 기본 터널만 패치 검토 중이고 PCIe 터널은 없습니다(위 Phoronix 링크).

**맥북 케이블이 필요 없어지는 때**: 포트 0은 하이퍼바이저가 맥북과 이야기하는 통로입니다. 게스트를 띄우는 순서 전체가 맥북 Python에 있기 때문입니다([사실] `nwoas_scripts/plan-20261115/standalone.md:13`). 그 일을 C로 옮기면(단독 부팅) 포트 0을 Windows에 넘길 수 있습니다. windows-on-m1은 단독 부팅 때 "3초 동안 맥북이 있나 보고 없으면 혼자 간다"는 방식을 씁니다([사실] `port-j274/GAP-LIST.md:47` R-9). 결정 D6에 따라 개발 중에는 맥북 연결을 유지하고 마지막에 단독 부팅을 완성합니다([사실] `nwoas_scripts/plan-20261115/DECISIONS.md:10`).

### 2.3 목표

1. 포트 1에 USB 장치(키보드·메모리·허브)를 꽂아 오래 써도 멈추지 않습니다(USB 2.0 속도).
2. 단독 부팅 때 포트 0도 같은 방식으로 Windows에 넘깁니다.
3. 그 다음 USB 3 속도. Thunderbolt는 이 로드맵 범위 밖(보류)으로 둡니다.

### 2.4 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 지금 하이퍼바이저(g254L)로 포트 1에 USB 메모리·키보드를 꽂고 Windows가 보는지만 확인 (코드 변경 없음) | 없음. 장치 목록 작업 파일 | 장치 관리자에서 부모가 `XHC1`인 장치가 나옴(9월 S187 방식) | **필요** (꽂기) | 중간: 9월 D70처럼 USB-A 키보드까지 멈출 수 있음 [사실] → USB-A 키보드 반응을 같이 봄 | 없음 |
| 2 | 1단계가 안 되면: 9월 D83 판과 지금 판의 USB-C 처리 차이를 표로 만들고, S175 DART 변환 방식을 지금 판에 다시 붙임 | `HV/hv_exc.c`, `HV/hv_vm.c` USB-C 구간, `usb-s174/guest_map.py` | 커서 움직임, USB 메모리 해시 비교 | 필요 (확인) | 중간 | 1단계 |
| 3 | 전송 장시간 시험 (D83 때 도중 멈춤 원인 찾기) | 관측 코드 | 1시간 입력·복사, 멈춤 0 | 필요 (입력 확인) | 중간 | 2단계 |
| 4 | 꽂았다 뽑기: CD3217 인터럽트 처리와 PHY 다시 설정을 C로 | `HV/usb.c`, `HV/tps6598x.c`, Asahi 순서 참고 | 5회 뽑고 꽂기 모두 재인식 | 필요 | 큼: PHY 순서를 틀리면 포트가 안 살아남 [가설] | 3단계 |
| 5 | 단독 부팅 때 포트 0을 Windows에 넘김: 두 번째 xHCI ACPI 장치(`XHC2`, 인터럽트 777) 추가, 맥북 없을 때만 켬 | `DSDT.asl`, 단독 부팅 C 실행기 | 맥북 없이 포트 0에 키보드 → 입력 | 필요 | 중간 | 단독 부팅(M8) |
| 6 | USB 3 속도: ATC PHY 조정값 적용 | 새 C/UEFI 코드, Asahi atcphy 참고 | 메모리 복사 속도가 USB 2.0 상한을 넘음 | 필요 | 큼 | 4단계 |
| — | Thunderbolt/USB4 | — | — | — | Linux도 미완 | 보류 |

### 2.5 재사용 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| CD3217(TPS6598x 계열) 제어 | `HV/tps6598x.c`, `HV/usb.c` (m1n1) | MIT |
| DWC3 UEFI 초기화 | `AppleUsbTypeCBringupDxe.c` | **"(BSD-2-Clause-Patent OR MIT) AND GPL-2.0"** — GPL 부분이 섞여 있다고 머리말에 적혀 있음 [사실] 같은 파일 14-15행 |
| 9월 USB-C 처리 | `m1n1-diag-s208` 등 옛 트리, `nwoas_scripts/usb-s175/`, `usb-s182/` | 우리 코드(m1n1 바탕 MIT) |
| Asahi atcphy·dwc3-apple | Linux 커널 | GPL-2.0 계열 → 참고만, 그대로 옮기지 않음 [가설: 파일별 라이선스는 확인 안 함] |

### 2.6 모르는 점

- [가설] 지금 하이퍼바이저에서 9월 D83의 "전송 버퍼 고쳐 쓰기"가 살아 있는지 확인하지 않았습니다. `hv_exc.c`에 USB-C 관측·인터럽트 우회는 있습니다(`:433-624`).
- [가설] 포트 0을 Windows에 넘길 때 맥북 연결 쪽 ADT 노드 삭제(G-2c)가 필요한지 모릅니다(`port-j274/GAP-LIST.md:20`).
- [가설] USB 3 조정값을 퓨즈에서 읽는 방법을 이 맥미니에서 확인하지 않았습니다.

### 2.7 크기: 큼 (1단계 작음, 2~5단계 중간~큼, USB 3 큼, Thunderbolt 매우 큼)

---

## 3. 내장 유선 이더넷 (Broadcom BCM57762)

### 3.1 칩 확인

| 근거 | 내용 |
|---|---|
| UEFI PCIe 열거 기록 | `PciBus: Discovered PCI @ [03|00|00] [VID = 0x14E4, DID = 0x1682]` [사실] `nwoas_scripts/logs/retest40-22h2.run14.log:2096` (logs 안 126개 파일에 같은 줄) |
| ADT | `/arm-io/apcie/pci-bridge2/lan-1gb`, `device_type = lan0`, `enet_config = 1`, MAC 주소 들어 있음, compatible 없음 [사실] ADT |
| 장치 트리 | `pci@2,0/ethernet@0,0`, 별칭 `ethernet0` [사실] `DTS:22`, `:3260-3268` |
| 0x1682 = BCM57762 | [사실] FreeBSD `bge` 지원 기록 https://lists.freebsd.org/pipermail/freebsd-amd64/2013-November/015680.html, `nwoas_scripts/plan-20261115/network.md:45` |

결론: **[사실] J274 내장 유선 랜은 PCIe 포트 2(버스 3)의 BCM57762입니다.** ADT에는 칩 이름이 없고 장치 ID로만 확인됩니다.

### 3.2 지금 상태

| 항목 | 내용 | 근거 |
|---|---|---|
| Windows에 보이나 | 안 보임. PCI 루트(PCI0)를 숨겨 둠 | [사실] `DSDT:234-241` `_STA=Zero` |
| 숨긴 이유 | "Windows가 버스는 읽지만 하위 장치를 켜지 않음(CMD=0)" + "Windows에 Apple DART 드라이버 없음" | [사실] `DSDT:225-232` |
| 인터럽트 | 포트 2 묶음 줄 701, `_PRT`에 이미 적혀 있음 | [사실] `DSDT:296`, `:319-326`; `DTS:3187` (0x2bd) |
| 주소 창 측정 | 포트 2: 32비트 창 꺼짐, 64비트 창 버스 주소 **0x4_00000000~0x4_000FFFFF** | [사실] `logs/usb-s235w-20261115-213244.R1R6or.vuart:349`; `HOU:249` |
| UEFI 64비트 창 설정 | `PcdPciMmio64Base = 0x400000000`, 번역 0 | [사실] `fw-audio-s256-mu/Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc:88,92-93` |
| 하드웨어가 해석하는 64비트 창 | 0x6A0000000~0x6BFFFFFFF (512 MiB) | [사실] `DTS:3196` ranges, `DSDT:267` |
| DART | UEFI가 포트 세 개의 DART를 같은 주소 표로 설정 | [사실] `network.md:50` (`AppleDartIoMmuDxe.c` 433-435, 720-739) |
| ARM64 Windows 드라이버 | 찾지 못함. 드라이버 모음 사이트의 `b57nd60a.inf` 목록은 모두 x64 | [사실] 검색 결과 https://treexy.com/products/driver-fusion/database/network-adapters/broadcom/netxtreme-gigabit-ethernet-controller/ ; Broadcom 공식 ARM64 패키지는 찾지 못함 [가설: 없음] |
| 설치된 Windows 드라이버 저장소 확인 | 하지 않음. 로그에서 `b57nd60a` 검색 결과 0 | [사실] 직접 검색 |

**중요한 어긋남 [가설, 근거 강함]**: UEFI가 이더넷 64비트 주소 창을 0x4_00000000에 잡았는데, Apple PCIe가 실제로 넘겨 주는 64비트 창은 0x6A0000000부터입니다(DTS ranges). 0x400000000은 windows-on-m1이 **빈 주소**로 보고 가짜 NVMe 주소로 쓴 자리입니다([사실] `port-j274/J274-BOARD-PROFILE.md:67` IO-2). 그래서 지금 설정 그대로면 CPU가 이더넷 칩 레지스터에 닿지 못할 가능성이 큽니다. 이더넷 첫 단계는 이 값을 DTS와 맞추는 것입니다.

### 3.3 windows-on-m1은 PCIe 장치를 어떻게 다루나

| 항목 | 내용 | 근거 |
|---|---|---|
| 실제 Apple PCIe | Windows에 넘기지 않음. m1n1 `pcie_init`은 Linux 부팅 경로(`kboot.c`)에서만 부름 | [사실] `WOM/m1n1_windows/src/kboot.c:2812` (다른 호출 없음, grep) |
| PCI 루트 | 하이퍼바이저가 흉내 내는 PNP0A08 루트 하나, 그 아래 가짜 NVMe만 | [사실] `WOM/mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl:180-189` |
| USB | DWC3를 ACPI 일반 장치(`_CCA=1`, 인터럽트 857)로 | [사실] 같은 파일 `:150-176` |
| 와이파이·블루투스 | 없음 | [사실] `WOM/documentation/LIMITATIONS.md:18` |

즉 **windows-on-m1에는 실제 PCIe 장치를 Windows에 넘긴 선례가 없습니다.** 우리 USB-A(FL1100)가 첫 사례입니다.

### 3.4 목표

Windows에서 내장 유선 랜이 1 Gbps로 연결되고, 받은 파일 해시가 원본과 같습니다. 맥북 없이도 됩니다.

### 3.5 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | Windows 드라이버 저장소에 BCM57762(`PCI\VEN_14E4&DEV_1682`)를 받는 ARM64 드라이버가 있는지 확인. 파이프 없이 전체를 파일로 받음 | 작업 파일(`pnputil /enum-drivers`, `dir DriverStore\FileRepository`) | 맥북에서 `b57nd60a`·`14E4` 검색 | 없음 | 낮음 (읽기만) | 없음 |
| 2 | UEFI 64비트 창을 하드웨어 창에 맞춤: `PcdPciMmio64Base` 0x400000000 → 0x6A0000000 | `T810XFamilyPkg.dsc.inc:92-93`, 펌웨어 재빌드 | 하이퍼바이저 ECAM 덤프(S235W 방식)에서 포트 2 창이 0x6A0000000대, 이더넷 BAR 읽기 값이 0xFFFFFFFF가 아님 | 없음 | 중간: FL1100은 32비트 창이라 영향 없을 것 [가설]. USB-A 회귀 시험(1장 U3)을 같이 함 | 없음 |
| 3 | 이더넷 칩이 살아 있는지 하이퍼바이저에서 읽기 전용 확인: 장치 ID 레지스터, PHY 링크 상태(케이블 꽂음) | `HV`에 읽기 전용 관측 몇 줄 | 링크 업 비트, MAC 주소가 ADT 값과 같음 | **필요** (랜선을 공유기에 꽂기) | 낮음 | 2단계 |
| 4 | ACPI에 이더넷을 일반 장치로 추가(USB-A의 `SCB0/XHC0`과 같은 꼴): 메모리 창, 인터럽트 701, `_INI`로 MSE/BME, 부모 컨테이너 `_DMA`, IORT 이름 항목. 하드웨어 ID는 우리 것(예: `NWOA5776`) | `DSDT.asl`, `IORT.aslc` | 장치 관리자에 "알 수 없는 장치"로 나옴, 문제 코드 28 | 없음 | 낮음 | 2단계, 8장 2단계 |
| 5 | 하이퍼바이저: 이더넷 BAR을 게스트에 넘기고(지금 USB-A처럼), 인터럽트 701 전달, DART 매핑 확인 | `HV/hv_vm.c`(FL1100 처리 본뜸), 단독 부팅 땐 C | 701 주입 기록, DART 오류 0 | 없음 | 중간 | 4단계, 8장 |
| 6 | Windows ARM64 랜 드라이버 작성(NetAdapterCx). FreeBSD `bge` 순서를 따라 초기화·송수신 고리·PHY·인터럽트. 처음엔 INTx, 체크섬 오프로드 끔 | 새 폴더 `port-j274/net/bcm57762/` (VM에서 빌드, 시험 서명) | `Get-NetAdapter` Up 1 Gbps, DHCP 주소 받음 | 시험 서명 설치 승인 | 큼: 새 드라이버 전체. 잘못된 DMA 주소로 메모리를 덮을 수 있음 [가설] → DART가 막는 범위 확인 필요 | 5단계 |
| 7 | 무결성 시험: 맥북 `python3 -m http.server`에서 2~4 GB 파일 10회 받아 SHA256 비교 (`network.md` 4단계 기준) | 작업 파일 | 아래 표 | 맥북 쪽 서버 켜기(Claude가 할 수 있음) | 낮음 | 6단계 |
| 8 | 체크섬 오프로드·MSI(8장 4단계 뒤)·절전 | 드라이버 | 속도·CPU 사용률 표 | 없음 | 중간 | 7단계 |

통과 기준 (`nwoas_scripts/plan-20261115/network.md:132-137`과 같음)

| 지표 | 기준 |
|---|---|
| 받은 총량 (GB) | 20 이상 |
| 해시 불일치 (회) | 0 |
| 장치 리셋 (회) | 0 |

참고(대안): USB-A 랜 어댑터(RTL8153 등)는 USB-A 손상이 고쳐졌으므로 그때보다 앞길이 열렸습니다. 다만 콘치님이 "USB 유선 랜은 우선순위 아님"으로 정하셨으므로([사실] `HANDOFF-2026-11-16-evening.md:322`) 이 로드맵의 순위에는 넣지 않았습니다.

### 3.6 재사용 코드와 라이선스

| 무엇 | 라이선스 | 쓰임 |
|---|---|---|
| FreeBSD `sys/dev/bge/if_bge.c` | BSD | 초기화 순서·레지스터를 옮겨 쓸 수 있음 [가설: 파일 머리말은 이번에 확인 안 함] |
| OpenBSD `bge` | ISC/BSD 계열 [가설] | 같은 쓰임 |
| Linux `tg3` | GPL-2.0 | 참고만 |
| Microsoft NetAdapterCx 예제(Realtek 예제 `RtEthSample`) | MIT [가설: 저장소 라이선스 확인 안 함] | Windows 쪽 뼈대 |
| 우리 FL1100 ACPI·HV 처리 | 우리 코드 | 4·5단계 본보기 |

### 3.7 모르는 점

- [가설] BCM57762가 펌웨어 파일을 올려야 하는지 모릅니다(Linux tg3에서 펌웨어가 필요한 것은 57766 쪽으로 기억하지만 확인 안 함).
- [가설] 칩의 DMA 주소 한계(32/40/64비트)를 확인하지 않았습니다.
- [가설] 포트 2 인터럽트 701이 UEFI에서 열려 있는지(포트 INTx 마스크 해제) 모릅니다. FL1100(698)은 열려 있습니다.
- [가설] Windows의 `pci.sys`가 하위 장치를 켜지 않던 원인은 풀리지 않았습니다. 일반 장치 방식으로 돌아가면 이 문제는 피합니다.
- [가설] 시험 서명 드라이버는 Vanguard(LoL) 같은 안티치트에서 문제가 된다는 커뮤니티 보고가 있습니다(`network.md:13`).

### 3.8 크기: 큼 (1~5단계 작음~중간, 6단계 큼)

---

## 4. HDMI 소리·이어폰 단자·내장 마이크

| 장치 | 상태 |
|---|---|
| HDMI 소리 | 막힘(안전장치) |
| 이어폰 단자 | 막힘(안전장치) |
| 내장 마이크 | 막힘(안전장치) |
| (참고) 내장 스피커 | Windows 드라이버 막힘(안전장치). 하이퍼바이저 전 단계 비프음은 됨 [사실] `HANDOFF-2026-11-16-evening.md:18-20` |

설계는 하지 않았습니다. CVP 승인 뒤 소리 갈래에서 다룹니다.

---

## 5. 전원 버튼·앞 LED·SMC 버튼

### 5.1 지금 상태

| 항목 | 내용 | 근거 |
|---|---|---|
| 맥미니 버튼 | 전원 버튼 하나. ADT `/buttons`에 `button-names = hold`, SMC 쪽 `smc-pmu`에 `function-pmu_button` | [사실] ADT |
| 버튼 신호 경로 | SMC가 알림(이벤트 0x7201, 버튼 0x01 = 맥미니 전원 버튼)을 보냄. 오래 누름(0x00)은 강제 전원 끄기 직전에 옴 | [사실] Asahi `drivers/input/misc/macsmc-input.c:33-39` (`GPL-2.0-only OR MIT`), https://github.com/AsahiLinux/linux/blob/asahi/drivers/input/misc/macsmc-input.c |
| Windows 쪽 | FADT에 "전원 버튼은 제어 방법 장치" 표시만 있고, 그 장치(PNP0C0C)나 알림 경로가 DSDT에 없음 | [사실] `fw-audio-s256-mu/.../FADT.aslc:73`; DSDT에 PNP0C0C·ACPI0013 없음(grep) |
| 하이퍼바이저 SMC | `HV/smc.c` 181줄, 키 읽기·쓰기만. 버튼 알림 처리 없음 | [사실] `HV/smc.c:44-181`, "button" 검색 0 |
| 앞 LED | 무엇이 제어하는지 모름. ADT에 LED 노드 없음 | [사실] ADT 문자열 검색 |
| Windows 재시작 | 맥미니 전체가 리셋되어 맥북 쪽 실행기를 다시 띄워야 함 | [사실] `HOU:101-102`, `network.md:33` |

### 5.2 목표

전원 버튼을 짧게 누르면 Windows가 "전원 단추 동작"(종료/절전)을 합니다. 오래 누르면 지금처럼 하드웨어가 강제로 끕니다. LED는 켜짐/꺼짐을 맞게 보입니다.

### 5.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 게스트 실행 중 SMC 알림이 하이퍼바이저에 오는지 읽기 전용 확인: SMC 메일함 알림을 기록만 | `HV/smc.c`, `HV/hv_vm.c` 틱 | 버튼을 짧게 눌렀을 때 `0x7201` 기록 | **필요** (짧게 1번 누르기, 오래 누르지 않기) | 중간: SMC 메일함을 하이퍼바이저와 게스트 m1n1이 함께 쓰면 엉킬 수 있음 [가설] | 없음 |
| 2 | ACPI에 GED(ACPI0013) + 전원 버튼(PNP0C0C) 추가. GED 인터럽트는 하이퍼바이저만 넣는 가상 SPI | `DSDT.asl` | 장치 관리자에 "ACPI 전원 단추" 정상 | 없음 | 낮음 | 없음 |
| 3 | 1단계 알림(눌림)을 받으면 그 가상 SPI를 넣음 | `HV/hv_vm.c`, `HV/hv_vgic.c` | 버튼 → Windows 종료 대화/절전 | 필요 | 중간: 절전은 아직 지원 안 됨 → 처음엔 "종료"로 설정 | 1·2단계 |
| 4 | Windows 종료 시 맥미니를 실제로 끔(PSCI SYSTEM_OFF → SMC 전원 끄기)인지 확인 | `HV/hv_psci.c` | 종료 후 전원 꺼짐 | 필요 (눈으로 확인) | 중간 | 없음 |
| 5 | 앞 LED: macOS에서 SMC 키 목록을 읽어 LED 키를 찾음(읽기만) | 맥미니 macOS 스크립트 | 키 후보 표 | 없음 | 낮음 | 없음 |

### 5.4 재사용 코드와 라이선스

| 무엇 | 라이선스 |
|---|---|
| Asahi `macsmc-input.c` (이벤트 번호) | GPL-2.0-only OR MIT → MIT로 쓸 수 있음 |
| m1n1 `smc.c` | MIT |
| edk2 GED/전원 버튼 ASL 예(RPi 등) | BSD-2-Clause-Patent [가설: 해당 파일 확인 안 함] |

### 5.5 모르는 점

- [가설] 게스트가 실행 중일 때 SMC 메일함을 누가 쥐고 있는지(하이퍼바이저/게스트 m1n1/아무도) 확인하지 않았습니다.
- [가설] LED 제어 방법을 모릅니다.

### 5.6 크기: 중간 (LED는 작음이지만 모르는 것이 많음)

---

## 6. UEFI 단계 키보드·마우스 (맥북 없이)

### 6.1 지금 상태

| 항목 | 내용 | 근거 |
|---|---|---|
| UEFI 드라이버 | PCIe(`PciHostBridgeDxe`, `PciBusDxe`, `AppleSiliconPciPlatformDxe`), `XhciDxe`, `UsbKbDxe`, `UsbMouseAbsolutePointerDxe`, `UsbMassStorageDxe`가 들어 있음 | [사실] `fw-audio-s256-mu/Platform/MacMini2020Pkg/MacMini2020.fdf:154-211` |
| UEFI가 FL1100을 붙잡나 | 붙잡음. 지금 부팅 기록에 UEFI xHCI 드라이버 기록이 있음 | [사실] `logs/agxmed1g-s249-20261116-202802.1sXX3e.vuart:43` `NWOAS-XHC bindstart BAR=0x6C0000000` |
| 사람이 UEFI 화면에서 키보드를 써 봤나 | 기록 없음 | [사실] 검색 결과 없음 |
| PCIe 링크 올리기 | 맥북 Python이 `p.pcie_init()`을 부름. UEFI 주석도 "m1n1 pcie_init이 컨트롤러와 루트 포트를 올린다"고 적음 | [사실] `nwoas_scripts/usb-s174/guest_map.py:7`; FDF `:180-185` |
| 4 GiB 낮은 창 매핑, FL1100 BAR·DART 통과 | 맥북 Python | [사실] `guest_map.py:8-16` |
| 단독 부팅 차이 목록 | G-5 "부분": 실제 apcie `pcie_init`이 단독 경로에 없음 | [사실] `port-j274/GAP-LIST.md:27` |
| UEFI 화면 | 지금 화면 준비도 맥북 Python(`tahoe_dcp_guest_hook.py`) 몫 | [사실] `standalone.md:15` |

### 6.2 목표

맥북 없이 켜도 UEFI·Windows 부팅 관리자·복구 화면에서 USB-A 키보드로 고르고 칠 수 있습니다.

### 6.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 맥북 연결 상태에서 UEFI 화면 키 입력 확인: 부팅 직전 기다림을 두고 키를 누르면 UEFI 기록에 키가 찍히게 | UEFI 관측 1줄 또는 기존 `UsbKbDxe` 기록 켜기, 펌웨어 재빌드 | 기록에 키 코드 | **필요** (키 누르기) | 낮음 | 없음 |
| 2 | `guest_map.py`가 하는 세 가지(`pcie_init`, FL1100 BAR·DART 통과, 낮은 창 매핑)를 하이퍼바이저 C로 옮김. 맥북 연결 때도 C가 하고 Python은 확인만 | `HV/hv.c` 또는 단독 실행기, `usb-s174/guest_map.py`(확인 전용으로) | 맥북 연결 부팅에서 USB-A 키보드·U3 시험 정상 | 없음 | 중간: 순서가 바뀌면 USB-A가 안 올라옴 | 8장 2단계 |
| 3 | 맥북 없이 부팅(단독 실행기)에서 UEFI 키보드 확인 | 단독 실행기(standalone.md M8) | 맥북 케이블 뺀 상태로 키 입력이 화면에 반영 | 필요 | 큼: 화면이 안 나오면 확인이 어려움 → 하이퍼바이저 기록을 SSD에 남기는 방법 필요 [가설] | M8, M12(화면) |

### 6.4 재사용 코드: m1n1 `pcie.c`(MIT), Mu 드라이버(BSD-2-Clause-Patent), 우리 `guest_map.py`.

### 6.5 모르는 점: [가설] 맥북 없이 iBoot→m1n1만으로 부팅할 때 PCIe 전원 영역(power-domains)이 켜져 있는지 확인하지 않았습니다.

### 6.6 크기: 중간 (단독 부팅 전체에 묶임)

---

## 7. (위 표에 없는 것) USB-C로 맥북 연결이 빠지면 바뀌는 것

| 지금 맥북 연결이 하는 일 | 대신할 것 | 근거 |
|---|---|---|
| 게스트 띄우기 순서 전체 | 하이퍼바이저 C 실행기 | [사실] `standalone.md:13` |
| 파일 전달·원격 명령 통로(NS2) | 없음 → 네트워크(이더넷) 또는 SSD 파일 | [사실] `GAP-LIST.md:31` G-9 |
| 하이퍼바이저 기록 보기 | SSD에 기록 남기기 [가설] | — |

그래서 **이더넷은 맥북 없는 개발에서 원격 명령 통로를 대신하는 길**이기도 합니다 [가설].

---

## 8. 모든 PCIe 장치가 함께 쓰는 기반 (FL1100·이더넷·와이파이)

### 8.1 지금 상태

| 항목 | 내용 | 근거 |
|---|---|---|
| 포트·버스 | 포트 0 → 버스 1(와이파이·블루투스), 포트 1 → 버스 2(FL1100), 포트 2 → 버스 3(이더넷) | [사실] `DTS:3203-3268` |
| 포트별 인터럽트 줄 | 695 / 698 / 701 (각 포트 INTx 묶음) | [사실] `DTS:3187`, `DSDT:292-297` |
| MSI | apcie는 MSI 주소 0xFFFFF000을 받아 AIC 704부터 32개 줄로 바꿈. 포트별 8개, 시작 0/8/16 | [사실] ADT `/arm-io/apcie` `msi-address = 0xfffff000`, `msi-vector-offset = 0x2c0`, `#msi-vectors = 32`; `pci-bridge0/1/2` `msi-vector-base = 0/8/16`; `DTS:3190` |
| Windows MSI | 쓰지 않음. IORT에 ITS 없음, `_OSC`로 기능 넘기지 않음 | [사실] `fw-audio-s256-mu/.../IORT.aslc:18-19`, `DSDT:276-` |
| vGIC ITS | 레지스터 값 저장만 하는 뼈대 | [사실] `HV/hv_vgic.c:79`, `:122-177` |
| SMMU 흉내 | 빈 함수 | [사실] `HV/hv_smmu.c:39-45` |
| vGIC 우선순위 버그 | 모든 장치 인터럽트가 엉뚱한 우선순위로 들어가던 것을 g252J에서 고침 | [사실] `HANDOFF-2026-11-16-evening.md:229-240` |
| 32비트 주소 창 | 0xC0000000~0xFFFFFFFF(1 GiB), CPU 0x6C0000000 | [사실] `DSDT:254-262`, `T810XFamilyPkg.dsc.inc:85-87` |
| 게스트 낮은 4 GiB | 게스트 주소 = 버스 주소로 RAM에 보임 | [사실] `HOU:28-30`, `:196-198` |
| 예약된 것 | 0xC0000000~0xC00FFFFF (1 MiB)만 | [사실] fw-s237 README |
| 와이파이 포트를 켜면 | FL1100 창이 0xC0D00000으로 밀려 예약 밖 (예측) | [사실: 예측이라고 적힌 문서] `nwoas_scripts/wifi-s240/hw/README.md:21-25` |
| 링크 올리기 | m1n1 `pcie_init`(맥북 Python이 부름) + UEFI `AppleSiliconPciPlatformDxe`(포트별 리셋·클럭) | [사실] `guest_map.py:7`, FDF `:180-187` |
| 포트 0 전원 | SMC GPIO로 켜야 함. 지금 코드 없음 | [사실] `network.md:47` → 와이파이 갈래라 **막힘(안전장치)** 쪽으로 둠 |
| Windows에 보이는 방식 | PCI 루트는 숨기고 장치마다 ACPI 일반 장치로 | [사실] `DSDT:225-241`, `:345-` |

### 8.2 목표

새 PCIe 장치를 붙일 때 "ACPI 항목 하나 + 하이퍼바이저 표 한 줄"로 되게 하고, 주소 창·MSI 주소가 게스트 RAM과 절대 겹치지 않게 합니다. 맥북 없이도 같은 일이 되게 합니다.

### 8.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 게스트 RAM에서 **PCIe 32비트 창 전체**(0xC0000000~0xFFFFFFFF)를 뺌. MSI 주소 0xFFFFF000도 이 안에 들어감 | `MemoryInitPeiLib.c`(S237 자리, 크기만 바꿈) | Windows 메모리 지도에서 그 구간 없음, U3 시험 3+3회 정상 | 없음 | 중간: Windows의 32비트 중간 복사 자리(지금 0xFFED0000~0xFFF60000 [사실] `HOU:132-133`)가 옮겨 감 | 없음 |
| 2 | 맥북 Python의 PCIe 일(`pcie_init`, BAR·DART 통과, 낮은 창)을 C로. 장치 표(포트, 버스, BAR, 인터럽트, DART) 하나로 묶음 | `HV/hv_vm.c` FL1100 상수(`0x6c0000000` 하드코딩 `:2286`, `:2328` 등) → 표, `guest_map.py` | 맥북 연결 부팅에서 USB-A 회귀 없음 | 없음 | 중간 | 1단계 |
| 3 | UEFI가 실제로 고른 BAR·창 값을 하이퍼바이저가 부팅 때 읽어 표와 비교, 다르면 기록하고 그 장치만 끔 (S235W ECAM 덤프를 늘 켜 둠) | `HV/hv_vm.c` | 기록에 포트 3개 창 값 | 없음 | 낮음 | 2단계 |
| 4 | 64비트 창 값 맞추기 (이더넷 2단계와 같음) | `T810XFamilyPkg.dsc.inc:92-93` | 3단계 기록에서 0x6A0000000대 | 없음 | 중간 | 3단계 |
| 5 | (나중) MSI: 하이퍼바이저가 ITS를 흉내 내거나, 장치 MSI 설정 쓰기를 가로채 주소를 0xFFFFF000, 값을 포트 줄 번호로 바꾸고 AIC 704+n을 Windows가 원하는 번호로 넘김 | `HV/hv_vgic.c`, `IORT.aslc` | 장치 하나(이더넷)에서 MSI 인터럽트 수신 | 없음 | 큼: Windows ARM64가 MSI에 무엇을 요구하는지 확인 안 함 [가설: GICv3 ITS 필요] | 이더넷 동작 뒤 |
| 6 | (나중) 포트 0 전원 | — | — | — | 막힘(안전장치, 와이파이 갈래) | — |

### 8.4 재사용 코드와 라이선스

| 무엇 | 라이선스 |
|---|---|
| m1n1 `pcie.c` | MIT |
| Mu `AppleSiliconPciPlatformDxe`, `AppleDartIoMmuDxe`, `MemoryInitPeiLib` | BSD-2-Clause-Patent (AppleWOA 쪽 머리말 확인 필요 [가설]) |
| 우리 S235 관측 코드 | 우리 코드 |

### 8.5 모르는 점

- [가설] 포트 INTx 줄이 레벨 신호라 장치 쪽에서 지우면 끝나는지, apcie 포트 레지스터에서 따로 지워야 하는지 확인하지 않았습니다. FL1100(698)은 지금 방식으로 됩니다.
- [가설] 장치가 0xFFFFF000으로 데이터를 보내면 apcie가 MSI로 받아 버릴 것입니다. 지금 그 페이지가 게스트 RAM으로 쓰이는지 확인하지 않았습니다.
- [가설] DART가 장치마다 따로인지(포트별 DART 3개, ADT `dart-apcie0/1/2`), Windows가 바꿀 수 없는 고정 표라서 장치가 게스트 RAM 어디든 쓸 수 있습니다. 드라이버 버그가 게스트 메모리를 덮을 수 있다는 뜻입니다.

### 8.6 크기: 중간 (MSI는 큼)

---

## 9. 막힘(안전장치)

| 항목 | 상태 |
|---|---|
| 와이파이 (BCM4378) | 막힘(안전장치) |
| 블루투스 펌웨어 올리기 | 막힘(안전장치) |
| 소리 드라이버(스피커·HDMI 소리·이어폰·마이크) | 막힘(안전장치) |
| PCIe 포트 0 전원(와이파이용) | 와이파이 갈래와 묶여 막힘(안전장치) |

---

## 10. 우선순위와 이유

| 순위 | 할 일 | 크기 | 이유 |
|---|---|---|---|
| 1 | USB-A 마무리 (쓰기·꽂았다 뽑기·절전 시험) | 작음 | 키보드·마우스·설치 USB가 모두 여기에 기댑니다. 남은 시험은 코드 변경 없이 바로 할 수 있고, 손상 원인이 풀린 지금 확인 비용이 가장 낮습니다 |
| 2 | PCIe 공통 기반 1~4단계 (32비트 창 전체 예약, C로 옮기기, 창 값 확인, 64비트 창 값 맞추기) | 중간 | 이더넷과 (승인 뒤) 와이파이가 모두 이것을 먼저 필요로 합니다. 특히 64비트 창 어긋남은 이더넷 첫 걸음을 막는 것으로 보입니다 [가설]. C로 옮기는 일은 단독 부팅에도 그대로 쓰입니다 |
| 3 | 내장 이더넷 1~5단계 (드라이버 저장소 확인, 칩 살아 있는지, ACPI 항목) | 작음~중간 | 콘치님이 로드맵에 넣으라고 하신 일이고, 앞 단계는 작습니다. 드라이버가 저장소에 있으면 6단계가 통째로 빠집니다 |
| 4 | UEFI 키보드 (맥북 없이) | 중간 | 단독 부팅의 일부라 2순위 결과를 그대로 씁니다. 화면(M12)이 함께 끝나야 실제로 쓸 수 있습니다 |
| 5 | 전원 버튼 | 중간 | 맥북 없이 쓰려면 끄는 방법이 필요합니다. 다른 장치와 얽히지 않아 따로 진행할 수 있습니다 |
| 6 | 내장 이더넷 6~8단계 (드라이버 작성) | 큼 | 3순위에서 드라이버가 없다고 확인되면 시작합니다. 맥북 없는 원격 작업 통로가 되므로 단독 부팅 전에 끝나면 좋습니다 [가설] |
| 7 | USB-C 포트 | 큼 | USB-A가 되므로 급하지 않습니다. 포트 0은 단독 부팅 전에는 맥북이 써서 어차피 못 넘깁니다. 1단계(꽂아 보기)만은 작아서 언제든 먼저 해 볼 수 있습니다 |
| 8 | 앞 LED | 작음 | 쓰임이 가장 적고 모르는 것이 많습니다 |
| — | Thunderbolt/USB4 | 매우 큼 | Linux도 끝나지 않았습니다. 보류 |
| — | 와이파이·블루투스·소리 | — | 막힘(안전장치). CVP 승인 뒤 |

---

## 11. 옛 문서와 지금 사실이 다른 곳 (참고)

| 문서 | 적힌 내용 | 지금 확인된 사실 |
|---|---|---|
| `NWOAS_USBA_NEXT_STEPS.md:2` | USB-A는 ASMedia ASM2214A, 펌웨어를 올려야 함 | ADT·장치 ID로 FL1100 (`usb-xhci,FL1100`, 1B73:1100) |
| `NWOAS_USBA_NEXT_STEPS.md:2`, FDF `:157` 주석 | USB-C 길은 Apple 설계상 막힘 | 9월 D83에서 USB-C 트랙패드 동작 |
| `nwoas_scripts/plan-20261115/usb.md:6-7`, `network.md:10` | USB-A 손상 원인 미해결, No Snoop 가설 | S232 시험으로 No Snoop 원인 아님, 브리지 창 겹침으로 확정·해결(S236/S237) |

---

## 12. 출처

프로젝트 파일 (위 본문에 줄 번호)
- `HANDOFF-2026-11-16-evening.md`, `HANDOFF-2026-11-16.md`
- `nwoas_scripts/usb-s232/HANDOFF-USB-A-CORRUPTION.md`, `nwoas_scripts/fw-s237/README.md`, `nwoas_scripts/usb-s236/`
- `nwoas_scripts/plan-20261115/{usb,network,standalone,DECISIONS}.md`, `nwoas_scripts/wifi-s240/hw/README.md`
- `port-j274/{GAP-LIST,J274-BOARD-PROFILE}.md`
- `NWOAS-STATUS-2026-09-06.md`, `NWOAS-STATUS-2026-09-10-SESSION.md`, `nwoas_scripts/usb-s175/README.md`
- `fw-audio-s256-mu/Platform/MacMini2020Pkg/AcpiTables/{DSDT.asl,IORT.aslc,FADT.aslc}`, `MacMini2020.fdf`, `Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc`
- `m1n1-gpu-hv2/src/{hv_vm.c,hv_exc.c,hv_vgic.c,hv_smmu.c,pcie.c,usb.c,smc.c}`
- `nwoas_scripts/usb-s174/guest_map.py`
- ADT `nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin`, DTS `nwoas_scripts/dtb-s221/recovered-j274.dts`
- 기록 `nwoas_scripts/logs/retest40-22h2.run14.log`, `usb-s235w-20261115-213244.R1R6or.vuart`, `agxmed1g-s249-20261116-202802.1sXX3e.vuart`
- `external/windows-on-m1/documentation/{ARCHITECTURE,LIMITATIONS,ROADMAP}.md`, `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl`, `mu/Silicon/Apple/AppleSiliconPkg/Drivers/AppleUsbTypeCBringupDxe/AppleUsbTypeCBringupDxe.c`, `m1n1_windows/src/kboot.c`

웹
- Asahi SMC 입력 드라이버: https://github.com/AsahiLinux/linux/blob/asahi/drivers/input/misc/macsmc-input.c
- Apple Silicon USB4/Thunderbolt 패치 보도: https://www.phoronix.com/news/Linux-USB4-Apple-Silicon
- M1 USB3가 Linux 6.19에 들어갔다는 보도(2차 출처): https://scribe.disroot.org/comment/8241087
- Apple Silicon USB3 패치 시리즈: https://lists.infradead.org/pipermail/linux-arm-kernel/2025-September/1059846.html
- Broadcom NetXtreme 드라이버 목록(드라이버 모음 사이트, x64만): https://treexy.com/products/driver-fusion/database/network-adapters/broadcom/netxtreme-gigabit-ethernet-controller/
- FreeBSD bge BCM57762 지원: https://lists.freebsd.org/pipermail/freebsd-amd64/2013-November/015680.html
