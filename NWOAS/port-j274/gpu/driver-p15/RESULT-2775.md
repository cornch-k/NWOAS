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

---

## 14. K47 결과와 다음 (01:15~01:38, `nwoas_scripts/gpupool-s272/runs/chain-2776-20261120-011535.txt`)

**결론: R은 고침이 아닙니다 [사실]. T는 배치를 의도대로 바꾸고 그 부팅에서 8/8 통과했지만, 같은 조건의 대조군이 없어 아직 고침으로 확정하지 않습니다. 다음은 T를 부팅마다 번갈아 켜는 시험이며, 2776 그대로 돌릴 수 있어 새 빌드가 필요 없습니다.**

| 부팅 | 다리 | 결과 | 되살리기(성공) |
|---|---|---|---|
| B (T=0) | off 1·4·7 | 1: 2048·3072 FAIL, 4: 3072 FAIL, 7: 3072 FAIL | 4(0), 2(0), 2(0) |
| B (T=0) | R 2·3·5·6·8 | 모두 3072 FAIL | 40(0) × 5 |
| B (T=0) | 9~12 | 모두 PASS | 0 |
| C (T=1, 부팅 때부터) | off·R 8다리 | 모두 PASS | 0 |

- R: 되살리기 40회 × 250 ms(약 10초) 동안 한 번도 풀로 돌아오지 않았습니다(5다리 모두). VidMm은 한 번 시스템 페이지로 보낸 CPU에서 보이는 버퍼를 적어도 10초 안에는 다시 풀에 들이지 않습니다 [사실].
- 3072 실패의 거절 대상은 64 MiB 스테이징이 아니라 64 KiB짜리 CPU에서 보이는 버퍼였습니다(풀 밖 0x10 페이지). 스테이징 두 개도 같은 때 구역 0으로 갔습니다. 같은 무렵 다른 프로세스(pid 3924)의 64 KiB 버퍼도 풀 밖이었습니다 [사실]. 그 프로세스가 무엇인지는 모릅니다 [가설: DWM 등]. 22:49의 DWM 충돌과 같은 경로일 수 있습니다 [가설].
- T=1 부팅의 배치: 스테이징 3,773·3,866·3,930 MiB(위쪽), 큰 버퍼 0·0.75 MiB(아래쪽), 구역 0으로 가는 갱신 0건 [사실]. 다만 같은 T=0 부팅 안에서도 다리 9~12는 통과했고, 2776 부팅 C(T=0)도 8/8 통과했으므로 "8/8 통과"만으로는 T의 효과라고 할 수 없습니다.
- T의 한계 [가설]: 앞 큰 버퍼의 자리가 늦게 풀리는 동안 새 큰 버퍼가 위쪽까지 써야 하면, VidMm이 위쪽 스테이징을 내보낼 수 있습니다.

다음 시험: `nwoas_scripts/gpupool-s272/run-t-alternate.sh` — 부팅 4번(T 0·1·0·1), 부팅마다 K48(8다리). 마지막 T=1 부팅에서 회귀 전부. 판정: T=1 부팅의 다리 16개가 모두 통과하고, T=0 부팅 중 하나라도 실패 다리가 있으면 T를 고침으로 인정 → 2777(T 기본 켬, 초안 `P41-draft-topdown-default.diff`).
구조 변경(CPU에서 보이는 버퍼를 구역 2로 분리)은 UMD의 CPU 매핑 검사(`POOL_ONLY`)까지 바꿔야 하고 구역 2가 952 MiB뿐이라, T가 안 될 때의 다음 단계로 둡니다.

---

## 15. Edge `edge://gpu`의 Rasterization·Skia Graphite "Unavailable" (목표 3)

**결론: 둘 다 Chromium의 공개 차단 목록에 걸린 것이고, 드라이버 오류가 아닙니다 [사실, 소스].** "Unavailable"은 상태값 `unavailable_off` = 차단 목록(`kGpuFeatureStatusBlocklisted`)일 때만 나옵니다(`content/browser/gpu/compositor_util.cc` GetFeatureStatusImpl). 꺼짐이면 "Disabled"로 나옵니다.

| 항목 | 걸린 조건 (Chromium main, 2026-11-20 받음) | 우리 값 | 드라이버로 풀 수 있나 |
|---|---|---|---|
| Rasterization | `gpu/config/software_rendering_list.json` 122번: Windows에서 GPU 래스터는 NVIDIA·Intel·AMD(FL 11.0 이상), Xbox, Qualcomm(공급자 0x4d4f4351, 드라이버 31.0.2.0 이상 등), LISUAN(0x4c54)만 허용 | 공급자 0x4c505041 → 목록 밖 | 공급자 번호를 남의 것(예: Qualcomm)으로 속여야만 됨 → 권하지 않음 |
| Skia Graphite | 같은 파일 188번: D3D11 기능 수준 12.0 미만이면 차단. 게다가 `gpu_finch_features.cc` IsSkiaGraphiteSupportedByDevice: Windows ARM64에서는 아예 false("requires further research") | D3D11 UMD 최대 11_1(`agx_d3d11_ddi.cpp` AgxD3d11Fl111Enabled, `build-native-asahi-state.py` 3DPIPELINESUPPORT) | 안 됨: 12_0을 알려도 ARM64 Edge에서는 Graphite를 쓰지 않음 |
| Direct Rendering Display Compositor | `kEnableDrDc`가 Windows에서 "NOT SUPPORTED. DO NOT ENABLE!" 기본 꺼짐, 표시는 `disabled_off_ok` | — | 해당 없음(모든 Windows PC가 Disabled) |

- 대조 [사실, VM]: Parallels VM의 Edge(공급자 0x05404c42)에서도 `rasterization=unavailable_off`, `skia_graphite=unavailable_off`, `direct_rendering_display_compositor=disabled_off_ok`로 같게 나옵니다.
- 참고: ANGLE은 D3D11 장치를 12_1·12_0·11_1·11_0 순으로 요청하고(`Renderer11.cpp:435-470`), Chromium은 그 장치의 GetFeatureLevel()을 씁니다(`gpu_info_collector_win.cc:239`).

권장 순서:

| 순위 | 할 일 | 이득 | 위험 |
|---|---|---|---|
| 1 | 드라이버는 그대로. `--enable-gpu-rasterization`(edge://flags "GPU rasterization" Enabled)으로 차단만 넘겨 우리 드라이버에서 GPU 래스터가 바르게·안정적으로 도는지 먼저 잼(K49 2회째 + 기존 Edge 시험) | 스크롤·페이지 그리기를 GPU로 | 그 Edge 프로필에만 적용, 되돌리기 쉬움 |
| 2 | 1이 좋으면 콘치님 Edge에 그 플래그를 켬(사용자 설정) | 같음 | 낮음 |
| 3 | Chromium에 공급자 0x4c505041 예외를 요청(장기) | 플래그 없이 | 시간 |
| 하지 않음 | 공급자 번호를 Qualcomm 등으로 바꿈 | 차단이 풀림 | Qualcomm용 우회 5개(`gpu_driver_bug_list.json` 433·446·448·462·481: WebNN·동영상 경로 끔)와 앱·게임의 공급자별 경로가 잘못 걸림, 사실과 다른 장치 정보 |
| 하지 않음 | D3D11 12_0 올리기(타일 자원 Tier 2 등 D3D11 쪽 구현 필요) | Graphite에는 효과 없음(ARM64) | 큼 |

`edge://gpu` 글로 받기: `gpucov/ps/edgegpu.ps1`(Edge를 사용자 세션에서 별도 프로필 + 127.0.0.1 DevTools 포트로 띄우고 `SystemInfo.getInfo` JSON 저장, 화면 캡처 없음), 작업 `gpucov/jobs/K49-edge-gpuinfo.cmd`, 실행 `gpupool-s272/run-edgegpu.sh`. VM에서 사용자 세션으로 실행해 JSON 7,335 B를 받는 것까지 확인 [사실, VM].

---

## 16. x86(32비트) UMD 준비 (P42, 목표 4) [빌드 전]

**결론: 코드는 이미 x86을 고려해 만들어져 있고(원본 EXP1222·EXP1874B, 고정 폭 통신 구조), 이번에 J274 빌드 경로·INF·시험만 붙였습니다. 빌드는 Parallels VM이 필요해 아직 하지 않았습니다.**

| 부분 | 상태 | 근거 |
|---|---|---|
| KMD | 바꿀 것 없음. INF가 `UserModeDriverNameWow`를 넣으면 `SupportsOnly64Bit=0`이 되어 32비트 앱에 어댑터가 보임 | `lifecycle.c:669-705, 1125-1134`, `render_wow64.c` [사실, 코드] |
| UMD↔KMD 통신 구조 | 포인터 크기 필드 없음(APPLE_AGX_U32/U64만). "identical for ARM64/x64/WOW64" | `render_win32_transport.h:37`, `apple_agx_g3_copy_abi.h` 등 [사실, 코드] |
| 사용자 공간 매핑(네이티브 스테이징) | KMD가 4 GiB 위에 매핑하면 UMD가 거절하고 그 기능만 끔 | `umd_win32_screen.c:813-815` [사실, 코드] |
| UMD 소스의 x86 분기 | 이미 있음(`_WIN64` 아닌 쪽: 펜스 64비트 읽기를 콜백으로) | `umd_gpuva_windows.c:923`, `umd.c:444` |
| 빌드 도구 | Mesa 스크립트는 x86을 이미 받음(`compiler-x86.ini`, `configure-x86-nir.ps1`, build-*.py `--architecture x86`). dxil-spirv 빌드만 x86이 없어서 추가 | `c49-build-dxspv.ps1` |

바꾼 것·새 파일 (P42, `P42-driver.diff`):
- `umd/AppleAgxRenderAdmissionUmd.vcxproj`: `Release|Win32` 구성 추가(다른 구성 그대로)
- `scripts/build-umd-x86-msbuild.ps1`(새): x64용(P37)과 같은 방식, Platform=Win32, x86 런타임
- `mesa/dxil/patches/c49-build-dxspv.ps1`: `-Architecture x86`
- `mesa/scripts/configure-x86-nir.ps1`: `-CrossFile`, `-Platforms` 인자(기본값은 원본 그대로)
- `AppleAgxRenderAdmission.arm64xwow.inf`(새): P37 INF + `AppleAgxRenderAdmissionUmd32.dll`을 SysWOW64(DIRID 10)에 복사, `UserModeDriverNameWow`
- VM 단계 `agx-build-p42/05y·08y·11y·12y`, 빌드 `build-p42.sh`
- Windows 작업: `agx-s248/jobs/I39-install-2777.cmd`, `R39-rollback-2776.cmd`, `gpucov/jobs/K50-x86-umd-p15.cmd`; 시험 도구 probe15(x86 판 + `AppleAgxRenderAdmissionUmd32.dll` 확인 줄, `gpucov/probe/build-probe15.cmd`); 전달 `file-delivery-s243/make-2777.py`; 연쇄 `gpupool-s273/run-2777-chain.sh`

위험 [가설]: J274 소스(P15~P40)는 x86으로 한 번도 컴파일된 적이 없어 빌드 오류가 날 수 있음. x86 Mesa 구성·빌드와 dxil-spirv x86은 처음이라 오래 걸림. VM에 `vcvarsarm64_x86.bat`가 있는지 모름. 되돌릴 때 SysWOW64 파일은 이름만 바꿔 둠(R39).

## 17. 11-20 09:18-09:56 T 교대 부팅 시험 — T=1 부팅이 한 번도 없었음 (스크립트 버그)
- [사실] run-t-alternate.sh 의 부팅 순번 변수 `i` 를 waitline()/job() 의 반복문이 `local` 없이 덮어써서
  (`BOOT 23 of 4`, `BOOT 24 of 4` 로 찍힘) 다음 부팅 값 `next` 가 늘 비었고, 네 부팅 모두 끝에 K47z(지우기)만 실행됨.
  K47t(쓰기)는 한 번도 실행되지 않았고, 마지막 T=1 부팅에서 돌릴 회귀 시험도 실행되지 않음.
- [사실] 네 부팅 모두 K48 이 읽은 값이 비어 있음(값 없음 = 드라이버는 0으로 읽음).
  배치 기록에서도 CPU가 보는 버퍼 267건이 전부 풀 아래쪽(2,000 MiB 이하)에 놓임. 01:15 시험의 T=1 부팅에서는 42건이 위쪽(3,866~3,930 MiB)에 놓였음.
- 그래서 이번 결과는 T=0 대조 4부팅으로만 쓸 수 있음. 부팅별 실패 갈래: 2/8, 1/8, 6/8, 2/8 (8갈래 × 4부팅 중 11갈래 실패).
  T가 고침인지 여부는 여전히 판정 전.
- 수정: 순번을 `bi` 로 바꾸고 waitline/job 에 `local i`. K48 이 읽은 값이 기대값과 다르거나 K47t 가 1을 쓰지 못하면
  K47z 로 지운 뒤 멈춤(exit 9). 하드웨어 없이 가짜 함수로 흐름만 모의 실행해 0→1→0→1 순서와 회귀 위치를 확인함 [빌드만].
  run-t-alternate.sh sha256 27318b10cc14b18e7f68d3198ca903c60563f158b4ba66eee42e969521f7664b (6,611 B).

## 18. 11-20 10:26-10:43 2777 연쇄 결과와 후속
- [사실] K50 arm64 의 device=0 은 판정 작업의 실수: arm64 줄만 `umd draw11` 로 돌려 `d3d11 device` 줄이 나올 수 없었음.
  같은 실행에서 arm64 `umd d3d11 … hr=0x00000000 feature_level=0xb000`, `draw11 result=PASS bad_pixels=0`, removed_reason=0.
  2776 대비 퇴행 아님. K50 arm64 줄에 d3d11 추가(K50 sha ce0745d0…31b9).
- [사실] x86: SysWOW64 에 vcruntime140/msvcp140 없음(err=126) → Umd32 가 올라가지 못해 d3d11/d3d12 hr=0x887a0004.
- 준비 [빌드만]: make-vcredist-x86.py + I40-vcredist-x86.template(→ agx-s248/jobs/I40-vcredist-x86.cmd),
  K51-crt32-find.cmd(읽기 전용), gpupool-s273/run-vcredist-x86.sh.

## 19. 11-20 2778(P43, TopDown 기본 켜짐) 빌드와 시험 준비
- [사실] 고친 교대 시험(11:35~12:19, 2777 위): 네 부팅 모두 읽은 값 확인 통과. T=0 1/8·6/8 실패, T=1 0/8·0/8. 01:15 T=1 0/8 포함 T=1 3부팅 24/24 통과, T=0 6부팅 모두 실패 있음 → 기준 충족(지휘자 판정).
- [빌드만] 2778 빌드 성공(PATCHES.md P43). 준비: make-2778.py, I41-install-2778.cmd, R40-rollback-2777.cmd, gpupool-s274/run-2778-chain.sh(모의 실행으로 흐름·판정 줄만 확인).
- [사실, 호스트만] 11-20 오후 점검: `run-2778-chain.sh`는 시작 전 검사에서 `KITJ: unbound variable`로 바로 멈추는 상태였음(2777 연쇄에서 남은 검사 줄, `set -u`). 맥미니에 닿기 전 단계라 실기 영향은 없음. 고친 것 3가지:
  ① 남은 2777 검사 줄 삭제, ② 배치 판정을 이번 부팅 K48의 프로세스 번호(pid) 것만 세도록 바꿈(K45t의 `first` 기록은 앞 부팅 것이 남을 수 있음) — `BOOT B/C PLACEMENT fresh_top=N fresh_low=M => OK|BAD`, OK = N > 0 이고 M = 0, ③ 요약 줄을 변수에 담아 쓰기(기록 파일을 읽으면서 같은 파일에 덧붙이던 것).
  배치 판정 규칙을 11:35 교대 시험 기록에 적용: T 값 없음 부팅 0/21·0/12 → BAD, T=1 부팅 14/0·14/0 → OK. 연쇄 전체는 여전히 실기 전.
  sha256 `be72a450617406a4a5d9d933cc90ae5cce604076f80b602304cf1d27e34be396` (6,810 B).

## 20. 11-20 Edge GPU 그리기(`--enable-gpu-rasterization`) 정확성·안정성 시험 K52 준비 [실기 전]

**결론: 드라이버를 바꾸지 않고, 같은 페이지를 "기본(CPU로 그림)"과 "GPU로 그림" 두 번 띄워 화면을 비교하는 시험을 만들었습니다. 맥미니에서는 아직 돌지 않았습니다. Parallels VM(Parallels GPU)에서는 도구가 끝까지 돌고 PASS가 나왔으며, 이것은 도구가 동작한다는 것만 뜻합니다.**

| 파일 | 하는 일 | sha256 |
|---|---|---|
| `nwoas_scripts/gpucov/web/raster1.html` | 움직이지 않는 시험 페이지(글자 6가지 크기·한글, 그러데이션, 둥근 테두리·그림자, SVG, 회전·흐림·반투명, 표, 캔버스, 긴 목록. 높이 6,398 px) | `8bc1a17d…3b7e` |
| `nwoas_scripts/gpucov/ps/edgeraster1.ps1` | Edge를 별도 프로필 + 127.0.0.1 DevTools로 띄워 4곳 캡처 → 60초 스크롤·확대 반복 → 다시 캡처. 비교는 8×8 칸 평균(칸 평균 차 16 초과 = 나쁜 칸) | `78f126f2…02a7` |
| `nwoas_scripts/gpucov/jobs/K52-edge-raster-ab.cmd` (2,324 B) | 두 번 실행 + 비교 + 판정 줄 | `0d9b9788…c9dc` |
| `nwoas_scripts/file-delivery-s243/make-edgeraster.py`, `manifests/gpucov-edgeraster.json`, `jobs/gpucov19-edgeraster.cmd` (1,672 B) | 위 두 파일을 `C:\NWOAS\gpucov`로 전달 | `3d81eb96…bf37` |
| `nwoas_scripts/gpupool-s275/run-edgeraster.sh` | 평소 실행기(S268) 부팅 → 전달 → K52. `gpupool-s272/run-edgegpu.sh`(10:23 실기에서 돈 것)와 같은 흐름 | `01a63d04…e9c6` |

판정 줄 `K52 VERDICT … => PASS` 조건: GPU 쪽 `raster=enabled…`, 네 캡처 모두 나쁜 칸 1.0% 이하, GPU 쪽의 반복 전·후 캡처가 나쁜 칸 0, GPU 프로세스 1개(다시 뜨지 않음), 최근 충돌 수 0, FATAL 0, 컨텍스트 잃음 0, 60초 동안 300프레임 이상, 캡처 5장.

VM 점검 값 [사실, VM · Parallels GPU, 13:05~13:08]:

| 항목 | 기본 | GPU로 그림 |
|---|---|---|
| rasterization 상태 | unavailable_off | enabled_force |
| 60초 프레임 수 | 3,564 | 3,559 |
| 기본 대비 나쁜 칸 (4장, %) | — | 0.000 / 0.000 / 0.000 / 0.000 |
| 기본 대비 가장 큰 칸 평균 차 (4장) | — | 6 / 9 / 1 / 3 |
| 반복 전·후 나쁜 칸 | 0 | 0 |
| GPU 프로세스 수 · FATAL · 잃음 | 1 · 0 · 0 | 1 · 0 · 0 |

- 일부러 다른 그림을 넣으면 나쁜 칸 39.9%·FAIL이 나오는 것도 확인 [사실, VM].
- VM에서 돌리지 않은 것: K52 작업 파일 자체(임시 .cmd 만들기), 전달 작업, `run-edgeraster.sh`. VM 시험 폴더(`C:\nwtest\er`, 공유 폴더 `ertest`)는 지움.
- 한계: 기준 "16"과 "1.0%"는 VM에서 본 차이(최대 9, 0%)에 여유를 둔 값이고, 맥미니 첫 결과를 보고 다듬어야 할 수 있음 [가설]. 화면 캡처는 DevTools가 합성 결과를 읽어 오는 것이라 모니터에 실제로 나간 화면과 같다는 보장은 아님.

## 21. 11-20 13:09~13:15 K52 실기 결과 (2777, 평소 실행기 S268, `nwoas_scripts/gpupool-s275/runs/edgeraster-20261120-130906.txt`)

**결론: 안정성은 통과, 그림 비교는 FAIL이며 원인은 아직 모릅니다. 캡처 크기로 보면 shot 1·2에서 VM 정상값과 다른 쪽은 GPU로 그린 쪽이 아니라 기본 설정 쪽입니다 [사실: 크기]. 무엇이 틀렸는지는 그림을 받아 봐야 합니다(K53).**

| 항목 | 기본 (A) | GPU로 그림 (B) |
|---|---|---|
| 그리기 상태 | unavailable_off | enabled_force |
| 60초 프레임 수 | 5,842 | 5,744 |
| GPU 프로세스 수 · 치명 · 잃음 | 1 · 0 · 0 | 1 · 0 · 0 |
| 반복 전·후 나쁜 칸 | 0 | 0 |
| 캡처 크기 shot 0~3 (base64 글자) | 175,488 / 186,592 / 130,668 / 167,560 | 166,380 / 123,664 / 255,356 / 167,604 |
| VM 정상값 (20장) | 175,488 / 133,048 / 256,520 / 167,560 | 171,692 / 128,996 / 255,124 / 167,640 |

| shot | A 대 B 나쁜 칸 (%) | 가장 큰 칸 차 | 자리 |
|---|---|---|---|
| 0 | 1.506 | 147 | 616,432-968,552 (넷째·다섯째 상자: 원뿔 그러데이션·점선 테두리) |
| 1 | 53.619 | 215 | 화면 전체 |
| 2 | 76.964 | 208 | 화면 전체 |
| 3 | 0.012 | 20 | 40,744-48,800 |

- [가설 가] 기본 설정(CPU로 그린 조각을 GPU에 올려 합성)에서 스크롤 뒤 조각이 틀리게 나옴. [가설 나] 캡처 시점 문제(시험 쪽). shot 0의 차이는 GPU 쪽 그리기 차이로 보임 [가설].
- 다음: `gpucov/jobs/K53-er-export.cmd`(읽기만, sha256 `28115a3d…44bd`)로 캡처를 받아 눈으로 확인.

## 22. 11-20 13:15~13:41 2778 실기 결과 (`nwoas_scripts/gpupool-s274/runs/chain-2778-20261120-131518.txt`)

**결론: 2778은 설치·동작하고 회귀는 없지만, "위에서부터 놓기"는 완전한 고침이 아닙니다 [사실]. 두 번째 부팅의 첫 갈래에서 2048 MiB가 한 번 실패했고, 그 순서는 14장에서 적어 둔 한계(앞 큰 버퍼 자리가 아직 안 풀려 새 큰 버퍼가 위쪽까지 올라옴)와 같습니다 [사실: 배치 기록].**

| 부팅 | 갈래 실패 | 배치 판정 | 그 밖 |
|---|---|---|---|
| 설치(풀 끔) | — | — | VC 30.0.2778.0, 레지스트리 값 없음(= 켜짐) |
| B | 0 / 8 | OK (위 14, 아래 0) | K43c PASS, K50 x86·x64·arm64 PASS, K38g·K38dg 치명 0, K41 0 |
| C | 1 / 8 (갈래 1의 2048) | OK (위 18, 아래 0) | K41 0, EL2 예외 0 |

실패 순간(부팅 C 갈래 1, pid 5632, `first` 기록, t = KMD 시작 뒤 ms):

| t (ms) | 일 | 구역 3 위치 (MiB) |
|---|---|---|
| 8.6 / 100 | 1792용 스테이징 두 개 | 3,922 / 3,858 (위쪽) |
| 1,166 | 1792 큰 버퍼 | 1.75 |
| 6,324~6,406 | 1792 큰 버퍼와 스테이징 파괴 | — |
| 6,668 | 2048용 스테이징 ① | 3,921.94 |
| 6,812~6,903 | 2048용 스테이징 ② | 3,792.69 (평소 3,858보다 아래) → 6,903.6에 **구역 0으로** |
| 8,247 | 2048 큰 버퍼 | **1,793.81** (앞 버퍼가 있던 자리 바로 위. 끝 = 3,841.81로 스테이징 ②의 자리와 겹침) |
| 11,527 | 조사: 스테이징 ② 64 MiB 전부 풀 밖 → 되살리기 2번 실패(0x102) → 거절 | — |

- 풀 4,096 MiB에서 큰 버퍼가 1,793.81~3,841.81을 쓰면 위쪽에 남는 자리는 254 MiB인데, 스테이징 ①이 3,921.94~3,985.94에 있어 ②가 3,792.69로 내려갔고 큰 버퍼와 겹쳤습니다 [사실: 숫자]. VidMm이 새 큰 버퍼를 0 근처가 아니라 앞 버퍼 자리 위에 놓은 이유는 [가설: VidMm 쪽 파괴가 늦게 끝남. 11장 부팅1의 2,041.25와 같은 모양].
- 합계: T 켬 5부팅 40갈래 중 39 통과(24/24 + 8/8 + 7/8), T 끔 6부팅은 모두 실패 있음(11:35 시험 1/8·6/8 등). 실패율은 크게 줄었지만 0은 아닙니다.
- 판정: 2778은 그대로 둡니다(2777보다 낫고 회귀 없음). 남은 실패를 없애려면 CPU가 보는 버퍼를 큰 버퍼와 다른 구역에 두어야 합니다(14장 끝의 구조 변경). 아직 설계·빌드 전입니다.

## 23. K52 캡처 그림 확인 (`nwoas_scripts/gpucov/runs/k53-export-20261120-1341/`, K53으로 받음)

**결론: GPU로 그리면 원뿔 그러데이션이 네 조각 단색으로 잘못 나옵니다 [사실, 그림]. shot 1·2의 큰 차이는 GPU 쪽 문제가 아니라, 기본 쪽 캡처가 요청한 위치(900·1,800 px)와 다른 스크롤 위치에서 찍힌 것입니다 [사실, 그림]. 왜 다른 위치였는지는 모릅니다.**

| 그림 | 보이는 것 |
|---|---|
| `er-A-s0-crop.png` (기본) | 원뿔 그러데이션이 매끄럽게 돎 |
| `er-B-s0-crop.png` (GPU) | 같은 상자가 4등분 단색(가운데 십자 주변만 조금 번짐). 점선 테두리 상자는 같음 |
| `er-B-s1.jpg` (GPU, 900 px) | SVG·변형 구역이 바르게 보임. 여기의 돌아가는 상자(원뿔 그러데이션)도 4등분 단색 |
| `er-A-s1.jpg` (기본, 900 px 요청) | 표가 화면 맨 위 = 약 1,600 px 위치의 내용 |
| `er-B-s2.jpg` (GPU, 1,800 px) | 표 아래쪽과 작은 글씨 단 = 요청 위치와 맞음 |
| `er-A-s2.jpg` (기본, 1,800 px 요청) | 긴 목록 3~23번 = 훨씬 아래 위치의 내용 |

- 원뿔 그러데이션: VM(Parallels GPU)에서는 GPU로 그려도 매끄럽습니다(`gpucov/runs/vm-edgeraster-20261120-1340/er-B-s0.png`). 그래서 우리 드라이버 경로의 문제로 봅니다 [사실: 대조]. 각도를 구하는 셰이더 계산(atan 계열)이 사분면까지만 맞는 모양입니다 [가설]. 어느 단계(DXBC → NIR → AGX)인지는 아직 보지 않았습니다.
- 기본 쪽 스크롤 위치: 그림 내용은 깨지지 않았고 위치만 다릅니다. shot 0·3은 VM과 글자 수까지 같습니다. **원인 미상** [사실]. 재실행 `K52b`(13:48~13:52, 같은 맥미니, 2778 풀 부팅, 앞에 사람 없음)에서는 재현되지 않았습니다: 기본 쪽 캡처 4장이 VM 정상값과 글자 수까지 같고(175,488 / 133,048 / 256,520 / 167,560) shot 2 차이 0.000% [사실]. 첫 실행 때 콘치님은 Edge 창이 스크롤되는 것을 보고 계셨다고 이 세션에 말씀하셨고(시험의 60초 반복 구간), 입력을 넣으셨는지는 확인된 바 없으므로 사람 입력은 원인으로 적지 않습니다. 첫 실행(S268, 2777)과 재실행(S265 풀 켬, 2778)은 부팅 종류와 드라이버 판이 달라, 재현 여부는 같은 조건에서 여러 번 돌려 봐야 합니다.
- K52b에서 남은 차이 [사실]: shot 0의 616,432-968,552(원뿔 그러데이션 상자) 1.506%, shot 1의 640,56-880,440(돌아가는 상자의 원뿔 그러데이션과 SVG 점선 원) 1.530%. 그림 `nwoas_scripts/gpucov/runs/k53b-export-20261120-1352/`.
- 그림에 나쁜 조각·검은 칸·깨진 글자는 받은 6장 안에서는 없습니다.

## 24. 11-20 13:55~13:58 K54 셰이더 계산 시험 실기 결과 (2778, S268, `nwoas_scripts/gpupool-s276/runs/psmath-20261120-135512.txt`)

**결론: 픽셀 셰이더의 계산(min·max·나눗셈·비교·sqrt·sin·pow·atan·atan2 등)은 맞습니다. 틀린 것은 입력 하나, D3D11 픽셀 셰이더의 SV_Position.xy가 픽셀 중심(x+0.5)이 아니라 정수(x)로 들어오는 것입니다 [사실, 측정]. 원뿔 그러데이션이 네 조각으로 나오는 원인은 이 시험으로는 설명되지 않았습니다(23장에 적은 "atan 계산이 틀림" 가설은 기각).**

| 실행 | 통과 / 전체 | 비고 |
|---|---|---|
| AGX ARM64 | 18 / 64 | 실패 46개 모두 아래 설명과 맞음 |
| AGX x64 | 18 / 64 | ARM64와 같은 목록 |
| 같은 기계의 WARP | 64 / 64 | 시험 자체는 정상 |
| VM Parallels GPU · VM WARP (20장 뒤 점검) | 64 / 64 · 64 / 64 | `gpucov/runs/vm-psmath-20261120-1356/` |

근거 [사실]:
- `abs` (0,0): 받은 값 255, 기대 251. a = p.x/32 − 1에서 p.x = 0이면 255, 0.5면 251.
- `frac(a*4)`: 4,096픽셀 모두 16 모자람 = a가 1/64(반 픽셀) 작음.
- `atan2`·`atan_small`·`min_over_max`·`rsqrt_norm`의 8×8 지도가 "픽셀 중심 보정 없이 CPU로 계산한 값"과 글자 하나까지 같음(보정을 넣은 기대값과는 다름).
- 큰 오차(최대 240~255)는 a = 0 또는 |a| = |b|가 정확히 맞아떨어지는 줄(0/0, 부호 0, 같음 비교)에서만 나옴 = 반 픽셀이 빠진 결과.
- 입력 되읽기(초록·파랑)는 허용 오차 2 안이라 `bad_inputs=0`으로 나왔음(반 픽셀 = 255분의 2).

- 코드에서 본 것 [사실, 코드]: 반 픽셀을 더하는 `dxbc_nir_lower_fragment_position`(= `nir_lower_wpos_center`)은 D3D12 경로(`mesa/winsys/agx_d3d12_windows.cpp:3929`)와 `mesa/dxil/agx_owned_program_nir_native.c:26`에서만 불립니다. D3D11 경로가 왜 빠지는지는 추적 중 [가설 단계].
- 영향 [가설]: SV_Position을 쓰는 셰이더(화면 좌표로 무늬·디더·클립을 계산하는 것, 텍스처를 화면 좌표로 Load하는 것)가 반 픽셀 어긋납니다. 대부분의 그리기는 정점에서 넘긴 좌표를 쓰므로 눈에 띄지 않았을 수 있습니다.
- 원뿔 그러데이션: 계산이 맞으므로 원인은 다른 곳입니다. 다음은 GPU 그리기를 켠 Edge가 실제로 쓰는 셰이더를 꺼내(드라이버의 `APPLE_AGX_D3D_SHADER_DUMP`, GPU 샌드박스를 꺼야 파일이 써짐) 그 셰이더를 그대로 시험 프로그램에서 돌려 보는 것입니다.

## 25. 11-20 14시대: 반 픽셀 누락 자리(코드), K55 셰이더, K56 정점→픽셀 값 시험

**결론: (가) 반 픽셀 보정은 D3D11 경로의 셰이더 준비 단계 세 갈래 모두에 없습니다 [사실, 코드 추적]. (나) 원뿔 그러데이션은 "정점에서 넘어온 입력(v#)을 뒤집어(-) atan2에 넣는 식"에서만 틀립니다 [사실, 측정]. 같은 atan2를 임시값(r#)으로 하면 맞습니다(K54). 어느 명령이 틀리는지는 K57로 가르는 중입니다.**

### 25.1 반 픽셀 누락 자리 (보조 에이전트 읽기 전용 추적, Mesa 고정 커밋 `9aa1215f` 기준)
- asahi 컴파일러는 `load_frag_coord`를 0.5 없이 정수 픽셀 좌표로 바꿉니다(`nir_lower_frag_coord_to_pixel_coord`, `agx_compile.c:3700`에서 조건 없이 호출). 0.5는 앞단 책임입니다.
- D3D12 경로만 `dxbc_nir_lower_fragment_position`을 부릅니다(`mesa/winsys/agx_d3d12_windows.cpp:3926-3929`).
- D3D11 픽셀 셰이더 생성 `AgxD3d10CreateShaderState`(`mesa/scripts/build-native-asahi-state.py:3958-4018`의 패치 안)의 세 갈래 — ① DXBC→NIR 변환기(ps_5_0 등), ② TGSI(단순 ps_4_0), ③ 강제 표본 수 변형(`mesa/d3d11/agx_d3d11_ddi.cpp:1134-1151`) — 어디에도 그 단계가 없습니다.
- 고칠 자리 후보: ① `build-native-asahi-state.py:3976-3981`의 `if (nir) {…}` 안에 픽셀 셰이더일 때 `dxbc_nir_lower_fragment_position(nir)`, ③ `agx_d3d11_ddi.cpp:1134` 뒤, ② TGSI 갈래는 `tgsi_to_nir` 뒤에 같은 단계(넣는 자리 두 후보, 내부 TGSI 셰이더 영향 확인 필요). 빌드 스크립트의 결과 해시 고정 여부는 미확인. **아직 고치지 않았습니다.**
- 같은 종류로 D3D12에만 있는 단계(깊이 보지 않음): 표본 추출기 최소·최대 필터 감싸기, 슬롯 차원 수집, 보수적 래스터화 변형.

### 25.2 K55: Edge가 쓰는 셰이더 (`nwoas_scripts/gpucov/runs/k55-conic-dxbc-20261120-1402/`, 35개, `disasm.txt`)
- 원뿔 그러데이션 픽셀 셰이더 `4-6d91a6dc`: 입력은 v0(위치)와 v4.xy(noperspective)뿐, 짝 정점 셰이더 `0-73be0e55`는 o1~o4를 씀. 각도는 `min/max |r1.x|,|v4.y|` → 다항식 → `min/max r1.x,-v4.y` 등 fxc의 atan2 전개.
- 푸는 도구: `nwoas_scripts/gpucov/tools/dxbc_tokens_dis.py`.

### 25.3 K56 (14:05~14:07, 2778, S268, `nwoas_scripts/gpupool-s277/runs/psvary-20261120-140450.txt`)

| 경우 (크기 1·100 × linear·noperspective) | AGX arm64 | AGX x64 | WARP |
|---|---|---|---|
| 값 그대로 넘기기, 입력 전부 읽음 (4개) | PASS | PASS | PASS |
| 값 그대로 넘기기, 마지막 입력만 읽음 (4개) | PASS | PASS | PASS |
| 각도식 atan2(-uv.y, -uv.x) (4개) | **FAIL, 1,994 / 4,096 픽셀, 최대 오차 127** | 같음 | PASS |

- 그러므로 "넘어오는 값이 잘린다"는 가설은 기각 [사실].
- 각도식 지도: 왼쪽 위·오른쪽 아래 사분면은 기대와 같고, 오른쪽 위는 대각선 기준으로 뒤집혀 있으며, 왼쪽 아래는 0으로 나옵니다 [사실].
- fxc 출력 비교(`gpucov/runs/vm-fxc-atan2-20261120-1412/`): 실패한 식과 K54에서 통과한 식은 명령 순서가 같고, 다른 점은 피연산자가 입력 레지스터(`-v4.x`, `|v4.y|`)냐 임시 레지스터(`-r0.y`)냐뿐입니다 [사실]. 변환기의 피연산자 읽기(`dxbc_to_nir.c:467-476`)는 둘을 같게 다루므로 [사실, 코드], 그 뒤 단계(NIR 최적화나 AGX 백엔드)가 의심됩니다 [가설].

### 25.4 다음: K57 [실기 전]
- probe18 `psfile=<파일>`: 식을 글 파일로 받아 같은 프로세스에서 AGX와 WARP로 그려 픽셀끼리 비교. 식 파일은 작업이 직접 쓰므로 다음부터는 재부팅 없이 식만 바꿔 돌릴 수 있습니다.
- `gpucov/jobs/K57-psfile-neg-inputs.cmd`(18개 식: 뒤집기, 절댓값, min/max, 비교, atan2 변형). VM(Parallels 대 WARP)에서는 17/18 같고 `lt_abs`만 10픽셀(|x|=|y| 대각선의 동률) 차이 — 이 10픽셀은 맥미니 판정에서 빼고 봅니다. 기록 `gpucov/runs/vm-psfile-20261120-1425/`.

## 26. 11-20 14:11~14:13 K57 결과와 24·25장 정정 (`nwoas_scripts/gpupool-s278/runs/psfile-20261120-141052.txt`)

**결론: 24장의 "계산은 모두 맞다"와 25장의 "입력 레지스터일 때만 틀린다"는 틀린 판정이었습니다. 맞는 것은 이것입니다 — ps_4_0(TGSI 갈래)은 맞고, ps_5_0(DXBC→NIR 변환기 갈래)에서 atan·atan2가 틀립니다 [사실, 측정]. 원인 후보는 변환기가 MOVC(조건으로 고르기)의 마이너스 표시를 정수 방식으로 뒤집는 것입니다 [사실: 코드. 실기 확인은 K58].**

- 정정 근거 [사실]: K54 기록에서 `atan` ps_4_0 bad 432 대 ps_5_0 bad 2,067, `atan2` 105 대 2,006, `atan2_neg` 135 대 2,020. 24장을 쓸 때 ps_4_0 지도만 기대값과 대조했습니다. 반 픽셀 누락 판정(24장)은 두 판 모두에 해당하므로 그대로입니다.
- K57 [사실]: 18개 중 12개 같음(값 그대로, 뒤집기, 절댓값, min/max, 비율, 비교, 비교 두 개 묶기). `lt_abs` 10픽셀은 VM과 같은 대각선 동률. `atan2_pos`·`atan2_neg`·`atan2_neg_tmp`(임시값 경유)·`atan2_t0` 각 1,994픽셀, `atan_ratio` 2,008픽셀 다름. `atan_ratio` 지도는 y/x가 음수인 두 사분면만 0.
- fxc가 만든 `atan(y/x)`의 끝(`gpucov/runs/vm-fxc-atan2-20261120-1412/atanr.asm`): `min r0.x, r0.x, 1.0` → `lt r0.x, r0.x, -r0.x` → `movc r0.x, r0.x, -r0.y, r0.y`.
- 코드 [사실]: `mesa/dxbc/dxbc_to_nir.c:1514-1518` MOVC는 값 두 개를 `SRC(…, U)`(정수형)로 읽고, `read_src`(467-476행)는 정수형이면 마이너스를 `nir_ineg`, 절댓값을 `nir_iabs`로 적용합니다. 소수 값에 정수 부호 뒤집기를 하면 다른 값이 됩니다. MOV(1474행, 포화가 아닐 때)와 SWAPC(1611-1616행)도 같은 방식입니다.
- 원뿔 그러데이션과의 연결 [가설]: Edge 셰이더 `4-6d91a6dc`에 `movc r1.x, r1.x, l(1), -v4.x`와 `movc r1.x, r1.x, -r1.y, r1.y`가 있습니다.
- 다음: `gpucov/jobs/K58-psfile-movc.cmd`(재부팅 없음)로 확인 → 맞으면 세 명령의 표시를 소수 방식으로 적용하게 고친 2779(P44, UMD만 변경).

## 27. 11-20 K58 결과와 2779(P44) 빌드·시험 준비

**결론: 원인 후보가 실기에서 확인됐습니다 [사실]. 조건으로 고르는 명령에 마이너스나 절댓값 표시가 붙은 경우만 틀립니다. 변환기 세 군데를 고친 2779를 빌드했고 [빌드만], 실기는 아직입니다.**

| K58 식 (AGX arm64·x64 같음, WARP 기준) | 결과 | 다른 픽셀 / 최대 차 |
|---|---|---|
| sel_plain, sel_neg_same, movc_const, mul_sign, atan_abs | 같음 | 0 |
| movc_neg, movc_neg2 (고를 값 한쪽에 마이너스) | 다름 | 1,984 / 126 |
| movc_abs (고를 값 한쪽에 절댓값) | 다름 | 1,024 / 251 |
| atan_negabs | 다름 | 4,016 / 126 |
| atan_signed | 다름 | 2,008 / 126 |

- 기록: `nwoas_scripts/logs/agxmed1g-s249-20261120-141111.VgAssI.link/job-6.log`. VM(Parallels)에서는 10/10 같음.
- 고친 것과 빌드: `PATCHES.md` P44.
- 실기 준비 [실기 전]: `nwoas_scripts/file-delivery-s243/make-2779.py`, `agx-s248/jobs/I42-install-2779.cmd`, `R41-rollback-2778.cmd`, `gpucov/jobs/K52c-edge-raster-ab-2779.cmd`·`K53c-er3-export.cmd`(파일 이름 er3-*, 앞 결과를 덮어쓰지 않음), `gpupool-s279/run-2779-chain.sh`(시작 전 검사만 호스트에서 통과).
- 통과 기준: VC 30.0.2779.0, K58 arm64·x64 각 10/10, K57 17/18(`lt_abs` 대각선 동률 10픽셀만 남음), K56 12/12, K52c에서 원뿔 그러데이션 자리 차이 사라짐, 풀 시험·K43c·K50·K38g·K38dg·K41 회귀 없음. K54는 반 픽셀 누락 때문에 FAIL 줄이 계속 나옵니다(2779에서 고치지 않음). 다만 K54의 ps_5_0 atan 계열 숫자가 ps_4_0과 같아져야 합니다(atan 432, atan2 105, atan2_neg 135).

## 28. 11-20 14:26~14:48 2779 실기 결과 (`nwoas_scripts/gpupool-s279/runs/chain-2779-20261120-142642.txt`)

**결론: 2779는 통과 기준을 모두 만족했습니다 [사실]. Edge를 GPU로 그렸을 때의 원뿔 그러데이션·점선 원 차이가 사라졌고, 회귀는 없습니다. 2779를 그대로 둡니다.**

| 항목 | 2778 | 2779 |
|---|---|---|
| K58 고르기 10식 (arm64 · x64) | 5 / 10 · 5 / 10 | 10 / 10 · 10 / 10 |
| K57 단계별 18식 | 12 / 18 | 17 / 18 (`lt_abs` 대각선 동률 10픽셀만, VM과 같음) |
| K56 정점→픽셀 12가지 (arm64 · x64) | 8 / 12 · 8 / 12 | 12 / 12 · 12 / 12 |
| K54 ps_5_0 틀린 픽셀: atan / atan2 / atan2_neg | 2,067 / 2,006 / 2,020 | 432 / 105 / 135 (ps_4_0과 같아짐 = 반 픽셀 누락분만 남음) |
| Edge 기본 대 GPU 그리기, 다른 칸 (%) shot 0~3 | 1.506 / 1.530 / 0.000 / 0.012 (K52b) | 0.006 / 0.000 / 0.000 / 0.012 (K52c) → VERDICT PASS |
| 풀 큰 버퍼 8갈래 · 배치 | 0~1 실패 · OK | 0 실패 · OK (위 14, 아래 0) |
| K43c · K50 x86/x64/arm64 · K38g/K38dg 치명 · K41 · EL2 예외 | PASS · PASS · 0 · 0 · 0 | PASS · PASS · 0 · 0 · 0 |

- 그림 [사실]: `nwoas_scripts/gpucov/runs/k53c-export-20261120-1447/er3-B-s0-crop.png`에서 원뿔 그러데이션이 기본 쪽과 같이 매끄럽게 나옵니다.
- K52c에 남은 차이는 shot 0의 1칸(960,536, 가장 큰 칸 차 17)과 shot 3의 2칸(40,744-48,800, 차 20)뿐이며 shot 3의 것은 K52·K52b에도 같은 자리에 있었습니다. 무엇인지는 보지 않았습니다.
- 큰 버퍼 시험은 이번 한 부팅에서 0/8이었지만 2778의 2048 잔여(22장)는 고친 것이 아니므로 다시 나올 수 있습니다.
- 남은 것: 반 픽셀 누락(24·25.1장), 2048 잔여(22장), K52 첫 실행의 기본 쪽 캡처 위치 어긋남(원인 미상, 23장).

## 29. 11-20 2780(P45, 반 픽셀 보정) 빌드와 시험 준비 [실기 전]

**결론: 2780을 빌드했고 맥미니에서는 아직 돌지 않았습니다. D3D11로 그리는 모든 프로그램(DWM·Edge 포함)에서 SV_Position을 읽는 셰이더가 달라지는 변경이라, 첫 실기는 콘치님이 계실 때 돌립니다.**

- 고친 것·스위치·지문: `PATCHES.md` P45.
- 준비 [실기 전, 호스트에서 시작 전 검사만 통과]: `nwoas_scripts/file-delivery-s243/make-2780.py`, `agx-s248/jobs/I43-install-2780.cmd`(3,975 B), `R42-rollback-2779.cmd`, `gpucov/jobs/K59-pixel-center-ab.cmd`, `K52d-edge-raster-ab-2780.cmd`·`K53d-er4-export.cmd`(파일 이름 er4-*), `gpupool-s280/run-2780-chain.sh`.
- 통과 기준: VC 30.0.2780.0 / K59 보정 켬 arm64·x64 각 64/64, 한 프로세스만 끈 arm64 18/64(= 2779와 같음) / K58 10/10 두 번, K57 17/18, K56 12/12 세 번 / K52d VERDICT PASS / 풀 배치 OK / K43c·K50 3종·K38g·K38dg·K9·K10·K25b·K41 회귀 없음, EL2 예외 0.
- 눈으로 볼 것(콘치님): 바탕화면, 글자, 창 테두리·그림자, Edge 페이지가 전과 같게 보이는지. DWM 셰이더가 화면 좌표를 쓰면 반 픽셀 차이가 보일 수 있습니다 [가설].
- 되돌리기 두 단계: ① 레지스트리 `HKLM\SOFTWARE\NWOAS\AgxD3d` `D3d11PixelCenter`=0 뒤 Windows 재시작(드라이버는 2780 그대로, 동작은 2779), ② `R42-rollback-2779.cmd` 뒤 재시작.

## 30. 11-20 17:32~17:56 2780 실기 결과 (`nwoas_scripts/gpupool-s280/runs/chain-2780-20261120-173256.txt`)

**결론: 2780은 숫자로 보는 통과 기준을 모두 만족했습니다 [사실]. 반 픽셀 보정이 듣고(64/64), 스위치도 듣습니다(한 프로세스만 끄면 2779와 같은 18/64). 콘치님 눈 확인은 이 문서를 쓰는 시점에 아직 받지 않았습니다.**

| 항목 | 2779 | 2780 |
|---|---|---|
| 셰이더 계산 64식, 보정 켬 (arm64 · x64) | 18 / 64 · 18 / 64 | 64 / 64 · 64 / 64 |
| 같은 시험, 한 프로세스만 보정 끔 (arm64) | — | 18 / 64 (2779와 같음) |
| K58 · K57 · K56 | 10/10 ×2 · 17/18 · 12/12 ×3 | 같음 |
| Edge 기본 대 GPU 그리기, 다른 칸 (%) shot 0~3 | 0.006 / 0.000 / 0.000 / 0.012 | 0.000 / 0.000 / 0.000 / 0.000 |
| 같은 비교, 가장 큰 칸 차 shot 0~3 | 17 / 7 / 1 / 20 | 6 / 7 / 1 / 3 (VM Parallels의 6 / 9 / 1 / 3과 같은 수준) |
| 풀 큰 버퍼 8갈래 · 배치 | 0 실패 · OK | 0 실패 · OK (위 14, 아래 0) |
| K43c · K50 3종 · K38g/K38dg 치명 · K41 · EL2 예외 | PASS · PASS · 0 · 0 · 0 | PASS · PASS · 0 · 0 · 0 |
| K9 · K10 · K25b | (2779 연쇄에는 없음) | 23/23 ×4 · 13/13 ×2 + present 4/4 · 17/17 ×2 (지휘 세션 보고) |

- 2779의 Edge 비교에 남아 있던 3칸(shot 0의 1칸, shot 3의 2칸)이 2780에서 사라졌습니다. 그 차이는 반 픽셀 누락에서 온 것으로 봅니다 [가설: 같은 자리 대조로만 판단].
- 그림: `nwoas_scripts/gpucov/runs/k53d-export-20261120-1756/`.
- 남은 것: 콘치님 눈 확인(바탕화면·글자·창 테두리·Edge), 2048 잔여(22장), K52 첫 실행의 캡처 위치 어긋남(원인 미상, 23장).

## 31. 11-20 18:02 블루스크린 0x116 (2780, 평소 부팅 S268, 모니터 재연결 직후)

**결론: 블루스크린은 "GPU 응답 없음 판정 → 어댑터 재설정 → 다시 시작(RestartFromTimeout)이 STATUS_DEVICE_NOT_READY로 실패"입니다 [사실: 덤프·기록]. 다시 시작이 실패한 까닭은 화면 출력 쪽 박자(Timeline.Running)가 재설정 시점에 이미 꺼져 있었기 때문입니다 [사실: NwResetDisplay]. 박자가 왜 꺼졌는지(화면 출력 고장 표시)와 애초의 응답 없음 판정 원인은 [가설]입니다. 2780(반 픽셀 보정)과의 관련은 확인되지 않았고, 기제상으로는 화면 출력·하이퍼바이저 쪽 문제로 보입니다 [가설].**

| 근거 | 값 | 뜻 |
|---|---|---|
| 오류 검사 | 0x116 (…, AppleAgxRenderAdmission+0x2420, 0xc00000a3, 4) | VIDEO_TDR_FAILURE, 마지막 실패 상태 = DEVICE_NOT_READY |
| 덤프 호출 순서 (`gpucov/runs/k60-tdr-20261120-1802/cdb-bugcheck-stack.txt`) | TdrResetFromTimeoutWorkItem → DXGADAPTER::Reset → ADAPTER_RENDER::Reset → TdrBugcheckOnTimeout | 재설정·다시 시작 단계에서 드라이버가 실패를 돌려줌 |
| `NwResetDisplay[7]` | 0x4b9c0 = 309,696 ms (부팅 17:57 + 5분 10초 = 18:02) | 이번 사건의 기록. 이 값은 RestartFromTimeout이 고장 표시 검사를 지난 뒤에 써지므로 (가) 스케줄러 고장은 아님 |
| `NwResetDisplay[2]` | 0x0101000001010101 | 대기 중인 표시 있음, 인터럽트 켜짐, vsync 알림 켜짐, **타이머 꺼짐, Timeline.Running = 0**, Enabled = 1, 패널 보임 |
| `Wom1GpuFault` | 사건 1건: 종류 4(Timeout), fence 0x3347b, 프로세스 5152, 제출 뒤 4 ms, 다시 이어 줌(Resumed) | 펌웨어가 작업 하나에 시간 초과를 알림 |
| HV 기록 | 모니터 끊김 → 다시 연결 → modeset 314,892 us → 약 10초 안에 SYSTEM_RESET | `logs/agxmed1g-s249-20261120-175654.Pcy14C.vuart` 1594~1605행 |

- 코드 [사실]: `RestartFromTimeout`은 끝에서 `AdmissionScanoutSetTimelinePaused(FALSE)`를 부르고, 박자가 Running이 아니면 `STATUS_DEVICE_NOT_READY`를 돌려줍니다(`scanout_windows.c:2590-2594`). Running은 일시 정지·정지 중·**고장 표시(Faulted)** 중 하나면 0입니다(2562-2566행). Faulted는 화면 출력 인터럽트가 오류를 알릴 때 서고(2304행) 다시 내려가지 않습니다.
- HV 쪽 [사실, 코드 읽기만]: 화면 장치(`m1n1-gpu-hv2/src/hv_agx_scanout_broker.c`)는 표시(PRESENT) 완료가 실패하면 오류 인터럽트를 올립니다(512·544행).
- [가설] 모니터가 끊겼다 다시 붙는 동안 Windows가 보낸 표시 하나가 실패 → 드라이버가 화면 출력을 고장으로 표시(되돌아오지 않음) → vsync가 멈춰 응답 없음 판정 → 다시 시작이 고장 표시 때문에 거절 → 블루스크린. 표시가 그 순간에 겹치느냐에 따라 갈리므로 간헐적입니다.
- 과거 [사실]: 모니터 재연결은 11-16~11-20에 12번 이상 있었고 재연결 직후 재시작은 이번이 처음입니다. 같은 0x116 (c00000a3, 4)은 11-18 04:44·04:57·05:06에도 있었습니다(2780 이전).
- 다음: ① 콘치님과 재현 시험(2780 그대로 3번 → 죽으면 스위치 끄고 3번). ② 드라이버 쪽 고침 후보(설계 전): 다시 시작 때 화면 출력 고장 표시를 풀고 박자를 다시 켜기, 표시 실패 한 번을 영구 고장으로 다루지 않기. ③ HV 쪽(재연결 중 표시를 실패시키지 않기)은 실행기 담당 갈래.

## 32. 0x116 발단 후보: 하이퍼바이저의 재연결과 표시 반영 판정이 겹치는 경합 (코드 읽기, 11-20 저녁)

**결론: 발단 후보를 하이퍼바이저(HV) 코드에서 찾았습니다 [사실: 코드 / 실기 확인 전]. 재연결 modeset이 "반영된 표시 번호"를 자기 것으로 바꾸기 때문에, 그 직전에 반영 대기 중이던 Windows 표시는 반영 실패로 판정되고, HV 화면 장치는 그 뒤 모든 표시를 BUSY로 거절하는 상태에 남습니다. 그래서 드라이버만 고쳐서는(고장 표시를 풀고 다시 보내기) 화면이 돌아오지 않습니다. 고침은 HV 쪽이 먼저입니다. 드라이버 2781은 아직 만들지 않았습니다.**

- 재현 시험 A [사실, 지휘 세션 보고]: 2780(보정 켬, S268)에서 케이블 뽑기·꽂기 재연결 #2~#6 모두 정상, 재시작·EL2 예외 0. 재연결만으로는 재현되지 않음. 18:02 사고 때는 모니터 전원이 여러 번 흔들렸음(같은 충전기에 휴대폰을 꽂고 뺌).

| 순서 | 코드 (읽기만) | 내용 |
|---|---|---|
| 1 | `m1n1-gpu-hv2/src/display.c:1798-1803` `display_nwoas_svc_latch_poll` | 표시의 반영(latch)은 "반영된 표시 번호가 기대값과 같고 정해진 시간이 지났는가"로 판정. 번호가 다르면 -2 |
| 2 | `display.c:1454-1462` (재연결·modeset의 SWAP 단계) | modeset이 끝나면 `nwsvc.applied_swap_id`를 modeset의 표시 번호로 바꿈(사고 때 swap_id=56) |
| 3 | `hv_agx_scanout_service.c:297-307` | 판정이 실패면 `hv_agx_scanout_broker_fail_latch` → 오류 인터럽트. 횟수는 `latch_failures`에만 세고 기록에는 찍지 않음 |
| 4 | `hv_agx_scanout_broker.c:535-546` | 실패 때 `latched_sequence`를 올리지 않음 → applied ≠ latched로 남음 |
| 5 | `hv_agx_scanout_broker.c:154-158` | ACTIVE 상태에서 applied ≠ latched면 다음 PRESENT를 BUSY로 거절 → 풀어 주는 경로 없음 |
| 6 | 드라이버 `scanout_windows.c:2302-2307` | 오류 인터럽트 → 화면 출력 Faulted(영구), 박자 Running = 0 |
| 7 | 드라이버 `scanout_windows.c:2590-2594`, `scheduler_windows.c:1028` | 응답 없음 판정 뒤 다시 시작이 DEVICE_NOT_READY → 0x116 |

- 6·7은 31장에서 기록으로 확인된 부분이고, 1~5는 코드상 성립하는 경로입니다. 사고 부팅의 HV 기록에는 이 실패를 찍는 줄이 없어 직접 확인은 못 했습니다 [가설].
- 겹치는 조건: Windows 표시가 반영되기 전(반영 지연 시간 안)에 재연결이 시작되거나, 재연결 중에 표시가 들어온 경우. 화면이 자주 바뀌거나(Cinebench 등) 끊김이 여러 번일수록 가능성이 커집니다.
- 드라이버만 고칠 때의 한계: 고장 표시를 풀고 다시 보내도 5 때문에 HV가 BUSY로 거절합니다. 드라이버 쪽에서 되살리려면 화면 출력을 통째로 내렸다 올려야(RELEASE → REGISTER → 모드 → 표시) 합니다.
- 고침 후보: (HV-1) 재연결 modeset이 끝났을 때 반영 대기 중인 표시를 반영된 것으로 처리(그 modeset이 현재 화면을 다시 내보냈으므로). (HV-2) 반영 실패 때도 `latched_sequence`를 맞춰 다음 표시를 받게 함. (KMD) 오류 원인 기록 추가, 다시 시작 때 화면 출력 재등록.
- 하지 않은 것: `Wom1GpuReset` 기록 해석(해석 도구 `decode_wom1_gpu_fault.py`를 이 트리에서 찾지 못함), 11-18 새벽 3건의 상황 확인(인계 문서에 기록 없음).

## 33. 11-20 저녁: 하이퍼바이저 g262U(재연결·반영 판정 경합 고침) 빌드와 시험 준비 [빌드만]

**결론: 지휘 세션 결정에 따라 하이퍼바이저 쪽 고침을 새 판 g262U로 만들었습니다 [빌드만]. 일상 하이퍼바이저와 실행기는 바꾸지 않았고, 맥미니에서는 아직 돌지 않았습니다. 32장의 경합 자리는 더 정확히는 "재연결 도중에 들어온 표시(가짜 번호로 넘어감)가 재연결 modeset이 끝나는 순간 번호 불일치로 실패 처리되는 것"입니다 [사실: 코드].**

- 왜 화면이 가만히 있을 때는 살아남았나 [가설, 코드와 맞음]: 재연결(약 0.3초) 동안 Windows가 표시를 보내지 않으면 겹칠 것이 없습니다. 화면이 계속 바뀌는 중(Cinebench 등)이면 재연결마다 겹칠 가능성이 높습니다. 재현 시험 A(바탕화면만, 5번 정상)와 18:02 사고(전원이 여러 번 흔들림, Cinebench를 열었을 수 있음)가 이 설명과 맞습니다.
- 고친 것·빌드·호스트 시험·실기 시험 안: `nwoas_scripts/hv-latch-g262U/README.md`. 요약 — 바이너리 `out/m1n1-g262U-agx-hv2-latch.bin` 2,326,528 B sha256 `7ef111d7…9d7a`(두 번 빌드 같음), 호스트 시험 18/18(고치기 전 소스 16/18), 경고 44(g262T와 같음).
- 실기 작업: `nwoas_scripts/gpucov/jobs/K61-present-during-reconnect.cmd`(Edge가 150초 동안 화면을 계속 바꾸는 사이 케이블 5번 재연결). 판정은 재시작 없음 + 하이퍼바이저 기록의 `NWOAS-DCP-G262U … latched by modeset` 줄(경합이 실제로 일어났고 받아 냈다는 증거) + `latch MISMATCH` 줄 없음.
- 일부러 죽여 보는 대조 시험(고치지 않은 하이퍼바이저에서 같은 K61)은 블루스크린을 내는 시험이라 넣지 않았습니다. 필요하면 콘치님 결정으로 합니다.
- 드라이버 2781(오류 원인 기록, 다시 시작 때 화면 출력 복구)은 g262U로 부족할 때 진행합니다.

## 34. 11-20 20:2x~20:37 Cinebench 창 안의 검은 사각형·노이즈 (원인 미상, 재현 실패)

**결론: 원인을 찾지 못했습니다. GPU 응답 없음 복구·블루스크린 계열은 아니고 [사실: 기록], 같은 조건을 포함해 네 가지 방법으로 다시 만들어 보려 했지만 한 번도 나오지 않았습니다 [사실]. 2780 보정·g262U·부팅 설정 어느 것도 원인으로 확인되지 않았고, 원인에서 뺄 근거도 충분하지 않습니다. 드문 문제로 두고, 다시 보이면 그 부팅에서 바로 기록을 잡습니다.**

증상 [사실: 콘치님 사진 2장, 20:34·20:37]: 평소 부팅(S268, HV g262U, 페이로드 S261, 드라이버 2780 보정 켬)에서 `bench-cb26/run-cb-chain.sh` base가 Cinebench 2026 멀티→싱글을 돌리는 중, Cinebench 창 안에서만 — 왼쪽 패널 전체 검정, 순위표에서 가운데 아래로 이어지는 큰 검은 사각형, 순위표 가장자리의 색 점 노이즈와 겹친 글자. 작업 표시줄·제목줄은 정상. 마우스를 움직이자 풀림.

| 확인 | 결과 | 기록 |
|---|---|---|
| HV 기록(20:10 부팅) | 재연결·G262U 줄 없음, 표시 실패 0, 예외 0. `S252I scanout irq deferred` 3줄은 증상 없던 부팅(13:55에 8줄)에도 있음 | `logs/agxmed1g-s249-20261120-201041.QqBtRw.vuart` |
| K62 기록 수집(21:25, 다음 부팅) | 19:55 이후 4101·41·506·507 이벤트 없음, 드라이버 재설정 기록은 18:02 값 그대로, 새 커널 보고서 없음 | `gpucov/runs/k62-20261120-2123/` |
| K15 저장돼 있던 셰이더 18개 | 픽셀 셰이더 9개 중 SV_Position을 읽는 것 0개. 다만 Cinebench 것인지 미확인(Cinebench는 22개로 기록됨) | `gpucov/runs/k15-dxbc-20261120-2126/` |
| K63 싱글 7분, 보정 켬·끔 | 각 28표본, 왼쪽 패널 순검정 0 | `gpucov/runs/k63-20261120-2126/` |
| K64a 멀티 645초 → 싱글 1,270초, S280, 보정 켬 | 92표본 0. 남긴 창 그림 정상(캡처가 창 내용을 읽는 것 확인) | `gpucov/runs/k64a-20261120-2148/` |
| K66 원래 연쇄 그대로(S261, g262U, 2780) + 감시 | 93표본 중 본 화면 비정상 0(0.236 한 건은 720x480 시작 화면) | `bench-cb26/runs/chain-k66-20261120-222707/`, `gpucov/runs/k66-20261120-2227/` |
| K68 흰 창으로 3초 가렸다 치우기 10곳(가만히 있을 때 5, 싱글 중 5) | 가렸던 자리의 변화 0 | `logs/agxmed1g-s249-20261120-230439.b9t8LI.link/job-4.log` |

- 측정 방식: 화면(DWM 합성 결과)에서 Cinebench 창 자리를 읽어 왼쪽 패널의 순검정 비율을 셈. 정상 패널은 0. 사진의 깨짐은 창 안이므로 이 방식에 잡힐 것으로 보지만, 깨진 상태를 실제로 잡아 본 적은 없습니다 [가설].
- 기각된 가설: "입력이 없어 화면이 꺼진 것"(사진에서 작업 표시줄이 정상), "7분 싱글·멀티→싱글 순서·S261 설정이면 난다", "다른 창이 가렸다 사라지면 난다"(K68, 흰 창 3초로는 안 남).
- 남은 차이 [사실]: 증상 때는 사람이 맥미니 앞에 있었고(화면을 보고 사진을 찍고 마우스를 움직임), 재현 시도는 모두 무인이었습니다. 그 사이 무엇이 화면에 있었는지는 기록이 없습니다.
- 다시 보이면: 그 부팅에서 재시작 전에 `gpucov/jobs/K62-black-screen-evidence.cmd`(기록 + 화면 캡처)를 넣습니다. Cinebench 연쇄에는 `K66s`/`K66r`(창 감시)을 붙여 둘 수 있습니다.

## 35. 11-20 하루 요약 (GPU 갈래)

| 판·항목 | 한 일 | 실기 결과 | 상태 |
|---|---|---|---|
| 2778 (P43) | CPU가 보는 버퍼를 풀 위쪽부터 놓기를 기본으로 | 두 부팅 16갈래 중 15 통과(2048 한 번 실패), 회귀 없음 (22장) | 설치됐다가 2779·2780으로 교체. 2048 잔여는 구조 변경 필요, 설계 전 |
| 2779 (P44) | DXBC→NIR 변환기: mov·movc·swapc의 -x·\|x\| 표시를 소수 방식으로 | 통과. Edge GPU 그리기의 원뿔 그러데이션·점선 원 정상 (28장) | 2780에 포함 |
| 2780 (P45) | D3D10/11 SV_Position 반 픽셀 보정(세 갈래), 스위치 `D3d11PixelCenter` | 숫자 기준 통과(계산 시험 64/64, Edge 비교 차이 0), 콘치님 눈 확인 통과 (30장) | 현재 설치 판 |
| Edge GPU 그리기 시험 | `edgeraster1.ps1`·`raster1.html`, K52 계열 | 2780에서 기본 대 GPU 그리기 차이 0 | 도구 완성. 첫 실행(13:11)의 기본 쪽 캡처 위치 어긋남은 원인 미상, 이후 4회 재현 안 됨 (23장) |
| 시험 프로그램 | probe16 psmath, probe17 psvary, probe18 psfile(식을 글 파일로 받아 AGX 대 WARP 비교) | 맥미니에 전달됨 | 다음 셰이더 문제에 그대로 사용 가능 |
| 0x116 (18:02) | 덤프·기록 분석 (31장), HV 경합 자리 (32장) | 다시 시작이 화면 박자 꺼짐으로 거절된 것 확정. 발단은 HV의 재연결·반영 판정 경합으로 추정 | — |
| HV g262U | 재연결이 대신 내보낸 표시를 반영 처리, 반영 실패 뒤에도 다음 표시 받기, 기록 줄 (33장) | 20:10 이후 평소 부팅에 쓰여 화면 표시 정상. **재연결 중 표시가 겹치는 시험(K61)은 아직 안 함** | K61은 콘치님 손(케이블 5번)이 필요 |
| Cinebench 창 깨짐 | 34장 | 재현 실패 | 원인 미상 |

남은 일(이 갈래): K61(g262U 검증), 드라이버 쪽 화면 출력 복구(2781, g262U로 부족할 때), 2048 잔여의 구역 분리 설계, 그리고 처음에 받은 큰 과제(DDisplay, 복사 엔진, 동영상 하드웨어 경로, OpenGL·Vulkan, 풀을 평소 실행기에 넣기)는 오늘 손대지 못했습니다.

## 36. 풀(구역 3, 4 GiB)을 평소 실행기에 넣을 때의 설정값과 판정 시험 (11-21, 정리만)

**결론: 실행기에서 바꿀 값은 하나, `NWOAS_AGX_POOL_IPA=0xd00000000`입니다 [사실: S265와 S268 실행기 비교]. 하이퍼바이저·페이로드·드라이버는 그대로 쓸 수 있습니다. 다만 풀은 지금까지 g254L 하이퍼바이저 + S259 페이로드(블루투스·전원 모듈·SSD 동시 처리 없음) 조합에서만 돌았고, 평소 조합(g262U + S280 + 블루투스 + 전원·SSD 모듈)과 함께 돈 적은 없습니다 [사실]. 그래서 합친 첫 부팅은 새 조합의 첫 실기입니다.**

| 항목 | 값 | 근거 |
|---|---|---|
| 풀 주소 (실행기 환경 변수) | `NWOAS_AGX_POOL_IPA=0xd00000000` (끄면 0), 크기 4 GiB 고정 | `agxdcp-s265-guest-test.sh:79-85`, `agxdcp-s268-guest-test.sh:99`, `tools-hv2/agx_hv2_module.py:29` |
| 주소 겹침 | 없음. 평소 실행기 모듈들이 쓰는 9자리 이상 주소: 0x300000000(브로커), 0x340000000·0x348004000·0x3C0000000(소리·블루투스), 0x6C0000000, 0xa00000000(로컬 예약 1 GiB), 0xF00000000~0xF20000000. 풀은 0xd00000000~0xdffffffff | 각 모듈 소스의 상수 |
| 하이퍼바이저 | 바꿀 것 없음. 풀 창 경로의 NULL 방어(g248C)는 g249D 이후 모든 판에 들어 있음(g262R·T·U 포함) | `RESULT-POOL-S265.md` 1장 |
| 페이로드·ACPI | 바꿀 것 없음. 풀은 Windows RAM을 쓸 때만 가져오고 ACPI는 크기를 정하지 않음 | 같은 문서 2.1장 |
| 드라이버 레지스트리 | `G3Armed=1`(D28이 매 부팅 설정), `NwSoftPoolCached=1`, `NwSoftOverlay=0`(설치 작업이 설정), `Wom1PoolCpuVisibleTopDown` 없음(= 켜짐, 2778 이후) | `agx-s248/jobs/D28-rearm.cmd`, `I43-install-2780.cmd` |
| 부팅 뒤 한 번 | D28(장치 재시작) — 평소 래퍼 `bt-s268/run-s268.sh:54`가 이미 넣음 | — |
| 부팅 때 볼 줄 | `NWOAS-AGX: pool window v1 IPA=0xd00000000 bytes=0x100000000 broker+0xe00`, `NWOAS-AGX: MEDIATE armed: … local=1 pool=1 …` | 17:36 풀 부팅 기록 |
| Windows가 잃는 RAM | 처음에 약 28 MiB(하이퍼바이저 표), 그 밖은 앱이 GPU 메모리를 쓸 때만 | `RESULT-POOL-S265.md` 1장 [계산] |

지금까지 풀이 돈 조합 [사실]: 오늘의 풀 연쇄(2775~2780)는 모두 `NWOAS_S262_HV=0`(g254L)로 돌았고 실행기는 S259 계열(S265)입니다. g262R 이후 판, 전원 모듈(WFI·보조 CPU 틱), SSD 동시 처리 모듈, 블루투스 모듈, S280 페이로드와는 같이 돈 적이 없습니다.

합친 뒤 달라지는 것:
- D3D12가 켜집니다(풀이 없으면 Edge 정보에 `dx12FeatureLevel=Not supported`). D3D12를 쓰는 앱이 그 경로를 타기 시작하므로 새 문제가 드러날 수 있습니다 [가설].
- 큰 버퍼(2 GiB대)를 연달아 만드는 앱은 드물게 장치 제거를 겪을 수 있습니다(22장의 2048 잔여, 40갈래 중 1).
- 드라이버 설치는 지금까지 풀 끈 부팅에서 했습니다(사용 중 재시작이 `CM_PROB_FAILED_POST_START`로 실패하기 때문, 13장). 설치 연쇄는 지금처럼 풀 끈 부팅을 쓰면 됩니다.
- 풀 켠 부팅의 종료는 드라이버 정지 경로가 풀 창 명령을 지나는 자리입니다. 종료 때 EL2 예외를 봐야 합니다(`boot-s265.sh`가 하던 검사).

판정 시험(합친 실행기의 첫 부팅과 둘째 부팅):

| 순서 | 시험 | 통과 기준 |
|---|---|---|
| 1 | 부팅 기록 | 위 두 줄(`pool window …`, `pool=1`), `install REFUSED` 없음 |
| 2 | D28 | `DEV OK`, `Wom1StartStage = 12`, `Wom1StartStatus = 0` |
| 3 | K48 + K45t | 8갈래 실패 0~1, 이번 부팅 프로세스의 CPU가 보는 버퍼가 모두 2,000 MiB 위(`run-2780-chain.sh`의 `legs` 판정식) |
| 4 | K43c, K50 | 채우기 14/14·D3D11 1,024 MiB PASS, x86·x64·arm64 PASS |
| 5 | K59, K58, K57, K56 | 64/64 ×2·18/64, 10/10 ×2, 17/18, 12/12 ×3 (2780에서와 같음) |
| 6 | K38g, K38dg, K52(er*-이름 새로), K41 | 치명 0, VERDICT PASS, dwm 0 |
| 7 | K9, K10, K25b | 23/23 ×4, 13/13 ×2 + present 4/4, 17/17 ×2 |
| 8 | 평소 기능(지휘 세션 쪽 작업) | 와이파이 연결, 블루투스, SSD 검증, 소리 — 풀 끈 평소 부팅과 같음 |
| 9 | 재시작(W4) | 종료 때 HV EL2 예외 0, 다음 부팅에서 1~2 다시 통과 |
