# CPU·메모리·전원·발열 로드맵 (2026-11-17, 읽기·계획 전용)

> 이 문서는 조사와 계획만 담았습니다. 빌드, 실기기 조작, 파일 수정(이 문서 말고)은 하지 않았습니다.
> 표기: **[사실]** = 파일·로그·코드·공개 문서로 확인(위치를 붙임). **[가설]** = 확인하지 않은 추측.
> 경로 줄임: `N/` = `/Users/angigyeom/Desktop/DEV/NWOAS`, `HV/` = `N/m1n1-gpu-hv2/src`, `MU/` = `N/fw-audio-s256-mu`(지금 실행 중인 S256 펌웨어 빌드 트리), `REF/` = `N/external/windows-on-m1`(커밋 6c656732, MIT).
> 지금 켜진 구성의 로그: `N/nwoas_scripts/logs/agxmed1g-s249-20261116-202802.1sXX3e`(아래 "실행 로그"), 같은 이름 `.vuart`(아래 "HV 로그"), `.cpustate.json`.
> 소리 드라이버와 와이파이는 이 문서에서 다루지 않습니다.

---

## 0. 결론부터

| 순위 | 발견 | 근거 | 영향 |
|---|---|---|---|
| 1 | **Windows가 쉴 때 CPU가 실제로 쉬지 않습니다.** Windows의 쉬는 명령(WFI)을 하이퍼바이저가 붙잡아 기다리지 않고 바로 다음 명령으로 넘깁니다. 그래서 쉬는 동안 8개 코어가 계속 돌아갑니다 | [사실] `HV/hv.c:88`(WFI 붙잡기 켬), `HV/hv_exc.c:1964-2028`(아무것도 안 하고 `handled=true`), `:2020`(다음 명령으로 넘김). Windows 쉬는 경로가 WFI라는 것은 `HV/hv_exc.c:363-366` 주석. 참조 프로젝트는 같은 자리에서 실제로 WFI로 잠듭니다(`REF/m1n1_windows/src/hv_exc.c:1862-1887`) | [가설] 쉬는 동안에도 전력·발열이 부하 상태와 비슷함. 실측 없음 |
| 2 | 코어 속도는 부팅 때 한 번 정한 값에 고정입니다: 효율 코어 5단계(2064 MHz), 성능 코어 12단계(2988 MHz). Windows는 속도를 바꿀 수단이 없습니다 | [사실] HV 로그 37행 `S189 ReadyToBoot ... current=12 target=12`, 실행 로그 611행 `p_status 0xcc e_status 0x55`. MHz는 Linux `t8103.dtsi` | 쉬어도 최고 일반 속도 유지 |
| 3 | Windows는 8개 코어를 모두 같은 종류(효율 등급 0)로 봅니다. 펌웨어 ACPI 표의 코어 종류 칸이 전부 0이기 때문입니다. 참조 프로젝트는 성능 코어에 1을 넣었습니다 | [사실] `MU/Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc:84-107`(끝에서 두 번째 인자 0), 인자 순서 `MU/Common/TIANO/EmbeddedPkg/Include/Library/AcpiLib.h:69-75`, 참조 `REF/mu/Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc:96-109`(값 1) | 고치기 가장 쉬운 항목 |
| 4 | Windows "끄기"를 하면 맥미니가 꺼지지 않고 재시작합니다. "재시작"도 SoC 전체 재설정입니다 | [사실] `HV/hv_psci.c:1539`(끄기 = `flush_and_reboot`), `:1523`(재시작 = `reboot`), `HV/start.S:212-222` → `HV/wdt.c:36-44`(감시 타이머로 전체 재설정) | 끄기 기능 없음 |
| 5 | 16 GiB 중 쓰이지 않거나 되찾을 수 있는 큰 덩어리: UEFI가 Windows에서 감춘 약 536 MiB, 맥북 연결용 메모리 약 896 MiB(맥북 없는 부팅 때만), GPU 예약 1 GiB(일부) | [사실] HV 로그 47·48행, `N/m1n1_windows/proxyclient/tools/run_guest.py:47`, `proxyutils.py:111`, HV 로그 4행 | §4 표 |
| 6 | 성능 코어에서만 캐시 없는 메모리 읽기가 6배 느린 현상은 아직 원인 미확정입니다. 가설 3개와 가르는 시험을 §4.6에 적었습니다 | [사실] `N/research-20261116/RESULT-20261116-afternoon.md:46-61` | GPU 대체 경로에만 영향 |

권하는 순서(앞 단계가 뒤 단계의 측정 기반이 됩니다):
1. 하이퍼바이저 나가기 횟수 세기(§5 1단계) — 지금 상태를 숫자로 남깁니다.
2. 실제로 쉬기(§3 1단계).
3. 코어 종류 알리기(§1 1단계).
4. 하이퍼바이저가 속도 조절(§2 2단계).
5. 온도 읽기와 안전 하한(§6 4~5단계).
6. 진짜 끄기(§6 2단계).

---

## 공통 바탕: 지금 켜진 구성의 CPU·메모리 값

| 항목 | 값 | 근거 |
|---|---|---|
| CPU | 효율 코어 4개(MPIDR 0~3), 성능 코어 4개(0x10100~0x10103) | [사실] `N/port-j274/J274-BOARD-PROFILE.md` CPU-1 |
| Windows가 보는 CPU 수(개) | 8 | [사실] `N/NWOAS-STATUS-2026-11-15.md:10` |
| Windows가 보는 RAM(MB) | 14,388 (11-15, GPU 1 GiB 예약 전) | [사실] `N/NWOAS-STATUS-2026-11-15.md:11` |
| Windows 물리 메모리(바이트) | 15,082,192,896 (9-10, 10 GiB 유지 + 낮은 창 4 GiB 구성) | [사실] `N/nwoas_scripts/memory-s180/hardware-memory-result.json` |
| GPU 1 GiB 예약 뒤 Windows RAM | **재지 않음** | — |
| 시계 주파수(Hz) | 24,000,000 | [사실] HV 로그 32행(`freq=24000000`) |
| 하이퍼바이저 확장 타이머(ECV) | 없음 | [사실] `N/nwoas_scripts/logs/retest40-22h2.run14.log`의 `HV: No ECV supported` |

---

## 1. 성능 코어·효율 코어 나눠 쓰기

### 1.1 지금 상태

| 항목 | 지금 값 | 근거 |
|---|---|---|
| ACPI MADT 각 CPU의 "전력 효율 등급" 칸 | 8개 모두 0 | [사실] `MU/.../MADT_Static.aslc:84-107` |
| Windows가 아는 효율 등급 | 8개 모두 0 | [사실] `N/HANDOFF-2026-11-16-evening.md:158`, `N/research-20261116/RESULT-20261116-afternoon.md:60` |
| ACPI PPTT(코어 묶음 구조) | 묶음 2개(효율 0, 성능 1) × 코어 4개. **캐시 정보 없음**(각 칸의 "Private resources"가 0) | [사실] `MU/Silicon/Apple/T810XFamilyPkg/AcpiTables/PPTT.aslc:52-99` |
| DSDT 프로세서 장치 | `CLU0`(CPU0~3), `CLU1`(CPU4~7). `_LPI`는 주석 처리, `_CPC` 없음 | [사실] `MU/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl:462-564` |
| 실제 L2 캐시 크기(MiB) | 효율 묶음 4, 성능 묶음 12 | [사실] Linux `arch/arm64/boot/dts/apple/t8103.dtsi` `l2_cache_0`/`l2_cache_1` |
| Linux가 쓰는 상대 성능 값 | 효율 714, 성능 1024 | [사실] 같은 dtsi `capacity-dmips-mhz` |
| 우리 측정: 같은 계산을 코어마다(ms) | 효율 코어 85~87, 성능 코어 59 (P12) | [사실] `N/nwoas_scripts/cpufreq-s193/hardware-result.json` `per_cpu_median_us` |
| 지금 우회책 | GPU 드라이버가 시작 때 CPU마다 재 보고 빠른 CPU(0~3)에 복사 스레드를 묶음 | [사실] `N/HANDOFF-2026-11-16-evening.md:157` |

### 1.2 목표 상태

- Windows가 CPU0~3을 효율 등급 0, CPU4~7을 효율 등급 1로 압니다. 참조 프로젝트(MacBook Air M1)는 이 상태를 실기기에서 확인했습니다 [사실] `REF/documentation/STABLE_8CORE_INPUT.md:11-12`.
- PPTT에 L2 캐시(4 MiB, 12 MiB)를 묶음별로 적어 Windows가 같은 캐시를 쓰는 코어끼리 묶어 일을 나눕니다.
- [가설] Windows 11 ARM64 스케줄러는 MADT의 이 칸을 보고 무거운 앞 작업을 성능 코어로, 가벼운 뒤 작업을 효율 코어로 보냅니다. Microsoft 공개 문서에서 "MADT 칸을 읽는다"는 문장은 찾지 못했습니다(검색어 2개). 이종 코어 설정 항목 자체는 문서에 있습니다: [HeteroClass1InitialPerf](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/configuration-for-hetero-power-scheduling-heteroclass1initialperf), [SchedulingPolicy](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/configuration-for-hetero-power-scheduling-schedulingpolicy). 참조 프로젝트의 실기기 결과가 지금 가장 강한 근거입니다.

