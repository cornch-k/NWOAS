# S264 — 맥북 없이 켜기 4-3·4-4: C 게스트 실행기 (지금 판 s264B)

**상태: s264A는 실기 미리보기 비교 (가)를 1번 돌렸고(11월 19일 08:02, 게스트 시작 안 함) 손님 ADT 한 줄만 달랐습니다. 그 차이를 고친 s264B는 [빌드만]입니다 — 이 맥북에서 빌드와 호스트 시험만 했고 맥미니에서는 아직 실행하지 않았습니다.** 빌드 성공과 호스트 시험 통과는 부팅 성공이 아닙니다.

표기: **[사실]** = 파일:줄·기록·이 맥북에서 실제로 돌린 결과로 확인한 것, **[가설]** = 확인하지 않은 추측, **[빌드만]** = 빌드·호스트 시험까지만 된 것.
경로 줄임: `N/` = `/Users/angigyeom/Desktop/DEV/NWOAS/`, `HVPY` = `N/m1n1_windows/proxyclient/m1n1/hv/__init__.py`, `SRC/` = `N/m1n1-standalone-s264/src/`, `LOG0100` = `N/nwoas_scripts/logs/agxmed1g-s249-20261119-010040.QNzidN`(S259 실행기 실기 기록).

---

## 0. 결론

0. **[11-19 08:02 실기 (가) 결과와 s264B] s264A 실기 비교 11줄 중 10줄이 같았고, 손님 ADT만 달랐습니다. 원인은 C 계획이 ADT 뿌리에 `pmgr-u8id`를 무조건 하나 더 붙인 것입니다. 실기에서 하이퍼바이저가 읽는 ADT에는 체인로드 때 Python이 이미 이 속성을 써 넣어 두었으므로 C 결과에 같은 이름이 두 번 들어갔습니다(속성 23개, Python은 22개, 40바이트 차이). s264B는 이미 있으면 그 자리 값만 바꾸고 없을 때만 붙입니다. 호스트 시험을 "iBoot가 남긴 ADT"와 "체인로드 뒤 ADT" 두 상태로 늘렸고, 두 상태 모두 15/15 같음입니다.** 고치기 전 s264A를 새 시험에 넣으면 실기와 똑같은 DIFF 줄이 나옵니다. 자세한 것은 5.3장입니다. [사실: 실기 기록·이 맥북 실행. s264B 실기는 아직]
1. **맥북의 Python 실행기가 게스트를 띄우기 전에 하는 일을 C로 옮긴 하이퍼바이저를 만들었습니다.** 맥북은 "C 실행기를 불러라"라는 명령 하나(`P_HV_NWOAS_S264`, 0xc3e)만 보내면 됩니다. 바탕은 지금 쓰는 최신 트리 g262Q입니다. [빌드만]
2. **같은 입력을 넣으면 C가 만든 결과가 Python 실행기 결과와 바이트 단위로 같습니다(호스트 시험).** 실제 Python 코드(`HVPY` 의 `HV.init`·`load_raw`·`start`, 실제 `guest_map.py`·`pmgr_gate.py`·`guest_module.py`·`agx_hv2_module.py`, 소리 모듈 A단계)를 가짜 통신 위에서 돌린 결과와, 하이퍼바이저에 들어간 것과 같은 C 계획 코드를 비교했습니다. 15개 항목이 모두 같았고, 일부러 바꾼 값 5개는 모두 "다름"으로 잡혔습니다. [사실: 이 맥북에서 실행, 5장]
3. **C가 계산한 값이 실제 맥미니 기록과도 맞습니다.** 손님 ADT 크기 0x4f248 B, 메모리 배치 8개 값, 전원 관리 감시 주소 7개가 `LOG0100` 과 같습니다. [사실: 5장 표]
4. **부팅 중 맥북에 묻던 70번(레지스터 통과 14·무시 1, 전원 관리 읽기 39·쓰기 2, 보조 CPU 켜기 쓰기 14)을 모두 C 처리기로 바꿨고, 같은 이름의 계수기로 셉니다.** 실기에서 이 수가 그대로 나오는지는 모릅니다. [빌드만]
5. **첫 판에서 빠진 것**: 소리(A단계 창만 만들고 상태 0 → 드라이버가 시작하지 않음), NS2 작업 통로(NS1만 알림), 와이파이 켜기(제어 페이지는 "꺼짐"만 보고), 맥북 없는 상태의 예외 처리(4-5), 디스크에서 파일을 읽어 혼자 시작하는 경로(4-1·4-2). 7장에 전부 적었습니다.
6. **다음 실기 단계는 (나) C가 게스트 시작입니다**: `bash N/nwoas_scripts/standalone-s264/s264-launch-watch.sh` 한 줄이고 콘치님 손은 필요 없습니다(화면 확인만 콘치님 눈). 스크립트는 s264B 바이너리 지문으로 고정했습니다. s264B로 (가)를 다시 돌리면 ADT 줄까지 실기로 확인되고, 명령은 그대로 `s264-dryrun-compare.sh`입니다(8장).

---

## 1. 만든 것

결론: 하이퍼바이저 소스 새 사본 1벌, 새 패치 1개, 새 바이너리 1개, 시험·비교 도구 9개입니다. 기존 바이너리·패치·실행기는 하나도 고치지 않았습니다. [사실]

| 무엇 | 경로 |
|---|---|
| 하이퍼바이저 소스(새 사본, g262Q + S264) | `N/m1n1-standalone-s264/` |
| 패치 (g262Q → S264) | `N/port-j274/gpu/hv/hv2-g262Q-to-s264.patch` |
| 바이너리 / 디버그 ELF (첫 판, 실기 (가) 1번) | `N/port-j274/gpu/hv/out-hv2/m1n1-s264A-agx-hv2-standalone.bin` / `.elf` |
| 패치 (s264A → s264B, ADT `pmgr-u8id` 수정) | `N/port-j274/gpu/hv/hv2-s264A-to-s264B.patch` |
| 바이너리 / 디버그 ELF (지금 판) | `N/port-j274/gpu/hv/out-hv2/m1n1-s264B-agx-hv2-standalone.bin` / `.elf` |
| C 계획 (하드웨어를 건드리지 않음, 호스트에서도 컴파일됨) | `SRC/nwoas_s264_plan.c`, `SRC/nwoas_s264_plan.h` |
| C 실행 (프록시 명령, 처리기, 하드웨어 단계) | `SRC/nwoas_s264.c`, `SRC/nwoas_s264.h` |
| 기존 파일에 넣은 연결 지점 | `SRC/hv_exc.c`(레지스터 표), `SRC/hv_vm.c`(NS1만·NVMe 처리기 연결), `SRC/nwoas_s229_adapter.inc`(동작 27), `SRC/nwoas_nvmeqd.inc`(NS1만), `SRC/proxy.c`·`proxy.h`(0xc3e), `Makefile` |
| 재현 빌드 | `build-s264.sh <새 폴더>` |
| 호스트 시험 (지금 돌릴 수 있음) | `s264_offline_test.py`, `host/s264_plan_host.c` |
| 실기 (가) 비교 | `s264-dryrun-compare.sh` → `s264_compare.py` |
| 실기 (나) C가 게스트 시작 | `s264-launch-watch.sh` → `s264_launch.py` |
| 공통 (결과 해석·페이지 표 모형·비교) | `s264_common.py`, `s264_pyref.py` |

S264 명령을 부르지 않으면 동작은 g262Q와 같습니다. 새 레지스터 표는 C 실행기가 켤 때만 쓰이고(`nwoas_s264_traps_armed`), NS1만 규칙도 동작 27을 불렀을 때만 켜집니다. [사실: 코드. 실기 미확인]

---

## 2. 지금 맥북 실행기가 하는 일 전체 목록 (S259 기준)

결론: 45가지를 아래처럼 나눴습니다. 부팅에 필요한 것 중 첫 판에서 뺀 것은 소리 B단계·와이파이 켜기·그림자 레지스터·와이파이 켜기 명령 4가지입니다.

| 판정 | 개수 |
|---|---|
| 이번에 C로 옮김 (와이파이 확인처럼 일부만 옮긴 1개 포함) | 30 |
| 원래 C에 있음 (부르는 쪽만 옮김) | 4 |
| 첫 판에서 뺌 | 4 |
| 맥북 전용 (개발 통로) | 3 |
| 진단용 (읽기만, 옮기지 않음) | 3 |
| 첫 판 범위 밖 선택 기능 (S260·S262) | 1 |

실행기: `N/nwoas_scripts/agxdcp-s259-guest-test.sh:183-197` → `N/m1n1_windows/proxyclient/tools/run_guest.py`.
판정: **C로(이번)** = 이번에 C로 옮김 [빌드만] / **C(기존)** = 원래 C에 있고 부르는 쪽만 옮김 / **못 옮김** = 첫 판에서 뺌 / **맥북 전용** = 개발 통로 / **진단** = 읽기만 하는 확인.

### 2.1 하이퍼바이저 준비 (`HV.init`)

