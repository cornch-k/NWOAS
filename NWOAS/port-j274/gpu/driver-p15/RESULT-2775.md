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

---

## 7. 정정: K43d 연쇄는 돌지 않습니다 → K43u·K43e로 바꿈 (2026-11-19 저녁)

**결론: `run-k43d-chain.sh`는 실행하지 마십시오(첫 줄에서 멈추게 막아 둠).** 실기에서 돌리기 전에 찾은 문제 3가지와 바꾼 것입니다. 모두 [빌드만]입니다. VM 점검은 아래 표의 'VM 점검' 칸대로만 했고(가짜 기록, 경로만 C:\nwtest로 바꾼 사본), 맥미니 판단에는 쓰지 않습니다. `run-k43d-chain.sh`는 막는 줄을 넣어 4.2장의 sha256(`bdda80b2`)과 달라졌습니다.

| # | 문제 | 근거 | 바꾼 것 |
|---|---|---|---|
| 1 | K43d 작업 파일이 5,990 B라 작업 통로 한도 4,032 B를 넘음 → 연쇄가 "ABORT queue K43d"로 멈춤 | `transport-s123/queue_job.py:11` [사실] | 실행 1개 + 뽑기 2개 + 판정 1개(`K43e1`~`K43e4`, 각각 1,940~3,532 B)로 나눔. 연쇄가 시작 전에 모든 작업 크기를 검사 |
| 2 | probe13 `settle=`은 이 프로세스의 DXGI 사용량을 기다리는데, K43c에서 그 값은 크기마다 이미 "크기+128 MiB"로 돌아와 있었음 → (c)는 K43c 반복이 되어 HISTORY 판정이 나올 수 없음 | `nwgpuprobe_mem2.inc` 133-141행, K43c 기록 [사실] | (c)를 크기마다 새 프로세스로, 20초 간격(KMD 해제 지연 2초 `pool_windows.c:209`보다 충분히 김). (d) 3072→2304 한 프로세스 추가 |
| 3 | K43t의 실패 줄 뽑기가 파일 앞 80줄만 보여 1792·2048 때의 정상 측정 줄에 밀릴 수 있음 | K43t 필터 [사실] | `K43u`: 실패 지점(reject-·runtime-set-error·실패한 제출) 앞 16줄·뒤 10줄, 실패 단계별 개수 |
| 4 | NWGET243은 덮어쓰기를 거절 → 같은 연쇄를 두 번 돌리면 probe13 전달에서 멈춤 | `file-delivery-s243/client/nwget243.c:232` [사실] | `gpucov15s-probe13-ifmissing.cmd`: 이미 있으면 건너뛰고 SHA-256만 대조 |

| 파일 | sha256 | VM 점검 |
|---|---|---|
| `nwoas_scripts/gpucov/jobs/K43u-umdtrace-anchor-2774.cmd` | `bdc08c2ccb1b30ca6b4322cc0afe519342bbab05d8f1fc4a08f88035acdf55a7` | 이 판 그대로, 가짜 추적 302줄 |
| `nwoas_scripts/gpucov/jobs/K43e1-ladder-runs-p13.cmd` | `f1043e7b7df6d0d71126cb0b26e2cfccd17abc2912e8b308275a08653c2e40c2` | 순서·대기·출력 파일만(시험 프로그램 대신 가짜 배치 파일, 실제 probe13 호출은 안 돌림) |
| `nwoas_scripts/gpucov/jobs/K43e2-extract-ab-p13.cmd` | `1ed8077e95468f6bf84bdfc57ad527260e1a0ea2865658daaa58141b4870c94f` | 이 판 그대로 |
| `nwoas_scripts/gpucov/jobs/K43e3-extract-cd-p13.cmd` | `7cd5a65814b48c0c091473582bcdf581a493dc1662cda99c344dc7046664358d` | 이 판 그대로 |
| `nwoas_scripts/gpucov/jobs/K43e4-verdict-p13.cmd` | `ba0f5f2bb3430b6ce6170e1aff6aab65a827b62d131ea73db373a48556857bb4` | 이 판 그대로(HISTORY·SIZE 두 경우) |
| `nwoas_scripts/file-delivery-s243/jobs/gpucov15s-probe13-ifmissing.cmd` | `9f9705f422edaff5f040ff441ecce49a09cbb16ca70ef6108d11e734a93a8eba` | 건너뛰기 경로만(가짜 파일·가짜 해시). NWGET243로 받는 경로는 안 돌림 |
| `nwoas_scripts/gpupool-s267/run-k43e-chain.sh` | `ea7a2985e055df1d6d96c3f940b51342786bd4647fc0e38ffa8af62b51999010` | `bash -n`, 시작 전 크기 검사만. 부팅·작업 경로는 안 돌림 |
| `nwoas_scripts/gpucov/jobs/gen-k43e-jobs.py` | `73366f68906276e4e640207062e679c060fb9917bf07e911f4017e6439b1f7e5` | K43e2~4를 만드는 스크립트(고칠 때는 이것을 고쳐 다시 만듦) |

