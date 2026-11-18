# S262 — SSD 명령 여러 개 동시 처리 (로드맵 5장 S-2, 하이퍼바이저 g262Q)

**상태: 빌드와 이 맥북에서의 호스트 시험만 했습니다. 맥미니에서는 한 번도 실행하지 않았습니다.** 빌드 성공은 동작 성공이 아닙니다.

## 결론
- g262Q = g260O + 동시 처리 경로(g262P + 설정 거부 원인 수정). `NWOAS_NVME_QD`를 주지 않으면 g260O와 같은 "한 번에 하나" 경로로 돕니다(같은 바이너리로 A/B).
- 켜면 하이퍼바이저가 Windows 명령을 복사·검사(검사 내용은 g260O와 같음)한 뒤 ANS 태그 하나씩에 넣고 **기다리지 않고** Windows로 돌아갑니다. 완료는 ANS 인터럽트(ADT `/arm-io/ans`의 AIC 줄, 실측 전 [가설] 590)와 문 두드리기·5 kHz 틱에서 거둡니다.
- 자료 규칙: FLUSH는 앞 명령이 다 끝난 뒤 혼자 나가고 끝날 때까지 다음 명령을 넣지 않음. FUA 쓰기·캐시 끈 상태의 쓰기는 쓰기와 그 뒤 FLUSH가 둘 다 끝나야 Windows에 완료를 알림. ANS 오류·1초 시간 초과·모르는 태그는 그 자리에서 고장 처리(g260O와 같은 정책). Windows가 큐를 끄거나 바꾸기 전에는 진행 중 명령을 모두 기다림.

## 스위치 (실행기 `agxdcp-s259-guest-test.sh`)
| 변수 | 값 | 뜻 |
|---|---|---|
| `NWOAS_S262_HV` | 1 | g262Q + `power-s260/power_module.py` + `nvmeqd_module.py` (S260 스위치 모두 사용 가능) |
| `NWOAS_NVME_QD` | 0/없음, 1..61 | 동시 명령 수. 0 = g260O 경로 |
| `NWOAS_NVME_QD_IRQ` | 1(기본)/0 | 0 = 인터럽트 없이 문 두드리기·틱으로만 거둠 |
| `NWOAS_NVME_QD_STRICT` | 0(기본)/1 | 1 = FUA 쓰기를 다른 명령 없이 혼자 실행 |
| `NWOAS_NVME_QD_KICK` | 1(기본)/0 | 0 = 다른 CPU에서 완료를 올렸을 때 CPU0을 깨우지 않음 |

## 파일
| 무엇 | 경로 |
|---|---|
| 패치 | `port-j274/gpu/hv/hv2-g260O-to-g262P.patch`(nvme.c, nvme.h, hv_vm.c, hv_exc.c, 새 nwoas_nvmeqd.inc) 다음 `hv2-g262P-to-g262Q.patch` |
| 재현 빌드 + 호스트 시험 | `port-j274/gpu/hv/tools-hv2/build-g262Q.sh <새 폴더>`, 시험 `tools-hv2/hosttest/test_nvmeqd.c` |
| 바이너리 | `port-j274/gpu/hv/out-hv2/m1n1-g262Q-agx-hv2-nvmeqd.bin` (2326528 B, `6a1ce6d4…`). g262P(`c89fc274…`)는 기록용 |
| 실행기 모듈 / 표 | `nvmeqd_module.py`, `nvmeqd_report.py <LINK_DIR>/nvmeqd.jsonl` |
| Windows 도구 | `NWQD.EXE` (`nwqd.c`, `build.sh`), 전달 `manifests/s262-nwqd.json` + `jobs/Q0-deliver-nwqd.cmd` |
| 작업 | `Q1-io-integ.cmd`(H2 읽기 시험 + 384 MiB 무결성), `Q2-verify.cmd`(재시작 뒤 다시 확인), `Q3-cleanup.cmd` |