| # | 하는 일 | 근거 | 판정 | C 위치 |
|---|---|---|---|---|
| 1 | Python 힙 768 MiB를 맥북이 정하고 그 끝을 손님 메모리 시작으로 씀 | `run_guest.py:47`, `proxyutils.py:100-113`, `HVPY:2211` | C로(이번) | `s264_phys_base()` |
| 2 | 13.5 펌웨어·화면 확인(`fw135_preflight.py`) | `display-s244/fw135_preflight.py:28-38` | 진단 | — |
| 3 | 다른 USB 통로 끄기, 출력 통로 나누기 | `HVPY:1849-1865` | C로(이번) | 계획 `OP_IODEV` |
| 4 | `hv_init()` | `HVPY:1867` | C(기존) | `hv.c:64` |
| 5 | `/arm-io` 범위 전체를 그대로 연결 | `HVPY:1886-1889` | C로(이번) | 계획 `OP_MAP` |
| 6 | HACR·MDCR·MDSCR·AMX·VMKEY·APSTS 쓰기 | `HVPY:1910-1943` | C로(이번) | `do_msr()` |
| 7 | 가상 UART 연결 | `HVPY:1945, 1957-1964` | C(기존) | `hv_map_vuart` |
| 8 | ACTLR EnMDSB | `HVPY:1947-1953` | C로(이번) | `do_msr()` |
| 9 | ADT 고치기: 제품 이름 3개, USB0 관련 노드 18개 지우기 | `HVPY:2107-2156` | C로(이번) | `emit_node()` |
| 10 | ADT 뿌리에 `pmgr-u8id` 속성이 붙음(Python ADT 라이브러리가 붙임). 이미 있으면 그 자리 값만 바뀜(s264B) | `adt.py:813, 622-629, 827-830` | C로(이번) | `root_extra_val` / `w.set` |

### 2.2 손님 메모리 배치 (`load_raw`)

| # | 하는 일 | 근거 | 판정 |
|---|---|---|---|
| 11 | ADT·TrustCache·펌웨어·SEPFW·preoslog·부트 인자 자리 계산 | `HVPY:2187-2216` | C로(이번) |
| 12 | RAM 아래쪽·위쪽 연결, 보호 영역(carveout) 3개 빼기 | `HVPY:2222-2226, 2166-2177` | C로(이번) |
| 13 | 펌웨어 묶음(33,669,120 B) 복사 | `HVPY:2228-2230` | C로(이번) — (나)에서는 맥북이 힙에 올린 것을 C가 복사 |
| 14 | SEPFW·TrustCache·preoslog 복사 | `HVPY:2232-2240` | C로(이번) |
| 15 | ADT `memory-map` 6개 값 바꾸기 | `HVPY:2242-2250` | C로(이번) |
| 16 | `__OS_LOG` 구간 지우기(aop, iop-aop-nub) | `HVPY:2252-2266` | C로(이번) |
| 17 | 부트 인자 쓰기 | `HVPY:2268-2283` | C로(이번) — 아래 주의 |
| 18 | 보조 CPU 7개 시작 주소(RVBAR) 쓰기 | `HVPY:2285-2292` | C로(이번) |
| 19 | PMU 패닉 횟수 지우기(SPMI 쓰기 1번) | `run_guest.py:103`, `hw/pmu.py:28-31`, `hw/spmi.py:60-75` | C로(이번) |

주의 [사실]: m1n1 C의 `struct boot_args`는 명령줄 시작이 112번째 바이트인데 Python과 XNU 형식은 108번째입니다(`SRC/xnuboot.h:19-47`을 호스트에서 `offsetof`로 확인, Python `tgtypes.py:58-80`). 그래서 C 실행기는 C 구조체를 쓰지 않고 Python 형식 그대로 바이트를 만듭니다. 참조 프로젝트(`N/external/windows-on-m1/m1n1_windows/src/hv_autonomous_runtime.c:208, 284`)처럼 구조체를 통째로 복사하면 명령줄이 4바이트 밀립니다.

### 2.3 실행기 모듈 (`-m`, 순서대로)

| # | 모듈 | 하는 일 | 근거 | 판정 |
|---|---|---|---|---|
| 20 | `cpufreq-s193/capture.py` | 클러스터 상태 읽기 | `:10-29` | 진단 |
| 21 | `usb-s174/guest_map.py` | `pcie_init`, 에뮬레이션 범위 3개, 아래쪽 4 GiB 창 | `:7-19` | `pcie_init` C(기존), 나머지 C로(이번) |
| 22 | `smp-s133/pmgr_gate.py` | 보조 CPU 켜기 쓰기를 막고 PSCI를 기다림 | `:18-30` | C로(이번) |
| 23 | `layout-s231/guest_module.py` | j274 확인, carveout 표, IRQ 900 겹침 확인, 메모리 크기 줄이기, `nvme_init`, 손님 주소 등록, GPT 디스크 GUID·슬롯 3·보호 칸 4개·쓰기 구역 확인 | `:21-35, 43-55, 106-108, 159-196` | C로(이번) (`s264_gpt_check`는 읽기만) |
| 24 | 같은 파일 | Python NVMe 제어기와 ECAM/BAR 감시 | `:199-285` | C(기존): S230부터 C가 주인 |
| 25 | `memory-s180/link_module.py` | NS2 가상 디스크(작업 통로) | 파일 전체 | 맥북 전용 → C 실행기는 NS1만 알림 |
| 26 | `file-delivery-s243/module.py` | NS2로 파일 전달 | 파일 전체 | 맥북 전용 |
| 27 | `layout-s231/module.py` | 정책 확인, 빠른 경로 능력(23)·켜기(16), 예상 밖 접근 감시 | `:7-28` | C로(이번): 동작 27(NS1만) + C 처리기 |
| 28 | `tools-hv2/agx_hv2_module.py` | GPU 중개 설정 512 B 만들어 보내기 | `:42-69` | C로(이번) (받는 `nwoas_agx_configure`는 기존) |
| 29 | `native-s230/query_module.py`, `cpufreq-s189/observe.py` | 읽기 전용 질의 | 파일 전체 | 진단 |
| 30 | `sound-s256/audio_module.py` A단계 | 창 0x340000000+0x40000, 머리말(상태 0) | `:125-199` | C로(이번) |
| 31 | 같은 파일 B단계 | 전원·클럭·DMA·앰프 준비 | `:202-` | 못 옮김 → 소리 없음 |
| 32 | `wifi-s259/wifi_module.py` 확인 | wlan 정보(보드·안테나·MAC·보정값), dart-apcie1 주소, BAR 범위 | `:35-87` | 일부 C로(이번): `wifi_probe` 사실 검사·dart-apcie0 검사는 안 옮김 |
| 33 | 같은 파일 | 제어 페이지 감시 | `:96-97` | C로(이번): NWOAS_WIFI=0 내용(꺼짐)만 |
| 34 | 같은 파일 | BAR 에뮬레이션 범위 | `:100-104` | C로(이번) |
| 35 | 같은 파일 + `nwoas_wifi_s259.py:355-` | 부팅 때 강제 끄기(SMC), Windows 요청 때 켜기 | `:92-93` | 못 옮김 → 와이파이 꺼짐 |
| 36 | `power-s260`, `nvmeqd-s262` 모듈 | S260·S262 선택 기능 | — | 첫 판에 없음(g260O 기본 경로) |

### 2.4 게스트 진입 (`hv.start`)

| # | 하는 일 | 근거 | 판정 |
|---|---|---|---|
| 37 | 통로 끄기·나누기 | `HVPY:2403-2414` | C로(이번) |
| 38 | 전원 관리 감시 7곳, 고정값 2곳, CPU_START 감시 | `HVPY:1966-2087` | C로(이번) |
| 39 | 페이지 표 반영 + TLB 비우기 | `HVPY:381-452` | C로(이번): 같은 최종 상태를 직접 만듦 |
| 40 | ADT 만들어 올리기 | `HVPY:2422-2424` | C로(이번) |
| 41 | 화면 `fb_improve_logo`·`fb_shutdown(True)` | `HVPY:2436-2442` | C(기존), 부르는 쪽 C로 |
| 42 | SPRR·GXF 켜기, `hv_start` | `HVPY:2444-2465` | SPRR·GXF C로(이번), `hv_start` C(기존) |

화면(DCP)은 이미 C입니다. 13.5 펌웨어에서 m1n1 C가 화면을 켜고 하이퍼바이저 DCP 서비스(S251)가 이어받습니다(`research-20261117/roadmap/standalone-boot.md` 0장 1번). [사실: 문서와 코드. 맥북 없는 상태에서는 미확인]

### 2.5 게스트 실행 중 맥북으로 넘어가는 것

