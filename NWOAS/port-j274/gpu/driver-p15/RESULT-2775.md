# 2774 실기 결과(2304·2560 MiB 실패) 분석과 다음 시험 (2026-11-19)

> 표기: **[사실]** = 코드·로그·헤더로 확인. **[계산]** = 기록의 값으로 계산. **[가설]** = 확인하지 않은 추측. **[빌드만]** = 만들기만 하고 맥미니에서 돌리지 않음.
> 작성 중 맥미니에는 손대지 않았습니다(와이파이 시험 중). 빌드와 점검은 이 맥북의 Parallels VM에서만 했습니다.
> 기록: `nwoas_scripts/gpupool-s266/runs/chain-2774-20261119-080440.txt` 143~334행(K43c). 드라이버 소스 행 번호는 2774(P38) 소스 기준입니다.

---

## 1. 결론

**2775 드라이버는 만들지 않았습니다. 고칠 곳을 정할 근거가 아직 없기 때문입니다.** 커널 드라이버(KMD) 영수증에는 이번 실패와 관련된 기록이 하나도 없었고, 실패를 가릴 줄은 Windows 디스크의 UMD 추적 파일(`C:\NWOAS\gpucov\k43c-umd.log`, 5827줄)에 남아 있는데 K43c가 그 가운데 앞 40줄(시작 때의 정상 측정 줄)만 보여 줬습니다.

| # | 확정한 것 | 구분 |
|---|---|---|
| 1 | 2304·2560 MiB는 버퍼 만들기와 0.5초 뒤까지 정상이었고, 첫 64 MiB 조각을 오가는 두 번의 복사 중에 장치가 제거됐습니다(0x887A0020) | [사실] |
| 2 | 쓰는 양 때문이 아닙니다. 3072 MiB(사용량 3200 MiB)는 통과했고 2304 MiB(사용량 2432 MiB)는 실패했습니다 | [사실] |
| 3 | KMD 쪽 실패 기록은 없습니다: 페이징 실행 실패 0, 커밋 실패 0, 예약 거절 0, KMD 제출 거절 0. 덤프된 영수증 3개(페이징·GPU 재설정·중개 실패)는 이번 시험 전부터 있던 값이고, `ddi_status`의 0xc01e0001은 정상적인 "DMA 버퍼 부족 → 나눠서 다시" 응답입니다 | [사실] |
| 4 | 그래서 오류는 UMD가 런타임에 보고한 것으로 봅니다(제출 직전 검사·상주 처리 중 하나) | [가설, 높음] |

- **다음 시험은 드라이버 설치 없이** 2774 그대로 두고 ① 이미 있는 UMD 추적에서 실패 줄을 뽑는 K43t, ② 순서를 바꿔 "크기 탓인지, 앞 시험의 흔적 탓인지"를 가르는 K43d를 돌립니다(4장). 두 결과로 고칠 곳이 정해지면 그때 2775를 만듭니다.
- 새로 만든 것은 시험 프로그램 probe13과 작업 파일뿐입니다 [빌드만]. 드라이버·하이퍼바이저·실행기·out/ 결과물은 건드리지 않았습니다.

---

## 2. 2774 K43c에서 일어난 일 [사실]

### 2.1 크기별 결과

**결론: 실패는 크기 순서의 가운데(2304·2560)에만 있었고, 둘 다 같은 자리(첫 조각 복사 뒤 READBACK Map)에서 같은 사유로 끝났습니다.**

