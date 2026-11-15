# GPU 가속 — windows-on-m1(J313)에서 우리 Mac mini(J274)로 옮기는 방법 검토

작성일: 2026-11-15 · 읽기만 했고, 아무것도 빌드·실행하지 않았습니다. 시리얼 장치(`/dev/cu.*`)도 열지 않았고, 지금 돌고 있는 Windows 게스트도 건드리지 않았습니다.

표기:
- **[사실]**: 파일과 줄 번호로 확인한 내용입니다.
- **[가설]**: 확인하지 못한 추측입니다.
- **[작성자 주장]**: windows-on-m1 쪽 문서에 적혀 있지만 우리가 증거로 확인하지 못한 내용입니다.
- 경로 약어: `WOM/` = `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1/`, `NW/` = `/Users/angigyeom/Desktop/DEV/NWOAS/`

---

## 1. 결론

**콘치님이 정하셔야 할 것은 하나입니다. "macOS와 시스템 펌웨어는 Tahoe 그대로 두고, Windows 부팅용 칸에만 macOS 13.5용 펌웨어 묶음을 쓰는 것"을 허락하실지입니다.** 공개된 GPU 코드는 모두 13.5 펌웨어에서만 동작합니다. 그런데 9월 6일에 "13.5 묶음으로 바꾸지 않는다"는 지시가 기록돼 있어, 콘치님 결정 없이는 시작할 수 없습니다.

- 여기서 "펌웨어 묶음(stub)"은 부팅할 때 iBoot가 GPU·화면 장치에 미리 올려 주는 펌웨어 세트입니다. macOS 본체(Macintosh HD)나 시스템 펌웨어와는 따로 고를 수 있습니다.

| 항목 | 내용 | 구분 |
|---|---|---|
| 그쪽 GPU 코드가 요구하는 펌웨어 | macOS 13.5 (iBoot-8422.141.2) 한 가지만 | [사실] |
| 그쪽이 아는 가장 새 OS 펌웨어 | 15.0 (Tahoe 26.x는 목록에 없음) | [사실] |
| GPU 자료 구조 버전 목록의 끝 | 13.5 (따로 지정하지 않으면 12.3으로 동작) | [사실] |
| 우리 Mac mini에서 13.5 펌웨어 묶음으로 부팅한 기록 | 있음. 7월 13~14일, 지금과 같은 시스템 펌웨어 위에서 부팅 | [사실] |
| 그 7월 부팅에서 증명된 것 | 13.5 펌웨어 로드, HDMI 화면 장치(DCP) 동작 | [사실] |
| 그 7월 부팅에서 증명되지 않은 것 | GPU 펌웨어가 미리 올라오는지, GPU가 켜지는지 | 기록 없음 |
| 9월 6일 지시 | Tahoe 유지, 13.5 묶음으로 바꾸지 않음 | [사실] |

정리하면, 13.5 펌웨어 묶음을 쓰는 길(아래 4절의 A안)이 공개 코드로 갈 수 있는 유일한 길입니다. 이 Mac mini에서 13.5 묶음으로 **화면까지** 띄운 전례도 있습니다. 다만 그 전례는 화면에 대한 것이고, **GPU 동작은 아직 아무도 확인하지 않았습니다.**

---

## 2. 그쪽(J313, M1 MacBook Air)에서 된 것

### 2.1 구성

