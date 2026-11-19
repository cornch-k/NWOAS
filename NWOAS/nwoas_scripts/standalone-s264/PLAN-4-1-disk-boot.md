# 4-1 디스크에서 켜기 계획 — 맥북 없이 C 실행기까지 (2026-11-20, 독립 검토 반영판)

> 아무것도 실행하지 않았습니다. 맥미니·복구 화면·ESP는 건드리지 않았습니다. 바뀐 것은 이 폴더의 파일과 빌드 결과뿐입니다.
> 표기: **[사실]** = 파일:줄, 로그, 공개 문서로 확인한 것. **[가설]** = 확인하지 않은 추측. **[빌드만]** = 빌드·호스트 시험까지만 한 것.
> 경로 줄임: `N/` = `/Users/angigyeom/Desktop/DEV/NWOAS/`, `S/` = `N/nwoas_scripts/standalone-s264/stage1/`.
> 11-16 실패 뒤의 규칙을 따릅니다. 콘치님이 치시는 명령은 그 복구 환경에 있는 것만 쓰고, 실패하면 칠 다음 명령을 미리 적습니다. 콘치님이 주무시거나 쉬실 때는 하지 않습니다.

---

## 0. 결론

1. **1TR은 아직 콘치님께 가져가지 않습니다.** 먼저 맥북 리허설 세 가지를 통과해야 합니다(6장 순서 4~6). 리허설은 모두 맥북 연결 상태에서 하고, 디스크·부팅 정책을 바꾸지 않습니다.
   - 4-2 끝까지: D4 → 1단계 → ESP의 s264G → 맥북 바이트 없이 자동 시작 → Windows
   - 1단계 → D4 → **재부팅 없이** S268 일상 실행 전체(QD 엔진 켬) → SSD 검사(Q1S·Q2·chkdsk)
   - 일부러 실패시키기 두 가지(파일 없음, 예외) → 1단계가 재부팅하지 않고 USB 프록시로 남는지 → 맥북이 D4를 다시 띄움
2. **1단계 파일 크기를 D4와 똑같이 2,097,152 B로 맞췄습니다(2장).** iBoot가 이미지 크기에 따라 배치를 바꾸는지는 증명할 수 없어서, iBoot가 받는 파일 크기를 지금과 같게 했습니다.
   - 1단계가 다음 이미지를 놓는 방식은 이제 맥북의 `chainload.py`와 같습니다. 호스트 시험 3/3으로 확인했습니다 [사실]. 고치기 전 코드는 3/3 모두 달랐습니다(preoslog·BootArgs를 안 옮김).
3. **1단계 안의 실패는 코드상 재부팅하지 않고 USB 프록시로 갑니다(3장) [빌드만].**
   - 대상: ESP 읽기 실패, 예외, SError, Rust panic, `panic()`, assert
   - 그 프록시에서 맥북이 `chainload.py -r`로 D4를 다시 넘길 수 있게 했습니다. 처음 판은 이것이 안 되는 구조였고, 검토에서 잡아 고쳤습니다.
   - 실기에서 리허설하는 것은 "파일 없음"과 "데이터 접근 예외" 두 가지입니다.
4. **성공·되돌리기 판단은 맥미니 화면이 아니라 맥북이 합니다(8장).** 콘치님이 자리에 계신 동안 `watch-after-go1.sh`가 세 가지 중 하나를 냅니다.
   - `KEEP`: 등록을 유지합니다.
   - `ROLLBACK`: 1단계 신호가 실패했습니다(프록시 없음, 1단계 대체 프록시, D4 아님, 배치 검사 G-2 실패). 그 자리에서 되돌립니다.
   - `HOLD`: 1단계는 통과했고 S268·SSD만 안 됐습니다. 1TR로 가지 않고 그 검사부터 다시 합니다.
5. **되돌리기는 11-16에 이 스텁에서 등록이 없던 상태로부터 성공한 그 순서입니다.** 스텁 1TR에서 스텁 자신의 `step2.sh`를 실행합니다 [사실: `research-20261116/RESULT-20261116-morning.md:52`].
   - 같은 스텁에 다시 등록해 본 적은 없습니다 [사실]. D4만 kmutil로 다시 등록한 성공 기록은 Macintosh HD 쪽에만 있습니다(09-07, 11-15).
6. **(나)는 두 번만 돌립니다.** 스위치를 전부 끈 기준 실행 한 번, 기본 실행 한 번입니다. 실패할 때만 반씩 나눠 찾습니다.

---

## 1. 지금 상태와 목표 체인