| 크기 (MiB) | 만들기 (ms) | 만들기 1 GiB당 (ms) | 만들기 직후 / 0.5초 뒤 제거 | 끝난 단계 | 로컬 사용량 (MiB) | 제거 사유 | 결과 |
|---|---|---|---|---|---|---|---|
| 1792 | 3297 | 1884 | 0 / 0 | 끝까지 | 1920 | 없음 | PASS |
| 2048 | 4672 | 2336 | 0 / 0 | 끝까지 | 2176 | 없음 | PASS |
| 2304 | 5718 | 2541 | 0 / 0 | probe_readback_map | 2432 | 0x887A0020 | FAIL |
| 2560 | 5391 | 2156 | 0 / 0 | probe_readback_map | 2688 | 0x887A0020 | FAIL |
| 3072 | 4578 | 1526 | 0 / 0 | 끝까지 | 3200 | 없음 | PASS |

- 1 GiB당 시간은 [계산]입니다. 로컬 사용량은 모든 크기에서 "크기 + 128 MiB(64 MiB UPLOAD + 64 MiB READBACK)"와 정확히 같습니다. 즉 0.5초 뒤 측정 시점에 이 프로세스에는 앞 크기의 버퍼가 남아 있지 않았습니다 [사실: DXGI 프로세스 사용량]. VidMm 안쪽의 지연 해제나 구역 안 배치까지 없다는 뜻은 아닙니다 [가설].
- 그 밖: `fill3` 256 MiB 힙 14개(3584 MiB) 표지 14/14, 깨끗한 E_OUTOFMEMORY 멈춤, `d3d11mem` 1024 MiB까지 PASS, 회귀 모두 PASS(사용자 보고).

### 2.2 실패 순간의 순서 (`gpucov/probe/nwgpuprobe_mem2.inc`, 2774판 행 번호)

| 순서 | 하는 일 | 2304·2560에서 |
|---|---|---|
| 1 | 64 MiB UPLOAD·READBACK 만들기 (`:95-98`) | 성공 |
| 2 | 큰 DEFAULT 버퍼 만들기, 직후·0.5초 뒤 제거 사유 (`:107-113`) | 성공, 둘 다 0 |
| 3 | UPLOAD Map → 첫 조각 무늬 쓰기 (`:126-128`) | 성공(실패했으면 `probe_first_map`) |
| 4 | 복사 UPLOAD→큰 버퍼, 복사 큰 버퍼→READBACK (`:129-131`) | 둘 다 "성공"으로 돌아옴(실패했으면 `probe_first_copy`) |
| 5 | READBACK Map (`:134`) | **실패, 이미 0x887A0020** |

- 4번이 "성공"이어도 장치가 살아 있다는 뜻은 아닙니다. 기다리는 함수는 펜스만 보고 제거 사유를 보지 않습니다(`nwgpuprobe.cpp:133-145`). 장치가 제거되면 펜스가 끝난 것으로 바뀌어 기다림이 그냥 끝납니다. 그래서 제거는 **4번의 두 복사 중 하나에서** 일어났습니다 [사실, 코드]. 둘 중 어느 쪽인지는 이번 기록으로 알 수 없습니다 → probe13이 복사마다 사유를 묻습니다.
- 0x887A0020(DRIVER_INTERNAL_ERROR)은 UMD가 `pfnSetErrorCb`로 메모리 부족·장치 제거가 아닌 오류를 넘기면 런타임이 쓰는 값입니다 [가설: D3D10/11 문서의 규칙이 D3D12에도 같다고 봄]. D3D12 실행 경로에서는 복사·제출이 실패하면 그 HRESULT를 그대로 넘깁니다(`d3d12/agx_d3d12_ddi.cpp:7356-7364`). 제출이 실패하면 UMD의 GPU 주소 실패 원인(`Runtime.GpuvaFailure`)이나 `DXGI_ERROR_DEVICE_HUNG`이 넘어갑니다(`mesa/winsys/agx_d3d12_windows.cpp:311-316`, `:5604-5605`). 어느 값이든 사유는 0x887A0020으로 같아져서, 사유만으로는 경로를 가릴 수 없습니다.

---

## 3. 영수증 풀이

### 3.1 한눈에

**결론: 실패 직후 읽은 KMD 영수증 가운데 이번 실패를 가리키는 것은 없습니다.**

