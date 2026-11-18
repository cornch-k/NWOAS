# GPU를 "진짜 그래픽 드라이버"로 만드는 계획 (2026-11-18)

> 표기: **[사실]** = 코드·실기기 로그로 확인. **[가설]** = 확인하지 않은 추측. **빌드 성공은 동작이 아닙니다.** 이 문서의 어떤 수정도 아직 맥미니에서 시험하지 않았습니다.
> 대상: 2757판(P21) 드라이버 + 하이퍼바이저 g254L, 실행기 `agxdcp-s256`(풀 창 없음, `NWOAS_AGX_POOL_IPA=0`).

---

## 0. 결론

**지금 드라이버는 바탕화면 그리기와 작은 D3D 앱까지만 됩니다. 16 MiB보다 큰 GPU 메모리를 한 번에 잡는 앱은 모두 실패합니다.** Cinebench 2026의 회색 창이 그 첫 사례입니다.

1. **원인 [사실, 2758 실기기 UMD 기록 + 소스 대조]**: D3D12 장치 내부 객체(native owner)의 CPU 매핑이 "모든 쪽이 풀(구역 3)에 있음"을 증명하는 방식으로 기본 설정되어 있습니다. 풀 창을 끈 지금 구성에서는 이 증명이 절대 통과하지 못합니다. 첫 CPU 매핑이 실패하면서 그 장치의 그리기 바탕이 통째로 망가지고, 그 뒤 모든 힙은 16 MiB 한도의 예비 경로로 갑니다. Cinebench의 152.6 MiB CPU 힙이 거기서 E_OUTOFMEMORY가 됩니다. 자세한 풀이는 2.5장.
2. **고침**: 2759판(P23 + P22). KMD가 풀 유무를 알리고, UMD는 풀이 있을 때만 그 방식을 켭니다. 재빌드 없이 2758에서 `APPLE_AGX_D3D12_CPU_POOL_MAP=0`으로 같은 동작을 먼저 비교할 수 있습니다(K4b). **실기기 시험 전입니다.**
3. 2758(P22, 16 MiB → 952 MiB)은 실기기에서 Cinebench를 고치지 못했습니다 [사실]. P22는 정상 경로에서 여전히 필요하지만 첫 원인은 아니었습니다. 처음 보고의 "Cinebench가 맞은 한도"는 틀렸습니다.
4. 작업 관리자에 "3D"와 전용 메모리만 보이는 것은 **드라이버가 엔진을 3D 하나만 알리기 때문**입니다 [사실]. 복사·동영상 엔진은 없습니다.
5. 일상용으로 가장 급한 순서: ① 메모리 한도(P22 → 풀 창) ② 화면 출력 정상화(DDisplay·60장/초) ③ x64·x86 앱용 사용자 드라이버 ④ 복사 엔진 ⑤ 동영상 재생 가속 ⑥ OpenGL·Vulkan.

---

## 1. 지금 상태 (정직한 표)

**바탕화면·창 그리기·해상도·모니터 재연결은 됩니다. 큰 메모리, x64 앱, 동영상 가속, OpenGL·Vulkan, 정상 출력 경로(DDisplay)는 안 됩니다.**

| 항목 | 상태 | 근거 |
|---|---|---|
| 바탕화면(DWM)·창 그리기 | 됨 | 콘치님 확인(11-16) |
| 해상도 8개, 모니터 끄기·켜기·재연결 | 됨 | 콘치님 확인(P21, g254L) |
| D3D11 장치 (ARM64 앱) | 기능 수준 11_1 알림 | 로드맵 6.1 [사실, 에이전트 확인] |
| D3D12 장치 (ARM64 앱) | 기능 수준 12_1, 셰이더 모델 6.5 알림 | 같음 |
| D3D12 힙 등급 | Tier 1 (`agx_d3d12_ddi.cpp:470`) | [사실, 코드] — 힙마다 "버퍼만/텍스처만" 지정해야 함 |
| 레이트레이싱·메시 셰이더 | 없음 (`agx_d3d12_ddi.cpp:8352`, `:8446`) | [사실, 코드] |
| D3D9 앱 | D3D9On12로 돎 (INF의 `"<>"` 표시) | [사실, INF] |
| **16 MiB 넘는 GPU 메모리 한 덩이** | **안 됨 (E_OUTOFMEMORY)** | [사실, 코드] 2장. 2758에서 고침, 실기기 미확인 |
| Cinebench 2026 UI | 회색 창, `directx_resource_heap.cpp(27)` E_OUTOFMEMORY | [사실, 콘치님 로그] |
| x64·x86 앱의 GPU 가속 | 없음 (패키지에 ARM64 UMD만, INF P9) | [사실, 패키지] |
| OpenGL | 없음. INF가 `AppleAgxOpenGL64.dll`을 가리키지만 파일이 패키지에 없음 → Windows 기본 GDI 구현 | [사실, INF·패키지] |
| Vulkan | 없음 | [사실] |
| 동영상 하드웨어 디코드(DXVA) | 없음 | [사실, 드라이버에 동영상 DDI 없음]. 실기기 확인은 K2 `video_device profiles=` |
| 엔진 | 3D 하나 (`callbacks.c:647`). 복사 노드는 시험용 레지스트리 + 하이퍼바이저 기능이 있을 때만 | [사실, 코드] |
| 출력 경로 | `DisableDDisplay`=1 우회. UMD `PresentMultiplaneOverlay1` 미구현 | [사실] |
| 화면 교체 | 진짜 수직 동기 신호 없음 → 초당 약 56장 상한 | 로드맵 C5 [사실] |
| GPU 주소 갱신 | 모두 하이퍼바이저 경유 | 로드맵 C6 [사실] |
| 하드웨어 GPU 스케줄링 | 없음 | [사실, 드라이버 능력 보고] |
| 오류 복구 | 실행 중 장치 재시작 0xC0000483 | HANDOFF 3.6 [사실] |
| GPU 점수·Edge·WebGL·동영상 | 잰 적 없음 | — |

---

## 2. Cinebench 회색 창 — 원인

> **2758 실기기 뒤 정정**: 아래 2.1~2.4의 16 MiB 한도는 정상 경로의 한도로 사실이지만, Cinebench의 첫 원인은 2.5장의 풀 전용 CPU 매핑 증명입니다.

**결론: 풀 창이 없는 지금 구성에서는 일반 GPU 버퍼 한 덩이의 최대 크기가 16 MiB입니다.** 이것이 D3D12 힙 생성의 `E_OUTOFMEMORY`를 만듭니다 [사실, 코드]. Cinebench가 16 MiB보다 큰 힙을 요청한다는 것은 [가설]이고, 시험 도구로 확인합니다.

### 2.1 메모리 구역 (KMD가 Windows에 알리는 것)

지금 구성에서는 구역 2개만 있습니다. 구역 3(풀)은 실행기가 풀 창 주소를 0으로 주기 때문에 없습니다.

