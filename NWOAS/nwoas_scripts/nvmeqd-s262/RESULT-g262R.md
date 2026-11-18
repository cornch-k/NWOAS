# S262 g262R — ANS 인터럽트 찾기 수정과 "인터럽트 없으면 엔진 끔" (2026-11-19 03시)

표기: **[사실]** = 파일·기록·이 맥북에서 실제로 돌린 결과로 확인한 것, **[가설]** = 확인하지 않은 추측, **[빌드만]** = 빌드·호스트 시험까지만 된 것.

## 0. 결론

1. **g262Q가 ANS 인터럽트를 못 찾은 원인은 ADT 속성 길이 검사였습니다.** J274 ADT의 `/arm-io/ans` `interrupts`는 5칸(20바이트)인데 g262Q는 4바이트(1칸)일 때만 받았습니다. 5번째 칸 590이 NVMe 완료 인터럽트입니다. [사실]
2. **QD만 주고 엔진이 인터럽트 없이 켜지면 절반으로 느려진 원인은, 완료를 200 µs 틱에서야 거뒀기 때문입니다.** g262Q는 인터럽트를 못 찾아도 엔진을 켰고(틱 조회), 명령 하나가 끝나서 Windows에 알리기까지 평균 92 µs → 215 µs로 늘었습니다. [사실: 계수기] 그 늘어난 시간이 틱 대기라는 것은 [가설](틱 주기와 수치가 맞음, 거둔 위치 계수기는 기록이 없음).
3. **mark 기록도 고장 나 있었습니다.** 작업 중 동시 처리 계수기가 한 번도 기록되지 않아(`nvmeqd.jsonl` 부팅 1줄), 결과표의 `inflight_max=0`과 "안전 계수기 모두 0"은 부팅 직후 값이었습니다. [사실: 같은 방식으로 재현]
4. **g262R은 세 가지를 고쳤습니다.** ① 5칸이면 5번째 칸을 씀(실제 덤프로 590 확인), ② 인터럽트를 못 얻으면 엔진을 켜지 않고 아무것도 준비하지 않음 = QD를 끈 것과 같은 경로, ③ mark 기록 연결. [빌드만]
5. **디스크 오류 판정을 바꿨습니다.** `Kernel-Boot` 153은 부팅마다 1건 나오는 "가상화 기반 보안 상태" 정보 이벤트이고, 디스크 오류 153은 `disk` 제공자의 "입출력 다시 시도" 경고입니다. 이제 제공자와 번호를 함께 보고 셉니다. [사실: Microsoft 문서·실기 기록]
6. **맥미니에서는 아무것도 하지 않았습니다.** g262R이 실제로 590 인터럽트로 동작하는지, 속도가 오르는지는 모릅니다.

---

## 1. 왜 ADT에서 ANS 인터럽트를 못 찾았는가

결론: g262Q의 조건 `len != 4`가 5칸짜리 실제 속성을 버렸습니다. 노드 경로·부모·번호 체계는 맞았습니다. [사실]

찾는 쪽 [사실, g262Q `nvme.c` `nvme_qd_find_irq`]:
```
irqs = adt_getprop(adt, "/arm-io/ans", "interrupts", &len);
if (!irqs || len != 4 || !irqs[0] || irqs[0] >= 0x1000) -> "polling only", return 0
```
Python 모듈은 ADT를 읽지 않고 HV 답(`flags`의 IRQ 비트)만 봅니다.

실제 ADT [사실, `nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin`, sha256 `32e63706…`, iBoot-8422.141.2]:

| 노드 / 속성 | ADT 안 위치 | 크기 (B) | 원래 바이트 | 값 |
|---|---|---|---|---|
| `/arm-io/ans` `interrupts` | 0x3a1a0 | 20 | `48020000 47020000 4a020000 49020000 4e020000` | 584, 583, 586, 585, 590 |
| `/arm-io/ans` `interrupt-parent` | 0x3a0cc | 4 | `5f000000` | 0x5f |
| `/arm-io/aic` `AAPL,phandle` | 0x2c8f8 | 4 | `5f000000` | 0x5f (부모 = AIC 맞음) |
| `/arm-io/ans` `reg` 4번째 범위 | 0x3a2b4 | 0x70 중 | `00 00 cc 7b …` | 0x27bcc0000 (NVMe 레지스터) |

