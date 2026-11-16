# S250 — 하이퍼바이저가 화면 장치(DCP)를 직접 쓰기 위한 비동기 화면 전환 이식

**상태 (2026-11-16): 맥북에서 빌드만 성공. 맥미니에서는 아무것도 실행하지 않았습니다.**
빌드 성공은 동작 성공이 아닙니다. 화면이 실제로 바뀌는지는 맥미니 앞에서 사람이 모니터를 보고서만 판정할 수 있습니다.

표기: **[사실]** = 이번 작업에서 파일·줄·명령 출력으로 직접 확인. **[가설]** = 확인하지 않은 추측.

## 1. 한눈에

| 항목 | 내용 |
|---|---|
| 새 m1n1 | `out-hv2/m1n1-g250E-agx-hv2-dcpasync.bin` (+ `.elf`), 빌드 태그 `v1.0.2-1474-gbddf7f06-g250E` |
| 바탕 | 지금 쓰는 g249D 소스(`m1n1-gpu-hv2`, AGX 로컬 예약 1 GiB 그대로 [사실 `src/hv_agx_local_reserve.h:20`]) |
| 더한 것 | 원본(paulsmir/windows-on-m1, 이하 UP)의 AFK 비동기 명령 계층 + `dcp_ib_swap_async_*` + 호스트가 부르는 시험용 프록시 명령 6개 |
| 시험 스크립트 | `tools-hv2/dcp_swap_test.py` (실행하지 않았음) |
| 하이퍼바이저 | 이 시험은 하이퍼바이저를 켜지 않습니다. 새 명령은 부르지 않으면 아무 일도 하지 않습니다 |

## 2. 옮긴 파일

| 파일 (`m1n1-gpu-hv2/`) | 처리 | 내용 |
|---|---|---|
| `src/afk_command.c/.h`, `src/afk_command_internal.h`, `src/afk_command_owner.c/.h`, `src/afk_deferred_message.c/.h` | **UP 그대로 복사** (새 파일) | 비동기 명령 상태 기계, 엔드포인트 버퍼 소유권, 알림 한 칸 보관 |
| `src/afk.c` | **병합** (HV2 바탕) | HV2의 NWOAS 링 구조 계산(`afk_ring_block_size`, 128/64바이트 블록, 범위 검사, Tahoe 거부)은 그대로 두고, UP의 명령 백엔드(`afk_epic_command_backend_*`, `afk_epic_command_reap`, `afk_epic_dispatch_deferred`)를 더함. 동기 `afk_epic_command()`는 HV2 동작 그대로(순번 0, 알림 즉시 처리) 두되 비동기 명령이 버퍼를 쥐고 있으면 `-16`(BUSY)을 돌려줌 |
| `src/afk.h` | 병합 | `afk_command.h` 포함, 새 함수 선언 3개(`afk_epic_dispatch_deferred`, `afk_epic_command_reap`, `afk_epic_command_idle`) |
| `src/dcp_iboot.c/.h` | **UP와 같아짐 + 도우미 2개** | HV2 원본은 UP의 조상이었음 [사실: 차이는 UP 추가분뿐]. `ready` 표시, `dcp_ib_is_ready`, 종료 실패 시 엔드포인트 유지, `dcp_ib_swap_async_init/begin/poll` 추가. NWOAS 도우미 `dcp_ib_dispatch_deferred`, `dcp_ib_command_idle` |
| `src/rtkit.c/.h` | 함수 1개 추가 | `rtkit_can_send()` (UP 그대로) |
| `src/display.c/.h` | NWOAS 구역 추가 | `display_nwoas_dcp_*` 6개 (§4). 기존 함수는 바꾸지 않음 |
| `src/proxy.c/.h` | 명령 6개 추가 | `0x1180`–`0x1185` |
| `proxyclient/m1n1/proxy.py` | 상수·메서드 추가 | 같은 번호 |
| `Makefile` | 오브젝트 3개 추가 | `afk_command.o afk_command_owner.o afk_deferred_message.o` |

옮기지 않은 것: UP `afk_raw_router.*`(비동기 화면 전환에 쓰지 않음), UP `rtkit.c`의 전송 예약·지연 큐·`rtkit_recv_one_quiet`(아래 §3), UP `display.c`의 `display_scanout_*`(참고만 함).

