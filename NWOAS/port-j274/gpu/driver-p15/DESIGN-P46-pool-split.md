# P46 설계 — 풀을 구역 두 개로 나누기 (2048 MiB 잔여 실패의 구조적 고침) [설계만, 2026-11-21]

> 표기: **[사실]** = 코드로 확인. **[가설]** = 확인하지 않은 추측. 아직 소스를 고치지 않았고 빌드도 하지 않았습니다.
> 근거 조사는 읽기 전용으로 했습니다(대상: `drivers/apple-agx/render-admission/src·include·umd/src`, `shared/include·src`, `mesa/winsys`).

## 0. 결론

**풀을 WDDM 구역 두 개(구역 3 = 큰 버퍼용, 새 구역 4 = CPU가 보는 버퍼 전용)로 나누는 것은 코드 구조상 됩니다. 다만 커널 쪽 8개 파일을 함께 고쳐야 하고, 하나라도 빠지면 어댑터가 안 켜지거나, 메모리가 조용히 겹치거나, 블루스크린(0x10E)이 날 수 있습니다 [사실: 코드]. UMD와 mesa는 구역 번호를 보지 않으므로 고칠 필요가 없을 것으로 봅니다 [가설].**

왜 필요한가: `RESULT-2775.md` 22장. 위에서부터 놓기(2778)로 40갈래 중 39가 통과했지만, 새 큰 버퍼가 앞 버퍼 자리 위에 놓이면 위쪽의 CPU가 보는 버퍼와 겹쳐 VidMm이 그 버퍼를 풀 밖으로 내보냅니다. 같은 구역 안에 있는 한 막을 수 없습니다.

## 1. 지금 구조 [사실]

| 구역 | 설명자 위치 | BaseAddress | 크기 | CpuTranslatedAddress |
|---|---|---|---|---|
| 1 어퍼처 | [0] | Topology.Aperture.Base | 256 MiB | 없음 |
| 2 펌웨어 예약 | [1] | Topology.Local.Base | 952 MiB | 있음 |
| 3 풀 | [2] | `ADMISSION_MEMORY_POOL_GPU_VA` = 0x1700000000 (`pool_windows.c:26, 1908`) | `PoolBytes` (RAM·정책으로 계산, `pool_windows.c:1763-1771`; 고정 블록 경로는 1 GiB) | `Context->PoolIpa` (실행 중 정해짐, 평소 0xd00000000) |

- 구역을 알리는 곳: `lifecycle.c:955-957 / 983-1004 / 1005-1007`, `memory_windows.c:60-125`(`AdmissionDdiQuerySegment5`, 구역 3은 113-122), `128-206`(`AdmissionDdiQuerySegment4`, 186-197). 예산 묶음 `memory_windows.c:14-19`(어퍼처가 아니면 Local).
- 구역 3이 붙는 길: `lifecycle.c:542` → `AdmissionMemoryPoolStart`(`pool_windows.c:1884-1957`) → `AppleAgxPhysicalTopologyAddPool`(`shared/src/apple_agx_physical_topology.c:61-86`, `SegmentCount != 2`면 거절, Id=3·수 3 고정). 정리 `pool_windows.c:1959-1966`.
- 구역 비트: 구역 n = `1 << (n-1)`. 구역 3 = 0x4, 구역 4 = 0x8.
- "풀 안/밖" 판단은 KMD가 합니다(`gpuva_g3_windows.c:1865, 1925-1927`). UMD는 `Reason`(NOT_POOL 0x102)만 읽습니다(`umd_gpuva_windows.c:2135, 2289, 2330, 2366, 2375`). mesa의 `DirectCpuPool`은 기능 비트 `AGX_WIN32_DEVICE_FEATURE_POOL_SEGMENT`만 봅니다(`agx_d3d12_windows.cpp:1134-1142`, 비트는 `lifecycle.c:761-767`).

CPU가 보는 버퍼의 구별과 지금 집합:

| 종류 (풀 있을 때) | PreferredSegment | 지원 집합 | 위치 |
|---|---|---|---|
| class 0, CPU가 봄 | 1 | {1} | `allocation_windows.c:582-586` |
| class 1~3, CPU가 봄 | 3 → 1, Direction0=1 | {3,1} = 0x5 | 625-635 |
| class 1~3, CPU가 안 봄 | 3 | {3} = 0x4 | 625-627 |
| GpuOnly | 3 | {3} (SysmemSpill이면 +1) | 642-648, 663-670 |

