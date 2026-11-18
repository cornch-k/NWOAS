# S259 내장 Wi-Fi를 Windows에 PCI 장치로 보이기 (전원·연결·BAR·DART·인터럽트)

표기: **[사실]** = 코드 행·실기 기록으로 확인. **[가설]** = 확인 안 함. **빌드와 가짜 하드웨어 시험(18개)·가짜 하이퍼바이저 모의 실행만 했습니다. 맥미니에서는 한 번도 돌리지 않았습니다.** 장치가 보여도 Wi-Fi가 된다는 뜻이 아닙니다. 드라이버 쪽 약속은 `research-20261118/wifi/CONTRACT-WIN.md`.

## 0. 결론

- UEFI는 지금과 똑같이 **전원이 꺼진 포트 0**을 만납니다(실행기 모듈이 UEFI 전에 칩을 확실히 끔). 그래서 UEFI의 PCI 주소 배정은 지금까지의 모든 부팅과 같고, USB-A(FL1100)는 `0xC0000000`에 그대로 남습니다. UEFI PCIe 코드는 한 줄도 바꾸지 않았습니다.
- **UEFI가 끝난 뒤** Windows ACPI가 `WLN0._INI`에서 제어 페이지에 "켜기"를 쓰면, 하이퍼바이저(맥북 Python HOOK)가 그 자리에서 S240·S257의 실기 통과 코드로 전원 → 연결 → BAR(`0xE0000000`대 고정) → DevCtl → DART 확인 → 버스 마스터까지 합니다. 펌웨어는 올리지 않습니다(드라이버 몫).
- 하이퍼바이저 C 코드는 바꾸지 않았습니다(같은 `m1n1-g254L` 이미지, 지문 그대로). 새로 생긴 것은 Python 모듈 1개, SSDT 1개, IORT 노드 1개, RAM 16 MiB 예약 1곳입니다.

### 0.1 첫 실기 (2026-11-18 12:56, `runs/run-20261118-125628-ynmpoboc/`)

- [사실] force-off·HOOK·SMC 켜기·연결(`linksts 0x99000001`)·ID(fn0 `0x442514e4`, fn1 `0x5f6914e4`) 통과, USB-A 그대로(XHC0 OK, USB 9/9).
- [사실] `FAIL BAR_SETUP (config read-back differs)`. 원인: UEFI가 루트 포트 0 명령을 `0x0007`(I/O·MEM·BME)로 남김(D4 프록시에서는 0). S257 `_setup_bars`는 BME만 빼서 `0x0003`을 쓰고 `(cmd & 7) == 2`를 요구 → 레지스터는 모두 맞는데 실패. 나머지 9개 값(끝점 명령·BAR 4칸·창 `0xe080e000`·버스 번호 `0x10100`)은 기대값과 같았음.
- 고침: `setup_bars_post_uefi()` — 같은 쓰기, I/O 비트는 UEFI 값 그대로 두고, 레지스터마다 "쓴 값 = 읽은 값"을 비교. 틀리면 `BAR read-back mismatch detail=<레지스터>: want .. got ..` 줄과 `bar_layout.expected/readback/mismatch`를 남김. 버스 마스터 단계도 I/O 비트를 유지(`0x0007`). 시험에 실기 루트 포트 상태(명령 `0x0007`, 창 `0xfff0`, 선반입 `0x1fff1`/`0xffffffff`)를 넣었고, 예전 검사가 이 상태에서 실기와 같은 문구로 실패함을 재현함. 펌웨어는 바뀌지 않음.

### 0.2 드라이버 BAR0 쓰기 SError (2026-11-18 14시대)

- [사실: 코드] 2단계 표의 1:1 장치 매핑은 메모리 속성을 바꾸지 않아(`hv_vm.c` 155·162행 `PTE_MEMATTR_UNCHANGED`, FWB 꺼짐) Windows의 Device-nGnRnE가 그대로 하드웨어로 갑니다. m1n1 EL2는 같은 범위를 Device-nGnRE로 매핑합니다(`memory.c` 409~415행, ADT `pmap-io-ranges 0x6c0000000 0x40000000 0x80000027`).
- [사실: 코드] BYPASS 추적기 = 안 보이는 소프트웨어 매핑 → 게스트 접근이 C 하이퍼바이저로 넘어가 `hv_pa_read`/`hv_pa_write`(EL2, `GUARD_SKIP`)로 수행되고 Python으로는 가지 않습니다(추적 플래그 없음).
- 고침: `wifi_module.py`가 `0x6e0000000+0x900000`(BAR2 8 MiB + BAR0 64 KiB)에 `wifi-s259-bar-emul` BYPASS 추적기를 겁니다. 시험 `Module.test_bar_range_is_hypervisor_emulated`가 실제 ADT와 m1n1 `DictRangeMap`으로 그 범위의 우선 방식이 BYPASS이고 FL1100 BAR·이웃 범위는 그대로임을 확인합니다. 펌웨어는 바뀌지 않았습니다. [가설] 이것으로 SError가 사라짐 — 실기 확인 전.