| 구역 | 종류 | 크기 (MiB) | GPU 주소 | 예산 묶음 | 근거 |
|---|---|---|---|---|---|
| 1 | 시스템 메모리 창(aperture) | 256 | 0x1600000000 | 비로컬 | `memory_runtime_windows.c:9-10` |
| 2 | 펌웨어 예약 메모리(로컬) | 952 | 0x1500000000 | 로컬 (작업 관리자 "전용") | 예약 1 GiB − 개인용 64 MiB − 백엔드 8 MiB (`apple_agx_local_reserve_abi.h`) |
| 3 | 풀(수요 기반) | **없음** | 0x1700000000 | 로컬 | `pool_windows.c:24-25`. 실행기 `NWOAS_AGX_POOL_IPA=0` (`agxdcp-s256-guest-test.sh:34`) |

### 2.2 거절되는 흐름

| 단계 | 위치 | 하는 일 |
|---|---|---|
| 1 | `render-admission/src/lifecycle.c:723` | UMD에 일반 버퍼 최대 크기 = `AdmissionGeneralMaximumBytes()` |
| 2 | `render-admission/src/allocation_windows.c:81-92` | 풀이 있으면 풀 크기, **없으면 16 MiB**(`ADMISSION_WIN32_ALLOCATION_LEGACY_MAX_BYTES`) |
| 3 | `mesa/winsys/agx_win32_screen.c:114` | `Bytes > MaximumBytes`면 KMD를 부르지 않고 거절 |
| 4 | `mesa/winsys/agx_d3d12_windows.cpp:1192` | `resource_create` NULL → `E_OUTOFMEMORY` |
| 5 | `d3d12/agx_d3d12_ddi.cpp:2175` `CreateHeap` | 그 값을 런타임에 돌려줌 → 앱의 `ID3D12Device::CreateHeap` 실패 |

- KMD 쪽 검사(`render_win32_transport.c:107-111`)도 같은 16 MiB를 씁니다.
- D3D11(Mesa d3d10umd)의 큰 텍스처·버퍼도 같은 3단계에서 막힙니다 [사실, 같은 경로].
- 원본 windows-on-m1은 항상 풀 창을 켜고 돌았기 때문에 이 한도에 걸리지 않았습니다. 우리는 풀 창 경로에서 하이퍼바이저가 죽은 일(HANDOFF 3.5) 뒤로 풀 창을 끄고 돌고 있습니다 [사실].

### 2.3 하이퍼바이저 GPU 주소 중개는 원인이 아님

**결론: 하이퍼바이저는 952 MiB 구역 전체를 받아 줍니다. 한도는 드라이버 쪽에만 있습니다** [사실, 코드].

| 한도 | 값 | 근거 (`m1n1-gpu-hv2/src/hv_agx_gpuva_v5.h`) |
|---|---|---|
| 로컬 예약 쪽 수 | 65536 × 16 KiB = 1 GiB | `HV_AGX_GPUVA_V5_LOCAL_PAGES` |
| 시스템 메모리 쪽 수 | 131072 × 16 KiB = 2 GiB | `HV_AGX_GPUVA_V5_BACKINGS` |
| GPU 주소표 | 4096개(16 KiB씩) | `HV_AGX_GPUVA_V5_TABLES` |
| GPU 사용 프로세스 | 128개 | `HV_AGX_GPUVA_V5_PROCESSES` |
| 프로세스 GPU 주소 범위 | 2^39 B = 512 GiB | `agx_d3d12_ddi.cpp:1767`, `agx_win32_gpuva.c:6` |

### 2.4 함께 알아 둘 것

- D3D12 힙 등급이 Tier 1이라 "모든 종류 허용" 힙(`D3D12_HEAP_FLAG_NONE`)은 `E_INVALIDARG`가 정상입니다. Cinebench 오류는 `E_OUTOFMEMORY`이므로 이 경우가 아닙니다 [사실, 오류 값].
- 앞으로 같은 오류가 다시 나오면 UMD 기록의 `measure-d3d12-heap hr=0x8007000e` 줄 두 번째 값 × 64 KiB가 요청 크기입니다.

### 2.5 2758 실기기 결과로 다시 찾은 원인

**결론: 첫 원인은 크기 한도가 아니라 "풀 전용 CPU 매핑 증명"입니다. 풀이 없으면 D3D12 장치 내부 객체가 처음 CPU 매핑에서 망가집니다** [사실, UMD 기록 + 소스].

| UMD 기록 줄 (시각 순) | 값 | 뜻 |
|---|---|---|
| `reject-cpu-pool-map hr=0x8007000e` | 40002340 / 3 / 0x1101540000 | 할당 0x40002340 CPU 매핑의 풀 증명 실패. 사유 3 = GPU 주소표에 그 쪽이 없음 |
| `reject-batch` 종류 4, 위치 0x103e6 | `agx_win32_asahi_bo.c` 998줄 | 위 실패로 Asahi 백엔드가 실패 상태가 됨 → 이 장치의 모든 버퍼가 NULL |
| `measure-d3d12-heap-refused` 사유 3 | CPU 힙, 0x98a × 64 KiB | 예비 경로에서 152.6 MiB > 16 MiB(UMD 상수 `kCpuVisibleMaximum`) |
| 성공한 `measure-d3d12-heap` | 0줄 | 정상 경로를 한 번도 못 씀 |

- 이 증명 방식(`APPLE_AGX_D3D12_CPU_POOL_MAP`)은 D3D12에서만 기본으로 켜져 있었습니다. DWM(D3D11)은 쓰지 않아 지금까지 드러나지 않았습니다.
- Cinebench 콘솔의 "No supported drawport type found"는 DirectX 그리기 자체를 포기했다는 뜻입니다. 라이선스 창 버튼이 안 보인 것도 이 때문이라고 봅니다 [가설]. 따로 형식 문제가 있는지는 2759 뒤에 K4로 다시 봅니다.
- 작업 관리자 엔진 계수기는 실기기에서 `eng0`~`eng8` 9개가 모두 3D로 나왔습니다 [사실, K6]. 노드는 3D 하나만 알리므로 계수기 번호가 노드 수와 같은 뜻인지는 확인하지 않았습니다 [가설].

### 2.6 풀 증명을 끈 뒤(2758 + `APPLE_AGX_D3D12_CPU_POOL_MAP=0`, 19:05)

**결론: 장치는 살아났고(힙 4개 성공, 백엔드 실패 0) 사각형은 그려지지만, 글자와 버튼 그림은 안 나옵니다. 텍스처를 쓰는 그리기에 문제가 있다고 봅니다 [가설]. 원인은 아직 확인하지 못했습니다.**

