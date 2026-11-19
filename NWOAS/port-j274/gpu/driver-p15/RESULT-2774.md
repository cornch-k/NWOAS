# 2 GiB 넘는 GPU 메모리 실패 분석과 2774판 (P38, 2026-11-19)

> 표기: **[사실]** = 코드·로그·빌드로 확인. **[계산]** = 코드의 값을 넣어 계산. **[가설]** = 확인하지 않은 추측. **[빌드만]** = 만들기만 하고 맥미니에서 돌리지 않음.
> 드라이버 소스 행 번호는 2773 소스(P38 적용 전) 기준입니다. 근거 분석 일부는 읽기 전용 보조 에이전트 3개(UMD·KMD·하이퍼바이저)가 했고, 핵심 행은 제가 다시 열어 확인했습니다.
> 작성 중 맥미니에는 손대지 않았습니다(SSD 시험 중). 빌드와 시험 프로그램 점검은 이 맥북의 Parallels VM에서만 했습니다.

---

## 1. 결론

**2 GiB 버퍼에서 장치가 사라진 직접 원인은 아직 확정하지 못했습니다. 그날 기록에는 그것을 가릴 값이 남지 않았습니다.** 대신 아래 세 가지는 코드로 확정했고, 2774판에서 고쳤습니다 [빌드만].

| # | 확정한 것 | 결과에 미친 영향 | 2774에서 바꾼 것 |
|---|---|---|---|
| 1 | 하이퍼바이저(풀 창·GPU 주소 중개)에는 4 GiB 안에서 걸리는 한도가 없고, 그날 거절도 0건 [사실] | 원인이 아님 | 없음(하이퍼바이저 새 판 없음) |
| 2 | D3D12 복사 함수가 2 GiB 이상 버퍼의 복사를 무조건 거절함(`Bytes > 0x7fffffff` → E_INVALIDARG → 장치 오류 0x887A0020) [사실] | 2048 시험이 만들기에서 안 막혔어도 첫 복사에서 같은 오류로 끝났을 것 [사실, 코드] | 상한을 4 GiB로, 복사를 1 GiB 조각으로 |
| 3 | 풀 예약이 구역 3 크기(4096 MiB)를 넘어서도 허락됨 → VidMm이 다른 곳에 둘 수 없는 버퍼를 내보내야 함 [사실, 코드] | fill2의 17번째 힙에서 "장치 재설정"(0x887A0007), 표지 0/16 [가설: 재설정까지의 경로] | 구역 크기 − 256 MiB를 넘는 생성은 깨끗한 메모리 부족(E_OUTOFMEMORY)으로 거절 |
| 4 | 2 GiB 버퍼 만들기 직후의 장치 제거를 남길 기록이 없었음(페이징 실행 실패·주소표 갱신 실패는 메모리에만 있었음) [사실] | 원인을 못 가림 | 새 영수증 `Wom1PagingExecFailure` + 시험 프로그램이 실패 직후 영수증을 읽음 |

- **그래서 2774로 `ladder3072` 통과를 약속할 수는 없습니다.** 2번은 확실한 다음 장애물이라 고쳤지만, 2048에서 먼저 일어난 장치 제거의 원인은 2774 실기 기록(4.3장)으로 처음 가려집니다.
- 바꾼 것은 드라이버뿐입니다. 하이퍼바이저·실행기·`m1n1-standalone-s264`·out/ 기존 결과물은 건드리지 않았습니다.

---

## 2. 그날 일어난 일 [사실, `nwoas_scripts/gpupool-s265/runs/chain-s265-20261119-070452.txt`]

### 2.1 하나짜리 큰 버퍼(memladder)

**결론: 1536 MiB까지 내용까지 맞았고, 2048 MiB는 만들기는 됐는데 그 뒤 첫 CPU 쓰기 준비(UPLOAD 버퍼 Map)에서 장치가 이미 제거되어 있었습니다.**