| 영수증 | 값 | 뜻 | 이번 실패와 관계 |
|---|---|---|---|
| `Wom1M2PoolDemand` v16 (2304 직후) | committed 2316 MiB, peak 4074 MiB, commit_failures 0, reserve_failures 0, reserved 90 MiB, admit 3840 MiB, segment_refusals 0 | 구역 3 커밋·예약 실패 없음 | 없음 [사실] |
| `Wom1PagingExecFailure` (P38 새 영수증) | 없음(present=0) | CPU 페이징 실행 실패·주소표 갱신 실패 오염 0건 | 없음 [사실] |
| `Wom1G3PagingFailure` | 3.2장 | 실패가 아닌 "표 초기화" 기록(상태 0) | 없음 [사실] |
| `Wom1GpuReset` | 3.3장 | 어댑터 재설정 1번, 성공 | 이번 시험 것이 아님 [가설, 높음] |
| `Wom1G3BrokerFailures` | 3.4장 | 중개 첫 명령 1건 실패(STALE) | 시작 때 것 [가설, 높음] |
| `Wom1DdiStatus` | 3.5장 | 정상 재시도·미지원 질의뿐 | 없음 [사실] |

- 페이징·재설정·중개 세 영수증은 2304 직후, 2560 직후, fill3 끝 세 번 모두 바이트가 같았습니다 [사실]. 이 값들은 장치 레지스트리 키(`...\APPL0002\0\Device Parameters`)에 있어 재부팅·재설치 뒤에도 남습니다. K43c는 시험 전에 이것들을 읽지 않았으므로 언제 쓰였는지는 모릅니다.

### 3.2 `Wom1G3PagingFailure` (104 B, `render_admission.h:1390-1397`)

| 칸 | 값 | 뜻 |
|---|---|---|
| Version / Bytes | 3 / 104 | |
| Branch | 8 | `AdmissionG3PagingTableInitialized` (`gpuva_g3_paging_windows.c:19`, 기록 자리 `:2228`) |
| Level / Index | 0 / 0xffffffff | |
| Status | 0 | 성공 |
| UpdateMode | 2 | |
| TableAddress | 0x10120000 | |
| TableIpa | 0xa10120000 | |

- 분기 8은 실패가 아니라 "처음 연결되기 전에 채워진 표" 기록이고 상태도 0입니다 [사실]. 이름과 달리 실패 기록이 아닙니다.

### 3.3 `Wom1GpuReset` v4 (792 B, `render_gpu_fault.h:234-252`)

| 칸 | 값 | 뜻 |
|---|---|---|
| Resets / Succeeded | 1 / 1 | 재설정 1번, 성공 |
| Entry | 2 | 어댑터 재설정(`DxgkDdiResetFromTimeout`, `render_gpu_fault.h:105`) |
| Status | 0 | 성공 |
| SchedulerFaulted / PacketState / ActiveFence | 0 / 0 / 0 | 돌던 작업 없음 |
| CompletedFence / LastSubmittedFence / LastAbortedFence | 0xff6 / 0xff6 / 0xff6 | 4086번 펜스까지 끝난 상태 |
| Quiesce / WaitMs | 0 / 0 | 기다린 작업 없음 |

- K43c 끝의 시스템 이벤트 조회(디스플레이·dxgkrnl, 4101 = GPU 응답 없음 복구, 20분 범위)는 0건이었습니다 [사실]. 그래서 이 재설정은 이번 시험 때 것이 아니라고 봅니다 [가설, 높음]. 시각 칸(EntryMs, 96바이트 위치)은 출력한 앞 64바이트 밖이라 확인하지 못했습니다.

### 3.4 `Wom1G3BrokerFailures` (528 B, `render_admission.h:1401-1408`, 고리 칸 `shared/include/apple_agx_gpuva_broker_v5_client.h:20-23`)