| 기록 | 값 | 뜻 |
|---|---|---|
| `measure-d3d12-owner hr=0` | — | 장치 내부 객체 생성 성공 |
| `measure-d3d12-heap hr=0` | 4개 (152.6 MiB × 2, 256 KiB × 2) | 큰 힙 성공 |
| `reject-cxx-throw` 2개 | 형식 이름 "Exception", 스택 모듈 "redshift…" | 우리 UMD가 아니라 Cinebench의 Redshift 모듈 안에서 난 C++ 예외. UMD는 프로세스 전체의 예외를 기록할 뿐입니다(`umd.c:386-433`). 기록의 E_UNEXPECTED는 기록 표시일 뿐 실제 오류 값이 아닙니다 |
| 창 측정 | 색 15~16개, 한 색 69~96 % | 그림 있음 |

확인 방법(재빌드 없음): `K9-tex.cmd`(R8·A8·RGBA8·BGRA8·BC1·BC3·BC7 텍스처를 올리고 샘플, 계속 매핑된 업로드 버퍼, 알파 섞기 글자 모양. 드라이버 기본·`LEVEL_UPLOAD=0`·`CPU_POOL_MAP=0`·WARP 기준값 네 번), `K4c-cb26-ui-nolevel.cmd`(텍스처 올리기를 단계 링 대신 스테이징 복사로).
- 시험이 맞는지는 VM의 WARP에서 모두 PASS로 확인했습니다. Parallels 장치는 R8·A8·BC1·알파 섞기가 실패하는데, 이것은 Parallels 드라이버의 결과라 우리 판단에 쓰지 않습니다.

### 2.7 2759 실기기(19:15~19:19) — 글자는 보이고 버튼은 안 보임

**결론: 텍스처·업로드·알파 섞기는 실기기에서 모두 통과했습니다(K9 23/23 세 조건). 버튼이 안 그려지는 원인은 아직 모릅니다.** UMD 기록에는 거절이 없습니다. 남은 두 줄은 Cinebench Redshift 모듈의 C++ 예외입니다. K4c의 `rejects=1`은 제 작업 파일의 계산 실수였고 고쳤습니다.

다음 확인(재빌드 없음, 도구만 새로 보냄): `nwgpuprobe3`의 `ui` 시험 13개(정점·색인 버퍼와 인스턴스, 서술자 힙 샘플러와 선형 필터, 루트 상수, 서술자 표 색인, 가위 영역, 4x MSAA와 해석, RGBA16F·R11G11B10 대상, 밉 수준 직접·미분, 텍스처 배열, SV_Position·discard·fwidth 둥근 모양, 이중 소스 섞기)와 `present` 변형(자식 창 스왑체인, DirectComposition 시각 요소). VM의 WARP에서 모두 PASS로 시험 자체를 확인했습니다. 작업 파일은 `K10-ui.cmd`입니다.

### 2.8 K10 실기기(19:28) 뒤 — D3D12 그리기·표시 경로는 모두 통과

**결론: 우리 드라이버는 `ui` 13개(WARP와 같음)와 표시 4개(메인 창·자식 창·DirectComposition 포함)를 모두 통과했습니다. 버튼은 이 밖의 경로를 쓰는 것으로 봅니다 [가설].** 다음 확인은 `nwgpuprobe4`의 `interop` 9개(D2D·DirectWrite를 D3D11과 D3D11On12에서, GDI 호환 스왑체인의 GetDC, D3D11↔D3D12 공유 텍스처 두 방향, 키 뮤텍스, 계층 창 두 가지)와 2760판의 기능 질의 기록(K4d)입니다. VM에서 `interop` 9개 모두 PASS로 시험 자체를 확인했습니다.

### 2.9 기능 질의 기록(2760, 19:4x) 풀이

**결론: 기능 질의 답에서 UI를 막을 만한 빈칸은 찾지 못했습니다.** 그래서 2761은 만들지 않았습니다. 다음은 이미 드라이버에 있는 그리기·파이프라인 기록을 Cinebench 기록 파일에서 뽑아 보는 것입니다(재빌드 없음).

| 질의 | 번호 | 우리 답 | 판단 |
|---|---|---|---|
| D3D11 THREADING / SHADER / 3DPIPELINESUPPORT | 0x80 / 0x81 / 0x82 | 0 / 2 / 0xf | 정상(장치 생성 성공) |
| D3D11 OPTIONS / ARCHITECTURE_INFO / MIN_PRECISION | 0x83 / 0x84 / 0x86 | 1 / 1(타일 기반) / 0 | 정상 |
| D3D11 OPTIONS1 / MARKER / OPTIONS2 / OPTIONS3 | 0x88 / 0x89 / 0x8f / 0x98 | 0 | 선택 기능 없음 |
| D3D11 MEMORY_ARCHITECTURE / GPUVA_CAPS | 0x91 / 0x99 | 1(UMA) / 0x20 | 정상 |
| D3D11 0x97 | 151 | E_NOTIMPL | WDK 26100 헤더에 없는 번호. 런타임이 알아서 넘어감(장치 생성 성공) |
| D3D11 TEXTURE_LAYOUT1 | 0x9a(154) | E_NOTIMPL | 표준 배치 미지원 뜻. 정상 |
| D3D12 MSAA 질의 3832번 | — | 2x·4x만 1 | Cinebench가 아니라 D3D12 런타임이 장치를 만들 때 모든 형식 × 표본 수를 묻는 것(`agx_d3d12_ddi.cpp:866-886` 설명과 같음) |
| D3D12 형식 118개 | — | 선택 비트만 | 기능 수준 12에서 렌더 대상·섞기·샘플 가능 여부는 런타임이 정함. 드라이버는 MSAA·UAV 읽기·타일 비트만 알림 |

### 2.10 Cinebench UI는 D3D11로 그려짐 (2760 K12)

**결론: Cinebench의 D3D12 장치는 그리기를 한 번도 하지 않았습니다. UI는 D3D11 장치 두 개로 그려집니다. D3D11 쪽에는 셰이더 생성 실패·그리기 거절 기록이 없습니다.** 그래서 남은 후보는 "만들어졌지만 틀리게 그리는 셰이더나 상태"입니다 [가설]. 2761판은 D3D11 셰이더마다 기록을 남기고 셰이더 코드를 파일로 저장합니다. 저장한 셰이더는 VM에서 우리 변환기로 다시 돌려 볼 수 있습니다.

| 2760 기록 | 개수 | 뜻 |
|---|---|---|
| `measure-d3d12-draw` | 0 | D3D12 그리기 없음 |
| `measure-shader-nir` | 13 | 변환기(SM5 등)로 간 D3D11 셰이더, 모두 성공 |
| `reject-seterror` | 0 | D3D11에서 셰이더 생성 실패·그리기 거절 없음 |
| `reject-no-target` | 0 | 대상 없는 그리기 없음 |
| `measure-vdm` | 64 | GPU 렌더 패스 64번 |
| `measure-native-ring-map` 실패 | 1 (0x8876017c) | 스테이징 링을 못 잡고 보통 경로로 넘어감(17번). 풀 창이 없어서로 봄 [가설] |