| 크기 (MiB) | 만들기 | 단계 | 로컬 사용량 (MiB) | 장치 제거 사유 | 걸린 시간 (ms) | 결과 |
|---|---|---|---|---|---|---|
| 256 | S_OK | 끝까지 | 384 | 없음 | 594 | PASS |
| 512 | S_OK | 끝까지 | 640 | 없음 | 1312 | PASS |
| 1024 | S_OK | 끝까지 | 1152 | 없음 | 2188 | PASS |
| 1536 | S_OK | 끝까지 | 1664 | 없음 | 3187 | PASS |
| 2048 | S_OK | map_upload(복사 전) | 2176 | 0x887A0020 (DRIVER_INTERNAL_ERROR) | 3031 | FAIL |

- 2048의 Map 실패는 UMD 안에서 생길 수 없습니다. `MapHeap`은 미리 잡아 둔 주소를 그대로 돌려줍니다(`d3d12/agx_d3d12_ddi.cpp:2807-2813`). 그러니 Map 전에 장치가 제거된 것입니다 [사실, 코드].
- 만들기와 Map 사이에 UMD가 장치 오류를 보고할 경로가 없습니다. UMD에는 별도 스레드가 없고, 오류 보고(`pfnSetErrorCb`)는 모두 호출 안에서만 일어나며 버퍼 만들기 경로는 E_OUTOFMEMORY만 돌려줍니다(`mesa/winsys/agx_d3d12_windows.cpp:1212-1250`) [사실, 코드]. 그래서 제거는 커널 쪽(VidMm/KMD)에서 왔다고 봅니다 [가설].
- 같은 시험 끝의 시스템 이벤트 조회(디스플레이 4101 = GPU 응답 없음 복구)는 0건이었습니다. 어댑터 전체 재설정(TDR)은 아니었던 것으로 봅니다 [사실: 이벤트 0건 / 가설: 해석].

### 2.2 256 MiB 힙 채우기(fill2)

**결론: 16개(4096 MiB = 구역 3 전체)까지 만들어졌고, 16번째는 사용량이 늘지 않았고, 17번째 만들 때 장치가 재설정되었습니다.**

| 힙 수 | 합계 (MiB) | 로컬 사용량 (MiB) | 비고 |
|---|---|---|---|
| 1~15 | 256~3840 | 256~3840 | 힙마다 사용량 +256 |
| 16 | 4096 | 3840 | 사용량이 늘지 않음 |
| 17 (생성) | — | — | 0x887A0005 DEVICE_REMOVED, 사유 0x887A0007 DEVICE_RESET → 표지 0/16 |

### 2.3 그 밖의 숫자

| 항목 | 값 | 근거 |
|---|---|---|
| DXGI 로컬 예산 (MiB) | 4290 | K43 `dxgi` 줄 |
| 952 + 4096 = 5048의 85 % (MiB) | 4290.8 | [계산] — 예산은 구역 2와 3을 합친 값 |
| 시험 전에 구역 3에 이미 올라간 양 (MiB) | 103 | K43 `pool demand committed_mib=103` |
| D3D11 버퍼 | 1024 MiB까지 PASS | K43 `d3d11mem` |

- 예산 4290 MiB에는 구역 2(952 MiB)가 들어 있지만, GPU 전용 버퍼는 구역 3에만 놓일 수 있습니다(`render-admission/src/allocation_windows.c:621-633`). 그래서 DXGI가 알려 주는 예산은 이런 버퍼가 실제로 쓸 수 있는 양보다 큽니다 [사실, 코드].

---

## 3. 원인 분석

### 3.1 하이퍼바이저는 한도가 아닙니다 [사실]

**결론: 4 GiB까지 하이퍼바이저 표·힙·창 어디에도 걸리지 않고, 그날 하이퍼바이저가 거절한 명령은 없습니다.**

| 한도 | 크기 | 1.5 GiB에 필요 | 2 GiB에 필요 | 3 GiB에 필요 | 4 GiB에 필요 | 근거 |
|---|---|---|---|---|---|---|
| GPU 주소 중개 페이지 기록 (16 KiB 1개당 1개, 공유 시 함께 씀) | 294,912 | 98,304 | 131,072 | 196,608 | 262,144 | `m1n1-gpu-hv2/src/hv_agx_gpuva_v5.c:495-514`, 기록 "GPU broker table 294912" |
| 주소표 기록 (32 MiB당 1개, 앱+페이징 두 프로세스) | 4,096 | 96 | 128 | 192 | 256 | `hv_agx_gpuva_v5.h:16`, `hv_agx_gpuva_v5.c:295-301` |
| 풀 창 크기 (MiB) | 4,096 | 1,536 | 2,048 | 3,072 | 4,096 | `hv_agx_pool_window.c:175-194` |
| 매핑할 때 더 쓰는 하이퍼바이저 힙 (MiB) | — | 0 | 0 | 0 | 0 | 창 준비 때 4 GiB분 단계 2 표를 미리 만듦(`hv_vm.c:4883-4919`) |