### 1.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 방법 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | MADT CPU4~7의 효율 칸을 0 → 1로 바꿉니다(참조와 같은 값) | `MU/Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc:98,101,104,107`의 끝에서 두 번째 인자. 빌드는 기존 `N/nwoas_scripts/fw-audio-s256/build.py` 방식으로 새 펌웨어 묶음 | 작업 통로로 PowerShell `Get-CimInstance Win32_Processor`와 `coreinfo`류 대신, Windows API `GetSystemCpuSetInformation`의 `EfficiencyClass`를 출력하는 작은 실행 파일(기존 `PERCPU.EXE` 방식, `N/nwoas_scripts/cpufreq-s164/percpu.c`)을 보냅니다. 기대: 0~3은 0, 4~7은 1 | 없음(Windows 재시작은 스크립트) | 부팅: 낮음(값 한 바이트, 참조가 실기기에서 씀). 데이터·발열: 없음 | 없음 |
| 2 | 같은 부팅에서 PERCPU 측정을 다시 돌려 코어별 시간이 그대로인지, GPU 복사 스레드 자동 선택이 여전히 0~3을 고르는지 봅니다 | 없음(측정만) | `NwSoftStats` 읽기(`agx-s248/jobs/D29-read-stats.cmd`), PERCPU 40개 체크섬 | 없음 | 없음 | 1단계 |
| 3 | PPTT에 묶음별 L2 캐시 구조를 넣습니다. L1 크기는 ADT `/cpus/cpuN`에 캐시 속성이 있는지 먼저 읽고, 없으면 L2만 넣습니다 | `MU/.../PPTT.aslc`(캐시 구조 추가, 묶음 칸의 Private resources에 연결) | Windows `GetLogicalProcessorInformationEx(RelationCache)` 출력: L2 4 MiB가 CPU0~3, 12 MiB가 CPU4~7에 묶여 나오는지 | 없음 | 부팅: 낮음~중간(표 구조가 틀리면 Windows가 표를 거부하거나 부팅 초기 버그체크 [가설]). 되돌리기는 이전 펌웨어 묶음 | 1단계 |
| 4 | 체감 확인: 무거운 계산 하나를 띄웠을 때 Windows가 성능 코어에 올리는지 | 없음 | 작업 관리자 대신 작업 통로에서 `Get-Process` + 스레드 이상 CPU 기록 실행 파일 | 화면을 보고 "부드러운지" 말씀해 주시는 정도(선택) | 없음 | 1·2단계 |

### 1.4 재사용할 기존 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| 효율 등급 1을 넣은 MADT | `REF/mu/Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc:96-109` | 파일 머리말 `BSD-2-Clause-Patent OR MIT`(AppleWOA) |
| PPTT 캐시 구조 매크로 | `MU/Silicon/Apple/AppleSiliconPkg/Include/Acpi/SgiAcpiHeader.h`(`RD_PPTT_CACHE_TYPE` 등) | EDK2 계열 BSD-2-Clause-Patent [가설: 파일 머리말 확인 필요] |
| 코어별 측정 실행 파일 | `N/nwoas_scripts/cpufreq-s164/percpu.c` | 우리 코드 |

### 1.5 모르는 점

- Windows가 MADT 효율 칸만으로 충분한지, PPTT나 다른 표가 더 필요한지(참조 프로젝트는 MADT만 바꾼 것으로 보입니다 [가설]).
- MADT의 PMU 인터럽트 번호(50, 54, 58, 62, 34, 38)는 Apple 칩에서 의미가 없어 보입니다 [가설]. Windows가 이 값으로 무엇을 하는지 모릅니다.

### 1.6 대략 크기: **작음**

---

## 2. CPU 속도 조절

### 2.1 지금 상태

| 항목 | 효율 묶음 | 성능 묶음 | 근거 |
|---|---|---|---|
| iBoot가 넘겨준 단계 | 5 | 1 | [사실] 실행 로그 `.cpustate.json`(`status_current` 5 / 1) |
| 게스트 m1n1이 맞춘 단계 | 5 | 7 | [사실] HV 로그 37행 `before=40000107107`. 이 단계를 게스트 m1n1의 `cpufreq_init()`이 한다는 분석은 `N/nwoas_scripts/cpufreq-s193/REVIEW.md` §3·§5 |
| UEFI ReadyToBoot가 바꾼 단계 | 그대로 5 | 12 | [사실] HV 로그 37행 `final=4000010C10C ... current=12 target=12 protected=1`; 코드 `MU/Silicon/Apple/AppleSiliconPkg/Include/Library/NwoasCpuPstate.h`, 부르는 곳 `MU/.../NwoasHideHighRamDxe/NwoasHideHighRamDxe.c` |
| Windows 실행 중 단계 | 5 | 12 | [사실] 실행 로그 611행 `p_status 0xcc`, `e_status 0x55` |
| 그 단계의 클럭(MHz) | 2064 (효율 최고) | 2988 (터보 아닌 최고) | [사실] Linux `t8103.dtsi` OPP 표 |
| 터보 단계 13~15(MHz) | — | 3096 / 3144 / 3204, Linux에서 꺼 둠 | [사실] 같은 dtsi의 `#if 0`. 참조 도구 설명: "CPU 깊은 잠자기가 구현되기 전에는 쓸 수 없음" `REF/tools/j313_cpufreq.py:6-9` |
| Windows 쪽 속도 조절 수단 | 없음(`_CPC` 없음, 전원 플러그인 없음) | 같음 | [사실] `MU/.../DSDT.asl:462-564` |
| 하드웨어 열·전력 제한 기능 | 켜 둠(ADT 값을 따름: cpu-apsc=1, ppt-thrtl=1, llc-thrtl=0, amx-thrtl=1) | 같음 | [사실] `N/nwoas_scripts/cpufreq-s164/README.md` "Review corrections", `cpufreq-s193/REVIEW.md` §6 표 |

코어별 같은 계산 시간(ms, 중앙값, 5회):

| 조건 | CPU0 | CPU1~3 | CPU4~7 | 근거 |
|---|---|---|---|---|
| 성능 7단계 | 87.1 | 85.4~85.6 | 90.1~90.3 | [사실] `N/nwoas_scripts/cpufreq-s164/baseline-p7-s163g200.json` |
| 성능 12단계(부팅 뒤 늦게 바꿈) | 90.8 | 85.5 | 59.0~59.1 | [사실] `cpufreq-s164/live-p12.json` |
| 성능 12단계(UEFI가 바꿈, 맥북 쪽 초기화 없음) | 86.9 | 85.4~85.5 | 59.0~59.1 | [사실] `cpufreq-s193/hardware-result.json` |

Cinebench 2026 CPU 점수(모두 효율 5·성능 12단계):

| 회차 | 점수 | 근거 |
|---|---|---|
| S180 | 1647.353 | [사실] `N/nwoas_scripts/memory-s180/cinebench-result.json` |
| S196 | 1905.281 | [사실] `N/NWOAS-MORNING-2026-09-10.md:15` |
| S218 | 1888.454 | [사실] 같은 곳 |

### 2.2 목표 상태

- 일이 없으면 묶음 속도를 내리고, 일이 많으면 올립니다(성능 묶음 1~12단계, 효율 묶음 1~5단계). 터보 13~15는 §3 깊은 쉬기가 생긴 뒤에도 따로 검토하기 전까지 쓰지 않습니다.
- 속도 단계는 **묶음 단위**입니다. 한 묶음 4개 코어가 같은 단계를 씁니다 [사실] Linux dtsi `cpufreq_e`/`cpufreq_p` 각각 하나의 제어기(0x210e20000, 0x211e20000).
- Windows 작업 관리자에 실제 속도가 보입니다(§7).

### 2.3 방법 비교

| 방법 | 내용 | 좋은 점 | 나쁜 점 |
|---|---|---|---|
| A. 하이퍼바이저가 스스로 조절 | §3에서 실제 쉬기를 넣으면 하이퍼바이저가 묶음별로 "쉰 시간 비율"을 압니다. 그 비율로 단계를 고릅니다(Linux `schedutil`처럼) | Windows 드라이버가 필요 없습니다. 하이퍼바이저가 이미 레지스터를 만질 수 있습니다 | Windows는 속도를 모릅니다(작업 관리자 표시는 §7에서 따로) |
| B. ACPI `_CPC`(표준 성능 요청 표) | 각 CPU에 `_CPC`를 넣고, 요청 칸을 하이퍼바이저가 가로채는 가짜 메모리 주소로 둡니다. Windows가 요청 값을 쓰면 하이퍼바이저가 묶음 단계로 바꿉니다 | Windows 기본 전원 관리가 직접 조절하고 표시합니다 | [가설] Windows ARM64 기본 프로세서 드라이버가 시스템 메모리 주소형 `_CPC`를 받아들이는지 모릅니다(공개 문서 못 찾음) |
| C. Windows 전원 플러그인(PEP) 드라이버 | 참조 프로젝트 계획: 작은 PEP가 하이퍼바이저와 우편함으로 대화 | Microsoft가 ARM 기기에 쓰는 정식 구조 | 가장 큽니다. 참조도 계획 단계 [사실] `REF/documentation/ROADMAP.md:123-160` |