## 1. 설계 질문의 답 (코드 근거)

| # | 질문 | 답 | 근거 |
|---|---|---|---|
| 1 | 누가 언제 켜나 / UEFI가 그냥 봐도 되나 | **UEFI 뒤, 하이퍼바이저 HOOK이 켬.** UEFI가 켜진 칩을 보면 안 됨 | [사실] UEFI 포트 설정은 칩이 켜져 있으면 PERST#를 풀고 연결을 올림(`AppleSiliconPciPlatformDxe.c` 300·322행), PciBusDxe는 큰 정렬부터 아래에서 배정(S240 `predict_wifi_window` 근거 행) → Wi-Fi 8 MiB가 `0xC0000000`을 먼저 차지하고 FL1100이 밀림 [가설: 실제 밀림]. FL1100 주소는 DSDT·DART 드라이버·`hv_exc.c` 689행·`hv_vm.c` 2286행·`guest_map.py` 8행에 고정. 연결 실패를 UEFI가 무시하고 계속함(336행 `return` 주석 처리) → 꺼진 칩이면 지금과 같음 |
| 2 | 버스 번호·BAR | 포트 0 → 버스 1, 기능 0만. BAR2 `0xE0000000`(8 MiB), BAR0 `0xE0800000`(64 KiB), 루트 포트 창 `0xE0000000~0xE08FFFFF`. CPU = 버스 + `0x600000000` | [사실] S257 실기 되읽기(`0x10=0xe0800004`, `0x18=0xe0000004`, 루트 `0x20=0xe080e000`). FL1100 창 `0xC0000000~0xC00FFFFF`와 안 겹침. 게스트 RAM `[0xE0000000,0xE1000000)`은 UEFI가 예약(S259 `MemoryInitPeiLib.c`, 빌드 FD에 `S259 reserved Wi-Fi bridge window` 문구 1개). CPU 주소는 `/arm-io` 범위 `0x6c0000000+1 GiB` 안이라 게스트에 그대로 보임(`hv/__init__.py` 1886행) |
| 3 | DMA | **장치 주소 = 게스트 물리 주소.** 포트 0 DART는 UEFI가 이미 USB-A와 같은 표로 묶어 둠: 우회(bypass)가 아니라 번역 + 낮은 4 GiB는 게스트 낮은 창 메모리로, 높은 RAM은 1:1 | [사실] `AppleDartIoMmuDxe.c` 435(DART 3개)·737·740(16 스트림 같은 L1)·1143(낮은 창)·1166(높은 RAM 1:1)행, 실기 기록 `SetupApcieDarts DONE`, `WIDE-DART identity ... ptes=693309`. 하이퍼바이저가 버스 마스터 전에 포트 0 DART = 포트 1 DART인지 비교, 다르면 포트 1 값 복사(`_dart_check`). Windows 쪽은 `NWC0._DMA`(TRA 0, 48 GiB)·`_CCA 0`·IORT 노드 — USB-A와 같은 모양 |
| 4 | 인터럽트 | INTx만. 포트 0 묶음 줄 AIC 695 = vGIC SPI 695, 하이퍼바이저 일반 경로(올리면 막고 넣음, EOI에 엶) | [사실] `hv_exc.c` 2257행, `hv_vgic.c` 426행, UEFI가 포트 0 INTx 마스크 풂(367행, 연결 실패와 무관), 하이퍼바이저가 다시 확인. Linux `pcie-apple.c`: INTx는 수준형, 포트 상태를 따로 안 지움. [가설] USB-A(698)처럼 일반 경로로 안 들어올 수 있음 → 드라이버는 폴링부터 |
| 5 | 펌웨어는 누가 | **Windows 드라이버.** 하이퍼바이저는 전원·연결·BAR·DART 확인·버스 마스터까지만 | 요청대로. 드라이버가 못 구하는 ADT 값(MAC·보정 데이터·안테나·보드)은 제어 페이지로 줌 |
| 6 | 종료·재시작 때 끄기 | `_PS3` → 하이퍼바이저가 버스 마스터 끔 → PERST# → SMC `gP0d` 끔 → 연결 내려감 확인. **다음 실행기는 UEFI 전에 무조건 다시 끔** | [사실] Windows 재시작·종료는 하이퍼바이저가 `reboot()`(`hv_psci.c` 1523~1541행)로 SoC만 재부팅 — SMC GPIO 유지 여부 모름 [가설] → `boot_force_off`가 끄고, 연결이 안 내려가면 실행기를 멈춤(USB-A 보호). `PORT_PERST_OFF`는 절대 지우지 않음(포트 0 DART가 그 블록 안, S257 실기 SError) |