| 칸 | 값 | 뜻 |
|---|---|---|
| FailureCount | 1 | 이 KMD가 시작한 뒤 중개 실패 1건 |
| Ring[0].Command | 1 | CREATE (`apple_agx_gpuva_broker_v5.h:35`) |
| Ring[0].Status | 2 | STALE (`m1n1-gpu-hv2/src/hv_agx_gpuva_v5.h:28-37`) |
| Ring[0].Sequence / ProcessId / Answered | 1 / 0 / 1 | 첫 번째 명령, 프로세스 0 |

- 첫 번째 명령(Sequence 1) 하나뿐이라 시작 때의 것으로 봅니다 [가설, 높음]. 이번 시험 중 중개 실패가 있었다면 FailureCount가 늘고 다시 쓰였을 것입니다(`receipts.c:2243-2245`) [사실, 코드].

### 3.5 `Wom1DdiStatus` 칸 (`render_ddi_status.h:4-8`, 칸 번호 `render_ddi_status_wrappers.h`)

**결론: 실패 칸은 모두 정상 동작에서 나오는 값입니다. 0xc01e0001은 "독점 모드 소유자 아님"이 아니라 "DMA 버퍼 부족"입니다.**

| 칸 | 대상 | 작업 | 상태 | 이름 (WDK 10.0.26100 `ntstatus.h`, VM에서 확인) | 횟수 (2304 직후 → fill3 끝) | 뜻 |
|---|---|---|---|---|---|---|
| 7 | QueryDeviceDescriptor | — | 0xC01E0401 | STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED | 2 → 2 | 화면 장치 기술자 없음, 정상 |
| 9 | QueryAdapterInfo | — | 0xC00000BB | STATUS_NOT_SUPPORTED | 63 → 77 | 지원 안 하는 질의 종류, 정상 |
| 28 | QueryInterface | — | 0xC00000BB | STATUS_NOT_SUPPORTED | 22 → 22 | 정상 |
| 31 | CreateAllocation | — | 0xC0000017 | STATUS_NO_MEMORY | 없음 → 1 | fill3의 15번째 힙 거절(의도한 상한) |
| 49 | Escape | — | 0xC000009A | STATUS_INSUFFICIENT_RESOURCES | 없음 → 4 | fill3 때 생김, memladder2와 무관 |
| 39 | BuildPagingBuffer (전체) | 9 | 0xC01E0001 | STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER | 968 → 1559 | 아래 |
| 82 | BuildPagingBuffer 작업별 | 2 DISCARD_CONTENT | 0xC01E0001 | 〃 | 1 → 2 | 〃 |
| 88 / 120 | 작업별 / 내부 | 8 VIRTUAL_TRANSFER | 0xC01E0001 | 〃 | 927 → 1477 | 〃 |
| 89 / 121 | 작업별 / 내부 | 9 VIRTUAL_FILL | 0xC01E0001 | 〃 | 40 → 80 | 〃 |

- 작업 번호는 WDK `d3dkmddi.h`의 `DXGK_BUILDPAGINGBUFFER_OPERATION`(8 = VIRTUAL_TRANSFER, 9 = VIRTUAL_FILL, 2 = DISCARD_CONTENT)과 같습니다 [사실, VM 헤더].
- 0xC01E0001은 KMD가 페이징 기록을 DMA 버퍼에 다 못 담았을 때 "나머지는 다음 버퍼에" 하고 돌려주는 값입니다(`gpuva_g3_paging_windows.c:1269-1273`, `:1386-1388`). VidMm이 허용하는 정상 응답이고(`:1594-1613`), 통과한 fill3 동안에도 늘었습니다 [사실].
- **주의: 이 영수증의 횟수는 늘 최신이 아닙니다.** KMD는 어떤 칸에 첫 실패가 생길 때만 레지스트리 값을 다시 씁니다(`render_ddi_status_wrappers.h:213-215`, `receipts.c:3216-3256`). 2304 직후와 2560 직후의 횟수가 똑같았던 것은 그 사이에 새 칸이 없어서 다시 쓰이지 않았기 때문입니다 [사실, 코드]. 대신 KMD가 제출을 거절했다면(칸 63 SubmitCommandVirtual, 38 SubmitCommand) 새 칸이 생겨 반드시 다시 쓰였을 텐데 그런 칸은 없습니다 → **KMD는 제출을 거절하지 않았습니다** [사실].

