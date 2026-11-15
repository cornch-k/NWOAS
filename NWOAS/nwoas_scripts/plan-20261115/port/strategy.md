# windows-on-m1(맥북 에어 J313용)을 우리 맥미니(J274)로 옮기는 전략

작성: 2026-11-15. 이 문서의 모든 내용은 파일을 읽기만 해서 얻은 것입니다. 빌드·실행은 하지 않았고, 맥미니에서 확인된 것은 하나도 없습니다.

표기:
- **[사실]** 파일과 줄 번호로 확인한 것
- **[가설]** 확인하지 않은 추측
- **[저자 주장]** 그쪽 문서에 적혀 있지만 우리가 증거로 확인하지 않은 것
- **[미검증]** 앞선 조사에서 나온 숫자인데 검증 단계에서 다시 세어 보지 않은 것

경로 줄임말:
- `그쪽/` = `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1/`
- `우리m1n1/` = `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/`
- `계획/` = `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/plan-20261115/`

---

## 1. 결론

1. **그쪽 코드를 바탕으로 삼고, 그 위에 "J274(맥미니)용 설정 묶음"을 따로 얹는 방식을 추천합니다.**
   - 그쪽 조각을 우리 코드에 하나씩 떼어 붙이지 않습니다.
   - 그쪽의 J313 코드를 직접 고치지도 않습니다.
   - 이유: GPU·SSD·자동 부팅 기능이 모두 그쪽이 크게 다시 쓴 하이퍼바이저 핵심 파일(`hv.c`, `hv_vm.c`, `hv_exc.c`)에 기대고 있어서, 떼어 붙이려면 결국 그 핵심을 우리 쪽에 다시 만들어야 합니다. (하이퍼바이저 = Windows 아래에서 하드웨어 접근을 중계하는 m1n1 부분)
2. **다만 옮기는 비용은 작지 않습니다.** 특히 UEFI 펌웨어(Project Mu) 쪽은 다시 짜야 하는 부분이 있습니다.
   - [사실] 우리 Mu 수정(커밋 35034be, 25개 파일) 중 6개는 그쪽도 고친 파일입니다.
   - [사실] 우리가 고친 메모리 주소 변환(MMU) 코드와 인터럽트 장치(GIC) 코드는 그쪽 트리에서 아예 지워졌고, 그쪽은 Mu 기본 부품(`ArmPkg`의 `ArmMmuBaseLib`/`ArmMmuPeiLib`)으로 바꿨습니다(`그쪽/mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc:318,454,603`). 그래서 이 두 부분은 옮겨 붙이는 것이 아니라 **새로 다시 구현**해야 합니다.
3. **지금 잘 되는 S231은 그대로 얼려 둡니다.** 새 방식이 맥미니에서 S231과 같은 수준(부팅·CPU 8개·USB·디스크 보호)에 이를 때까지 비교 기준으로 씁니다.
4. **진짜 관문은 코드가 아니라 펌웨어 판입니다.**
   - [사실] 그쪽 GPU 경로는 macOS 13.5 짝 펌웨어만 받습니다(`그쪽/m1n1_windows/src/hv_agx_retained_platform.c:457`).
   - [사실] 우리 계획 `계획/dualboot.md:17`은 시동 항목용 껍데기 볼륨(스텁)을 지금 macOS와 같은 **26.x**로 만들자고 추천했습니다.
   - 둘이 부딪칩니다. **일을 시작하기 전에 콘치님이 13.5와 26.x 중 하나를 정해 주셔야 합니다.** 13.5를 고르면 dualboot 계획의 추천을 뒤집는 것이고, 화면과 USB 검증을 처음부터 다시 해야 합니다(5장 1단계).
5. **라이선스는 막지 않습니다.** 관련된 라이선스는 모두 MIT·BSD 계열이거나 MIT를 고를 수 있습니다. 의무는 저작권 표시를 남기는 것뿐이고, 배포할 때(우리 GitHub 포크 등)만 생깁니다.

### 콘치님이 정하실 것

| 선택지 | 좋은 점 | 나쁜 점 |
|---|---|---|
| 13.5 스텁 | 그쪽 GPU·화면 코드를 그대로 쓸 길이 열림 | dualboot 추천과 반대, m1n1 화면 초기화 경로가 바뀌어 화면·USB를 처음부터 재검증, macOS 26 펌웨어 위에서 13.5 스텁이 되는지 [미검증] |
| 26.x 스텁 | 지금까지의 화면·USB·NVMe 검증 결과를 그대로 씀 | 그쪽 GPU·화면 코드는 그대로 못 씀, GPU는 우리가 Tahoe 펌웨어를 직접 분석해야 함(몇 달 걸릴 것으로 봄 [가설]) |