- S265 실행기의 지문 검사(HV g254L, S259 페이로드, wifi_module.py 등 모든 `shasum`·`md5`·`stat` 줄)를 지금 호스트 파일로 평가해 모두 맞음 [사실, 호스트 파일].

- 순서: ① 지금 켜진 Windows(S268, 풀 꺼짐)에 `K43u`만 넣기(읽기만, 재시작 없음) → 실패한 버퍼와 사유가 나오면 그것으로 2775를 만듦. ② 그래도 모자라면 `bash gpupool-s267/run-k43e-chain.sh <.out> <.link>`(W4 재시작 → S265 풀 켬, g254L, 블루투스 없음 → K43u·K43e1~4·K41, 약 10분). 끝나면 `bt-s268/run-s268.sh`로 평소 실행기로 돌아감.
- K43e4 판정: SIZE = (a)와 (c)의 2304가 둘 다 새 프로세스에서 실패, HISTORY = 새 프로세스 2304는 통과하고 (b)나 (d)의 2304만 실패, NOREPRO = 모두 통과, MIXED = 그 밖.

---

## 8. K43u 결과 풀이 (2026-11-19 20:50, 지휘자 실행, 기록 `nwoas_scripts/gpupool-s267/runs/K43u-20261119-2050.txt`)

**결론: 2304·2560 실패는 "64 MiB 올리기(UPLOAD) 버퍼가 처음 제출 전에 이미 통째로 풀 밖(VidMm 시스템 페이지)에 있었고, 되살리기를 두 번 해도 VidMm이 다시 풀 밖에 놓아 UMD 제출 전 검사가 거절 → 장치 제거"입니다 [사실]. VidMm이 왜 이 두 크기에서만 풀 대신 시스템 페이지를 골랐는지는 아직 모릅니다 [가설]. 그래서 2775는 만들지 않았습니다.**

| 줄 | 값 | 뜻 (근거) |
|---|---|---|
| `reject-native-allocation` | 할당 0x40001b80, GPU 주소 0x11_001f0000, 크기 0x04000000, 쓰기 1, 사유 3, 실패 주소 = 시작 주소 | 64 MiB 버퍼의 첫 페이지부터 잎 없음(`umd_gpuva_windows.c:2586-2591`, 사유 3 = 잎 없음). 주소 0x11001f…는 같은 크기 실행의 첫 64 MiB 힙(`measure-d3d12-heap … 0011001f`) = UPLOAD. 2560도 같음(0x1100270000 = `00110027`) |
| `measure-cpu-pool-rehome` 1회차 | 방식 0(제출), 회차 0, 사유 0x102, 풀 페이지 0, 풀 밖 페이지 0x4000, 비운 슬롯 1, 비운 MiB 0, 폴 0x104, 뒤 사유 0x102, 풀 0, 풀 밖 0x4000 | 4 KiB × 16,384 = 64 MiB 전부 풀 밖(`apple_agx_g3_copy_abi.h:38`, 0x102 = NOT_POOL). 빈 자리 확보용 축소는 64 KiB짜리 1개뿐(`measure-trim` 값). 내보내기 끝(gone) 뒤 다시 상주시켰는데 그대로 풀 밖 (`umd_gpuva_windows.c:2105-2220`) |
| 2회차 | 회차 1, 같은 결과 | 두 번째도 실패 → `reject-…` → 0x887A0005 |