---

## 4. 남은 후보와 가르는 시험

### 4.1 후보 [모두 가설]

| 순위 | 후보 | 근거 | 맞으면 보이는 것 |
|---|---|---|---|
| 1 | 64 MiB UPLOAD·READBACK(CPU에서 보이는 버퍼)이 큰 버퍼를 만드는 동안 구역 3 밖(4 KiB 시스템 페이지)으로 밀려나, UMD 제출 직전 검사가 "GPU 주소표에 없음"으로 거절. 되돌리기(rehome)도 실패 | KMD 주석: 구역 3이 모자라면 CPU에서 보이는 버퍼가 4 KiB 페이지에 놓이고 그 제출은 거절됨(`allocation_windows.c:585-600`). UMD에 바로 이 경우를 위한 되돌리기 경로와 거절 줄이 있음(`umd_gpuva_windows.c:2105-2220`, `:2553-2595`) | `reject-native-allocation` 값의 크기 칸 = 0x04000000(64 MiB), 사유 3·4. `measure-cpu-pool-rehome` hr≠0 |
| 2 | 큰 버퍼 자체가 일부 주소표에 안 올라감 | 같은 검사 | `reject-native-allocation` 크기 칸 = 0x90000000 / 0xA0000000 |
| 3 | VidMm 상주 요청 거절(예산) | `umd_gpuva_windows.c:420-475` | `reject-make-resident hr=0x8007000e` |
| 4 | GPU 작업 자체가 실패(펌웨어 오류) | 이 경우 UMD는 DEVICE_HUNG을 넘김 | 거절 줄 없이 `measure-d3d12-owner-submit` hr=0x887a0006, 값 셋째 칸(faulted)=1 |

- 2.1장의 "3072는 되고 2304는 안 됨"은 총량으로 설명되지 않습니다. 남는 설명은 (가) 크기마다 구역 3 안 배치가 달라지는 것 또는 (나) 바로 앞 크기 시험의 흔적(VidMm 지연 해제, KMD 커밋 캐시 — 2304 직후 committed 2316, peak 4074)입니다 [가설]. K43d가 이 둘을 가릅니다.

### 4.2 만든 것 [빌드만]