| # | 경로 | 근거 | S259 01:00 실기 횟수 | 판정 |
|---|---|---|---|---|
| 43 | NS2 명령, 매 부팅 GPU 다시 켜기 작업(D28) | `SRC/hv_vm.c:1448`, `agx-s248/jobs/D28-rearm.cmd` | 기록에 안 찍힘 | 맥북 전용 → NS1만 모드에서는 "없는 네임스페이스"로 답함 |
| 44 | Python 그림자 레지스터(MDSCR·디버그 레지스터·VMSA_LOCK), 처리 못 한 예외 | `HVPY:755-799, 808-822` | 0 | 못 옮김 → 4-5 |
| 45 | 와이파이 켜기 명령 처리 | `nwoas_wifi_s259.py:587-597` | (와이파이 켬 상태에서만) | 못 옮김 |

---

## 3. 부팅 중 Python 처리기 → C 처리기

결론: 실기 기록의 70번을 모두 C로 처리하고, 같은 종류로 셉니다. 기대값은 Python 기록과 같은 수입니다. [사실: 기록 수. C 수는 빌드만]

| 종류 | Python 처리 | Python 기록 수 (`LOG0100`) | 02:40 기록 수 | C 처리기 | C 계수기 |
|---|---|---|---|---|---|
| 레지스터 통과 (OSLAR, HID5, EHID9/10/20, ACC_CFG 읽기) | `HVPY:1110-1129` | 14 | 합계 70 (같은 구성) | `hv_exc.c` S264 표 | `msr_pass` |
| 레지스터 무시 (ACC_CFG 쓰기) | `HVPY:819-829` | 1 | | 같은 표 | `msr_skip` |
| 전원 관리 상태 읽기 | `HVPY:1975-1979` | 39 | | `s264_hook_pmgr` | `pmgr_r` |
| 전원 관리 상태 쓰기 | `HVPY:1970-1973` | 2 | | 같은 처리기 | `pmgr_w` |
| 보조 CPU 켜기 쓰기 | `HVPY:2056-2087` | 14 | | `s264_hook_cpustart` | `cpustart_w` |
| 그중 막은 CPU 켜기 | `pmgr_gate.py:18-27` | 7 | | 같은 처리기 | `gated` |
| CPU 상태 읽기 | `HVPY:2043-2054` | 0 | | `s264_hook_cpustate` | `cpustate_r` |
| 전원 게이트 고정값 읽기 | `HVPY:2019-2026` | 0 | | `s264_hook_pg` | `pg_r` |

C는 손님이 처음 NVMe에 닿을 때(UEFI 저장장치 시작, 게스트 m1n1 앞부분이 끝난 뒤) 한 줄을 찍습니다:
`HVLOG: S264 COUNTERS (first guest NVMe access) msr_pass=… msr_skip=… pmgr_r=… pmgr_w=… …`

---

## 4. C 실행기 동작 방식

결론: 먼저 하드웨어를 건드리지 않는 "작업 목록"(94개)을 만들고, 미리보기면 그 목록만 돌려주고, 실행이면 목록을 차례로 실행한 뒤 게스트로 들어갑니다. 미리보기와 실행이 같은 목록을 쓰므로 미리보기 비교가 곧 실행 내용 비교입니다. [사실: 코드]

| 하위 명령 (`P_HV_NWOAS_S264`, 인자 = 명령, a0..a3) | 하는 일 |
|---|---|
| 0 질의 | `0x5332363400000001` 돌려줌 (다른 하이퍼바이저면 비교 도구가 멈춤) |
| 1 미리보기 (설정 주소, 크기, 결과 주소, 크기) | 목록·ADT·부트 인자·GPU 설정·소리 머리말을 결과에 씀. MMIO·레지스터 쓰기·페이지 표 변경 없음. ADT는 Python 힙 맨 위 8 MiB 자리(손님 RAM 밖, 비어 있음)에서 조립 |
| 2 실행 (같은 인자) | 목록 실행 후 `hv_start`. 게스트가 끝날 때까지 돌아오지 않음. 실패하면 음수와 결과(단계별 상태) |
| 3 계수기 | 3장 계수기 복사 |

작업 목록의 종류 (오프라인 입력 기준, 실기에서는 carveout 수 등에 따라 달라질 수 있음) [사실: 오프라인 결과 묶음을 세어 봄]:

| 종류 | 개수 |
|---|---|
| 주소 연결 (MAP) | 22 |
| 통로 설정 (IODEV) | 20 |
| 단계 (STEP: hv_init, NVMe, GPU 설정, 화면, hv_start 등) | 16 |
| 감시 (HOOK: 전원 관리 7, NVMe 2, 고정값 2, 와이파이 1, CPU_START 1) | 13 |
| 레지스터 쓰기 (MSR) | 10 |
| 64비트 쓰기 (보조 CPU 시작 주소 7, 메모리 크기 1) | 8 |
| 복사 | 4 |
| 가상 UART | 1 |
| 합계 | 94 |

설정 기본값 = S259 실행기 환경입니다(`s264_default_config`): GPU 중개 mediate, 로컬 예약 IPA 0xa00000000, 기본 P상태 3, 스캔아웃 vINTID 889, DCP 서비스 켬, 창 0..4 GiB·어긋남 0x34000, 최대 전송 1 MiB(mdts 8), NS2 없음.

---

## 5. 같은지 확인하는 도구와 결과

결론: 호스트 시험은 s264B로 두 ADT 입력 상태 모두 통과했습니다. 실기 비교 (가)는 s264A로 1번 돌려 ADT 한 줄이 달랐고(5.3장), s264B 실기 비교는 아직입니다.

### 5.1 호스트 시험 (지금 돌릴 수 있음, 맥미니 안 건드림)

명령:
```
/Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows/.venv-hv/bin/python3 -B /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/standalone-s264/s264_offline_test.py
```
입력: J274 ADT 덤프 `N/nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin`(sha256 32e63706…, iBoot-8422.141.2), 부트 인자·힙 위치·carveout은 `LOG0100` 값, GPT는 `guest_module.py`의 보호 칸으로 만든 합성 디스크.

s264B부터는 같은 시험을 ADT 입력 두 상태로 돌립니다 [사실: `s264_offline_test.py` `chainload_state`]:

| 상태 | 무엇 | 뿌리 속성 수 | 언제 쓰이는가 |
|---|---|---|---|
| `iboot` | 위 덤프 그대로 | 21 | 나중에 맥북 없이 켤 때(iBoot가 넘긴 ADT) |
| `chainload` | 덤프를 Python `load_adt()` → `build()` 한 바이트를 같은 0x58000 자리에 덮어쓴 것(`chainload.py:106` `u.push_adt()`, `proxyutils.py:300-305`) | 22 (`pmgr-u8id` 포함) | 지금의 실기 (가)·(나): 맥북이 체인로드한 뒤 하이퍼바이저가 읽는 ADT |

두 상태 모두 아래 15항목이 같아야 하고, 추가로 C 손님 ADT의 한 노드 안에 같은 이름 속성이 두 번 있으면 실패로 봅니다(일부러 뿌리 속성 하나를 두 번 넣은 ADT로 이 검사가 잡히는지도 확인). 체인로드가 바꾸는 memory-map·보조 CPU 시작 주소 값은 길이가 같고 C와 Python이 같은 바이트를 읽으므로 재현하지 않았습니다.

결과(2026-11-19 이 맥북에서 실행, s264A 첫 판 기준 표. s264B 결과는 5.3장) [사실]:

| 항목 | 결과 | 내용 |
|---|---|---|
| 손님 ADT 바이트 | 같음 | 324,168 B |
| 부트 인자 바이트(메모리 크기 줄인 뒤) | 같음 | 736 B (부트 인자 판 2) |
| 2단계 주소 연결(정규화한 구간 목록) | 같음 | 48 구간 |
| 레지스터 쓰기(레지스터마다 마지막 값) | 같음 | 10개 |
| 보조 CPU 시작 주소 쓰기 | 같음 | 7번 |
| 통로 설정 순서 | 같음 | 20번 |
| 복사(펌웨어 묶음, SEPFW, TrustCache, preoslog) | 같음 | 4번 |
| GPU 중개 설정 512 B | 같음 | |
| 소리 머리말 0x4c B | 같음 | |
| 게스트 진입 주소 / x0 | 같음 | 0x83d564800 / 0x83fb5c000 |
| PMU SPMI 쓰기 | 같음 | 2번 |
| 와이파이 제어 페이지 0x0-0x1ffc | 같음 | 2048 단어 |
| 전원 관리 감시 주소 vs `LOG0100` | 같음 | 0x23b7001c0, 1c8, 220, 270, 420, 0x23d280088, 098 |
| 메모리 배치 vs `LOG0100` | 같음 | phys_base·guest_base·부트 인자·진입·창 뒷받침·메모리 크기 2개·이미지 크기 = 8개 |
| Python `hv.entry`/`guest_base` | 같음 | |

일부러 바꾼 값 5개(ADT 1바이트, 메모리 크기 값, 전원 관리 감시 1개 빼기, HACR 값, BAR 범위를 직접 연결로)는 모두 "다름"으로 잡혔습니다. [사실]
같은 ADT 크기 0x4f248은 실기 기록 `LOG0100`의 `Uploading ADT (0x4f248 bytes)`와 같습니다. [사실]

