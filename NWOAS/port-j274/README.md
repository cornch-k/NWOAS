# port-j274 — windows-on-m1(J313) 방식을 우리 Mac mini(J274)로 옮기는 준비 (0~1단계)

## 한국어 요약

**결론:** 맥북 에어용 참조 프로젝트의 부팅 경로에서 기종에 묶인 값 47개를 찾아 Mac mini 값과 나란히 정리했고, 참조 프로젝트 형식 그대로 Mac mini용 메모리 배치 초안을 만들었습니다. 오프라인 계산 시험 24개는 모두 통과했습니다(2026-11-15 검토 지적 수정 뒤). **실기기에서는 아무것도 확인하지 않았고, 계산이 맞는다고 부팅된다는 뜻은 아닙니다.**

| 항목 | 개수(개) |
|---|---|
| 정리한 기종별 값 | 47 |
| 저장된 로그·장치 정보·우리 코드로 확인한 값 | 36 |
| 확인한 값으로 계산해 얻은 값 | 6 |
| 제안으로 정한 값 | 3 |
| 우리 코드가 같은 값을 쓰지만 실기기 값은 안 읽은 것 | 1 |
| 추측 | 1 |
| 그중 실기기에서 더 읽어야 하는 부분이 있는 값 | 4 |
| 오프라인 계산 시험(통과/전체) | 24 / 24 |

**게스트 메모리 시작 주소가 부팅마다 바뀌는 문제를 참조 프로젝트가 푸는 방법:** 맥북 연결 방식에서는 m1n1 작업 메모리 끝을 정해 둔 주소(0x850000000)에 고정하고, 맥북 없이 부팅할 때는 그 주소를 빌드할 때 박아 넣습니다. 장치 정보·보안 펌웨어 크기로 다시 계산한 주소가 박아 둔 값과 하나라도 다르면 부팅을 멈춥니다. 우리 로그 461개에서 시작 주소는 0x83b754000~0x83dbdc000 사이에서 372가지로 바뀌었고(2026-11-15 다시 세면 486개 파일에서 394가지, 최솟값·최댓값은 같음), RAM 끝은 늘 0xbe1000000이었습니다. 초안은 시작 주소를 0x850000000으로 고정했습니다(가장 높은 관측값보다 약 292 MiB 위).

**그대로는 맞지 않는 점(콘치님이나 설계 단계에서 정할 것):**

| 번호 | 내용 |
|---|---|
| 1 | 우리 낮은 주소 창(주소 0부터 4 GiB)은 참조 형식의 규칙(0x1000 아래 금지)과 UEFI 설정값 크기(32비트)를 넘습니다 |
| 2 | 우리는 Windows에 알려 주는 RAM 크기를 4 GiB 줄이는데(지난 S102 고장 대책), 참조 형식에는 이 칸이 없습니다 |
| 3 | 참조 쪽 가상 디스크 설정 주소(0x690000000)는 Mac mini에서 실제 PCIe(USB-A·이더넷·Wi-Fi) 주소라 쓸 수 없습니다. 초안은 우리 값 0x700000000을 씁니다 |
| 4 | 참조 쪽 화면 넘기기는 내장 화면 + 13.5 펌웨어 전용이라 HDMI인 Mac mini에서는 쓸 수 없습니다. 처음에는 화면 없이 시작해야 합니다 |
| 5 | 참조 쪽은 맥북 없이 부팅해도 예상 못 한 예외(SError 등)가 나면 여전히 연결 컴퓨터를 기다립니다. 우리 쪽과 같은 빈틈입니다 |
| 6 | 초안의 게스트 메모리 범위 안에 iBoot가 잡아 둔 예약 영역(carveout) 20개가 들어 있습니다(9월 장치 정보 기준). 그중 1개(region-id-48)는 장치 정보·UEFI·부트 인자·가상 화면 자리와 겹치고, 19개는 가상 디스크 자리 안에 있습니다. 어느 보조 프로세서의 영역인지 모르므로, 저장 장치(NVMe) 영역이 섞여 있다면 macOS 저장 장치에 닿을 수 있는 유일한 경로입니다 [가설: 지금 S231도 같은 범위를 쓰므로 해가 없을 수도 있음]. 설계 단계에서 풀어야 할 막힘 항목(L-8)으로 올렸습니다 |