| 파일 | 내용 | sha256 앞 8자리 |
|---|---|---|
| `nwoas_scripts/gpucov/probe/nwgpuprobe_mem2.inc`, `nwgpuprobe.cpp` | probe13 = probe12 + ① 크기마다 `pre` 줄(이 프로세스 로컬 사용량, 풀 예약·커밋, 앞 장치를 닫은 뒤 지난 ms) ② 두 복사 각각 뒤의 제거 사유(`probe_first_write_removed` / `probe_first_read_removed`) ③ `settle=<ms>`: 크기마다 사용량이 처음 값 + 16 MiB로 돌아올 때까지 기다림 ④ `rcpt`에 `Wom1G4SubmitFailure`·`Wom1G4OmittedLeaves`·`Wom1G3UnpublishedGroups`·`Wom1G3NativePteRefusal` 추가, `ddi_status`에 기록 나이(ms) | — |
| `gpucov/probe/build-probe13.cmd` | VM 빌드(MSVC, arm64·x64 모두 rc=0, 경고 없음) | — |
| `gpucov/bin/nwgpuprobe13-arm64.exe` / `-x64.exe` | 290304 / 313856 B | `43ed4e40` / `93f65771` |
| `gpucov/jobs/K43t-umdtrace-2774.cmd` | 읽기만: 08:10 K43c의 UMD 추적에서 실패·거절 줄(파일 순서, 최대 80줄), 단계별 줄 수, 큰 힙 줄(GPU 주소), 그다음 probe13 `rcpt` | `525927a2` |
| `gpucov/jobs/K43d-ladder-order-p13.cmd` | (a) 새 프로세스에서 2304 하나 (b) 2048 → 2304 바로 이어서(K43c 순서) (c) `settle=15000`으로 1792~3072. 실행마다 UMD 추적 파일을 따로 두고 실패 줄을 뽑음. 끝에 `K43d VERDICT` | `c0b3f792` |
| `file-delivery-s243/make-probe13.py` → `manifests/gpucov-probe13.json`, `jobs/gpucov15-probe13.cmd` | probe13 두 파일을 `C:\NWOAS\gpucov`로 | job `54f404d1` |
| `nwoas_scripts/gpupool-s267/run-k43d-chain.sh` | 풀 켬 부팅(부팅 중 probe13 전달) → D28 → `K43t K43d K41`. 부팅은 `S265 BOOT DONE link=` 줄로만 기다림. 설치된 판이 30.0.2774.0이 아니면 멈춤 | `bdda80b2` |

- VM 점검 [사실, VM — 맥미니 판단에는 쓰지 않음]: probe13 `memladder2 ladder=256,512 settle=3000 memcap=512 rcpt` PASS(불일치 0, `pre` 줄·새 영수증 줄 출력 확인). K43t·K43d의 뽑기·판정식은 가짜 기록으로 돌려 `HISTORY`·`SIZE` 두 경우가 맞게 나오는 것을 확인했습니다(첫 판정 함수 이름 `R`이 PowerShell 별칭과 겹쳐 고침). 점검용 임시 폴더는 지웠습니다.