| 층 | 하는 일 | 위치 | 구분 |
|---|---|---|---|
| 사용자 모드 드라이버 | Mesa의 Apple GPU 컴파일러에 D3D12 셰이더(DXIL)를 번역해 넣음 | `WOM/drivers/apple-agx/mesa`, `d3d12` | [사실] (README.md:73-90) |
| 커널 모드 드라이버(WDDM) | Windows 그래픽 드라이버 본체. GPU 펌웨어와 직접 통신 | `WOM/drivers/apple-agx/windows` | [사실] |
| m1n1 하이퍼바이저 쪽: GPU 페이지 표 중개 | Windows가 요청하면 하이퍼바이저가 주소를 실제 메모리 주소로 바꾸고, 16 KiB 단위로 연속인지 검사함 | `m1n1_windows/src/hv_agx_gpuva_v5.c` (927줄) | [사실] |
| 전원 중개 | 가짜 레지스터 페이지(0x300000000)로 GPU 전원 켜기/끄기 요청을 받음 | `hv_agx_g2.generated.h:16`, `hv_agx_power_broker.c`, `hv_agx_power_platform.c` | [사실] |
| 화면 출력 중개 + 화면 장치(DCP) 제어 | 13.5 펌웨어 전용 호출 순서, 내장 패널 고정 | `dcp_iomfb_v13_5_abi.c:13-14`, `dcp_iomfb_bootstrap.c:137,216,234,309` | [사실] |

- 중개 요청은 128바이트 요청 / 64바이트 응답 우편함으로 주고받습니다. [사실] (`hv_agx_gpuva_v5_mmio.c:3-4`, :13-35)
- 연속성 검사는 `hv_agx_retained_platform.c:60-73`에 있습니다. 작업 시작·끝 알림, 주소 변환 캐시 비우기 같은 처리는 `hv_agx_gpuva_v5.c:728-835`에 있습니다. [사실]
- 전원 함수 이름은 `j313_*`이지만, 실제로는 고정 주소가 아니라 기기 정보 트리(ADT)의 이름 `/arm-io/gfx-asc`, `/arm-io/sgx`로 전원을 켭니다(`hv_agx_power_platform.c:16-28`). [사실] 같은 칩이라 이름이 같으므로 J274에서도 거의 그대로 쓸 수 있을 것입니다. [가설]

### 2.2 펌웨어는 누가 맡는가

- 설계상 GPU 펌웨어, 펌웨어 통신 통로(0x20/0x21), 초기 설정 자료, GPU 주소 표, 작업 줄, 인터럽트 9개는 Windows 드라이버가 맡습니다. 하이퍼바이저는 전원 켜기/끄기와 메모리 보호만 맡습니다. [사실] (`WOM/documentation/design/2026-08-26-j313-agx-g2-direct-firmware-ownership.md:5-12`, 표 :54-66)
- 드라이버가 펌웨어 파일을 직접 올리지는 않습니다. iBoot가 부팅한 OS에 짝지어진 펌웨어를 미리 올려 줍니다. Asahi Linux도 같은 방식입니다(우리 `plan-20261115/gpu.md`에 인용된 Asahi 문서 기준).
- 그쪽은 독립 설치를 `curl alx.sh | sh`(Asahi 설치기)로 시작하고 "UEFI 환경만" 고릅니다. [사실] (`WOM/documentation/INSTALL.md:44-90`)
- Asahi 설치기는 M1에 13.5 펌웨어 묶음을 줍니다. 그래서 그쪽이 13.5에 고정된 것입니다. [가설, 확신 높음] (`dualboot.md` §2.4가 인용하는 asahi-installer main.py 기준)
- 두 번째 Mac을 쓰는 보조 모드도 같은 Asahi 항목에서 출발합니다. 이때 보조 Mac은 "독립 부팅의 3초 대기 시간"을 잡아야 합니다(`WOM/documentation/RUN.md:139-142`). 따라서 그쪽 GPU 개발도 13.5 펌웨어 위에서 이뤄졌습니다. [사실] 우리 `gpu.md` §4의 "모름" 항목은 이것으로 풀립니다.

### 2.3 단계별 관문과 기록

| 관문 | 통과 조건 | 기록 | 구분 |
|---|---|---|---|
| G0 | 기기 정보(ADT) 항목이 계약서와 정확히 일치 | — | [사실] (`AGX_BRINGUP.md`) |
| G1 | 펌웨어 시작 → 1초 안에 응답(Pong) → 정지 → **전원을 완전히 끄고 다시 켜기**, 10회 | EXP-20260825-078 통과, 심박 간격 2.18~2.38 ms | [작성자 주장, 결과 해시 기록 있음] (:29-46, :102-106) |
| G1Q | 63번 작업 통로에서 빈 작업 1건 완료, 제한 500 ms | — | [사실] 조건만 (:162-176, :200) |