---

## 2. 그쪽(J313)에서 된 것

그쪽 = `paulsmir/windows-on-m1`, MIT 라이선스, 대상은 M1 맥북 에어(J313). 칩(T8103)은 우리와 같습니다.

### 2.1 우리가 받은 사본의 상태 [사실]

| 저장소 | 커밋 | 날짜 | 비고 |
|---|---|---|---|
| windows-on-m1 (본체) | 6c65673 | 2026-11-07 | 최신 1개 커밋만 받은 사본 |
| m1n1_windows (그쪽 m1n1) | 882985f | 2026-11-07 | 같음 |
| mu (그쪽 UEFI) | 4ffc474 | 2026-11-07 | 같음 |

- 최신 1개 커밋만 받았으므로 git으로 공통 조상을 찾을 수 없어서, 파일 내용을 직접 비교했습니다.
- [사실] 그쪽의 최신 GPU 작업은 `integration/ad04-windows-compiler` 가지에 있고, 우리 사본은 main 가지라서 **그쪽의 가장 새 상태가 아닙니다.**
- [사실] 그쪽 조사 기록 폴더 `그쪽/investigation/`에는 45개 파일(`CHANGES.csv`, `ANS_*`, `AD04_*`, `GPU_CURRENT_STATE.md` 등)이 우리 사본 안에 들어 있습니다. 아직 내용을 하나씩 검증하지는 않았습니다.

### 2.2 그쪽이 해냈다고 적은 것

| 기능 | 쉬운 설명 | 상태 |
|---|---|---|
| 자체 Apple GPU 드라이버(WDDM, D3D12 기능 수준 12_1) | Windows가 Apple GPU로 직접 그림을 그리게 하는 드라이버 | [저자 주장] README. 단 [사실] `그쪽/documentation/LIMITATIONS.md:18-27`에는 아직 "가속 GPU 드라이버 없음, Microsoft 기본 디스플레이"라고 적혀 있어 문서끼리 맞지 않음 |
| AppleAns + ANS-DIRECT | 내장 SSD 제어 장치를 Windows에 통째로 넘기고 Windows용 SSD 드라이버로 직접 쓰는 방식 | [저자 주장] README("더 이상 디스크 작업에 관여하지 않음") |
| SPI-HID 입력 | 맥북 내장 키보드·트랙패드 드라이버 | [저자 주장] `그쪽/documentation/APPLE_INPUT.md` |
| 단독 부팅 | Asahi 방식으로 만든 시동 항목 + 내장 Mu로 두 번째 맥 없이 부팅 | [저자 주장] |
| 연결 부팅 | 두 번째 맥이 USB로 띄우는 방식(우리 지금 방식과 같은 종류) | [저자 주장] |

### 2.3 그쪽이 아직 못 푼 것 [사실]

| 문제 | 출처 |
|---|---|
| 시스템 전체가 약 20초씩 멈춤 | `그쪽/documentation/LIMITATIONS.md:7-8, 49-51` |
| 디스크 연결 코드 결함이 아무 칸(파티션)이나 망가뜨릴 수 있음 | `그쪽/documentation/LIMITATIONS.md:35-36` |
| GPU 주소 변환 캐시(TLB) 문제: 게스트가 VMID 0(가상 머신 번호 0)으로 돌아서, Windows가 보내는 "주소 캐시 비우기" 명령이 GPU 쪽 주소까지 지워 버린다는 것이 저자의 현재 추정 | `그쪽/investigation/GPU_CURRENT_STATE.md:30-36` (이 원인 자체는 저자의 [가설]) |
| 위 문제의 해결 시도는 실험 가지 `exp/m1n1-brokerlock-vmid1`에 있고 우리 사본에는 없음 | 같은 곳 |
| GPU를 다시 켜려면 전원을 완전히 꺼야 했음 | 앞선 조사 보고(그쪽 investigation 기록 기반, 줄 번호 미확인) |
| 다른 모델은 "보드 이름만 바꿔서는" 안 됨 | `그쪽/documentation/LIMITATIONS.md:14-16` |

---

## 3. 우리(J274)와 다른 점

### 3.1 코드의 뿌리