### 1.1 지금 [사실]
| 항목 | 값 | 근거 |
|---|---|---|
| 기본 부팅 | 스텁(VGID `C15A9C5D-F2F8-486D-95CA-F47E3C47A7CD`, macOS 13.5 스텁, OS 펌웨어 iBoot-8422.141.2) | `RESULT-20261116-morning.md` 5장 |
| 스텁에 등록된 것 | D4 m1n1(2,097,152 B, md5 `7bec0225…`), `bputil -nc` 해 둠 | `stub-135/tool/step2/step2.sh`, 같은 5장 |
| Macintosh HD | D4 등록이 남아 있어 macOS는 안 켜짐 | `RESULT-20261116-morning.md:57` |
| ESP | GPT 4번 칸, 칸 GUID `a3e73003-c14a-487b-9f2e-8854fe119192`, Windows가 만든 FAT32 | `standalone-boot.md` 3장 |
| iBoot 부팅 인자(13.5 펌웨어) | phys_base `0x801224000`, mem_size `0x3dfddc000`. m1n1 위치만 매번 다름 | 11-16 이후 탐침 기록(`logs/*.probe`) |

### 1.2 목표 체인
```
iBoot → 스텁 → 1단계 m1n1 (stage1.bin, 2,097,152 B; 화면 안 건드림; NVMe 켜서 ESP 읽고 끔)
      → ESP m1n1/boot.bin
          4-1: D4 그대로 (맥북 개발 방식 그대로)
          4-2: s264G + nwoas_autostart=3 (3초 안에 맥북 바이트가 없으면 ESP 페이로드로 C 실행기)
```

---

## 2. 크기와 배치 — iBoot가 보는 크기를 지금과 같게 했다

**결론: 이미지 크기와 상관없다는 것은 증명하지 못했습니다. 그래서 1단계 파일을 D4와 정확히 같은 2,097,152 B로 만들었습니다.**
- iBoot에서 본 크기 밖의 차이(내용)는 맥북 리허설과 1TR 뒤 G-2 검사로 확인합니다.
- 1단계 뒤의 배치는 지금 맥북 방식과 같은 공식임을 시험으로 확인했습니다.

### 2.1 무엇이 무엇에 달렸나
| 값 | 정하는 쪽 | 이미지 크기에 달렸나 | 근거 |
|---|---|---|---|
| phys_base, mem_size, 화면 주소(video) | iBoot | 모름 → **크기를 같게 해서 피함** | 13.5 펌웨어 탐침에서 m1n1 위치가 바뀌어도 이 값은 같았음 [사실]. 다른 크기 이미지를 등록한 기록은 없음 [사실] |
| iBoot가 놓는 SEPFW 위치 | iBoot | 붙어 있을 가능성 큼 | upstream m1n1은 "이미지 바로 뒤에 SEPFW"를 끝 표시로 씀(`payload.c:265-267`) [사실]. 그래서 크기를 같게 함 |
| guest skew(=pin), FBPROBE 범위 | 탐침이 ADT·부팅 인자로 계산 | 직접은 아님 | `display-s244/fw135_model.py:206-210`: 항목은 ADT 크기·화면 값 [사실] |
| 다음 이미지 위치·SEPFW 복사 위치·preoslog·BootArgs·top_of_kernel_data | 앞 단계 체인로드 | 다음 이미지 크기에 달림(공식) | 아래 2.2 |
| S102 mem_size | 부팅 인자의 mem_size를 그대로 받음 | iBoot 값 그대로 | 1단계 C 체인로드는 부팅 인자를 복사하고 top_of_kernel_data만 바꿈(`chainload.c`) [사실] |

### 2.2 1단계 C 체인로드 = 맥북 `chainload.py` (호스트 시험)
- `S/stage1_layout_test.py`: 진짜 `chainload.c`를 맥북에서 컴파일합니다. 실제 J274 ADT 값(SEPFW `0x59c000`, preoslog `0x40000`)으로 결과를 `chainload.py` 공식과 비교합니다.
- 비교 항목: 다음 단계 인자, ADT 속성, 부팅 인자 top_of_kernel_data, 옮긴 바이트

| 경우 | 고친 뒤 | 고치기 전(s264G 원본 코드) |
|---|---|---|
| 1단계 → ESP boot.bin = D4 (2,097,152 B) | SAME, 영역 `0x7e4000` | DIFF: preoslog 안 옮김, BootArgs 칸 안 고침, 영역 `0x7a4000` |
| 1단계(4-2) → nwoas-s264g.bin (s264G + 변수) | SAME, 영역 `0x860000` | DIFF (같은 이유) |
| preoslog 없는 ADT | SAME | DIFF (BootArgs 칸) |