- 풀 창 명령의 모든 칸은 64비트이고, 0x80000000 부호 문제는 없습니다(`apple_agx_pool_window_abi.h:14-24`, KMD `pool_windows.c:51-81`) [사실].
- 실패한 풀 창 명령은 모두 기록되는데(`nwoas_agx_broker.c:769-772`), 그날 기록에 `pool window cmd=` 줄은 0개입니다. 주소 중개의 첫 용량 초과도 기록되는데(`hv_agx_retained_platform.c:296-320`) 0개입니다 [사실].
- 다만 주소 중개가 "용량 초과" 말고 다른 거절(TLB, TAINTED, INVALID)을 하면 기록이 남지 않습니다. 이 경우 KMD는 주소표 갱신 실패로 그 프로세스를 오염 처리합니다(`gpuva_g3_paging_windows.c:943-951` → `:1615-1640`) [사실, 코드]. 2048에서 이것이 일어났는지는 알 수 없습니다 [가설]. 2774의 새 영수증이 이 경우를 잡습니다(4.3장).
- 첫 커밋 상태: m1n1-gpu-hv2 소스는 g254L보다 새것이지만, g254L 뒤 패치는 풀 창·주소 중개 파일을 건드리지 않습니다. 해당 파일 해시가 `MANIFEST-hv2.sha256`과 같습니다 [사실].

### 3.2 KMD의 생성·예약 경로는 2 GiB에서 막히지 않습니다 [사실]

**결론: 크기는 끝까지 64비트이고, 예약이 실패했다면 만들기가 E_OUTOFMEMORY로 끝났어야 하는데 S_OK였습니다.**

| 확인한 곳 | 내용 | 근거 |
|---|---|---|
| 일반 버퍼 최대 | 구역 3 크기(4096 MiB) | `allocation_windows.c:81-88` |
| 예약 | 64 KiB 단위 개수를 64비트로 계산, 거절하면 STATUS_NO_MEMORY | `pool_windows.c:794`, `allocation_windows.c:611-615` |
| 커밋(실제로 붙이기) | 64 MiB씩 32번, 실패하면 "메모리 없음"으로 프로세스 오염 | `render_pool_demand.c:100-115`, `pool_windows.c:455-485` |
| 채우기·복사 페이징 | 한 번에 `MAXULONG`(4 GiB−1)까지 | `gpuva_g3_paging_windows.c:1255` |

- 그날 시작 영수증의 `commit_failures=0`은 시험 전 값입니다. 2048 뒤의 값은 읽지 않았습니다(K43이 시작 때만 읽음) [사실].
- 실패를 남기는 장치가 부족했습니다. CPU 페이징 실행기가 실패하면 VidMm에 "페이지 오류"(DXGK_INTERRUPT_DMA_PAGE_FAULTED)로 알리는데(`paging_windows.c:286-307`) 그 상태는 메모리에만 있고, 주소표 갱신 실패로 인한 오염도 메모리 카운터(`G3PagingContractCompletions`)뿐이었습니다(`render_admission.h:705-706`) [사실].
- 남은 후보 [모두 가설, 순위]:

| 순위 | 후보 | 맞으면 2774 기록에 나오는 것 |
|---|---|---|
| 1 | 2 GiB를 올리는 페이징 기록 하나가 CPU 실행기에서 실패 → 페이지 오류 → 이 장치만 제거 | `exec_failure failures≥1 last_status=…` |
| 2 | 주소 중개의 기록 없는 거절(TLB 등) → 주소표 갱신 실패 → 프로세스 오염 | `exec_failure contract_completions≥1 contract_status=0xc0000186` |
| 3 | 커밋 실패(창 매핑) | `pool_demand commit_failures≥1 last_window_result≠0` |
| 4 | UMD가 모르는 DDI를 불러 오류 보고 | UMD 추적 `runtime-set-error` 또는 `measure-d3d12-rejected` |