이 시험의 한계 [사실]: 가짜 통신 위에서 돌렸습니다. `link_module.py`·`layout-s231/module.py`·와이파이 모듈은 같은 연결 효과만 재현했고(`s264_pyref.py` `layout_module_effect`, `run_wifi_disabled`), C의 GPT 확인 함수(`s264_gpt_check`)와 하드웨어 단계는 호스트에서 돌리지 않았습니다.

### 5.2 실기 비교 (가) — 한 명령

```
bash /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/standalone-s264/s264-dryrun-compare.sh
```
같은 부팅 안에서 ① C 미리보기 ② 실제 S259 Python 실행기 경로(실제 모듈 전부, 단 소리는 A단계만, NWOAS_WIFI=0, 펌웨어 묶음 내용은 올리지 않음)를 돌리고, `hv.start`의 통로 변경·화면 끄기·SPRR/GXF·`hv_start`만 막은 채 기록해 비교합니다. 결과: `N/nwoas_scripts/logs/s264-compare-<시각>/compare.md`. s264B부터는 두 손님 ADT도 `c-adt.bin`·`py-adt.bin`으로 남깁니다(08:02 실행에는 C 결과 묶음만 남아 Python ADT 바이트는 없음).

### 5.3 실기 (가) 1번째 결과 (s264A, 2026-11-19 08:02)와 s264B 수정

결론: 손님 ADT만 달랐고, 원인은 C가 뿌리 `pmgr-u8id`를 중복으로 붙인 것이며, s264B에서 고쳐 호스트 시험 두 상태 모두 15/15 같음입니다. [사실]

기록: `N/nwoas_scripts/logs/s264-compare-20261119-080223/`(`compare.md`, `compare.json`, `c-result.bin`)과 같은 이름의 `.log`.

| 항목 | 실기 결과 |
|---|---|
| 같음 | 10줄 (부트 인자 736 B, 2단계 주소 연결 48구간, 레지스터 10개, 보조 CPU 시작 주소 7번, 통로 설정 20번, 복사 4번, GPU 중개 설정, 소리 머리말, 진입 주소 0x83d190800 / x0 0x83f788000, PMU SPMI 쓰기 2번) |
| 다름 | 1줄: 손님 ADT 길이 C 324,208 B / Python 324,168 B, 첫 차이 +0x0(뿌리 속성 수 C 0x17 / Python 0x16) |

실기 비교 표는 11줄입니다. 호스트 시험의 나머지 4줄(와이파이 제어 페이지, 기록과 맞추는 3줄)은 실기 도구에 없습니다.

실기 C ADT 뿌리 속성(`c-result.bin`을 풀어 확인) [사실]:

| 순서 | 이름 | 길이 (B) | 값 |
|---|---|---|---|
| 0~20 | iBoot 속성 21개 (`regulatory-model-number` … `#size-cells`) | | |
| 21 | `pmgr-u8id` | 4 | 0 — 입력 ADT에 이미 있던 것 |
| 22 | `pmgr-u8id` | 4 | 0 — C가 새로 붙인 것 (속성 1개 = 이름 32 + 길이 4 + 값 4 = 40 B) |

왜 생겼나 [사실: 코드·기록]:
1. Python `load_adt()`는 `pmgr_init()`에서 `self.pmgr_u8id = …`를 합니다(`N/m1n1_windows/proxyclient/m1n1/adt.py:813, 829-830`). `ADTNode.__setattr__`가 이것을 뿌리 속성 사전 `_properties["pmgr-u8id"]`에 넣습니다(`adt.py:622-629`). 사전이므로 이미 있으면 값만 바뀌고 순서·개수는 그대로, 없으면 맨 뒤에 붙습니다. 다시 읽으면 4바이트 정수로 읽히고 같은 바이트로 다시 만들어집니다(이 맥북에서 확인).
2. 실기 (가)의 `chainload.py -r`가 106번째 줄에서 `u.push_adt()`를 부릅니다. 이것은 `load_adt()`로 읽은 ADT를 `build()`해 원래 자리에 다시 씁니다(`proxyclient/m1n1/proxyutils.py:300-305`). 기록에 `Pushing ADT (354044 bytes)...`가 찍혔습니다. 덤프 원본을 같은 식으로 만들면 354,044 B이고 원본보다 40 B 큽니다. 그래서 S264 하이퍼바이저가 읽은 ADT 뿌리에는 이미 `pmgr-u8id`가 있었습니다(속성 22개).
3. Python 실행기(`HV.init`, `N/m1n1_windows/proxyclient/m1n1/hv/__init__.py:1843`)는 이 ADT를 다시 `load_adt()` 합니다 → 값만 바뀌고 22개. C 계획 s264A는 입력에 있는지 보지 않고 뿌리 끝에 항상 하나 더 붙였습니다(s264A `SRC/nwoas_s264_plan.c` 363줄 `pc + 1`, 397-401줄 `out_prop_named(w, "pmgr-u8id", …)`, 951-960줄 `w.root_extra_val = u8id ? 1 : 0`) → 23개.
4. 호스트 시험은 iBoot 덤프(뿌리 21개, `pmgr-u8id` 없음)만 넣었기 때문에 두 쪽 모두 1개씩 붙여 같았습니다. 덤프 파일은 체인로드 전에 뜬 것이고, 실기 입력은 체인로드 뒤 것이라 달랐습니다.

고친 것 (s264B, `hv2-s264A-to-s264B.patch`, `SRC/nwoas_s264_plan.c` 한 곳) [사실: 코드]: 입력 뿌리에 `pmgr-u8id`가 있으면 기존 바꿔치기 표(`w.set`)에 넣어 그 자리 값만 새 값(4바이트, 원래 플래그 유지)으로 쓰고 끝에 붙이지 않습니다. 없으면 전처럼 끝에 붙입니다.

확인 [사실: 이 맥북에서 실행]:

| 시험 | `iboot` 상태 | `chainload` 상태 |
|---|---|---|
| 고치기 전 s264A 소스 | 15/15 같음 | 14/15 — ADT 다름, 실기와 같은 문구(C 324,208 / Python 324,168, 0x17 / 0x16), 중복 `pmgr-u8id` 잡힘, C 결과 묶음 330,304 B(실기와 같은 크기) |
| s264B 소스 | 15/15 같음, 중복 없음 | 15/15 같음, 중복 없음, ADT 324,168 B |
| 일부러 바꾼 값 5개 + 뿌리 속성 중복 1개 | 6개 모두 잡힘 | 6개 모두 잡힘 |

- s264B의 `iboot` 상태 C 결과 묶음과 ADT 지문은 s264A와 같습니다(`5b5c07cf…`, `b2b83b98…`). 즉 체인로드가 없는 경우의 출력은 바뀌지 않았습니다.
- 실기 C ADT에서 C가 붙인 22번 속성 하나를 빼면 324,168 B로 실기 Python 길이와 같습니다. Python ADT 바이트는 남아 있지 않아 바이트 비교는 하지 못했습니다.
- (나) 실행도 같은 C 계획을 쓰므로 s264A로 (나)를 했다면 Windows가 받는 ADT에 `pmgr-u8id`가 두 번 들어갔을 것입니다. 그것이 부팅에 영향을 주었을지는 모릅니다. [가설]

---

## 6. 빌드와 지문

결론: 같은 패치로 새 폴더에서 처음부터(g262Q → s264A → s264B) 다시 빌드해 같은 지문이 나왔습니다. 경고 44개는 g262Q와 같은 목록이고 s264B도 s264A와 같은 목록입니다(새 경고 0). [사실]

| 파일 | 크기 (B) | sha256 |
|---|---|---|
| `m1n1-s264A-agx-hv2-standalone.bin` | 2,408,448 | `178a004f963d424f17a9fbac1662c1ffe1d1dd24045a464ffffccf266de46c11` |
| `m1n1-s264A-agx-hv2-standalone.elf` | 3,787,208 | `8f7491f1d1b0896ac47a23a99b203160a66cd625201be8bb98427940c915d19c` |
| `hv2-g262Q-to-s264.patch` | 103,499 | `220611a7d7a21138cba9810c87057c63367dffc6204c245cf943aedbb5e39ede` |
| `m1n1-s264B-agx-hv2-standalone.bin` (지금 판) | 2,408,448 | `a677658782f48367d32071325f137c8fcae02a9a38f62f2f5b40ac1304d7d927` |
| `m1n1-s264B-agx-hv2-standalone.elf` | 3,787,616 | `1ed22207a624d50df7dab3584baafe079f6d39d518dadb43d6f37e23f57d7265` |
| `hv2-s264A-to-s264B.patch` | 2,389 | `26ceeddb2833d2750073d9141b3cf598dbd62f04331b39c6c3c1445104cf9814` |
| 바탕 g262Q (재현 확인) | 2,326,528 | `6a1ce6d4519146ae66940a5296803e44cf8dee65710956322ba20e778cca25d2` |
| 오프라인 C 결과 묶음 | 330,264 | `5b5c07cf12ecddbc2b94ebbadbd500b7da77735d3d3b12f75d59c435107956d3` |
| 오프라인 손님 ADT | 324,168 | `b2b83b988c2c1accf84e03714d18fb3fc75dc534f7840459aa49fc695fd2b264` |

