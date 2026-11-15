# S240 내장 Wi-Fi 1단계: 전원 켜고 장치 번호 읽기

표기: **[사실]** = 파일·행 번호로 확인한 내용. **[가설]** = 확인하지 않은 추측.
이 폴더의 코드는 **아직 실제 맥미니에서 한 번도 실행하지 않았습니다.** 맥북에서 가짜 하드웨어로 시험한 것만 통과했습니다. 시험 통과는 동작한다는 뜻이 아닙니다.

## 1. 무엇을 하는가

`wifi_probe.py`는 **맥미니가 막 켜져 m1n1 대기 상태에 있을 때**(체인로드·Windows 시작 전) 맥북에서 한 번 실행하는 확인용 스크립트입니다. `nwoas_scripts/ans2-s87/probe.py`처럼 대기 중인 m1n1에 직접 붙습니다.

| 순서 | 하는 일 | 근거 |
|---|---|---|
| 0 | 직렬 포트 2개를 아무도 안 쓰는지 `lsof`로 확인, ADT가 `j274`인지·주소가 기록값과 같은지 확인. 하나라도 다르면 아무것도 쓰지 않고 멈춤 | [사실] 주소 기록값은 `experiments/tahoe-afk-20260906/current.adt`에서 직접 읽음(`wifi_probe.py`의 `EXPECT`) |
| 1 | `p.pcie_init()` | [사실] `usb-s174/guest_map.py` 7행과 같은 호출 |
| 2 | SMC 키 `gP0d`에 `0x800001` 쓰기(Wi-Fi·블루투스 칩 전원) | [사실] `m1n1_windows/proxyclient/experiments/pcie_enable_devices.py` 13~18행, C 쪽 선례 `m1n1-usb-s232/src/dcp.c` 50~53행 |
| 3 | **포트 0만** 리셋 신호(PERST#)·기준 클록·연결 시작 | [사실] UEFI `AppleSiliconPciPlatformDxe.c` 182~330행을 같은 순서로 옮김 |
| 4 | 버스 1 장치 0 기능 0·1의 설정 공간 읽기: 장치 번호, 종류, BAR 크기, PCIe 장치 제어 레지스터(No Snoop·Relaxed Ordering) | — |
| 5 | **항상**(오류가 나도) 되돌리기: 루트 포트 버스 번호 원래대로, PERST# 낮춤, `gP0d`에 `0x800000`, SMC 연결 정리 | [사실] 끄는 값: `experiments/smc.py` 28행. SMC 정리 `stop()`은 C `smc_shutdown`→`rtkit_quiesce`와 같은 전원 상태 순서(`fw/asc/__init__.py` 108행, `smc.c` 117행, `rtkit.c` 659~710행) |

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

`run.sh --run`은 `nwoas_scripts/logs/wifi-s240-<시각>.XXXXXX`에 전체 출력을 남기고, 결과 JSON을 `nwoas_scripts/wifi-s240/run-<시각>/result.json`에 씁니다.

조절할 수 있는 값(모두 상한이 있음):

| 옵션 | 기본값 (ms) | 허용 범위 (ms) | 뜻 |
|---|---|---|---|
| `--power-settle-ms` | 100 | 0~2000 | 전원 켠 뒤 리셋 신호 전까지 기다림 [가설: 100이면 충분] |
| `--link-timeout-ms` | 1000 | 10~5000 | 연결이 올라올 때까지 기다림 |
| `--config-retry-ms` | 1000 | 10~5000 | 장치가 "아직 준비 안 됨"으로 답할 때 다시 읽는 시간 |
| `--no-bar-sizing` | — | — | BAR 크기 재기(쓰고 되돌리기)를 생략하고 읽기만 |

단계별 시간 제한(초): 연결 30, `pcie_init` 20, 사전 확인 15, SMC 켜기 20, 포트 올리기 15, 연결 대기 10, 설정 읽기 30, 끄기 20.

## 4. 예상 출력 [가설]

```
[s240] pcie_init rc=0x0
[s240] root port 0 id=0x....106b port_status=0x1 linksts=0x0
Starting endpoint #0x20 (smcep)
[s240] smc key info key=gP0d info=(4, 'ui32', ...)
[s240] smc write key=gP0d value=0x800001
[s240] port 0 LTSSM started
[s240] port 0 link up=True linksts=0x...1
[s240] bus1 fn0 id=0x442514e4 class_rev=0x0280....
[s240] bus1 fn1 id=0x5f6914e4 class_rev=0x....
[s240] power down confirmed=True
{ "verdict": "PASS_IDS", ... }
S240 verdict PASS_IDS. This is a config-space observation only, not a driver or boot result.
```

`gP0d` 키 정보의 형식(`ui32` 등), 클래스 번호, BAR 개수·크기는 모릅니다. 실제 값이 나오면 그것이 기록입니다.

## 5. 결과 판정

| 종료 코드 | 결과 | 뜻 | 다음 할 일 |
|---|---|---|---|
| 0 | `PASS_IDS` | 기능 0 = `14e4:4425`, 기능 1 = `14e4:5f69`, 전원 끄기 확인 | `result.json`의 BAR 크기·장치 제어 레지스터를 계획 문서 1단계 표에 기록 |
| 1 | `LINK_DOWN` | 전원을 켰는데 연결이 안 올라옴 | 6장의 "전원만으로 되는가" 확인 |
| 1 | `ID_MISMATCH` / `NO_CONFIG_RESPONSE` | 다른 장치이거나 답이 없음 | 결과 JSON 확인 |
| 2 | `ERROR: ...` | 도중 오류(시간 초과 포함). 끄기는 시도했고 확인됨 | 오류 문구 확인 |
| 3 | `REFUSED` / `STOP` | 사전 확인에서 멈춤. 아무것도 켜지 않음 | 문구대로 조건을 맞춤 |
| 4 | 끄기 미확인 | 칩 전원이 켜진 채일 수 있음 | **맥미니 전원을 직접 껐다 켬** |

`PASS_IDS`도 "칩이 PCIe에 보인다"는 관찰일 뿐입니다. 드라이버·Windows·부팅이 된다는 뜻이 아닙니다.

## 6. 위험과 모르는 점

| 항목 | 내용 | 이 스크립트의 대응 |
|---|---|---|
| SMC 쓰기 | 전원 관리 칩에 쓰는 동작. 키 `gP0d`·값 `0x800001`은 Asahi 실험 코드와 DT(`pwren-gpios = <0xb5 0x0d 0x00>`, `dtb-s221/recovered-j274.dts` 3215행) 기준이고 이 맥미니에서 실측한 적 없음 | 키 정보가 4바이트 키가 아니면 쓰지 않음. 이 키 하나만 씀 |
| SMC 상태 되읽기 | 핀 상태를 읽는 SMC 명령 형식을 확인하지 못함 | 되읽기는 하지 않고, PCIe 연결이 올라오는지로 판단 |
| SMC 다시 연결 | 이미 돌고 있는 SMC에 다시 붙는 동작. Asahi 도구(`tools/smccli.py`)가 늘 하는 방식이지만 이 맥미니에서는 처음 [가설: 안전] | 시간 제한 20초 |
| 전원만으로 연결되는가 | 클록 요청 핀(AP GPIO 150), ADT `manual-enable` 등 다른 조건이 있을 수 있음 [가설] | `LINK_DOWN`으로 보고하고 끔 |
| PERST# 핀 쓰기 방식 | UEFI GPIO 드라이버는 다른 비트를 지우고 값만 씀(`AppleEmbeddedGpioControllerDxe.c` 76행). 이 스크립트는 다른 비트(풀업 등)를 남기고 필요한 비트만 바꿈 [가설: 둘 다 동작] | 원래 값을 `result.json`에 기록 |
| 루트 포트 버스 번호 | 버스 1 읽기가 루트 포트의 버스 번호 설정에 달려 있다고 봄 [가설]. UEFI는 나중에 같은 값(0/1/1)을 씀 | 원래 값 저장 후 끝에 되돌림 |
| 연결 없는 장치 읽기 | 연결이 없을 때 버스 1을 읽으면 예외(SError)가 날 수 있음 [가설] | 연결이 올라온 경우에만 읽고, 읽을 때 m1n1 예외 보호(`GUARD_SKIP`, `exception.c` 334·471행)를 켜고 예외 횟수를 기록 |
| BAR 크기 재기 | 장치 BAR에 `0xFFFFFFFF`를 썼다가 되돌림(표준 방식). 메모리 해석이 이미 켜져 있으면 하지 않음 | 되돌린 값을 다시 읽어 `restored`로 기록 |
| 시간 초과 뒤 | 직렬 통신 도중 끊기면 다음 명령이 꼬일 수 있음 | 끄기를 따로 시도하고, 확인 못 하면 종료 코드 4 |
| **USB-A 주소 밀림** | 이 스크립트는 끝에 전원을 끄므로 다음 UEFI 부팅은 지금과 같습니다. 하지만 **다음 단계에서 Wi-Fi를 켠 채 UEFI를 띄우면**, UEFI가 PCI 주소를 자동 배정하면서 USB-A 컨트롤러 주소 `0x6C0000000`(DSDT·DART·하이퍼바이저·`usb-s174/guest_map.py` 8행에 고정값)이 밀릴 수 있습니다 [가설: 실제로 밀리는지] | 이 스크립트는 그 확인을 하지 않습니다. 여기서 얻은 BAR 크기가 그 확인과 주소 고정 설계의 입력입니다 |
| 체인로드와 섞기 | 이 스크립트 뒤에 바로 체인로드하면 포트 상태가 런처 기대와 다를 수 있음 [가설] | 끝나면 맥미니 전원을 껐다 켬(3장 (4)) |

## 7. 파일

| 파일 | 내용 |
|---|---|
| `wifi_probe.py` | 스크립트. 기본은 계획 출력, `--run`일 때만 실행 |
| `test_wifi_probe.py` | 가짜 프록시·가짜 SMC·가짜 칩으로 순서와 거부 경로 27개 시험 |
| `run.sh` | 환경 변수 설정·직렬 포트 확인·로그 저장 래퍼. 체인로드 없음 |