### 3.3 확실한 다음 장애물: D3D12 복사의 2 GiB 상한 [사실]

**결론: 2048에서 장치가 살아 있었더라도 첫 `CopyBufferRegion`에서 같은 0x887A0020으로 끝났습니다.**

- `AgxD3d12OwnerCopy`가 대상이나 원본 버퍼가 0x7fffffff 바이트보다 크면 E_INVALIDARG를 돌려줍니다(2773 `mesa/winsys/agx_d3d12_windows.cpp:1516`).
- 이 값은 재생 경로(`d3d12/agx_d3d12_ddi.cpp:7237`)를 거쳐 `pfnSetErrorCb(E_INVALIDARG)`로 런타임에 보고되고(`:7363-7364`), 메모리 부족이 아닌 오류라 장치가 제거됩니다.
- 상한이 있던 이유는 Mesa `pipe_box`의 x·width가 32비트 부호 정수이기 때문입니다(Mesa 9aa1215f `src/util/box.h:14-15`, VM에서 확인).
- Asahi의 버퍼 복사(`agx_blit.c` `agx_resource_copy_region`, 패치 없음)는 `src_box->x * bs`와 `bs * width`를 부호 없는 32비트로 계산합니다(VM의 Mesa 원본 618-650행). 그래서 2^31 이상 위치도 2의 보수로 넘기면 정확한 위치가 됩니다 [사실, 코드].

### 3.4 fill2 재설정: 구역을 넘는 예약 [사실 + 가설]

**결론: 예약 상한이 "구역 크기"라서 구역이 꽉 찬 뒤에도 만들기가 허락되고, 그 뒤처리를 VidMm이 하지 못합니다.**

- 예약 약속은 `MIN(예약 합계, 구역 크기)`까지만 실제 페이지로 받쳐 두고, 그 위의 예약은 페이지 없이 허락됩니다(`render_pool_slab.c:22-25`, `:105-135`; `pool_windows.c:1764-1765`) [사실].
- GPU 전용 버퍼는 구역 3에만 놓일 수 있고 내보낼 구역도 지정되지 않습니다(`allocation_windows.c:621-633`, `:652`) [사실]. 구역이 차면 VidMm은 다른 버퍼를 시스템 메모리로 내보내야 합니다. 그 4 KiB 페이지들은 대부분 AGX 주소표(16 KiB 단위)에 올릴 수 없습니다(`allocation_windows.c:585-600`의 원본 주석 EXP1265·1267) [사실, 코드 주석].
- 16번째 힙에서 사용량이 늘지 않은 것은 이미 하나가 내보내졌다는 뜻으로 봅니다. 시험 전에 다른 프로세스가 구역 3에 103 MiB를 쓰고 있었기 때문입니다 [가설]. 그 내보내기·재진입 중 어디서 재설정으로 갔는지는 기록이 없습니다 [가설].

---

## 4. 2774판 (P38)

### 4.1 바꾼 것 [빌드만]

| # | 파일 | 바뀐 것 | 풀이 없을 때 |
|---|---|---|---|
| U1 | `mesa/winsys/agx_d3d12_windows.cpp` `AgxD3d12OwnerCopy` | 상한 0x7fffffff → 0xffffffff. 복사를 1 GiB 조각으로 나누고 32비트 위치를 2의 보수로 `pipe_box.x`에 넘김 | 같음(작은 버퍼는 조각 1개 = 2773과 같은 호출) |
| U2 | `mesa/winsys/agx_win32_asahi_bo.c` (`agx_bo_create`) | GPU 주소 연결 실패가 깨끗이 되돌려졌으면 백엔드를 고장 처리하지 않고 이번 만들기만 실패(E_OUTOFMEMORY) | 같음 |
| K1 | `render-admission/src/pool_windows.c`, `include/render_pool_demand.h` | 예약 합계가 "구역 크기 − 여유"를 넘는 생성을 거절(새 사유 6 SEGMENT). 여유 기본 256 MiB, `Wom1PoolAdmitMarginMiB`(DWORD, MiB)로 바꿈, 0이면 끔. 영수증 `Wom1M2PoolDemand` v16(뒤에 `AdmitGranules@752`, `SegmentRefusals@760` 추가, 앞부분 위치 그대로) | 풀이 없으면 예약 자체를 안 함 → 변화 없음 |
| K2 | `render-admission/src/allocation_windows.c` | 일반 버퍼 최대를 4 GiB − 64 KiB로 제한(채우기·복사 페이징이 4 GiB 이상을 거절하고, 그 거절은 버그체크 코드라서) | 풀이 없으면 기존(952 MiB) |
| K3 | `paging_windows.c`, `gpuva_g3_paging_windows.c`, `receipts.c`, `render_paging.h`, `render_admission.h` | 새 영수증 `Wom1PagingExecFailure`(112 B, 즉시 디스크에 씀): 실패한 페이징 기록(상태·종류·세그먼트·IPA·VA·크기·펜스)과 주소표 갱신 실패 오염 횟수·상태 | 기록만 추가 |

