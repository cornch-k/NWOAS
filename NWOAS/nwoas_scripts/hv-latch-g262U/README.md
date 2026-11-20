# g262U — 모니터 재연결과 화면 표시 반영 판정의 경합 (2026-11-20) [빌드만]

> 표기: **[사실]** = 코드·기록으로 확인. **[가설]** = 확인하지 않은 추측. **[빌드만]** = 빌드·호스트 시험까지만 함.
> 맥미니에서는 돌지 않았습니다. 일상 바이너리와 일상 실행기·래퍼는 고치지 않았습니다.

## 0. 결론

1. **고친 것은 하이퍼바이저의 화면 서비스 두 군데와 기록 한 군데입니다 [빌드만].** 11-20 18:02 블루스크린(0x116)의 발단 후보(`port-j274/gpu/driver-p15/RESULT-2775.md` 31·32장)를 겨냥합니다.
2. **경합의 자리 [사실: 코드].** 재연결 중에 들어온 Windows 표시는 가짜 번호를 받고 넘어갑니다(`display.c` S254). 그 표시의 반영 판정을 기다리는 사이 재연결 modeset이 끝나며 "반영된 번호"를 자기 것으로 바꿉니다. 판정은 "번호가 다름"(-2)이 되고, 화면 장치는 오류 인터럽트를 올린 뒤 applied ≠ latched로 남아 이후 모든 표시를 BUSY로 거절합니다.
3. **이것이 18:02 사고의 실제 원인이었는지는 확인하지 못했습니다 [가설].** 사고 때 기록에는 이 실패를 찍는 줄이 없었습니다. g262U는 그 줄을 찍습니다.

## 1. 바뀐 것 (`hv2-g262T-to-g262U.patch`, 바탕 g262T)

| 파일 | 내용 |
|---|---|
| `src/nwoas_latch.h` (새) | 반영 판정을 하드웨어 접근 없는 함수 두 개로 분리(`nwoas_latch_decide`, `nwoas_latch_supersede`) |
| `src/display.c` modeset SWAP 단계 | modeset이 바꾸기 전의 번호를 `superseded_swap_id`에 기억 |
| `src/display.c` `display_nwoas_svc_latch_poll` | 기대 번호가 그 기억한 번호면 반영된 것으로 판정(그 modeset이 현재 화면을 다시 내보냈으므로). 그 밖의 불일치는 전처럼 -2 |
| 같은 함수 | `NWOAS-DCP-G262U present … latched by modeset`, `NWOAS-DCP-G262U latch MISMATCH …` 줄(처음 8번, 그 뒤 256번마다) |
| `src/hv_agx_scanout_broker.c` `hv_agx_scanout_broker_fail_latch` | 실패를 알린 뒤 `latched_sequence = sequence` — 다음 PRESENT·SET_POWER를 받게 함 |

- 한계 [사실: 코드]: modeset이 내보낸 화면은 modeset이 시작될 때의 것이라, 그 뒤에 들어온 표시는 다음 표시가 올 때까지 한 장 늦게 보일 수 있습니다.
- 한계 [사실: 코드]: Windows 드라이버는 오류 인터럽트를 받으면 지금도 화면 출력을 영구 고장으로 표시합니다. 두 번째 고침(다음 표시를 받게 함)은 드라이버 쪽 복구를 나중에 넣을 수 있게 하는 준비이고, 그것만으로 블루스크린을 막지는 못합니다.

## 2. 빌드 [빌드만]

| 판 | 바탕 | 크기 (B) | sha256 | 경고 수 |
|---|---|---|---|---|
| g262U | g262T (`pmu-cyc/build-pmu.sh g262T` 재현, `d5ee4781…60af`) | 2,326,528 | `7ef111d7e0bcc1b2b769818354e860afd26e8773f233e9757f9461a529ce9d7a` | 44 (g262T와 같음) |

- 재현: `bash build-g262u.sh <새 폴더>` (`T_WORK=`로 이미 만든 g262T를 쓸 수 있음). 두 번 빌드해 같았습니다.
- 바이너리: `out/m1n1-g262U-agx-hv2-latch.bin`(+ `.elf`). 일상 경로(`port-j274/gpu/hv/out-hv2`)에는 넣지 않았습니다. 지문은 `SHA256SUMS`.
- 호스트 시험 `hosttest/test_latch.c`: 18/18. 같은 시험을 고치기 전 화면 장치 소스에 돌리면 16/18(실패 2개가 고친 동작).
- 같은 패치를 `m1n1-standalone-s264`와 `m1n1-gpu-hv2` 트리에 미리 대 보면(dry-run) 걸림 없이 들어갑니다. 적용하지는 않았습니다.

## 3. 실기 시험 안 (콘치님 재석 때)

1. g262U로 평소 부팅: 일상 실행기에서 하이퍼바이저만 바꿔 부팅(`pmu-cyc/launch-pmu.sh`와 같은 방식, 바꿀 값은 바이너리 경로와 sha256 두 줄). 실행기 파생 스크립트는 지휘 세션이 만듭니다.
2. 화면이 평소처럼 뜨는지, 기록에 `present #1…` 줄과 `failures 0`이 나오는지.
3. `gpucov/jobs/K61-present-during-reconnect.cmd`: Edge가 150초 동안 계속 화면을 바꾸는 사이 모니터 케이블을 뽑고 5초 뒤 꽂기를 5번.
4. 판정: Windows 재시작 없음, K61의 ER 줄 `fatals=0 lost=0`, 하이퍼바이저 기록에 `latched by modeset` 줄이 있고 `latch MISMATCH` 줄이 없음. `latched by modeset`이 한 줄이라도 나오면 경합이 실제로 일어났고 고침이 받아 낸 것입니다. 한 줄도 없으면 이번 시험에서는 겹치지 않은 것이라 판정 보류입니다.