- 고친 것: `chainload.c`가 preoslog를 SEPFW 뒤로 옮기고, ADT `BootArgs` 칸을 `chainload.py`처럼 고칩니다. malloc 실패 검사도 넣었습니다. `chainload.py`에는 원래 있던 단계입니다.
- RVBAR(보조 CPU 시작 주소)는 다음 m1n1이 직접 씁니다(`smp.c:383`) [사실]. 그래서 옮기지 않았습니다.

### 2.3 D4가 정확히 2 MiB인 이유
- m1n1 raw 이미지는 bss와 스택까지 포함하고, 16 KiB 단위로 끝납니다(`m1n1.ld:172-191`) [사실].
- D4는 코드·데이터·스택 합이 우연히 `0x200000`에서 끝난 것입니다. 끝 표시 위치로 본 판단입니다 [가설].

### 2.4 1단계 크기 줄이기
| 판 | raw 크기 (B) | 파일 크기 (B) |
|---|---|---|
| 처음 판(검토 전) | 2,490,368 | 2,490,433 |
| 지금: `CHAINLOADING` 빌드에서만 DART 쪽 풀 48쪽 → 1쪽 | 1,720,320 | 2,097,152 (변수 + 0 채움) |
| D4 | 2,097,152 | 2,097,152 |

- 1단계는 하이퍼바이저를 돌리지 않으므로 DART 풀이 필요 없습니다. 하이퍼바이저 판(`NWOAS_AGX=1`, `CHAINLOADING` 없음)은 48쪽 그대로입니다.
- **이 트리로 하이퍼바이저를 `CHAINLOADING=1`로 빌드하면 안 됩니다.** 풀이 1쪽이 됩니다.
- 변수는 raw 끝(`_payload_start` = `0x1a4000`, 빌드 스크립트가 확인)에 붙고, 4바이트 0 뒤로 2 MiB까지 0입니다. m1n1은 0 4바이트에서 읽기를 멈춥니다(`payload.c:265`) [사실].

### 2.5 4-2(1단계 → 더 큰 s264G)
- s264G(2,605,056 B + 변수)를 넘길 때의 공식은 2.2 둘째 줄입니다. 오늘 맥북이 D4에서 하이퍼바이저를 넘기는 것과 같은 결과입니다 [사실: 시험].
- 덮어쓰는 범위는 오늘 맥북 방식이 이미 덮어쓰는 범위와 같습니다. 1단계가 D4 자리에 있기 때문입니다 [사실: 공식].
- 4-2에서 다른 점은 s264G가 D4 없이 바로 뜬다는 것입니다. 이것은 4-2 리허설(4.1)로 확인합니다.

---

## 3. 1단계의 실패 동작 — 재부팅하지 않고 USB 프록시로

**결론: 1단계 시작부터 다음 이미지로 넘기기 직전까지, 코드상 모든 실패가 재부팅 대신 USB 프록시로 갑니다 [빌드만]. 실기에서 리허설하는 것은 두 가지입니다.**

| 실패 | 코드가 하는 일 | 근거 (`m1n1-standalone-s264/src`, 패치 `S/hv2-s264G-to-stage1.patch`) | 리허설 |
|---|---|---|---|
| ESP 파일 없음·GPT 이상·다른 칸 | Rust가 -1 → `chainload_load` -1 → "NWOAS-STAGE1: chainload failed" → `run_actions`의 평소 프록시 | `payload.c` 체인로드 블록, `main.c` "No valid payload found" | `missing` (호스트 시험 27/27에 "파일 없음 → rc < 0" 추가) |
| 동기 예외(데이터 접근 등) | `exc_sync` GUARD_OFF에서 `exc_recover_pc`가 있으면 그 주소로 복귀 → `nwoas_stage1_fallback` | `exception.c` | `fault` (`nwoas_stage1_fault=1`이 ESP 읽기 직전 일부러 접근 오류) |
| SError | `exc_serr`에서 같은 방식 | `exception.c` | 없음 [빌드만] |
| Rust panic | `nwoas_rust_panic` → 대체 프록시 | `rust/src/lib.rs`, `main.c` | 없음 [빌드만] |
| `panic()`, assert | `flush_and_reboot`가 `CHAINLOADING`일 때 대체 프록시로 | `utils.c` | 없음 [빌드만] |
| 멈춤(예외 없이 안 끝남) | 프록시가 안 뜸 | — | 맥북 감시의 "180초 안에 프록시 없음" → ROLLBACK |
| iBoot가 이미지를 거부 / 재부팅 반복 | 1단계 코드 밖 | — | 같은 감시 신호 |