- [사실] 두 프로젝트 모두 AppleWOA의 m1n1_windows에서 갈라졌습니다.
  - 우리 `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows`의 원본은 `AppleWOA/m1n1_windows`, 바탕 커밋은 b7912250(2025-12-16, "Merge SMP and vGIC changes to main"), 우리 첫 커밋은 59fb5442(2026-06-29)입니다.
  - 두 쪽 `hv_vgic.c` 모두 "amarioguy (AppleWOA authors)" 머리글을 유지합니다.
  - `우리m1n1/`은 git 기록이 없는 사본입니다.
- [가설] 그쪽의 바탕도 b7912250이거나 그 근처입니다.
- [사실] 우리 Mu는 AppleWOA `apple_silicon_platforms_mu`의 8dbbcad(2026-05-29) 위에 우리 커밋 35034be(25개 파일, +9529줄)를 얹은 것입니다.

### 3.2 AppleWOA 바탕(b7912250)과 비교한 m1n1 `src/` 차이 [미검증]

| 구분(파일 수, 개) | 그쪽 m1n1 | 우리 S231 |
|---|---|---|
| 바탕과 같음 | 144 | 171 |
| 바탕에서 고침 | 50 | 23 |
| 그 트리에만 있음 | 185 | 4 (`nwoas_frontend/`, `nwoas_stage8.inc`, `nwoas_s229_adapter.inc`, `afk_ring.h`) |
| `.c` 파일 전체 | 195 | 110 |

| 파일 | 우리 줄 수(줄) | 그쪽 줄 수(줄) | 다른 줄 수(줄) |
|---|---|---|---|
| `hv_exc.c` | 2449 | 2391 | 2470 (거의 새로 씀) |
| `hv.c` | 481 | 1404 | 1043 |
| `hv_vm.c` | 4422 | 1458 | 3852 |
| `hv_vgic.c` | 2076 | 2477 | 949 |

위 두 표의 숫자는 앞선 조사에서 나왔고, 검증 단계에서 다시 세지 않았습니다.

### 3.3 Mu(UEFI 펌웨어) 차이

| 항목 | 값(파일 수, 개) | 표기 |
|---|---|---|
| 8dbbcad 대비 그쪽: 같음 | 178 | [미검증] |
| 8dbbcad 대비 그쪽: 고침 | 32 | [미검증] |
| 8dbbcad 대비 그쪽: 없앰(AppleArmMmuLib, AppleSARTLib, ArmGicArchLib, TIANO 서브모듈 포함) | 15 | [미검증] |
| 우리 35034be의 파일 중 그쪽도 고친 것 | 6 | [사실] |
| 우리 35034be의 파일 중 그쪽 트리에 없는 것(우리 새 파일이거나 그쪽이 지운 파일) | 11 | [사실] |

- [사실] 겹치는 6개: `AppleSiliconPkg.dsc.inc`, `AppleDartIoMmuDxe.c`, `AppleSiliconPciPlatformDxe.c`, `MADT_Static.aslc`, `MemoryInitPeiLib.c`, `T810XFamilyPkg.dsc.inc`.
- [사실] 우리 커밋은 `AppleArmMmuLib/.../ArmMmuLibCore.c`와 `ArmGicArchLib.c`를 고쳤는데, 그쪽은 두 라이브러리를 지우고 Mu 기본 부품으로 바꿨습니다(`그쪽/mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc:318,454,603`). → 다시 구현해야 함.
- [사실] 맥미니용 묶음 `Platform/MacMini2020Pkg`는 **그쪽 Mu에도 이미 있습니다.** AppleWOA에서 물려받았고 8dbbcad와 같은 내용이며, `MacMiniFamilyPkg`도 있습니다. → 새로 만들 것이 아니라 거기에 우리 35034be를 옮겨 얹으면 됩니다.
- [사실] 그쪽이 더한 것: J313 ACPI 표(`J313AppleAgx*.asl`, `J313AppleAns*.asl.inc`, `J313AppleInput.asl.inc`), `WindowsAutoBootDxe`, `AppleAnsExitBootServicesGateLib`, `BootLaunchApp`. (ACPI 표 = Windows에 장치 목록과 주소를 알려 주는 표)

### 3.4 그쪽이 J313에 묶여 있는 곳 [사실]