권장: **A를 먼저**(§3 1단계 뒤), 그다음 B를 시험해 받아들여지면 A의 결정을 Windows 요청으로 바꿉니다. C는 B가 안 될 때만 봅니다.

### 2.4 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 방법 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 지금 고정값을 그대로 두고, 하이퍼바이저에 묶음 단계 읽기·쓰기 함수만 C로 둡니다(지금은 UEFI 헤더와 맥북 Python에만 있음) | 새 `HV/nwoas_cpufreq.c` (바탕: `HV/cpufreq.c`의 `set_pstate`, `N/nwoas_scripts/cpufreq-s170/Include/Library/NwoasCpuPstate.h`의 바쁨 대기·되돌리기 규칙) | 부팅 직후 하이퍼바이저가 읽은 값 한 줄 출력: 효율 5/0x55, 성능 12/0xcc | 없음 | 낮음(읽기만) | 없음 |
| 2 | 방법 A: 하이퍼바이저 틱에서 묶음별 쉰 시간 비율을 100 ms마다 계산해 단계를 고릅니다. 처음에는 "성능 7 ↔ 12" 두 단계만 씁니다. 바꿀 때는 항상 바쁨 비트가 꺼질 때까지 기다리고, 실패하면 7로 되돌립니다(기존 규칙과 같음) | `nwoas_cpufreq.c`, `HV/hv.c` `hv_tick` 호출, 쉰 시간은 §3의 WFI 처리부에서 셈 | ① 가만히 둘 때 성능 묶음이 7로 내려가는지(하이퍼바이저 출력, 10초마다 한 줄) ② PERCPU를 돌리면 59 ms대로 돌아오는지 ③ Cinebench 한 번(점수 1647~1905 범위인지) | 없음 | 발열: 낮음(최고값이 지금보다 높아지지 않음). 부팅: 낮음. 진동(단계가 계속 왔다 갔다): 중간 → 바꾸는 간격 하한(예 100 ms)과 출력 횟수 제한 | §3 1단계, §5 1단계 |
| 3 | 단계를 1~12 전부, 효율 묶음 1~5까지 넓힙니다 | 같은 파일 | 같은 측정 + 2시간 가만히 두기(멈춤·WHEA 0회) | 없음 | 낮음~중간(낮은 단계에서 화면·USB 시간 초과 [가설]) | 2단계 |
| 4 | 방법 B 시험: DSDT CPU4에만 `_CPC`를 넣고 요청 칸을 하이퍼바이저 가짜 주소로. Windows가 그 주소에 쓰는지 관찰만 합니다(처음에는 쓰기를 기록만 하고 단계는 안 바꿈) | `MU/.../DSDT.asl` CPU 장치, `HV/` 가짜 주소 처리(기존 `hv_map_hook` 방식) | 하이퍼바이저 기록에 요청 쓰기가 찍히는지, Windows 장치 관리자 프로세서 장치 상태 | 없음 | 부팅: 중간(잘못된 `_CPC`로 프로세서 드라이버 시작 실패 [가설]). 되돌리기 쉬움 | 2단계 |