- 범위: `exc_recover_pc`는 `m1n1_main` 첫머리에 켜집니다. 넘기기 직전(`exception_shutdown` 앞)과 평소 프록시 들어가기 직전에 꺼집니다. 프록시 안의 동작은 D4와 같습니다.
- 대체 프록시에서 넘기기: P_VECTOR(`chainload.py -r`)는 `next_stage`만 정하고 프록시를 빠져나옵니다(`proxy.c:92-99`). 그래서 m1n1_main 끝의 넘기기 부분을 `nwoas_handover()`로 묶어 대체 프록시에서도 부르게 했습니다.
- 예외를 맞은 바로 그 단계(USB 켜기 전, MMU 켜기 전 등)에 따라 대체 프록시 자체가 안 될 수 있습니다. 그때 두 번째 오류는 재부팅합니다(대체할 곳이 없음).
- 1단계에는 `EARLY_PROXY_TIMEOUT`이 없습니다. 이 값은 `RELEASE` 빌드에서만 켜집니다(`config.h:46-50`, 빌드 스크립트가 확인). 그래서 맥북이 붙어 있어도 1단계는 늘 ESP를 읽습니다.

---

## 4. 리허설 (모두 맥북 연결, 디스크·부팅 정책 안 바꿈)

### 4.1 4-2 끝까지 — `S/run-stage1-42-rehearsal.sh` (슬롯 `42`)
1. D4 탐침이 통과해야 시작합니다.
2. NY3를 듣기만 합니다(`capture_vuart.py`, 쓰기 없음, 재연결을 견딤).
3. `chainload_nonop.py -r rehearsal/stage1-42.bin`을 실행합니다. `chainload.py` 1~171줄을 그대로 쓰되 두 곳이 다릅니다.
   - 점프를 `p.reload` 대신 응답을 기다리지 않는 P_VECTOR로 보냅니다. `reload`의 재연결 대기가 NY1을 최대 20초 붙잡아, 그동안 s264G의 첫 줄이 사라질 수 있기 때문입니다.
   - 확인용 nop을 빼고 포트를 닫습니다. 그 nop은 s264G에게 "맥북이 붙었다"는 신호가 됩니다(`uartproxy.c:165-167`).
4. 점프 뒤 NY1도 듣기만 합니다. 자동 시작 판단 줄은 NY1에 찍히기 때문입니다. NY3가 콘솔이 되는 것은 C 실행기의 나누기 단계 뒤입니다(`nwoas_s264_plan.c:1626-1629`) [사실: 코드].
5. 통과(rc 0) 조건:
   - NY1 또는 NY3에 `S264 entering guest`가 찍힘
   - `Proxy: host seen`이 없음
   - 앞의 두 줄(`S264G no MacBook proxy client within 3 s`, `S264G auto-start: … -> `)은 잡히면 함께 보고합니다
   - 그 뒤 콘치님이 화면에서 Windows를 확인
6. 끝난 뒤에는 맥북 작업 통로가 없습니다. Windows 시작 메뉴에서 다시 시작하면 D4로 돌아온다고 봅니다 [가설]. 다음 슬롯은 `idle -`로 시작합니다.
- 1단계 코드는 등록할 `stage1.bin`과 같고 변수의 파일 이름만 다릅니다(`build-stage1.sh`).
- ESP에는 `m1n1/nwoas-s264g.bin`(= esp-4-2의 boot.bin)과 `nwoas-payload.bin`을 미리 넣습니다. `m1n1/boot.bin`은 D4 그대로입니다.

### 4.2 1단계 + S268 + SSD — 슬롯 `main`
1. `run-stage1-rehearsal.sh main`을 실행합니다.
   - 단계: D4 탐침 → `chainload.py -r esp-4-1/stage1.bin` → 프록시 복귀 → D4 신원 + G-2
   - 결과: rc 0 통과, 2 프록시 없음, 3 D4 아님, 4 G-2 다름
2. 통과하면 같은 부팅에서 **재부팅 없이** `S/run-s268-after-stage1.sh`를 실행합니다.
   - `run-s268.sh`의 idle 줄은 `reboot.py`를 먼저 불러 시험할 상태를 버립니다. 그래서 그 줄을 뺀 사본을 새로 만들었습니다. `run-s268.sh`는 고치지 않았습니다.
   - S268 전체를 `S268 DONE`까지 돌리고, `NVMe QD engine ON` 줄을 확인합니다.
   - 이어서 `run-s268-regress.sh` 2절과 같은 SSD 검사를 합니다: Q1S → 안전 검사 → Q2 → 안전 검사 → R3 chkdsk + 디스크 이벤트
   - 마지막 줄: `ST1-S268 DONE | qd=ON | q1s=PASS | q2=PASS | safety=ok/ok | chkdsk_rc=0 | diskevt=0 | VERDICT=PASS`