| 항목 | 값 | 출처 |
|---|---|---|
| j313을 언급하는 그쪽 m1n1 `.c` 파일 | 24개 | 검증 단계에서 확인 |
| GPU 경로가 받는 보드 번호 | `board_id 0x26`만 | `그쪽/m1n1_windows/src/hv_agx_retained_platform.c:454` |
| GPU 경로가 받는 펌웨어 판 | `V13_5`만 | 같은 파일 `:457` |
| GPU 제어 명령 인코더 | `…G13V13_5` 계열 | `그쪽/drivers/apple-agx/shared/include/apple_agx_device_control.h:53-65` |
| 화면 장치(DCP) 시작 코드 | "Pinned J313/13.5" 주석 | `그쪽/m1n1_windows/src/dcp_iomfb_bootstrap.c:216,234` |
| `config/j313-*.json` 설정 파일 | 7개 (8번째 `apple-ans-broker-abi.json`은 보드 공통) | `그쪽/config/` |
| 그 설정이 고정한 RAM 끝 주소 | 0xa00000000 (8 GB) | `그쪽/config/j313-guest-layout.json` |
| 그 설정이 고정한 화면 | 내장 화면 2560x1600 | 같은 폴더 |
| 그쪽 xHCI(USB) 주소 `0x502280000` | J313의 두 번째 USB-C 제어 장치 | `그쪽/m1n1_windows/src/hv_vgic.c:47` `J313_XHCI1_BASE` |

### 3.5 하드웨어·환경 차이

| 항목 | J313(맥북 에어, 그쪽) | J274(맥미니, 우리) | 표기 |
|---|---|---|---|
| 칩 | T8103 | T8103 | [사실] |
| RAM | 8 GB(설정 기준) | 16 GB | [사실] |
| 보드 번호 | 0x26 | 0x22로 추정 | [가설] — 우리 장치 트리(ADT, iBoot가 넘겨주는 하드웨어 목록)에서 아직 읽지 않음 |
| 화면 | 내장 패널 | HDMI(DCP + DP→HDMI 변환 칩) | [가설, 확신 높음] |
| PCIe 장치 | (해당 없음) | Wi-Fi, FL1100 USB-A, 이더넷 3개 | [가설, 확신 높음] |
| 키보드·트랙패드 | SPI 내장 | 없음(USB 외장) → 그쪽 입력 드라이버는 필요 없음 | [가설, 확신 높음] |
| OS 짝 펌웨어 | macOS 13.5 | macOS 26 Tahoe | [사실] 그쪽 코드 / [거의 사실] 우리 `계획/gpu.md` §2.3 |
| 디스크 경로 | NVMe 연결 코드 또는 ANS-DIRECT | NVMe 흉내 + 쓰기 허용 구역 + GPT 보호 검사 | [사실] |

- [저자 주장] 그쪽 맥북 에어는 macOS 15도 돌린다고 적혀 있습니다(`그쪽/README.md:30`). 하지만 이 줄은 GravityMark 벤치마크 결과를 설명하는 문맥이지, 펌웨어를 어떻게 불러오는지에 대한 증거가 아닙니다. 그리고 15이지 26이 아닙니다.

### 3.6 우리 쪽에서 꼭 지켜 가져갈 것

| 항목 | 이유 |
|---|---|
| FL1100 USB-A 처리(`우리m1n1/src/hv_vm.c` 약 3020-3080줄 [사실]) + 계획 중인 S232 수정 | J313에는 FL1100이 없음. 우리만 가진 코드 |
| NVMe 흉내 + 쓰기 허용 구역(시작 39944960) + `nvme-s124/gpt_guard.py` | macOS 칸을 지키는 유일한 장치. 그쪽 **두 디스크 경로 모두** "아무 칸이나 망가뜨릴 수 있음" 경고가 붙어 있음(`LIMITATIONS.md:35-36`은 NVMe 연결 코드에 대한 것). 단독 부팅에는 맥북 파이썬이 없으므로 **C로 옮겨야 함** |
| 80 GB 디스크 배치(`layout-s231/manifest.json`, NWSTUB 4 GB 예약) | dualboot 계획과 이미 맞음 |
| Tahoe 화면 작업(`display-s190/tahoe_dcp_guest_hook.py`) | 26.x 펌웨어에서 화면을 띄우는 유일한 길. 13.5가 안 되면 이것이 대안 |
| 원격 작업 도구(NWOS.EXE, NS2 경유) | WinPE·Windows에서 이미 검증됨. 그쪽은 RDP·SSH·KD에 의존 |
| 우리 실행기와 버전 고정 습관 | S231을 그대로 재현해 나란히 비교할 수 있음 |

### 3.7 우리 코드도 같은 VMID 0 문제를 안고 있음 [사실]

