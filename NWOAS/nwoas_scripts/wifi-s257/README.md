# S257 내장 Wi-Fi 2·3단계: 칩 펌웨어를 올려 "돌고 있고 답한다"까지, 그리고 주변 Wi-Fi 이름 검색

표기: **[사실]** = 파일·행 번호·실측으로 확인한 내용. **[가설]** = 확인하지 않은 추측.

## 0. 결론

- `wifi_up.py`는 맥미니가 m1n1 대기 상태(D4 프록시, 체인로드 전)일 때 맥북에서 실행해, BCM4378 Wi-Fi 칩을 **전원 켜기 → PCIe 연결 → 칩 번호 확인 → OTP 읽기 → 펌웨어·NVRAM을 칩 메모리(TCM)에 쓰기 → 칩 CPU 시작 → "준비됨" 신호(공유 영역 주소) 받기 → 펌웨어 콘솔에서 버전 문자열 읽기**까지 합니다. 끝나면 오류가 나도 항상 전원을 끕니다.
- [사실] `fw` 단계는 2026-11-18 11:36 실기에서 `PASS_FW_UP`을 받았습니다(칩 4378 리비전 3, 펌웨어 18.20.511.0.7.8.207, 공유 영역 `0x4d63c0`, 플래그 `0x78810007`, 판 7, 전원 끄기 확인). 기록: `runs/run-20261118-113659-qq0o4o_8/`, `../logs/wifi-s257-20261118-113659*`.
- [사실] `scan` 단계는 2026-11-18 12:13 실기에서 검색에 성공했습니다(`PASS_SCAN`, BSS 11개, `GET_VERSION`·`clmload`·`txcapload`·`calload`·`UP` 모두 0). 다만 끝내기에서 DART 되돌리기가 실패해 종료 코드 4가 났습니다. 원인은 되돌리기를 포트 전원 끄기 **뒤에** 했기 때문입니다. DART(`0x681008000`)는 포트 0 레지스터 블록(`0x681000000`) 안에 있어, 포트 리셋(`PORT_PERST_OFF` 지움) 뒤에는 읽기가 데이터 중단(`FAR 0x681008040/54/50`), 쓰기가 SError(73회)가 됐습니다. 지금은 되돌리기를 전원 끄기 **앞으로** 옮겼습니다. 고친 판은 실기 전이며 가짜 하드웨어 시험 33개만 통과했습니다. 기록: `runs/console-wifi257scan-20261118-121319.txt`, `runs/run-20261118-121320-aznboqcr/`.
- `scan` 단계(`wifi_scan.py`)가 하는 일: 펌웨어가 뜬 뒤, 맥미니 m1n1 프록시 메모리에 메시지 링을 만들고 포트 0의 DART(장치용 주소 변환기)로 **그 버퍼들만** 칩에 보이게 한 다음에야 DMA(버스 마스터)를 켜고, CLM·TxCap·보정 데이터를 보내고, 무선을 켜서 **주변 AP를 한 번 검색해 SSID 표를 찍습니다.** 접속·인증은 하지 않고, 기본값(수동 검색)에서는 전파를 한 번도 내보내지 않습니다(듣기만 함). 시험 통과는 동작한다는 뜻이 아닙니다.
- 2026-11-18 고친 점: 실기 ADT의 GPIO phandle 번호가 13.5 스텁 iBoot에서는 102, 기록값은 106이라 첫 실행이 거부됐습니다. 이 번호는 iBoot가 ADT를 만들 때 붙이는 번호라 하드웨어 사실이 아니므로 비교에서 뺐고, "PERST#가 `/arm-io/gpio`를 가리킨다"는 구조 확인은 그대로 둡니다(`wifi_up.py` `validate_facts`).
- 1단계(전원·연결·장치 번호)는 이미 2026-11-15에 실기에서 통과했습니다(`wifi-s240/hw/README.md`, 판정 `PASS_IDS`). 이 스크립트는 그 코드(`wifi-s240/wifi_probe.py`)를 **고치지 않고 그대로 불러 씁니다.** 파일 지문이 실기 통과 때와 다르면 실행을 거부합니다.
- 펌웨어 파일은 이 맥북에 이미 있습니다(`/usr/share/firmware/wifi/C-4378__s-B1/`, macOS는 모든 Apple 실리콘 기종의 펌웨어를 함께 싣습니다). 내려받을 것은 없습니다.

## 1. 실행 방법

```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS
PY=m1n1_windows/.venv-hv/bin/python3

# (1) 맥북만으로 시험 (하드웨어 접근 없음)
$PY -I -B nwoas_scripts/wifi-s257/test_wifi_up.py

# (2) 계획만 출력 (직렬 포트를 열지 않음)
bash nwoas_scripts/wifi-s257/run.sh --stage fw

# (3) 펌웨어 파일 오프라인 점검 (ADT 덤프 + 맥북의 펌웨어 폴더)
bash nwoas_scripts/wifi-s257/run.sh --check-files

# (4) 실기: Windows 재시작 → D4 프록시에서 Wi-Fi 2단계 → m1n1 재부팅 → agxdcp-s256으로 다시 켜기
cd nwoas_scripts
STAGE=fw bash agx-s248/run-reboot-arm-wifi257.sh <지금 실행기 .out> <지금 .link> wifi257a

# (5) 실기: 주변 Wi-Fi 검색(3단계). (4)와 같은 흐름에서 단계만 scan
STAGE=scan bash agx-s248/run-reboot-arm-wifi257.sh <지금 실행기 .out> <지금 .link> wifi257scan
```

