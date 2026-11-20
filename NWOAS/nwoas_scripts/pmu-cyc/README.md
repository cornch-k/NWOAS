# PMU 사이클 계수기 — Windows 클럭 표시 22~34 MHz (2026-11-20) [빌드만]

> 표기: **[사실]** = 코드·기록·공개 소스로 확인. **[가설]** = 확인하지 않은 추측. **[빌드만]** = 빌드·호스트 시험까지만 함.
> 맥미니·VM·설치는 쓰지 않았습니다. 일상 바이너리(g262R)와 일상 실행기·래퍼는 고치지 않았습니다.

## 0. 결론

1. **원인은 사이클 계수기의 "어느 단계에서 셀지" 설정이 Windows 쪽이 아니라 하이퍼바이저 쪽 칸에 써지는 것으로 봅니다 [가설, 근거 강함].**
   - Windows는 각 CPU에서 PMUv3 `PMCCFILTR_EL0 = 0x08000000`(EL0·EL1을 셈)을 씁니다 [사실: 하이퍼바이저 기록 11회].
   - 하이퍼바이저는 이 값을 Apple `PMCR1`로 옮기는데, `PMCR1_EL1` 번호(`s3_1_c15_c1_0`)로 씁니다(`hv_exc.c` PMCCFILTR 칸).
   - 하이퍼바이저는 HCR_EL2.E2H=1로 돕니다(`exception.c:171`). 이때 EL2에서 그 번호는 **호스트 칸**입니다. 게스트(EL1, 호스트가 아닌 EL0)용 칸은 별도 별칭 `PMCR1_EL12`(`s3_1_c15_c7_2`)입니다 [사실: Linux 커밋 46573d944f00 "drivers/perf: apple_m1: Support host/guest event filtering", `arch/arm64/include/asm/apple_m1_pmu.h:39-40`].
   - 그래서 Windows가 돌 때 PMC0(= 게스트가 읽는 PMCCNTR_EL0)이 거의 늘지 않습니다.
2. **숫자도 이 설명과 맞습니다.** 사이클 계수기가 1 µs에 약 28 늘어났고, 기준 시계(QPF)는 24 MHz였습니다.
   - 실제 클럭(약 2~3 GHz)이라면 1 µs에 2,000~3,000이어야 합니다.
   - 하이퍼바이저는 느린 경로에서 계수기를 멈춥니다(`hv_exc_entry`). 그래서 남는 것은 빠른 경로 등 일부 EL2 시간뿐이라는 설명이 맞습니다 [가설].
3. **고친 것(가장 작은 수정):** `PMCCFILTR_EL0` 읽기·쓰기가 `PMCR1_EL12`를 쓰게 했습니다. `U=1`일 때 EL1 비트를 지우던 오타도 고쳤습니다. 다른 동작은 같습니다.
4. **SMBIOS·_CPC로 고치는 것은 고르지 않았습니다.**
   - SMBIOS(최대 3,204 / 현재 2,988 MHz)는 이미 들어가 있는데도 Windows는 22~34 MHz를 보였습니다 [사실: `smbios-s262/runs/run-s262-wifi-20261119-082101.txt`]. 그러니 Windows는 SMBIOS 값을 쓰지 않습니다.
   - CPU마다 값이 다르고(22·23·24·30·31·33·34 MHz), 스레드 사이클 시간도 같은 비율입니다. 그래서 Windows는 사이클 계수기를 기준 시계와 비교해 잰 값을 씁니다 [가설: 공개 문서로 확인하지 못함].
   - DSDT의 CPU 장치(ACPI0007)에는 `_CPC`가 없습니다 [사실: `DSDT.asl:474-554`]. `_CPC`를 넣으면 Windows가 성능 제어 레지스터에 쓰기 시작하므로 작은 수정이 아닙니다.
5. **표시만의 문제가 아닐 수 있습니다 [가설].** Windows 스케줄러는 사이클 수로 스레드 시간 몫을 셉니다. 사용자 모드 시간이 거의 세지지 않으면 시간 배분이 어긋날 수 있습니다. Cinebench 싱글 370에 영향이 있는지는 실기 A/B로만 압니다.

## 1. 빌드 [빌드만]

| 판 | 바탕 | 패치 | 크기 (B) | sha256 | 경고 수 |
|---|---|---|---|---|---|
| g262T | g262R (`build-g262R.sh` 재현) | `hv2-g262R-to-g262T.patch` | 2,326,528 | `d5ee4781920a13993d186f56f3f37d8ff2d600a5e40b5e7d34f26239a31660af` | 44 (g262R과 같음) |
| s264H | s264G (`build-s264g.sh` 재현) | `hv2-s264G-to-s264H.patch` | 2,605,056 | `e5070a2e3268a04cb0571c96e4ea2ee382efdcf154c6699ee291a0c9ff466949` | 44 (s264G와 같음) |