- "같은 부팅에서 다시 쓰면 다음 펌웨어 시작이 시간 초과로 실패했다"고 적혀 있습니다(`AGX_BRINGUP.md:44-46`). 즉 GPU 재시작은 아직 전원을 껐다 켜는 방법밖에 없습니다. [사실]

### 2.4 현재 상태 (그쪽 주장)

| 항목 | 값 | 출처 | 구분 |
|---|---|---|---|
| D3D12 기능 수준 | 12_1 | README.md:22-25 | [작성자 주장] |
| GravityMark HD 성능 관문 | 40.77 fps | `investigation/GPU_CURRENT_STATE.md:76-77` | [작성자 주장] |
| 성능 손실 원인 추정 | GPU 주소 변환 손실 (EXP2229/2230) | 같은 파일 :31-39 부근 | [작성자 주장] |
| 원인 가설 | Windows가 0번 가상 머신 번호로 돌아서, 하이퍼바이저의 주소 변환 캐시 비우기(`TLBI ASIDE1OS`)와 Windows 자체의 캐시 비우기가 겹침 | `hv_agx_retained_platform.c:76-86`, :30-38, README.md:103-104 | [작성자 가설] |
| 1번 번호로 바꾼 실험(EXP2232) | "대기 중(Queued)", 결과 없음 | `GPU_CURRENT_STATE.md:36-39`, `EXPERIMENTS.md:74316` | [사실] |
| GPU 사용에 보조 Mac 필요 여부 | 필요 | README.md:25, :122-123 | [사실] |

- 클론 상태: 상위 저장소는 6c65673(이력 1개만 받은 얕은 클론)입니다. `m1n1_windows` 하위 모듈은 882985f입니다. 우리 `gpu.md`가 인용한 "1번 번호로 바꿔도 소용없었다"는 커밋은 이 클론에서 확인할 수 없습니다. [사실]
- `documentation/LIMITATIONS.md:18`, :24-27은 아직 "가속 GPU 드라이버 없음"이라고 적고 있어 README와 맞지 않습니다. 문서가 갱신되지 않은 것으로 보입니다. 그래서 위 수치는 모두 작성자 주장으로만 다룹니다. [사실]

---

## 3. 우리(J274, Mac mini)와 다른 점