번호 대조 [사실, Linux `arch/arm64/boot/dts/apple/t8103.dtsi`]:

| Linux 장치 | AIC 번호 | ADT 칸 |
|---|---|---|
| `ans_mbox` (send-empty, send-not-empty, recv-empty, recv-not-empty) | 583, 584, 585, 586 | 1~4번째 (순서만 584, 583, 586, 585) |
| `nvme@27bcc0000` (`apple,nvme-ans2`) | 590 | 5번째 |

README가 예상한 590은 맞았고, 받는 조건만 틀렸습니다. 이 맥북에서 g262R 코드를 실제 덤프에 돌린 결과: `g262R choice: reason=0 irq=590 (cell 4 of 5)`, 같은 바이트에 g262Q 조건: `unusable -> polling only`. [사실: `test_ansirq_g262R`]

참고: 이 덤프는 11-16 것입니다. 지금 맥미니 부팅의 ADT도 같은 모양인지는 실기에서 `[S262] NVMe QD engine ON … ANS irq=590 (… cell 4 of 5)` 줄로 확인합니다. [가설: 같은 iBoot이면 같음]

---

## 2. 왜 QD만 줘도 절반으로 느려졌는가

결론: 인터럽트가 없어 `flags=0x0`이 됐지만 엔진 자체는 켜졌고(`depth=32`, `eng_active=1`), 그 엔진은 명령을 넣고 기다리지 않은 채 Windows로 돌아갔다가 완료를 다음 문 두드리기나 CPU0 틱에서야 거둡니다. Windows가 명령을 한 번에 하나씩만 넣고 있어서 다음 문 두드리기가 오지 않았고, 결국 틱을 기다렸습니다. [사실 + 가설, 아래 구분]

### 2.1 계수기 비교 (S260 계수기 `hvstats.jsonl`, `qd-io-begin` → `qd-io-end` 구간 = NWPWR io 40초) [사실]

| 부팅 | 설정 | 명령 수 | 실행 시간 평균 (µs) | 문 두드리기→완료 평균 (µs) | 20~100 µs 비율 (%) | 100~500 µs 비율 (%) | 완료 때 남은 명령 평균 | 틱 (회/s) |
|---|---|---|---|---|---|---|---|---|
| 021750 | QD 끔 | 312,985 | 91.9 | 92.1 | 89.2 | 3.7 | 1.000 | 3,686 |
| 022051 | QD=8 (엔진 켜짐, 인터럽트 없음) | 157,102 | 215.2 | 217.8 | 0.2 | 91.1 | 1.003 | 4,896 |
| 022351 | QD=32 (엔진 켜짐, 인터럽트 없음) | 157,337 | 214.6 | 217.2 | 0.2 | 90.9 | 1.004 | 4,899 |

- "실행 시간"은 엔진 경로에서 ANS에 넣은 때부터 거둔 때까지, 옛 경로에서 문 두드리기 처리 시간입니다. 틱 주기는 `hv_tick_interval 4800` / 24 MHz = 200 µs입니다.
- 완료 때 남은 명령 평균이 QD8·QD32에서도 1.00입니다: HV 안에는 늘 명령 1개만 있었습니다. 엔진 경로의 최댓값(93·127)은 부팅 중 값입니다.
- 결과표 IOPS와 맞춰 보면, QD1 8,360 → 4,289는 명령당 120 → 233 µs이고 위 지연 증가(+125 µs)와 거의 같습니다.

### 2.2 QD>0일 때 g262Q에서 달라지는 경로 전체 (flags=0이어도) [사실: 코드]

