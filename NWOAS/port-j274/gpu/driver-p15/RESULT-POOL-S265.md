# GPU 메모리 한도 올리기 — 풀 창(구역 3) 켜기 (S265, 2026-11-19)

> 표기: **[사실]** = 코드·로그·빌드로 확인. **[계산]** = 코드의 공식에 실제 값을 넣어 계산(실기 미확인). **[가설]** = 확인하지 않은 추측. **[빌드만]** = 만들기만 하고 맥미니에서 돌리지 않음.
> 이 문서의 어떤 것도 맥미니에서 시험하지 않았습니다. 작성 중 맥미니에는 손대지 않았습니다(와이파이 시험 중).

---

## 1. 결론

**드라이버와 하이퍼바이저는 새로 빌드할 필요가 없었습니다. 그래서 2774판은 만들지 않았습니다.** 풀 창을 켠 실행기 사본 하나(`nwoas_scripts/agxdcp-s265-guest-test.sh`)로 GPU가 쓸 수 있는 메모리를 지금의 952 MiB에서 약 5 GiB로 올리는 설계입니다 [빌드만, 실기 미시험].

| 항목 | 지금(S259 실행기) | S265 실행기(예상) | 근거 |
|---|---|---|---|
| 구역 2(펌웨어 예약, 작업 관리자 "전용") (MiB) | 952 | 952 | [사실] 코드·11-16 콘치님 확인 1.0 GB |
| 구역 3(풀, Windows 메모리를 쓸 때만 가져옴) (MiB) | 없음 | 4096 | [계산] 3.3장 |
| 구역 1(시스템 메모리 창, 비로컬) (MiB) | 256 | 256 | [사실] `memory_runtime_windows.c:9-10` |
| 일반 GPU 버퍼 한 덩이 최대 (MiB) | 952 (P22) | 4096 | [사실] `allocation_windows.c:81-109` |
| Windows가 처음부터 잃는 RAM (MiB) | 1024(예약) | 1024 + 하이퍼바이저 표 약 28 | 풀은 앱이 할당할 때만 가져옴 [사실, 코드] |

- **새 빌드가 필요 없는 이유 1 [사실]**: 11-16에 풀 창 경로에서 하이퍼바이저가 죽던 것(`backing_find` NULL)을 고친 g248C 수정은 g249D부터 이후 모든 판(g252J, g253K, g254L, g260O, g262R)에 이미 들어 있습니다. g262R은 오늘 `build-g262R.sh`로 다시 빌드해 실행기에 고정된 해시(`83add623…`)가 그대로 나왔고, 그 소스 `hv_agx_gpuva_v5.c:78`에 `if (!index) return NULL;`이 있습니다. g249D 뒤 패치는 이 파일을 건드리지 않습니다(패치 파일 목록). 그래서 "g262R 위에 보호를 옮기는" 새 판은 만들지 않았습니다.
- **새 빌드가 필요 없는 이유 2 [사실]**: 지금 설치된 2772(P36)도 풀 경로 코드가 켜진 채 빌드되어 있습니다(빌드 기록의 `/D APPLE_AGX_GPUVA_G3_QUALIFICATION=1`). 풀이 있으면 P22(한도)와 P23(D3D12 CPU 풀 매핑)은 코드상 자동으로 원본 windows-on-m1 동작으로 돌아갑니다. 풀 시작이 실패하면 지금과 똑같은 2구역 동작으로 남습니다.
- **가장 큰 위험 [가설]**: 11-16(g247B)에 풀 창 실기에서 GPU를 끈 직후 하이퍼바이저가 죽었습니다. 이 길(드라이버 정지 → 풀 창 명령)을 S265에서는 부팅 직후 D28(GPU 장치 재시작)이 처음 지납니다. 보호 코드는 있지만 실기에서 지난 적은 없습니다. 또 풀 창은 DCP 화면 서비스(g251F 이후)와 함께 돈 적이 한 번도 없습니다.

---

## 2. 지금 Windows가 보는 GPU 메모리와 한도의 출처 [사실]

### 2.1 구역과 예산 묶음