| 항목 | 그쪽 J313 | 우리 J274 | 구분·근거 |
|---|---|---|---|
| 보드 번호(Board-ID) | 0x26. 하이퍼바이저가 이 값이 아니면 GPU 초기화를 거부 (`hv_agx_retained_platform.c:454`) | 0x22 | [사실] `NW/nwoas_scripts/logs/appark-chainload-1558.log:49` 외 다수 |
| 칩 / GPU 세대 | T8103 / G13 | 같음 (Chip-ID 0x8103) | [사실] |
| 13.5 버전 검사 | `:457`은 펌웨어 버전이 13.5인지 보고 성능 설정 자료가 유효한지만 표시함. 초기화를 멈추지 않음 | — | [사실] 진짜 차단은 :454 |
| GPU·화면 장치 레지스터 주소 | 0x204000000 / 0x206400000 | 같을 것 (칩 고정) | [가설] 기기 정보 덤프로 확인 필요 |
| GPU용 예약 메모리 | 0x9FFFB8000 / 0x9FFF70000 (8 GB 끝) | 13.5 부팅 때 0xBFFFB8000 / 0xBFFF70000 영역이 잡힘 | 그쪽 값 [사실] `tools/generate_j313_agx_abi_admission.py:127-128`. 우리 값 [사실] `appark-chainload-1558.log:108-109`. 두 값이 같은 용도라는 것은 [가설] |
| GPU 인터럽트 | 9개, 연속 아님: 563~566, 575~579 (`config/j313-agx.json:12-20`). Windows 쪽 번호 880~888 (`J313AppleAgx.asl.inc:22-33`) | 인터럽트 번호는 같을 것. Windows 쪽 번호는 우리 S231 하이퍼바이저의 인터럽트 전달 표에 맞춰야 함 | [가설] |
| 추가 Windows 쪽 인터럽트 번호 | 889(화면 출력용 가짜 인터럽트), 960(SSD 중개) | 전달 표가 최소 880~889까지 받아야 함 | [사실] 하위 모듈 커밋 882985f 기준 |
| 전원·성능 설정 값 | `gpu-*` 속성 약 80개를 J313 값으로 고정 (`tools/generate_agx_hwdata_profile.py:20-49`) | 팬이 있으므로 전원 한도 등이 다를 것 | [가설] 우리 기기 정보 덤프 필요 |
| 화면 | 내장 eDP 2560x1600 고정 (`hv_agx_scanout_broker.h:29-30`, `hv_agx_power_mmio.c:636-640`, `apple_agx_fixed_panel_modes.h`) | HDMI, 기본 화면 장치(disp0)가 "외부"로 표시됨 | [사실] `appark-chainload-1558.log:117-119`. 그쪽 화면 장치 제어 코드는 "외부" 표시된 disp0를 거부함 (`dcp.c:600-604`) |
| 화면 장치 제어 코드 조건 | `J313AP`, 13.5, 내장 disp0 셋 다 필요 (`dcp.c:600-604`) | 셋 중 내장 조건이 안 맞음 | [사실] 고치지 않으면 J274에서는 화면 출력 담당 자체가 없음 |
| 메모리·Windows 메모리 배치 | J313용 8 GB 배치 | 16 GB, 우리 S231 쓰기 구역 | [사실] `drivers/apple-agx`의 108개 파일이 J313을 언급 |
| UEFI(Mu) 플랫폼 | MacBookAirMid2020Pkg에 GPU 장치(AGX0) ACPI 있음 | 우리 MacMini2020Pkg에는 AGX0 없음 | [사실] |
| OS 펌웨어 버전 | 13.5 | 지금은 Tahoe. 로그에 `OS FW version: unknown (mBoot-18000.121.3)`로 찍힘 | [사실] 우리 로그 39줄 |

참고: 그쪽 GPU 자료 구조 버전 설정(`Ver.set_version`)은 부팅된 OS 펌웨어 버전을 보고 정해집니다. 지금 Tahoe 상태에서는 그 값이 `unknown`이라, 지금 구성으로 GPU 실험을 하면 G1 전에 실패할 것입니다. [가설] 그래서 펌웨어 묶음을 정하기 전에는 G1을 미리 시험해 볼 수 없습니다.

---

## 4. Tahoe와 맞지 않는 문제 — 고를 수 있는 길

| 안 | 공개 코드로 GPU 가능 여부 | 비용·위험 |
|---|---|---|
| **A. Windows 부팅 칸(NWSTUB 3번 칸, 4 GB)에만 13.5 펌웨어 묶음.** Macintosh HD와 시스템 펌웨어는 Tahoe 유지 | 가능. Asahi는 G13에서 12.3/13.5를 받고(`gpu.md` §2.3), 그쪽은 13.5만 받음 | 9월 6일 지시, `dualboot.md`의 26.x 추천과 충돌. 13.5에서는 우리 m1n1의 C 화면 초기화가 다시 돌고 Tahoe용 화면 처리 코드는 쓰이지 않음(`src/main.c:169-172`). USB·SSD는 다시 검증 필요(7월에는 동작) |
| B. 26.x 펌웨어 묶음 | 불가 | Tahoe GPU 펌웨어의 통신 방식(초기 설정 자료, 통로, 작업 명령)과 iBoot/화면 장치 26.x 표를 새로 알아내야 함. 수개월 [가설] |
| C. 12.3 펌웨어 묶음 | Asahi만 가능, 그쪽 코드는 불가 | A보다 나쁨 |