- 재현: `bash build-pmu.sh g262T|s264H <새 폴더>`(바탕 재빌드 포함; `BASE_WORK=`로 이미 만든 바탕을 쓸 수 있음). 두 판 모두 두 번 빌드해 같았습니다.
- 바이너리 확인:

| 바이너리 | `PMCR1_EL12` 읽기 명령 (개) | `PMCR1_EL12` 쓰기 명령 (개) |
|---|---|---|
| g262T | 2 | 1 |
| s264H | 2 | 1 |
| g262R | 0 | 0 |

- 호스트 시험 `hosttest/test_pmu_filter.c`: 6/6. Windows 값 0x08000000을 넣으면 EL0+EL1을 세고, 다시 읽으면 0x08000000이 나옵니다. U·P 비트 각각과 비트 위치도 Linux 정의와 맞습니다.
- 바이너리 위치: `out/m1n1-g262T-agx-hv2-pmu.bin`(+ `.elf`), `out/m1n1-s264H-agx-hv2-pmu.bin`. 일상 경로(`port-j274/gpu/hv/out-hv2`)에는 넣지 않았습니다.
- 위험 [가설]: T8103에서 EL2가 `PMCR1_EL12`를 쓸 수 있는지는 이 기기에서 확인하지 않았습니다. Linux는 VHE에서 M 계열 전체에 쓰고, Apple 기기에서 시험됐습니다(Tested-by Janne Grunau). 어느 칩인지는 커밋에 없습니다.

## 2. 실기 시험 순서 (맥북 연결, 콘치님 손은 Cinebench만)

| 순서 | 명령 | 통과 기준 |
|---|---|---|
| 1 | 기준: `bash pmu-cyc/run-pmu.sh r <옛 .out> <옛 .link>` (일상 g262R 그대로) | S3: `CYC cyclesPerUs` ≈ 28, `PPI MaxMhz` 22~34 (지금과 같음). H1의 load Mops를 기준으로 기록 |
| 2 | 수정: `bash pmu-cyc/run-pmu.sh t <옛 .out> <옛 .link>` (HV만 g262T) | ① HV 기록 `PMCCFILTR_EL0 … -> PMCR1_EL12`에 비트 8·16이 켜짐(값 `0x…10100` 포함) ② S3: E코어(CPU 0~3) cyclesPerUs 약 600~2,064, P코어(4~7) 약 600~3,204. `PPI MaxMhz`·`~MHz`도 같은 범위 ③ S268 DONE, QD 엔진 ON(일상과 같음) |
| 3 | 2번 부팅에서 H1(NWPWR idle/load/timer) | load Mops가 1번과 ±5% 안 = 일은 그대로이고 숫자만 바뀜. timer 간격이 1번과 같음 |
| 4 | 콘치님: 작업 관리자 → 성능 → CPU "속도"와 Cinebench 싱글·멀티 (1번 부팅 기록 370 / 1,969와 비교) | 속도가 GHz 단위. 점수는 비교만(바뀌면 5장의 스케줄러 가설을 따로 봄) |
| 5 | (나) 통과 뒤 C 실행기: `bash pmu-cyc/run-s264h-launch-slot.sh <옛 .out> <옛 .link>` | 작업 관리자 속도가 GHz 단위(작업 통로가 없어 눈으로만) |

- 실패하면: 2번에서 부팅이 멈추거나 HV 예외가 나면 `PMCR1_EL12` 접근 자체가 문제입니다. 일상 래퍼로 다시 켜면 g262R로 돌아옵니다(아무것도 바뀌지 않음).
- 2번에서 HV 줄은 맞는데 cyclesPerUs가 그대로면 이 가설이 틀린 것입니다. 그때는 Windows가 사이클 시간을 읽는 길(PMCCNTR 읽기 수, `NWOAS-PMCC` 기록)부터 다시 봅니다.
- 코어별 기대 범위의 위 끝은 SMBIOS 표 최대값(3,204 MHz)과 E코어 최대 2,064 MHz입니다. 아래 끝은 절전 중 클럭이 내려간 경우를 넉넉히 잡은 값입니다 [가설].

## 3. 파일
| 파일 | 하는 일 |
|---|---|
| `apply_pmu_fix.py` | 트리에 수정 적용(패치를 만든 도구) |
| `hv2-g262R-to-g262T.patch`, `hv2-s264G-to-s264H.patch` | 패치(sha256은 `build-pmu.sh`에 고정) |
| `build-pmu.sh` | 재현 빌드 + 호스트 시험 + 명령 개수 확인 + 지문 비교 |
| `hosttest/test_pmu_filter.c` | 비트 변환 시험 |
| `launch-pmu.sh` | 일상 실행기를 읽어 HV 두 줄만 g262T로 바꿔 실행 |
| `run-pmu.sh r\|t` | 일상 래퍼를 읽어 실행기 호출(t만)·결과 파일·끝 작업(S3, H1)만 바꿔 실행 |
| `run-s264h-launch-slot.sh` | s264G 실행 단계를 읽어 HV만 s264H로 바꿔 실행 |
| `SHA256SUMS` | 모든 지문 |