### 4.3 맥미니 명령 (와이파이 시험이 끝난 뒤, `nwoas_scripts`에서)

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
nohup bash gpupool-s267/run-k43d-chain.sh <지금 실행기 .out> <지금 .link> > /tmp/claude-501/s267.txt 2>&1 &
# 맥미니가 D4 대기면: run-k43d-chain.sh idle -
```

- 더 가볍게: 2774로 Windows가 이미 켜져 있으면 재시작 없이 `python3 -I transport-s123/queue_job.py <.link> gpucov/jobs/K43t-umdtrace-2774.cmd`만 돌려도 1단계 답(어느 버퍼가 왜 거절됐는지)이 나옵니다. 이때 `rcpt` 부분은 probe13이 없어서 "probe13 missing"으로 넘어갑니다.

| 단계 | 하는 일 | 기다리는 줄 |
|---|---|---|
| 1 | W4 재시작 → S265 실행기(풀 켬) → probe13 전달 → D28 | `S265 BOOT DONE link=` |
| 2 | 60초 쉼 → K43t → K43d → K41 | 작업 완료 |
| 끝 | `S267 CHAIN DONE` | 결과 `gpupool-s267/runs/chain-k43d-*.txt` |

### 4.4 통과 기준과 볼 것

| 단계 | 통과 | 실패로 보는 것 |
|---|---|---|
| 부팅 | `pool armed=1`, `S243 DELIVER COMPLETE`, D28 뒤 EL2 예외 0 | `POOL NOT ARMED`, `ABORT ...` |
| K43t | `K43t VC ... 30.0.2774.0`, `K43t FILE bytes=…`(08:1x에 쓰임), `UMDTRACE lines=5827` | `FILE missing`(지워졌으면 K43d 결과로만 판단) |
| K43d | `K43d VERDICT` 줄이 나옴 | 줄이 없음(시험 프로그램이 멈춤) |
| K41 | `dwm_events=0` | DWM 재시작 |

`K43d VERDICT … => X`의 뜻:

| X | 조건 | 뜻 | 다음 |
|---|---|---|---|
| SIZE | (a) 2304 단독도 FAIL | 크기 자체의 문제 | 같은 실행의 `UTFAIL` 줄로 4.1장 후보를 정해 2775에서 고침 |
| HISTORY | (a) PASS, (b) 2304 FAIL, (c) 모두 PASS | 바로 앞 큰 버퍼의 흔적 때문 | (b)의 `pre` 줄(앞 장치를 닫은 뒤 ms, 커밋 MiB)과 `UTFAIL`로 KMD 커밋 캐시·VidMm 지연 해제 중 어느 쪽인지 정함 |
| NOREPRO | (a)(b)(c) 모두 PASS | 재현 안 됨 | K43c 그대로 한 번 더 |
| MIXED | 그 밖 | 둘 다 아님 | `UTFAIL`·`pre`·`rcpt` 줄 전체를 대조 |

`UTFAIL` 줄에서 볼 것(값 순서는 `umd_gpuva_windows.c:2586-2591`):

| 줄 | 값 | 뜻 |
|---|---|---|
| `reject-native-allocation hr=…` | {할당, GPU 주소 하위·상위, 크기 하위·상위, 쓰기, 사유, 실패 주소 하위·상위}. 사유 1 표 루트 없음, 2 표 없음, 3 잎 없음, 4 잎 미공개, 5 끝 부족 (`shared/include/apple_agx_g3_copy_query_receipt.h:17-23`) | 크기 0x04000000이면 후보 1, 0x90000000·0xA0000000이면 후보 2 |
| `measure-cpu-pool-rehome hr=…` | {방식, 회차, 사유, 풀 쪽 페이지, 풀 밖 페이지, …} | 풀 밖 페이지 > 0이면 후보 1 확정 |
| `reject-make-resident hr=0x8007000e` | 상주 요청 수, 회차, … | 후보 3 |
| `measure-d3d12-owner-submit` / `measure-d3d12-execute` | hr = 런타임에 넘긴 값 | 0x887a0006이고 거절 줄이 없으면 후보 4 |

---

## 5. 위험

| # | 위험 | 대응 |
|---|---|---|
| 1 | K43t가 읽을 `k43c-umd.log`가 그 뒤 지워졌을 수 있음(K43c 다시 실행 시에만 지워짐) | K43d가 같은 정보를 새로 남김 |
| 2 | K43d (b)·(c)가 2304에서 다시 장치를 제거함 | 시험 프로세스의 장치만 제거됨(K43c와 같음). 바탕화면은 K41로 확인 |
| 3 | `settle=15000`이어도 사용량이 안 돌아오면 그대로 진행 | `pre` 줄의 `settle_ms=15000`과 사용량으로 드러남 |
| 4 | `ddi_status` 기록 나이(ms)는 부팅이 바뀌면 의미 없음(-1 또는 큰 값) | 같은 부팅 안의 값만 봄 |

---

## 6. 되돌리기

**결론: 이번에는 아무것도 설치하지 않으므로 드라이버를 되돌릴 일이 없습니다.** probe13 두 파일은 `C:\NWOAS\gpucov`에 남아도 다른 시험에 영향이 없습니다.

| 상황 | 명령 |
|---|---|
| 2773으로 돌아가려면(필요할 때만) | `python3 -I transport-s123/queue_job.py <.link> agx-s248/jobs/R36-rollback-2773.cmd` → `VC … 30.0.2773.0` 확인 → `bash gpupool-s265/boot-s265.sh <.out> <.link>` |
| 풀만 끄기 | `NWOAS_S265_POOL=0 bash gpupool-s265/boot-s265.sh <.out> <.link>` |
| 연쇄가 멈추고 맥미니가 D4 대기 | `bash gpupool-s265/boot-s265.sh idle -` |
