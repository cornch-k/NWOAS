# S240 내장 Wi-Fi 1단계: 전원 켜고 장치 번호 읽기

표기: **[사실]** = 파일·행 번호로 확인한 내용. **[가설]** = 확인하지 않은 추측.
이 폴더의 코드는 **아직 실제 맥미니에서 한 번도 실행하지 않았습니다.** 맥북에서 가짜 하드웨어로 시험한 것만 통과했습니다. 시험 통과는 동작한다는 뜻이 아닙니다.

## 1. 무엇을 하는가

`wifi_probe.py`는 **맥미니가 막 켜져 m1n1 대기 상태에 있을 때**(체인로드·Windows 시작 전) 맥북에서 한 번 실행하는 확인용 스크립트입니다. `nwoas_scripts/ans2-s87/probe.py`처럼 대기 중인 m1n1에 직접 붙습니다.

| 순서 | 하는 일 | 근거 |
|---|---|---|
| 0 | 직렬 포트 2개를 아무도 안 쓰는지 `lsof`로 확인, ADT가 `j274`인지·주소가 기록값과 같은지 확인. 하나라도 다르면 아무것도 쓰지 않고 멈춤 | [사실] 주소 기록값은 `experiments/tahoe-afk-20260906/current.adt`에서 직접 읽음(`wifi_probe.py`의 `EXPECT`) |
| 1 | `p.pcie_init()` | [사실] `usb-s174/guest_map.py` 7행과 같은 호출 |
| 2 | SMC 키 `gP0d`에 `0x800001` 쓰기(Wi-Fi·블루투스 칩 전원). 키 정보가 4바이트이고 형식이 `ui32`·`hex_`일 때만 씀 | [사실] `m1n1_windows/proxyclient/experiments/pcie_enable_devices.py` 13~18행의 `gP0d` 쓰기, C 쪽 선례 `m1n1-usb-s232/src/dcp.c` 50~53행. **같은 스크립트 16행은 `gP1a`에 `1`도 쓰지만 이 스크립트는 쓰지 않습니다**(6장 [가설] 항목) |
| 3 | **포트 0만** 리셋 신호(PERST#)·기준 클록·연결 시작. 기본값은 UEFI와 같은 순서입니다: 포트 0의 리셋 해제 비트(`0x681000814` 0번)는 **세우기만 하고 지우지 않습니다.** 그 비트를 PERST# 핀을 낮춘 직후 지웠다가 다시 세우는 동작은 `--controller-reset`을 줄 때만 합니다 | [사실] UEFI `AppleSiliconPciPlatformDxe.c` 182~330행 순서, 그 비트는 296행에서 세우기만 함. m1n1도 세우기만 함(`m1n1_windows/src/pcie.c` 591~592행). m1n1 `pcie_init`가 이미 세워 두므로 이 "세우기"는 아무 일도 하지 않으며, 실제 부팅에서 UEFI가 m1n1 뒤에 돌 때도 똑같이 아무 일도 하지 않는데 USB-A(FL1100)는 잡힙니다. [가설] 지우면 `pcie_init`가 적용한 조정값·DBI 설정(`pcie.c` 560~640행)이 풀린 채 연결을 시작할 수 있음 |
| 4 | 버스 1 장치 0 기능 0·1의 설정 공간 읽기: 장치 번호, 종류, BAR 크기, PCIe 장치 제어 레지스터(No Snoop·Relaxed Ordering) | — |
| 5 | **항상**(오류가 나도) 되돌리기: 먼저 직렬 통신을 다시 맞춤(남은 바이트 버리기 + 빈 명령 왕복), 루트 포트 버스 번호 원래대로, PERST# 낮춤(고정값 `0x2`), 리셋 해제 비트 지움(고정값 `0`), `gP0d`에 `0x800000`, SMC 연결 정리, 연결이 내려갔는지 확인, 포트 1 비교. 끄기에서는 레지스터를 읽어 고쳐 쓰지 않고 **정해진 값을 그대로 씀.** **각 단계를 따로** 실행하고(단계마다 시간 제한), 한 단계가 실패해도 다음 단계를 계속 시도하며, 실패한 단계와 오류 문구를 `result.json`의 `power_down.errors`에 남김 | [사실] 끄는 값: `experiments/smc.py` 28행. SMC 정리 `stop()`은 C `smc_shutdown`→`rtkit_quiesce`와 같은 전원 상태 순서(`fw/asc/__init__.py` 108행, `smc.c` 117행, `rtkit.c` 659~710행). 끌 때 리셋 해제 비트를 지우는 것은 m1n1 `pcie_shutdown`과 같음(`pcie.c` 757행). PERST# 고정값 `0x2`는 UEFI GPIO 드라이버가 "출력 0"일 때 쓰는 값과 같음(`AppleEmbeddedGpioControllerDxe.c` 76·136~137행). 직렬 통신 맞추기는 `proxy.py` 426~435행 `_resync_link`와 `nop` |

하지 않는 것:

- 저장 장치(NVMe)를 전혀 건드리지 않습니다.
- USB-A 컨트롤러(FL1100)가 붙은 포트 1에는 **쓰지 않습니다.** 모든 메모리 쓰기는 정해 둔 주소 목록(포트 0 레지스터 4개, 포트 0 PHY 2개, PERST# GPIO 1개, 루트 포트 0 버스 번호 1개, 버스 1의 BAR 12개)으로만 가고, 그 밖의 주소는 막힙니다. 포트 1 레지스터는 처음과 끝에 읽기만 해서 바뀌었는지 비교합니다.
- Windows·UEFI에 장치를 넘기지 않습니다. 끝나면 칩 전원은 꺼진 상태입니다.
- 펌웨어를 올리지 않습니다(그건 2단계).

## 2. 실행 전 조건

1. 다른 작업(런처, 직렬 포트 캡처, 체인로드)이 맥미니를 쓰고 있지 않아야 합니다.
2. 맥미니를 전원을 껐다 켜서 m1n1 대기 상태로 둡니다. 체인로드는 하지 않습니다.
3. 맥북 전원 연결 상태 권장(GPU 작업은 없음).

## 3. 실행 순서

```bash
cd /Users/angigyeom/Desktop/DEV/NWOAS

# (1) 맥북만으로 시험(하드웨어 접근 없음)
python3 -I -B nwoas_scripts/wifi-s240/test_wifi_probe.py
m1n1_windows/.venv-hv/bin/python3 -I -B nwoas_scripts/wifi-s240/test_wifi_probe.py   # ADT 기록 대조 시험 포함

# (2) 계획만 출력(직렬 포트를 열지 않음)
nwoas_scripts/wifi-s240/run.sh

# (3) 실제 실행 — 맥미니가 막 켜진 m1n1 대기 상태일 때만
nwoas_scripts/wifi-s240/run.sh --run

# (4) 끝나면 결과와 상관없이 맥미니 전원을 껐다 켠 뒤에 다른 런처를 씁니다
```

`run.sh --run`은 `nwoas_scripts/logs/wifi-s240-<시각>.XXXXXX`에 전체 출력을 남기고, 결과 JSON을 `nwoas_scripts/wifi-s240/run-<시각>-<임의 문자>/result.json`에 씁니다. 이 폴더는 맥미니에 붙기 **전에** 만들고(같은 초에 두 번 실행해도 따로 생김), 만들 수 없으면 아무것도 건드리지 않고 종료 코드 3으로 멈춥니다. 끝난 뒤 `result.json`을 쓰지 못하면 전체 결과를 `RESULT_JSON ` 줄로 로그에 남깁니다.

조절할 수 있는 값(모두 상한이 있음):

| 옵션 | 기본값 (ms) | 허용 범위 (ms) | 뜻 |
|---|---|---|---|
| `--power-settle-ms` | 100 | 0~2000 | 전원 켠 뒤 리셋 신호 전까지 기다림 [가설: 100이면 충분] |
| `--link-timeout-ms` | 1000 | 10~5000 | 연결이 올라올 때까지 기다림 |
| `--config-retry-ms` | 1000 | 10~5000 | 장치가 "아직 준비 안 됨"으로 답할 때 다시 읽는 시간 |
| `--no-bar-sizing` | — | — | BAR 크기 재기(쓰고 되돌리기)를 생략하고 읽기만 |
| `--controller-reset` | 꺼짐 | — | 포트 0 리셋 해제 비트를 PERST# 아래에서 한 번 지웠다가 다시 세움. UEFI·m1n1·Linux 어디에도 없는 동작이라 기본값은 꺼짐 [가설: 필요한지 모름] |

단계별 시간 제한(초): 연결 30, `pcie_init` 20, 사전 확인 15, SMC 켜기 20, 포트 올리기 15, 연결 대기 10, 설정 읽기 30.
끄기 단계별 시간 제한(초): 직렬 통신 다시 맞추기 5, 버스 번호 되돌리기 5, PERST# 낮추기 5, 컨트롤러 리셋 비트 지우기 5, SMC 끄기 10, SMC 정리 10, 연결 내려감 확인 5, 포트 1 비교 10.

## 4. 예상 출력 [가설]

```
[s240] pcie_init rc=0x0
[s240] root port 0 id=0x....106b port_status=0x1 linksts=0x0
Starting endpoint #0x20 (smcep)
[s240] smc key info key=gP0d info=(4, 'ui32', ...)
[s240] smc write key=gP0d value=0x800001
[s240] port 0 PORT_PERST left as set by pcie_init (reference sequence) before=0x1
[s240] port 0 LTSSM started
[s240] port 0 link up=True linksts=0x...1
[s240] bus1 fn0 id=0x442514e4 class_rev=0x0280....
[s240] bus1 fn1 id=0x5f6914e4 class_rev=0x....
[s240] power down confirmed=True errors=none port1_unchanged=True
{ "verdict": "PASS_IDS", ... }
S240 verdict PASS_IDS. This is a config-space observation only, not a driver or boot result.
```

`gP0d` 키 정보의 형식(`ui32` 등), 클래스 번호, BAR 개수·크기는 모릅니다. 실제 값이 나오면 그것이 기록입니다.

## 5. 결과 판정

| 종료 코드 | 결과 | 뜻 | 다음 할 일 |
|---|---|---|---|
| 0 | `PASS_IDS` | 기능 0 = `14e4:4425`, 기능 1 = `14e4:5f69`, 전원 끄기 확인, 포트 1 그대로 | `result.json`의 BAR 크기·장치 제어 레지스터·`bridge_window_prediction`을 계획 문서 1단계 표에 기록 |
| 1 | `LINK_DOWN` | 전원을 켰는데 연결이 안 올라옴 | 6장의 "전원만으로 되는가" 확인 |
| 1 | `ID_MISMATCH` / `NO_CONFIG_RESPONSE` | 다른 장치이거나 답이 없음 | 결과 JSON 확인 |
| 2 | `ERROR: ...` | 맥미니에 붙은 뒤 도중 오류(시간 초과 포함). 끄기의 모든 단계가 성공했고 연결이 내려간 것까지 확인됨 | 오류 문구 확인 |
| 3 | `REFUSED` / `STOP` | 사전 확인(직렬 포트, 결과 폴더, ADT, `pcie_init`, SMC 키)에서 멈춤. 아무것도 켜지 않음 | 문구대로 조건을 맞춤 |
| 4 | 끄기 미확인 | 끄기 단계 중 하나라도 실패했거나, 끈 뒤에도 연결이 올라가 있음(`link_down_after` 가 `true`가 아님). 칩 전원이 켜진 채일 수 있음 | **맥미니 전원을 직접 껐다 켬. 전원 버튼이 듣지 않으면 전원 케이블을 뽑음** [가설: SMC를 정리한 뒤라 버튼이 안 들을 수 있음] |
| 5 | 포트 1 변화 | 끄기는 확인됐지만 USB-A(FL1100) 포트 1의 읽기 값이 처음과 다르거나 다시 읽지 못함 | 맥미니 전원을 껐다 켠 뒤에 런처 사용. `result.json`의 `port1_before`·`port1_after` 비교 |
| 6 | `CONNECT_FAILED` | 맥미니 m1n1에 붙지 못함. 켜는 단계는 하나도 시도하지 않음 | 직렬 연결·m1n1 대기 상태 확인 |

종료 코드 4가 5보다, 5가 0~3보다 먼저입니다(예: 끄기 미확인이면서 포트 1도 바뀌었으면 4).

`PASS_IDS`도 "칩이 PCIe에 보인다"는 관찰일 뿐입니다. 드라이버·Windows·부팅이 된다는 뜻이 아닙니다.

## 6. 위험과 모르는 점

| 항목 | 내용 | 이 스크립트의 대응 |
|---|---|---|
| SMC 쓰기 | 전원 관리 칩에 쓰는 동작. 키 `gP0d`·값 `0x800001`은 Asahi 실험 코드와 DT(`pwren-gpios = <0xb5 0x0d 0x00>`, `dtb-s221/recovered-j274.dts` 3215행) 기준이고 이 맥미니에서 실측한 적 없음 | 키 정보가 4바이트 `ui32`·`hex_` 키가 아니면 쓰지 않음. 이 키 하나만 씀 |
| `gP1a` 키 | Asahi `pcie_enable_devices.py` 16행은 `gP0d` 다음에 `gP1a`에 `1`을 씀. 무엇을 켜는 키인지, Wi-Fi 연결에 필요한지 모름 [가설] | 1단계에서는 쓰지 않음. `LINK_DOWN`이 나오면 다음 후보로 검토 |
| 컨트롤러 쪽 리셋 | [사실] UEFI(`AppleSiliconPciPlatformDxe.c` 296행)와 m1n1(`m1n1_windows/src/pcie.c` 591~592행), Linux는 포트 0의 리셋 해제 비트(`0x681000814` 0번)를 **세우기만 하고 연결을 올리는 도중 지우지 않습니다.** m1n1 `pcie_init`가 이미 세워 두므로, 실제 부팅에서도 UEFI의 세우기는 아무 일도 하지 않고 USB-A(FL1100)는 잡힙니다. 그래서 기본값은 이 순서를 그대로 따릅니다. 앞선 수정에서 이 비트를 지우도록 바꾸면서 "UEFI 순서와 같아진다"고 적었는데, 이는 틀린 설명이었습니다. [가설] 지우면 `pcie_init`가 적용한 조정값·DBI 설정(`pcie.c` 560~640행)이 풀린 채 연결을 시작해, `LINK_DOWN`이 나와도 다른 원인으로 오해할 수 있음 | 기본값은 지우지 않음. `--controller-reset`을 줄 때만 지움. 지우기 전 값과(지웠으면) 지운 뒤 값, 옵션 사용 여부를 `result.json`의 `port0_perst_reg`·`controller_reset_option`에 기록 |
| SMC 상태 되읽기 | 핀 상태를 읽는 SMC 명령 형식을 확인하지 못함 | 되읽기는 하지 않고, PCIe 연결이 올라오는지로 판단 |
| SMC 다시 연결 | 이미 돌고 있는 SMC에 다시 붙는 동작. Asahi 도구(`tools/smccli.py`)가 늘 하는 방식이지만 이 맥미니에서는 처음 [가설: 안전] | 시간 제한 20초 |
| 전원만으로 연결되는가 | 클록 요청 핀(AP GPIO 150), ADT `manual-enable` 등 다른 조건이 있을 수 있음 [가설] | `LINK_DOWN`으로 보고하고 끔 |
| PERST# 핀 쓰기 방식 | [사실] UEFI GPIO 드라이버는 다른 비트를 지우고 값만 씀(`AppleEmbeddedGpioControllerDxe.c` 76행, "출력 0"이면 `0x2`). 이 스크립트는 켤 때는 다른 비트(풀업 등)를 남기고 필요한 비트만 바꾸고 [가설: 둘 다 동작], 끌 때는 UEFI처럼 `0x2`를 그대로 씀 | 원래 값을 `result.json`에 기록. 끌 때 쓴 고정값은 `power_down.fixed_writes`에 기록 |
| 루트 포트 버스 번호 | 버스 1 읽기가 루트 포트의 버스 번호 설정에 달려 있다고 봄 [가설]. UEFI는 나중에 같은 값(0/1/1)을 씀 | 원래 값 저장 후 끝에 되돌림 |
| 연결 없는 장치 읽기 | 연결이 없을 때 버스 1을 읽으면 예외(SError)가 날 수 있음 [가설] | 연결이 올라온 경우에만 읽고, 읽을 때 m1n1 예외 보호(`GUARD_SKIP`, `exception.c` 334·471행)를 켜고 예외 횟수를 기록 |
| BAR 크기 재기 | 장치 BAR에 `0xFFFFFFFF`를 썼다가 되돌림(표준 방식). 메모리 해석이 이미 켜져 있으면 하지 않음 | 되돌린 값을 다시 읽어 `restored`로 기록 |
| 시간 초과 뒤 | 직렬 통신 도중 끊기면 다음 명령이 꼬일 수 있음. [사실] 프록시 클라이언트는 답의 명령 번호만 맞는지 보므로(`proxy.py` 707~736행), 늦게 온 읽기 답을 다음 읽기의 답으로 받을 수 있음. [가설] 그 값을 PERST# 레지스터에 다시 쓸 수 있었음 | 끄기의 첫 단계에서 남은 바이트를 버리고 빈 명령을 왕복해 통신을 다시 맞춤(실패하면 기록하고 나머지 단계는 계속, 종료 코드 4). 끄기에서는 PERST#·리셋 해제 비트를 읽지 않고 고정값만 씀. 끄기 단계를 하나씩 따로 시도하고(한 단계 실패가 다음 단계를 막지 않음), 하나라도 확인 못 하면 종료 코드 4. 전원 버튼이 듣지 않으면 전원 케이블을 뽑음. [가설] 기다리는 시간(0.2초)보다 더 늦게 온 답은 이 방법으로 못 잡음 |
| **USB-A 주소 밀림** | 이 스크립트는 끝에 전원을 끄므로 다음 UEFI 부팅은 지금과 같습니다. 하지만 **다음 단계에서 Wi-Fi를 켠 채 UEFI를 띄우면**, UEFI가 PCI 주소를 자동 배정하면서 USB-A 컨트롤러 주소 `0x6C0000000`(DSDT·DART·하이퍼바이저·`usb-s174/guest_map.py` 8행에 고정값)이 밀릴 수 있습니다 [가설: 실제로 밀리는지] | 이 스크립트는 그 확인을 하지 않습니다. 여기서 얻은 BAR 크기가 그 확인과 주소 고정 설계의 입력입니다 |
| **Wi-Fi 루트 포트 주소 창도 게스트 RAM에서 빼야 함** | USB-A 손상과 같은 원인입니다(`nwoas_scripts/usb-s232/HANDOFF-USB-A-CORRUPTION.md` 7~10장, `nwoas_scripts/usb-s236/README.md`). [사실] 낮은 4 GiB 게스트 창은 게스트 물리 주소 = PCI 버스 주소이고, PCIe 브리지는 자기 주소 창 안으로 가는 장치 DMA를 RAM으로 넘기지 않습니다. 지금은 FL1100 창 버스 `0xC0000000..0xC00FFFFF`만 BCD `badmemorylist`(PFN `0xC0000..0xC00FF`)로 빼 두었습니다. [사실] 지금 루트 포트 0(Wi-Fi) 창은 꺼져 있습니다(같은 문서 9장). **Wi-Fi를 Windows에 넘기면 UEFI가 루트 포트 0에도 창을 배정하므로, 그 창도 같은 방법으로 빼야 합니다.** **주소는 아직 모릅니다** — UEFI가 Wi-Fi를 켠 채 배정한 뒤 설정 공간에서 읽어야 정해집니다(S235W 방식 ECAM 출력). [가설] EDK2 배정 규칙(`T810XFamilyPkg.dsc.inc` 85~87행, `PciHostBridge.c` 779~827행, `PciResourceSupport.c` 93~127·1164~1172행)대로라면, BAR 중 1 MiB보다 큰 것이 있으면 Wi-Fi 창이 `0xC0000000`부터 오고 FL1100 창은 그 뒤로 밀립니다 | 이 스크립트가 잰 BAR 크기로 예상 창을 계산해 `result.json`의 `bridge_window_prediction`에 남깁니다(버스·CPU 주소, `badmemorylist` PFN 범위, FL1100이 밀리는지). 이 값은 예측일 뿐이고, 실제 주소가 확인되면 이 칸에 적고 `badmemorylist`·`usb-s236/firmware-reserve-bridge-window.patch`·DSDT 고정 주소를 함께 고쳐야 합니다 |
| 체인로드와 섞기 | 이 스크립트 뒤에 바로 체인로드하면 포트 상태가 런처 기대와 다를 수 있음 [가설] | 끝나면 맥미니 전원을 껐다 켬(3장 (4)) |

## 7. 파일

| 파일 | 내용 |
|---|---|
| `wifi_probe.py` | 스크립트. 기본은 계획 출력, `--run`일 때만 실행 |
| `test_wifi_probe.py` | 가짜 프록시·가짜 SMC·가짜 칩으로 순서·거부 경로·고장 주입 61개 시험(2026-11-15 검토 지적 1·2차 수정분 포함) |
| `run.sh` | 환경 변수 설정·직렬 포트 확인·로그 저장 래퍼. 체인로드 없음 |