다시 빌드: `bash /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/standalone-s264/build-s264.sh <없는 새 폴더>` (g262Q 재현 → 패치 → `buildvar.sh … s264A NWOAS_AGX=1` → 지문 비교 → s264B 패치 → `buildvar.sh … s264B NWOAS_AGX=1` → 지문 비교 → s264B 소스로 호스트 시험 두 상태). 2026-11-19 08:26 이 맥북에서 끝까지 돌려 두 지문이 모두 같고 호스트 시험 PASS였습니다. [사실]

이 바이너리는 ESP에 둘 2단계 파일 후보입니다. 스텁에 등록할 1단계 파일 크기 제한(D4 2,097,152 B 이하)은 이 파일에 해당하지 않습니다. [가설: 2단계 크기 한도는 힙 여유에 달림, `standalone-boot.md` 1단계 위험]

---

## 7. 아직 안 된 것 (스텁)

| 항목 | 지금 C가 하는 일 | Windows에서 보일 것 [가설] | 옮길 단계 |
|---|---|---|---|
| 소리 B단계 | 창만 연결, 머리말 상태 0 | NWAU0001 드라이버가 시작하지 않음, 소리 없음 | 9단계 |
| 와이파이 켜기 | 제어 페이지가 "꺼짐(DISABLED)" | `WLN0._STA`가 장치를 숨김 (`wifi_module.py:7-9`) | 별도 |
| 와이파이 ADT 사실 검사 | wlan 정보·dart-apcie1·BAR 범위만 | — | 별도 |
| NS2 작업 통로 | 알리지 않음, nsid 2 명령은 "없는 네임스페이스" | 두 번째 디스크 없음, 맥북 작업·파일 전달·GPU 다시 켜기(D28) 안 됨 → GPU 드라이버 첫 시작 실패 가능, 기본 화면 | 4-7 / Windows 작업 T1(콘치님 결정 대기) |
| 맥북 없는 상태의 예외 | 지금처럼 맥북으로 넘김 | 맥북이 없으면 멈춤 | 4-5 |
| 혼자 시작(디스크에서 파일 읽기, 묶음 붙이기, 3초 대기 창) | 프록시 명령으로만 시작 | — | 4-1·4-2 |
| 진단 모듈(capture, query, observe, preflight) | 안 함 | — | 필요 없음 |

---

## 8. 실기 검증 순서 (안전한 것부터)

결론: 다음 실기는 (나) `s264-launch-watch.sh`입니다. (가)는 s264A로 1번 돌려 ADT 한 줄만 달랐고(5.3장), 그 원인을 호스트에서 같은 문구로 재현한 뒤 s264B에서 고쳤습니다. s264B로 (가)를 다시 돌리면 ADT 줄까지 실기로 확인됩니다(게스트를 시작하지 않으므로 더 안전한 순서). (다)는 오늘 하지 않습니다. 모두 콘치님 손은 필요 없습니다((나)의 화면 확인만 콘치님 눈). 두 스크립트 모두 s264B 바이너리(2,408,448 B, `a6776587…`)와 도구 지문으로 고정했고, 이 맥북에서 고정값이 실제 파일과 맞는지 확인했습니다. [사실]

### (가) 체인로드 + C 미리보기 비교 — 게스트 시작 안 함

- 명령: `bash /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/standalone-s264/s264-dryrun-compare.sh`
  - 앞부분은 S259 실행기와 같습니다: 통로 확인 → `proxy_ready.py` → `probe_fw135_s244.py`(D4 확인, 실패하면 아무것도 올리지 않음) → `chainload.py -r m1n1-s264B…bin`(크기·지문 고정) → 진단 출력 기록 → `s264_compare.py`.
- 통과 기준:
  1. `compare.md`의 모든 줄이 SAME, 종료 코드 0.
  2. 진단 출력에 `HVLOG: S264 dry-run plan err=0 ops=…`.
- 실패 때:
  - `STOP: running hypervisor is not S264` → 체인로드가 안 된 것. 기록의 `chainload` 부분 확인, 다시 실행.
  - `C dry-run returned …` 음수 → `-(100+n)`이면 계획 오류 n(`nwoas_s264_plan.h` `S264_E_*`, 예: 7 = 전원 관리 표, 9 = wlan 정보). 결과 묶음이 없으니 진단 출력 확인.
  - DIFF 줄 → 그 줄의 "only C / only Python" 내용으로 원인을 찾고 C를 고칩니다. 하드웨어 변경은 없었으므로 되돌릴 것 없음.
- 끝나면: 맥미니는 하이퍼바이저만 초기화된 채 프록시에 있습니다. **다음 시험 전에 재시동**합니다(평소 방법).

### (나) 체인로드 + C가 게스트 시작, 맥북은 지켜보기만

- 조건: (가)에서 ADT 말고 다른 줄은 모두 같았음(s264A, 08:02). s264B의 ADT 줄은 호스트 시험 `chainload` 상태로만 확인했습니다. [사실]
- s264B와 맞는지 [사실: 이 맥북에서 확인]: `HV=…m1n1-s264B-agx-hv2-standalone.bin`, 크기·지문 고정값이 실제 파일과 같음, `s264_common.py`·`s264_pyref.py`·`s264_launch.py` 지문 확인 통과, `bash -n` 통과. `s264_launch.py`는 ADT를 따로 만들지 않고 하이퍼바이저 안 C 계획을 그대로 쓰므로 이번 수정이 그대로 적용됩니다.
- 명령: `bash /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/standalone-s264/s264-launch-watch.sh`
  - 맥북이 펌웨어 묶음을 Python 힙에 올리고(앞·가운데·끝 4 KiB 다시 읽어 확인) 실행 명령 하나만 보냅니다. 그 뒤로 맥북으로 넘어오는 사건은 기존 처리기가 처리하되 모두 셉니다.
- 통과 기준:
  1. 진단 출력에 `HVLOG: S264 C launcher: … ops`, `[S230] target owns PCI/control/admin; NS1 only (S264, no NS2)`, `HVLOG: S264 entering guest`.
  2. `HVLOG: S264 COUNTERS (first guest NVMe access)` 줄의 수가 3장 기대값과 같음(14·1·39·2·14·7).
  3. HDMI에 UEFI 로고 → Windows 바탕화면(콘치님 눈).
  4. 바탕화면까지 `[S264-watch] forwarded to the MacBook` 줄이 0개.
  5. Windows 디스크 오류 이벤트(7·51·129·153) 0건 — 다음 맥북 연결 부팅(S259 실행기) 때 작업 통로로 확인.
- 예상되는 차이 [가설]: 두 번째 디스크(NS2) 없음, 와이파이 장치 없음, 소리 없음, GPU 다시 켜기 작업이 안 가므로 기본 화면일 수 있음.
- 실패 때:
  - 실행 명령이 음수로 돌아옴 → `s264_launch.py`가 단계별 상태를 찍습니다(예: `GPT_CHECK: FAIL 0x5` = 슬롯 3이 기록된 두 값이 아님). 게스트는 들어가지 않았으므로 재시동만 하면 됩니다. GPT 확인은 읽기만 합니다.
  - 바탕화면 전에 멈춤 → 진단 출력의 마지막 `S264`·`HVLOG` 줄과 계수기 줄을 봅니다. 맥북으로 넘어온 사건이 있으면 그 종류(예: `EXC_SYNC`)가 찍힙니다. 전원을 10초 눌러 끄고, 평소 S259 실행기로 돌아갑니다(파일은 하나도 바뀌지 않았음).
  - 디스크 오류가 보이면 그 자리에서 중단하고 S259 실행기로 `chkdsk C: /scan`(읽기만).

### (다) 1TR 등록 — 오늘 하지 않음

- (가)·(나)가 실기에서 통과하고, 4-1(디스크에서 다음 파일을 읽는 1단계 m1n1)·4-2(3초 맥북 대기 창)·4-5(맥북 없는 예외 처리)를 만든 뒤에만 합니다. 절차와 13.5 복구 환경에 있는 명령 목록은 `N/ROADMAP-2026-11-17.md` 2.2와 `N/research-20261117/roadmap/standalone-boot.md` 1단계에 있습니다. 직전에 Asahi 이슈 #441·#404·#425 상태를 다시 봅니다.

---

## 9. 위험 [모두 가설]