**결론: Windows에 보이는 GPU 메모리는 구역 2(952 MiB, 로컬 묶음)와 구역 1(256 MiB, 비로컬 묶음)뿐입니다. 풀(구역 3)은 실행기가 주소를 0으로 주어 꺼져 있습니다.**

| 구역 | 크기 (MiB) | 예산 묶음 | 정하는 곳 |
|---|---|---|---|
| 1 시스템 메모리 창 | 256 | 비로컬 | `memory_runtime_windows.c:9-10` (`ADMISSION_APERTURE_BYTES`) |
| 2 펌웨어 예약 | 952 = 1024 − 64(개인용) − 8(백엔드) | 로컬 | 하이퍼바이저 `hv_agx_local_reserve.h:20`(g249D 1 GiB) → 영수증 → `memory_windows.c:94-97`, 묶음 `memory_windows.c:17` |
| 3 풀 | 없음 | (켜지면 로컬) | 실행기 `agxdcp-s259-guest-test.sh:71` `NWOAS_AGX_POOL_IPA=0` → 모듈 `agx_hv2_module.py:29` → 하이퍼바이저 "pool window: none configured"(06:29 부팅 HV 기록) |

- ACPI(AGX0 장치, _DSM)는 크기를 정하지 않습니다. 구역 2 크기는 하이퍼바이저 영수증에서 옵니다(`memory_runtime_windows.c:5-6` 주석 M2-f2c) [사실, 코드].
- 게스트 RAM: 06:29 부팅 기록 `mem_size=0x2A49D0000` = 10.57 GiB. 그 안에 1 GiB 예약이 있어 Windows가 쓰는 RAM은 약 9.56 GiB입니다 [계산].
- DXGI가 실제로 알리는 예산(`QueryVideoMemoryInfo`)은 맥미니에서 잰 적이 없습니다. K2·K3는 실기에서 한 번도 돌지 않았습니다(`gpucov/runs/`에 결과 없음). 작업 관리자 "전용 1.0 GB"만 콘치님이 11-16에 확인했습니다.

### 2.2 한도를 넘으면 [사실, 코드]

| 경우 | 일어나는 일 | 위치 |
|---|---|---|
| 한 덩이 > 952 MiB | UMD가 KMD를 부르기 전에 거절 → 앱은 E_OUTOFMEMORY | P22 `allocation_windows.c:96-107`, `agx_win32_screen.c:114` |
| 합계 > 952 MiB | VidMm이 구역 2에서 내보냄. CPU에서 보이는 버퍼는 4 KiB 시스템 쪽에 놓이는데 AGX 주소표(16 KiB 쪽)가 그것을 매핑하지 못해 그 제출이 거절됨 | `allocation_windows.c:585-600` 주석(원본 EXP1265·EXP1267) |
| 실기 확인 | 없음. K3(64 MiB × 16)는 실기에서 돌지 않음 | — |

---

## 3. 설계

### 3.1 후보 비교

**결론: 풀 창(A)을 골랐습니다. 다른 두 방법도 결국 하이퍼바이저를 바꿔야 하고, B는 RAM을 영구히 빼앗습니다.**

| 방법 | 바꿀 곳 | 장점 | 단점 | 판단 |
|---|---|---|---|---|
| A 풀 창 켜기 | 실행기 한 줄 | 원본이 J313에서 쓰던 경로, 쓸 때만 RAM을 가져감, 끄기 쉬움 | 풀 창 + DCP 서비스 조합 실기 미검증 | **택함** |
| B 예약을 2~4 GiB로 | 하이퍼바이저 `HV_AGX_LOCAL_BYTES` + GPU 주소 중개 `HV_AGX_GPUVA_V5_LOCAL_PAGES`(65536 × 16 KiB = 1 GiB 한도, `hv_agx_gpuva_v5.h:26`) + Mu 펌웨어 예약(PEI) | 검증된 구역 2 경로 그대로 | Windows RAM을 부팅부터 2~4 GiB 영구히 잃음, 펌웨어 판 전부(와이파이·전력·SMBIOS) 다시 빌드 | 안 함 |
| C 구역 1(시스템 메모리 창) 키우기 | 드라이버 | 하이퍼바이저 그대로 | VidMm 시스템 메모리는 4 KiB 쪽이라 AGX 16 KiB 주소표에 못 올림(`pool_windows.c:7-8` EXP1244, `allocation_windows.c:585-600`). 결국 연속 주소를 만들어 줄 하이퍼바이저 창이 필요 = A | 안 함 |