| 경로 | QD 끔 | QD>0, flags=0 (g262Q) | 영향 |
|---|---|---|---|
| SQ 문 두드리기 (`hv_vm.c` 0x1008) | 명령 1개를 그 자리에서 끝까지 실행하고 완료를 씀 | 엔진: 거두기 → 모두 ANS에 넣고 바로 돌아감 | **완료가 다음 문 두드리기·틱까지 밀림 (절반의 원인)** |
| CQ 문 두드리기 (0x100c) | 머리 갱신만 | + 엔진 거두기·채우기 | 작음 |
| 5 kHz 틱 (`nwoas_nvme_fastpath_poll`) | 밀린 명령 1개 처리 | 엔진: 거두기·채우기·1초 시간 초과 검사(64칸)·`nvme_poll_syslog` | 거둘 기회는 여기뿐, CPU0 부담 약간 |
| m1n1 자체 I/O 큐 명령 (`nvme_exec_command(&ioq)`) | 태그 0 | `nvme_qd_exec_sync` 태그 1 | 같은 동기 방식, 작음 |
| 부팅 전 준비 (`nvme_qd_setup`) | 없음 | PRP 256 KiB 할당 | 성능 영향 없음 |
| CQ 다시 만들기 | 없음 | 인터럽트가 없어 하지 않음 | 없음 |
| HV 인터럽트 가로채기 (`hv_exc.c`) | 없음 | 줄 번호 0이라 하지 않음 | 없음 |
| 큐 끄기·다시 걸기 (동작 0·1) | — | `nwoas_qd_drain` (진행 중 0이면 바로 끝) | 없음 |

[가설] 늘어난 125 µs는 틱 대기입니다(장치 약 92 µs 끝난 뒤 다음 틱까지 평균 100 µs + 처리). 어디서 거뒀는지 세는 `done_tick`·`done_sqdb` 계수기는 3장의 mark 고장 때문에 기록이 없어 직접 확인은 못 했습니다.

[가설] Windows가 QD8·QD32에서도 한 번에 하나씩만 넣은 이유는 모릅니다(측정 도구 NWPWR는 겹치기 읽기 8·32개를 먼저 냅니다, `power-s260/nwpwr.c:307`). 인터럽트가 붙어도 이것이 그대로면 무작위 4K 목표(QD8 25,000)에는 못 갑니다. g262R 실기의 `inflight mean`으로 확인합니다.

---

## 3. mark 기록이 안 된 원인

결론: `nvmeqd_module.py`가 `_pwr_mark`를 바꿔 끼운 곳이 `power_module.py`가 실제로 찾는 곳이 아니었습니다. [사실]

- `HV.run_script()`는 스크립트마다 `runpy.run_path(init_globals=shell_locals)`로 새 이름공간을 만듭니다(`m1n1/hv/__init__.py:2394`). `_pwr_receive`는 자기 이름공간의 `_pwr_mark`를 부르므로, 뒤에 실행된 모듈이 바꾼 이름은 닿지 않습니다.
- 같은 호출 방식으로 이 맥북에서 재현: 바꿔 끼운 함수가 불리지 않음(`pwr x`만 찍힘, `same dict? False`).
- 실기 기록: `logs/agxmed1g-s249-20261119-022351.6zuhBE.link/nvmeqd.jsonl`은 `boot` 1줄뿐입니다.
- g262R 모듈은 `_pwr_receive.__globals__['_pwr_mark']`를 바꿉니다. 가짜 HV로 돌린 시험에서 작업 mark가 기록됐습니다(2줄, 전력 mark도 그대로 불림).

---

## 4. g262R에서 바꾼 것

결론: HV 4개 파일, 실행기 모듈, 실행기 선택, 단계 스크립트, Q4 작업을 바꿨습니다. 기존 바이너리·패치는 건드리지 않았습니다. [빌드만]