1. 미리보기 비교의 레지스터 값: C는 `hv_init` 전에 AMX·ACTLR 현재 값을 읽고 Python은 `hv_init` 뒤에 읽습니다. `hv_init`이 이 둘을 바꾸지 않는 것은 코드로 확인했지만(`SRC/hv.c:64-157`), 다르면 (가)에서 DIFF로 보입니다.
2. 보조 CPU 켜기 막기는 Python과 같은 방식이지만 C에서 처음 돕니다. 잘못되면 9월의 이중 시작 멈춤과 같은 증상일 수 있습니다.
3. CPU 상태 감시는 게스트 실행 중에 페이지 표를 바꿉니다(Python도 같은 C 함수를 같은 상황에서 불렀음). 기록상 그 주소 읽기는 0번이었습니다.
4. NS1만 모드는 한 번도 켜 본 적이 없습니다. Windows가 NS2를 찾는 시작 작업이 있으면 기다릴 수 있습니다.
5. NS2가 없으면 맥북이 GPU 다시 켜기(D28)를 보낼 수 없어 GPU 드라이버가 첫 시작에 실패할 수 있습니다.
6. C 실행기의 작업용 자리(ADT 조립 4 MiB, NVMe 16 KiB, 소리 256 KiB)는 Python 힙 맨 위 8 MiB입니다. (나)에서 맥북이 Python 힙을 760 MiB 넘게 쓰면 겹칩니다(스크립트가 펌웨어 묶음 위치를 확인함).
7. m1n1 힙이 128 MiB를 넘게 자라면 Python 힙과 겹치는 위험은 지금 Python 실행기와 같습니다.

---

## 10. s264C (2026-11-19 밤) [빌드만]

결론: s264B에 ① g262R(ANS 5칸 인터럽트 수정), ② S262 동시 처리 SSD 설정(`S264_STEP_NVME_QD`, 기본 QD 32·인터럽트), ③ 맥북 없이 버틸 예외 정책(`S264_POL_LOCAL` = Python 처리기와 같은 일을 C에서, `S264_POL_FATAL` = 그래도 맥북으로 갈 것은 기록 후 재시작)을 더했습니다. 맥미니에서는 아직 돌리지 않았습니다.

- 10:50 실행에서 맥북으로 넘어온 `EXC_SYNC`의 "20번"은 감시 도구의 출력 상한(`s264_launch.py:87` `seen <= 20`)이고 총수가 아닙니다 [사실]. Python 처리기가 아무 줄도 찍지 않은 것과 S268 부팅의 HV 계수기(바탕화면까지 기타 동기 예외 217~259번)로 보아, Windows의 `BRK`(디버그 서비스 호출)를 Python `handle_brk` → `_lower`가 조용히 게스트로 되돌린 것으로 봅니다 [가설]. s264C는 이것을 C에서 되돌리고 `HVLOG: S264C LOWER #n esr=…` 줄로 남깁니다.
- 바이너리 `out-s264C/m1n1-s264C-agx-hv2-standalone.bin` (2,424,832 B, sha256 `6e6182c0d7a36ef09174d2c6af96142f5d2534476cf2ca292db18f8b3ce0ae0d`), 패치 `out-s264C/hv2-s264B-to-s264C.patch`, 재현 `build-s264c.sh <새 폴더>`.
- 호스트 시험: `s264c_offline_test.py` (s264B 15/15 × 2상태 + s264C 6항목 + 음성 대조 4개). 실기 단계: `run-s264c-compare-slot.sh`(미리보기 비교), `run-s264c-launch-slot.sh`(C가 게스트 시작; `S264C_NO_QD=1`·`S264C_NO_LOCAL=1`·`S264C_FATAL=1` 스위치).
- 옮기지 않은 것: 와이파이 켜기·사실 검사, 소리 B단계, 블루투스. GPU 다시 켜기(D28)는 Windows 시작 작업 `jobs/T1-install-d28-boot-task.cmd`로 준비만 했습니다(설치 전 `T0-disk-check.cmd`로 규칙 확인, GPU 담당과 합의 필요).

## 11. s264D (2026-11-20) [빌드만]

결론: s264C에 ① 소리 B단계·감지 단계·마지막 머리말(`sound-s256/audio_module.py`)과 ② 와이파이 하이퍼바이저 쪽(`NWOAS_WIFI=1`: 부팅 때 강제 끄기, 제어 페이지 안에서 켜기·끄기·설정 창, ADT 사실 검사)을 C로 옮겼습니다. 호스트 비교 시험은 모두 같음이고, 맥미니에서는 아직 돌리지 않았습니다.

- 바이너리 `out-s264D/m1n1-s264D-agx-hv2-standalone.bin` (2,523,136 B, sha256 `2c3127d59814394a34c8f9ad0924fccbd446866bc5dfcc8f889481398a5ac17e`), 패치 `out-s264D/hv2-s264C-to-s264D.patch` (sha256 `f439a05e7a41717ff0a0938aa6ffc919033014c33208f9dbbe8a68af1b830a2f`), 재현 `build-s264d.sh <새 폴더>` (s264A→B→C→D 네 개 모두 같은 지문으로 재현됨).
- 기본 설정 깃발 `0xeff` = s264C `0x2ff` + `AUDIO_B`(1<<10) + `WIFI_ON`(1<<11). 보호 단계 `audio_prot`(옛 `reserved[1]`) 기본 0 = sense(Python 기본과 같음).

소리(`src/nwoas_s264_audio.c`, 단계 18 `AUDIO_B`)
- Python 모듈과 proxyclient 드라이버(i2c·admac·mca·nco·dart8020·pmgr)의 레지스터 읽기·쓰기를 같은 순서·같은 값으로 옮겼습니다(`Register.set()`의 읽고-쓰기, 기록 줄용 읽기, 실패 메시지용 다시 읽기까지).
- 일부러 다르게 한 것: 쓰기마다 20 µs, 기다림 되풀이마다 10 µs 쉼. Python은 접근마다 USB 왕복이 있었고 C는 연달아 하기 때문입니다(m1n1 `i2c.c`·`pmgr.c` 방식) [가설: 실기 필요].
- DART-sio 페이지 표는 `buf+0x40000`(L1)·`+0x44000`(L2). Python 힙 안·게스트 RAM 아래인지 플랜이 검사합니다.
- B0 "그대로 통과" 검사는 플랜이 만든 매핑으로 합니다(통과 쪽 시험만 있음). 실패하면 Python처럼 부팅은 계속하고 머리말 상태 0으로 끕니다.

와이파이(`src/nwoas_s264_wifi.c`, 단계 11 `WIFI`의 d = 1)
- 옮긴 것: `wifi-s259/nwoas_wifi_s259.py`(WifiS259), 그것이 쓰는 `wifi-s257/wifi_up.py`·`wifi-s240/wifi_probe.py`·`wifi_scan.py` 부분, `wifi_module.py`의 사실 검사. 쓰기 허용 목록과 FL1100(포트 1) 전후 비교도 C 쓰기 경로에 그대로 있습니다.
- S259 의미 그대로입니다(블루투스 S261 제어 페이지·S268 내용은 아직 없음).
- ADT 사실 검사는 이제 와이파이를 꺼도 플랜 때 돕니다(Python도 두 경우 모두 검사). 거부되는 ADT면 플랜이 멈춥니다.
- SMC: `smc_get_key_info`를 `smc.c`에 더했습니다 [빌드만]. 시작·키 정보·쓰기·멈춤마다 20 s 기한을 둡니다(`nwoas_pwr.c`와 같은 설정-후-0 방식).
- 제어 페이지 명령 쓰기(켜기·끄기, 수 초 걸림)는 Python 프록시 때처럼 다른 CPU를 모으고(`hv_rendezvous`) 걸린 시간을 게스트에서 숨깁니다(`hv_add_time`). 근거: `hv_exc.c:71` `time_stealing = true`. `nwoas_scripts`·`port-j274/gpu/hv/tools-hv2`·`m1n1_windows/proxyclient` 전체(`*.py`, `*.sh`)와 `src/*.c`에서 이것을 끄는 곳은 `proxyclient/hv/trace_avd.py`·`trace_codecs.py`(우리 실행기에서 쓰지 않음)뿐입니다. HV 감시견도 그 동안 멈춥니다.
- DART 무효화 기다림(1000번 읽기)은 되풀이마다 100 µs 쉼 = 100 ms 한도입니다(일부러 다르게 한 것).
- 남은 위험 [가설]: SMC 기한은 `smc_work`에만 걸립니다. `rtkit.c`의 부팅(646줄)·정지(684·704줄) 기다림은 한도가 없어, SMC가 답하지 않으면 명령 쓰기 안에서 모든 CPU가 모인 채 멈출 수 있습니다(Python은 20 s 알람으로 실패 처리). `dcp.c`·`nwoas_pwr.c`도 같은 함수를 씁니다.
- 옮기지 않은 것: 사건 JSON·실행 폴더 기록, `wifi_probe.py` 파일 지문 검사, SMC 외 단계 알람(C에서는 MMIO가 USB에 막히지 않음).

호스트 시험(`s264d_offline_test.py`, 12/12 같음)
- s264C 시험 전체 + AUDIO_B 플랜 7항목 + WIFI d 항목.
- `s264d_audio_test.py`: 진짜 `audio_module.py`와 C를 같은 가짜 하드웨어에서 돌려 MMIO·클럭·캐시·메모리 기록을 비교, 10/10 같음(보호 3단계 + 실패 주입 7가지).
- `s264d_wifi_test.py`: 진짜 S259/S257/S240 Python과 C를 S240/S257 가짜 하드웨어(각자 새 사본, 같은 정수 µs 시계)에서 돌려 비교, 28/28 같음(ADT 2개 + 사실 변형 10가지 + 시나리오 13가지 + 음성 대조 3가지).
- 실기 입출력 함수(`s264a_*`, `s264w_*`), `smc_get_key_info`, 통과 검사의 실패 쪽은 [빌드만]입니다.