### 2.5 재사용할 기존 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| m1n1 `cpufreq.c`(묶음 초기화·단계 설정) | `HV/cpufreq.c` (552줄) | MIT(m1n1) |
| 바쁨 대기·되돌리기 규칙, 시험 포함 | `N/nwoas_scripts/cpufreq-s170/`(헤더, `tests/`) | 우리 코드 |
| Linux 묶음 속도 드라이버 | [apple-soc-cpufreq.c](https://github.com/torvalds/linux/blob/master/drivers/cpufreq/apple-soc-cpufreq.c) — 상태 레지스터 [7:4]로 현재 단계를 읽음 [사실] `cpufreq-s164/README.md` | GPL-2.0 — **코드 복사 금지, 동작 참고만** |
| 참조 프로젝트 P12 도구 | `REF/tools/j313_cpufreq.py` | MIT |

### 2.6 모르는 점

- 단계별 실제 전력(W). 공개 OPP 표에는 전압·전력이 없습니다.
- 단계를 바꾸는 데 드는 시간: dtsi `clock-latency-ns`는 성능 12단계 55,000 ns, 효율 5단계 50,000 ns [사실]. 하이퍼바이저 틱 안에서 기다려도 되는 길이인지는 실측이 필요합니다.
- APSC(칩이 스스로 하는 전력 조절) 켜짐 상태에서 우리가 고른 단계가 실제로 얼마나 지켜지는지.

### 2.7 대략 크기: **중간** (A만), B까지 **큼**

---

## 3. CPU 쉬기

### 3.1 지금 상태

| 항목 | 지금 | 근거 |
|---|---|---|
| Windows 쉬는 경로 | 커널 `HalProcessorIdle`의 WFI | [사실] `HV/hv_exc.c:363-366` 주석(커널 rva 0x434b64) |
| 하이퍼바이저가 WFI를 붙잡는지 | 붙잡음(`HCR_TWI`) | [사실] `HV/hv.c:88` |
| 붙잡은 뒤 처리 | 처음 40번만 기록하고 **기다리지 않고 다음 명령으로** | [사실] `HV/hv_exc.c:1964-2028`, `:2018-2023` |
| 결과 | Windows의 쉬기 = "붙잡힘 → 바로 돌아옴"의 반복 | [사실] 위 코드에서 나오는 동작. [가설] 실제로는 계속 도는 상태와 같아 전력·발열이 큼. 실측 없음 |
| WFE(가벼운 기다림) | 붙잡지 않음 | [사실] `HV/hv.c:91-96` |
| 하이퍼바이저 자신의 깊은 WFI 설정 | 기본값(0)으로 되돌림 | [사실] `HV/hv.c:144-146` |
| PSCI `CPU_SUSPEND` | 받아들이는 상태: 코어 대기, 묶음 대기, 시스템 끄기. 코어 전원 끄기는 주석 처리. 처리는 하이퍼바이저가 얕은 WFI 1번 | [사실] `HV/hv_psci.c:240-257`, `:1490`, `:1431` |
| ACPI `_LPI`(쉬는 단계 목록) | 주석 처리 → Windows는 PSCI 쉬기를 부르지 않음 [가설] | [사실] `MU/.../DSDT.asl:476` 등 |
| 참조 프로젝트 | WFI를 붙잡은 뒤 대기 중인 가상 인터럽트가 없으면 **하이퍼바이저가 실제 WFI로 잠듦** | [사실] `REF/m1n1_windows/src/hv_exc.c:1862-1887`, 판단 함수 `REF/m1n1_windows/src/hv_wfx_policy.h:28,49` |
| 이 붙잡기를 넣은 이유 | "winload 멈춤을 보이게 하려는 진단용" | [사실] `HV/hv.c:88-96`, `HV/hv_exc.c:1951-1963` 주석 |
| 9월 옛 설치본의 우회 설정 | `IDLEDISABLE=1`, `disabledynamictick yes`, `useplatformtick yes` | [사실] `N/nwoas_scripts/smp-s142/disable-deep-idle.cmd`. **지금 설치본(11-15 새로 설치)에 적용됐는지는 기록을 못 찾음** |

Apple 칩 쉬기에 관한 공개 사실:

| 사실 | 출처 |
|---|---|
| Apple 코어에는 상태를 잃는 대신 더 많이 꺼지는 "깊은 WFI"가 있음. Asahi의 비공식 cpuidle 드라이버는 상태를 저장하고 이 모드로 WFI를 함. M1~M3은 코어마다 상태 유지 여부를 칩 설정 비트로 정할 수 있음 | [Asahi 진행 보고 7.2](https://asahilinux.org/2026/08/progress-report-7-2/) |
| Linux 제안 패치(RFC): T8103에 PSCI 쉬기 2단계. `wfi`(진입·탈출 1 µs, 최소 1 µs), `deep-wfi`(PSCI 값 1, 진입·탈출 5 µs, 최소 10,000 µs) | [RFC 6/6](https://lkml.iu.edu/2607.1/00283.html) |

### 3.2 목표 상태

- 1차: Windows가 쉬면 코어가 실제로 WFI로 잠들고, 인터럽트가 오면 깹니다.
- 2차: Windows가 `_LPI`로 "얕은 쉬기"와 "깊은 쉬기"를 고르고, 하이퍼바이저가 PSCI `CPU_SUSPEND`로 처리합니다. 깊은 쉬기는 Apple 깊은 WFI.
- 3차: 묶음 전체가 쉬면 묶음 단계를 낮춥니다(§2와 연결).

### 3.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 방법 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | WFI 처리부를 참조처럼: 가상 인터럽트가 대기 중이면 바로 돌아가고, 아니면 하이퍼바이저가 실제 `wfi`를 한 뒤 돌아갑니다. 앞뒤로 `hv_update_fiq()`를 불러 깨움을 놓치지 않게 합니다. 처음 40번 기록은 유지 | `HV/hv_exc.c:1964-2028`(참조 `hv_wfx_policy.h` 가져오기) | ① 부팅해서 바탕화면까지(지금과 같은 시간 안에) ② 키보드·마우스·화면 반응 ③ 30분 가만히 + 30분 PERCPU 반복, 멈춤·0x124·WHEA 0회 ④ §5 1단계 계수기로 CPU별 WFI 횟수와 "잠든 시간 합" | 화면·입력 반응이 지금과 같은지 말씀해 주시는 정도. (선택) 손으로 만져 본 본체 온도, 팬 소리 차이 | 부팅: **중간**. 이 붙잡기는 winload 멈춤 진단용으로 넣은 것이라, 실제로 잠들면 그 시절 문제가 다시 보일 수 있습니다 [가설]. 깨움 신호가 늦으면 최악 하이퍼바이저 틱 200 µs 뒤에 깹니다(5 kHz, `HV/hv.c:24`) [가설]. 되돌리기: 이전 하이퍼바이저 파일로 | §5 1단계(전후 비교용) |
| 2 | `_LPI`에 얕은 쉬기 한 단계만 넣습니다(PSCI `CPU_SUSPEND` 값 0 = 코어 대기, `HV/hv_psci.c:242`가 이미 받음) | `MU/.../DSDT.asl` CPU0~7 `_LPI`, 묶음 `CLU0/1`에도 필요한지 확인 | 하이퍼바이저 기록에 `CPU_SUSPEND` 호출 횟수, Windows `powercfg /energy` 대신 작업 통로로 `Get-Counter '\Processor Information(*)\% Idle Time'` | 없음 | 부팅: 중간(`_LPI` 형식 오류 [가설]) | 1단계 |
| 3 | 깊은 쉬기: Apple 깊은 WFI를 PSCI 값 1로 받습니다. 먼저 "상태 유지" 칩 설정 비트를 켠 상태로 시험해 상태 저장 없이 되는지 봅니다. 안 되면 EL1 레지스터 저장·복원을 하이퍼바이저에 넣습니다 | `HV/hv_psci.c:244-245`(주석 해제·새 상태), `HV/hv.c:146`(WFI 모드), `MU/.../DSDT.asl` `_LPI` 두 번째 단계(최소 머무는 시간 10 ms, Linux RFC 값) | 같은 안정성 시험 + 깨어난 뒤 계산 체크섬 40/40 | 없음 | 부팅·데이터: **중간~높음**(상태를 잃으면 엉뚱한 값으로 계속 실행 → 파일 손상 가능 [가설]). 저장 장치 쓰기 시험 전에 가만히 두기만 먼저 | 2단계, §5 계수기 |
| 4 | 9월 우회 설정이 지금 설치본에 있는지 확인하고, 1단계 뒤에는 필요 없는지 시험 | 없음(조회 작업 파일) | `powercfg /query SCHEME_CURRENT SUB_PROCESSOR IDLEDISABLE`, `bcdedit /enum {current}` (`N/nwoas_scripts/nvme-s143/validate-live.cmd:3-6`과 같은 명령) | 없음 | 조회만 | 없음 |

### 3.4 재사용할 기존 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| 실제 WFI 처리와 판단 함수 | `REF/m1n1_windows/src/hv_exc.c:1862-1887`, `hv_wfx_policy.h` | MIT(`REF/m1n1_windows/LICENSE`, Asahi 기여자) |
| TWI·TWE 비트 유지 점검 | `REF/m1n1_windows/src/hv.c:234`, `:755` 부근 | MIT |
| PSCI 쉬기 틀 | `HV/hv_psci.c` | m1n1_windows 원본(AppleWOA) |
| Linux 쉬기 단계 값 | [RFC 6/6](https://lkml.iu.edu/2607.1/00283.html) | 값만 참고 |

### 3.5 모르는 점

- 지금 쉬는 동안 실제 전력·온도(측정 수단이 없음, §6 4단계).
- 하이퍼바이저가 붙잡지 않는 가상 타이머 깨움이 하이퍼바이저의 WFI를 깨우는지: `hv_update_fiq()`가 타이머 FIQ를 잠시 끄는 경로가 있습니다(`HV/hv_exc.c:882-926`). 이때는 하이퍼바이저 틱이 깨웁니다 [가설].
- Windows 11 ARM64가 FFH(PSCI) 방식 `_LPI`를 받아들이는지 Microsoft 문서로 확인하지 못했습니다.

### 3.6 대략 크기: 1단계 **작음**, 2단계 **작음**, 3단계 **큼**

---

## 4. RAM

### 4.1 지금 상태 — 16 GiB가 어디에 쓰이는지 (지금 켜진 구성)

| 구간 | 주소 | 크기(MiB) | 누가 씀 | 근거 |
|---|---|---|---|---|
| DRAM 전체 | 0x800000000~0xc00000000 | 16,384 | — | [사실] 실행 로그 436행 |
| iBoot 앞 구간 | 0x800000000~0x801224000 | 18.14 | iBoot 영역. 기존 S231은 그대로 연결 | [사실] HV 로그 2행 `DRAM owner region-id-24 0x801224000+0x3dfddc000`, `J274-GUEST-LAYOUT.md` L-5 |
| 하이퍼바이저 m1n1 이미지·자기 힙 등 | 0x801224000~0x80c50c000 | 178.91 | 하이퍼바이저(이미지 2,293,760 B) + m1n1 힙 128 MiB + 기타 | [사실] 주소는 실행 로그 433행 `heap_limit=0x80c50c000`. 128 MiB는 `N/m1n1_windows/proxyclient/m1n1/proxyutils.py:111` |
| 맥북 연결용 프록시 힙 | 0x80c50c000~0x83c50c000 | 768 | Python 쪽 할당. 지금 소리 버퍼(0x80c630000)도 여기 | [사실] 실행 로그 437행, `run_guest.py:47`(`heap_size = 768 MiB`) |
| 게스트 RAM 전체 | 0x83c50c000~0xbe1000000 | 14,922.95 | Windows 쪽 | [사실] 실행 로그 393행 |
| ├ 그중 앞부분(ADT·UEFI 묶음 등) | 0x83c50c000~0x83d5bc000 | 16.69 | 게스트 부팅 자료 | [사실] 실행 로그 394행 |
| ├ Windows에 알린 높은 RAM | 0x83c50c000~0xae0fcc000 | 10,826.75 | — | [사실] HV 로그 14행 |
| │ ├ 그대로 남긴 10 GiB | 0x83c50c000~0xabc50c000 | 10,240 | Windows | [사실] HV 로그 47행 `high_keep` |
| │ │ └ 그 안의 GPU 로컬 예약 | 0xa00000000~0xa40000000 | 1,024 | GPU 드라이버 | [사실] HV 로그 4·18행 |
| │ └ UEFI가 감춘 구간 | 0xabc50c000~0xae0fcc000 | 586.75(이 중 일반 메모리 536.4가 감춰짐) | UEFI 런타임·DART 표 + **Windows가 못 쓰는 일반 메모리** | [사실] HV 로그 47행 `hidden_pages=0x21872`(= 536.4 MiB), 48행 `hidden_MB=536` |
| └ 낮은 창(4 GiB) 실제 자리 | 0xae0fcc000~0xbe0fcc000 | 4,096 | Windows가 주소 0~4 GiB로 보는 RAM(USB 32비트 DMA용). **Windows가 일반 RAM으로 씀** | [사실] 실행 로그 419·421행, HV 로그 48행 `window_kept=[0,0x100000000)` |
| 게스트 RAM 끝 짜투리 | 0xbe0fcc000~0xbe1000000 | 0.20 | 보호 페이지·로그 버퍼·SIO 자료 | [사실] 실행 로그 352행 `guest_skew_terms` |
| RAM 끝 위(Apple 소유) | 0xbe1000000~0xc00000000 | 496 | 아래 4줄 + 빈 곳 | [사실] 실행 로그 55-57·397-399행 |
| ├ TZ1 | 0xbe1000000~0xbe1008000 | 0.03 | 보안 영역 | 같음 |
| ├ TZ0 | 0xbe1008000~0xbe2c60000 | 28.34 | 보안 영역 | 같음 |
| ├ 화면 메모리(vram) | 0xbe3f60000~0xbe5460000 | 21.00 | iBoot HDMI 화면 | [사실] 실행 로그 352행 |
| └ TZ3 | 0xbe6e20000~0xbffe20000 | 400 | 보안 영역 | 같음 |
| UEFI가 따로 예약한 것 | 0x840000000+0x5C000, 낮은 창 0xC0000000+1 MiB | 0.36, 1 | 넘겨주기 자료, PCIe 다리 창 | [사실] HV 로그 16·17행 |

### 4.2 RAM 줄이기의 이유(기록)

| 장치 | 이유 | 근거 |
|---|---|---|
| 낮은 창 4 GiB + 알린 크기 −4 GiB(S102) | USB-A(FL1100)가 64비트 주소를 못 씀(AC64=0). 그리고 예전 UEFI 덮어쓰기 고장 대책 | [사실] HV 로그 40행 `AC64=0`, `memory-s167/README.md`, `J274-GUEST-LAYOUT.md` L-3 |
| 높은 RAM을 4→8→10 GiB로 단계별로만 늘림 | 실기기 검사를 단계마다 함. "전부 남기기(full=1)"는 시험한 적 없음 | [사실] `memory-s167/README.md`, `memory-s172/README.md`, HV 로그 38행 `full=0` |

### 4.3 되찾을 수 있는 것

| 항목 | 크기(MiB) | 되찾는 방법 | 조건·위험 | 판정 |
|---|---|---|---|---|
| UEFI가 감춘 일반 메모리 | 536.4 | 감추기 상한을 "전부"로(full=1) | [가설] 높은 주소 버퍼를 USB가 직접 쓰는 경우 문제 여부가 미해결(`memory-s167/README.md` "High Windows client-buffer DMA bounce behavior is still an open question") | **먼저 할 일** |
| 맥북 연결용 프록시 힙 + m1n1 힙 | 768 + 128 | 맥북 없는 부팅이 되면 필요 없음. 개발 중에는 `run_guest.py` 힙 크기를 줄여 게스트 시작을 앞으로 당길 수 있음 | 소리 버퍼가 지금 이 힙에 있음 → 옮겨야 함. 참조 프로젝트 방식은 시작을 0x850000000에 고정해 오히려 약 315 MiB 늦어짐 [사실·계산] `J274-GUEST-LAYOUT.md` §2 | 맥북 없는 부팅 때 |
| GPU 로컬 예약 | 1,024 중 일부 | 풀 창 방식(g248C)을 쓰면 Mesa 버퍼가 공유 쪽으로 가서 예약을 줄일 수 있음 | g248C 실기기 미검증 [사실] `HANDOFF-2026-11-16-evening.md:84,187`. 예전에 384 MiB에서 287/320 MB가 참 | GPU 담당과 같이 |
| iBoot 앞 18 MiB, TZ0/1/3 429 MiB, vram 21 MiB | 468 | 없음 | Apple·보안 영역 [가설: TZ3 400 MiB의 주인은 모름] | 되찾지 않음 |
| 게스트 RAM 안의 iBoot 예약 구역(region-id-48 등 20개) | 모름 | 주인 확인 뒤 2단계 지도에서 빼거나 받아들임 | 주인 모름. NVMe 쪽이면 macOS 저장 장치에 닿을 수 있는 경로 [가설] `J274-GUEST-LAYOUT.md` L-8 | 확인부터 |

### 4.4 메모리 속성

| 항목 | 지금 | 근거 |
|---|---|---|
| 하이퍼바이저 2단계 지도의 RAM 속성 | 일반 쓰기 저장(write-back) = Windows가 정한 속성이 그대로 이김 | [사실] `HV/hv_vm.c:155-162`(FWB 안 씀) |
| 일부러 캐시 없는 속성으로 바꾸는 곳 | FL1100 USB 고리 4곳만 | [사실] `HV/hv_vm.c:2999`, 쓰는 곳 `:4086-4109` |
| GPU 로컬 예약 | 주소 그대로(IPA = PA) 연결, 속성 바꾸지 않음 | [사실] `HV/hv_agx_local_reserve.c:69-80`(V2) |
| UEFI 링 버퍼 | WC 할당 | [사실] HV 로그 45·52행 |

### 4.5 목표 상태

- Windows가 쓸 수 있는 RAM: 지금보다 약 536 MiB 많게(감춤 해제), 맥북 없는 부팅에서는 약 896 MiB 더.
- GPU 예약은 실제 쓰는 양에 맞춰 조절.
- 게스트 RAM 안 iBoot 예약 구역의 주인을 알고, 위험한 것은 지도에서 뺀 상태.

### 4.6 성능 코어에서만 캐시 없는 읽기가 느린 문제

측정값 [사실] `N/research-20261116/RESULT-20261116-afternoon.md:48-58`:

| 조건 | 8 MB 읽기(ms) |
|---|---|
| 일반 RAM → RAM | 0.49 |
| GPU 메모리, 캐시 / WC / 캐시 없음 연결 | 5.66 / 5.66 / 5.69 |

| CPU | GPU 메모리 읽기(ms) | RAM 복사(ms) | 계산 2천만 회(ms) |
|---|---|---|---|
| 0~3(효율) | 5.7 | 0.43 | 79 |
| 4~7(성능) | 34.6 | 0.18 | 48 |

가설과 가르는 시험:

| 가설 | 내용 | 가르는 시험 | 기대 결과 |
|---|---|---|---|
| H1 | Windows가 이 구역을 "RAM이 아닌 장치 주소"로 보고, 캐시 요청과 상관없이 장치 메모리(Device) 속성으로 연결함. 세 연결 방식이 같은 속도인 것과 맞음 | 하이퍼바이저가 그 순간 Windows의 1단계 페이지 표를 직접 읽어 실제 속성(MAIR 칸)을 출력(하이퍼바이저는 게스트 메모리를 읽을 수 있음) | Device면 H1 |
| H2 | 같은 물리 주소를 드라이버 다른 곳이 WC로 먼저 연결해 Windows가 그 속성을 따름(기존 가설, 같은 문서 37행) | 위 시험과 같음. 연결이 여러 개면 모두 출력 | 모두 Normal-NC면 H2 |
| H3 | 성능 묶음은 캐시 없는 읽기를 한 번에 하나씩만 처리해(또는 경로가 길어) 느림. 속성과 상관없는 칩 성질 | 일반 RAM을 Windows에서 `PAGE_NOCACHE`와 `PAGE_WRITECOMBINE`으로 따로 받아 효율·성능 코어에서 같은 8 MB 읽기 | 일반 RAM에서도 성능 코어만 느리면 H3 |
| H4 | 성능 묶음 속도 단계와 관계 | §2가 생긴 뒤 성능 7단계와 12단계에서 같은 시험 | 단계에 비례하지 않으면 H4 아님 |

이 문제의 실제 영향: 하이퍼바이저가 DCP로 화면을 바로 보여 주는 지금 구성에서는 복사를 쓰지 않습니다. 대체 구성(g249D)에서만 영향이 있습니다 [사실] `HANDOFF-2026-11-16-evening.md:159`.

### 4.7 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 방법 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | GPU 1 GiB 예약 뒤 Windows RAM을 다시 잽니다(측정만) | 작업 파일 하나(`Win32_ComputerSystem.TotalPhysicalMemory`, `Win32_PhysicalMemory`, 작업 관리자 "하드웨어 예약" 값) | 숫자 기록 | 없음 | 없음 | 없음 |
| 2 | 게스트 RAM 안 iBoot 예약 구역 표를 지금 부팅에서 읽고 주인(ADT 노드)을 짝짓습니다(읽기만) | 없음(`N/port-j274/tools/extract_adt_facts.py` 사용) | 구역마다 주인 이름 | 없음 | 없음 | 없음 |
| 3 | 감추기 상한을 전부로(full=1) 바꾼 펌웨어 시험. USB-A 메모리·키보드, NVMe 읽기·쓰기 체크섬, 메모리 시험(`memory-s171-test/MEMTEST.EXE`를 12 GiB로) | `MU/Silicon/Apple/AppleSiliconPkg/Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c`의 keep 값 | Windows RAM +약 536 MiB, 체크섬 전부 맞음, USB 장치 오류 0 | USB-A 장치를 꽂아 두는 일(이미 꽂혀 있으면 없음) | 데이터: **중간**(USB DMA가 4 GiB 위 주소를 받으면 엉뚱한 곳에 쓰기 [가설]). 시험 중 Windows 쓰기 구역 밖 쓰기는 하이퍼바이저가 막음 [사실] 실행 로그 `S98 ... data writes LBA 39944960-59968629` | 1단계 |
| 4 | 성능 코어 느린 읽기 가르기: H1/H2 시험(하이퍼바이저 페이지 표 읽기) → H3 시험(실행 파일) | 하이퍼바이저 진단 명령 하나(기존 시험 명령 0x1180대 옆), 새 시험 실행 파일 | §4.6 표 | 없음 | 낮음(읽기만) | 없음 |
| 5 | 맥북 없는 부팅 설계 때 프록시 힙 자리를 게스트에 돌려주는 배치(소리 버퍼 옮기기 포함) | `N/port-j274/config/j274-guest-layout.json` 초안, 실행기 | 같은 메모리 시험 | 없음 | 부팅: 중간 | 맥북 없는 부팅 작업(별도 문서) |

### 4.8 재사용할 기존 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| 감추기 상한·분할 코드와 시험 | `N/nwoas_scripts/memory-s167/`, `memory-s172/`, `memory-s180/` | 우리 코드 |
| 메모리 시험 실행 파일 | `N/nwoas_scripts/memory-s171-test/memtest.c` | 우리 코드 |
| 배치 검사 도구 | `N/port-j274/tools/check_j274_layout.py`, `extract_adt_facts.py` | 우리 코드 |

### 4.9 모르는 점

- 지금 Windows가 실제로 쓰는 RAM(1 GiB 예약 뒤).
- TZ3 400 MiB와 게스트 RAM 안 예약 구역 20개의 주인.
- USB(FL1100)가 4 GiB 위 버퍼를 받는 경로가 Windows 안에 있는지.

### 4.10 대략 크기: 측정·감춤 해제 **작음**, 느린 읽기 원인 **중간**, 맥북 없는 배치 **큼**

---

## 5. 타이머·인터럽트 부담

### 5.1 지금 상태(코드에서 확인한 구조, 횟수는 실측 없음)

| 경로 | 동작 | 근거 |
|---|---|---|
| 하이퍼바이저 자체 틱 | 초당 5,000번. ECV가 없어서 보조 CPU도 같은 간격 | [사실] `HV/hv.c:24,127,134-142`, ECV 없음은 §공통 |
| 틱 처리 | CPU0: 감시 타이머, 맥북 통로 확인, 가상 UART, NVMe 확인, GPU·화면 서비스. 다른 CPU: FIQ 갱신 후 바로 돌아감 | [사실] `HV/hv.c:478-495`, `HV/hv_exc.c:2346-2350,2382-2388` |
| 게스트 타이머 넣기 | CPU마다 최소 간격: Windows 커널 단계 약 1 ms(24,000,000 >> 10 = 23,437 tick), UEFI·winload 단계 약 62 ms | [사실] `HV/hv_exc.c:350-421`, `:386-387` |
| 장치 인터럽트 | AIC → 하이퍼바이저 → `aic_ack` → 가상 GIC 목록 레지스터에 넣음. 우선순위는 Windows가 정한 값(g252J에서 고침) | [사실] `HV/hv_exc.c:2082-2130`, `HANDOFF-2026-11-16-evening.md:233-245` |
| CPU 사이 인터럽트(IPI) | Windows의 `ICC_SGI1R` 쓰기를 붙잡음 → 대상 CPU 대기열 + 실제 IPI | [사실] `HV/hv_exc.c:1122-1171` |
| WFI | 매번 붙잡힘(§3) | [사실] |
| 느린 처리 경로 | 모든 CPU가 하나의 잠금(`bhl`)을 차례로 잡음 | [사실] `HV/hv_exc.c:1838` |
| 시간 감추기 | 맥북 통로를 쓰는 동안 흐른 시간을 가상 카운터에서 뺌 | [사실] `HV/hv_exc.c:114-118,1859` |

계산으로 나오는 값(실측 아님):

| 항목 | 값 | 계산 |
|---|---|---|
| 하이퍼바이저 틱으로 인한 나가기(번/초) | 40,000 | 5,000 × 8 CPU |
| Windows 타이머가 1 ms보다 짧게 요청할 때 | 1 ms 근처로 늦춰짐 | 최소 간격 23,437 tick |

### 5.2 목표 상태

- CPU별·종류별 나가기 횟수와 걸린 시간을 언제든 읽을 수 있습니다.
- 보조 CPU의 쓸데없는 틱을 줄입니다(§3 쉬기와 함께 전력에 직결).
- Windows 고해상도 타이머(1 ms 이하)가 늦춰지지 않습니다.

### 5.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 방법 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | CPU별 계수기: WFI, 틱 FIQ, 타이머 넣기(넣음/건너뜀), 장치 IRQ, IPI 붙잡기, 데이터 중단(MMIO), MSR, 잠금 기다린 시간. 10초마다 한 줄(출력 수 제한) 또는 시험 명령으로 읽기 | `HV/hv_exc.c` 각 분기, 새 `HV/nwoas_stats.c` | 가만히 둘 때 / PERCPU 중 / 화면 움직일 때 세 조건 표 | 없음 | 낮음(계수만. 출력은 틱 밖에서) | 없음 |
| 2 | Windows 쪽 지연 시험 실행 파일: `timeBeginPeriod(1)` + `Sleep(1)` 1,000번의 실제 간격, QPC 간격, CPU 사이 깨우기 왕복 시간 | 새 실행 파일(`PERCPU.EXE` 만드는 방식, `N/nwoas_scripts/cpufreq-s164/build.sh`) | 평균·99% 값 표 | 없음 | 없음 | 없음 |
| 3 | 보조 CPU 틱 줄이기: 보조 CPU는 5 kHz 대신 낮은 간격(예 100 Hz)으로. 깨움·모으기(rendezvous) 경로가 틱에 기대는지 먼저 코드로 확인 | `HV/hv.c:134-142`, `HV/hv_exc.c:2346-2388` | 1단계 계수 비교, 안정성 2시간, 모으기(`hv_rendezvous`) 쓰는 맥북 명령 동작 | 없음 | 부팅·멈춤: **중간**(9월 "CPU 1~7 모으기 실패" 같은 문제 [사실] `REF/documentation/PLATFORM_STABILITY.md:20-30`) | 1단계, §3 1단계 |
| 4 | 커널 단계 타이머 최소 간격을 1 ms → 더 짧게(또는 없앰). 근거: 커널은 타이머를 미래로 다시 맞추므로 같은 시각 반복 문제가 없음(주석 `HV/hv_exc.c:360-367`) | `HV/hv_exc.c:386-387` | 2단계 시험 + 안정성 | 없음 | 중간(타이머 폭주 시 멈춤 [가설]) | 1·2단계 |

### 5.4 재사용할 기존 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| 참조의 "목록 레지스터 다시 계산을 줄인" 수정 | `REF/documentation/PLATFORM_STABILITY.md:30-35` 설명, 코드 `REF/m1n1_windows/src/hv_vgic.c` | MIT |
| 기존 진단 출력 제한 방식 | `HV/hv_exc.c:388-398`(`NWOAS-TINJ`) | 우리 코드 |

### 5.5 모르는 점

- 나가기 한 번에 드는 시간(µs).
- Windows가 실제로 요청하는 타이머 주기(동적 틱 여부).
- 잠금(`bhl`)을 기다리는 시간이 GPU·NVMe 처리 중 얼마나 되는지.

### 5.6 대략 크기: 계수기 **작음**, 틱 줄이기·간격 완화 **중간**

---

## 6. 전원 상태 바꾸기, 전원 버튼, RTC, SMC(팬·온도), 발열 안전

### 6.1 지금 상태

| 항목 | 지금 | 근거 |
|---|---|---|
| Windows 재시작 | PSCI `SYSTEM_RESET` → `reboot()` → 감시 타이머로 SoC 전체 재설정 → 맥미니 처음부터 → 맥북에서 실행기를 다시 띄워야 함 | [사실] `HV/hv_psci.c:1523-1529`, `HV/start.S:212-222`, `HV/wdt.c:36-44`, `N/NWOAS-STATUS-2026-11-15.md:36` |
| Windows 끄기 | PSCI `SYSTEM_OFF` → `flush_and_reboot()` = **재시작** | [사실] `HV/hv_psci.c:1539-1541`, `HV/utils.c:129-133` |
| 끄기·재시작 전 SSD 정리 | 하이퍼바이저에 없음(맥북 없는 부팅 전에 고쳐야 함) | [사실] `N/nwoas_scripts/plan-20261115/standalone.md:129,201` |
| 재설정 레지스터(FADT) | 비어 있음 | [사실] `MU/Platform/MacMini2020Pkg/AcpiTables/FADT.aslc:74` |
| 절전(모던 스탠바이) | FADT에 "S0 저전력 쉬기 가능" 표시. 화면 끄기·켜기·키로 깨우기 됨 | [사실] `FADT.aslc:73`, `HANDOFF-2026-11-16-evening.md:15,163-172` |
| 절전 중 CPU | §3 때문에 쉬지 않음 [가설] | — |
| 전원 버튼 | ACPI에 전원 버튼 장치(PNP0C0C)·이벤트 장치(GED) 없음. FADT에는 "전원 버튼은 제어 방식 장치" 표시만 | [사실] `MU/.../DSDT.asl` 검색 결과 없음, `FADT.aslc:73` |
| RTC 읽기 | UEFI가 부팅 때 PMU RTC를 직접 읽어 시각을 씀 | [사실] HV 로그 32행 `S192 RTC DIRECT SERA ... => 2026-11-16 11:28:13 UTC` |
| RTC 쓰기(SetTime)·깨우기 알람 | 지원 안 함 | [사실] `N/nwoas_scripts/rtc-s188/README.md` |
| Windows가 RTC를 UTC로 보는 설정 | 9월 옛 설치본에는 `RealTimeIsUniversal=1`을 넣음. **지금 설치본은 기록 못 찾음** | [사실] `N/nwoas_scripts/rtc-s183/set-utc-policy.cmd`, `rtc-s185/hardware-first-boot.json` |
| SMC | 하이퍼바이저 m1n1에 초기화·4바이트 쓰기·종료만 있음(읽기 함수 없음). 화면 전원 GPIO 켤 때만 씀 | [사실] `HV/smc.c:115-180`, `HV/dcp.c:52-58` |
| SMC 사용 시 주의 | 시작·멈춤 메시지가 SMC 상태를 바꾸므로 9월에는 게스트 시작 전에만 읽음 | [사실] `N/nwoas_scripts/rtc-s183/README.md` |
| 온도·팬 | Windows는 온도를 모름(ACPI 열 영역 없음). 팬은 [가설] SMC 펌웨어가 스스로 돌림 | `MU/.../DSDT.asl`에 `_TZ` 없음 [사실] |
| 칩 스스로 하는 제한 | 전력 제한(ppt)·AMX 제한 켜짐 | [사실] §2.1 |
| 오래 돌린 기록 | Cinebench 약 12분 1회 점수 나옴, 5분 계산+읽기 통과 | [사실] `memory-s180/cinebench-result.json`(render 728,441 ms), `cpufreq-s193/soak-result.json` |

Linux(Asahi)가 쓰는 SMC 방법 [사실]:

| 기능 | Linux 방법 | 출처 |
|---|---|---|
| 끄기 | SMC 키 `MBSE`에 `off1` 쓰기(PMU 비트 하나가 있어야 꺼진 채로 있음). 재시작은 `phra` | [macsmc-reboot.c](https://sources.debian.org/src/linux/6.18.9-1/drivers/power/reset/macsmc-reboot.c) |
| 전원 버튼 | SMC 알림을 받는 `macsmc-input` | [패치 v6](https://lkml.iu.edu/hypermail/linux/kernel/2512.1/05184.html) |
| 온도·전압·전력·팬 | `macsmc-hwmon`. 수동 팬 조절은 기본으로 꺼져 있고 "안전하다고 증명할 수 없다"고 적혀 있음 | [문서](https://kernel.org/doc/html/next/hwmon/macsmc-hwmon.html) |
| SMC 위치 | `smc@23e400000`, 공유 메모리 0x23fe00000+0x100000, 자식 `reboot`·`hwmon`·`rtc`·`gpio` | Linux `t8103.dtsi` |

### 6.2 목표 상태

| 기능 | 목표 |
|---|---|
| 재시작 | SoC 재설정은 그대로 둡니다(가장 깨끗합니다). 그 전에 SSD를 정리합니다. 맥북 없는 부팅이 되면 스텁 → m1n1 → Windows가 자동으로 이어집니다 |
| 끄기 | 실제로 꺼집니다(SMC `MBSE`) |
| 절전 | 화면 꺼짐 + 코어 실제로 쉼 + 속도 낮음. 깨우기는 지금처럼 키 |
| 전원 버튼 | 짧게 누르면 Windows가 설정대로(절전 또는 끄기) |
| RTC | Windows 시간 맞추기가 RTC에 저장됨(선택) |
| 온도 | Windows가 SoC 온도를 보고, 하이퍼바이저가 너무 뜨거우면 스스로 속도를 낮춤 |
| 팬 | SMC에 맡김(손대지 않음) |

### 6.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 방법 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 재시작·끄기 전에 SSD 정리(`nvme_flush`·`nvme_shutdown` 해당 함수) | `HV/hv_psci.c:1523-1541` | Windows 재시작 10번 뒤 `chkdsk /scan` 무오류, 이벤트 로그에 "예기치 않은 종료" 없음 | 없음 | 데이터: 낮음(지금보다 안전해짐). 정리 중 멈추면 재시작이 안 됨 → 시간 제한 | 없음 |
| 2 | 진짜 끄기: 하이퍼바이저에 SMC 쓰기로 `MBSE`=`off1`. Linux가 쓰는 PMU "꺼진 채 유지" 비트는 처음에 쓰지 않고 결과만 봅니다. 실패하면 지금처럼 재시작으로 | `HV/hv_psci.c:1539`, `HV/smc.c`(키 쓰기 재사용) | Windows "시스템 종료" → 맥미니 전원 LED·팬이 꺼지는지 | **필요**: 꺼졌는지 눈으로 확인, 전원 버튼으로 다시 켜기 | 부팅: 중간(다시 켤 때 iBoot가 복구 화면으로 갈 가능성 [가설]). 데이터: 1단계 뒤라 낮음. SMC 소유: 하이퍼바이저가 끄기 직전에만 SMC를 여니 충돌 낮음 [가설] | 1단계 |
| 3 | 전원 버튼: 하이퍼바이저가 SMC 알림을 받도록 SMC를 계속 열어 둠 → 버튼 알림이 오면 가상 인터럽트 → DSDT의 GED(ACPI0013) `_EVT`가 PNP0C0C에 Notify(0x80) | `HV/smc.c`(알림 받기 추가), 새 하이퍼바이저 모듈, `MU/.../DSDT.asl` | 짧게 누르기 → Windows 전원 설정 동작. 길게 누르기 → 하드웨어 강제 끄기가 여전히 되는지 | **필요**: 버튼 누르기 | 부팅: 낮음. SMC를 오래 열어 두는 것이 화면 GPIO 등 다른 SMC 사용과 겹치는지 [가설] | 2단계 |
| 4 | 온도 읽기(읽기만): 하이퍼바이저에 SMC 키 읽기 함수 추가, 온도 키 몇 개와 팬 회전수를 10초마다 한 줄 | `HV/smc.c`(읽기 추가), 새 모듈. 키 이름은 `macsmc-hwmon`이 쓰는 J274 목록에서(값만 참고) | 가만히 / PERCPU / Cinebench 세 조건 온도·팬 표. §3 1단계 전후 비교 | 없음 | 낮음(읽기만). SMC 시작 메시지가 상태를 바꾸는 점은 9월 기록대로 조심 | 없음(§3보다 먼저 하면 전후 비교 가능) |
| 5 | 발열 안전 하한: 온도가 정한 값을 넘으면 하이퍼바이저가 성능 묶음을 7단계로 내리고, 내려가면 되돌림. Windows와 상관없이 동작 | `HV/nwoas_cpufreq.c`(§2), 4단계 모듈 | 기준 온도를 일부러 낮게 잡고 Cinebench로 하한이 걸리는지 | 없음 | 발열: 위험을 줄이는 단계. 값을 너무 낮게 잡으면 성능만 떨어짐 | 4단계, §2 1단계 |
| 6 | Windows에 온도 보이기: DSDT 열 영역(`_TZ`)의 `_TMP`가 하이퍼바이저 가짜 주소를 읽음. 수동 냉각 기준(`_PSV`)은 §2 B 방식이 될 때만 | `MU/.../DSDT.asl`, 하이퍼바이저 가짜 주소 | Windows `Get-CimInstance MSAcpi_ThermalZoneTemperature -Namespace root/wmi` | 없음 | 부팅: 낮음 | 4단계 |
| 7 | RTC 쓰기(선택): UEFI SetTime이 PMU RTC 보정값(0xd100)을 씀 | `N/nwoas_scripts/rtc-s188/NwoasHardwareBootRtcLib/` | Windows 시간 맞춘 뒤 재시작 → 시각 유지, macOS 시각도 맞는지 | macOS로 부팅해 시각 확인(선택) | 낮음~중간(macOS 시각에 영향 [가설]) | 없음 |
| 8 | 지금 설치본 시간 설정 확인(`RealTimeIsUniversal`) | 조회 작업 파일 | 값 출력 | 없음 | 조회만 | 없음 |

### 6.4 재사용할 기존 코드와 라이선스

| 무엇 | 위치 | 라이선스 |
|---|---|---|
| m1n1 SMC 클라이언트 | `HV/smc.c` | MIT |
| 9월 SMC·PMU 읽기 도구와 결과 | `N/nwoas_scripts/rtc-s183/bounded_smc.py`, `rtc-s184/nwoas_sera_rtc.c` | 우리 코드 |
| RTC UEFI 라이브러리 | `N/nwoas_scripts/rtc-s188/NwoasHardwareBootRtcLib/` | 우리 코드 |
| Linux macsmc-reboot·input·hwmon | 위 링크 | GPL-2.0 — **키 이름·값·순서만 참고, 코드 복사 금지** |
| OpenBSD PMU RTC | `aplpmu.c` (`rtc-s183/README.md`에 위치) | ISC(OpenBSD 관례) [가설: 파일 머리말 확인 필요] |

### 6.5 모르는 점

- SMC를 Windows 실행 중 계속 열어 두어도 되는지(9월에는 게스트 시작 전에만 씀).
- `off1`만 쓰고 PMU 비트를 쓰지 않으면 꺼진 채로 있는지.
- 지금 쉬지 않는 상태(§3)에서 실제 온도. 측정 전에는 "위험하다/안전하다" 둘 다 말할 수 없습니다.
- 팬을 SMC가 스스로 돌린다는 것은 가설입니다. 4단계에서 회전수가 부하에 따라 바뀌는지로 확인합니다.

### 6.6 대략 크기: 1단계 **작음**, 2단계 **작음~중간**, 3단계 **중간**, 4~5단계 **중간**, 6단계 **작음**, 7단계 **작음**

---

## 7. Windows에서 "제대로 된" CPU 느낌에 필요한 것

### 7.1 지금 상태

| 보이는 곳 | 지금 | 근거 |
|---|---|---|
| 작업 관리자 CPU 수 | 8 | [사실] |
| 코어 종류 | 모두 같음 | [사실] §1 |
| 표시 속도 | Windows가 30 MHz로 보고한 기록 | [사실] `memory-s180/cinebench-result.json` limitations, `NWOAS-MORNING-2026-09-10.md:15` |
| SMBIOS 최대 속도 / 현재 속도(MHz) | 3228 / 0 | [사실] `MU/Silicon/Apple/AppleSiliconPkg/Drivers/SmbiosInfoDxe/SmbiosInfoDxe.c:297-298`. 9월 S142에서 현재 속도를 3228로 바꾼 적 있으나 지금 트리는 0 (`N/nwoas_scripts/smbios-s142/build_payload.py`) |
| M1 실제 최고(MHz) | 3204(터보, 꺼 둠), 일반 최고 2988 | [사실] Linux dtsi. [가설] 3228은 다른 칩(M1 Pro/Max) 값을 옮긴 것 |
| 코어 쉬게 하기(코어 주차) | 효과 없음 [가설]: 주차된 코어도 §3 때문에 계속 돎 | — |
| PMU 사이클 카운터 | Windows `PMCCNTR_EL0` 읽기를 Apple PMC0으로 돌려줌 | [사실] `HV/hv_exc.c:1271-1297` |

### 7.2 목표 상태

| 항목 | 목표 |
|---|---|
| 코어 종류 | 효율 4 + 성능 4로 보임(§1) |
| L2 캐시 | 4 MiB·12 MiB로 보임(§1 3단계) |
| 기본 속도 | 실제 값(성능 2988 MHz)에 가깝게 |
| 현재 속도 | 단계가 바뀌면 따라 바뀜(§2 B가 되면) |
| 가만히 둘 때 CPU 사용률 | 거의 0%, 코어 실제로 쉼(§3) |
| 절전 | 실제로 전력이 줄어듦 |

### 7.3 단계별 계획

| 단계 | 할 일 | 바꿀 파일 | 실기기 검증 방법 | 콘치님 손 | 위험 | 선행 조건 |
|---|---|---|---|---|---|---|
| 1 | 지금 값 모으기: `wmic cpu get CurrentClockSpeed,MaxClockSpeed`, 레지스트리 `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\~MHz`, `GetSystemCpuSetInformation` | 조회 작업 파일 | 표로 기록 | 없음 | 없음 | 없음 |
| 2 | 30 MHz의 출처 찾기: 부팅 중 Windows의 `PMCCNTR` 읽기와 `CNTVCT` 읽기 기록(하이퍼바이저 기존 `NWOAS-PMCC` 32줄 출력)으로 Windows가 속도를 어떻게 재는지 봅니다 | 없음(기존 출력) 또는 출력 제한 늘리기 | `~MHz` 값과 PMC0 증가 속도 비교 | 없음 | 낮음 | 1단계 |
| 3 | SMBIOS 값을 실제에 맞춤(최대 3204 또는 2988, 현재 2988). 어느 쪽이 Windows 표시를 바꾸는지는 2단계 결과로 정함 | `SmbiosInfoDxe.c:297-298` | 작업 관리자·`wmic` 값 | 화면 확인(선택) | 낮음 | 2단계 |
| 4 | §1·§3·§2를 차례로 끝낸 뒤 종합 확인: 가만히 둘 때 사용률, 코어 종류, 속도 변화, 절전 중 온도 | 없음 | 종합 표 | 화면 확인(선택) | 없음 | §1·§2·§3 |

### 7.4 재사용할 기존 코드와 라이선스: SMBIOS 드라이버(AppleWOA, BSD-2-Clause-Patent OR MIT), 9월 S142 빌드 스크립트(우리 코드).

### 7.5 모르는 점

- Windows ARM64가 작업 관리자 "속도"를 어디서 읽는지(SMBIOS, 부팅 때 측정, `_CPC`). 2단계로 일부 가립니다.

### 7.6 대략 크기: **작음**(표시만), 실제 속도 변화 표시는 §2 B에 묶임

---

## 8. 모든 단계를 한 줄로 (의존 순서)

| 순서 | 항목 | 크기 | 콘치님 손 |
|---|---|---|---|
| 1 | §5-1 나가기 계수기 + §6-4 온도 읽기(전후 비교 기준) | 작음 + 중간 | 없음 |
| 2 | §4-1·§7-1·§3-4·§6-8 현재 값 조회 | 작음 | 없음 |
| 3 | §3-1 실제로 쉬기 | 작음 | 화면·입력 반응 확인 |
| 4 | §1-1 코어 종류 | 작음 | 없음 |
| 5 | §6-1 끄기·재시작 전 SSD 정리 | 작음 | 없음 |
| 6 | §2-1·2 하이퍼바이저 속도 조절 | 중간 | 없음 |
| 7 | §6-5 발열 안전 하한 | 중간 | 없음 |
| 8 | §4-3 감춘 536 MiB 되찾기 | 작음 | USB-A 장치 |
| 9 | §5-3·4 틱 줄이기, 타이머 간격 완화 | 중간 | 없음 |
| 10 | §6-2 진짜 끄기 | 작음~중간 | 전원 확인·버튼 |
| 11 | §3-2·3 `_LPI`, 깊은 쉬기 | 작음 → 큼 | 없음 |
| 12 | §6-3 전원 버튼, §6-6 Windows 온도 표시, §2-4 `_CPC` | 중간~큼 | 버튼 누르기 |

모든 실기기 단계는 지금 규칙대로 "Windows 재시작 스크립트(`agx-s248/run-reboot-arm-*`)"로 새 판을 올리고, 한 번에 하나만 바꿉니다. **빌드 성공은 동작 확인이 아닙니다.** 각 단계의 성공은 위 "실기기 검증 방법"의 결과로만 판단합니다.

---

## 9. 출처

로컬 파일은 각 줄에 적었습니다. 웹 출처:

- [Linux t8103.dtsi](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/apple/t8103.dtsi) (OPP 표, L2 캐시, SMC 자식 노드)
- [Linux apple-soc-cpufreq.c](https://github.com/torvalds/linux/blob/master/drivers/cpufreq/apple-soc-cpufreq.c)
- [Asahi 진행 보고 Linux 7.2](https://asahilinux.org/2026/08/progress-report-7-2/) (깊은 WFI)
- [T8103 PSCI·쉬기 단계 RFC 6/6](https://lkml.iu.edu/2607.1/00283.html)
- [macsmc-reboot.c (Debian 6.18 소스)](https://sources.debian.org/src/linux/6.18.9-1/drivers/power/reset/macsmc-reboot.c)
- [macsmc-input 패치 v6](https://lkml.iu.edu/hypermail/linux/kernel/2512.1/05184.html)
- [macsmc-hwmon 문서](https://kernel.org/doc/html/next/hwmon/macsmc-hwmon.html)
- [Microsoft 이종 코어 설정 HeteroClass1InitialPerf](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/configuration-for-hetero-power-scheduling-heteroclass1initialperf)
- [Microsoft 이종 코어 설정 SchedulingPolicy](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/configuration-for-hetero-power-scheduling-schedulingpolicy)
- [Linux CPPC 효율 등급 패치(MADT GICC 칸 설명)](https://source-hub.etos-pro.ru/microsoft/WSL2-Linux-Kernel/commit/d3c3db41df7e1bdefc9c68073070b62ce3b260bd)

찾지 못한 것(검색 범위를 밝힘): Windows ARM64가 MADT 효율 칸·시스템 메모리형 `_CPC`·FFH `_LPI`를 받아들인다는 Microsoft 공식 문장 — 제 검색 도구에서 검색어 3개로는 나오지 않았습니다.