검사 도구(`tools/check_j274_layout.py`)는 이제 막힘 항목이 하나라도 있으면 종료 코드 1을 냅니다. 사람이 `--allow L-1`처럼 하나씩 받아들이겠다고 적은 항목만 넘어갑니다. 지금 초안은 L-1·L-2·L-8 때문에 1입니다.

**맥북 Python이 하던 일 대응:** 부팅 때 하던 일 17개 중 6개는 참조 C 코드가 그대로 해 주고, 4개는 다른 방식으로 피하고, 2개는 일부만, 4개는 없습니다(1개는 필요 없음). 실행 중에 하던 일 9개 중 4개는 있고 5개는 없습니다. 자세한 표는 `GAP-LIST.md`에 있습니다.

---

## English details

Date: 2026-11-15. Step 0-1 of `N/nwoas_scripts/plan-20261115/port/boot.md` §4
and `port/strategy.md` §4 ("0단계"). Offline work only:

- no `/dev/cu.*` opened, no launcher/proxy/chainload/run_guest executed;
- nothing in `external/windows-on-m1` executed or imported (its JSON layout was
  read as data by one test; its sources were read as text);
- no existing file modified; everything is new under this directory;
- the J274 ADT dump was parsed with our own `m1n1_windows/proxyclient/m1n1/adt.py`.
  The first pass used a scratch script outside the repo (deleted; interpreter not
  recorded). It is now reproducible: `tools/extract_adt_facts.py` writes
  `config/j274-adt-facts.json` with the dump's sha256, the interpreter and the exact
  command (see "How to re-run" below).

`E/` = `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1`,
`N/` = `/Users/angigyeom/Desktop/DEV/NWOAS`.

### Files

| File | Content |
|---|---|
| `J274-BOARD-PROFILE.md` | every J313-specific constant in the reference boot path with the J274 value, status and file:line evidence; UNKNOWN list |
| `j274-board-profile.json` | same 47 rows, machine-readable |
| `config/j274-guest-layout.json` | draft J274 layout in the exact reference key set (`E/guest_layout.py:15-37`) |
| `J274-GUEST-LAYOUT.md` | per-field basis, how the reference handles the moving `phys_base`, and 8 format conflicts (L-1..L-8; L-8 is a blocker) |
| `GAP-LIST.md` | each S231 host-Python duty (census table) mapped to reference C, with verdicts |
| `tools/check_j274_layout.py` | independent offline checker (re-states the reference rules and the `hv_assisted_layout_compute` arithmetic; carveout overlap check; fail-closed exit code with `--allow L-n`) |
| `tools/extract_adt_facts.py` | extracts apcie reg[0], disp0 `external`, compatible and the `/chosen/carveout-memory-map` table from a saved ADT dump with our `adt.py` |
| `config/j274-adt-facts.json` | its output for `experiments/tahoe-afk-20260906/d6-adt.bin` (sha256 cb773d76…5f7f, 78 carveouts) |
| `tests/test_check_j274_layout.py` | 24 unittest cases |

### How to re-run the offline checks

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/port-j274
python3 -I -B -m unittest discover -s tests -v
python3 -I -B tools/check_j274_layout.py                      # exit 1: L-1, L-2, L-8 block
python3 -I -B tools/check_j274_layout.py --allow L-1 --allow L-2 --allow L-8   # exit 0
# ADT facts (needs `construct`, so the hv venv; -I -B leaves no __pycache__):
cd /Users/angigyeom/Desktop/DEV/NWOAS
m1n1_windows/.venv-hv/bin/python3 -I -B port-j274/tools/extract_adt_facts.py \
    experiments/tahoe-afk-20260906/d6-adt.bin port-j274/config/j274-adt-facts.json