(4)는 `run-reboot-arm-beep255.sh`(D4 프록시 단계에서 스피커 시험)와 `wifi-s240/hw/run-hw.sh`(Wi-Fi 1단계 실기 실행) 흐름을 합친 것입니다. Wi-Fi 단계 뒤에는 1단계 때처럼 `tools/reboot.py`로 m1n1을 다시 시작한 다음 실행기를 띄웁니다(포트 0과 SMC 연결을 건드렸으므로 깨끗한 상태에서 UEFI가 돌게 함). 전원 끄기가 확인되지 않으면(종료 코드 4) 다시 켜지 않고 멈춥니다.

이미 맥미니가 대기 상태라면 `bash nwoas_scripts/wifi-s257/run.sh --run --stage fw`(또는 `--stage scan`)로 바로 실행하고, 끝나면 `tools/reboot.py`로 재부팅합니다.

`--stage`로 어디까지 갈지 정합니다. `--run`만 주면 기본값은 `otp`(칩 CPU를 시작하지 않음)입니다.

| 단계 | 하는 일 | 칩에 쓰는 것 |
|---|---|---|
| `ids` | 1단계와 같음: 전원, 연결, 장치 번호, BAR 크기 | 없음 (설정 공간 BAR 크기 재기만) |
| `chip` | + 장치 주소(BAR) 배정, 칩 번호·내부 코어 목록 읽기, 설정 공간 거울 확인 | BAR0 창 선택 레지스터, PCIe2 코어 CONFIGADDR |
| `otp` | + 칩 CPU 정지, 칩 감시 타이머 리셋, 칩 메모리 크기, OTP 읽기, NVRAM 파일 고르기 | 코어 리셋 레지스터, 감시 타이머 |
| `fw` | + 펌웨어·NVRAM·무작위 시드를 칩 메모리에 쓰기, 되읽어 비교, 칩 CPU 시작, 공유 영역 주소 기다리기, 펌웨어 콘솔 읽기 | 칩 메모리 |
| `scan` | + 메시지 링 준비(아래 2.1), DART 설정, 그 뒤에만 버스 마스터 켬, CLM·TxCap·보정 데이터, `mpc 0`, `UP`, 검색 1회, SSID 표, `DOWN` | 칩 메모리(링 위치), PCIe2 코어 메일박스, 장치 제어(DevCtl), 명령 레지스터의 버스 마스터 비트, 포트 0 DART 레지스터, 맥미니 RAM(우리가 잡은 버퍼만) |

조절할 수 있는 값:

| 옵션 | 기본값 | 허용 범위 | 뜻 |
|---|---|---|---|
| `--fw-dir` | `/usr/share/firmware/wifi/C-4378__s-B1` | — | macOS 형식 펌웨어 폴더 (아래 2장) |
| `--nvram` | 없음 | — | NVRAM 파일을 직접 지정 (OTP로 고르지 않음) |
| `--bar-base` | `0xE0000000` | 8 MiB 단위, `0xC0000000`~`0xFFF00000` 미만 | 칩 BAR를 둘 PCI 버스 주소 (CPU 주소는 +`0x600000000`) |
| `--fw-timeout-ms` | 5000 | 500~10000 | 칩 CPU 시작 뒤 공유 영역 주소를 기다리는 시간 (5000은 brcmfmac `BRCMF_PCIE_FW_UP_TIMEOUT`과 같음) |
| `--hold-seconds` | 3 | 0~60 | 펌웨어가 뜬 뒤 콘솔을 더 지켜보는 시간 |
| `--no-verify` | — | — | 펌웨어를 쓴 뒤 되읽어 비교하는 것을 생략 |
| `--no-macaddr` | — | — | ADT의 MAC 주소를 NVRAM에 붙이지 않음 |
| `--scan-seconds` | 15 | 5~30 | `scan`: 검색 결과를 기다리는 시간(초) |
| `--scan-type` | `passive` | `passive`, `active` | `scan`: `passive`는 듣기만 함(검색 요청 프레임을 보내지 않음). `active`는 채널마다 검색 요청(probe request)을 보냄 |
| `--country` | 없음(펌웨어 기본값) | 대문자 두 글자(예: `KR`) | `scan`: 펌웨어 국가 코드 설정 |
| `--no-cal` | — | — | `scan`: ADT `wifi-calibration-msf`(보정 데이터)를 보내지 않음 |
| `--rxbufs` | 32 | 0~256 | `scan`: 미리 넣어 두는 데이터 수신 버퍼 수(검색만 하므로 데이터는 오지 않음 [가설]) |
| `--rid2sid` | `leave` | `leave`, `map` | `scan`: `leave`는 포트 0의 요청자→스트림 표를 읽어 기록만, `map`은 0번 칸에 `0x100`(Wi-Fi)→스트림 1(Linux DT와 같음)을 쓰고 끝에 되돌림 |
| `--doorbell` | `auto` | `auto`, `bwfm` | `scan`: 칩을 깨우는 레지스터 위치. `auto`는 Linux brcmfmac 방식(PCIe2 코어 리비전 64 이상이면 `0xA20`/`0xA24`), `bwfm`은 OpenBSD 방식(BAR0+`0x2000`의 `0x140`/`0x144`) |
| `--power-settle-ms` 외 1단계 옵션 | 1단계와 같음 | 1단계와 같음 | `wifi-s240/README.md` 3장 |