- 차이: `P38-driver.diff`(10개 파일). 빌드: `build-p38.sh`(실행 이름 j274g4n), 판 30.0.2774.0.

### 4.2 빌드 결과 [빌드만]

**결론: VM 빌드가 끝까지 성공했고(7단계 모두 rc=0), 경고 수는 2773과 같은 98개라 바꾼 파일에서 새 경고는 없습니다.** 맥미니에서는 돌리지 않았습니다.

- 결과물: `out/j274-full-20261119-0754-p38`(VM `/Volumes/media/nwoas/out/`에서 복사), 소스 커밋 `b600a8a6`, INF `DriverVer = 11/19/2026,30.0.2774.0`.
- `.sys` 안에 `Wom1PagingExecFailure`·`Wom1PoolAdmitMarginMiB` 문자열이 들어간 것을 확인했습니다 [사실].

| 파일 | 바이트 | sha256 |
|---|---|---|
| AppleAgxRenderAdmission.sys | 652520 | `ef926ebf2226d0e63ca8dcd2a0d5f3a7ba43fc19ce471fceff79b95a709bcbe5` |
| AppleAgxRenderAdmissionUmd.dll (ARM64X 전달) | 14848 | `d8f5b7006c32aca88c97a14c8490f099f80f09913cb7a387b33547c8c2d6b923` |
| AppleAgxRenderAdmissionUmdArm64.dll | 13522944 | `0e076329fcaf39bf450caf608fdd71e03c5a9d6564d2dc58fc542acf740d6819` |
| AppleAgxRenderAdmissionUmdX64.dll | 14092288 | `3bd02d9c7da3116bdf14d089c4138da1d21e76869bde16ae98edcf95ff58c43b` |
| AppleAgxRenderAdmission.arm64x.inf | 4290 | `e884378040af056b28e5bf4341da02169776fa503ba757fdeb3c2db20aa3e9b5` |
| appleagxrenderadmission.cat | 4112 | `813316feab6a0f1254c986b4c3646d38f02075c877fbd03032e372812a1e81d6` |

- 전달 목록 `file-delivery-s243/manifests/agx-j274-2774.json`(패키지 6개 + probe12 2개)과 전달 작업 `jobs/agx-j274-2774-pkg.cmd`(→ `C:\NWOAS\drop\agx-j274-2774`), `jobs/gpucov14-j274-2774.cmd`(→ `C:\NWOAS\gpucov`)는 `make-2774.py`로 만들어 두었습니다.

### 4.3 시험 도구 [빌드만, VM 자기 점검만]

| 파일 | 내용 |
|---|---|
| `nwoas_scripts/gpucov/probe/nwgpuprobe_mem2.inc` + `nwgpuprobe.cpp`, `build-probe12.cmd` | 새 시험 `memladder2`·`fill3`·`rcpt` (probe11 기능은 그대로) |
| `nwoas_scripts/gpucov/bin/nwgpuprobe12-arm64.exe` / `-x64.exe` | VM 빌드 288768 / 311808 B, sha256 `b62590cc…` / `c778a7f1…` |
| `nwoas_scripts/gpucov/jobs/K43c-pool-mem-p38.cmd` | 4.4장 시험 |
| `nwoas_scripts/agx-s248/jobs/I36-install-2774.cmd`, `R36-rollback-2773.cmd` | 설치 / 되돌리기 |
| `nwoas_scripts/file-delivery-s243/make-2774.py` | 전달 목록과 전달 작업 생성 |
| `nwoas_scripts/gpupool-s266/run-2774-chain.sh` | 연쇄(4.4장) |