변환기(`mesa/dxbc/dxbc_to_nir.c`)가 처리하지 않는 DXBC 명령 [사실, 코드 대조]: 배정밀도 전부(DADD·DMUL·DFMA·DDIV·DRCP·DEQ·DGE·DLT·DNE·DMIN·DMAX·DMOV·DMOVC·FTOD·DTOF·DTOI·DTOU·ITOD·UTOD), 인터페이스·함수 표(DCL_INTERFACE·DCL_FUNCTION_BODY·DCL_FUNCTION_TABLE, "subroutines"), ABORT, DEBUG_BREAK. 그 밖에 거절하는 형태: 래스터라이저 대상 sampleinfo·samplepos, 버퍼 UAV에 resinfo, 타일 피드백 명령. 이것들은 모두 생성 실패(`reject-seterror`)로 나타나야 하는데 2760에서는 0개였으므로, 이번 버튼 문제의 원인은 아닌 것으로 봅니다. SM4 셰이더는 Mesa의 TGSI 경로(`ShaderTGSI.c` → `tgsi_to_nir`)로 가고, 그쪽 빈칸은 이번에 조사하지 않았습니다.

### 2.11 2761 실기기(20:02~20:08) — 셰이더는 두 경로 모두 정상, 버튼은 여전히 없음

**결론: D3D11 셰이더 22개(VS 4.0 11개, PS 4.0 11개)를 모든 셰이더 변환기 경로로 돌려도 거절 0개, 결과도 같았습니다(버튼 없음). 셰이더 변환이 원인일 가능성은 낮아졌고, 그리기 상태나 자원을 의심합니다 [가설].** 2762는 그리기마다 묶인 상태를 기록합니다. 맥에서 `gpucov/analyze-drawstate.py`로 같은 상태끼리 묶고, 화면에 아무것도 남지 않는 조건(쓰기 마스크 0, 빈 가위·뷰포트, 표본 마스크 0, 대상 없음, 깊이 NEVER 등)을 표시합니다.

- K14가 실패한 이유: 사용자 세션이 아직 없었습니다(`explorer_sessions=` 빈칸, 작업 결과 0x41303 = 작업이 한 번도 실행되지 않음). 화면 측정 줄은 이전 실행의 남은 파일이었습니다. K14c·K16은 explorer가 나타날 때까지 최대 240초 기다리고, 실패하면 한 번 더 시도하고, 시작 전에 이전 결과 파일을 지웁니다.
- 실제 셰이더로 다시 그려 보는 도구는 아직 없습니다. D3D11 API는 DXBC 컨테이너(서명·체크섬)를 요구하는데, 드라이버가 받는 것은 토큰과 입출력 신호뿐이기 때문입니다. 2762는 둘 다 저장하므로(`.bin`, `.sig`) 다음에 컨테이너를 다시 만들어 우리 드라이버와 WARP를 비교할 수 있습니다.

### 2.12 VM(Parallels)에서 본 Cinebench의 그리는 구조 — 버튼이 어느 경로인지

**결론: Cinebench의 라이선스 글 영역은 Cinebench가 직접 그리는 것이 아니라 내장 Chromium(CEF, 별도 프로세스)이 그립니다. 메인 창과 대화 상자 틀·버튼은 Cinebench 프로세스가 D3D11 스왑체인(구형 `CreateSwapChain`, BGRA, 버퍼 1개)으로 그립니다** [사실, VM에서 dxgi 대리 DLL·창 구조 기록]. 맥미니에서 글만 보이고 나머지가 안 보인다는 것은, CEF 쪽은 되고 Cinebench 자체 그리기 경로가 안 된다는 뜻입니다.

| 창(VM, t=50 s) | 클래스 | 크기 | 그리는 주체 |
|---|---|---|---|
| Cinebench 2026.1.3 (메인) | C4DR_WINA_0_0 | 1040x736 | Cinebench, D3D11 스왑체인 1024x697 |
| License Agreement (대화 상자) | C4DR_WINA_0_0 | 882x586 | Cinebench, D3D11 스왑체인 |
| 그 안의 글 영역 | Browser Object → Chrome_WidgetWin_0/1 → Chrome_RenderWidgetHostHWND, Intermediate D3D Window | 850x500 | CEF(disclaimer.html) |
| 숨은 창 2개 | C4DR_WINA_0_0 | 136x100 (화면 밖) | Cinebench, 스왑체인 120x61 |

- VM에서는 스왑체인이 모두 D3D11 장치(Parallels)에 만들어졌습니다. 맥미니는 D3D12 기능 수준 12_1을 알리므로 Cinebench가 자기 그리기에 D3D12를 고를 수 있고, 맥미니에서 D3D12 그리기는 0번이었습니다(2760 K12) [가설: 이것이 원인].
- 확인 작업: K21(스왑체인 목록·창 구조·프로세스별 그리기 수), K22(같은 것 + `APPLE_AGX_D3D12=0`, 우리 D3D12 UMD가 Cinebench를 거절해 D3D11을 쓰게 함). K22에서 버튼이 보이면 원인은 우리 D3D12 경로입니다.

### 2.13 2762 실기기(20:43~20:51) — 원인은 우리 D3D11 그리기

**결론: Cinebench의 D3D11만 WARP로 돌리면(디스플레이는 우리 드라이버) 버튼과 메인 창이 모두 보였습니다(K18, 콘치님 확인). D3D12를 끄면(K22) 그대로였습니다. 그러므로 원인은 우리 D3D11 경로가 Cinebench 자체 그리기를 틀리게 그리는 것입니다** [사실]. 그리기는 1920x1009(302번, 메인 창)와 866x547(63번, 대화 상자)에 실제로 들어갔습니다.

다음 확인:
- `nwgpuprobe5 d3d11ui`(K25): SM4 셰이더로 D3D11 프런트엔드를 지나는 시험 12개 — 컬링(시계·반시계, FrontCounterClockwise), 동적 정점 버퍼(DISCARD 뒤 NO_OVERWRITE), 동적 상수 버퍼(그리기마다 DISCARD), 미리 곱한 섞기, 가위, BGRA 텍스처, UpdateSubresource, 화면 밖 대상에 그린 뒤 합치기, 구형 블릿 스왑체인 두 개(1버퍼, 120x61에서 크기 변경). VM의 WARP·Parallels에서 모두 통과해 시험 자체를 확인했습니다.
- 2763 스위치(K26~K29): 컬링 끔, 앞면 방향 뒤집기, 가위 끔, 섞기 끔을 하나씩 켜고 Cinebench를 봅니다. 버튼이 나타나는 스위치가 원인 상태입니다.
- Edge GPU 프로세스 충돌(0x80000003): K24로 덤프를 꺼내고 `gpucov/analyze-dumps.sh`로 VM의 cdb에서 스택을 봅니다. 우리 UMD 소스에는 `__debugbreak`·`DebugBreak`가 없으므로, Chromium의 CHECK 실패일 가능성이 큽니다 [가설].