- `우리m1n1/src/hv_vm.c:245`는 `VTTBR_EL2`(게스트 주소표 위치 레지스터)를 주소표 값만으로 설정합니다. 즉 VMID 0입니다.
- `우리m1n1/src/hv_vm.c:3002`는 `tlbi vmalls12e1is`(모든 주소 캐시 비우기)를 냅니다.
- 따라서 GPU를 J274로 옮기면 그쪽 2.3의 TLB 문제를 그대로 물려받는다고 봐야 합니다. [가설]

---

## 4. 옮기는 순서(담당·검증 방법)

담당 표기:
- **분석·코드(Claude)**: 맥미니 없이 할 수 있는 일. 프로젝트 규칙대로 일반 작업은 Sonnet, 하이퍼바이저 핵심·인터럽트·MMU는 Opus가 맡습니다.
- **실기(콘치님)**: 맥미니 실제 부팅과 확인. 부팅 성공 여부는 이것으로만 판단합니다.

**빌드 성공은 부팅 성공이 아닙니다.** 아래 모든 관문은 맥미니에서 직접 돌려 봐야 통과입니다.

### 0단계: 하드웨어 없이 하는 준비 (병렬 가능)

| 할 일 | 담당 | 확인 방법 |
|---|---|---|
| 13.5 / 26.x 스텁 중 하나 결정 | 콘치님 | 결정을 `계획/dualboot.md`와 이 문서에 적음 |
| 그쪽 커밋 고정 + `THIRD_PARTY_NOTICES.md` 작성(아래 표) | 분석·코드 | 고정 목록에 6c65673, 882985f, 4ffc474, Mesa 9aa1215f, AppleWOA b7912250·8dbbcad, **Asahi 설치 도구 `stub.py`의 커밋**까지 들어 있는지 확인. 1단계가 고친 `stub.py`에 기대기 때문(`계획/dualboot.md:84`) |
| 우리 ADT에서 보드 번호 등 J274 값을 읽어 그쪽 J313 상수와 대응표 작성 | 분석·코드(ADT 파일이 있으면) / 실기(없으면 맥미니에서 덤프) | 0x22 [가설]이 맞는지 판정 |
| 그쪽 GPU 가지(`integration/ad04-windows-compiler`)와 VMID 실험 가지(`exp/m1n1-brokerlock-vmid1`)를 읽기 전용으로 받아 분석 | 분석·코드 | 받기 전에 콘치님 승인 필요(외부 저장소 내려받기) |
| 그쪽 `investigation/` 45개 기록 검토 | 분석·코드 | 저자 주장 중 증거가 있는 것을 표로 분리 |
| S232(USB-A 수정)는 S231 위에서 따로 계속 | 기존 계획대로 | `계획/usb.md` |
| Windows 드라이버 빌드 환경(WDK/MSBuild) 확보 여부 확인 | 콘치님 | 그쪽은 `.github/workflows/apple-agx-wdk.yml`, `Dockerfile.build` 사용 |

라이선스와 남겨야 할 표시 [사실]:

| 부품 | 라이선스 | 해야 할 일 |
|---|---|---|
| windows-on-m1 (`LICENSE`, "Copyright (c) 2026 paulsmir") | MIT | 모든 사본에 저작권·허가 문구 유지 |
| m1n1 (양쪽 `LICENSE` 같음, "The Asahi Linux Contributors") | MIT | 같음 |
| m1n1 안의 제3자 코드(`m1n1_windows/README.md:77-130`) | libfdt BSD-2 또는 GPL-2(BSD 선택), minlzma MIT, tinf zlib, ATF BSD-3, dlmalloc·PDCLib CC0, Source Code Pro OFL-1.1 | `3rdparty_licenses/` 유지 |
| `src/dcp/*.c` (예: `dcp/parser.c:1`) | GPL-2.0-only 또는 MIT | MIT로 사용 |
| Mu (TianoCore/Mu, Linaro, ARM, Intel, Apple, EFIDroid, Bingxing Wang, Renegade, amarioguy) | BSD-2-Clause-Patent / BSD, 양쪽 `LICENSE` 같음 | 표시 유지 |
| Mu T600x `.dtsi` | GPL-2.0 또는 MIT | MIT로 사용 |
| Mesa | 사본에 포함 안 됨. `drivers/apple-agx/mesa/mesa-source.lock.json`이 9aa1215f 커밋과 `license_path licenses/MIT`를 지정. 덧씌운 파일은 원래 머리글 유지(예: `windows-overlay/agx_pack.c:1-3`, Alyssa Rosenzweig, MIT) | Mesa 전체는 라이선스가 섞여 있음. GPU 쪽 "MIT"는 그쪽이 쓰는 Mesa의 일부(Asahi 컴파일러, gallium asahi, wgl)에 대한 것이므로, 가져오는 파일마다 자기 머리글을 따름 |
| Microsoft 예제(`drivers/apple-agx/windows/icd/agx_d3d12_probe_cs_nbody.hlsl:3-4`) | MIT | 머리글 유지 |
| Asahi drm/asahi (`계획/gpu.md` 기준, `driver.rs:1`) | GPL-2.0-only 또는 MIT | MIT로 참고 |