- 리허설과 실제 부팅의 차이: 리허설의 1단계는 D4가 USB를 쓴 뒤에 뜹니다. 실제 부팅에서는 iBoot 바로 다음에 뜹니다. NVMe는 둘 다 iBoot가 남긴 상태에서 1단계가 처음 켭니다. D4는 NVMe를 건드리지 않습니다 [사실: D4의 옛 `nvme_init`은 부르면 죽어서 쓰지 않음, `RESULT-20261116-morning.md:50`].

### 4.3 일부러 실패시키기 — 슬롯 `negative`
1. `run-stage1-rehearsal.sh missing`을 실행하고, 이어서 `fault`를 실행합니다.
   - 통과 조건: 돌아온 프록시가 1단계 자신의 것일 것. `probe_running_image.py`가 기준 주소의 "NWOAS-STAGE1: USB proxy fallback" 문자열을 확인합니다. D4가 보이면 재부팅한 것이므로 실패(rc 5)입니다.
   - 그 뒤 맥북이 `chainload.py -r esp-4-1/boot.bin`(D4)을 실행하고, D4 탐침이 통과해야 합니다(실패 rc 6).
2. 끝나면 `run-s268.sh idle`로 평소 부팅에 돌아갑니다.

---

## 5. 공개 실패 사례 (2026-11-20 03시 `gh issue view`로 확인, 직전에 다시 봄)

| 이슈 | 상태 | 내용 | 우리와 겹치나 | 대응 |
|---|---|---|---|---|
| asahi-installer #441 | 열림 | M4, 26.5.1. 순정 step2가 미완성 스텁 1TR에 닿지 못함(노란 화면) | 낮음. 우리 스텁은 완성됐고, 11-16에 그 1TR에서 step2를 끝냄 | 노란 화면이면 카드 1) 실패 칸 |
| #404 / #407 | 열림 / 닫힘 | Tahoe에서 `bless --setBoot` Code=112 | 이번 순서에서는 bless를 부르지 않음 | NOT-PAIRED이면 Enter를 누르지 않음(카드) |
| #425 | 닫힘 | M1, 26.5. 정책 갱신 뒤 "Local policy update is in progress" 반복 | **있음** | `cleanbp-local.sh`(목록 먼저, 지우기는 그 자리 승인) |
| #255 | 닫힘 | step2 부팅 반복, cleanbp로 해결 | **있음** | 같음 |
| 11-16 이 기기 | — | `shasum` 없음, 부팅 정책 없음(bless 실패 → 인터넷 필요), GPT 속성 노란 화면 | 고쳤거나 피함 | `md5`·`stat`만 씀, 정책 확인 먼저, bless 안 씀 |

---

## 6. 순서 (콘치님이 깨어 계실 때만)