### 2.14 2763 K25·K30 — 구형 블릿 스왑체인의 화면 내보내기가 거부됨, 2764(P28)에서 고침

**결론: 원인은 우리 UMD의 DXGI Blt가 맥북 화면 크기(2560x1600)일 때만 받도록 박혀 있던 것입니다. 맥미니(1920x1080)에서는 창 크기 버퍼로 오는 Blt가 모두 E_INVALIDARG로 거부되어, 창에 우리 그림이 한 번도 도착하지 않았습니다** [코드로 확인한 사실, 실기기 확인은 2764 K25b]. K25에서 사각형 자리에 뒤 바탕화면 색(a58d79, bac6d0)이 보인 것, K30에 KMD의 `Wom1G2PresentDdi` 기록이 아예 없는 것과 맞습니다. "프레젠트 16번"은 기록 상한(umd.c `ordinal <= 16`)이라 버려진 수가 아닙니다.

2764에서 바꾼 것(P28):
- DXGI Blt·Blt1·CopyResource·Present가 크기를 호출 인자와 리소스 정보에서만 얻습니다. 크기가 다르면 GPU로 늘이고(선형 필터), BGRA↔RGBA 변환과 MSAA 풀기도 같은 블릿이 합니다.
- Present: 대상 리소스가 있는 호출(hDstResource)과 AllowTearing·IMMEDIATE_ALLOW_TEARING도 받습니다(Edge가 씁니다).
- KMD 화면 블릿: X8R8G8B8(형식 22) 대상도 받습니다.
- 해상도 상수를 없앴습니다. 원래 해상도는 부팅 때 펌웨어가 넘긴 화면(POST)의 크기를 그대로 씁니다. 다른 모니터로 부팅하면 그 모니터 크기가 원래 해상도가 되고, 늘여 보이는 모드 목록도 그 안에 들어가는 후보만 냅니다. 남긴 숫자와 이유는 PATCHES.md P28에 표로 적었습니다.

시험: K25b(크기 변경 있음·없음 블릿, 플립 스왑체인, UMD 거부 기록), K31(실행 중 모드 전환 뒤 같은 시험, 원래 모드로 복귀), K30(KMD 기록), K21(Cinebench), K23·K23b(Edge, 두 번째는 DirectComposition 끔).

### 2.15 2764 실기기(21:58~22:04) — 블릿 방식은 여전히 안 보임, UMD Blt는 불리지 않음

**결론: DXGI는 구형 블릿 스왑체인을 우리 UMD의 `pfnPresent`(Blt 플래그, 대상 없음)로만 내보내고, 우리 UMD는 그것을 `pfnPresentCb`로 넘겨 S_OK를 받습니다. 그 뒤 KMD `DdiPresent`에는 Blt가 한 번도 오지 않았습니다(`Wom1G2PresentDdi` 없음). 따라서 화면에 안 나오는 원인은 dxgkrnl이 그 프레젠트를 창의 리디렉션 표면으로 옮기는 단계이고, 어디서 끊기는지는 아직 모릅니다** [K25b UMD 기록으로 확인한 사실, 끊기는 지점은 미확인]. 2764의 UMD Blt 수정은 이 경로에 닿지 않았습니다.

| 시험 | 우리 드라이버 | WARP |
|---|---|---|
| blt_swapchain (크기 변경) | 실패 0c0c0c | 통과 |
| blt_noresize (늘임) | 실패 0c0c0c | 통과 |
| flip_swapchain | 통과 | 통과 |
| 모드 전환(1920x1080→1680x1050→복귀) | 전환·복귀 통과, 플립 통과, 블릿 실패 | — |

Edge: DirectComposition을 꺼도(K23b) 같은 `skia_output_surface_impl.cc:1228` FATAL이 나고, 그 앞에 `skia_output_device_gl.cc:223 Couldn't create surface`가 찍힙니다. ANGLE이 창 표면(스왑체인 또는 공유 텍스처)을 못 만드는 것으로 보입니다 [가설].

2765(P29)는 끊기는 곳을 찾는 기록판입니다: KMD `DdiPresent`·표준 할당·할당 열기·할당 생성 실패를 모두 남기는 기록 링, 레지스트리로 켜는 UMD 기록(DWM·Edge GPU 프로세스처럼 환경 변수를 줄 수 없는 프로세스), 모든 DXGI 진입점의 첫 8번 기록. 시험은 K33(DWM 기록 + 블릿 시험), K34(Edge GPU 프로세스 기록), K32(KMD 기록 링 판독).

### 2.16 2765 실기기(22:21~22:24) — 스테이징 표면 요청을 모두 거부하고 있었음, 2766(P30)에서 받음

**결론: dxgkrnl이 요청한 스테이징 표준 할당(STAGINGSURFACE) 108번이 전부 STATUS_INVALID_PARAMETER로 거부됐습니다. 블릿 방식 프레젠트를 CPU 쪽 리디렉션 표면으로 옮길 때 쓰이는 표면으로 보이며, 프레젠트가 S_OK 뒤 사라지는 것과 맞습니다** [거부는 기록으로 확인한 사실, 블릿 실패와의 인과는 2766 K25b로 확인할 가설]. 2766은 주어진 크기의 CPU 가시 선형 32비트 표면으로 받습니다.

| 기록 | 값 |
|---|---|
| GetStandardAllocationDriverData 호출 | 112 |
| 그중 실패(모두 STAGINGSURFACE) | 108 |
| OpenAllocation (처음 48번) | 모두 성공 |
| DdiPresent(MMIO 플립 제외) | 0 |

K33·K34의 DWM·Edge 기록 파일은 만들어지지 않았습니다. 작업이 레지스트리를 32비트 보기로 썼을 수 있어, 2766 작업은 `/reg:64`로 쓰고 지우기 직전에 키를 출력합니다 [가설].

### 2.17 2766 실기기 — Cinebench 화면과 버튼이 보임, Edge만 남음

**결론: 스테이징 표면을 받자 블릿 방식 시험이 모두 통과했고 콘치님이 Cinebench 메인 창과 대화 상자 버튼을 확인했습니다. Edge는 여전히 ":(" 화면입니다** [실기기 확인]. Edge 원인을 가르려고 2767에 두 가지를 넣었습니다. 하나는 Chromium·ANGLE이 출력 표면을 만들 때 부르는 호출을 하나씩 해 보는 시험(`nwgpuprobe7 chromium`, K35)이고, 다른 하나는 죽는 GPU 프로세스에서도 남는 UMD 기록(P31, K34)입니다.