## g262P가 QD 설정을 거부한 원인 (2026-11-19 실기, 확정)
- g262P는 동시 처리 설정을 모두 거부했습니다(`ret=0x0`). 원인은 설정 조건 `lifecycle_epoch == 0`이 틀린 조건이었기 때문입니다.
- `layout-s231/module.py`(동작 16, `s229_enable`)가 먼저 돌 때 `nwoas_frontend_init` → 제어 초기화 → `reset_io` → `s229_control_local(0)`이 `lifecycle_epoch`를 1로 올립니다. 실제 프런트엔드 코드를 이 맥북에서 돌려 `reset_io` 호출 1회를 관찰했습니다.
- 와이파이·전원 모듈은 이 경로를 쓰지 않으므로 실행 순서는 원인이 아닙니다.
- g262Q는 그 조건을 빼고 "게스트 큐가 아직 등록되지 않음"(SQ/CQ 주소·크기 0)으로 바꿨습니다. 거부하면 `0x51444e4f_000000ss_rr`(rr = 사유 번호, ss = 하위 사유)로 답하고, 모듈이 사유를 글로 찍습니다.

## 실기기 시험 순서와 통과 기준
| 순서 | 실행 | 통과 기준 |
|---|---|---|
| 0 | `NWOAS_DELIVER_MANIFEST=…/s262-nwqd.json` 로 부팅, Q0 | `S243 READBACK MATCH NWQD.EXE` |
| 1 | `NWOAS_S262_HV=1` (QD 없음), Q1 | 바탕화면, H2 숫자가 g260O와 비슷, `INTEG PASS` |
| 2 | `NWOAS_S262_HV=1 NWOAS_NVME_QD=8`, Q1 | `INTEG PASS`, 아래 성능 목표, 안전 계수기 0 |
| 3 | `NWOAS_NVME_QD=32`, Q1 | 같음 |
| 4 | QD=32 + `NWOAS_PWR_NVME_SHUT=1`: Q1 → 재시작 → Q2, 3회 | Q2 `INTEG PASS` 매번, `chkdsk C: /scan` 오류 0 |
| 5 | (비교) QD=32 `NWOAS_NVME_QD_IRQ=0`, QD=32 `NWOAS_NVME_QD_STRICT=1` | 무결성 같음, 속도 비교용 |

- 안전(필수): `nvmeqd_report.py`의 안전 계수기(faults, timeouts, dev_errors, drain_fail, eng_bad_tag, eng_inval_fail, storm) 모두 0, HV 로그에 `NWOAS-S262 NVMe QD fault`·`REFUSED write outside WINTEST` 없음, Windows 디스크 오류 이벤트(7·51·129·153) 0건.
- 성능 목표 [가설]: rand4k QD8 ≥ 25,000 IOPS, QD32 ≥ 35,000 IOPS, QD1 ≥ 지금 값의 90%, seq1m ≥ 1.8 GB/s. hvstats의 depth 평균 > 1.
- 기대 HV 로그: `nvme: S262 concurrent engine on: tags 2..33, irq=590 (CQ1 re-created with IEN)`, `NWOAS-S262 NVMe QD=32 flags=0x1 irq=590 armed`, 첫 입출력 때 `nvme: S262 ANS IRQ 590 unmasked, target CPU 0`.
- 되돌리기: `NWOAS_NVME_QD`를 빼면 같은 바이너리로 옛 경로, `NWOAS_S262_HV`를 빼면 g254L/g260O.

## 위험 [모두 가설]
1. (g262Q에서도 아직 실기 미확인) 부팅 전에 ANS I/O 큐를 인터럽트 켠 상태로 다시 만드는 것은 ANS에서 해 본 적이 없습니다. 실패하면 모듈이 멈추고 게스트를 시작하지 않습니다.
2. ANS 인터럽트가 CQ 문 두드리기 뒤 내려간다고 봤습니다(Linux apple.c가 그렇게 씀). 안 내려가면 32번 뒤 인터럽트를 끄고 틱 조회로 바뀝니다(그때 QD1은 지금보다 느려질 수 있음).
3. FUA 뒤 FLUSH는 다른 명령과 동시에 나갑니다(Linux와 같음). 의심되면 `STRICT=1`.
4. 시간 초과한 태그는 다시 쓰지 않지만, ANS가 늦게 그 버퍼에 쓰면 Windows 메모리가 상할 수 있습니다(g260O의 고장 처리와 같은 한계).
5. `NWOAS_PWR_NVME_SHUT` 없이 재시작하면 진행 중 명령을 기다리지 않습니다(지금과 같음).
6. 완료 인터럽트가 모두 CPU0으로 가서 CPU0 부담이 늘 수 있습니다.
7. 절전·최대 절전·충돌 덤프 경로는 시험하지 않았습니다.
8. Bluetooth 실행기(`agxdcp-s261-guest-test.sh`)에는 넣지 않았습니다.