- 풀/풀 밖 판단은 VidMm이 UpdatePageTable로 넘긴 PTE의 구역 번호 그대로입니다(`gpuva_g3_paging_windows.c:851`, 조사 `gpuva_g3_windows.c:1846-1880`). 우리 코드가 잘못 세는 것이 아니라 **VidMm이 그 버퍼를 풀이 아닌 곳에 놓은 것**입니다 [사실, 코드].
- 그때 이 프로세스 사용량은 2,432 / 4,096 MiB였습니다. 숫자상 풀에 1.6 GiB가 비어 있어야 하는데 VidMm이 풀을 고르지 않은 이유는 기록에 없습니다 [가설: 풀 주소 공간 조각남, 앞 크기의 해제가 늦음, UMD가 그 전에 버퍼 상주를 스스로 내려놓음 중 하나].
- 다음: `gpucov/jobs/K43v-umdtrace-window-2774.cmd`(읽기만, 재시작 없음, sha256 `09668f0b41bc1b09578aeb307562226fee60dafbc1945b5ca1db200128c24efc`, 1,613 B, VM에서 가짜 기록으로 점검). 크기마다 큰 버퍼 힙 줄 앞 12줄부터 첫 제출까지와, 모든 축소·상주 줄을 보여 줍니다. 실패 크기에만 UMD의 축소(`measure-trim` 마지막 값 0)나 `measure-make-resident-trimmed`가 있으면 원인은 우리 UMD → 그것을 고치는 것이 P39. 없으면 원인은 VidMm 배치 → 2775는 고침이 아니라 "실패 순간 풀 안 배치 목록(시작·크기·할당)" 기록판으로 만들고 K43e 연쇄와 함께 돌립니다.

---

## 9. K43v 결과와 2775 기록판 (2026-11-19 21시)

**결론: UMD는 실패 크기에서도 UPLOAD 버퍼의 상주를 스스로 내려놓지 않았습니다 [사실]. 그러므로 UPLOAD를 풀 밖에 둔 것은 VidMm의 배치 결정이고, 그 이유는 아직 모릅니다 [가설]. 그래서 2775는 고침이 아니라 VidMm이 보낸 배치를 그대로 남기는 기록판이고, probe14로 "큰 버퍼를 먼저 만들면 어떻게 되는지"를 함께 봅니다.**

| K43v 근거 (`nwoas_scripts/gpupool-s267/runs/K43v-20261119.txt`) | 값 |
|---|---|
| 축소·상주·거절 줄 전체 | 41줄 = 상주 요청 25 + 축소 2 + 되살리기 4 + 거절 10 |
| 축소 2줄의 마지막 값 | 둘 다 1(되살리기 안의 즉시 축소). 예산 때문의 느린 축소(마지막 값 0)는 0줄 |
| 크기별 큰 버퍼 힙 → 첫 제출 사이 | 1792·2048·3072: 거절·되살리기 없음. 2304·2560: 되살리기 2번 → 거절 |

- 2775 바뀐 것·sha256: `PATCHES.md` P39. probe14(`gpucov/bin/nwgpuprobe14-arm64.exe` `4c5432fe…4c50`, `-x64.exe` `8badfe35…8971`) = probe13 + `order=bigfirst`. VM(Parallels GPU) 64·128 MiB로 실행 확인만 함.
- 연쇄: `nwoas_scripts/gpupool-s269/run-2775-chain.sh`. 되돌리기: `agx-s248/jobs/R37-rollback-2774.cmd` → W4.
- 읽는 법(`K45t`): `U`(VidMm 페이지표 갱신) 줄의 `seg=3 pool_base_mib=`가 UPLOAD·READBACK·큰 버퍼의 구역 3 위치, `seg=0`/`other_mib`이 풀 밖. `C` 줄이 되살리기 실패 순간. 실패 크기에서 UPLOAD가 처음엔 구역 3에 있다가 큰 버퍼 배치 무렵 풀 밖으로 갔는지, 처음부터 풀 밖이었는지, 큰 버퍼가 구역 3의 어디를 차지했는지를 봅니다.
- 정정(21:3x): 연쇄 순서를 `K45a K45l K45t K45b K45t K43c …`로 바꿨습니다. K43c는 사다리 뒤에 fill3·D3D11까지 돌아 링(64칸)이 그쪽 기록으로 덮이기 때문에, 사다리만 도는 `K45l`(시작 전에 두 값을 지움, probe pid 출력)을 앞에 두었습니다. `K45b`는 최신 값만 지우고 First는 둡니다. K45t의 `written_age_s`는 부팅이 바뀌면 의미가 없으므로 신선한지는 `pid=`가 K45l/K45b의 `NWGP begin … pid=`와 같은지로 봅니다. `SKIP_INSTALL=1`이면 설치를 건너뜁니다(패키지 전달은 덮어쓰기를 거절하므로 두 번째 전체 실행은 전달에서 멈춤).
- 주의: 2775 준비 중 `make-2775.py`의 이름 실수로 `file-delivery-s243/jobs/gpucov14-j274-2774.cmd`를 한 번 덮어썼고, 같은 생성기(`make_job.build`)와 바뀌지 않은 `manifests/agx-j274-2774.json`으로 다시 만들었습니다(sha256 `b3e0b5fa…6709`, probe12 해시 b62590cc…·c778a7f1… 들어 있음). 원본과 바이트 비교는 하지 않았습니다.