| 시험 | 2765 | 2766 |
|---|---|---|
| K25b 우리 드라이버 | 13/17 | 17/17 |
| 스테이징 표준 할당 | 108번 모두 거부 | 성공(1920x1009, 피치 0x1e00) |
| KMD 블릿 DdiPresent | 없음 | 도착, 성공 |
| Cinebench 버튼 | 안 보임 | 보임 |
| Edge | ":(" | ":(" |

### 2.18 2767 K35 — 공유 R10G10B10A2 텍스처 하나가 장치를 제거시킴, 2768(P32)에서 고침

**결론: 우리 UMD는 공유 텍스처를 BGRA8·RGBA8·A8만 받았고, 거부를 E_INVALIDARG로 런타임에 알렸습니다. D3D11 런타임은 CreateResource에서 E_OUTOFMEMORY 외의 오류를 받으면 장치를 제거하므로, 그 뒤 모든 호출이 DEVICE_REMOVED가 됐습니다** [K35 기록으로 확인한 사실]. Edge가 죽는 원인일 가능성이 큽니다 [가설]. 2768은 D3D11.1이 공유를 보장하는 단일 평면 형식을 모두 받고, 받지 못하는 생성은 그 호출만 E_OUTOFMEMORY로 실패시킵니다.

| K35 (2767) | 통과 | 실패 |
|---|---|---|
| 우리 드라이버 | 67 | 26 (첫 실패 뒤 모두 장치 제거) |
| WARP | 139 | 9 (세션·출력 관련) |

### 2.19 2769 실기기 — Edge FATAL의 원인 후보는 D3D11 MSAA 보고 없음, D3D11On12로는 Edge가 그려짐

**결론: Edge 기본 경로(ANGLE D3D11 → 우리 D3D11 UMD)는 우리 UMD가 MULTISAMPLE_RENDERTARGET 비트를 어떤 형식에도 주지 않아 Skia가 BGRA8·RGBA8을 렌더 불가로 보고 멈춘다고 봅니다. D3D11On12 경로는 우리 D3D12 UMD가 2x·4x를 보고하므로 그려집니다(콘치님 확인)** [소스 사슬로 세운 가설, 2770 K37m·K37f로 확인]. 근거 표는 PATCHES.md P34에 있습니다.

| 형식 | 우리 D3D11 (2769 K35) | WARP | 다른 비트 |
|---|---|---|---|
| BGRA8 | 0x11def3f3 | 0x03fef3f3 | MULTISAMPLE_RENDERTARGET 없음, 타입 UAV 없음, 비디오 출력 있음 |
| RGBA8 | 0x13def3f3 | 0x03fef3f3 | MULTISAMPLE_RENDERTARGET 없음, 비디오 출력 있음 |
| RGB10A2 | 0x13def3f3 | 0x03fef3f3 | MULTISAMPLE_RENDERTARGET 없음, 비디오 출력 있음 |
| RGBA16F | 0x12def3f3 | 0x02fef3f3 | MULTISAMPLE_RENDERTARGET 없음, 비디오 출력 있음 |

D3D11On12 경로의 첫 GPU 프로세스는 WebGL에서 ANGLE `ResourceManager11::allocate`가 0x8007000E(E_OUTOFMEMORY)로 실패하고 다시 시작했습니다. 이 WebGL 페이지는 24바이트 정점 버퍼 하나만 만드므로, 실패한 것은 ANGLE 자신의 버퍼(동적 업로드 등)로 봅니다 [가설]. 어느 D3D12 할당인지는 K37d(환경 변수로 GPU 프로세스 기록)로 찾습니다.

### 2.20 2770 실기기 — 실제 MSAA로 Edge 기본 경로가 됨, 2771(P35)에서 기본값으로

**결론: 정책 0x20 + 화면 MSAA(실제 2x·4x)를 켜면 Edge 기본 경로가 우리 드라이버에서 FATAL 없이 돌았습니다(K37f). 정책 0x80(비트만)은 장치를 만들지 못했습니다(K35m 0x887a0020). 2771에서 실제 MSAA를 기본으로 켜고, 레지스트리로 끄는 스위치를 넣었습니다** [K37f·K35f 실기기 결과, 2771 기본값은 아직 실기기 확인 전].

| 시험(2770) | 결과 |
|---|---|
| K35f(0x20 + 화면 MSAA) | 143/151 통과, 2x·4x 품질 단계 1, 실패 8개는 WARP와 같은 시험 도구 문제 |
| K35m(0x80) | 장치 생성 실패 0x887a0020 |
| K37f Edge | fatals 0, 충돌 보고 0, 우리 UMD 유지 |
| K37 Edge(기본 2770) | fatals 3 |
| K25b | 17/17 |

남은 것:
- Edge 첫 GPU 프로세스가 한 번 사라짐: K37f에서도 WebGL에서 ANGLE `ResourceManager11::allocate`가 E_OUTOFMEMORY로 실패한 뒤 다시 시작했습니다. P32부터 생성 실패를 E_OUTOFMEMORY로 바꿔 보고하므로, 실제로는 우리가 받지 못한 생성 요청일 가능성이 큽니다 [가설]. K38t 기록의 `reject-create-error-as-oom {줄, 원래 hr}`로 확인합니다.
- 기록 파일이 안 남은 원인(유력): 이전 작업 D17이 `setx /M`으로 `APPLE_AGX_UMD_TRACE_FILE=C:\NWOAS\umd-trace.log`를 시스템 전체에 걸어 두었습니다. 끄는 작업은 없습니다. 그러면 모든 프로세스가 그 파일로 기록하고 레지스트리 기록 확인을 건너뜁니다. 흔적 파일이 하나도 없던 것과 맞습니다. 또 K37 계열은 GPU 샌드박스를 켠 채 돌려, 낮은 무결성 GPU 프로세스가 `C:\NWOAS\gpucov`에 쓰지 못했습니다 [가설, K40으로 확인]. K40이 시스템 변수와 그 파일을 읽고, D17b가 끕니다.

---

## 3. 고침 — 2758판(P22)

**결론: 풀 창이 없을 때 일반 버퍼 한도를 16 MiB에서 구역 2 크기(952 MiB)로 올렸습니다. 빌드와 테스트 서명까지 했고, 실기기 시험 전입니다.**