- 구별 기준: 할당 private data `ADMISSION_WIN32_ALLOCATION_CREATE`(`render_win32_transport.c:67-113`), 드라이버는 `description->CpuVisible`(`allocation_windows.c:567, 582, 628`)과 GpuOnly 플래그(642).
- 새 구역에 들어갈 것은 64 MiB 스테이징만이 아니라 CPU가 보는 General·Shader·Encoder 버퍼 전부입니다.
- 어퍼처(구역 1)는 집합에서 뺄 수 없습니다(빼면 dxgmms2가 CPU가 보는 할당을 거절, `allocation_windows.c:597-599` 주석 EXP1027·EXP1266).

## 2. 주소 규약이 두 가지 [사실 + 표시한 가설]

1. **페이지 표 갱신 경로(G3)**: VidMm이 주는 PTE 주소를 "구역 안의 상대 오프셋"으로 씁니다(`shared/src/apple_agx_gpuva_g3_translation.c:104-116`: `guest_ipa = PoolIpa + address`, `Pool.Base`는 빼지 않음). 그래서 **구역 4의 페이지에는 "아래 구역 크기"를 직접 더해야** 합니다. 고칠 자리 `gpuva_g3_paging_windows.c:857-859`, `1814-1815`, 64K 검사 `865-872`. VidMm이 상대 오프셋을 준다는 것 자체는 추론입니다 [가설: 구역 3의 BaseAddress가 0x1700000000인데도 동작한다는 점이 근거].
2. **물리 TRANSFER/FILL/DISCARD 경로**: 절대 주소에서 `Pool.Base`를 뺍니다(`paging_windows.c:87-132` → `render_memory.c:289-310` → `physical_paging.c:9-24`, 실행 `memory_runtime_windows.c:1087`). 구역 4의 BaseAddress를 `0x1700000000 + 아래 구역 크기`로 주고 `AdmissionMemoryPagingSegment`가 구역 4를 풀 전체로 답하게 하면 계산식은 그대로 됩니다. G3에서 풀에 이 경로가 실제로 쓰이는지는 확정하지 못했습니다.
3. **CpuTranslatedAddress**: 구역 4는 `PoolIpa + 아래 구역 크기`.

그 뒤 단계(PoolPlace, PagingRefs, NoteRecord, 실행기, 복사)는 모두 창 오프셋(`GuestIpa - PoolIpa`)으로 계산하므로 계산식은 그대로이고 구역 번호 판정만 넓히면 됩니다.

## 3. 반드시 함께 고쳐야 하는 자리 (빠지면 생기는 일)

| # | 자리 | 빠지면 |
|---|---|---|
| 1 | `lifecycle.c:189-192` `AdmissionGpuvaG3DeclarationReady`가 구역 수 2·3만 허용 (837-838, 866, 908, 1031, `callbacks.c:653`도) | 어댑터 시작 실패 |
| 2 | `gpuva_g3_paging_windows.c:857-859, 1814-1815` 구역 4 오프셋 보정 | 구역 4 페이지가 구역 3 아래쪽에 조용히 겹침(오류 없음) |
| 3 | `render_paging.h:12-15` `ADMISSION_PAGING_MEMORY_SEGMENT`에 4 추가 (`gpuva_g3_paging_windows.c:1345-1359`의 방향 검사) | 가상 전송이 `STATUS_INVALID_PARAMETER` → 주석(1622-1627)대로면 0x10E 블루스크린 |
| 4 | `render_memory.h:20-22` `ADMISSION_G3_FRAME_SEGMENT`에 4 추가 (961-964 등), `>` 상한 검사 `gpuva_g3_windows.c:4772, 4912, 4997`, `render_paging.c:64-65` | 구역 4가 다른 종류로 처리되거나 거절 |
| 5 | `memory_windows.c:113-122, 186-197` 설명자 위치 3(구역 4) 추가, `SegmentCount == 3u` 조건 | 구역 4 설명 누락 |
| 6 | `apple_agx_physical_topology.h:24-33`, `topology.c:61-86` AddPool의 수·Id 고정, `pool_windows.c:1905-1912, 1950-1951, 1959-1966` | 등록·정리 불일치 |
| 7 | `pool_windows.c:746-753` `AdmissionMemoryPoolNoteRecord`에 구역 4 | 실행 전에 granule이 풀릴 수 있음 |
| 8 | `allocation_windows.c:12-13, 625-635` 구역 4 집합·선호, `81-96`·`lifecycle.c:735` CPU가 보는 버퍼의 최대 크기 | 새 구역을 안 쓰거나, 새 구역보다 큰 CPU가 보는 버퍼가 영영 못 들어감 |
| 9 | `gpuva_g3_windows.c:1865, 1925-1926, 2274-2282, 2301, 4492` 풀 판정에 구역 4 | KMD가 구역 4를 "풀 밖"으로 답해 UMD가 거절 |
| 10 | `render_memory.c:297-310`, `memory_runtime_windows.c:1087` 물리 경로 | 물리 전송이 쓰일 때 거절 |