---

## 10. 2775 실기 결과 (22:00~22:18, `nwoas_scripts/gpupool-s269/runs/chain-2775-20261119-220052.txt`)

**결론: 이번에는 2304·2560 실패가 다시 나오지 않았습니다(사다리 3번, 15개 크기 모두 통과). 그래서 원인은 아직 확정되지 않았습니다. 기록판은 제대로 동작했고(통과한 경우의 배치를 그대로 남김) [사실], 08:10 실패 때와 지금의 차이로는 "VidMm의 내용 옮기기(TRANSFER)와 KMD가 모르게 사라진 배치(kills)가 그때만 많았다"는 것이 보입니다 [사실, 숫자]. 그것이 원인이라는 것은 [가설]입니다.**

| 항목 | 08:10 (2774, 실패) | 22:06~22:09 (2775) |
|---|---|---|
| 사다리 결과 | 1792·2048·3072 통과, 2304·2560 실패 | K45l·K45b·K43c 모두 5/5 통과 |
| 큰 버퍼 만들기 (ms/GiB) | 1,884~2,541 | 약 1,000~1,300 |
| 풀 kills / places | 701 / 1,249 (2304 실패 직후) | 7 / 2,345 (fill3 끝) |
| 페이징 TRANSFER 나눔 횟수(칸 88) | 927 (2304 실패 직후) | 3 (fill3 끝) |
| 페이징 FILL 나눔 횟수(칸 89) | 40 | 165 |

- 기록(K45t, 통과 경우) [사실]: UPLOAD 111.8 MiB, READBACK 175.8 MiB, 큰 버퍼 239.8 MiB 위치(구역 3 아래부터 차례로), 크기가 바뀌어도 같은 자리. 큰 버퍼 먼저(K45b)면 큰 버퍼 112 MiB, 스테이징은 그 뒤(3,184·3,248 MiB). 풀 밖 갱신·조사 실패(`C`) 0건.
- 08:10에는 큰 버퍼를 0으로 채우는 대신(FILL) 내용을 옮기는(TRANSFER) 경로가 많이 쓰였고 kills가 많았으므로, 그때 VidMm이 큰 버퍼를 먼저 구역 3 밖에 두었다가 들여오면서 스테이징을 밀어냈다는 것이 가장 그럴듯한 설명입니다 [가설]. 왜 그날 그 부팅에서만 그랬는지는 모릅니다.
- 회귀: K43c 채우기 14/14·D3D11 1,024 MiB PASS, K9 23/23×4, K10 13/13×2·present 4/4, K25b 17/17×2, K21 화면 내용 있음, K38g·K38dg Edge 치명 0·OOM 0, K41 DWM 사건 0, EL2 예외 0(두 부팅) [사실]. 설치 때 풀 끈 부팅의 장치 Error(CM_PROB_FAILED_POST_START, StartStage 8)는 2774 설치 때와 같고 풀 켠 부팅에서 `DEV OK` [사실].
- 다음: `K45r`(사다리 3번 반복 + 매번 영수증)을 한 부팅에 2번, 다시 부팅해 1번 = 9번. 실패가 한 번이라도 나오면 `Wom1PoolTimelineFirst`에 그 순간이 남습니다. 9번 다 통과하면 2774로 되돌려 같은 반복을 해서, 기록 코드가 시간을 바꿔 실패를 감춘 것인지 가립니다.