실기 단계(맥미니 필요, 아직 안 함)
- (가) `run-s264d-compare-slot.sh <옛 .out> <옛 .link>`: C 미리보기 vs Python(같은 부팅), 게스트 시작 안 함.
- (나) `run-s264d-launch-slot.sh <옛 .out> <옛 .link>`: C가 Windows 시작. 스위치 `S264D_NO_WIFI=1`, `S264D_NO_AUDIO_B=1`, `S264D_AUDIO_PROT=sense|moderate|full`(실행 스크립트 기본 moderate), 그 밖에 `S264C_*`.
- 첫 (나)는 나눠서 권합니다: ① `S264D_NO_WIFI=1 S264D_NO_AUDIO_B=1`(s264C와 같은 동작 확인) → ② 소리만 → ③ 와이파이까지.
- 페이로드는 아직 S259(`fw-wifi-s259`)입니다. S261 블루투스 페이로드로 바꾸는 것은 블루투스 제어 페이지와 함께 합니다(다음 단계).

## 12. s264E (2026-11-20 새벽) [빌드만]

결론: s264D에 ① 보조 처리기(RTKit·SMC·DCP용 AFK) 기다림 10 s 한도, ② 블루투스 S261(`bt-s261/bt_module.py` + `nwoas_bt_s261.py`)의 하이퍼바이저 쪽 C 이식, ③ S261 블루투스 페이로드로 교체, ④ 소리 보호 기본값 moderate를 더했습니다. 맥미니에서는 아직 돌리지 않았습니다.

- 바이너리 `out-s264E/m1n1-s264E-agx-hv2-standalone.bin` (2,572,288 B, sha256 `9254d61922779ba997e8be1575868b0e92d8ec6ee7db3419fc39e0dd58949615`), 패치 `out-s264E/hv2-s264D-to-s264E.patch` (sha256 `bd9c54224189b8070f90cfe2d44c3b28d78533671931537e1776fb1488b0f4b2`), 재현 `build-s264e.sh <새 폴더>`.
- 기본 설정 깃발 `0x3eff` = s264D `0xeff` + `S261`(1<<12) + `BT_ON`(1<<13). `BT_NOFENCE_WR`(1<<14) = `NWOAS_BT_FENCE_WIFI_RESET=0`. `audio_prot` 기본 1 = moderate.

기다림 한도(①)
- `rtkit.c`: 부팅 때 IOP 켜짐, 정지 때 AP·IOP 전원 상태 기다림에 10 s 한도(원래 무한).
- `smc.c`: 호출자가 기한을 안 주면 메시지마다 10 s(원래 무한, `dcp.c`가 이 경우).
- `afk.c`(DCP): 막는 폴링, 명령 답 기다림, 서비스 알림 기다림에 10 s.
- 한도에 걸리면 그 함수가 실패를 돌려줍니다(정상 동작 때는 걸리지 않음 [가설]).
- 이미 한도가 있어 고치지 않은 것: `nwoas_pwr.c`(277줄), `nvme.c`의 기다림들, `dcp.c`·`dcp_iboot.c`(자체 루프 없음, RTKit·SMC·AFK를 씀).
- 이 한도는 체인로드 때마다(미리보기 비교 포함) 도는 부팅 코드에도 들어갔으므로, s264E의 (가)는 바뀐 부팅 코드를 처음 돌리는 것입니다.

블루투스(②, `src/nwoas_s264_wifi.c`의 WifiBtS261 부분)
- 옮긴 것: fn1 설정(BAR0 0xE0900000·BAR2 0xE0C00000, 루트 포트 창 0xE0F0E000, DevCtl, 명령), BT 제어 페이지(0x348004000: 상태·설정 창·FLR·ARM·ADT BD 주소·보정 자료), fn1 BAR 울타리(부팅·전원 켜고 끄기·Wi-Fi 칩 리셋·fn1 쉼 때 닫힘, 드라이버 ARM으로 열림), fn1 실패 때 되돌리기.
- 플랜: BT 페이지 HOOK, fn1 BAR 두 개 BYPASS, 그 위 울타리 HOOK, BT 페이지·fn1 BAR 범위 검사, `NWOAS_BT=1`이면 ADT 블루투스 자료 검사. 단계 `WIFI`의 d: bit0 Wi-Fi, bit1 S261, bit2 BT, bit3 리셋 때 울타리 안 닫음.
- 울타리 여닫기는 실기에서 `hv_map_hook`/`hv_map`(BYPASS로 되돌림) + TLB 무효화입니다 [빌드만].
- BT 명령(CMD)·FLR 쓰기는 Wi-Fi 명령처럼 다른 CPU를 모으고 시간을 숨깁니다. 울타리를 여닫을 때(ARM 쓰기, Wi-Fi 0xBC 쓰기 포함)도 2단계 표를 바꾸기 전에 다른 CPU를 모읍니다.

호스트 시험(`s264e_offline_test.py`, 11/11 같음)
- s264C 시험 전체를 S268 모듈 묶음(Python 쪽 진짜 `bt_module.py`)으로: 2단계 매핑 51구간 같음(BT 페이지·fn1 BAR·울타리 포함).
- 플랜 항목 7개, `s264d_audio_test.py` 10/10, `s264d_wifi_test.py` 28/28, `s264e_bt_test.py` 18/18(실제 ADT 2개의 BT 페이지 + 시나리오 14 + 음성 대조 2).

실기 단계(맥미니 필요, 아직 안 함)
- (가) `run-s264e-compare-slot.sh <옛 .out> <옛 .link>`.
- (나) `run-s264e-launch-slot.sh <옛 .out> <옛 .link>`: 콘치님이 보고 계실 때만. 스위치 `S264D_NO_WIFI=1`, `S264D_NO_AUDIO_B=1`, `S264D_AUDIO_PROT=…`(기본 moderate), `S264E_NO_BT=1`, `S264E_S259=1`(S259 모듈 묶음 + S259 페이로드), `S264E_NO_FENCE_WR=1`, 그 밖에 `S264C_*`.
- 첫 (나) 권장 순서: ① `S264D_NO_WIFI=1 S264D_NO_AUDIO_B=1`(s264C와 같은 동작 + S261 페이지는 꺼진 상태) → ② 소리만 → ③ `S264E_NO_BT=1`로 와이파이까지 → ④ 블루투스까지(기본값).
- 아직 C에 없는 것: `power-s260/power_module.py`(S268 기본 실행에 들어 있음).

## 13. s264F (2026-11-20 새벽) [빌드만]

결론: s264E에 `power-s260/power_module.py`(S268 실행기가 S262 기본으로 돌리는 그대로)를 C로 옮겼습니다(`src/nwoas_s264_pwr.c`, 단계 19 `PWR`, 설정 깃발 `1<<15` 기본 켬, 설정 칸 `pwr`(옛 `reserved[2]`) 기본 0 = 모든 `NWOAS_PWR_*` 꺼짐). 맥미니에서는 아직 돌리지 않았습니다.

- 바이너리 `out-s264F/m1n1-s264F-agx-hv2-standalone.bin` (2,572,288 B, sha256 `9db7de2ae0ef1a8afb781bc3246d8a33795465524015a166b9998717868fc8ea`), 패치 `out-s264F/hv2-s264E-to-s264F.patch` (sha256 `1b2771bb0fff9a33d5ed1b61e77074fc290bf877b40e857fa6ddf25bc5a52f72`), 재현 `build-s264f.sh <새 폴더>`(A~F 모두 같은 지문).
- 기본 깃발 `0xbeff`. 순서는 Python과 같습니다: WIFI(bt_module) → PWR → NVME_QD.
- 옮긴 것: 시작 전의 P_HV_NWOAS_PWR 요청 순서 전부(QUERY, CONFIG, MCC_STAT, [SLC + MCC_STAT], 부팅 표시 = SNAPSHOT [+ SMC_READ]). 기본 S268에서는 CONFIG(0,0,0,0) = HV 초기 상태와 같은 값입니다 [사실: `nwoas_pwr.c:46-75`].
- 옮기지 않은 것: 작업 출력의 `NWSTAT-MARK`/`NWSTAT-SET` 처리와 맥북의 `hvstats.jsonl`(맥북 통로가 필요), `NWOAS_PWR_SMC_KEYS` 바꾸기(기본 키 6개만).
- `pwr` 칸: flags 7:0, sec_tick_hz 23:8, timer_gap_us 43:24, print_s 55:44, SLC 56. Python이 거부하는 값(SECTICK 5, TGAP_US 70000, PRINT 4000)은 플랜 오류 `0xb0`입니다.
- 호스트 시험 `s264f_offline_test.py` 2/2: s264E 시험 전체 + `s264f_pwr_test.py` 16/16(환경 11가지에서 진짜 `power_module.py`와 요청 순서·인자·메모리 쓰기 비교, 거부 3가지, 끔 1가지, 음성 대조 1가지).
- 실기 단계: `run-s264f-compare-slot.sh`(Python 쪽에 `power_module.py`도 돎), `run-s264f-launch-slot.sh`(스위치 `S264F_NO_PWR=1` 추가).
- 디스크에서 켜기 계획: `PLAN-4-1-disk-boot.md`.

