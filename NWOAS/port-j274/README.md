# port-j274 — windows-on-m1(J313) 방식을 우리 Mac mini(J274)로 옮기는 준비 (0~1단계)

## 한국어 요약

**결론:** 맥북 에어용 참조 프로젝트의 부팅 경로에서 기종에 묶인 값 47개를 찾아 Mac mini 값과 나란히 정리했고, 참조 프로젝트 형식 그대로 Mac mini용 메모리 배치 초안을 만들었습니다. 오프라인 계산 시험 11개는 모두 통과했습니다. **실기기에서는 아무것도 확인하지 않았고, 계산이 맞는다고 부팅된다는 뜻은 아닙니다.**

| 항목 | 개수(개) |
|---|---|
| 정리한 기종별 값 | 47 |
| 저장된 로그·장치 정보·우리 코드로 확인한 값 | 36 |
| 확인한 값으로 계산해 얻은 값 | 6 |
| 제안으로 정한 값 | 3 |
| 우리 코드가 같은 값을 쓰지만 실기기 값은 안 읽은 것 | 1 |
| 추측 | 1 |
| 그중 실기기에서 더 읽어야 하는 부분이 있는 값 | 4 |
| 오프라인 계산 시험(통과/전체) | 11 / 11 |

**게스트 메모리 시작 주소가 부팅마다 바뀌는 문제를 참조 프로젝트가 푸는 방법:** 맥북 연결 방식에서는 m1n1 작업 메모리 끝을 정해 둔 주소(0x850000000)에 고정하고, 맥북 없이 부팅할 때는 그 주소를 빌드할 때 박아 넣습니다. 장치 정보·보안 펌웨어 크기로 다시 계산한 주소가 박아 둔 값과 하나라도 다르면 부팅을 멈춥니다. 우리 로그 461개에서 시작 주소는 0x83b754000~0x83dbdc000 사이에서 372가지로 바뀌었고, RAM 끝은 늘 0xbe1000000이었습니다. 초안은 시작 주소를 0x850000000으로 고정했습니다(가장 높은 관측값보다 약 292 MiB 위).

**그대로는 맞지 않는 점(콘치님이나 설계 단계에서 정할 것):**

| 번호 | 내용 |
|---|---|
| 1 | 우리 낮은 주소 창(주소 0부터 4 GiB)은 참조 형식의 규칙(0x1000 아래 금지)과 UEFI 설정값 크기(32비트)를 넘습니다 |
| 2 | 우리는 Windows에 알려 주는 RAM 크기를 4 GiB 줄이는데(지난 S102 고장 대책), 참조 형식에는 이 칸이 없습니다 |
| 3 | 참조 쪽 가상 디스크 설정 주소(0x690000000)는 Mac mini에서 실제 PCIe(USB-A·이더넷·Wi-Fi) 주소라 쓸 수 없습니다. 초안은 우리 값 0x700000000을 씁니다 |
| 4 | 참조 쪽 화면 넘기기는 내장 화면 + 13.5 펌웨어 전용이라 HDMI인 Mac mini에서는 쓸 수 없습니다. 처음에는 화면 없이 시작해야 합니다 |
| 5 | 참조 쪽은 맥북 없이 부팅해도 예상 못 한 예외(SError 등)가 나면 여전히 연결 컴퓨터를 기다립니다. 우리 쪽과 같은 빈틈입니다 |

**맥북 Python이 하던 일 대응:** 부팅 때 하던 일 17개 중 6개는 참조 C 코드가 그대로 해 주고, 4개는 다른 방식으로 피하고, 2개는 일부만, 4개는 없습니다(1개는 필요 없음). 실행 중에 하던 일 9개 중 4개는 있고 5개는 없습니다. 자세한 표는 `GAP-LIST.md`에 있습니다.

---

## English details

Date: 2026-11-15. Step 0-1 of `N/nwoas_scripts/plan-20261115/port/boot.md` §4
and `port/strategy.md` §4 ("0단계"). Offline work only:

- no `/dev/cu.*` opened, no launcher/proxy/chainload/run_guest executed;
- nothing in `external/windows-on-m1` executed or imported (its JSON layout was
  read as data by one test; its sources were read as text);
- no existing file modified; everything is new under this directory;
- the J274 ADT dump was parsed with our own `m1n1_windows/proxyclient/m1n1/adt.py`
  via a scratch script outside the repo.

`E/` = `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1`,
`N/` = `/Users/angigyeom/Desktop/DEV/NWOAS`.

### Files

| File | Content |
|---|---|
| `J274-BOARD-PROFILE.md` | every J313-specific constant in the reference boot path with the J274 value, status and file:line evidence; UNKNOWN list |
| `j274-board-profile.json` | same 47 rows, machine-readable |
| `config/j274-guest-layout.json` | draft J274 layout in the exact reference key set (`E/guest_layout.py:15-37`) |
| `J274-GUEST-LAYOUT.md` | per-field basis, how the reference handles the moving `phys_base`, and 7 format conflicts (L-1..L-7) |
| `GAP-LIST.md` | each S231 host-Python duty (census table) mapped to reference C, with verdicts |
| `tools/check_j274_layout.py` | independent offline checker (re-states the reference rules and the `hv_assisted_layout_compute` arithmetic) |
| `tests/test_check_j274_layout.py` | 11 unittest cases |

### How to re-run the offline checks

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/port-j274
PYTHONDONTWRITEBYTECODE=1 python3 -I -m unittest discover -s tests -v
PYTHONDONTWRITEBYTECODE=1 python3 -I tools/check_j274_layout.py
```

Result on 2026-11-15: 11/11 tests OK; checker exit 0 with one intended reference
rule failure (`low_mem_ipa < 0x1000`) and one finding (`low_mem_size` exceeds the
UINT32 PCD).

What the tests prove (offline only):

| Test | Shows |
|---|---|
| key set | draft has exactly the reference keys |
| J313 file passes restated rules | the re-statement of `E/guest_layout.py:43-104` is not stricter than the original |
| S231 arithmetic | the reference formula reproduces our S231 log: base `0x83ca24000` → guest `0x83dae0000`, bootargs `0x8400d8000` (LOG:320-337) |
| low-window backing | `0xbe1000000 − 4 GiB − 0x34000 = 0xae0fcc000` (LOG:348) |
| pinned addresses | `adt_base`/`firmware_base`/`boot_args_base` re-derive exactly from J274 sizes (padded FD) |
| unpadded FD | a 0x1d88000 FD would move `boot_args_base` (fail-closed in the reference runtime) |
| ECAM | the reference ECAM `0x690000000` would collide with the real J274 apcie |

### Facts vs hypotheses (short)

FACT: board-id 0x22, J274AP, external display, RAM end 0xbe1000000, iBoot sizes
(ADT 0x5c000, TrustCache 0x60000, SEPFW 0x59c000, preoslog 0x40000), MPIDRs,
xHCI/DART/vUART addresses, our ECAM/BAR/IRQ 900, our low window, our Mu PCDs.

HYPOTHESIS / UNKNOWN: whether the stage-1 heap stays below `0x850000000`;
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

Nothing here has been built, flashed or booted. Hardware validation is only
possible on the Mac mini by the user.
