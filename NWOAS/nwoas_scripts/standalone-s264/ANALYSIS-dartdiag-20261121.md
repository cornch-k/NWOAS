# USB DART 진단 줄(DARTDIAG·DARTERR·CMDDUMP) — C 실행기와 Python 실행기 (2026-11-21)

> 기록·소스만 읽음(실기·빌드 없음). 조사: 지휘 세션의 Opus 에이전트. 1번 표는 지휘 세션이 다시 세어 확인.
> 표기: [사실] 기록·코드·계산 확인, [가설] 추측.

## 0. 결론
고칠 것 없음. 이 줄은 C 실행기에서만 나오는 것이 아니고, 두 실행기가 만든 USB 상태도 같습니다 [사실].
- 지휘 세션이 README 16장에 "Python 부팅 0줄"이라고 적은 것은 잘못 센 값입니다. `err=0xc000400`으로 찾았는데, 이 값은 부팅마다 다릅니다(00:38 부팅은 `0x9000400`).
- 진단 이름 `MAPPED-WRONG-TARGET`은 7월의 "IOVA = 물리 주소" 배치를 기대값으로 비교해서 붙습니다. 실제 DART 항목은 지금 배치(낮은 4 GiB → 높은 저장소, 차이 0xAE0FCC000)와 맞습니다.

## 1. 줄 수 (지휘 세션 다시 셈) [사실]
| 부팅 | 실행기 | `.vuart` 진단 줄 (DARTDIAG·DARTERR·CMDDUMP, 개) | MAPPED-WRONG-TARGET (개) |
|---|---|---|---|
| 00:24 `agxmed1g-s249-20261121-002401.7ZRzU6` | Python S269 | 28 | 6 |
| 00:38 `agxmed1g-s249-20261121-003813.CpsrIi` | Python S268 | 28 | 6 |
| 00:40 `s264g-launch-20261121-004023` | C s264G 기준 | 28 | 6 |
| 00:46 `s264g-launch-20261121-004627` | C s264G 기본 | 28 | 6 |

에이전트 확인: 진단 묶음(FORK·EVTDUMP·CMDDUMP·DARTDIAG·DARTERR·POST-CONSUME, 부팅마다 56줄)에서 부팅마다 바뀌는 값(elr, 오류 레지스터 stream·err·lo)만 지우면 네 부팅이 바이트 단위로 같음(sha1 앞자리 `f4f8e4061406`).

## 2. 찍는 코드 [사실]
`hv_vm.c`(m1n1-gpu-hv2; s264G 트리는 약 +45줄) — FL1100 xHCI USB 문제를 쫓던 7월 진단.
| 줄 | 함수 | 하는 일 |
|---|---|---|
| `CMDDUMP` | `nwoas_dump_evtring` 2477~2515 | 명령 링 8칸 캐시 값 vs DRAM 값 |
| `DARTDIAG s1` | `nwoas_dump_dart_dcbaa` 2891 | FL1100 DART stream 1 TCR·TTBR·오류 레지스터 원값 |
| `DARTERR STALE/LIVE-FAULT` | 2897~2904 | FLAG(bit31)=0이면 `STALE(FLAG=0,ignore)` |
| `DARTDIAG erstba/dcbaap/crcr` | 2905~2928 | 세 링 IOVA의 L1/L2 항목을 `DART_L2_IDENTITY(iova)`와 비교 |

찍는 때: USBSTS 읽기 가로채기 16,384번째마다(부팅당 최대 16번) + 첫 이벤트 소비 직후 한 번(`hv_vm.c` 4381~4389, 4405~4414).

## 3. 무해한 근거 [사실, 계산]
| 줄 | 값 | 해석 |
|---|---|---|
| `DARTERR STALE(FLAG=0,ignore) stream=8/9/12/13` | FLAG=0, code=0x400 | 유효한 오류 아님, FL1100(stream 1) 것 아님 |
| `erstba iova=0xfffde000 l2[2039]=0xfff0be0fa8003` | 0xbe0fa8000 − 0xfffdc000 = 0xAE0FCC000 | 탐침의 `low_backing_if_pin_holds`와 같음 |
| `dcbaap iova=0xfffd9000 l2[2038]=0xfff0be0fa4003` | 0xbe0fa4000 − 0xfffd8000 = 0xAE0FCC000 | 같음 |
| `CMDDUMP TRB[0..7] … same` | 캐시·DRAM 모두 0 | 어긋남 없음 |

USB 키보드·마우스는 C 실행에서도 정상(콘치님 확인, 00:4x).

## 4. 고친다면 (선택, 동작 변화 없음)
`nwoas_dump_dart_dcbaa`의 `want = DART_L2_IDENTITY(iova)` → `DART_L2_IDENTITY(nwoas_ipa_to_pa(iova))`, 표시 `-IDENTITY-OK` → `-STAGE2-OK`. 다음 하이퍼바이저 수정과 묶어서만 함.