```

`-I` implies `-E`, so `PYTHONDONTWRITEBYTECODE` is ignored; `-B` is what stops
`__pycache__` from being written.

Checker exit codes: 0 = nothing blocks (every L-n blocker absent or accepted with
`--allow`); 1 = something blocks (MISMATCH, missing/extra key or another reference
rule can never be accepted); 2 = usage error. Without a carveout list
(`--no-carveouts`, or the facts file missing) L-8 is reported as UNCHECKED and blocks.

Result on 2026-11-15 (after the review fixes): 24/24 tests OK. Checker exit 1 by
default (L-1 `low_mem_ipa < 0x1000`, L-2 `low_mem_size` exceeds the UINT32 PCD,
L-8 20 carveouts inside guest RAM; region-id-24 is the whole RAM region and is INFO);
exit 0 with `--allow L-1 --allow L-2 --allow L-8`; exit 1 with `--fw-size 0x1d88000`
even with every `--allow` (MISMATCH boot_args_base).

What the tests prove (offline only):

| Test | Shows |
|---|---|
| key set | draft has exactly the reference keys |
| J313 file passes restated rules | the re-statement of `E/guest_layout.py:43-104` is not stricter than the original |
| S231 arithmetic | values parsed from the S231 log itself: base `0x83ca24000` → guest `0x83dae0000`, bootargs `0x8400d8000` (LOG:320-337); sizes and RAM end from the same log |
| low-window backing | parsed `[s174] low-window map` line: `0xbe1000000 − 4 GiB − 0x34000 = 0xae0fcc000` (LOG:348) |
| natural phys_base | read-only scan of every file in `nwoas_scripts/logs`: draft `phys_base` is above the highest "Physical memory:" base found (2026-11-15: 394 distinct values in 486 files, max `0x83dbdc000`) |
| exit code | the checker fails closed; each L-n must be accepted explicitly; MISMATCH and other reference rules cannot be accepted |
| carveouts | the recorded table gives region-id-48 over ADT/firmware/boot args/vfb and 19 ids inside the RAM disk; synthetic edge cases |
| ADT facts | `config/j274-adt-facts.json` sha256 matches the dump; apcie reg[0] and disp0 `external` |
| pinned addresses | `adt_base`/`firmware_base`/`boot_args_base` re-derive exactly from J274 sizes (padded FD) |
| unpadded FD | a 0x1d88000 FD would move `boot_args_base` (fail-closed in the reference runtime) |
| ECAM | the reference ECAM `0x690000000` would collide with the real J274 apcie |

### Facts vs hypotheses (short)

FACT: board-id 0x22, J274AP, external display, RAM end 0xbe1000000, iBoot sizes
(ADT 0x5c000, TrustCache 0x60000, SEPFW 0x59c000, preoslog 0x40000), MPIDRs,
xHCI/DART/vUART addresses, our ECAM/BAR/IRQ 900, our low window, our Mu PCDs.

HYPOTHESIS / UNKNOWN: whether the carveouts inside guest RAM (L-8) are harmless and who
owns them (only the September dump was read);
whether the stage-1 heap stays below `0x850000000`;
whether the S231 RAM-LOW map, `setup_adt` USB-node removal and `__OS_LOG`
removal are needed; raw ACTLR/AMX values; current carveout table (only a
September dump read); xHCI low-DMA end for a 4 GiB window; whether iBoot sizes
change on updates.

### Open questions for the main session

1. Low window: keep S231's IPA-0 / 4 GiB window (needs a J274 relaxation of the
   reference rule and a new "advertised RAM size" field), or move to the
   reference's 1 MiB-guarded window (needs Mu changes and hardware retest)?
2. Payload shape: keep the guest m1n1 prefix (then a C CPU_START drop-hook is
   required) or enter Mu directly like the reference (then Mu must be built for a
   fixed `firmware_base` or keep its runtime relocation)?
3. FD size: use the padded 0x1e00000 FD (draft) or the raw 0x1d88000 FD? It moves
   `boot_args_base`.
4. The 13.5 vs 26.x stub decision (`DECISIONS.md`) still gates any reference
   display/GPU code; this step assumes display=none.
5. Carveouts inside guest RAM (L-8): unmap them from stage 2 (like S231 unmaps the
   TZ carveouts), move `phys_base`/the RAM disk, or accept them after the current
   boot's table and owners are read?

Nothing here has been built, flashed or booted. Hardware validation is only
possible on the Mac mini by the user.