## 3. UP와 다르게 한 곳 [사실]

| 위치 | UP | 이번 이식 | 이유 |
|---|---|---|---|
| 송신 직렬화 | `rtkit_try_reserve_send` → `rtkit_commit_reserved_send` (ASC 예약 API) | 비차단 모드는 `rtkit_can_send()`가 거짓이면 바로 BUSY, 아니면 기존 `rtkit_send()` | HV2의 rtkit/asc에는 예약 API가 없음. HV2의 AFK 호출자는 모두 한 실행 흐름(프록시·부팅 코드)에서만 부름. **여러 CPU가 동시에 보내면 안전하지 않음** — 하이퍼바이저 안에서 쓰려면 다시 봐야 함 |
| 한 번에 하나 받기 | `rtkit_recv_one_quiet()` (메모리 할당·출력 없음) | HV2 `afk_epic_poll(…, false)` 한 번 | UP 방식은 rtkit 지연 큐가 필요. HV2 방식은 다른 엔드포인트의 GETBUF/INIT도 처리하지만 **할당·출력을 할 수 있어 예외 처리 중에는 못 씀** |
| 엔드포인트 시작 실패 | 대기 중 오류면 `break` 후 계속 | HV2의 `goto err` 유지 | UP 쪽은 실패한 엔드포인트를 돌려줄 수 있음 |
| `afk_epic_work()`의 보관 알림 처리 | 실패하면 오류 반환 | 결과 무시 | HV2 동기 경로가 `afk_epic_handle_std_service()` 실패를 무시해 온 것과 맞춤 |
| 동기 `afk_epic_command()` | 비동기 계층 위의 감싸개(순번 증가, 알림 보관) | HV2 원래 코드 + 소유권 확인 | 기존 호출자(`dcp/system_ep.c:94`, `dcp/dptx_port_ep.c:119`, `dcp_iboot.c`)의 동작을 바꾸지 않기 위해 |
| 엔드포인트 종료 실패 | 버퍼를 풀지 않고 유지 | UP 따름 | IOP가 아직 쓸 수 있는 메모리를 풀지 않기 위해 |

비동기 명령은 응답의 순번(`sub.seq`)과 rx 버퍼 주소가 보낸 것과 같아야 완료로 봅니다(UP 그대로). 맥미니 13.5 펌웨어가 이 두 값을 되돌려 주는지는 **확인하지 않았습니다 [가설]**. UP 기록에는 이 경로로 내장 화면을 되돌린 실험이 있지만(J313 노트북, 내장 화면) [사실 `external/windows-on-m1/investigation/EXPERIMENTS.md:28945-28948`], 외장 HDMI(J274)에서는 처음입니다. 또 UP의 화면 출력 서비스는 외장 화면이면 아예 쓰지 않습니다 [사실 UP `src/display.c:623-628` `!display_is_external`].

추가로 알아 둘 것: 지금 HV2의 `display_configure()`는 화면 전환에 `IBOOT_SET_SURFACE`(명령 1) 하나만 씁니다 [사실 `src/display.c` `display_swap()`]. 비동기 경로는 `SWAP_BEGIN`(15) → `SWAP_SET_LAYER`(16) → `SWAP_END`(18) 세 단계입니다. 맥미니에서 이 세 단계를 쓴 적은 **없습니다 [사실: HV2에 `dcp_ib_swap_begin` 호출자 없음]**.

## 4. 새 프록시 명령 (`src/proxy.h`, `proxyclient/m1n1/proxy.py`)

번호 `0x1180`–`0x1185`는 두 쪽 모두에서 비어 있었습니다 [사실: `P_DISPLAY_*`는 `0x1100`–`0x1104`, 다음 묶음 `P_DAPF_*`가 `0x1200`].