| A안 관련 크기 | 값 (GB) | 구분 |
|---|---|---|
| NWSTUB 3번 칸 | 4 | [사실] `dualboot.md` |
| Asahi 펌웨어 묶음 | 약 2.5 | [사실] `dualboot.md` |

- **추천은 A안입니다.** 공개 코드가 있는 유일한 길이고, macOS는 바뀌지 않습니다. `dualboot.md`가 이미 계획한 펌웨어 묶음 칸을 버전만 26.x에서 13.5로 바꿔 같이 쓸 수 있습니다.
- 단 9월 6일 지시(`NW/NWOAS-STATUS-2026-09-06.md:173`, 13.5 복원 항목은 :155에서 "SUPERSEDED")와 충돌하므로 **콘치님의 명시적 허락이 필요합니다.**
- 7월에 쓴 13.5 펌웨어 묶음은 지금은 지워진 WINTEST 영역에 있었을 것입니다. 그래서 다시 만들어야 할 것입니다. [가설]
- A안은 GPU뿐 아니라 **화면 동작의 출발점도 바꿉니다.** 7월 13.5에서는 HDMI 화면이 떴고(`NWOAS-STATUS-2026-07-12.md:14,27`), 9월 6일 Tahoe에서는 HDMI에서 화면 장치 초기화가 실패했습니다(`NWOAS-STATUS-2026-09-06.md:165-169`). 그러므로 펌웨어 묶음 교체는 별도 변수로 시험하고, GPU 시험과 섞지 않아야 합니다.

---

## 5. 옮기는 순서 (담당·검증 방법)

원칙: 각 단계를 통과해야 다음 단계로 갑니다. **빌드 성공은 부팅 성공이 아닙니다.** 실제 검증은 콘치님의 Mac mini에서만 가능합니다.