| 항목 | 값 |
|---|---|
| 바꾼 파일 | `port-j274/gpu/driver-p15/drivers/apple-agx/render-admission/src/allocation_windows.c` (J274 빌드에서만, 19줄) |
| 차이 파일 | `port-j274/gpu/driver-p15/P22-driver.diff` |
| 기록 | `port-j274/gpu/driver-p15/PATCHES.md` P22 |
| 빌드 | `port-j274/gpu/driver-p15/build-p22.sh` (VM 실행 이름 j274g3v, 약 3분) |
| 결과물 | `port-j274/gpu/driver-p15/out/j274-full-20261118-1816-p22` |
| KMD sha256 | `6d83224d7cf6165d8c5947c36719d8655fa53288a2699cea5a3eeb85a6303ec2` |
| UMD sha256 | `1a04727132f8e4c7d691406ebe47450fdc787235b9885b0e4a8a41fdcbfbd27c` |
| 전달 매니페스트 | `nwoas_scripts/file-delivery-s243/manifests/agx-j274-2758-gpucov.json` (드라이버 4개 + 시험 도구 8개) |
| 설치 | `bash agx-s248/run-install-2758.sh <옛 .out> <옛 .link>` (실행기 기본 `agxdcp-s256`, `LAUNCHER=`로 바꿈), 재시작 없는 판 `run-install-2758-norestart.sh` |
| 설치 작업 | `agx-s248/jobs/I20-install-2758.cmd` (G3Armed 먼저, I19와 같은 순서) |

| 검사 | 결과 |
|---|---|
| `/W4 /WX` 컴파일 | 통과, 경고 줄 수 2757과 같음(97) |
| 테스트 서명 | WDKTestCert(2757과 같은 인증서) |
| InfVerif | 통과 |
| 맥미니 시험 | **안 함** |

남은 위험 [가설]:
- 큰 버퍼가 구역 2를 채우면 VidMm이 바탕화면 표면을 시스템 메모리로 내보냅니다. 그 경로(페이징 복사)는 큰 크기로 시험한 적이 없습니다. K3(64 MiB × 최대 16개)로 확인합니다.
- 952 MiB는 브라우저·게임을 함께 쓰기에는 작습니다. 진짜 해결은 4장 1번(풀 창)입니다.

---

## 4. 진짜 드라이버와의 차이

**결론: Windows가 "그래픽 드라이버"에 기대하는 것 중 지금 있는 것은 3D 엔진 하나, 구역 2개, ARM64용 D3D11/12입니다.** 일상 사용에 필요한 것은 아래 순서입니다.

### 4.1 무엇이 보이고 무엇이 없나 (작업 관리자 기준)

| 작업 관리자 항목 | 보통 드라이버 (AMD·Intel·Qualcomm) | 우리 드라이버 | 이유 |
|---|---|---|---|
| 3D 엔진 | 있음 | 있음 | 노드 0 = 3D |
| Copy 엔진 | 1~2개 | **없음** | 노드 1(복사)은 시험용 설정 + 하이퍼바이저 기능이 있을 때만 (`gpuva_g3_windows.c:515-517`) |
| Compute 엔진 | 있음(일부) | 없음 | 계산도 3D 노드에서 돎 |
| Video Decode / Encode / Processing | 있음 | **없음** | 동영상 DDI와 AVD 연결 없음 |
| 전용 GPU 메모리 | 있음 | 952 MiB | 구역 2 |
| 공유 GPU 메모리 | 보통 RAM의 절반 | 256 MiB 창만 | 구역 1. 풀 창(구역 3)이 꺼져 있음 |

### 4.2 빠진 것 — 일상 사용 우선순위

| 순위 | 할 일 | 막고 있는 것 | 크기 |
|---|---|---|---|
| 1 | **메모리**: P22 검증 → 풀 창(구역 3) 켜기. 하이퍼바이저 g248C 보호(`backing_find` NULL)를 실기기에서 검증하고 실행기 `NWOAS_AGX_POOL_IPA`를 다시 켬 | 큰 텍스처를 쓰는 모든 앱, 브라우저 여러 탭, 게임 | P22 작음, 풀 창 중간 |
| 2 | **화면 출력 정상화**: UMD `PresentMultiplaneOverlay1` 구현 → `DisableDDisplay` 없이. 수직 동기 진짜 신호로 60장/초 | 끊김·지연, 전체 화면 앱 | 중간~큼 |
| 3 | **x64·x86 사용자 드라이버**(+ ARM64X 전달 DLL). 빌드 스크립트 `render-admission/umd/build-umd-x64.ps1`, `build-umd-x86.ps1`가 있음 | Prism으로 도는 모든 앱·대부분의 게임 | 큼 |
| 4 | **복사 엔진**을 기본으로 켜기 (노드 1) | 업로드·다운로드가 그리기와 겹치지 못함, 작업 관리자 Copy | 중간 |
| 5 | **동영상 재생 가속**(Apple AVD) + D3D11 동영상 DDI | 동영상이 CPU로만 디코드됨(재생은 됨, 전력·발열) | 큼 |
| 6 | 오류 복구(TDR)·실행 중 장치 재시작(0xC0000483) | 드라이버 판 교체마다 재부팅 | 중간 |
| 7 | OpenGL ICD(ARM64). 원본에 WGL ICD 소스 `windows/icd/agx_wgl_icd.cpp` 있음 | OpenGL 앱(일부 게임·도구) | 중간 |
| 8 | GPU 주소 갱신을 드라이버로(하이퍼바이저 부담 줄이기, 로드맵 C6) | 성능 | 큼 |
| 9 | Vulkan | Vulkan 앱 | 매우 큼 |

---

## 5. 작업 순서

**결론: 먼저 2757 그대로 시험 도구를 한 번 돌려 지금 값을 남기고, 2758을 설치해 같은 시험을 다시 돌립니다.** 그 결과로 풀 창 작업으로 갈지 정합니다.

| 순서 | 할 일 | 명령 | 콘치님 손 |
|---|---|---|---|
| 1 | (끝남) 2758 설치, K6·K4 | — | — |
| 2 | 2758 그대로 비교: 풀 증명 끔 | `KIT="K4b K2b K8" bash gpucov/run-gpucov.sh run <link>` | 라이선스 창 확인 |
| 3 | 2759 설치 후 전체 시험 | `bash agx-s248/run-install-2759.sh <옛 .out> <옛 .link>` → `KIT="K6 K2 K3 K4 K5 K7" bash gpucov/run-gpucov.sh run <link>` | 화면 확인(Cinebench 창) |
| 4 | 2758이 통과하면 풀 창 실험: g248C 보호 판으로 `NWOAS_AGX_POOL_IPA=0xd00000000` 실행기 사본 | 새 실행기 사본 | 재부팅 |
| 5 | DDisplay: UMD `PresentMultiplaneOverlay1` | VM 빌드 | 화면 확인 |
| 6 | x64 UMD + ARM64X 전달 DLL | VM 빌드 | — |
| 7 | 복사 노드, 동영상 가속, 오류 복구 | — | — |

---

## 6. 합격 시험

**결론: 아래 표를 모두 통과해야 "그래픽 드라이버를 만들었다"고 말할 수 있습니다.** 판정은 시험 도구 출력과 콘치님 눈으로만 합니다.