| 순서 | 할 일 | 콘치님 손 | 통과 기준 | 실패하면 |
|---|---|---|---|---|
| 1 | (가) `bash standalone-s264/run-s264g-compare-slot.sh <옛 .out> <옛 .link>` | 없음 | `DRY-RUN COMPARE: PASS` | DIFF 줄을 보고 멈춤 |
| 2 | (나)-기준: `S264C_NO_QD=1 S264C_NO_LOCAL=1 S264D_NO_WIFI=1 S264D_NO_AUDIO_B=1 S264E_NO_BT=1 S264F_NO_PWR=1 bash standalone-s264/run-s264g-launch-slot.sh <옛 .out> <옛 .link>` | 화면 보기 | 바탕화면 | 맥북 방식으로 돌아가 원인부터 찾음(그 아래는 안 함) |
| 3 | (나)-기본: 스위치 없이 같은 명령 | 화면 보기 | 바탕화면 | 그때만 반씩 나눔: 소리·Wi-Fi·전력을 켜고 블루투스·QD·LOCAL은 끔 → 실패한 쪽을 다시 반씩 |
| 4 | ESP 준비: 평소 실행기를 `NWOAS_DELIVER_MANIFEST=…/file-delivery-s243/manifests/esp-4-1.json`으로 켜고 `esp-4-1-deliver.cmd` → `S/J-esp-4-1-copy.cmd` (파일 6개) | 없음 | `NWOAS ESP-4-1 COPY COMPLETE`(ESP에서 SHA-256 되읽기 6개 일치) | `MOUNT FAIL`/`MISMATCH` → 멈춤. 원래 있던 같은 이름 파일은 `C:\NWOAS\drop\esp-4-1\before\` |
| 5 | **4-2 리허설**: `bash S/run-stage1-rehearsal-slot.sh 42 <옛 .out> <옛 .link>`. 3번 통과가 조건 | 화면 보기, 끝나면 시작 → 다시 시작 | rc 0 + 바탕화면 | rc 2(줄이 안 잡힘) → **화면 먼저**: Windows가 떠 있으면 줄만 못 잡은 것 → 시작 → 다시 시작. 화면이 죽어 있을 때만 전원 길게. rc 3 → 맥북이 쓴 바이트를 찾음 |
| 6 | **1단계 + S268 + SSD**: `bash S/run-stage1-rehearsal-slot.sh main idle -` | 없음 | `ST1-S268 DONE … VERDICT=PASS` | rc 3·4 또는 VERDICT=CHECK → **1TR로 가지 않음** |
| 7 | **일부러 실패**: `bash S/run-stage1-rehearsal-slot.sh negative <옛 .out> <옛 .link>` | 없음 | missing·fault 둘 다 `NEGATIVE PASS`, 끝에 평소 부팅 | rc 5 → 1단계가 재부팅함 = **1TR로 가지 않음** |
| 8 | `gh issue view 441 404 425 255 -R AsahiLinux/asahi-installer` | 없음 | 5장과 다른 새 실패 보고 없음 | 겹치면 멈춤 |
| 9 | **1TR 카드(8장)** | 1TR 약 10분 + 맥북 판정까지 자리에 계심(약 20~50분) | `GO1-WATCH KEEP` | 카드의 ROLLBACK/HOLD 칸 |
| 10 | (나중) 4-2 배치: `esp-4-2`(boot.bin = s264G + `nwoas_autostart=3`) 전달·복사 | 화면 보기 | 맥북 없이 켜짐 | ESP `boot.bin`을 `before\`의 D4로 되돌리는 같은 방식 작업 |

---

## 7. 1TR 명령 대조 — 이 기기 1TR에서 실제로 돈 것 (11-15 / 11-16)

| 명령 | 이 기기 1TR에서 실제로 돌았나 | 근거 | 이번 쓰임 |
|---|---|---|---|
| `sh "<스텁>/Finish Installation.app/Contents/Resources/step2.sh"` (shasum 줄 지운 사본) | **돎**, 11-16 11:2x 스텁 1TR, 등록 없던 상태에서 `bputil -nc` + `kmutil` → `STEP2-DONE` | `RESULT-20261116-morning.md:52`, `HANDOFF-2026-11-16.md:4398` | 되돌리기 1순위 |
| `stat -f %z`, `md5 -q`, `grep -q`, `/tmp` 쓰기, `bputil -d -v`, `bputil -nc -v`, `kmutil configure-boot … -v`, `reboot` | 돎(위 step2.sh 안) | `stub-135/tool/step2/step2.sh` | go1.sh |
| `mount -u -w "/Volumes/Windows 1"`, `sed -i ''` | 돎(11-16 00:4x, 콘치님 입력) | `HANDOFF-2026-11-16.md:4398` | 쓰지 않음 |
| `kmutil configure-boot`만으로 D4 재등록 | 돎, 단 **Macintosh HD**에서(09-07 `REPAIR COMPLETE`, 11-15 `kmutil returned success`) | `NWOAS-STATUS-2026-09-06.md:484`, `HANDOFF-2026-11-16.md:488` | `go1.sh --d4`(되돌리기 2순위) |
| 같은 **스텁**에 두 번째 `kmutil configure-boot` | **안 해 봄** | — | go1.sh 자체 [가설] |
| 이미 낮춘 정책에 `bputil -nc` 다시 | **안 해 봄** | — | 되돌리기 1순위 안에 있음 [가설] |
| `bless --setBoot` | 돎, 인터넷 없이는 실패(11-16 00:4x), Macintosh HD 1TR에서는 성공(11:2x) | `HANDOFF-2026-11-16.md:4440-4460` | 쓰지 않음 |
| FAT32 ESP 연결(`diskutil mount`, `mount_msdos`) | **안 해 봄** | — | 카드 3)·4). 명령은 13.5 목록에 있음 |
| `diskutil info <칸 GUID>` | 안 해 봄 | — | 쓰지 않음(장치 이름 `disk0s4`로 확인) |
| 1TR 들어가기: 전원 길게 → **옵션** | 스텁이 기본일 때는 안 해 봄. 11:2x는 미완성 스텁을 골라 들어감 | `RESULT-20261116-morning.md:52` | 카드 1). go1.sh가 Paired·1TR을 먼저 확인하므로 틀린 복구면 아무것도 안 바꾸고 멈춤 |

- 스크립트에 쓴 명령 `sh stat md5 cp grep sed bputil kmutil diskutil mount reboot shutdown ls mkdir mount_msdos cat rm`은 모두 13.5 복구 명령 목록(`research-20261116/C4-recovery-13.5-commands.txt`)에 있습니다 [사실: 기계 대조]. 켜진 화면에서 본 목록은 아닙니다.
- go1.sh는 ESP에 아무것도 쓰지 않습니다. 정책 결과는 `/tmp`와 화면에만 남깁니다. ESP는 읽기 전용으로 연결합니다. 그래서 등록 뒤 부팅은 리허설과 같은 ESP 바이트를 읽습니다.

---

## 8. 콘치님 1TR 카드 (한 화면)

```
[맥북] 6장 4~8 통과 확인.   [맥미니] Windows: 시작 → 전원 → 종료, 10초 기다림.
1) 전원 버튼을 떼지 말고 길게 → "시동 옵션 로드 중" → [옵션] → 계속 → 관리자 암호
     ✗ Windows가 그냥 켜짐 → 다시 끄고 더 길게      ✗ 노란 "다시 설치" → 아무것도 누르지 말고 전원 10초, 사진만