출력: 전체 기록은 `nwoas_scripts/logs/wifi-s257-<시각>.XXXXXX`, 결과 JSON은 `nwoas_scripts/wifi-s257/runs/run-<시각>-<임의 문자>/result.json`, (4)의 화면 출력은 `wifi-s257/runs/console-<태그>-<시각>.txt`, 요약은 `agx-s248/runs/reboot-<태그>.txt`.

## 2. 펌웨어 파일

| 종류 | 파일 (`C-4378__s-B1/` 안) | 크기 (바이트) | 칩에 올리는 때 |
|---|---|---|---|
| 칩 펌웨어 | `atlantisb-X3.trx` → `honshu.trx` (바로가기) | 1,497,470 | 이 스크립트 (`fw` 단계) |
| NVRAM | `P-atlantisb-X3_M-RASP_V-<u 또는 m>__m-<버전>.txt` | 7,723 (`u`) / 7,784 (`m`) | 이 스크립트 (변환 후 칩 메모리 맨 위) |
| CLM(무선 규제표) | `atlantisb-X3.clmb` | 34,061 | 이 스크립트는 **확인만**. 펌웨어 명령 통로(메시지 링, DMA 필요)가 생긴 뒤에 올림 |
| TxCap(송신 출력 한계) | `atlantisb-X3.txcb` | 304 | 위와 같음 |
| 보정 데이터 | ADT `wifi-calibration-msf` (2,096) | — | 위와 같음 |

- [사실] 맥북(macOS 27.2.1)의 펌웨어 버전: `4378b1-roml/config_pcie_perf_udm Version=18.20.511.0.7.8.207 Date=2026-08-11 FWID=01-2606388a` (`.trx` 끝의 표시).
- [사실] 칩 리비전: 1단계 실기에서 설정 공간 리비전 `0x3`. brcmfmac 표에서 4378 리비전 3 = B1, 5 = B3. 그래서 폴더는 `C-4378__s-B1`입니다. 스크립트는 칩 번호 레지스터에서 리비전을 다시 읽고, 폴더 이름의 리비전과 다르면 거부합니다.
- [사실] NVRAM 내용은 제조사(`V-u`/`V-m`) 두 가지뿐이고, 같은 제조사 안의 버전별 파일은 내용이 같습니다(`cmp`로 확인). 칩 OTP의 `M`·`V`·`m` 값으로 정확히 맞는 파일을 고르고, 없으면 멈춥니다(`NVRAM_NOT_FOUND`). 짐작으로 고르지 않습니다.
- [사실] `.trx`는 변환 없이 그대로 올립니다. Asahi 설치기(`asahi_firmware/wifi.py`, MIT)도 `.trx`를 내용 그대로 `.bin`으로 이름만 바꿉니다. 파일 맨 앞 4바이트가 칩 CPU 시작 명령(`0xbb04f373`)입니다.
- 맥미니 자체 macOS의 파일을 쓰고 싶으면: 맥미니에서 macOS로 부팅한 뒤 `/usr/share/firmware/wifi/C-4378__s-B1/`의 `atlantisb*`, `honshu*`, `P-atlantisb*` 파일을 맥북의 다른 폴더로 복사하고 `--fw-dir <그 폴더>`를 줍니다(폴더 이름은 `C-4378__s-B1`로 끝나게). [가설] 버전이 달라도 같은 칩·보드용이라 둘 다 동작할 것으로 봅니다.
- 펌웨어는 Apple 파일이므로 저장소나 공개 장소에 올리지 않습니다.

## 2.1 `scan` 단계가 하는 일 (순서대로)