| 시험 | 하는 일 | 무엇을 가려 주나 |
|---|---|---|
| `memladder2` | 크기마다(1792·2048·2304·2560·3072 MiB) 새 D3D12 장치. 만들기 시간, 만들기 직후와 0.5초 뒤 장치 제거 사유, 첫·마지막 64 MiB 조각만 먼저 쓰고 읽기, 그다음 전체 64 MiB 조각 쓰기·읽기·비교. 실패하면 1.5초 뒤 영수증 읽기 | 만들기 자체 / 위쪽 절반 복사 / 내용 중 어디서 깨지는지 |
| `fill3` | 256 MiB 힙을 만들 때마다 표지를 쓰고 바로 읽어 비교, 만들기 실패까지(최대 20개), 끝에 전부 다시 읽기, 영수증 | 몇 번째 힙부터 깨지는지, 거절이 깨끗한지 |
| `rcpt` | `Wom1M2PoolDemand`(커밋 실패·예약 실패 사유·SEGMENT 거절), `Wom1PagingExecFailure`, `Wom1G3PagingFailure`·`Wom1GpuReset`·`Wom1G3Poison`·`Wom1G3BrokerFailures`(앞 64바이트 16진), `Wom1DdiStatus` 실패 칸 | 3.2장 후보 표 |

- `memladder2`는 UMD 거절 추적을 켠 채 돌고(`APPLE_AGX_UMD_TRACE_FILE`, 거절만), K43c가 그 가운데 관련 줄 40개까지 보여 줍니다.
- VM(Parallels 장치) 자기 점검 [사실, VM — 맥미니 판단에는 쓰지 않음]: `memladder2 ladder=256,512` PASS(첫·마지막 조각 포함 불일치 0), `fill3 memcap=512` 표지 2/2·2/2, `rcpt`는 값이 없어 모두 `present=0`. K43c 판정식은 가짜 출력으로 VM에서 돌려 `=> PASS`가 나오는 것을 확인했습니다(임시 폴더는 지움).