- [사실] 그쪽 드라이버 소스 41개 중 22개는 라이선스 머리글이 없고 저장소 `LICENSE`가 덮습니다. 우리가 복사할 때는 출처 저장소·커밋·MIT를 적은 머리글을 붙입니다.
- [사실] 그쪽 `AGENTS.md:38-39`는 라이선스가 분명하지 않은 외부 코드는 복사하지 않는다고 적었습니다. 우리도 같은 수준으로 출처를 남깁니다.
- 우리 코드는 MIT로 두어 호환을 유지합니다.
- 배포하는 이미지에 Apple 펌웨어나 ADT 파일을 넣지 않습니다.

### 1단계: 고른 판의 스텁으로 시동 항목 만들기

| 할 일 | 담당 |
|---|---|
| NWSTUB 칸(`계획/dualboot.md`)에 0단계에서 고른 판(13.5 또는 26.x)의 스텁을 만들고, 거기서 지금의 S231 연결 부팅을 이어 불러오기 | 분석·코드(절차·`stub.py` 수정) + 실기(복구 화면 작업·부팅) |

주의 [사실]: `우리m1n1/src/main.c:169-172`는 iBoot 판이 `mBoot-18000.121.3`(지금 26.x)일 때만 화면 초기화를 맥북 쪽에 넘깁니다(`계획/dualboot.md:146`). 13.5 스텁이면 m1n1이 직접 `display_init()`을 실행하므로, S231의 Tahoe 화면 연결과 USB 동작은 검증되지 않은 상태가 됩니다. 즉 13.5를 고르면 이 단계는 "펌웨어만 바꾸기"가 아니라 화면·USB 검증을 다시 여는 일입니다.

검증 방법(실기):

| 확인 항목 | 통과 기준 |
|---|---|
| m1n1 로그의 OS 펌웨어 판 | 고른 판이 찍힘(13.5면 "OS FW version: 13.5") |
| 새 항목에서 S231 연결 부팅 | 바탕화면까지, CPU 8개 |
| 디스크 보호 | 전후 GPT·칸 해시가 같음 |
| macOS 26 항목 | 그대로 부팅됨 |
| 화면 | 13.5면: 우리 C 화면 경로(Tahoe는 `afk.c:323-328`에서 거부 [사실])로 그림이 나옴. 26.x면: 지금과 같이 나옴 |
| USB(13.5인 경우 필수) | USB-A 복사 후 바이트 비교 통과 |

실패하면: 26.x에 머뭅니다. 이 경우 GPU는 우리가 Tahoe 펌웨어를 직접 분석해야 합니다(몇 달 걸릴 것으로 봄 [가설]).

### 2단계: 그쪽 하이퍼바이저 + Mu에 J274 설정을 얹어 S231과 같은 수준 만들기

| 할 일 | 담당 |
|---|---|
| 16 GB용 `config/j274-guest-layout.json` 등 J274 설정 파일 생성(그쪽 J313 파일과 나란히, 그쪽 코드는 고치지 않음) | 분석·코드(Sonnet) |
| 그쪽 Mu의 기존 `Platform/MacMini2020Pkg`에 우리 35034be 옮겨 얹기(PCIe·FL1100·HDMI용 DSDT 포함) | 분석·코드(Sonnet) |
| 겹치는 6개 파일 합치기, 우리 MMU·GIC 수정을 Mu 기본 `ArmPkg` 부품 기준으로 다시 구현 | 분석·코드(Opus — MMU·인터럽트는 시스템 핵심부라 Sonnet으로 부족) |
| FL1100 처리, NVMe 쓰기 허용 구역, GPT 보호 검사(C로)를 그쪽 하이퍼바이저에 옮기기 | 분석·코드(Opus — 하이퍼바이저 예외 처리 경로 변경) |
| 디스크는 그쪽 NVMe 연결 코드를 기본으로 하되 GPT 보호 검사를 반드시 앞에 둠(ANS-DIRECT는 쓰지 않음) | 분석·코드 |
| J274용 실행 파일(예: `hv_launch_j274.c`)을 새 파일로 추가 | 분석·코드 |