1. **공유 영역 읽기** — 펌웨어가 칩 메모리에 만든 구조에서 링 개수·링 위치 표·메일박스 주소를 읽습니다. [사실] 실기 플래그 `0x78810007` = 판 7, "링 위치를 호스트 메모리에 두라"(`0x10000`), 4바이트 위치값(`0x100000` 없음), 호스트 준비 신호 사용(`0x10000000`), 메일박스 대신 제어 링으로 메일박스 데이터(`0x2000000` 없음). PCIe2 코어 리비전 68 → 64비트 판 메일박스 레지스터(`0xC30`/`0xA20`)를 씁니다.
2. **DMA 메모리 잡기** — m1n1 프록시 힙(맥북 Python이 관리하는 맥미니 RAM)에서 버퍼 한 덩어리를 잡고, 버퍼마다 16 KiB 쪽 경계에서 시작하고 **바로 뒤에 비워 둔 쪽 한 개**를 둡니다. 칩이 버퍼 끝을 넘어 쓰면 DART에서 막힙니다.
3. **DART 설정** — ADT `/arm-io/dart-apcie0`(`0x681008000`, `dart,t8020`)의 레지스터 전체를 먼저 저장합니다. 잠금 비트가 켜져 있으면 멈춥니다(`DART_LOCKED`). 스트림 16개를 모두 우리 표 하나(L1·L2 각 16 KiB)로 돌리고, 그 표에는 2번의 버퍼 쪽만 들어 있습니다. 칩이 보는 주소(IOVA)는 `0x10000000`부터입니다.
4. **칩 메모리에 링 위치 쓰기** — 링 5개의 주소·크기, 위치값 버퍼 4개의 주소, 임시 버퍼 2개, 호스트 능력값(`host_cap`)을 칩 메모리에 쓰고 되읽어 비교합니다.
5. **DevCtl 정리 후 버스 마스터 켜기** — 장치 제어 레지스터의 No Snoop·Relaxed Ordering을 끄고(USB-A 손상 교훈, `plan-20261115/wifi.md` 4.6), **DART가 번역 중인 것을 확인한 뒤에만** 장치와 루트 포트의 버스 마스터 비트를 켭니다. 스크립트 안에 "DART 준비 전에는 버스 마스터 비트를 쓰지 못하게 막는" 검사가 있습니다.
6. **호스트 준비 신호, 버퍼 넣기** — 명령 응답 버퍼 8개, 이벤트 버퍼 8개, 수신 버퍼 32개.
7. **펌웨어 설정** — `GET_VERSION` → `cur_etheraddr`(ADT 값과 같은지만 기록, 값은 남기지 않음) → `ver` → `clmload` → `clmload_status` → `txcapload` → `calload`(ADT 보정 데이터 2,096바이트) → `mpc 0` → (선택) `country` → `event_msgs`(검색 결과 이벤트만) → `UP` → `scan_ver`.
8. **검색 1회** — `escan`(검색 판 2, SSID 지정 없음, 채널 지정 없음 = CLM이 허락하는 모든 채널). 검색 끝 이벤트가 오거나 15초가 지날 때까지 결과를 모읍니다. 끝나면 `DOWN`(무선 끔).
9. **표 출력** — BSSID 기준으로 중복을 빼고(가장 센 신호만 남김) 신호 세기 순으로 SSID·BSSID·채널·대역·RSSI를 찍습니다(최대 100줄, 나머지는 `result.json`).

끝날 때(오류가 나도): ① 장치·루트 포트 버스 마스터 끔(고정값) 후 되읽어 꺼졌는지 확인 → ② DevCtl·RID2SID 되돌림 → ③ **포트 전원을 끄기 전에** DART 정리: 버스 마스터가 꺼진 것이 되읽기로 확인되고 저장값에 "통과(bypass)" 스트림이 없을 때만 저장값으로 되돌리고, 아니면 모든 스트림을 막힘(번역 끔)으로 둡니다. DART 읽기에서 예외가 한 번이라도 나면 쓰기는 하지 않습니다 → ④ 표의 항목을 지우고 메모리 반납(DART가 우리 표를 더는 가리키지 않을 때만) → ⑤ 기존 2단계 되돌리기 → ⑥ 1단계 전원 끄기. DART는 포트 0 블록 안에 있어 ⑥ 뒤에는 만질 수 없습니다(2026-11-18 실기).

보정 데이터를 어떻게 보내는지 [사실]: OpenBSD `bwfm.c` `bwfm_preinit`이 `clmload`, `txcapload`, `calload` 순서로 같은 묶음 형식(종류 2 = CLM, 1,400바이트씩, 처음·끝 표시)으로 보내고, 보정 데이터 원본은 m1n1이 ADT `wifi-calibration-msf`를 그대로 `brcm,cal-blob`으로 넘긴 것입니다(`m1n1-layout-s231/src/kboot.c` 1004~1012행). 별도의 `cal_load` 명령은 쓰지 않습니다.

## 3. 기록 줄의 뜻

`[s240]` 줄은 1단계 코드, `[s257]` 줄은 이 스크립트가 찍습니다.