### 3.2 풀 창이 하는 일 [사실, 코드]

1. 하이퍼바이저가 IPA 0xd00000000부터 4 GiB 빈 창을 준비합니다(`nwoas_agx_broker.c:116`, `:334-394`, 단계 2 표 `hv_vm.c:4883-4915`). 게스트 RAM과 겹치면 거절합니다.
2. KMD는 창을 열고(`pool_windows.c:103-113`) "범위 매핑" 방식(`hv_agx_pool_window.c:244`의 STATUS_RANGES)이면 빈 구역 3을 만듭니다(`pool_windows.c:1835-1843`).
3. 앱이 할당하면 KMD가 그 크기만큼 Windows 메모리를 64 KiB 단위로 가져와 창에 붙입니다(`allocation_windows.c:605-618`). 다 쓰이지 않으면 되돌려 줍니다.
4. 바탕화면·화면 출력 표면(종류 0)은 지금처럼 구역 2와 1에 남습니다(`allocation_windows.c:574`). DCP 화면 서비스가 보는 메모리는 바뀌지 않습니다.

### 3.3 구역 3 크기 계산 [계산]

**결론: 이 맥미니에서는 창 크기 상한 4096 MiB가 그대로 구역 3이 됩니다.**

| 값 | 크기 (MiB) | 출처 |
|---|---|---|
| Windows RAM(가정) | 약 9790 | 10.57 GiB − 1 GiB 예약 |
| VidMm 그래픽 몫 = RAM × 50 % | 약 4895 | `render_pool_demand.c:289-311`, Windows 기본 정책 |
| 몫 − 창 여유 256 | 약 4639 | `render_pool_demand.c:346` |
| 하이퍼바이저 GPU 표 − 256 | 4352 | 294912쪽 × 16 KiB − 256 (`nwoas_agx_broker.c` select, 11-16 실기 기록 "294912 system pages") |
| 창 크기 | 4096 | `HV_AGX_POOL_WINDOW_CAPACITY` |
| **구역 3 = 가장 작은 값(64 MiB 단위)** | **4096** | `render_pool_demand.c:329-350` |

- Windows가 구역 3을 몫 − 8 MiB보다 크게 알리면 어댑터 시작을 거부하는데(`render_pool_demand.h` rc109 주석), 4096은 4895보다 작아 여유가 있습니다 [계산].
- 하이퍼바이저 힙: 풀 창 표(약 28 MB)는 지금도 설치 검사(`nwoas_agx_broker.c:965`)에 늘 포함되어 있어 켜도 검사 결과는 같습니다. 06:29 부팅의 설치 뒤 남은 힙은 약 112 MiB였습니다(`heap 0x804bde680->0x8055de680`, 한도 0x80c5fc000) [사실]. 켜면 약 84 MiB가 남습니다 [계산].

### 3.4 만든 것