2) 메뉴 [유틸리티] → [터미널]
3) diskutil info disk0s4 | grep -E "Device Node|Partition UUID|File System"
     → Partition UUID 가 A3E73003-C14A-487B-9F2E-8854FE119192 인지 봄
     ✗ 다르거나 "Could not find" → diskutil list internal  사진 (바뀐 것 없음)
4) diskutil mount readOnly disk0s4  →  diskutil info disk0s4 | grep "Mount Point"   (나온 경로 = P)
     ✗ 연결 실패 → mkdir -p /tmp/esp && mount_msdos -o rdonly /dev/disk0s4 /tmp/esp   (P = /tmp/esp)
5) sh "P/m1n1/go1.sh"
     CHECK 줄들 → "ROLLBACK LINE" 두 줄이 나오면 사진 → Enter → 암호 → GO1-DONE
     ✗ STOP: ...       → 바뀐 것 없음. shutdown -h now, 사진
     ✗ NOT-PAIRED / NOT-1TR → 바뀐 것 없음. shutdown -h now → 10초 → 1)  (bless는 하지 않음)
     ✗ Enter 뒤 kmutil 실패 → 등록이 없어졌을 수 있음 → 그 자리에서 [되돌리기] R2부터
6) 맥북에 "엔터 누름"을 알리고 Enter (재시동).  화면은 검어도 정상입니다. 판정은 맥북이 합니다.
   맥북 마지막 줄을 기다립니다(자리에 계심):
     GO1-WATCH KEEP     → 끝. 등록 유지
     GO1-WATCH HOLD     → 1TR 할 일 없음. 맥북이 S268·SSD 검사를 다시 함
     GO1-WATCH ROLLBACK → 바로 [되돌리기]
     "Local policy update…" 반복 화면 → 전원 10초 → 1)~4) → sh "P/m1n1/cleanbp-local.sh" (목록만)
          → 목록을 보고 승인하시면 --apply (DELETE 입력) → 5)
[되돌리기] (같은 자리에서. 11-16에 이 스텁을 등록 없는 상태에서 살린 그 순서)
R1) 전원 10초로 끔 → 1)~2)
R2) 사진의 ROLLBACK LINE 입력:  sh "/Volumes/<스텁>/Finish Installation.app/Contents/Resources/step2.sh"
     (사진이 없으면 ls /Volumes 로 이름 확인. 11-16에는 "Windows 1")
     CHECK boot.bin = D4 → CHECK paired 1TR → Enter → 암호(bputil) → 암호(kmutil) → STEP2-DONE
     ✗ NOT-PAIRED → Enter 누르지 말고 Ctrl-C (Enter는 bless = 인터넷 필요) → shutdown -h now → R1
     ✗ bputil이 암호가 아닌 오류로 계속 실패 → Ctrl-C → 3)~4) → sh "P/m1n1/go1.sh" --d4 → GO1-DONE
R3) 맥북에 알리고 Enter → 맥북 watch --rollback 마지막 줄 "GO1-WATCH D4-BACK" = 오늘 방식으로 돌아옴
     ✗ D4-BACK이 아님 → 시동 옵션에서 Macintosh HD(그곳에도 D4) → 맥북이 상태를 봄. DFU는 콘치님 결정