검증 방법(실기):

| 확인 항목 | 통과 기준 |
|---|---|
| 찬 부팅(전원 끈 상태에서) | 3회 연속 바탕화면 |
| CPU 수·RAM | S231과 같음 |
| NS2 원격 작업 | 실행됨 |
| USB-A 복사·바이트 비교 | 3회 통과 |
| GPT·macOS 칸 해시 | 변화 없음 |
| 1시간 대기·부하 | HV 로그에 SError(하드웨어 오류 예외)·워치독 없음 |

### 3단계: J274에서 GPU 펌웨어 켜고 끄기만

| 할 일 | 담당 |
|---|---|
| 그쪽 G0/G1에 해당하는 일. GPU 자원 설정을 J274의 실제 ADT 값으로 다시 만들기. 그림 그리기는 아직 안 함 | 분석·코드(Opus — GPU 중계는 하이퍼바이저 핵심부) |
| VMID 0 문제(3.7)를 어떻게 다룰지 그쪽 실험 가지를 보고 정하기 | 분석·코드(Opus) |

검증 방법(실기):

| 확인 항목 | 통과 기준 |
|---|---|
| HV 로그 | GPU 펌웨어 시작, 초기 주고받기 완료, 깔끔한 종료 |
| 다시 시작 | 재부팅 반복이 되거나, 안 되면 전원을 완전히 꺼야 한다는 사실을 기록 |
| Windows 화면 | 여전히 기본 디스플레이 |
| HV 로그 | SError 없음 |

### 그 뒤 (그쪽 저자의 순서, 관문은 우리 기준)

| 순서 | 할 일 | 전제 |
|---|---|---|
| 4 | Windows GPU 드라이버 연결 → 바탕화면을 GPU로 → D3D12 간단 시험 | 3단계 통과, WDK 환경 |
| 5 | 그쪽 13.5 화면 코드로 HDMI | 13.5 스텁을 고른 경우만 |
| 6 | 그쪽 C 자동 부팅(`hv_autonomous_*`)으로 우리 맥북 파이썬 32단계 대체(`계획/standalone.md`) | 2단계 통과 |
| 7 | 시동 선택 화면에 Windows 항목 따로 두기(연결 부팅은 이어 불러오기 경로로 유지) | 6 통과 |
| 8 | 이더넷·Wi-Fi | 두 프로젝트 모두 아직 없음 |
| 9 | AppleAns/ANS-DIRECT(선택) | 오래 돌려 본 뒤, 백업을 둔 상태에서만 |

### 그쪽과 협업 (선택)

그쪽 트리에는 J274나 맥미니 언급이 없고, 보드별 설정을 생성하는 구조라 두 번째 보드를 얹기 좋습니다. J274 설정과 FL1100/PCIe 발견을 그쪽에 올리면 양쪽에 도움이 될 수 있습니다. GitHub 이슈나 토론을 열지는 콘치님이 정하실 일이고, 아무에게도 연락하지 않았습니다.

---

## 5. 위험과 모르는 점