---

## 11. 재현 성공 (22:45~22:53, `nwoas_scripts/gpupool-s269/runs/chain-2775-20261119-224504.txt`)

**결론: 실패가 기록판 위에서 재현됐고, 실패 순간의 순서가 확정됐습니다 [사실]. VidMm이 64 MiB 스테이징 두 개를 시스템 페이지(구역 0)로 내보낸 바로 뒤에 큰 버퍼를 그 자리(구역 3의 0 MiB부터)에 놓았고, 되살리기 두 번(약 0.2초 안) 모두 VidMm이 다시 구역 0에 두었습니다. VidMm이 왜 그렇게 골랐는지는 [가설]입니다.**

| 실행 | 결과 | kills | TRANSFER 나눔(칸 88) |
|---|---|---|---|
| 부팅1 K45r 첫 번째 | 2304 FAIL | 738 | 925 |
| 부팅1 K45r 두 번째 | 2304·2560 FAIL | 950 | 2,076 |
| 부팅2 K45r 1회 | 모두 PASS | 7 | 1 |
| 부팅2 K45r 2회 | 2560 FAIL | 737 | 925 |

실패 순간(부팅1 첫 실패, `first` 기록, t = KMD 시작 뒤 ms):

| t (ms) | 일 | 구역 3 위치 (MiB) |
|---|---|---|
| 1,108 | 1792 큰 버퍼 | 247.25 |
| 7,873 | 2048 큰 버퍼 (1792 파괴 1.5초 뒤) | **2,041.25** (평소처럼 247이 아니라 앞 버퍼 끝 위) |
| 14,858 / 14,943 | 2304용 UPLOAD / READBACK | 119.25 / 183.25 |
| 15,835 / 16,170 | 페이징 프로세스가 둘을 **구역 0**으로 | — |
| 17,580 | 2304 큰 버퍼 | **0** (스테이징 자리 덮음) |
| 21,114 | 조사: UPLOAD 64 MiB 전부 풀 밖 → 되살리기 실패 → 거절 | — |

- 부팅2 2560 실패는 앞 크기(2304)가 평소 자리(239.69)였는데도 같은 순서(스테이징 구역 0 → 큰 버퍼 0 MiB)였습니다. 그래서 "앞 버퍼가 이상한 자리"는 필요조건이 아닙니다 [사실].
- 실패 뒤 다음 크기의 스테이징은 0.44 MiB 자리에 놓였습니다. VidMm이 스테이징 아래쪽 영역까지 통째로 비웠다는 뜻입니다 [사실].
- 실패 순간 기록에는 큰 버퍼 [0, 크기) 위쪽에 아무것도 없습니다. 그래서 "자리가 없어서 못 돌아왔다"로는 설명되지 않습니다 [사실: 기록 범위 안에서]. VidMm이 한 번 내보낸 버퍼를 곧바로 다시 들이지 않는 이유는 [가설].
- TRANSFER 나눔 925 ≈ 64 MiB × 2를 옮긴 양이라 원인이 아니라 결과로 봅니다 [계산·가설].

→ 2776은 고침 후보를 스위치로 넣은 시험판입니다(12장).

---

## 12. 2776 시험판과 판정 방법

**결론: 원인의 "무엇"은 확정됐지만 "왜"가 [가설]이라 고침을 하나로 정하지 않고, 후보 3개를 꺼짐 기본 스위치로 넣은 2776을 만들었습니다 [빌드만]. 같은 부팅에서 꺼짐 다리가 실패하고 켜짐 다리가 모두 통과한 후보만 고침으로 인정합니다.** 2304·2560 실패는 22:04 부팅처럼 고침 없이도 전부 통과하는 부팅이 있으므로 "9번 다 통과"만으로는 판정할 수 없습니다.

| 후보 | 근거 | 구분 |
|---|---|---|
| R: 되살리기 회차 16번, 회차 사이 250 ms | 지금은 두 회차가 약 0.2초 안에 끝남. 시간이 지나면 VidMm이 다시 들일 수 있는지 봄 | [가설 시험] |
| P: CPU에서 보이는 풀 버퍼 상주 우선순위 MAXIMUM | VidMm이 큰 버퍼 자리를 만들려고 스테이징을 내보냄 | [가설 시험] |
| T: CPU에서 보이는 버퍼 위에서부터 배치 | 큰 버퍼(아래부터)와 자리가 겹치지 않게 함 | [가설 시험] |