```
- 스텁 이름은 1TR마다 `Windows` 또는 `Windows 1`로 보였습니다. go1.sh는 이름이 아니라 설치본 `step2.sh` 안의 VGID로 찾고, 찾은 경로로 ROLLBACK LINE을 만들어 보여 줍니다.
- 디스크 이름 `disk0`(내장)과 4번 칸은 GPT 계획 기록에서 온 것입니다 [가설]. 3)에서 UUID로 확인하고, 다르면 아무것도 바꾸지 않습니다.
- `diskutil mount readOnly`와 `mount_msdos -o rdonly`의 읽기 전용 옵션이 13.5 복구에서 되는지는 확인하지 않았습니다 [가설]. 실패하면 4)의 다음 줄로 갑니다.

---

## 9. 모르는 점 (실기에서만 알 수 있음)
| 모르는 점 | 언제 알게 되나 |
|---|---|
| iBoot가 같은 크기의 다른 내용(1단계)을 같은 phys_base·mem_size·화면 값으로 띄우는지 | 1TR 뒤 첫 부팅의 G-2 |
| 1단계가 iBoot 바로 다음에 이 트리의 시작 코드(firmware·mcc·pmgr·sep 등)를 문제없이 도는지. 지금까지는 D4 뒤에서만 돌았음 | 같음. 멈추면 ROLLBACK 신호 |
| 같은 스텁 재등록, 이미 낮춘 정책에 `bputil -nc` 다시 | 1TR |
| 13.5 복구에서 FAT32 ESP 읽기 전용 연결 | 1TR 4) |
| SError·Rust panic·`panic()` 길의 대체 프록시 | 리허설 없음 [빌드만] |
| 4-2 리허설 뒤 시작 메뉴 다시 시작이 D4로 돌아오는지 | 6장 5번 |

---

## 10. 준비물과 지문 (모두 빌드만, 실행 안 함)
| 파일 | 하는 일 | sha256 |
|---|---|---|
| `S/hv2-s264G-to-stage1.patch` | s264G 소스 → 1단계 소스 | `9588a8f1d2b1d5ea3c918691be0e58734e94122d5ba27759b5b4c346aac889b4` |
| `S/build-stage1.sh <build-s264g.sh 작업 폴더> <새 폴더>` | 1단계 4종 빌드(재현 확인: 두 번 빌드해 같음) | raw `cc2809ce111880b99204d54efdfb2d7e4c26120abaf46f62ff80607d83818a5c` (1,720,320 B) |
| `S/esp-4-1/stage1.bin` = `S/out/stage1.bin` | 등록할 1단계 (2,097,152 B, md5 `d01ab86bf4867f457d839cb4d6811dce`) | `8879def896f3577da90ec3d87caef3a549d2b4a0d9b7d6d3ee87b88ee4914778` |
| `S/rehearsal/stage1-missing.bin` / `stage1-fault.bin` / `stage1-42.bin` | 리허설 전용(맥북에서만) | `S/stage1-variants.sha256` |
| `S/stage1_layout_test.py` | 2.2 시험 | 3/3 SAME |
| `s264g_auto_test.py` | ESP 읽기·자동 시작 판단(파일 없음 경우 추가) | 1단계 소스 27/27, s264G 소스 27/27 |
| `S/run-stage1-42-rehearsal.sh`, `S/chainload_nonop.py` | 4.1 (NY1·NY3 둘 다 듣기만) | `S/SHA256SUMS` |
| `S/run-stage1-rehearsal.sh main\|missing\|fault`, `S/probe_running_image.py` | 4.2·4.3 | 같음 |
| `S/run-s268-after-stage1.sh` | 재부팅 없는 S268 + SSD | 같음 |
| `S/run-stage1-rehearsal-slot.sh 42\|main\|negative <옛 .out>\|idle <옛 .link>\|-` | 슬롯 | 같음 |
| `S/go1.sh [--d4]`, `S/cleanbp-local.sh`, `S/watch-after-go1.sh [--rollback]` | 1TR·판정 | 같음 |
| `S/make-esp-bundles.sh`, `S/J-esp-4-x-copy.cmd`, `file-delivery-s243/jobs/esp-4-x-deliver.cmd` | ESP 묶음·작업 | 같음 |

- 하이퍼바이저는 s264G 그대로입니다(`d1fea3d7…`, 이번에 `build-s264g.sh`로 다시 재현). s264H는 만들지 않았습니다. 작업 트리 `m1n1-standalone-s264`는 이제 "s264G + 1단계 패치"입니다.
- 모든 지문: `cd S && shasum -a 256 -c SHA256SUMS` → 35개 OK.
- 실패 길의 증명은 소스 수준입니다. 빌드된 ELF를 역어셈블해 확인하려 했으나 `llvm-objdump`가 이 ELF에서 멈춰 하지 못했습니다.
- (나) 스위치 6개는 슬롯 → `s264g-launch-watch.sh:68` → `s264f_launch.py` 인자로 그대로 전달됩니다 [사실: 코드].