`gpuva_g3_paging_windows.c`에서 구역 판정이 있는 줄: 69-84, 121, 126, 155, 498-517, 544-546, 915, 941-947, 961-964, 1028, 1037, 1060, 1081, 1347-1349, 1439-1449, 1812-1817, 1829-1830.

## 4. 고치는 방식 두 가지

| 방식 | 내용 | 장점 | 단점 |
|---|---|---|---|
| 1 | PTE를 받을 때(`gpuva_g3_paging_windows.c:878-885`) 구역 4를 오프셋 보정한 뒤 논리 PTE의 구역 번호를 3으로 바꿔 저장 | 그 뒤 코드를 거의 안 건드림 | live wait(1812-1834)는 원래 PTE를 보므로 따로 고쳐야 함. 영수증에서 구역 4가 구분되지 않음 |
| 2 | "풀 구역(3 또는 4)" 판정 매크로를 만들어 3장의 비교를 전부 바꿈 | 기록에 구역 4가 그대로 보임 | 고칠 줄이 많음 |

## 5. 지원 집합과 크기

| 안 | CPU가 보는 버퍼의 선호 | 지원 집합 | 장점 | 위험 |
|---|---|---|---|---|
| A | 4 | {4,1} = 0x9 | 큰 버퍼와 완전히 분리 | 구역 4가 가득 차면 NOT_POOL(장치 제거) |
| B | 4 → 3 | {4,3,1} = 0xD | 넘쳐도 풀 안 | 넘친 것은 다시 큰 버퍼와 경쟁. 구역 4↔3 이동이 실제로 되는지 미확인 |

- 큰 버퍼(CPU가 안 보는 것, GpuOnly)의 집합은 {3}만 두고 4를 넣지 않습니다.
- 먼저 A로 효과를 재고, 구역 4 크기는 레지스트리로 바꿀 수 있게 합니다.
- 나누는 경계와 크기는 64 KiB 배수(granule 0x10000).

| 예 (풀 4 GiB, PoolIpa 0xd00000000) | 값 |
|---|---|
| 구역 3 크기 | 0xE0000000 (3,584 MiB) |
| 구역 4 크기 | 0x20000000 (512 MiB) |
| 구역 4 BaseAddress | 0x17E0000000 |
| 구역 4 CpuTranslatedAddress | 0xDE0000000 |

- 구역 4가 가득 차도 할당 생성은 성공합니다(예약은 풀 전체로 셈: `pool_windows.c:801-813, 1789-1792, 1835`). 그 뒤 VidMm이 어퍼처나 시스템 페이지에 둘 것으로 봅니다 [가설].
- 예산: 구역 4도 Local 묶음에 자동으로 들어갑니다(`memory_windows.c:17-18`). Flags는 구역 3과 같게(CpuVisible, PopulatedFromSystemMemory, Use64KBPages, SlabSize, SegmentType LOCAL).
- 위쪽부터 놓기(`Direction0`)는 구역 4 안에서의 뜻으로 바뀝니다.

## 6. 스위치

- 읽는 자리: `AdmissionMemoryPoolStart` 안, `AddPool` 전(`pool_windows.c:1906` 또는 1950 앞). 기존 방식(`IoOpenDeviceRegistryKey(PLUGPLAY_REGKEY_DEVICE)` + `AdmissionPoolReadDword`, `pool_windows.c:1567-1583, 1605-1616`).
- 값은 `Context`에 둡니다(QuerySegment와 PTE 해석이 잠금 없이 읽어야 함).
- 고정 1 GiB 경로에서는 나누기를 끕니다.
- 구역 수는 `AdmissionDdiStartDevice`에서 정해지므로 값을 바꾸면 장치를 다시 시작(실제로는 재부팅)해야 합니다.
- 기본값 제안: 첫 판은 **꺼짐 기본**(레지스트리로 켬)으로 내고, 켠 부팅에서 통과한 뒤 다음 판에서 기본을 바꿉니다(2776 → 2778 때와 같은 순서).

## 7. 시험 계획 [초안]

1. 꺼짐 기본으로 설치 → 기존 회귀 전부(2780 연쇄와 같은 결과여야 함. 꺼져 있으면 구역 수 3 그대로).
2. 레지스트리로 켜고 재부팅 → 어댑터 시작(`DEV OK`), 구역 4개 확인 → K48(8갈래) → K45t에서 CPU가 보는 버퍼의 구역 번호가 4인지 → K43c, K50, Edge, DWM.
3. 켠 부팅 3번·끈 부팅 3번 번갈아(K48 8갈래씩) → 켠 쪽 24/24, 끈 쪽 실패 있음이면 고침으로 인정.
4. 위험이 큰 첫 실기(커널 쪽 구역 구성 변경, 블루스크린 가능)이므로 콘치님이 계실 때.