| 파일 | 내용 | sha256 앞 8자리 |
|---|---|---|
| `nwoas_scripts/agxdcp-s265-guest-test.sh` | S259 실행기 사본. 바뀐 것은 풀 IPA(`NWOAS_S265_POOL=1` 기본 → 0xd00000000, `=0`이면 S259와 같음), 기록 파일 이름, 기록 한 줄. 하이퍼바이저·페이로드·모듈·S262 스위치 그대로 | `f0e4295d` |
| `nwoas_scripts/gpupool-s265/boot-s265.sh` | 재시작 → S265 부팅 → (도구 전달) → D28 → `S265 BOOT DONE link=… pool=<0|1>`. 작업 기다리기 중 하이퍼바이저 EL2 예외·실행기 종료를 보면 바로 멈춤 | `463671e7` |
| `nwoas_scripts/gpupool-s265/run-s265-chain.sh` | 부팅(probe11 전달) → K43 + 회귀 시험 → 재시작(종료 경로 시험) → 두 번째 부팅 → K43b·K41. 부팅 완료는 `S265 BOOT DONE link=` 줄로만 기다림 | `34eb4f12` |
| `gpucov/probe/nwgpuprobe_mem.inc` + `nwgpuprobe.cpp` | 새 시험 `pool`(KMD 풀 영수증 해석), `memladder`, `fill2`, `d3d11mem` | — |
| `gpucov/bin/nwgpuprobe11-arm64.exe` / `-x64.exe` | VM 빌드(MSVC 14.44, `build-probe11.cmd`) 273920 / 295936 B | `180d539d` / `eec4c39b` |
| `gpucov/jobs/K43-pool-mem.cmd` / `K43b-pool-receipt.cmd` | 4.2장 시험. ASCII, 2423 / 836 B | `a50d28d6` / `acd871ef` |
| `file-delivery-s243/make-s265.py` → `manifests/gpupool-s265-probe11.json`, `jobs/gpupool-s265-probe11.cmd` | probe11 두 파일을 `C:\NWOAS\gpucov`로 | — |

새 시험의 내용:

| 시험 | 하는 일 | 내용 검사 |
|---|---|---|
| `pool` | `Device Parameters`의 `Wom1M2PoolProbe`(창 상태 10 = 켜짐, 7 = 없음)·`Wom1M2PoolDemand`(구역 3 크기, 가져온 양) 해석 | — |
| `memladder` | D3D12 DEFAULT 버퍼 256·512·1024·1536·2048·2560·3072 MiB를 하나씩 | 64 MiB마다 위치에 따라 다른 무늬를 UPLOAD에서 써 넣고 READBACK으로 읽어 4 KiB마다 한 단어 + 각 64 MiB의 앞 1 MiB 전부 비교(겹침·빠진 쪽 검출). 읽기 속도(ms/8 MiB)도 기록 |
| `fill2` | 256 MiB 힙을 실패할 때까지 붙잡음(최대 20개 = 5 GiB) | 힙마다 앞·끝 64 KiB 표지를 쓰고 마지막에 모두 읽어 비교 |
| `d3d11mem` | D3D11 DEFAULT 버퍼 64~1024 MiB | 16 MiB 스테이징으로 같은 무늬 검사 |

VM(Parallels 장치, `memcap=512`로 크기 제한) 자기 점검 [사실, VM — 맥미니 판단에는 쓰지 않음]:

| 시험 | 결과 |
|---|---|
| memladder 256·512 MiB | PASS, 불일치 0 |
| fill2 2개 | 표지 2/2, 장치 제거 없음 |
| d3d11mem 64·128·256·512 MiB | PASS, 불일치 0 |
| K43 판정 줄 해석 | 동작(VM에는 풀이 없어 FAIL로 나오는 것이 정상) |

### 3.5 바꾸지 않은 것

| 대상 | 이유 |
|---|---|
| 드라이버(2772/2773) | 풀 경로 코드가 이미 들어 있음(1장). 바꾸면 변수가 둘이 됨 |
| 하이퍼바이저(g254L·g262R) | 보호 코드가 이미 있음(1장). 새 판 없음 |
| `agxdcp-s259-guest-test.sh`, out/ 결과물, `m1n1-standalone-s264` | 건드리지 않음(S259 실행기는 지금 와이파이 시험이 돌리는 중) |

---

## 4. 맥미니 시험