| 번호 | 이름 | 인자 | 돌려주는 값 |
|---|---|---|---|
| `0x1180` | `P_NWOAS_DCP_PROBE` | 없음 | `0x303532504344574e` ("NWDCP250"). 장치에 아무것도 하지 않음 |
| `0x1181` | `P_NWOAS_DCP_START` | `flags`, `info_ptr`, `info_size` | 0 / 음수 오류. `flags` 비트0 = `display_configure(NULL)`(부팅 때와 같은 모드 설정 + 기본 화면 전환), 0이면 `display_start_dcp()`만. 그 뒤 HPD·타이밍 모드 목록·현재 모드를 `info_ptr`에 씀(머리 80바이트 + 모드당 16바이트 `{valid,width,height,fps(16.16)}`) |
| `0x1182` | `P_NWOAS_DCP_POWER` | `on` | `dcp_ib_set_power()` 결과. 화면 전환이 진행 중이면 -16 |
| `0x1183` | `P_NWOAS_DCP_SWAP` | `pa`, `width`, `height`, `stride`, `format`(0=BGRA8888만), `flags`(비트0=캐시 정리 생략) | 양수 쿠키 / 음수 오류. 버퍼는 `iova_alloc` 후 `display_map_fb()`로 dart-disp0와 dart-dcp에 **같은 IOVA**로 붙임(`display_configure()`가 새 화면 버퍼에 쓰는 방식과 같음). 같은 버퍼는 한 번만 붙임(최대 4개). 보내기 전에 버퍼 전체 `dc civac` |
| `0x1184` | `P_NWOAS_DCP_POLL` | `cookie`, `max_wait_us`, `out_ptr` | 묶은 값: 비트56-63 상태(0 진행 중, 1 APPLIED, 2 FAILED, 3 없음), 48-55 단계, 32-47 swap_id 하위 16비트, 0-31 보낸 뒤 지난 µs. `max_wait_us`>0이면 장치 안에서 끝날 때까지 기다림(시간은 장치가 잼). `out_ptr`이 있으면 48바이트 `{status, stage, swap_id, cmd_result, elapsed_us, cookie, clean_us, endpoint_idle}` |
| `0x1185` | `P_NWOAS_DCP_SHUTDOWN` | `mode`(`dcp_shutdown_mode`, 1 = 외장이면 재움) | 비트0 기본 화면 복귀, 비트1 시험 버퍼 떼어냄, 비트2 종료함. 시험 버퍼가 화면에 있으면 기본 화면으로 되돌리고 APPLIED + 150 ms 뒤에 떼어냄(UP `display_scanout_quiesce_*` 순서). 되돌리기에 실패하면 떼어내지 않음 |

"APPLIED"는 세 번째 명령(`SWAP_END`)의 응답을 받았다는 뜻입니다. 모니터가 실제로 새 그림을 띄운 순간이 아닙니다. 그래서 UP도 APPLIED 뒤 150 ms를 더 기다립니다 [사실 UP `src/display.c:822`].

## 5. 맥미니에서 돌리는 법 (콘치님 승인 후에만)

앞 단계는 지금까지 g249D를 올리던 절차와 같고, **`run_guest.py`(Windows 부팅)는 하지 않습니다.** 근거 파일: `nwoas_scripts/agxmed1g-s249-guest-test.sh`.

그 파일에서 그대로 가져오는 줄 [사실]:

- `:98` `"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$LOG" 2>&1`
- `:101` `"$PY" -B -u "$S244/probe_fw135_s244.py" > "$LOG.probe" 2>&1 || PROBE_RC=$?`
- `:104` `[ "$PROBE_RC" -eq 0 ] || { echo "STOP: S244 probe exit $PROBE_RC …"; exit 1; }` (설치된 D4 m1n1 + 13.5 펌웨어가 아니면 멈춤)
- `:105` `"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1`
- `:106` `grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || exit 2`

여기서 `HV`만 g250E로 바꿉니다. 실행 순서:

```bash
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1=$ROOT/m1n1_windows
PY=$M1N1/.venv-hv/bin/python3
S244=$ROOT/nwoas_scripts/display-s244
HV=$ROOT/port-j274/gpu/hv/out-hv2/m1n1-g250E-agx-hv2-dcpasync.bin
LOG=$ROOT/nwoas_scripts/logs/dcp-s250-$(date '+%Y%m%d-%H%M%S').log
export M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH=$M1N1/proxyclient:$ROOT/nwoas_scripts
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = 9693cc6ac4a50c8428e58a1ddfd21011188e026d8ec4cd3dab6216dbeec0c0ee ] || exit 1
"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$LOG" 2>&1
PROBE_RC=0; "$PY" -B -u "$S244/probe_fw135_s244.py" > "$LOG.probe" 2>&1 || PROBE_RC=$?
[ "$PROBE_RC" -eq 0 ] || { echo "STOP: probe exit $PROBE_RC"; exit 1; }
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || exit 2
caffeinate -dims "$PY" -u "$ROOT/port-j274/gpu/hv/tools-hv2/dcp_swap_test.py" \
    --out "$LOG.json" 2>&1 | tee -a "$LOG"
# 끝나면 설치된 D4로 돌아가기 (nwoas_scripts/agx-g1-s246/g1_run.sh:20 과 같은 방법)
timeout 60 "$PY" -u "$M1N1/proxyclient/tools/reboot.py"
```

- 스크립트는 먼저 신원 확인을 합니다: 로컬 바이너리 sha256, 실행 중 m1n1의 `base + 0x748e8` 위치 빌드 태그 바이트, `P_NWOAS_DCP_PROBE` 응답, `/chosen/firmware-version` = `iBoot-8422.141.2`. 하나라도 다르면 DCP 명령을 하나도 보내지 않고 종료 코드 2로 멈춥니다. D4는 이 명령이 없어 여기서 멈춥니다. `--gate-only`로 이 확인만 따로 할 수 있습니다.
- 진행: START(모드 설정) → HPD·모드 목록 출력 → 1920x1080 버퍼 2개(A 빨강|파랑, B 초록|자홍, 아래쪽 띠가 장마다 길어짐) → A/B 번갈아 120번(장마다 250 ms 보여 줌, `--hold-ms`) → 전원 끄기 5초 → 켜기 → A 한 번 더 → HPD 다시 → 종료(기본 화면 복귀, DCP 재움) → 최소/평균/최대 표.
- 전원을 켠 뒤 모니터가 계속 어두우면, 다음 시험은 `--reconfigure-after-power`(켠 뒤 모드를 다시 설정)를 붙여 다시 합니다.
- 이 시험은 GPU 일을 하지 않습니다(화면 장치만). 시간은 약 1분입니다 [가설: 120 × (250 ms + 왕복)].

## 6. 판정

**로그만으로는 통과가 아닙니다.** 맥미니에 연결된 모니터를 사람이 보고 아래가 모두 맞아야 통과입니다.

| # | 모니터에서 보여야 하는 것 | 로그에서 함께 볼 것 |
|---|---|---|
| 1 | 빨강|파랑 화면과 초록|자홍 화면이 번갈아 바뀜(약 1초에 4번), 아래 흰 띠가 점점 길어지다 다시 짧아짐 | `swap N A/B APPLIED` 120줄 |
| 2 | 전원 끄기 동안 약 5초 모니터가 꺼지거나 신호 없음 | `set_power(off) ret=0` |
| 3 | 켠 뒤 다시 빨강|파랑 화면 | `final swap A APPLIED`, 끝의 `hpd=1` |
| 4 | 끝에 부팅 때 화면(m1n1 기본 화면)으로 돌아감 | `shutdown result=0x7` |

모니터에 1이 안 보이는데 로그는 APPLIED면 **실패**로 적습니다(명령 응답과 실제 표시는 다름). 2에서 모니터가 안 꺼지면 "전원 명령이 외장 HDMI에서 화면을 끄지 않음"으로 기록하고 실패로 적습니다 — 원래 이 명령은 내장 화면용으로 쓰였습니다 [사실 `src/display.c` `display_configure()`의 `!display_is_external` 분기] [가설: HDMI에서도 꺼짐].

## 7. 위험