| 줄 | 뜻 | 정상 |
|---|---|---|
| `[s257] target board=atlantisb antenna=X3 cal_len=2096` | ADT에서 읽은 보드·안테나·보정 데이터 크기 | 이 값 |
| `[s240] pcie_init rc=0x0` | m1n1 PCIe 초기화 | `0x0` |
| `[s240] smc write key=gP0d value=0x800001` | 칩 전원 켬 | — |
| `[s240] port 0 link up=True` | PCIe 연결 됨 | `True` |
| `[s240] bus1 fn0 id=0x442514e4` | Wi-Fi 기능 장치 번호 | `0x442514e4` |
| `[s257] BARs assigned ... ok=True` | 칩 레지스터(BAR0 64 KiB)·칩 메모리(BAR2 8 MiB)에 주소 배정, 루트 포트 창, 장치 명령 `0x402`(메모리만, DMA 금지, 인터럽트 끔) | `ok=True`, `ep_cmd=0x402` |
| `[s257] chip id reg=0x10034378 chip=0x4378 rev=0x3 socitype=0x1` | 칩 내부 번호 레지스터 | chip `0x4378`, rev 3, type 1 |
| `[s257] core id=... name=...` | 칩 내부 코어 목록 | `chipcommon`, `pcie2`, `arm-cr4`, `gci`, `80211`이 있음 |
| `[s257] config mirror via PCIe2 core ... match=` | 칩 레지스터 창만으로 설정 공간을 읽을 수 있는지(Windows 설계용 관찰) | 어느 쪽이든 기록 |
| `[s257] CR4 halted (passive)` | 칩 CPU 멈춤 | — |
| `[s257] config after watchdog lost=` | 칩 리셋 뒤 BAR 설정이 남았는지. 지워졌으면 다시 씀 | `False` 기대 [가설] |
| `[s257] chip watchdog reset done ... chipid=` | 칩 리셋 뒤에도 칩 번호가 읽힘 | 칩 번호 `0x4378` 포함 |
| `[s257] TCM rambase=0x352000 ramsize=...` | 칩 메모리 시작·크기 | 크기는 실측값이 기록 |
| `[s257] OTP module=RASP vendor=u modrev=6.5` | 칩 OTP에서 읽은 모듈·제조사·버전 | 값이 셋 다 있음 |
| `[s257] file trx/nvram/clmb/txcb ...` | 쓸 파일 이름·크기·지문 앞 16자 | — |
| `[s257] firmware written` / `firmware read-back match=True` | 칩 메모리에 쓰고 되읽어 같음 | `True` |
| `[s257] NVRAM + seed written ... nvram_match=True` | NVRAM·시드 표시·256바이트 무작위 값 씀 | `True` |
| `[s257] CR4 released rstvec=0xbb04f373` | 칩 CPU 시작 | — |
| `[s257] shared RAM pointer value=... changed=True` | 펌웨어가 칩 메모리 마지막 칸에 공유 영역 주소를 씀 = **펌웨어가 돌고 있음** | `changed=True`, 칩 메모리 범위 안 |
| `[s257] shared info flags=... version=...` | 공유 영역 판 번호 | 5~7 |
| `[s257] fw-console \| ...` | 펌웨어 콘솔 글 (최대 150줄, 줄당 200자) | 펌웨어 시작 문구 |
| `[s257] firmware version (console) text=...` | 콘솔에서 찾은 버전 줄 | 버전 문자열 |
| `[s257] hold ... shared_ptr_same=True` | 지켜보는 동안 펌웨어가 멈추지 않았는지 | `True` |
| `[s257] stage-2 undo errors=none` | 장치 명령 0, BAR 0, 창 되돌림 | `none` |
| `[s240] power down confirmed=True ... port1_unchanged=True` | 전원 끔 확인, USB-A 포트 그대로 | 둘 다 `True` |
| `S257 verdict PASS_FW_UP` | 최종 판정 | 아래 표 |

`scan` 단계에서 더 나오는 줄:

| 줄 | 뜻 | 정상 |
|---|---|---|
| `[s257] msgbuf shared flags=0x78810007 version=0x7 idx_sz=0x4 max_sub=... max_compl=...` | 공유 영역에서 읽은 링 개수 | 판 7, 위치값 4바이트 |
| `[s257] DMA arena pa=... iova=0x10000000 pages=... walk_ok=True` | 잡은 메모리, 칩이 볼 주소, 표를 소프트웨어로 따라가 본 결과 | `walk_ok=True` |
| `[s257] TCM ring descriptors written ... readback_ok=True` | 링 위치를 칩 메모리에 쓰고 되읽음 | `True` |
| `[s257] DART before config=... tcr=...` | 건드리기 전 DART 상태(기록) | 잠금 비트 `0x8000` 없음 |
| `[s257] DART programmed (16 streams -> one table) ... ok=True` | DART 설정 되읽기 | `ok=True` |
| `[s257] DevCtl before=0x2c10 after=0x2400` | No Snoop·Relaxed Ordering 끔 | 뒤 값에 `0x810` 없음 [가설: 값] |
| `[s257] bus master ON ... rp_cmd=0x6 ep_cmd=0x406` | 이때 처음 버스 마스터를 켬 | 두 값 모두 `0x6` 포함 |
| `[s257] first ioctl answered (GET_VERSION) status=0x0 io_type=0x2` | **첫 명령 왕복 성공 = 링·DMA·DART 길이 열림** | `status=0x0` |
| `[s257] cur_etheraddr ... matches_adt=True` | 펌웨어 MAC이 ADT 값과 같음(값은 안 남김) | `True` |
| `[s257] clmload ok bytes=0x850d chunks=0x19` 등 | CLM·TxCap·보정 데이터 보냄 | `ok` |
| `[s257] interface UP status=0x0` | 무선 켬 | `0x0` |
| `[s257] escan requested tried=[...]` | 검색 요청 | `status` 0 |
| `[s257] fw-console \| ...` | 검색 중 펌웨어 콘솔(최대 200줄) | `TRAP`/`ASSERT`가 없음 |
| `[s257] scan result: N BSS (status SUCCESS, ...)` 뒤 표 | 검색 결과 표 | 1개 이상 |
| `[s240] power down confirmed=True ...`, `[s257] stage-2 undo errors=none` | 끄기 확인. `result.json`의 `power_down.dart`가 `restored`(저장값에 통과 스트림이 있었으면 `blocked`도 정상) | 둘 다 `True`, m1n1 콘솔에 `SError` 없음 |