## 2. 파일

| 파일 | 내용 |
|---|---|
| `nwoas_wifi_s259.py` | 하이퍼바이저 쪽 논리: `boot_force_off`, `power_up`, `power_down`, 제어 페이지 레지스터, 설정 공간 쓰기 허용 표. S240 `wifi_probe.py`·S257 `wifi_up.py`/`wifi_scan.py`를 **고치지 않고** 불러 씀 |
| `wifi_module.py` | `run_guest.py -m` 모듈. ADT 사실·지문·DART 위치·제어 페이지 겹침 검사 → 끄기 → HOOK 설치. 실패하면 예외(실행기가 게스트 전에 멈춤) |
| `acpi/NwoasWifiJ274Ssdt.asl`, `acpi/NwoasWifiJ274.asl.inc` | `\_SB.NWC0`(ACPI0004) + `WLN0`(`NWOA4378`) |
| `test_wifi_hv.py` | 가짜 하드웨어 시험 18개 |
| `jobs/W1-wifi-pci-status.cmd` | Windows 점검 작업: 장치 상태·문제 코드·자원(메모리·IRQ)·`pnputil /resources`·USB-A 상태·관련 이벤트 |
| `run-s259.sh` | 재시작(또는 대기 상태) → s259 실행기 → D28 → W1 |
| `../fw-wifi-s259/build.py`, `src/MemoryInitPeiLib.c` | 펌웨어 빌드(S256 + SSDT + IORT + 예약). 결과 `out/s259/` |
| `../agxdcp-s259-guest-test.sh` | s256 실행기 사본 + 페이로드·Wi-Fi 모듈 지문(35개 지문 모두 실제 파일과 일치 확인) |

| 결과물 | 크기 (바이트) | sha256 |
|---|---|---|
| `fw-wifi-s259/out/s259/m1n1-payload-s259-wifi.bin` | 33,669,120 | `fbc4406b0103cf028be5be2eb8ec60844af8952d9c845405338d48cb64e50497` |
| Wi-Fi SSDT AML | 701 | `bcdcbb03b167eac4a845a8efcb4ba05a0a6f9caa46732b76fd63364221643254` |
| AGX·소리 SSDT AML | — | S247·S256과 같음(`agx_ssdt_identical_to_s247`, `audio_ssdt_identical_to_s256` = true) |

빌드 명령(이 맥, 약 1분 20초, CPU만):
```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS
mkdir fw-wifi-s259-mu; (cd fw-s237-mu && for e in .git* *; do [ "$e" = venv ] && continue; cp -Rc "$e" ../fw-wifi-s259-mu/; done)
python3 -B nwoas_scripts/fw-wifi-s259/build.py --tree fw-wifi-s259-mu --variant wifi --out nwoas_scripts/fw-wifi-s259/out/s259
```
빌드 뒤 `build.py`에서 `audio_module.py` 지문 고정을 뺐습니다(다른 작업이 12:46에 그 파일을 바꿈; 펌웨어에 필요한 창 주소 검사는 그대로). 그 수정 뒤에는 다시 빌드하지 않았습니다.

## 3. 시험 계획 (오케스트레이터가 실기에서; 이 에이전트는 실행하지 않음)

```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS
PY=m1n1_windows/.venv-hv/bin/python3
# 0) 맥북만: 가짜 하드웨어 시험
$PY -I -B nwoas_scripts/wifi-s259/test_wifi_hv.py
# 1) 실기, Windows가 돌고 있을 때 (W4 재시작 → s259)
bash nwoas_scripts/wifi-s259/run-s259.sh <지금 실행기 .out> <지금 LOG.link>
#    맥미니가 D4 대기 상태일 때
bash nwoas_scripts/wifi-s259/run-s259.sh idle -
# 2) 비교(같은 페이로드, SMC 안 씀, Wi-Fi 숨김): USB-A가 1)과 같은지
NWOAS_WIFI=0 bash nwoas_scripts/wifi-s259/run-s259.sh <.out> <.link>
# 3) 종료 때 끄기 확인: W4 재시작 뒤 이전 LOG에서
grep -a -E '\[s259\] (CMD 0x2|OFF|FAIL)' <이전 LOG>
```

판정 (단계마다 앞 단계가 통과해야 다음):