| 단계 | 할 일 | 담당 | 검증 방법 (통과 조건) |
|---|---|---|---|
| P0 (기기 불필요) | 그쪽 m1n1·Mu 코드를 읽기 전용으로 새 폴더에 복사하고, J313 고정값을 J274 값으로 바꿀 목록 작성(3절 표 + `grep -l J313`) | Sonnet | 모든 고정값이 파일:줄과 함께 목록에 있고 검토 완료 |
| P1 (콘치님 결정 후) | `dualboot.md` §3.2의 0~6단계대로 13.5 펌웨어 묶음 생성. 바꾸는 것은 버전뿐 | 콘치님 결정 → Sonnet 준비 → 콘치님 실기 | ① 시리얼에 `OS FW version: 13.5 (iBoot-8422.141.2)`, Board-ID 0x22, S231 시작 문구 ② 기존 실행 스크립트로 Windows 바탕화면까지(콘치님 확인) ③ USB-A 960 KiB 0 덩어리 문제 재시험 ④ HDMI 화면 동작 확인. **GPU 시험과 섞지 않음** |
| P2 (G0, 기기 정보 읽기만) | 게스트 없이 프록시로 `/arm-io/sgx`, `/arm-io/gfx-asc`, 예약 메모리, `gpu-*` 속성 덤프. 그쪽 형식으로 `config/j274-agx.json` 작성 후 J313과 비교 | Sonnet | 장치 2개, 인터럽트 9개, 영역 주소 확인. 차이마다 설명 있음. Tahoe에서도 읽을 수는 있으나 `gpu-*` 값은 13.5 때와 다를 수 있으므로 P1 뒤에 다시 읽음 |
| P3 (G1, 펌웨어 켜고 끄기) | **새 시험 코드 작성 필요.** 우리 `proxyclient/experiments/agx_boot.py`는 이미 `Ver.set_version(u)`를 부르지만(:11), 클럭 켜기·초기 설정 자료 만들기 후 셸로 빠질 뿐입니다. 시작·응답·정지·10회 반복 기능은 없습니다. 그쪽 `tools/agx_m1n1_backend.py`의 흐름을 읽고 옮겨 씁니다(그쪽 코드는 실행하지 않음) | **Opus** (펌웨어·GPU 주소 표를 다뤄 Sonnet으로는 판단이 부족함) | 펌웨어 시작 → 1초 안에 응답 → 정지 → 전원 완전히 끄고 켜기, 10회 중 10회 응답, 펌웨어 오류 기록 0건. **여기가 실제 진행/중단 판단 지점** |
| P4 (G1Q) | 63번 작업 통로에서 빈 작업 1건, 제한 500 ms | Opus | 10회 전원 재시작마다 성공 |
| P5 (하이퍼바이저 통합) | 전원 중개(ADT 이름 기반이라 작업량 적을 것 [가설]), GPU 페이지 표 중개, 예약 메모리를 S231에 옮김. 인터럽트 전달 표에 880~889 추가(960은 SSD 중개용이라 별개). Windows를 처음부터 0이 아닌 가상 머신 번호로 실행(그쪽의 미해결 가설을 미리 피함). 우리 Mu MacMini2020Pkg에 예약 메모리와 J274용 AGX0 ACPI 추가 | **Opus** (하이퍼바이저·인터럽트·메모리 보호 핵심부) | Windows가 AGX0를 가진 채 전처럼 부팅. 보드 번호·계약서만 고친 그쪽 드라이버가 장치 시작 단계(전원 켜기·조회·끄기)까지 가고 SSD 드라이버(stornvme) 재설정 0회 |
| P6 (그리기 전용) | 드라이버가 펌웨어를 맡고, 그쪽 D3D12 시험 묶음 119개(GPU_DRIVER_ARCHITECTURE.md §2.6) 실행. 화면은 기본 디스플레이 드라이버로 HDMI에 계속 출력 | Opus → 안정되면 Sonnet | 시험 통과, GPU 응답 없음 재설정(TDR) 0회, 시스템 오류(SError) 0회, 1시간 연속 실행 |
| P7 (J274 화면 출력, **필수**) | 화면 장치 제어 코드를 "외부" disp0·HDMI용으로 고침. 밝기 조절 없음, 고정 해상도 표 대신 모니터가 알려 주는 해상도 목록(EDID), 케이블 뽑기/꽂기 처리 | Opus | 바탕화면 합성이 GPU로 모니터 기본 해상도에서 동작. 뽑았다 꽂아도 유지. **P7 없이는 J274에서 GPU 화면 출력 담당이 없음** |
| P8 (선택) | 보조 Mac 없는 독립 부팅으로 전환(그쪽도 아직 못 함) | — | — |

---

## 6. 위험과 모르는 점

| 항목 | 내용 | 구분 |
|---|---|---|
| 13.5 펌웨어 묶음 허락 | 9월 6일 지시와 충돌. 콘치님 결정 전에는 P1 이후 진행 불가 | [사실] |
| GPU가 J274에서 켜지는지 | 7월 기록은 화면 장치만 보여 줌. GPU 펌웨어가 미리 올라오는지는 기록 없음 | 모름 |
| HDMI 화면 장치 호출 순서 | 그쪽 13.5 호출 순서는 내장 패널 전제. HDMI는 다른 호출이 필요할 수 있음 | [가설] |
| GPU 재시작 | 전원을 완전히 끄고 켜야만 복구됨. 드라이버 재설정(TDR) 복구는 증명 안 됨. 우리 Mac mini는 WinPE·Windows를 재시작할 때마다 이미 전원 재설정이 일어남 | [사실] + [가설] |
| USB 문제와 겹침 | 그쪽 중개 코드는 그쪽 메모리 보호 배치를 전제. 우리 S231 쓰기 구역·DART 작업(USB 0 덩어리 문제)이 같은 메모리 보호·캐시 정리 코드를 건드림. 한 시험에 두 변수를 섞지 않아야 함 | [가설] |
| 전원·성능 설정 값 | J313 값 약 80개를 그대로 쓰면 팬이 있는 Mac mini에 맞지 않을 수 있음 | [가설] |
| 그쪽 성능 수치 | 모두 작성자 주장. LIMITATIONS.md와 README가 서로 맞지 않음 | [사실] |
| 0번 가상 머신 번호 가설 | 그쪽 실험(EXP2232)이 아직 대기 중이라 결론 없음 | [사실] |
| 7월 13.5 묶음 위치 | 지워진 WINTEST 영역에 있었을 것. 다시 만들어야 할 것 | [가설] |