MAC 주소는 기록에 남기지 않습니다(NVRAM에 붙였는지만 `macaddr_appended`로 남김).

## 4. 성공했을 때 모습 [가설: 값은 실측 전]

```
[s240] bus1 fn0 id=0x442514e4 class_rev=0x2800003
[s257] BARs assigned bar0_cpu=0x6e0800000 bar2_cpu=0x6e0000000 window=0xe0000000-0xe08fffff ep_cmd=0x402 ok=True
[s257] chip id reg=0x10034378 chip=0x4378 rev=0x3 socitype=0x1
[s257] OTP module=RASP vendor=u modrev=6.5 ...
[s257] firmware read-back match=True
[s257] CR4 released rstvec=0xbb04f373
[s257] shared RAM pointer value=0x...... ms=... changed=True
[s257] shared info flags=0x...... version=0x7 console=0x......
[s257] fw-console | ... Firmware: BCM4378/3 wl0: ... version 18.20.511.0.7.8.207 ...
[s240] power down confirmed=True errors=none port1_unchanged=True
S257 verdict PASS_FW_UP (requested stage fw). Proxy-side hardware observation only; ...
```

`PASS_FW_UP`도 "맥북에서 칩 펌웨어가 떠서 공유 영역을 만들었다"는 관찰일 뿐입니다. Windows에서 Wi-Fi가 된다는 뜻이 아닙니다.

`scan` 단계 성공 모습 [가설: 값은 실측 전, SSID는 예시]:

```
[s257] bus master ON (DART translating, only our pages mapped) rp_cmd=0x6 ep_cmd=0x406
[s257] first ioctl answered (GET_VERSION) status=0x0 io_type=0x2
[s257] clmload ok bytes=0x850d chunks=0x19
[s257] interface UP status=0x0
[s257] scan result: 14 BSS (status SUCCESS, 22 events, 4.8 s)
[s257] SSID                              BSSID              CH  BAND   RSSI
[s257] (예시) home-5G                    xx:xx:xx:xx:xx:xx   36  5G      -41
[s257] (예시) home                       xx:xx:xx:xx:xx:xx    6  2G      -45
...
[s240] power down confirmed=True errors=none port1_unchanged=True
S257 verdict PASS_SCAN (requested stage scan). Proxy-side hardware observation only; ...
```

`PASS_SCAN`은 "프록시 단계에서 칩이 주변 AP 목록을 돌려줬다"는 관찰입니다. 접속·데이터 송수신·Windows 드라이버와는 무관합니다.

## 5. 판정과 종료 코드

| 종료 코드 | 판정 | 뜻 | 다음 할 일 |
|---|---|---|---|
| 0 | `PASS_<요청 단계>` | 요청한 단계까지 통과, 전원 끔 확인, USB-A 포트 그대로 | `result.json`을 계획 문서에 기록 |
| 1 | `LINK_DOWN`, `ID_MISMATCH`, `NO_CONFIG_RESPONSE` | 1단계에서 막힘 | `wifi-s240/README.md` 5장 |
| 1 | `BAR_SETUP_FAILED`, `CHIP_MISMATCH`, `TCM_LAYOUT_BAD` | 주소 배정·칩 번호·메모리 배치 문제 | `result.json`의 `bar_layout`·`chip` |
| 1 | `OTP_INCOMPLETE`, `NVRAM_NOT_FOUND` | OTP로 NVRAM을 못 고름 | `result.json`의 `otp`, 필요하면 `--nvram` |
| 1 | `FW_READBACK_MISMATCH`, `NVRAM_READBACK_MISMATCH` | 칩 메모리에 쓴 것이 다르게 읽힘 | 쓰기 경로 문제. 칩 CPU는 시작하지 않음 |
| 1 | `FW_NOT_UP`, `FW_BAD_SHARED` | 펌웨어가 공유 영역을 안 만들었거나 이상한 값 | 펌웨어 판·NVRAM·보정 데이터 의심 (6장) |
| 0 | `PASS_SCAN` | 검색 결과 1개 이상, 끄기 확인, DART 되돌림 | 표를 계획 문서에 기록 |
| 1 | `SCAN_NO_RESULTS` | 검색은 끝났거나 시간이 다 됐는데 결과 0개 | `result.json`의 `scan.escan`, `scan.blobs`(CLM 실패?), `--scan-type active`·`--country KR` |
| 1 | `SCAN_BAD_SHARED`, `SCAN_TCM_READBACK_MISMATCH`, `DART_TABLE_BAD` | 공유 영역 값이 범위 밖, 또는 쓴 값이 다르게 읽힘 | 버스 마스터는 켜지 않은 상태에서 멈춤 |
| 1 | `DART_LOCKED`, `DART_PROGRAM_FAILED` | DART가 잠겨 있거나 설정이 안 먹음 | 버스 마스터는 켜지 않은 상태에서 멈춤. `scan.dart_before` |
| 1 | `IOCTL_NO_REPLY` | 첫 명령(`GET_VERSION`)에 답이 없음 = 링/메일박스/DMA 길 중 하나가 막힘 | 아래 6장 가설 순서대로 (`--rid2sid map`, `--doorbell bwfm`), `scan.dart_fault`, `scan.port0_rid2sid_0_15` |
| 1 | `IOCTL_TIMEOUT`, `UP_FAILED`, `ESCAN_REFUSED` | 중간 명령에서 막힘 | `scan.counts`, `scan.blobs` |
| 1 | `FW_TRAP` | 펌웨어 콘솔에 `TRAP`/`ASSERT`, 또는 메일박스에 펌웨어 정지 | `scan.console_trap_lines` |
| 1 | 요청보다 앞 단계의 `PASS_*` | 앞 단계까지만 감 | — |
| 2 | `ERROR: ...` | 도중 오류(시간 초과·m1n1 예외 포함). 전원 끄기는 확인됨 | 오류 문구 |
| 3 | `REFUSED` / `STOP` | 사전 확인에서 멈춤(직렬 포트, 지문, ADT, 펌웨어 폴더 등) | 문구대로 맞춤 |
| 4 | 끄기 미확인 | 끄기 단계 하나라도 실패, 또는 끈 뒤에도 연결이 살아 있음 | **맥미니 전원을 직접 껐다 켬** (1단계와 같음) |
| 5 | USB-A 포트 변화 | USB-A 포트 레지스터가 처음과 다름 | m1n1 재부팅 후 실행기 |
| 6 | `CONNECT_FAILED` | 맥미니 m1n1에 못 붙음. 아무것도 켜지 않음 | 직렬 연결 확인 |