## 8. 아직 모르는 것

- VidMm이 구역 4의 PTE를 상대 오프셋으로 주는지(2장 1번의 가설). 틀리면 조용한 겹침이 납니다 → 첫 실기에서 K45t의 구역 4 위치(`pool_base_mib`)가 3,584 MiB 이상인지로 확인합니다.
- 예산 압박 때 VidMm이 구역 4의 버퍼를 덜어내는지.
- G3에서 풀에 물리 TRANSFER 경로가 실제로 쓰이는지.

## 9. 설계 보완 (11-21, 구현 전 검토에서 고친 것)

**결론: 세 가지를 바꿉니다. 비교 시험은 위에서부터 놓기를 끈 상태로 하고, 오프셋 가정은 원래 값을 기록해서 확인하며, 구역 4의 크기와 지원 집합은 고정하지 않고 레지스트리 값으로 둡니다.**

1. **비교 시험의 조건.** 위에서부터 놓기를 켠 상태의 실패율은 40갈래 중 1이라, 24갈래짜리 "나누기 끔" 쪽이 실패 없이 지나갈 확률이 절반을 넘습니다. 그래서 두 쪽 모두 `Wom1PoolCpuVisibleTopDown=0`으로 돌립니다(11-20 기록: 그 상태의 끔 쪽은 32갈래 중 11 실패). 나누기가 구조적 고침이면 위에서부터 놓기 없이도 24/24가 나와야 합니다. 그 뒤 내보낼 조합(나누기 켬 + 위에서부터 놓기 켬)을 따로 확인합니다. 7장 3번을 이것으로 바꿉니다.
2. **상대 오프셋 가정의 확인.** K45t의 `pool_base_mib`는 드라이버가 이미 보정한 주소에서 계산하므로 3,584 MiB 이상으로 나오는 것은 당연하고, 가정을 틀렸다고 가려 주지 못합니다(8장 첫 줄 정정).
   - 영수증에 구역 4 PTE의 **원래 오프셋**(보정 전)을 따로 남깁니다.
   - 두 변환 자리(`gpuva_g3_paging_windows.c:857-859, 1814-1815`)를 하나의 도우미로 모으고, 구역별 범위 검사를 넣습니다(구역 3은 경계 아래, 구역 4는 자기 크기 아래). 범위를 벗어나면 매핑하지 않고 거절합니다.
   - 내용 수준의 겹침은 큰 버퍼 시험의 `mismatches=0`으로 잡습니다. 나누기를 켠 상태에서 3,072 MiB 갈래가 돌게 합니다(구역 3이 3,584 MiB이므로 들어감).
3. **크기·집합을 고정하지 않기.** CPU가 보는 class 1~3 버퍼가 전부 새 구역으로 옮겨 가므로, {4,1}만 두면 넘칠 때 지금은 4 GiB 풀로 들어가던 것이 장치 제거가 됩니다(퇴행).
   - 같은 빌드에 레지스트리 값 두 개: 구역 4 크기(MiB, 0 = 나누지 않음 = 지금과 같음), 지원 집합(A = {4,1}, B = {4,3,1}).
   - 먼저 2780에서 Edge·DWM·Cinebench·K43c를 돌릴 때 CPU가 보는 버퍼가 풀을 최대 얼마나 쓰는지 잽니다(영수증에 최댓값 추가 또는 K45t 집계).
   - 나누기를 켰을 때의 K43c 기대값(채우기 힙 수가 달라질 수 있음)을 실기 전에 적어 둡니다.

구현 원칙:
- 상수 `ADMISSION_MEMORY_POOL_SEGMENT`, `ADMISSION_PAGING_POOL_SEGMENT`와 구역 매크로 둘의 이름을 바꿔, 쓰는 자리가 전부 컴파일 오류로 드러나게 한 뒤 하나씩 고칩니다(검색 목록만 믿지 않음). 숫자 리터럴(3장 5·6번)은 손으로 확인합니다.
- 하드웨어 전에 호스트 시험: 나눈 토폴로지, 구역 4의 풀 페이지 변환(범위 검사 포함), 전송 방향·상한 검사.

되돌리기(켜는 실기의 전제):
- 나누기 값은 풀 창이 열린 뒤에만 읽습니다(`AdmissionMemoryPoolStart` 안) — 코드로 확인하고 구현합니다. 그러면 풀을 끈 부팅(`NWOAS_S269_POOL=0`)에서는 나누기가 일어나지 않으므로, 켠 값 때문에 부팅이 돌지 않을 때 풀 끈 부팅으로 들어가 값을 지울 수 있습니다.

실기 요청은 둘로 나눕니다: ① 설치 + 꺼짐 상태 회귀(지금 판과 같아야 함) ② 켠 부팅들(블루스크린 가능 → 콘치님 재석).
