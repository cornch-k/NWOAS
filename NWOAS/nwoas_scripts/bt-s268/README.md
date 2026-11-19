# S268 — Bluetooth + SSD 동시 처리 기본값을 함께 쓰는 일상 실행기

**실기 검증 안 됨.** Bluetooth(`bt-s261/bt_module.py`, S261 페이로드의 BT SSDT)는 g262R HV에서도,
`power-s260/power_module.py`·`nvmeqd-s262/nvmeqd_module.py`와 함께로도 한 번도 돌지 않았습니다.
S261은 g254L HV에서만 돌았습니다. 빌드·문법 검사·오프라인 시험 통과는 부팅 결과가 아닙니다.

## 무엇인가
- `../agxdcp-s268-guest-test.sh` = 지금의 `agxdcp-s259-guest-test.sh`(S262 기본 켬: 변수 없으면
  `NWOAS_S262_HV=1` g262R + `NWOAS_NVME_QD=32`)에 S261의 Bluetooth 부분만 옮긴 것입니다.
  - 페이로드 `fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin`(SHA-256 `26428b57…0ab92`)
  - `-m wifi-s259/wifi_module.py` 대신 `-m bt-s261/bt_module.py`(와이파이 일도 bt_module이 함)
  - 마지막 모듈 순서: `bt_module.py` → `power_module.py` → `nvmeqd_module.py`(맨 끝)
  - `NWOAS_S260_MADT=1`은 멈춤(그 페이로드에는 BT SSDT가 없음)
  - S261 페이로드를 `wifi_module.py`로 띄우는 길은 없습니다. Bluetooth만 끄려면 `NWOAS_BT=0`.
- `run-s268.sh` = `bt-s261/run-s261.sh` 복사본. 실행기를 띄우고 D28(GPU 다시 켜기) → B0(Bluetooth·와이파이·USB-A
  상태) → W1(와이파이 PCI 상태) 순서로 작업을 돌립니다. 결과는 `bt-s268/runs/`, 마지막 줄
  `S268 DONE link=… out=… log=…`.

## 실행
```
bash bt-s268/run-s268.sh idle -                               # 맥미니가 D4 프록시에서 대기 중일 때
bash bt-s268/run-s268.sh <지금 실행기 .out> <지금 LOG.link>      # Windows가 돌고 있을 때(W4 재시작 먼저)
```

## 스위치(환경 변수)
| 변수 | 기본 | 뜻 |
|---|---|---|
| `NWOAS_BT` | 1 | 0 = 와이파이는 S259와 같게, Bluetooth 숨김(같은 페이로드·같은 bt_module) |
| `NWOAS_WIFI` | 1 | 0 = SMC 접근 없음, 와이파이·Bluetooth 둘 다 숨김 |
| `NWOAS_BT_FENCE_WIFI_RESET` | 1 | 0 = 와이파이 드라이버 칩 재설정 때 fn1 BAR 울타리를 닫지 않음 |
| `NWOAS_S262_HV` | (없음 = 1) | `0` 또는 빈 값 = 옛 g254L 한 번에 하나 경로 |
| `NWOAS_NVME_QD` | (`NWOAS_S262_HV`도 없을 때만 32) | 1..61 동시 명령 수, 0 = 같은 g262R에서 한 번에 하나. `NWOAS_S262_HV=1`을 직접 주면 QD는 자동으로 안 붙어 엔진이 꺼짐(S259에서 물려받은 동작) |
| `NWOAS_S260_HV` | 0 | 1 = g260O + power_module. `NWOAS_S262_HV=0`도 함께 줘야 g260O가 됨(S262가 이김) |
| `NWOAS_NVME_QD_IRQ/STRICT/KICK/POLL`, `NWOAS_PWR_*` | — | `nvmeqd-s262/README.md`, `power-s260/power_module.py` 머리말과 같음 |
| `NWOAS_DELIVER_MANIFEST` 등 | 없음 | 파일 전달(S243), 실행기 머리말 참고 |

## 들어 있지 않은 것
- S265 GPU 풀 실행기(`agxdcp-s265-guest-test.sh`, `gpupool-s26x`)와 S264 C 실행기(`standalone-s264`, 맥북 없이
  바탕화면)에는 Bluetooth가 **없습니다.** S268은 S259 기준이라 GPU 풀(`NWOAS_AGX_POOL_IPA=0`)도 없습니다.

## 확인할 줄(실기에서)
- 실행기: `[S268] Wi-Fi+Bluetooth payload (S261) + bt_module.py on m1n1-g262R-agx-hv2-nvmeqd.bin, NWOAS_WIFI=1 NWOAS_BT=1`
- `[s261] BOOT control pages HOOK …`, `[s261] READY fn1 …`, `[S262] NVMe QD engine ON: QD=32 … ANS irq=590 …`
- 이후 작업 mark의 `[S262] mark … faults=0 timeouts=0`

## 회귀 점검(재부팅 없음): `run-s268-regress.sh`
```
bash bt-s268/run-s268-regress.sh <실행기 .out> <LOG.link> <LOG> [와이파이 관찰 초, 기본 780, 720..2200]
```
- S268로 이미 켜져 있는 Windows에서 돌립니다. 재부팅·W4·B7/B9·드라이버 설치·레지스트리 설정 변경·파일 전달은 하지 않습니다.
- 1) 와이파이: R1(드라이버 1.0.1.9인지) → W2·R2 → W10 → W19 핑 시작 → 240초마다 W2 → W19 멈춤 → W15 → W10 → R2.
- 2) SSD: Q1S(보통 우선순위, 4 KiB 무작위 QD1/8/32 + 1 MiB 순차 읽기, 무결성 시험) → 안전 검사 → Q2(같은 부팅에서 다시 읽기) → 안전 검사 → R3(chkdsk /scan, 이번 부팅 이후 디스크 이벤트를 공급자별로 셈).
  `C:\NWOAS\s262\integ.bin`(384 MiB)은 남겨 둡니다(지우려면 `nvmeqd-s262/jobs/Q3-cleanup.cmd`).
- 3) B2(Bluetooth 상태).
- 새 작업 파일 `jobs/R1-wifi-drvver.cmd`, `R2-wlan-since-boot.cmd`, `R3-chkdsk-diskevt-boot.cmd`는 모두 읽기만 합니다(R3의 chkdsk /scan은 Q4와 같음).
- 결과는 `runs/regress-<시각>.txt`, 마지막 줄은 `S268R DONE | …` 또는 `S268R ABORT <이유>`입니다.