| 무엇 | 내용 |
|---|---|
| ANS 줄 고르기 (`nwoas_ans_irq.h`, 새 파일) | 1칸 → 그 칸, 5칸 → 5번째, 그 밖은 거부. 부모가 AIC phandle이어야 함. 5번째가 앞 4개와 같거나 0·AIC 범위 밖이면 거부 |
| `nvme_qd_probe_irq()` (nvme.c) | ADT만 읽는 함수. 결과를 보고 엔진을 켤지 정함 |
| `nvme_qd_setup(depth, irq)` | 이미 찾은 줄을 받음 |
| 설정(동작 24) | 인터럽트를 못 얻으면 PRP 할당·CQ 다시 만들기 없이 "엔진 꺼짐 + 사유"로 답함. `NWOAS_NVME_QD_POLL=1`일 때만 틱 조회 엔진 |
| 인터럽트가 멈춘 것처럼 보일 때 | 그 부팅 동안 엔진을 끄고 한 번에 하나 경로로 돌아감(`QD_POLL=1`이면 g262Q처럼 틱 조회) |
| 기록 줄 | 실행기 로그 `[S262] NVMe QD engine ON: … ANS irq=590 (ADT /arm-io/ans interrupts cell 4 of 5)` 또는 `[S262] NVMe QD engine OFF (…): reason N: … -> synchronous path, same cost as QD off` |
| 계수기 `flags` | 비트 10 = 멈춘 인터럽트로 엔진 끔, 비트 16~23 = 꺼진 사유, 비트 24 = 엔진 켜짐 |
| 인터페이스 판 | 3 (모듈이 3만 받음 — g262Q 바이너리와 섞어 쓰면 모듈이 멈춤) |
| 실행기 (`agxdcp-s259-guest-test.sh`) | `NWOAS_S262_HV=1` → g262R, 크기·해시 고정, 모듈 해시 고정, `QD_POLL` 표시. 지금 돌고 있는 와이파이 관찰 실행기가 옛 파일을 읽고 있어, 새 파일로 바꿔 끼우는 방식(이름 바꾸기)으로 교체했습니다 |
| 단계 스크립트 (`run-s262-steps.sh`) | 엔진 상태 줄 기록, `nvmeqd.jsonl` 2줄 미만이면 안전 실패, Q4에서 `chkdsk_rc=0`과 `DISKEVT_COUNT 0`을 판정 |
| Q4 작업 (`jobs/Q4-chkdsk.cmd`) | 제공자+번호로 셈: `disk` 7·11·51·153·154·157, `stornvme`/`storport` 129, `Ntfs` 50·55·140. `Kernel-Boot` 153은 `BOOT153_COUNT`로 따로 |

디스크 이벤트 근거 [사실]:
- Microsoft Learn "Troubleshoot data corruption and disk errors": 153 = "The IO operation at logical block address … was retried"(저장 드라이버 시간 초과), 129 = "Reset to device", 55·98·140 = NTFS.
- `Microsoft-Windows-Kernel-Boot` 153 = "Virtualization-based security (policies: …) is …"(이벤트 제공자 목록 기준). 실기에서도 12건이 모두 부팅 시각에 1건씩이었습니다(`runs/steps-20261119-021731.txt` 1098~1110줄).
- Q4의 PowerShell 줄은 Windows에서 돌려 보지 않았습니다. [빌드만]

---

## 5. 빌드·시험 결과

결론: 두 번 깨끗하게 빌드해 같은 해시가 나왔고, 호스트 시험 두 개가 모두 통과했습니다. [사실: 이 맥북] 맥미니 동작은 미확인입니다.

| 파일 | 크기 (B) | sha256 |
|---|---|---|
| `port-j274/gpu/hv/out-hv2/m1n1-g262R-agx-hv2-nvmeqd.bin` | 2,326,528 | `83add623acbd7658449e84b20d664205ac085ae92e841bf0fab47c5554af89d5` |
| `…/m1n1-g262R-agx-hv2-nvmeqd.elf` | 3,649,792 | `191007a23490ef3f9c7921242d7e0d09123590c43bb1ca5163570e0de7844915` |
| `port-j274/gpu/hv/hv2-g262Q-to-g262R.patch` | — | `852c10f0817415a9a74d788734298d9292a88e4c1894d8caddc55c2e00b7317b` |
| `nwoas_scripts/nvmeqd-s262/nvmeqd_module.py` | — | `561579ac89b632e459d98a964e84db2ba333026b6ee494ed9ba04161f72401b8` (g262Q 때 `5a3939eb…`) |