- **DCP 펌웨어가 죽으면 전원을 완전히 뽑았다 꽂아야만 돌아올 수 있습니다 [가설].** 프록시 재부팅으로 안 돌아오고 모니터가 계속 검으면, 맥미니 전원선을 10초 뽑았다 다시 꽂습니다. 스크립트는 종료가 실패하면 이 안내를 출력합니다.
- 응답 순번·rx 주소가 맞지 않으면 화면 전환마다 500 ms에서 시간 초과 → FAILED. 시간 초과 뒤에는 그 엔드포인트가 "늦은 응답"을 기다리는 상태로 잠겨 동기 명령도 BUSY가 됩니다. 종료(`0x1185`)가 엔드포인트를 닫으면서 풀립니다.
- 시험 버퍼는 파이썬 힙(m1n1 힙 위 128 MiB 지점부터)에 있습니다. 종료가 버퍼를 떼어내지 못하면 스크립트는 그 메모리를 풀지 않습니다.
- 송신 직렬화가 UP보다 약합니다(§3). 이 시험은 한 흐름이라 괜찮지만 하이퍼바이저 안에서 쓰기 전에 다시 봐야 합니다.

## 8. 빌드·호스트 시험 결과 (맥북, 2026-11-16)

| 빌드 | 명령 | rc | 경고 수 | 오류 | 크기 (바이트) | sha256 |
|---|---|---|---|---|---|---|
| g250E AGX | `buildvar.sh … v1.0.2-1474-gbddf7f06-g250E NWOAS_AGX=1` | 0 | 45 (이전과 같음) | 0 | 2,260,992 | `9693cc6ac4a50c8428e58a1ddfd21011188e026d8ec4cd3dab6216dbeec0c0ee` |
| g250E 기본 | 같은 명령, `NWOAS_AGX=1` 없음 | 0 | 45 | 0 | 2,179,072 | `04ed18f2ea2c16ed6cab32ce1406900808c90bbb1c436669ddb80889268b2c37` |
| `.elf` (AGX) | — | — | — | — | 3,483,120 | `02f77cbe20995de19faef73b53a396a6b92ba8485df2d93f08cfae32c2e98ee6` |

경고 45개의 파일별 수는 이전 빌드 로그(`out-hv2/build-g247A.log` 등)와 같고, 새로 고친 파일에서 새 경고는 없습니다(`dcp_iboot.c` 1개는 원래 있던 구조체 경고) [사실].

| 호스트 시험 | 결과 |
|---|---|
| `tools-hv2/hosttest/run.sh …/g0-adt-20261116-live1/adt.bin` | rc 0, `ALL PASS: 0 failure(s)` |
| UP `tests/afk_command_test.c` (HV2 사본으로 빌드) | `ok` |
| UP `tests/afk_command_owner_test.c` | `ok` |
| UP `tests/afk_deferred_message_test.c` | `ok` |

UP 단위 시험은 가짜 백엔드로 상태 기계만 봅니다. `afk.c`에 병합한 실제 백엔드와 장치 응답은 시험하지 못했습니다.

## 9. 실기기 결과 (2026-11-16 17:31, 맥미니 J274, 13.5 스텁, g250E 체인로드, 하이퍼바이저 없이)

| # | 판정 | 근거 |
|---|---|---|
| 1 화면 넘기기 | **통과** | 콘치님 눈 확인: 반쪽씩 빨강/파랑 ↔ 초록/자홍이 번갈아 바뀌고 아래 띠가 차오름. 로그 120번 모두 APPLIED |
| 2 전원 끄기 | 화면이 검게 됨 (콘치님) — "신호 없음"인지는 미확인 | `set_power(0) ret=0` |
| 3 전원 켠 뒤 다시 표시 | **실패** — 계속 검은 화면 | `set_power(1) ret=0`, `final swap A APPLIED` 인데 화면 없음. 외장 HDMI는 켠 뒤 모드를 다시 설정해야 하는 것으로 봄 [가설] → 다음 시험은 `--reconfigure-after-power` |
| 4 종료 뒤 기본 화면 | 통과 (재부팅 뒤 보통 화면) | `shutdown result=0x7` |

| 측정 | 횟수 | 최소 | 평균 | 최대 |
|---|---|---|---|---|
| submit→APPLIED (μs, 장치) | 120 | 677 | 738.5 | 1326 |
| 넘기기 전 캐시 정리 (μs) | 120 | 62 | 62.8 | 80 |
| 넘기기+확인 왕복 (ms, 맥북) | 120 | 0.9 | 1.2 | 9.9 |

모니터 해상도 목록 33개(720x480 ~ 3840x2160@60). 응답 순번·rx 주소 문제(§7)는 나타나지 않음.