### 4.1 명령 (와이파이 시험이 끝난 뒤, `nwoas_scripts`에서)

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
nohup bash gpupool-s265/run-s265-chain.sh <지금 실행기 .out> <지금 .link> > /tmp/claude-501/s265.txt 2>&1 &
# 맥미니가 D4 대기면: run-s265-chain.sh idle -
```

순서: 재시작 → S265 부팅(probe11 전달) → D28 → `K43 K41 K9 K10 K25b K21 K38g K38dg K41` → 재시작 → S265 두 번째 부팅 → D28 → `K43b K41` → `S265 CHAIN DONE`. 결과 `gpupool-s265/runs/chain-s265-*.txt`.

- 2773 연쇄(`agx-s248/run-2773-chain.sh`)와 한 번에 섞지 않습니다. 어느 판 위에서 돌았는지는 K43의 `VC … | 30.0.277x.0` 줄로 남습니다.
- SSD 시험 판으로 하려면 앞에 `NWOAS_S262_HV=1 NWOAS_NVME_QD=…`를 붙입니다(g262R, 보호 코드 있음). 처음 한 번은 기본(g254L)으로 하는 것을 권합니다. 바뀌는 것을 하나로 두기 위해서입니다.

### 4.2 통과 기준

| 단계 | 통과 | 실패로 보는 것 |
|---|---|---|
| 하이퍼바이저 설치 | `pool window v1 IPA=0xd00000000 bytes=0x100000000`, `MEDIATE armed: … pool=1`, `GPU broker table 294912 system pages` | `pool window REFUSED`/`unavailable`, `install REFUSED` → 연쇄가 `POOL NOT ARMED`로 멈춤 |
| D28(첫 정지 경로) | `DEV OK`, `D28 done; HV EL2 exceptions so far: 0` | `ABORT hypervisor/launcher gone during D28-rearm.cmd`(11-16과 같은 자리) |
| K43 `pool` | `window_status=10 (demand_on)`, `segment_mib=4096`(3.3장 계산, 다르면 기록만) | `window_status=7`(창 못 찾음)·5·6(매핑 실패) |
| K43 `dxgi` | 로컬 `budget_mib`가 952 근처에서 수천 MiB로 커짐(정확한 값은 기록) | 952 근처 그대로 |
| K43 `memladder` | 7개 모두 `result=PASS`, `largest_pass_mib=3072` | `mismatches` > 0, `removed` ≠ 0, 만들기 실패 |
| K43 `fill2` | `heaps_256mib` ≥ 12(3 GiB), `tags_ok=n/n`, `removed=0x00000000` | 표지 불일치, 장치 제거 |
| K43 `d3d11mem` | 불일치 0, 장치 제거 없음. 512·1024 MiB의 `create_hr=0x80070057`은 D3D11 런타임의 자원 크기 한도일 수 있어 기록만 [가설] | `mismatches` > 0 |
| K43 VERDICT | `=> PASS` | `=> FAIL` |
| K41 앞·뒤 | `dwm_events=0` | DWM 오류 |
| K9 | 드라이버 기본(풀 매핑 켜짐)·`CPU_POOL_MAP=0` 모두 23/23 | 기본만 실패 → D3D12 풀 매핑 경로 문제(5장 2번) |
| K10 | ui 13/13, 표시 4/4 | 하나라도 실패 |
| K25b | driver pass=17 fail=0 | 2772와 다름 |
| K21 | Cinebench 창 내용 있음 | 회색 |
| K38g / K38dg | 각 작업 머리말의 PASS 조건(`fatals=0`, `oom_lines=0` 등) | Edge `:(` |
| 재시작(종료 경로) | 두 번째 부팅 기록에 `old launcher exited, no EL2 exception in its HV console` | `OLD BOOT HV EXCEPTION at shutdown` |
| K43b | `pool_demand=1` | 0 → 재시작 뒤 풀이 안 돌아옴 |
| 콘치님 눈 | 바탕화면·창 그리기가 지금과 같음, 작업 관리자 "공유 GPU 메모리"가 늘어남 | 화면 이상 |

---

## 5. 위험 [모두 가설]

| # | 위험 | 드러나는 곳 | 대응 |
|---|---|---|---|
| 1 | D28·종료 때 드라이버 정지 → 풀 창 명령 경로에서 하이퍼바이저가 다른 곳에서 죽음(보호는 `backing_find` 한 곳뿐) | `boot-s265.sh`가 `ABORT hypervisor…`와 예외 레지스터를 남김 | 콘치님 전원 껐다 켜기가 필요할 수 있음 → 6장 되돌리기. 예외 PC(rel)를 `m1n1-g254L-agx-hv2-hpdwatch.elf`로 풀어 원인 확인 |
| 2 | 풀이 켜지면 D3D12 장치가 원본의 "CPU 풀 직접 매핑"(P23)을 다시 씀. J274에서 이 방식으로 성공한 적 없음 | K9 기본 실패, K38dg(Edge D3D11On12) | 재빌드 없이 앱별 `APPLE_AGX_D3D12_CPU_POOL_MAP=0` |
| 3 | 풀 읽기도 CPU에서 느림(11-16 구역 2: 효율 코어 5.7 ms, 성능 코어 34.6 ms / 8 MiB). CPU 창이 쓰기 결합(WC)으로 매핑됨(`pool_windows.c:1773-1774` `MmMapIoSpaceEx … PAGE_WRITECOMBINE`) | K43 `readback_cpu_read ms_per_8mib` | 성능 문제로 따로 다룸(이번 범위 밖) |
| 4 | 64 KiB 단위 붙이기·떼기가 모두 하이퍼바이저를 거쳐 큰 할당이 느림 | K43 `memladder … ms=` | 기록 후 판단 |
| 5 | 앱이 4 GiB 가까이 잡으면 Windows RAM(약 9.6 GiB)이 모자라 시스템이 느려짐 | K43 `OS after free_mib` | `fill2`는 실패할 때까지 잡았다가 바로 놓음 |
| 6 | 하이퍼바이저 힙 여유가 약 112 → 84 MiB로 줄어 다른 기능(DCP·SSD 동시 처리)이 나중에 힙을 못 얻음 | HV 기록 `unavailable` | S265를 끄면 원래대로 |
| 7 | 0xd00000000~0xe00000000에 다른 단계 2 매핑이 있으면 창 준비가 거절됨. 실행기 모듈에서 이 주소를 쓰는 곳은 찾지 못함 | `pool window unavailable (… stage-2 range not free)` | 연쇄가 `POOL NOT ARMED`로 멈춤, Windows는 풀 없이 정상 |

---

## 6. 되돌리기

**결론: Windows 쪽에 남는 것이 없으므로 예전 실행기로 다시 부팅하면 끝입니다.**

| 상황 | 명령 |
|---|---|
| Windows가 켜져 있음 | `bash wifi-s259/run-s259.sh <S265 .out> <S265 .link>` (S259 = 풀 꺼짐) |
| 같은 실행기로 풀만 끄기 | `NWOAS_S265_POOL=0 bash gpupool-s265/boot-s265.sh <.out> <.link>` |
| 맥미니가 D4 대기(하이퍼바이저가 죽어 다시 시작된 뒤) | `bash wifi-s259/run-s259.sh idle -` |
| Windows가 굳음 | 콘치님 전원 껐다 켜기 → 위 `idle -` |

- 드라이버·레지스터리 설정은 바꾸지 않았습니다. 풀이 없으면 KMD가 2구역 동작(P22·P23)으로 돌아갑니다 [사실, 코드].

---

## 7. 확인하지 못한 것

| 무엇 | 왜 | 언제 알 수 있나 |
|---|---|---|
| 11-16 g247B 실기에서 GPU가 왜 꺼졌는지(13:35, 시작 약 80초 뒤 정지·전원 끔) | 그때 Windows 쪽 기록이 없음 | S265 D28·종료 경로 기록 |
| DXGI 예산 실제 값(지금·S265 모두) | 실기 측정 없음 | K43 |
| 구역 3이 정말 4096 MiB인지 | Windows RAM 합계와 VidMm 값은 실기에서만 앎 | K43 `pool segment_mib` |
| D3D11 큰 버퍼의 런타임 한도 | Microsoft 문서 기준 한도 공식을 이번에 확인하지 않음 | K43 `d3d11mem create_hr` |

- VM 시험 중 실수: VM 안의 `C:\NWOAS\gpucov` 폴더를 지웠습니다. 제가 만든 시험 파일을 지우려던 것인데, 그 폴더가 전에 있었는지 확인하지 않았습니다. 예전 VM 시험 도구가 있었다면 함께 지워졌습니다(맥미니와 이 맥북의 파일은 영향 없음).
- 공개 저장소 복사 목록(`publish-s263/stage.sh`)에 새 폴더 `gpupool-s265`는 넣지 않았습니다.