| 시험 | 결과 |
|---|---|
| `build-g262R.sh` 재현 빌드 (새 폴더 2회) | 같은 해시 2회, 경고 44개(g262Q와 같은 수, 새 코드에서 0개) |
| `test_nvmeqd_g262R` (g262Q 시험 전체 + 엔진 끔 규칙 7가지) | PASS, 실패 0 |
| `test_ansirq_g262R` (실제 J274 ADT + 모양 9가지) | PASS, 590 (5칸 중 5번째) |
| 모듈 가짜 HV 시험 (켜짐 590 / 꺼짐 칸 수 / 꺼짐 IRQ=0 / QD 없음) | 줄 모두 예상대로, 작업 mark 기록됨 |
| 실행기·단계 스크립트 `bash -n` | 통과 |

---

## 6. 실기 다시 돌리기 (콘치님 손 필요 없음, 맥미니를 다시 부팅함)

결론: 지금 돌고 있는 와이파이 관찰이 끝난 뒤, 그 부팅의 `.out`과 `.link`를 넣어 아래 한 줄을 돌립니다. 스크립트가 단계마다 맥미니를 다시 부팅하고, 실패하면 옛 경로로 되돌립니다.

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts && bash nvmeqd-s262/run-s262-steps.sh <out> <link>
```
(지금 부팅이면 `<out>` = `logs/launcher-agxdcp259-wifi-20261119-023704.out`, `<link>` = `logs/agxmed1g-s249-20261119-023704.LwlSqu.link`)

통과 기준:

| 항목 | 기준 |
|---|---|
| 엔진 상태 (QD 단계) | `ENGINE: [S262] NVMe QD engine ON: … ANS irq=590 (ADT /arm-io/ans interrupts cell 4 of 5)` |
| 엔진이 꺼진 경우 | `engine OFF … reason N` 줄과 함께, 속도가 1단계(QD 끔)의 90% 이상 (= 비용 없음 확인) |
| 무결성 | 매 단계 `INTEG PASS`·`integ_rc=0`, 재시작 3회 `verify_rc=0` |
| 안전 | `nvmeqd.jsonl` 2줄 이상, `SAFETY_COUNTERS all 0`, HV 로그에 `QD fault`·`REFUSED write outside` 없음 |
| 끝 | `chkdsk_rc=0`, `DISKEVT_COUNT 0` (`BOOT153_COUNT`는 부팅 수와 같으면 정상) |
| 속도 목표 [가설] | QD1 ≥ 1단계의 90%, QD8 ≥ 25,000 IOPS, QD32 ≥ 35,000 IOPS, `inflight mean` > 1 |

---

## 7. 위험 [모두 가설]

1. **부팅 전에 ANS I/O 큐를 인터럽트 켠 상태로 다시 만드는 일이 이번에 처음 실제로 일어납니다**(g262Q는 줄을 못 찾아 건너뜀). 실패하면 모듈이 멈추고 게스트를 시작하지 않습니다. 그때는 `NWOAS_NVME_QD`를 빼고 다시 부팅하면 옛 경로입니다.
2. 590 줄이 CQ 문 두드리기 뒤 내려가지 않거나, 관리 큐 완료도 같은 줄을 올리면 빈 인터럽트가 쌓여 엔진이 꺼집니다(그때 `storm` 계수기가 1이 되어 단계 스크립트는 안전 실패로 멈춥니다).
3. Windows가 한 번에 하나씩만 넣는 상태(2장)가 그대로면 인터럽트가 붙어도 QD8·QD32 목표에 못 미칩니다.
4. 시간 초과한 태그는 다시 쓰지 않지만, ANS가 늦게 그 버퍼에 쓰면 Windows 메모리가 상할 수 있습니다(g262Q와 같음).
5. **S264 트리(`m1n1-standalone-s264/`)는 g262Q 바탕이라 같은 `len != 4` 조건을 그대로 갖고 있습니다**(`src/nvme.c:835`). `hv2-g262Q-to-g262R.patch`는 그 트리에 그대로 올라갑니다(시험 적용만 함, 트리는 고치지 않음). 다만 S264의 C 실행기는 동작 24(QD 설정)를 부르지 않으므로, 맥북 없이 켜기에서 동시 처리를 쓰려면 그 호출도 따로 옮겨야 합니다.