## 6. 안전장치와 남은 위험

| 항목 | 내용 |
|---|---|
| DMA 금지 (`ids`~`fw`) | 장치와 루트 포트 모두 버스 마스터(DMA) 비트를 끈 채로 진행합니다. 칩은 맥 RAM에 쓸 수 없습니다. 펌웨어가 DMA를 시도하면 칩 안에서 실패할 뿐입니다 [가설: 맥 쪽 오류로 번지지 않음] |
| DMA 허용 범위 (`scan`) | 버스 마스터는 DART가 우리 표로 번역 중인 것을 되읽어 확인한 뒤에만 켭니다(검사 코드가 그 전의 버스 마스터 쓰기를 막음). 표에는 우리 버퍼 쪽만 있고 버퍼 사이마다 비운 쪽이 있어, 칩이 그 밖으로 읽거나 쓰면 DART가 막고 오류 레지스터에 남깁니다(`scan.dart_fault`로 기록). 스트림 16개를 모두 같은 표로 돌리므로 요청자→스트림 대응을 몰라도 어느 스트림으로 오든 같은 범위로 갇힙니다 |
| DART 되돌림 (`scan`) | 포트 전원 끄기 **전에**, 버스 마스터를 끄고 되읽어 확인한 뒤 합니다. 확인이 안 됐거나 저장값에 통과 스트림이 있으면 모든 스트림을 막힘으로 둡니다. 되돌림이 실패하면 끄기 미확인(종료 코드 4) |
| 캐시 (`scan`) | 버퍼에 쓴 뒤 캐시를 메모리로 밀어내고(`dc cvac`), 읽기 전에 비웁니다(`dc civac`). 64바이트 줄 단위로 맞춥니다(m1n1 범위 함수가 시작 주소부터 64바이트씩 세므로). PCIe가 캐시 일관이어도 해가 없습니다 |
| 인터럽트 | 장치 명령 레지스터의 인터럽트 끔 비트(`0x400`)를 켭니다. m1n1 대기 상태는 인터럽트를 받지 않습니다 |
| 쓰기 범위 | 1단계의 정확한 주소 목록 + 루트 포트 0 창 레지스터 5개 + Wi-Fi 기능 0의 설정 레지스터 3개(`0x04`, `0x80`, `0xBC`) + 배정한 BAR 두 범위만. 그 밖은 막힘. USB-A(포트 1)에는 쓰지 않음. `scan`은 여기에 포트 0 DART 레지스터(TCR 16개, TTBR 64개, 무효화·선택·오류·스트림 켜기 4개), DevCtl 1개(`0xB4`, 실기 능력 위치 `0xAC`+8), `--rid2sid map`일 때만 포트 0 `0x828` 1개를 더함. RAM 쓰기는 우리가 잡은 버퍼 안으로만(범위 검사) |
| 끄기 순서 | 직렬 통신 다시 맞추기 → 장치 명령 0 → BAR 0 → BAR0 창 되돌림 → 루트 포트 창·명령 되돌림 → (1단계) 버스 번호 되돌림 → PERST# 낮춤 → 리셋 해제 비트 지움 → `gP0d` ← `0x800000` → SMC 정리 → 연결 내려감 확인 → USB-A 포트 비교. 각 단계 따로, 단계마다 시간 제한 |
| 시간 제한 | 단계별 알람(칩 60초, 리셋·OTP 90초, 펌웨어 쓰기 240초, 시작 30초, 콘솔 20초+지켜보는 시간; `scan`: DMA 준비 60초, 펌웨어 설정 150초, 검색 `--scan-seconds`+30초, `DOWN` 15초, 명령 하나 3초). 바깥 제한 1,200초는 SIGINT로 보내서 끄기 단계가 반드시 돎 |
| 기록 크기 (`scan`) | 검색 중 콘솔 200줄, 펌웨어 메시지 기록 40줄, 표 100줄, 저장 BSS 256개 |
| 칩 감시 타이머 리셋 | Linux brcmfmac·OpenBSD bwfm이 늘 하는 동작이지만 이 맥미니에서는 처음. 리셋 뒤 BAR 설정이 지워졌으면 다시 쓰고 기록 [가설: PCIe2 코어 리비전 14 이상은 설정이 남음] |
| 펌웨어가 무선을 켜는가 | `fw` 단계: CLM을 올리지 않았고 호스트 명령도 없으므로 펌웨어는 무선을 켜지 않은 상태로 기다립니다 [가설, brcmfmac·bwfm 동작 순서 기준]. `scan` 단계: `UP`부터 `DOWN`까지만 무선이 켜지고, 기본 수동 검색은 수신만 합니다. 접속(`join`·`SET_SSID`)·암호 설정 명령은 코드에 없습니다 |
| `scan`이 막힐 만한 곳 [가설] | ① 포트 0이 요청자→스트림 표에 없는 요청을 버림 → `IOCTL_NO_REPLY`. 시험: `--rid2sid map`. ② 칩 깨우기 레지스터 위치가 다름 → 같은 증상. 시험: `--doorbell bwfm`. ③ D4 프록시 단계의 DART 전원·클록 상태를 모름 → DART 레지스터 접근에서 m1n1 예외(`ERROR`). ④ CLM이 거부되면 채널이 없어 결과 0개 → `scan.blobs`. ⑤ 펌웨어가 깊은 잠에 들어가면(`DS_ENTER_REQ`) 확인 응답을 제어 링으로 보내지만, 이 경로는 실기에서 본 적 없음 |
| **USB-A 주소 밀림** | 이 스크립트는 끝에 전원을 끄고 m1n1을 재부팅하므로 다음 UEFI 부팅은 지금과 같습니다. 주소가 밀릴 위험은 **Wi-Fi를 켠 채 UEFI를 띄우는 다음 단계**의 문제입니다. 1단계 BAR 크기로 계산한 예측: Wi-Fi 창 `0xC0000000~0xC0CFFFFF`, USB-A 창이 `0xC0D00000`으로 밀림 [가설, `wifi-s240/hw/README.md`]. 대책은 `plan-20261115/wifi.md` 8장 |
| BAR 주소 `0xE0000000` | 이 스크립트 실행 중에만 쓰고 끝나면 지웁니다. m1n1 대기 상태라 이 주소를 쓰는 다른 장치가 없습니다(UEFI가 아직 돌지 않음) |