| # | 시험 | 도구 | 합격 기준 |
|---|---|---|---|
| A1 | 큰 힙·버퍼·텍스처 | K2 | `heap_summary` 버퍼·텍스처·업로드·읽기 `largest_ok_mib` ≥ 512, `committed_texture` 8192x8192 `hr=0x00000000` |
| A2 | 그리기·계산이 맞는 값 | K2 | `clear_readback result=PASS`, `compute result=PASS` (D3D11·D3D12 모두) |
| A3 | 창에 실제로 보임 | K2 | `present11`·`present12` `screen_check=PASS`, fps ≥ 55 |
| A4 | 채우기 뒤에도 장치 정상 | K3 + 바탕화면 | 64 MiB × 14개 이상, `removed_reason=0x00000000`, DWM 충돌 0 |
| A5 | **Cinebench 2026 UI** | K4 + 눈 | `WINSTAT ... result=CONTENT`(회색 한 색이 아님), 콘솔 로그에 `DX critical error` 없음, UMD 기록 `heap_oom=0` |
| A6 | **Edge + WebGL** | K5 | `webgl_draw=PASS`, 렌더러 문자열에 Apple AGX 장치(Basic Render·SwiftShader 아님), 10초에 frames ≥ 550 |
| A7 | WebGPU | K5 | `webgpu=ok` (지금은 기대 안 함, 기록만) |
| A8 | **동영상 재생** | K5 `decode_*`, K2 `video_device` | 1단계: 1080p H.264 재생이 끊기지 않음(`sup_smooth`). 2단계(AVD 뒤): `_hw`, `profiles` ≥ 1 |
| A9 | **D3D11 게임 또는 벤치마크 하나** | 콘치님 실행 (ARM64판이 있는 것, 예: 3DMark Night Raid ARM64) | 완주, 화면 정상, 점수 기록 |
| A10 | **작업 관리자 엔진 목록** | K6 | 1단계: `ENGINE ... 3D` + `copy`. 2단계: `VideoDecode` |
| A11 | dxdiag | K1 | "No problems found", 기능 수준·드라이버 모델 기록 |
| A12 | x64 앱 | K2 x64 | `d3d11 device` 어댑터가 Apple AGX(Basic Render 아님) — 3번 작업 뒤 |

---

## 7. 시험 도구 (`nwoas_scripts/gpucov/`)

**결론: 맥미니에서 GUI가 필요한 시험은 모두 로그인한 사용자 세션에서 돌도록 만들었고, 같은 도구를 Parallels VM에서 먼저 돌려 동작을 확인했습니다**(VM 결과는 Parallels 장치 값이라 맥미니 판단에는 쓰지 않습니다).

| 파일 | 하는 일 |
|---|---|
| `probe/nwgpuprobe.cpp` → `bin/nwgpuprobe-arm64.exe`, `bin/nwgpuprobe-x64.exe` | DXGI 어댑터·예산, D3D12/D3D11 장치·기능, 힙·버퍼·텍스처 크기 사다리(1~512 MiB, 1024²~16384²), 지우기+읽기 확인, 계산 실행+읽기 확인, 동영상 디코드 종류 수, 스왑체인 표시와 화면 픽셀 확인, `fill`(64 MiB 힙을 실패할 때까지, 1 GiB 상한) |
| `probe/build-probe.cmd` | VM 빌드(MSVC 14.44, ARM64와 x64). 소스를 `\\Mac\media\nwoas\gpucov-build\`에 두고 실행 |
| `web/webgl.html` | WebGL1/2 렌더러 문자열, 삼각형 그린 뒤 픽셀 읽기, WebGPU 어댑터, 코덱별 디코드 가능 여부(하드웨어 여부), 10초 애니메이션 프레임 수를 창 제목에 |
| `ps/usersess.ps1` | 작업 스케줄러(Users 그룹)로 사용자 세션에서 .cmd 실행, 완료 파일 대기 |
| `ps/winstat.ps1` | 창 영역을 화면에서 캡처해 색 수·한 색 비율 계산(회색 한 색이면 `BLANK`) |
| `ps/cb26ui.ps1` | Cinebench UI 시작(이 프로세스에만 UMD 기록 켬) → 25·50·75초에 창 측정 → 종료 |
| `ps/edge.ps1` | Edge 헤드리스 결과 + 일반 창 10초, 자기 전용 프로필만 씀·닫음 |
| `ps/engines.ps1` | GPU 엔진 계수기 종류, 메모리 계수기, WMI, 디스플레이 클래스 레지스트리 |
| `jobs/K1`~`K7` | K1 dxdiag, K2 프로브, K3 채우기, K4 Cinebench UI, K5 Edge, K6 엔진, K7 결과만 다시 읽기 |
| `run-gpucov.sh` | `deliver`(재시작 + 도구 전달 + D28 + 시험) / `run <link>`(켜진 세션에서 시험만) |
| 전달 작업 | `file-delivery-s243/jobs/gpucov-j274-20261118.cmd` (도구 8개 → `C:\NWOAS\gpucov`) |

VM 시험 결과(도구가 도는지만 확인):

| 작업 | VM 결과 |
|---|---|
| K1 | dxdiag 1262줄, 카드 이름·기능 수준·드라이버 모델 추출 |
| K2 | 사용자 세션 실행, D3D11·D3D12 지우기·계산 PASS, 표시 58.5·50.4 장/초, 화면 픽셀 PASS |
| K3 | 64 MiB × 16개 |
| K4 | Cinebench 없음 경로(`NO_APP`)만 확인 |
| K5 | Edge 157, WebGL 그리기 PASS, 10초 585장 |
| K6 | 엔진 종류·메모리 계수기 출력 |

주의:
- 맥미니 Windows에 사용자가 로그인되어 있지 않으면 사용자 세션 시험(K1·K2·K4·K5)은 돌지 않습니다. 출력의 `explorer_sessions=`가 비어 있으면 그 경우입니다.
- 도구 전달은 같은 이름 파일을 덮어쓰지 않습니다. 다시 보낼 때는 Windows의 `C:\NWOAS\gpucov` 도구 파일을 먼저 지워야 합니다.
- K4의 UMD 기록은 50 MB를 넘으면 지웁니다.

---

## 8. 모르는 점

| 모르는 점 | 확인 방법 |
|---|---|
| Cinebench가 실제로 요청하는 힙 크기 [가설: 16 MiB 초과] | K4 UMD 기록 |
| 2758에서 Cinebench가 메모리 다음 단계에서 또 막히는지(셰이더·형식 등) | K4 콘솔 로그, 창 측정 |
| 큰 할당이 바탕화면 표면을 밀어낼 때 페이징 경로가 버티는지 | K3 + 바탕화면 확인 |
| Edge(ARM64)가 우리 장치로 WebGL을 하는지, 아니면 소프트웨어로 내려가는지 | K5 렌더러 문자열 |
| 풀 창을 켠 하이퍼바이저(g248C 보호)가 실기기에서 버티는지 | 5장 4번 |