| 단계 | 볼 곳 | 통과 | 실패면 |
|---|---|---|---|
| A 게스트 전 | 실행기 LOG `[s259] BOOT` | `force-off confirmed`, `control page HOOK ... installed`, `ADT wlan board=atlantisb antenna=X3 cal_len=2096` | `STOP` 문구대로. `link still up`이면 맥미니 전원을 직접 껐다 켬 |
| B UEFI | VUART `HVLOG: S259 reserved Wi-Fi bridge window base=E0000000 bytes=1000000`, `S237 reserved ... C0000000` | 둘 다 있음 | 페이로드 확인 |
| C Windows ACPI | LOG `[s259] CMD 0x1` 뒤 `[s259] READY ids=0x442514e4 chip=0x10034378 ... dart_check=1` | `READY`, `port1_unchanged=True` | `FAIL <코드>`: `SMC_*`(게스트 중 SMC 문제), `LINK_DOWN`(UEFI 뒤 재연결 안 됨), `DART_MISMATCH` |
| D USB-A 회귀 | W1 `DEV ACPI\PNP0D10\0 status=OK`, `USB present=` 값이 2)와 같음, 키보드·마우스 | 같음 | Wi-Fi를 끄고(`NWOAS_WIFI=0`) 비교 |
| E 장치 | W1 `DEV ACPI\NWOA4378\0` problem 28(드라이버 없음), `MEM 6E0800000-6E080FFFF`, `MEM 6E0000000-6E07FFFFF`, `MEM 348000000-348003FFF`, `IRQ 695` | 자원 4개 배정 | problem 12(자원 충돌)면 NWC0 `_CRS` 확인 |
| F 끄기 | 재시작 뒤 이전 LOG `[s259] OFF (CMD_POWER_DOWN) confirmed` | 있음 [가설: Windows가 `_PS3` 부름] | 없으면 다음 실행기 `boot_force_off`가 끔(A단계 기록 확인) |

W1에서 `WIFI ABSENT`이고 C에서 `FAIL`이면 정상적인 안전 동작(`_STA`=0)입니다. W1에서 장치가 있는데 LOG에 `OFF`가 먼저 찍혔다면 Windows가 드라이버 없는 장치를 D3로 내린 것입니다 [가설] — 드라이버가 붙을 때 `_PS0`이 다시 켭니다.

## 4. 위험

| 위험 | 구분 | 내용·대책 |
|---|---|---|
| USB-A 주소 이동 | [사실: 막는 장치] / [가설: 남은 경로] | UEFI 전에 칩을 끄고 연결 내려감을 확인, 안 내려가면 실행기 중단. 남은 경로: 끄기 확인 뒤 UEFI 전까지 누가 다시 켜는 경우(코드상 없음) |
| 공유 DART 표 | [사실] | 포트 0·1이 같은 L1을 씀. 하이퍼바이저 FL1100 고침(`nwoas_dart_alias_low`, `hv_vm.c` 2576·2809행)은 FL1100 링 주소가 4 GiB 위일 때만 낮은 창 칸을 고쳐 쓰는데, 그러면 Wi-Fi DMA도 바뀐 칸을 탐. 지금은 FL1100을 32비트로 묶어(`NWOAS_FORCE_FL32 1`) 최근 5회 부팅 기록에 `DART-LOWALIAS` 0회 |
| 격리 없음 | [사실] | Wi-Fi DMA가 게스트 RAM 전체에 닿음. 드라이버 버그가 USB-A·Windows 메모리를 덮을 수 있음 |
| 게스트 중 SMC | [가설] | `_INI` HOOK 안에서 맥북 Python이 SMC RTKit을 다시 엶. 실패하면 Wi-Fi만 꺼지고(`_STA`=0) 부팅은 계속 |
| HOOK 시간 | [가설] | 켜기 1~2초 동안 그 CPU가 멈춤(가짜 하드웨어 0.2초). 시간 초과 경보가 프록시 응답 중간에 끊으면 프록시 흐름이 어긋날 수 있음 |
| UEFI 뒤 재연결 | [가설] | UEFI가 꺼진 칩으로 연결을 시도한 뒤의 포트 상태에서 S240 순서가 다시 통함 |
| 페이로드·실행기 짝 | [사실] | S259 SSDT는 제어 페이지를 읽으므로 이 페이로드를 s256 실행기로 켜면 매핑 안 된 주소 → 게스트 오류. 반드시 `agxdcp-s259-guest-test.sh` |
| MSI 주소 `0xFFFFF000` | [가설] | 예약 안 됨. 드라이버가 그 페이지를 DMA에 쓰면 MSI로 먹힐 수 있음 → 계약서에서 4 GiB 위 버퍼 권장 |