## 7. 파일

| 파일 | 내용 |
|---|---|
| `wifi_up.py` | 스크립트. 기본은 계획 출력, `--check-files`는 오프라인 점검(`scan`의 DMA 배치·CLM 묶음 수도 출력), `--run`일 때만 실행 |
| `wifi_scan.py` | `scan` 단계: DART 표·레지스터, DMA 메모리, 메시지 링, 명령 왕복, CLM/TxCap/보정 데이터, 검색, 결과 표 |
| `test_wifi_up.py` | 가짜 프록시·가짜 BCM4378(코어 목록, CPU, 칩 메모리, OTP)·가짜 DART·가짜 메시지 링 펌웨어로 33개 시험(정상 검색, 포트 리셋 뒤 DART 접근 0회, 저장값 통과 스트림이면 막힘, 버스 마스터가 안 꺼지면 막힘, DART 접근 불가면 버스 마스터 전에 멈춤, DART 밖 DMA 차단, 펌웨어 무응답, 펌웨어 트랩, DART 잠김, ADT DART 불일치 거부, 능동 검색·국가 코드·RID2SID 옵션, 배치·표·묶음·이벤트 단위 시험, `main` 종료 코드) |
| `run.sh` | 환경 변수·직렬 포트 확인·기록 저장 래퍼 |
| `../agx-s248/run-reboot-arm-wifi257.sh` | Windows 재시작 → D4 프록시 Wi-Fi 2단계 → m1n1 재부팅 → `agxdcp-s256` 다시 켜기 → D28 |

라이선스: `wifi_up.py`·`wifi_scan.py`의 칩 쪽 순서는 OpenBSD `bwfm`(ISC)을 Python으로 다시 쓴 것이고 Linux `brcmfmac`(ISC)과 대조했습니다. 파일 머리에 두 저작권 표시를 남겼습니다. DART 레지스터 위치와 표 형식은 m1n1 proxyclient `hw/dart8020.py`(MIT)의 값을 씁니다. GPL 코드(Linux `pcie-apple.c`·`apple-dart.c`, UEFI `AppleSiliconPciPlatformDxe.c`)는 복사하지 않았고(RID2SID 레지스터 위치 `0x828`·비트 값만 사실로 사용), 포트 0 전원·연결 부분은 1단계 스크립트를 그대로 불러 씁니다.