### 4.4 맥미니 시험 (SSD 시험이 끝난 뒤, `nwoas_scripts`에서)

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
nohup bash gpupool-s266/run-2774-chain.sh <지금 실행기 .out> <지금 .link> > /tmp/claude-501/s266.txt 2>&1 &
# 맥미니가 D4 대기면: run-2774-chain.sh idle -
```

순서와 기다리는 줄:

| 단계 | 하는 일 | 기다리는 줄 |
|---|---|---|
| 1 | S265 실행기를 풀 끔(`NWOAS_S265_POOL=0`, 2773 설치 때와 같은 조건)으로 부팅, 2774 패키지 전달, D28 | `S265 BOOT DONE link=` |
| 2 | I36 설치(판이 30.0.2774.0이 아니면 멈춤), probe12 전달 | 작업 완료 |
| 3 | 재시작 → 풀 켬으로 부팅 → D28 → `K43c K41 K9 K10 K25b K21 K38g K38dg K41` | `S265 BOOT DONE link=` |
| 4 | 재시작(2774로 풀 창 종료 경로) → 풀 켬 → `K43b K41` | `S265 BOOT DONE link=` |
| 끝 | `S266 CHAIN DONE` | 결과 `gpupool-s266/runs/chain-2774-*.txt` |

### 4.5 통과 기준

| 단계 | 통과 | 실패로 보는 것 |
|---|---|---|
| I36 | `VC … 30.0.2774.0`, `DEV1 OK` | 판이 다름 → 연쇄가 멈춤, 6장 되돌리기 |
| 하이퍼바이저·D28 | S265와 같음(`pool=1`, EL2 예외 0) | `ABORT hypervisor/launcher gone` |
| K43c `memladder2` | 5개 모두 `result=PASS`, `largest_pass_mib=3072` | `removed_at_create`/`removed_500ms` ≠ 0, `stage=probe_*`, 불일치 > 0 |
| K43c `fill3` | `removed=0x00000000`, `tags_end=n/n`(n ≥ 12), 멈춤이 `stop_hr=0x8007000e`(깨끗한 메모리 부족), `rcpt pool_demand segment_refusals≥1` | 장치 제거·재설정, 표지 불일치 |
| K43c `d3d11mem` | 1024 MiB까지 PASS | 불일치·제거 |
| K43c VERDICT | `=> PASS` | `=> FAIL`이면 같은 기록의 `rcpt` 줄로 3.2장 후보 표를 대조 |
| 회귀 K9·K10·K25b·K21·K38g·K38dg·K41 | S265(2773)와 같음(K9 23/23, K10 13/13+4/4, K25b 17/0, K21 내용 있음, Edge 정상, DWM 0) | 하나라도 다름 |
| K43b(두 번째 재시작 뒤) | `pool_demand=1` | 0 |

- 예상 [계산]: 여유 256 MiB면 예약 상한은 3840 MiB입니다. 다른 프로세스 약 103 MiB와 표지용 버퍼를 빼면 `fill3`은 힙 14개(3584 MiB)쯤에서 깨끗하게 멈출 것입니다. `memladder2` 3072 MiB는 3072 + 128(복사용) + 103 = 3303 MiB로 상한 안입니다.

---

## 5. 위험

| # | 위험 | 드러나는 곳 | 대응 |
|---|---|---|---|
| 1 | 2048 장치 제거의 원인이 2774에서도 그대로 [가설, 가능성 높음 — 원인을 고친 것이 아님] | `memladder2 … removed_at_create≠0` 또는 `removed_500ms≠0` | 그 줄 바로 뒤 `rcpt` 값으로 원인을 정하고 다음 판에서 고침 |
| 2 | 예약 상한이 지금까지 되던 큰 작업(GravityMark 등)을 메모리 부족으로 거절 [가설] | 앱의 E_OUTOFMEMORY, `segment_refusals` | `Wom1PoolAdmitMarginMiB`=0(장치 키 DWORD)으로 끄고 장치 재시작 |
| 3 | 2^31 이상 위치를 2의 보수로 넘기는 것이 Asahi 버퍼 복사 말고 다른 경로(형식이 다른 복사, CPU 대체 경로)로 가면 틀린 위치 [가설: 버퍼↔버퍼는 항상 버퍼 경로라 해당 없다고 봄, `agx_blit.c` 623행] | `memladder2` 위쪽 절반 `probe_last_content` 불일치 | 2773으로 되돌리기 |
| 4 | 새 영수증 쓰기 자체(페이징 실패 때 레지스트리 즉시 쓰기)가 문제를 일으킴 [가설: 기존 `Wom1G3PagingFailure`와 같은 작업 항목 경로라 낮음] | K43c 중 멈춤 | 2773으로 되돌리기 |
| 5 | 설치는 풀 끔 부팅에서 하므로 2773 설치와 같지만, 2774가 풀과 함께 처음 시작하는 것은 3단계 D28 | `ABORT hypervisor/launcher gone during D28` | 콘치님 전원 껐다 켜기 후 6장 |

---

## 6. 되돌리기

**결론: 2773 패키지가 Windows의 `C:\NWOAS\drop\agx-j274-2773`에 그대로 있으니, 작업 하나와 재시작으로 2773에 돌아갑니다.**

| 상황 | 명령 |
|---|---|
| Windows가 켜져 있음 | `python3 -I transport-s123/queue_job.py <.link> agx-s248/jobs/R36-rollback-2773.cmd` → `VC … 30.0.2773.0` 확인 → `bash gpupool-s265/boot-s265.sh <.out> <.link>`(재시작) |
| 예약 상한만 끄기 | 장치 키 `HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters`에 `Wom1PoolAdmitMarginMiB`=0(DWORD) → D28 |
| 풀만 끄기 | `NWOAS_S265_POOL=0 bash gpupool-s265/boot-s265.sh <.out> <.link>` |
| 맥미니가 D4 대기 | `bash gpupool-s265/boot-s265.sh idle -` |

- R36은 2773 파일 6개의 sha256을 먼저 확인하고, 하나라도 다르면 아무것도 바꾸지 않고 멈춥니다. 2773과 2774는 INF 이름이 같아 둘 다 드라이버 저장소에서 지운 뒤 2773을 다시 넣습니다.