## 14. s264G + 4-1 1단계 준비 (2026-11-20 새벽) [빌드만]

결론: ① 4-2 자동 시작(맥북이 N초 안에 안 붙으면 ESP의 설정·페이로드로 LAUNCH)을 넣은 s264G, ② 4-1용 1단계 m1n1과 1TR 스크립트·맥북 리허설·ESP 작업을 만들었습니다. 아무것도 실행하지 않았습니다.

- s264G 바이너리 `out-s264G/m1n1-s264G-agx-hv2-standalone.bin` (2,605,056 B, sha256 `d1fea3d73ad7d0d7da562802dbdec88527490dfe5f395d28dbd464a324c43ce6`), 패치 `out-s264G/hv2-s264F-to-s264G.patch` (sha256 `d688e7ebf4fe904b5cd8dbb87c3acbc9c067ad784ea60bf8bd888f6c6655cfa9`), 재현 `build-s264g.sh`(A~G 같은 지문).
- 자동 시작은 이미지 변수 `nwoas_autostart=<초>`가 있을 때만 켜집니다(ESP의 4-2 `boot.bin`에만 붙임). 맥북이 `chainload.py -r`로 띄우는 s264G에는 이 변수가 없어 s264F와 같습니다.
- 마감 전에 프록시 장치로 1바이트라도 오면 지금 방식. 마감이 지나면: ESP `m1n1/nowin`이 있으면 계속 맥북 대기, 없으면 `m1n1/nwoas-s264.cfg`(정확히 192 B, 없으면 기본 설정) + `m1n1/nwoas-payload.bin`을 Python 힙 자리(heapblock 위 + 128 MiB)에 읽고 예외 정책 FATAL을 더해 LAUNCH. 실패하면 맥북 대기로 남습니다.
- ESP 읽기는 m1n1 Rust 코드에 더한 `rust_load_file_into`입니다. 빌드 경로가 바이너리에 들어가지 않게 Makefile에 `--remap-path-prefix`를 더했습니다(재현을 위해).
- 호스트 시험 `s264g_offline_test.py` 2/2: s264F 시험 전체 + `s264g_auto_test.py` 26/26. 판단 시험 17개 + 진짜 `nvme.rs`·`gpt.rs`·`chainload.rs`를 호스트로 빌드해 4 KiB 섹터 GPT + FAT32 ESP 이미지에서 읽기 9개. 경로 `m1n1\boot.bin`(역슬래시)은 못 찾음 → 1단계 변수는 `m1n1/boot.bin`.
- 1단계: `stage1/build-stage1.sh` (s264G 트리, `CHAINLOADING=1`, `USE_FB` 끔). `stage1.bin` 2,490,433 B, sha256 `10ebb73b07c6ea5bd974f4aaa46b6d42b6be96ece93448d0b5aade1932066968`, md5 `0cf97ea05c5100e740b74878e0eb85fd`. 다른 폴더에서 다시 빌드해 같음.
- `stage1/go1.sh`, `stage1/cleanbp-local.sh`, `stage1/run-stage1-rehearsal.sh`(+ 슬롯), `stage1/make-esp-bundles.sh`, ESP 작업, 지문 `stage1/SHA256SUMS`. 순서와 1TR 카드는 `PLAN-4-1-disk-boot.md` 7·8장.

## 15. 4-1 독립 검토 반영 (2026-11-20) [빌드만]

결론: 1TR은 아직 콘치님께 가져가지 않습니다. 맥북 리허설 세 가지(4-2 끝까지, 1단계 + S268 + SSD, 일부러 실패시키기)를 먼저 통과해야 합니다. 자세한 내용은 `PLAN-4-1-disk-boot.md`(0·2·3·4·6·7·8장)에 있습니다. 아무것도 실행하지 않았습니다.

- 1단계 = s264G 소스 + `stage1/hv2-s264G-to-stage1.patch`(sha256 `9588a8f1…89b4`)입니다. raw 1,720,320 B이고, 파일은 D4와 같은 2,097,152 B로 0을 채웠습니다. 두 번 빌드해 같았습니다.
  - `chainload.c`: preoslog·ADT BootArgs를 `chainload.py`처럼 옮기고, malloc 실패를 검사합니다. 호스트 시험 `stage1/stage1_layout_test.py` 3/3 SAME이고, 고치기 전 코드는 3/3 DIFF였습니다.
  - `CHAINLOADING` 빌드에서는 예외·SError·Rust panic·`panic()`·assert가 넘기기 전까지 USB 대체 프록시로 갑니다. 그 프록시에서도 P_VECTOR로 넘길 수 있습니다(`nwoas_handover`).
  - `nwoas_stage1_fault=1` 변수는 리허설에서 예외 길을 시험하는 데만 씁니다.
  - DART 풀은 `CHAINLOADING`에서만 1쪽입니다. **이 트리로 하이퍼바이저를 `CHAINLOADING=1`로 빌드하지 않습니다.**
- 하이퍼바이저는 s264G 그대로입니다(다시 재현해 같음). s264H는 없습니다.
- `s264g_auto_test.py`에 "파일 없음 → rc < 0"을 더했습니다. 27/27입니다(1단계 소스와 s264G 소스 둘 다).
- 새 스크립트(모두 준비만, 실행 안 함):
  - `stage1/run-stage1-42-rehearsal.sh` + `chainload_nonop.py`(점프 뒤 맥북 바이트 없음)
  - `stage1/run-stage1-rehearsal.sh main|missing|fault` + `probe_running_image.py`
  - `stage1/run-s268-after-stage1.sh`(재부팅 없는 S268 + Q1S/Q2/chkdsk)
  - `stage1/run-stage1-rehearsal-slot.sh 42|main|negative`(`idle -` 가능)
  - `stage1/watch-after-go1.sh [--rollback]`(KEEP / HOLD / ROLLBACK)
  - `stage1/go1.sh [--d4]`: ESP에 쓰지 않고, 되돌리기 줄을 화면에 보여 줌
- ESP 묶음 `esp-4-1`은 이제 파일 6개입니다: boot.bin(D4), stage1.bin, go1.sh, cleanbp-local.sh, nwoas-s264g.bin, nwoas-payload.bin. 지문은 `stage1/SHA256SUMS`(35개 OK)에 있습니다.

## 16. s264G 실기 (2026-11-21 00:37~00:5x) [사실]

결론: 맥북 연결 상태에서 (가) 미리보기 비교 통과, (나) C 실행기로 Windows 켜기 2번 모두 바탕화면까지 갔습니다. 기본값(와이파이·소리·블루투스·전력·QD·예외 정책 LOCAL 모두 켬)에서는 맥북으로 넘어온 사건이 0번이고, 콘치님이 와이파이·소리 동작을 확인했습니다. 다음은 PLAN-4-1 6장 4번(ESP 준비) → 5~7번 맥북 리허설입니다.

| 단계 | 기록 | 결과 |
|---|---|---|
| (가) 미리보기 비교 | `runs/slot-s264g-20261121-003733.txt`, `logs/s264g-compare-20261121-003755.log` | `DRY-RUN COMPARE: PASS (every item SAME)` |
| (나)-기준 (QD·LOCAL·와이파이·소리 B·BT·전력 끔) | `logs/s264g-launch-20261121-004023.*`, `runs/launch-slot-s264g-*.txt` | 바탕화면. 계수기 14·1·39·2·14·7 = 기대값. 맥북으로 넘어온 것 197번 = 모두 `BRK #0xF002`(196)·`#0xF004`(1) — LOCAL을 끈 기준 실행이라 예상대로 |
| (나)-기본 (스위치 없음) | `logs/s264g-launch-20261121-004627.*`, `runs/launch2-default-*.txt` | 바탕화면, 와이파이·소리 동작(콘치님 눈). 102 ops, 계수기 같음, POLICY pol=1, `S264C LOWER` 12줄, 맥북으로 넘어온 것 **0번** |

- 두 C 실행 진단 출력의 `DARTDIAG … MAPPED-WRONG-TARGET`·`DARTERR STALE` 묶음(28줄)은 같은 날 맥북 실행기 S268/S269 부팅에도 똑같이 있습니다(처음 "0줄"이라 적은 것은 부팅마다 다른 `err=` 값으로 찾아 잘못 센 것). 두 실행기의 USB 상태가 같고 무해함 → `ANALYSIS-dartdiag-20261121.md`. USB 키보드·마우스 정상(콘치님 확인).
- 끝내는 방법: 시작 메뉴 → 다시 시작 → 맥미니가 맥북 연결 대기(D4)로 돌아오고 감시 도구는 USB가 끊겨 rc=1로 끝남(정상).