| 번호 | 위험 | 표기 | 대응 |
|---|---|---|---|
| 1 | 13.5 스텁과 26.x 스텁이 서로 부딪침 | [사실] `hv_agx_retained_platform.c:457` vs `계획/dualboot.md:17` | 0단계에서 결정 |
| 2 | macOS 26 시스템 펌웨어 위에서 13.5 스텁이 되는지 | 모름 | 1단계에서 실기로 확인 |
| 3 | Mu 옮기기 비용: 겹치는 6개 파일, MMU·GIC 재구현 | [사실] | 2단계에서 Opus 배정 |
| 4 | J313에 깊게 묶임(보드 번호, 8 GB, 내장 화면, j313 파일 24개) | [사실] | J274 설정을 새 파일로 추가 |
| 5 | J274 보드 번호 0x22 | [가설] | 0단계에서 ADT로 확인 |
| 6 | VMID 0 문제를 우리도 안고 있음 | [사실] 우리 코드 / [가설] 원인 | 3단계 전에 그쪽 실험 가지 분석 |
| 7 | 두 디스크 경로 모두 칸을 망가뜨릴 수 있음 | [사실] `LIMITATIONS.md:35-36` | GPT 보호 검사를 C로, 두 경로 모두에 |
| 8 | 그쪽의 20초 멈춤 | [사실] `LIMITATIONS.md:7-8,49-51` | 2단계 1시간 시험에서 지켜봄 |
| 9 | 그쪽 문서끼리 맞지 않음(GPU 있음/없음) | [사실] `LIMITATIONS.md:18-27` vs README | 성능 숫자는 모두 저자 주장으로 다룸 |
| 10 | 작성자 1명, 매일 커밋 → 계속 움직임 | [사실] | 커밋 고정, 정해진 때에만 맞추기 |
| 11 | 우리 사본이 그쪽 최신이 아님(GPU 가지 없음) | [사실] | 0단계에서 읽기 전용으로 받기(승인 후) |
| 12 | Windows 드라이버 빌드 환경(WDK/MSBuild) 유무 | 모름 | 콘치님 확인 |
| 13 | S231과 새 방식을 동시에 유지하는 이중 부담 | [사실] | 새 방식이 2단계를 통과하면 S231 정리 |
| 14 | 3.2·3.3의 파일·줄 수 | [미검증] | 필요하면 다시 셈 |

### 모르는 것 목록

- J274 보드 번호와 GPU·화면 관련 ADT 값
- 13.5 스텁이 macOS 26 펌웨어 맥미니에서 되는지
- Asahi 설치 도구가 26.x 스텁을 받는지(`계획/dualboot.md` 3.4절)
- 그쪽 GPU 가지의 현재 상태, VMID 실험 가지의 결과
- 그쪽 `investigation/` 기록 중 증거가 붙은 것이 어느 것인지
- 8단계(로더→커널 넘겨주기)는 S231에서 이미 넘었지만, 새 방식에서는 아직 한 번도 확인되지 않았습니다

---

## 6. 출처

그쪽(읽기 전용 사본, `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1/`):
- `LICENSE` — MIT, "Copyright (c) 2026 paulsmir"
- `README.md:30` — macOS 15 언급(GravityMark 문맥)
- `AGENTS.md:38-39` — 외부 코드 복사 원칙
- `documentation/LIMITATIONS.md:7-8, 14-16, 18-27, 35-36, 49-51`
- `documentation/APPLE_INPUT.md:119` — 테스트 서명(`testsigning on`)
- `investigation/GPU_CURRENT_STATE.md:30-36` — VMID 0 / TLB 문제
- `investigation/` — 45개 파일
- `config/j313-*.json` (7개), `config/apple-ans-broker-abi.json`, `config/j313-guest-layout.json`
- `m1n1_windows/src/hv_agx_retained_platform.c:454, 457`
- `m1n1_windows/src/dcp_iomfb_bootstrap.c:216, 234`
- `m1n1_windows/src/hv_vgic.c:47` — `J313_XHCI1_BASE`
- `m1n1_windows/src/dcp/parser.c:1` — GPL-2.0-only OR MIT
- `m1n1_windows/README.md:77-130` — 제3자 라이선스
- `drivers/apple-agx/shared/include/apple_agx_device_control.h:53-65`
- `drivers/apple-agx/mesa/mesa-source.lock.json` — 9aa1215f, `licenses/MIT`
- `drivers/apple-agx/mesa/windows-overlay/agx_pack.c:1-3`
- `drivers/apple-agx/windows/icd/agx_d3d12_probe_cs_nbody.hlsl:3-4`
- `.github/workflows/apple-agx-wdk.yml`, `Dockerfile.build`
- `mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc:318, 454, 603`
- `mu/Platform/MacMini2020Pkg`, `mu/Platform/MacMiniFamilyPkg`

우리:
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/hv_vm.c:245, 3002, 약 3020-3080`
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/main.c:169-172`
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/afk.c:323-328`
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows` (원본 AppleWOA, b7912250 / 59fb5442)
- `/Users/angigyeom/Desktop/DEV/NWOAS/apple_silicon_platforms_mu` (8dbbcad / 35034be)
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/nvme-s124/gpt_guard.py`
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/plan-20261115/dualboot.md:17, 84, 146`
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/plan-20261115/gpu.md` §2.3
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/plan-20261115/usb.md`
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/plan-20261115/standalone.md`
- `/Users/angigyeom/Desktop/DEV/NWOAS/NWOAS-STATUS-2026-11-15.md`