- 연쇄 `nwoas_scripts/gpupool-s272/run-2776-chain.sh`: 설치(풀 끔) → 부팅 B: K46a(off R off P off R off P), K46t(T 3번, 장치 재시작), K46z(off 2번, 다시 재시작) → 부팅 C: K46a 한 번 더 + 회귀(모든 스위치 꺼짐).
- 셋 다 안 되면: 구조적 해결은 CPU에서 보이는 버퍼를 다른 구역으로 분리하는 것이고(UMD의 CPU 매핑 검사 `POOL_ONLY`까지 바꿔야 함), 오늘 밤에는 만들지 않습니다.

---

## 13. 2776 A/B 결과 (23:23~23:55, `nwoas_scripts/gpupool-s272/runs/chain-2776-20261119-232330.txt`)

**결론: 세 후보 모두 아직 고침으로 인정할 수 없습니다. R은 실험이 되지 않았고(R 다리에서 되살리기가 한 번도 돌지 않음), P는 실패했으며, T는 장치 재시작 실패 때문에 시험이 무효입니다** [사실].

| 부팅 | 다리 | 결과 | 되살리기(성공) |
|---|---|---|---|
| B | off 1·3·5·7 | 1: 2304 FAIL, 3: 2304 FAIL, 5: PASS, 7: 2048·2304 FAIL | 2(0), 2(0), 0, 4(0) |
| B | R 2·6 | 모두 PASS | 0(0), 0(0) |
| B | P 4·8 | 4: PASS, 8: 2560 FAIL | 0, 2(0) |
| B | K46t T 1~3 | 모두 2048부터 FAIL | 0 |
| B | K46z off 1~2 | 모두 2048부터 FAIL | 0 |
| C | 8다리 모두 | 모두 PASS(그 부팅은 판정에 못 씀) | 0 |

- **R:** R 다리 4개 모두 `rehome=0`입니다. R은 조사가 "풀 밖"이라고 답할 때만 동작하는 코드라(`umd_gpuva_windows.c` 되살리기 경로), 이번 R 다리들은 꺼짐과 똑같이 돌았습니다. 통과는 운입니다 [사실, 코드]. 그래서 R을 기본으로 켠 2777은 근거가 없어 만들지 않았습니다.
- **P:** 다리 8에서 2560 FAIL → 고침 아님 [사실].
- **T:** K46t의 장치 재시작 뒤 `DEV Error CM_PROB_FAILED_POST_START`. K46z(T=0)의 재시작 뒤에도 같았습니다. 그래서 T가 아니라 "사용 중 재시작"이 실패한 것이고, 2048부터의 실패는 장치가 깨진 상태의 결과입니다(되살리기·거절 0) [사실]. 설치 때(2774·2775·2776 모두, 풀 끈 부팅 중 재시작)도 같은 Error(StartStage 8, 0xc0000483)가 나옵니다. 부팅 직후 D28 재시작은 매번 `DEV OK` [사실]. `Wom1PoolCpuVisibleTopDown`은 K46z에서 0으로 돌아갔고 부팅 C에서 0 [사실].
- DWM: 22:49:10 dwm.exe 충돌(dwmcore.dll, 예외 0xc00001ad) 1건. 2775 K45r 두 번째(2304·2560 동시 실패) 중이었습니다 [사실]. 2776 연쇄 동안 새 충돌은 없습니다.
- 회귀(부팅 C, 스위치 모두 꺼짐): K43c PASS 등 2775와 같음.

다음 시험(새 빌드 없음, 2776 그대로): K47a = 한 부팅에서 off R R ×4(R = 되살리기 40회 × 250 ms, 버퍼당 최대 약 10초). R은 "되살리기가 돈 다리"에서만 판정합니다: `rehome_ok` 1 이상이고 그 크기가 PASS면 R이 고친 것, 되살리기 40회가 다 실패하면 R은 고침이 아닙니다. T는 사용 중 재시작을 피해 K47t로 값만 1로 바꾸고 다음 부팅에서 K47b(off R ×4)를 돌립니다. 같은 부팅에 T 꺼짐 대조군이 없으므로 T는 실패 비율 비교만 됩니다(약한 근거).