---

## 7. 출처

우리 쪽:
- `NW/nwoas_scripts/logs/appark-chainload-1558.log:49` (Board-ID 0x22), :86-87 (OS FW 13.5, 시스템 펌웨어 mBoot-18000.121.3), :108-109 (0xbfffb8000 / 0xbfff70000), :117-125 (외부 화면, dart-dcp, rtkit(dcp) 부팅)
- 7월 13~14일 appark-chainload-*.log 파일 전체에 13.5 줄 28개. 모든 로그를 합치면 34개입니다. 11월 15일 로그에서도 같은 시스템 펌웨어 문자열이 537줄 이상 나옵니다.
- `NW/NWOAS-STATUS-2026-07-12.md:14,27` (13.5 화면 장치 조합, Windows 설치 화면 표시)
- `NW/NWOAS-STATUS-2026-09-06.md:155` (13.5 복원 SUPERSEDED), :165-169 (Tahoe HDMI 화면 장치 초기화 실패), :173 (Tahoe 유지 지시)
- `NW/nwoas_scripts/plan-20261115/dualboot.md` §2.4, §3.2, §3.4:143-149
- `NW/nwoas_scripts/plan-20261115/gpu.md` §2.3, §2.4, §4
- `NW/m1n1-layout-s231/src/main.c:169-172` (mBoot-18000.121.3일 때만 화면 초기화 미룸)
- `NW/m1n1-layout-s231/proxyclient/experiments/agx_boot.py:11`
- `NW/m1n1-layout-s231/proxyclient/m1n1/constructutils.py:804`

그쪽(windows-on-m1, MIT, 읽기 전용):
- `WOM/README.md:22-25`, :25, :73-90, :103-104, :122-123
- `WOM/documentation/LIMITATIONS.md:18`, :24-27
- `WOM/documentation/INSTALL.md:44-90`, `WOM/documentation/RUN.md:139-142`
- `WOM/documentation/AGX_BRINGUP.md:29-46`, :102-106, :162-176, :200
- `WOM/documentation/design/2026-08-26-j313-agx-g2-direct-firmware-ownership.md:5-12`, :54-66
- `WOM/investigation/GPU_CURRENT_STATE.md:31-39`, :36-39, :76-77; `EXPERIMENTS.md:74316`
- `WOM/config/j313-agx.json:7-9`, :12-20
- `WOM/mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgx.asl.inc:22-33`, :49
- `WOM/m1n1_windows/src/hv_agx_retained_platform.c:30-38`, :60-73, :76-86, :454, :457
- `WOM/m1n1_windows/src/hv_agx_gpuva_v5.c` (927줄), :728-835; `hv_agx_gpuva_v5_mmio.c:3-4`, :13-35
- `WOM/m1n1_windows/src/hv_agx_g2.generated.h:16`; `hv_agx_power_platform.c:16-28`
- `WOM/m1n1_windows/src/dcp.c:600-604`; `dcp_iomfb_v13_5_abi.c:13-14`; `dcp_iomfb_bootstrap.c:137,216,234,309`
- `WOM/m1n1_windows/src/hv_agx_scanout_broker.h:29-30`; `hv_agx_power_mmio.c:636-640`
- `WOM/m1n1_windows/src/firmware.c:23-43`
- `WOM/m1n1_windows/proxyclient/m1n1/constructutils.py:801`, :804, :882-887
- `WOM/tools/agx_m1n1_backend.py:117-121`; `tools/generate_agx_hwdata_profile.py:20-49`; `tools/generate_j313_agx_abi_admission.py:127-128`
- 클론 상태: 상위 저장소 6c65673(얕은 클론), `m1n1_windows` 하위 모듈 882985f
