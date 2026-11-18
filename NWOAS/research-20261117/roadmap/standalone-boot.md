# 맥북 없는 부팅·macOS와 나란히 쓰기·부팅 안정성·맥북 없을 때의 진단과 복구 — 로드맵 (2026-11-17)

> 읽기와 계획만 했습니다. 아무것도 빌드·실행하지 않았고, 시리얼·USB·맥미니는 건드리지 않았습니다.
> 표기: **[사실]** = 파일:줄, 로그, URL로 확인한 것. **[가설]** = 확인하지 않은 추측.
> 경로 줄임: `N/` = `/Users/angigyeom/Desktop/DEV/NWOAS/`, `HV2/` = `N/m1n1-gpu-hv2/src/`, `L256` = `N/nwoas_scripts/agxdcp-s256-guest-test.sh`, `E/` = `N/external/windows-on-m1/`, `LOG256` = `N/nwoas_scripts/logs/agxmed1g-s249-20261116-202802.1sXX3e`.
> **이 문서의 어떤 단계도 "부팅된다"는 증명이 아닙니다.** 부팅 여부는 맥미니 실기기에서만 확인됩니다.

---

## 0. 결론

1. **화면 쪽 맥북 의존은 이미 사라졌습니다.** 11-15 조사의 12번째 단계("Tahoe DCP를 C로")는 13.5 스텁으로 바꾼 뒤 필요 없어졌습니다. 지금 실행기는 Tahoe용 파이썬 화면 준비 스크립트를 쓰지 않습니다. 13.5 펌웨어에서는 m1n1 C 코드가 화면을 켜고, 그 뒤 하이퍼바이저 C 코드가 DCP를 넘겨받습니다. [사실: `L256:117-118`(사전 스크립트는 읽기만 하는 `fw135_preflight.py`), `N/nwoas_scripts/display-s244/fw135_preflight.py:1-20`, `HV2/main.c:169-172`, `HV2/display.c:1484-1557`]
2. **맥북 없이 켜려면 반드시 한 번은 콘치님 손이 필요합니다.** 지금 스텁에 등록된 D4 m1n1에는 디스크에서 파일을 읽는 기능이 없습니다. 그래서 부팅 파일을 새로 등록해야 하고, 이것은 스텁 쪽 복구 화면(1TR)에서만 됩니다. [사실: D4 파일 `N/experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin`(md5 7bec0225…) 안에 "Chainloading files not supported in this build!" 문자열이 있음, `strings`로 확인. `HV2/chainload.c:115-146`. 1TR 조건은 https://asahilinux.org/docs/platform/open-os-interop/ ("stage 1 can only be modified by the user via booting in 1TR")]
3. **그 한 번을 가장 먼저, 가장 작게 합니다.** 새로 등록할 파일은 "ESP(Windows 부팅 칸)에 있는 파일을 읽어 넘기고, 실패하면 지금 D4처럼 맥북을 기다리는" 작은 m1n1입니다. 처음에는 ESP에 D4를 그대로 놓습니다. 그러면 맥북 개발 방식이 지금과 똑같이 돌아가는지부터 볼 수 있습니다. 그 뒤로는 ESP의 파일만 바꾸면 되므로 복구 화면에 다시 갈 일이 없습니다. [가설: 실기기 미확인. 같은 구조를 Asahi가 쓴다는 것은 사실 — https://asahilinux.org/docs/sw/m1n1-user-guide/ "Stage 2 … m1n1/boot.bin on the EFI system partition"]
4. **가장 큰 일은 여전히 "게스트 띄우기"를 C로 옮기는 것입니다.** 하이퍼바이저 시작은 지금도 맥북의 프록시 명령으로만 됩니다. 참조 프로젝트(windows-on-m1)의 J313용 C 코드가 거의 같은 일을 하므로 이것을 바탕으로 J274 값을 얹습니다. [사실: `HV2/proxy.c:475-482`, 다른 C 호출자 없음. 참조: `E/m1n1_windows/src/hv_autonomous_runtime.c:204-329, 347-492`]
5. **부팅 중 맥북에 묻는 일은 지금 실측으로 약 70번이고, 모두 Windows가 아니라 게스트 m1n1 앞부분에서 나옵니다.** CPU 설정 레지스터 통과 14번·무시 1번, 전원 관리 읽기 39번·쓰기 2번, 보조 CPU 켜기 쓰기 14번입니다. 이것들을 C 처리기로 바꿔야 합니다. [사실: `LOG256:519-622` 세어 봄. 표는 1장]
6. **Windows 재시작은 맥북 없이도 이미 맥미니를 다시 켭니다. 하지만 Windows "종료"도 지금은 재시작입니다.** 종료를 진짜 전원 끄기로 바꾸는 일이 필요합니다. [사실: `HV2/hv_psci.c:1523-1528`(재시작 = `reboot()`), `:1539-1540`(종료도 `flush_and_reboot()`)]
7. **개발용 작업 통로(NS2 가상 디스크)는 맥북 연결 모드에서만 남기고, 맥북 없는 모드에서는 끄는 것을 추천합니다.** 맥북 없는 상태의 작업 통로는 네트워크(USB 유선 랜 또는 와이파이)로 바꿉니다. 와이파이는 안전장치 승인(CVP)을 기다리는 중이라 USB 유선 랜이 먼저입니다. [사실: NS2는 명령마다 맥북에 물음 `HV2/hv_vm.c:1443`. 와이파이 상태는 `N/HANDOFF-2026-11-16-evening.md:9-25`]
8. **GPU를 매 부팅 다시 켜는 작업(G3Armed + 장치 재시작)을 맥북이 보내고 있습니다.** 맥북 없이 바탕화면이 GPU로 그려지려면 Windows 예약 작업(T1, 이미 써 둠)이나 드라이버 수정이 필요합니다. T1은 Windows 설정을 바꾸는 일이라 **콘치님 결정**으로 남아 있습니다. [사실: `N/nwoas_scripts/agx-s248/jobs/D28-rearm.cmd`, `T1-arm-task.cmd`, `N/HANDOFF-2026-11-16-evening.md:363-365`]

---

## 1. 지금 맥북이 하는 일 전체 목록 (s256 실행기 기준으로 다시 확인)

기준: `L256`(지금 쓰는 실행기)과 그 `-m` 모듈, `HV2`(g254L 소스). "필요"는 맥북 없이 Windows 바탕화면을 띄우는 데 필요한가입니다.

### 1.1 켜기 전·켜는 중

| # | 맥북이 하는 일 | 근거 | 맥북 없이도 필요? | C 대체가 있나 | 메모 |
|---|---|---|---|---|---|
| B1 | 프록시 응답 확인(`proxy_ready.py`) | `L256:104` | 아니오 | 필요 없음 | 확인용 |
| B2 | 13.5 펌웨어·D4 신원·메모리 배치 확인(`probe_fw135_s244.py`) | `L256:107-110`; D4 md5 고정 `display-s244/probe_fw135_s244.py:34-35,50` | 검사만 필요 | 없음(작음) | 새 1단계 파일을 쓰면 "D4가 아님(종료 3)"으로 멈춤 → 신원 검사 갱신 필요 [사실] |
| B3 | 하이퍼바이저 파일 올리기(`chainload.py -r`) | `L256:111` | 예 | **반쯤**: `chainload_load`는 `CHAINLOADING=1`일 때만 있음(`HV2/chainload.c:115-146`). D4에는 없음(문자열 확인) | 디스크에서 읽으려면 새 1단계 등록이 필요 |
| B4 | vUART 진단 출력 받기(`capture_vuart.py`) | `L256:113-115` | 아니오 | 없음 | 맥북 없으면 기록이 사라짐 → 5단계(기록 남기기) |
| B5 | 13.5 확인·화면 크기 확인(`fw135_preflight.py`, 읽기만) | `L256:118`, `fw135_preflight.py:28-41` | 검사만 필요 | 없음(작음) | C에서는 `os_firmware.iboot` 문자열과 `cur_boot_args.video`만 보면 됨 [가설] |
| B6 | `hv.init`·`load_raw`·`map_essential`·`setup_adt`·`start`(run_guest.py 본체) | `L256:117`; 하이퍼바이저 시작은 프록시 명령뿐 `HV2/proxy.c:475-482` | 예 | **없음**(우리 트리). 참조에 J313판 있음 `E/m1n1_windows/src/hv_autonomous_runtime.c` | 가장 큰 일 |
| B7 | 화면: `hv.start`가 `fb_improve_logo`·`fb_shutdown(True)` 호출 | `N/m1n1_windows/proxyclient/m1n1/hv/__init__.py:2435-2442`, preflight 설명 `fw135_preflight.py:13-15` | 예 | C 함수 있음(`fb_shutdown`, `HV2/main.c:205-207`에서도 씀) | 호출만 옮기면 됨 [가설] |
| B8 | Tahoe용 DCP 준비 스크립트 | 지금 실행기에 **없음** `L256:117-129` | **아니오** | 해당 없음 | 11-15 조사 12번 단계는 13.5 스텁으로 대체됨 [사실] |
| B9 | `capture.py`(CPU 상태 읽기) | `L256:119` | 아니오 | 필요 없음 | 진단 |
| B10 | `guest_map.py`: `pcie_init`, 통과 범위 3개, 아래쪽 4 GiB 창 | `N/nwoas_scripts/usb-s174/guest_map.py:7-19` | 예 | `pcie_init`·`hv_map_hw` 있음 | 부르는 코드만 새로 [가설: 작음] |
| B11 | `pmgr_gate.py` + `map_essential`의 전원 관리·CPU 켜기 감시 | `smp-s133/pmgr_gate.py:18-30`; 실측 `LOG256:519-622` | 예 | **없음** | 아래 1.2 표의 R1~R3 |
| B12 | `guest_module.py`: 메모리 크기 4 GiB 줄이기, `nvme_init`, 게스트 주소 등록, GPT·디스크 GUID·보호 칸 검사 | 11-15 조사, `standalone.md` 2.4 #25 | 예 | 부품만 있음(`nvme_init`, `nvme_set_guest_map`) | GUID·보호 칸 검사는 새로 |
| B13 | `link_module.py`: NS2 가상 디스크(작업 통로) 만들기 | `L256:123` | 바탕화면에는 아니오 | 없음 | 개발용. 2장 참고 |
| B14 | `file-delivery-s243/module.py`: NS2에 파일 전달 | `file-delivery-s243/module.py:1-10` | 아니오 | 없음 | 개발용, NS2에 기댐 |
| B15 | `layout-s231/module.py`: NVMe 빠른 경로 켜기(`fastpath(16, 8448, 8)`) | `layout-s231/module.py:12-15` | 예 | `s229_enable` 있음. 단 NS2 크기 0을 거부 `HV2/nwoas_s229_adapter.inc:112` | 맥북 없는 모드는 "NS1만" 판이 필요 |
| B16 | `agx_hv2_module.py`: GPU 설정 512바이트 보내기 | `port-j274/gpu/hv/tools-hv2/agx_hv2_module.py:22,42-69`; 받는 쪽 `HV2/proxy.c:515-517` | GPU 쓰려면 예 | 받는 C(`nwoas_agx_configure`)는 있음. 만드는 쪽만 없음 | 값이 모두 상수·배치에서 나옴 → 작음 [가설] |
| B17 | `query_module.py`, `observe.py` | `L256:127-128` | 아니오 | 필요 없음 | 진단 |
| B18 | `audio_module.py`: 스피커 길 준비(전원·클럭·DART 2번 스트림·앰프 I2C·게스트 창) | `sound-s256/audio_module.py:1-22,144-159` | 소리 쓰려면 예 | **없음**. C에 `i2c.c`·`dart.c`·`pmgr.c`는 있으나 ADMAC·MCA·NCO 코드는 `HV2/`에 없음(`ls`) | SSDT `SPK0`가 가리키는 창이 매핑 안 되면 안 됨(모듈이 그래서 멈춤, `audio_module.py:16-19`) → 첫 맥북 없는 판은 소리 없는 펌웨어(S247)로 [가설] |
| B19 | 부팅마다 GPU 다시 켜기(D28 작업) | `agx-s248/jobs/D28-rearm.cmd` | 맥북 없이 GPU 바탕화면이면 예 | Windows 예약 작업 T1(`T1-arm-task.cmd`)이 있음, 미설치 | 콘치님 결정 사항 |
| B20 | Windows 재시작 뒤 다시 켜기(`run-reboot-arm-*.sh`) | `N/HANDOFF-2026-11-16-evening.md:38-50` | 맥북 없는 부팅이 되면 아니오 | 없음 | 맥북 없는 부팅이 되면 자동 |

### 1.2 실행 중(게스트 진입 뒤) 맥북으로 넘어가는 경로

| # | 경로 | 근거 | 지금 실측 횟수(s256 한 번) | 맥북 없을 때 일어나는 일 | C 대체 |
|---|---|---|---|---|---|
| R1 | CPU 설정 레지스터 통과(OSLAR, HID5, EHID9/10/20, ACC_CFG 읽기) | `LOG256:520-534` | 통과 14, 무시 1 | 모든 CPU가 `uartproxy_run`에서 멈춤 [사실: `HV2/hv_exc.c:2073-2075` → `:109`] | 없음 → 해당 레지스터만 통과·무시하는 C 표 |
| R2 | 전원 관리 상태 읽기·쓰기 감시 | `LOG256:535-574, 615-620` | 읽기 39, 쓰기 2("Dangerous write") | 같음 | 없음 → C 감시(읽기는 실제 하드웨어로, 위험한 쓰기는 버림) |
| R3 | 보조 CPU 켜기 쓰기(CPUSTART) | `LOG256:575-614` | 14 | 같음 | 없음 → C 감시가 버리고 셈. 보조 CPU는 PSCI로 켜짐(C, `hv_psci.c`) |
| R4 | NS2 명령마다 | `HV2/hv_vm.c:1443` | 로그에 안 찍힘 | 멈춤 | NS1만 판에서 사라짐 |
| R5 | Python 감시 범위 접근(HV_HOOK_VM) | `HV2/hv_vm.c:4162, 4417` | 0 (NVMe 감시는 오면 오류 `layout-s231/module.py:17-28`) | 멈춤 | C 판에서는 Python 감시를 안 놓으면 사라짐 [가설] |
| R6 | 가상 타이머 문의(HV_VTIMER) | `HV2/hv_exc.c:2407-2410` | 0 | 멈춤 | 무시 처리 |
| R7 | 처리 못 한 동기 예외·SError | `HV2/hv_exc.c:2073-2075, 2465` | 0 | 멈춤 | 없음 → 기록 남기고 재시작 |
| R8 | 성능 카운터 FIQ(UPMC) | `HV2/hv_exc.c:2422-2427` | 0 | 멈춤 | 무시 처리 |
| R9 | 맥북이 Ctrl-C를 보냄(hv_tick) | `HV2/hv.c:491` | — | 일어나지 않음 | 필요 없음 |
| R10 | 게스트 인터럽트·vGIC·타이머·PSCI·vUART·NVMe I/O·DCP 화면 서비스·GPU 중개 | `HV2/hv_vgic.c`, `hv_psci.c`, `hv_vm.c` 빠른 경로, `display.c` S251~S254 | — | 이미 C | 있음 |
| R11 | Windows 재시작 | `HV2/hv_psci.c:1689-1698, 1523-1528`; UEFI도 PSCI 사용 `N/apple_silicon_platforms_mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc:511-512` | — | 이미 C로 `reboot()` | 있음. SSD 정리 없이 재시작 [사실: 재시작 경로에 `nvme_shutdown` 없음] |
| R12 | Windows 종료 | `HV2/hv_psci.c:1539-1540` | — | **재시작됨**(전원 안 꺼짐) | 없음 → SMC로 끄기 |

`_hv_exc_proxy`는 맥북 응답이 없으면 돌아오지 않습니다. 응답이 "처리 안 됨"이면 재시작합니다(`HV2/hv_exc.c:124-127`). 그래서 맥북이 없으면 재시작조차 되지 않고 멈춥니다. [사실: 코드. 실기기에서 케이블을 뽑아 본 기록은 없음 — 11-15 `standalone.md` 2.2와 같음]

### 1.3 11-15 조사 12단계를 지금 트리로 다시 판정

| 단계 | 11-15 내용 | 지금 판정 | 근거 |
|---|---|---|---|
| M1 기준선 | S231 기록 | **바꿔 함**: 기준은 s256(g254L)로. 횟수는 1.2 표 | `LOG256` |
| M2 진단 모듈 빼기 | capture·observe·query | 그대로 유효(아직 실행기에 있음) | `L256:119,127-128` |
| M3 MSR·VTIMER 횟수 | 기록 추가 | **이미 나옴**: 통과 14·무시 1·VTIMER 0 | `LOG256:520-534` |
| M4 NS1만 판 | adapter 고치기 | 그대로 필요. NS2 크기 0 거부도 그대로 | `HV2/nwoas_s229_adapter.inc:112` |
| M5 케이블 뽑기 시험 | 바탕화면 뒤 뽑기 | 그대로 필요(한 번도 안 함) | 기록 없음 |
| M6 C 전원 관리·CPU 켜기 감시 | | 그대로 필요 | 1.2 R2·R3 |
| M7 NVMe C 초기화·GUID 검사 | | 그대로 필요 | B12·B15 |
| M8 C 게스트 실행기 | | 그대로 필요, 가장 큼 | B6 |
| M9 스텁·체인로드 | APFS 스텁 만들기 | **스텁은 이미 있음**(슬롯 3, 13.5, 기본 부팅). 남은 것은 1단계 파일 새로 등록 + ESP에 2단계 파일 | `N/research-20261116/RESULT-20261116-morning.md` 5장 |
| M10 Macintosh HD 되살리기 | 1TR | 그대로 필요, 방법은 11-16에 실제로 써 봄(`bputil -g`) | `N/stub-135/README.md:87-91`, `R3` 1장 1번 |
| M11 맥북 없는 2단계 | | 그대로 필요 | — |
| M12 C Tahoe DCP | | **필요 없어짐**(13.5 스텁 + m1n1 C 화면 + 하이퍼바이저 DCP 서비스) | 0장 1번 |

---

## 2. 목표 상태

1. 전원 버튼을 누르면 맥미니가 혼자 Windows 바탕화면(HDMI, GPU 그리기)까지 갑니다.
   부팅 순서: iBoot → 스텁(13.5) → 새 1단계 m1n1 → ESP의 `m1n1\boot.bin`(하이퍼바이저 + 펌웨어 묶음) → 게스트 m1n1 앞부분 + Mu → Windows.
2. 맥북을 꽂아 두고 켜면, 2단계 파일이 몇 초 동안 맥북을 기다렸다가 맥북이 붙으면 지금과 똑같은 개발 방식(프록시 + Python + NS2 작업 통로)으로 넘어갑니다. [참조 프로젝트도 3초 창을 둠: `E/m1n1_windows/src/hv_autonomous_profile.c:37-45`, `hv_autonomous_boot_runtime.c:128-157`]
3. 전원 버튼을 길게 누르면 시동 옵션에서 Macintosh HD(순정 macOS)와 Windows를 고를 수 있습니다.
4. Windows 재시작은 맥북 없이 다시 Windows로, Windows 종료는 진짜 꺼짐으로 끝납니다.
5. 부팅이 실패해도 (가) 맥북을 꽂으면 프록시로, (나) macOS로 켜서 ESP 파일을 되돌리면 복구됩니다. 디스크 칸을 다시 나누지 않습니다.
6. 하이퍼바이저가 맥북 없이 치명적 오류를 만나면 멈추지 않고 기록을 남긴 뒤 재시작합니다.

---

## 3. 단계별 계획

순서 원칙: 콘치님 손이 필요한 일(1TR)은 맨 앞에 한 번 몰고, 그 뒤 일은 ESP 파일 바꾸기와 Windows 재시작만으로 되게 합니다. 모든 단계에서 지금 방식(맥북 + D4)으로 돌아갈 수 있어야 합니다.

### 1단계 — 디스크에서 다음 파일을 읽는 새 1단계 m1n1 등록 (맥북 개발 방식은 그대로)

- **할 일**
  1. 1단계 파일 빌드: D4와 같은 화면 동작을 하는 m1n1 트리에서 `CHAINLOADING=1`로 빌드합니다. 끝에 변수 `chainload=a3e73003-c14a-487b-9f2e-8854fe119192;m1n1\boot.bin`(ESP = GPT 4번 칸 고유 GUID)을 붙입니다. [사실: GUID는 `N/nwoas_scripts/stub-attr-s245/plan/new-lba-2.bin` 4번 칸을 읽어 얻음. 변수 처리 `HV2/payload.c:200, 313`. 변수 붙이는 형식은 Asahi `m1n1.build`(11-15 조사 bootchain 보고)]
     - `RELEASE=1`은 쓰지 않습니다. `RELEASE`의 초기 맥북 대기 창은 "화면이 없고 SIP 값이 127일 때만" 열려서 HDMI가 꽂힌 맥미니에서는 안 열릴 수 있습니다. [사실: `HV2/config.h:48-52`, `HV2/main.c:85`. 맥미니에서 실제 값은 가설]
     - 체인로드가 실패하면 m1n1은 원래대로 프록시로 떨어져 맥북을 기다립니다. 즉 실패해도 D4와 같은 상태입니다. [사실: `HV2/main.c:119-138`]
  2. 처음 2단계 파일은 **D4 그대로**(`m1n1-dcp-defer.bin`)를 ESP `\m1n1\boot.bin`에 둡니다. 그러면 체인로드 뒤 실제로 도는 것은 D4이므로 지금 실행기의 D4 신원 검사도 그대로 통과합니다. [가설: 검사가 "실행 중인 m1n1 이미지 바이트"를 보므로 위치가 달라도 통과할 것 — `probe_fw135_s244.py:12`]
  3. 파일을 ESP에 놓는 방법: 지금 Windows가 켜지므로 작업 통로로 `mountvol S: /s` → `S:\m1n1\` 만들기 → 복사 → 해시 확인. 1단계 파일(`stage1.bin`)과 짧은 등록 스크립트(`go1.sh`)도 같은 폴더에 둡니다. ESP는 FAT32로 이미 만들어졌고 Windows 부팅 파일이 있습니다. [사실: `N/HANDOFF-2026-11-16.md:1716`(J3 포맷), `:1770-1786`(J5 bcdboot, `S:\EFI\Boot\bootaa64.efi` 확인)]
- **바꿀 파일**: 새 빌드 폴더(예: `port-j274/stage1/`), 새 `go1.sh`, `stub-135/tool/step2/step2.sh`를 바탕으로 한 사본(지문 값만 새 파일로). 기존 파일은 고치지 않습니다.
- **실기기 검증**
  1. 등록 뒤 재시동 → 맥북에서 프록시가 응답함 → 실행기 `L256`이 그대로 끝까지 감 → Windows 바탕화면.
  2. 시리얼 출력에 "Checking for payloads…" 다음 체인로드 성공 줄이 있고, 이어서 D4 시작 줄("m1n1 v…")이 한 번 더 찍힘.
  3. 일부러 ESP의 `boot.bin` 이름을 바꿔 두고 켜면 1단계 m1n1이 프록시로 떨어지는지(맥북이 붙는지) 확인. 이때는 신원 검사가 "D4 아님"으로 멈추는 것이 정상입니다.
- **콘치님 손: 필요(1번, 약 5분)**. 스텁 자기 짝 복구 화면(13.5 recoveryOS)에서만 됩니다.
  - 들어가는 법: 맥미니를 끔 → 10초 → 전원 버튼을 떼지 않고 길게 → "시동 옵션 로드 중" → "옵션" → 계속 → 암호. 지금 기본 부팅이 스텁이므로 그 짝 복구가 뜰 것입니다. [가설. 근거: Asahi 문서 "takes you to the paired recovery snapshot of the blessed APFS container" https://asahilinux.org/docs/platform/security/ , 11-16 11:2x에 스텁을 기본으로 한 뒤 1TR이 열림 `RESULT-20261116-morning.md` 5장]
  - 칠 줄(예): `diskutil mount a3e73003-c14a-487b-9f2e-8854fe119192 && sh "$(diskutil info a3e73003-c14a-487b-9f2e-8854fe119192 | sed -n 's/^ *Mount Point: *//p')/m1n1/go1.sh"`
  - `go1.sh`가 할 일: `stat`·`md5`로 크기·지문 확인(**`shasum`은 13.5 복구에 없음**) → `bputil -d -v C15A9C5D-F2F8-486D-95CA-F47E3C47A7CD`로 `Paired`·`one true recoveryOS` 확인(아니면 아무것도 바꾸지 않고 멈춤) → `kmutil configure-boot -c stage1.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v /Volumes/Windows` → 결과 기록 → `reboot`.
  - 실행 환경 확인: 13.5 복구 명령 목록에 `diskutil`, `mount_msdos`, `md5`, `stat`, `sh`, `bputil`, `kmutil`, `bless`, `reboot`, `shutdown`이 있고 `shasum`은 없습니다. [사실: `N/research-20261116/C4-recovery-13.5-commands.txt`. 단 이 목록은 13.5 묶음의 복구 디스크 이미지에서 뽑은 것이고, 실제 켜진 복구 화면에서 본 것은 아님]
  - `bputil -nc`는 11-16에 이미 해 두었으므로 다시 하지 않습니다. 같은 스텁에서 `kmutil configure-boot`를 다시 부르면 등록이 바뀝니다. [사실: `step2.sh:80-100`을 11-16 11:2x에 실행해 `STEP2-DONE`. "다시 부르면 바뀐다"는 Asahi의 "Upgrade m1n1 on an existing OS" 흐름과 같은 원리 — `research-20261116/src/asahi-installer/src/main.py:409-450`, 그 흐름은 macOS에서 파일을 바꾼 뒤 1TR에서 다시 등록. 우리 판의 재등록 성공은 가설]
  - 와이파이를 켜 둡니다. 11-16에 복구 화면 등록 때 서버 서명이 필요할 수 있다고 보고 켜 두었습니다. [가설: `HANDOFF-2026-11-16.md:4462`]
  - 실패할 때 다음 명령(미리 함께 드림): `kmutil` 실패 → 같은 줄 다시 실행(스크립트가 반복). "Local policy update is in progress" 재시동 반복 → 전원 10초로 끄고 사진만(asahi-installer #425·#255 계열). 등록 뒤 아무것도 안 뜸 → 3번 되돌리기.
- **위험**
  - 1단계 파일 크기: D4는 2,097,152 B입니다. 등록 가능한 최대 크기는 모릅니다. 1단계 파일은 D4 이하로 만듭니다. [가설: 한도 미확인]
  - m1n1 Rust FAT 읽기가 이 디스크(4 KiB 블록, Windows가 만든 FAT32)에서 되는지 실기기 기록이 없습니다. [가설. Asahi가 같은 종류 디스크에서 쓰는 방식이라는 것은 사실]
  - 체인로드는 파일 전체를 힙에 읽고 한 번 더 복사합니다. 2단계가 커지면(약 36 MB) 힙 여유가 문제일 수 있습니다. [가설: 11-15 조사 bootchain 보고]
- **되돌리는 법**: 같은 스텁 1TR에서 원래 `step2.sh`(D4 `boot.bin`, `/Volumes/Windows/Finish Installation.app/Contents/Resources/`)를 다시 실행하면 D4 등록으로 돌아갑니다. [사실: 그 파일과 지문 검사 `step2.sh:14-16,31-33`]. 그 사이에도 Macintosh HD의 D4로 켜서 맥북 개발을 할 수 있습니다(시동 옵션에서 Macintosh HD).
- **선행 조건**: Windows가 켜져 작업 통로가 되는 상태(ESP에 파일 넣기). 1단계 파일 오프라인 빌드·변수 확인.

### 2단계 — 2단계 파일을 우리 하이퍼바이저로 바꾸고 "맥북 기다리는 창"을 넣기 (아직 맥북이 게스트를 띄움)

- **할 일**: g254L 하이퍼바이저에 다음만 더한 판을 ESP `\m1n1\boot.bin`에 둡니다.
  - 켜지면 USB를 올리고 정해진 시간(참조처럼 3초) 동안 맥북을 기다립니다. 맥북이 붙으면 프록시로 갑니다(지금과 똑같은 개발 방식). 이 단계에서는 맥북이 안 붙어도 그냥 프록시에서 기다립니다.
  - ESP에 `\m1n1\nowin` 파일이 있으면 늘 프록시로 가는 규칙(나중 복구용)을 미리 넣습니다.
  - 실행기 쪽: 신원 검사(B2)가 이 하이퍼바이저 판을 받아들이게 새 사본을 만듭니다. `chainload.py -r`로 또 다른 하이퍼바이저를 올리는 지금 흐름은 그대로입니다.
- **바꿀 파일**: `m1n1-gpu-hv2/src/main.c`(맥북 기다리는 창), 새 `display-s244` 신원 검사 사본, 새 실행기 사본. 참조: `E/m1n1_windows/src/hv_autonomous_boot_runtime.c:28-52, 128-157`.
- **실기기 검증**: 맥북 붙임 → 실행기 끝까지 → 바탕화면. 맥북 안 붙임 → 시리얼(USB) 없이도 HDMI에 m1n1 글자 화면이 남는지(콘치님 눈) [가설].
- **콘치님 손**: 없음(ESP 파일 바꾸기는 작업 통로로).
- **위험**: 이 하이퍼바이저가 1단계에서 체인로드된 뒤 다시 `chainload.py -r`로 체인로드되는 3겹 구성은 처음입니다. 게스트 메모리 시작 주소가 지금과 달라질 수 있습니다(Python은 실행 때 계산하므로 문제 없을 것) [가설].
- **되돌리는 법**: Windows 또는 macOS에서 ESP `boot.bin`을 D4로 되돌림. 아무것도 안 켜지면 맥북을 꽂음(1단계 m1n1이 프록시로 떨어짐).
- **선행 조건**: 1단계.

### 3단계 — 부팅 중 맥북 문의 없애기(C 처리기) + NVMe 스스로 켜기

- **할 일**
  1. R1: 실측된 레지스터(OSLAR_EL1, HID5_EL1, EHID9/10/20_EL1 통과, ACC_CFG_EL1 쓰기 무시)를 C 표로 처리합니다. 표에 없는 것만 맥북으로(맥북 없으면 5단계 규칙).
  2. R2·R3: 전원 관리 감시를 C로(읽기는 실제 하드웨어로 넘기고, 위험한 쓰기·CPU 켜기 쓰기는 버리고 셈). 다른 방법으로 게스트 m1n1 앞부분에서 `smp_start_secondaries()`를 빼는 것도 있습니다(11-15 조사 modules 보고) [가설].
  3. B12·B15: `nvme_init`, 게스트 주소 등록, 디스크 GUID·"EFI PART"·보호 칸 4개 비교, 빠른 경로 켜기를 C에서 스스로. 맥북 없는 모드는 NS2를 알리지 않는 "NS1만" 판으로 `s229_enable`을 고칩니다(`HV2/nwoas_s229_adapter.inc:110-115`).
- **바꿀 파일**: `HV2/hv_exc.c`, `HV2/hv_vm.c`, `HV2/nwoas_s229_adapter.inc`, 새 `HV2/nwoas_standalone_*.c`.
- **실기기 검증**: 맥북 연결 모드로 켜되 Python 쪽 `pmgr_gate.py`와 PMGR·CPUSTART 감시를 빼고 돌려 시리얼에 C 카운터(통과 14, 무시 1, CPU 켜기 14)가 찍히고 Windows가 CPU 8개로 뜨는지. 디스크 GUID를 일부러 틀리게 한 판은 빠른 경로를 켜지 않고 멈춰야 함.
- **콘치님 손**: 없음.
- **위험**: CPU 켜기 쓰기를 잘못 처리하면 9월의 이중 시작 멈춤과 같은 증상 [가설]. NS1만 판은 한 번도 켜 본 적 없음(11-15 조사 C-3).
- **되돌리는 법**: ESP `boot.bin`을 2단계 판으로.
- **선행 조건**: 2단계.

### 4단계 — C 게스트 실행기 (맥북 없이 바탕화면, 화면 포함)

- **할 일**: `hv.init` → 하드웨어 범위 연결 → 레지스터 값 → vUART → ADT 고치기(USB0 노드 지우기 등, Python과 같게) → 메모리 배치·부트 인자·SEPFW/TrustCache 복사 → 펌웨어 묶음(2단계 파일 뒤에 붙인 것) 복사 → B10 창 → 3단계 처리기 → GPU 설정 512바이트 만들기(B16) → `fb_shutdown`(B7) → `hv_start`. 참조의 순서를 바탕으로 J274 값을 넣습니다(`port-j274/config/j274-guest-layout.json`, `GAP-LIST.md` G-2a~G-2g).
  - 첫 판은 **소리 없는 펌웨어 묶음(S247)**을 씁니다. S256 묶음은 SSDT가 소리 창을 가리키는데 그 창은 Python 모듈이 매핑합니다(`audio_module.py:16-19, 156`).
  - 첫 판은 게스트 m1n1 앞부분을 그대로 둡니다(13.5에서 화면을 켜는 것이 이 앞부분이기도 함, `fw135_preflight.py:7-11`).
- **바꿀 파일**: 새 `HV2/nwoas_boot.c`(참조 `hv_autonomous_runtime.c` 바탕), `HV2/main.c`(기다리는 창이 끝나면 호출), 묶음을 붙이는 오프라인 도구(참조 `E/standalone_image.py`).
- **실기기 검증**
  1. 먼저 맥북 연결 모드로 켜되, 맥북이 "C 실행기를 불러라"라는 명령 하나만 보냄 → 시리얼에 C가 만든 ADT·부트 인자를 Python 판과 바이트 비교(시작 주소 차이는 뺌, `standalone.md` 3.2).
  2. 그다음 맥북을 빼고 켬 → HDMI에 UEFI 로고 → Windows 바탕화면(콘치님 눈). Windows 시작 작업이 `C:\NWOAS\boot-marker.txt`에 시각을 쓰게 하고, 다음 맥북 연결 부팅 때 읽어 확인.
  3. 바탕화면에서 10분 키보드·마우스 사용.
- **콘치님 손**: 맥북 빼고 켜기, 화면 보기(물리 조작만).
- **위험**: 이식 범위가 가장 큼. 화면은 이론상 m1n1 C + 하이퍼바이저 DCP 서비스로 나오지만 맥북 없는 상태에서 확인한 적 없음 [가설]. 예약 영역(carveout) 20개가 게스트 RAM 안에 있음(`port-j274/README.md` 6번, 막힘 항목 L-8).
- **되돌리는 법**: 맥북을 꽂고 켜면 기다리는 창에서 프록시로. 또는 ESP에 `nowin` 파일.
- **선행 조건**: 3단계.

### 5단계 — 맥북 없을 때의 예외 처리, 기록 남기기, 종료·재시작 정리

- **할 일**
  1. 맥북 없는 표시가 켜져 있으면 `_hv_exc_proxy`가 맥북을 기다리지 않게 합니다. 가상 타이머·성능 카운터는 무시하고, 모르는 레지스터는 읽기 0·쓰기 무시로 세고, 처리 못 한 예외·SError는 기록 후 재시작합니다. 참조 프로젝트도 이 부분은 없습니다. [사실: `GAP-LIST.md` R-1~R-3, `E/m1n1_windows/src/hv_exc.c`에 맥북 없는 처리 없음(grep)]
  2. 하이퍼바이저 기록: 힙에 기록 고리(예: 1 MiB)를 두고, 치명적 오류 때 ESP의 미리 만들어 둔 연속 파일(`\m1n1\hvlog.bin`, LBA를 시작 때 FAT에서 찾음)에 써서 남깁니다. 다음 부팅에 Windows 작업이나 macOS가 읽습니다. [가설: 설계안. ESP는 쓰기 허용 구역 39944960~59968629 안에 있음 — `nwoas_scripts/layout-s231/manifest.json:54`, 칸 범위 39944960~40076031]
  3. 재시작(R11): `reboot()` 전에 `nvme_flush` + `nvme_shutdown`을 부릅니다. [사실: 지금은 안 부름 `HV2/hv_psci.c:1523-1528`; `nvme_shutdown` 안의 정리 함수는 주석 처리 `HV2/nvme.c:483-486`]
  4. 종료(R12): SMC에 `MBSE` = `off1`을 써서 전원을 끕니다. Asahi Linux 드라이버가 같은 값을 씁니다(재시작은 `phra`). m1n1 C에 SMC 쓰기 함수가 있습니다. [사실: https://raw.githubusercontent.com/AsahiLinux/linux/asahi/drivers/power/reset/macsmc-reboot.c , `HV2/smc.c:123,173`. 하이퍼바이저 안에서 SMC를 써도 되는지는 가설]
  5. 개발 기록용: 하이퍼바이저 종료 포착(BUGPROBE)이 지금 ntoskrnl과 안 맞아 꺼져 있습니다. 맞춰 둡니다. [사실: `HANDOFF-2026-11-16-evening.md:246`]
- **바꿀 파일**: `HV2/hv_exc.c`, `HV2/hv_psci.c`, 새 기록 파일.
- **실기기 검증**: Windows "종료" → 전원 표시등 꺼지고 다시 켜면 정상 부팅. Windows "다시 시작"을 맥북 없이 5번 연속 → 매번 바탕화면, 시험 파일 유지. 일부러 처리 못 할 예외를 내는 시험 판 → 재시작되고 `hvlog.bin`에 기록.
- **콘치님 손**: 전원 표시등 보기 정도.
- **위험**: SMC 쓰기 순서가 틀리면 꺼지지 않거나 바로 다시 켜질 수 있음(Asahi는 `shutdown_flag`도 씀) [가설]. 기록 파일 쓰기가 Windows 디스크 사용과 겹치는 문제 [가설].
- **되돌리는 법**: ESP `boot.bin`을 4단계 판으로.
- **선행 조건**: 4단계.

### 6단계 — Windows 쪽 준비(맥북 없이 GPU·서명·업데이트)

- **할 일**
  1. GPU 다시 켜기: T1 예약 작업 설치(`agx-s248/jobs/T1-arm-task.cmd`) 또는 드라이버가 G3Armed 없이도 첫 시작에 성공하게 고치기. **콘치님 결정 필요**(Windows 설정을 바꾸는 일이라 인계 문서에 결정 사항으로 남아 있음).
  2. 서명: `bcdedit /enum {current}`에 `testsigning Yes`가 있는지 부팅마다 기록하는 시작 작업. 꺼져 있으면 GPU 드라이버가 안 뜨고 기본 화면으로 떨어집니다.
  3. Windows Update 대비 확인(읽기만): `S:\EFI\Microsoft\Boot`, `S:\EFI\Boot\bootaa64.efi`, `S:\m1n1\`가 업데이트 뒤에도 그대로인지 지문 목록을 부팅마다 남김.
- **실기기 검증**: 맥북 없이 켜서 작업 관리자에 GPU 표시(콘치님 눈). 누적 업데이트 1번, 가능하면 기능 업데이트 1번 뒤 위 기록 비교.
- **콘치님 손**: T1 결정. 업데이트 화면 지켜보기(선택).
- **위험**
  - 기능 업데이트가 BCD를 새로 만들면서 `testsigning`을 잃을 수 있다는 보고가 있습니다. [가설: 포럼 경험담뿐 — https://www.elevenforum.com/t/why-do-windows-10-11-feature-updates-recreate-the-boot-configuration-database-bcd.9542/ ]
  - UEFI 변수가 저장되지 않으므로(재부팅마다 사라짐) Windows가 만든 부팅 항목은 남지 않고, Mu는 늘 `\EFI\Boot\bootaa64.efi`로 갑니다. 그래서 업데이트가 이 파일을 갱신해도 됩니다. [사실: `AppleSiliconPkg.dsc.inc:94-96`(가상 비휘발 변수). Mu가 이 기본 파일로 간다는 것은 참조 문서 `E/documentation/INSTALL.md:203-205`. 우리 Mu에서의 동작은 가설]
  - 기능 업데이트는 여러 번 재시작합니다. 맥북 연결 모드에서는 재시작마다 실행기를 다시 띄워야 하므로, **기능 업데이트는 5단계 뒤에** 하는 것이 맞습니다.
- **되돌리는 법**: `schtasks /delete /tn "NWOAS-AgxArm" /f`, `bcdedit /set testsigning on`(맥북 연결 모드 작업 통로로).
- **선행 조건**: 4단계(시험은 5단계 뒤).

### 7단계 — Macintosh HD를 순정 macOS로 되돌리기(듀얼 부팅 완성)

- **할 일**: Macintosh HD에 남은 D4 등록을 지웁니다. 4·5단계가 실기기에서 확인된 뒤에만 합니다(그 전에는 Macintosh HD의 D4가 비상용 경로이기 때문).
- **바꿀 파일**: 없음(콘치님 손 절차만). 보조 스크립트를 만든다면 `bputil -d`로 상태만 출력.
- **콘치님 손: 필요(1번)**
  1. Macintosh HD를 기본 부팅으로: 스텁 1TR 터미널에서 `bless --setBoot --mount "/Volumes/Macintosh HD"` → `shutdown -h now`. [가설: 반대 방향(Macintosh HD 1TR에서 스텁 지정)은 11-16에 성공. 이 방향은 미확인. 대안: 시동 옵션 화면의 "항상 사용" — Apple 문서 확인 필요]
  2. 10초 → 전원 길게 → 옵션 → Macintosh HD 짝 1TR → 터미널 → `bputil -g` → 묻는 이름·암호 → `reboot`. [사실: 같은 기기에서 11-16 00:2x에 이 방법으로 D4 등록이 지워지고 macOS가 켜짐 — `stub-135/README.md:87-91`, `R3` 1장]
  3. macOS가 켜지면 Windows를 기본으로 할지 정합니다: `sudo bless --setBoot --mount /Volumes/Windows`(11-16 10:40에 macOS에서 성공 — `RESULT-20261116-morning.md` 2장) 또는 시스템 설정 > 시동 디스크.
- **실기기 검증**: 전원 길게 → Macintosh HD 선택 → macOS 로그인. 다시 Windows 선택 → 맥북 없이 바탕화면.
- **위험**: macOS에서 bless가 "Preserved restore bundle in preboot is missing"(Code=112)로 실패하는 보고(asahi-installer #404·#407). 11-16에는 성공했지만 macOS 업데이트 뒤 다시 날 수 있음 [가설]. macOS 자동 업데이트가 시스템 펌웨어를 바꿔도 스텁(13.5)은 영향이 적지만 m1n1이 보는 시스템 펌웨어 판이 바뀝니다 [가설] → 자동 업데이트 설정을 콘치님이 정할 일.
- **되돌리는 법**: Macintosh HD 1TR에서 `install-in-recovery.sh`로 D4를 다시 등록(11-15·11-16에 두 번 성공, `R3` 1장 1번).
- **선행 조건**: 4·5단계 실기기 확인.

### 8단계 — 맥북 없는 모드의 작업 통로 바꾸기(개발용)

- **할 일**
  - 맥북 연결 모드: NS2 작업 통로·파일 전달을 그대로 씁니다(결정 D6, `plan-20261115/DECISIONS.md` D6).
  - 맥북 없는 모드: NS2를 알리지 않습니다(3단계 NS1만 판). 대신 네트워크 통로를 씁니다. USB-A 유선 랜 어댑터 → Windows OpenSSH 서버 또는 작은 작업 수신기. 와이파이는 CVP 승인 뒤.
  - 파일은 네트워크 복사나 USB 메모리(USB-A 검증 통과 상태)로.
  - 맥북 없이 남긴 덤프·기록: Windows 미니덤프는 `C:\Windows\Minidump`(g252H 때 실제로 만들어짐), 하이퍼바이저 기록은 `S:\m1n1\hvlog.bin`. 다음 맥북 연결 부팅 때 작업 통로로 가져오거나 네트워크로 가져옵니다.
- **실기기 검증**: 맥북 없이 켠 뒤 맥북에서 네트워크로 `ssh`/작업 1개 실행, 결과 파일 받기.
- **콘치님 손**: 어댑터 꽂기(있으면).
- **위험**: USB-A 데이터 손상 문제가 다시 나오는지(네트워크 계획 `plan-20261115/network.md` 2.3). 인터넷이 없으면 OpenSSH 서버 설치에 설치 원본이 따로 필요할 수 있음 [가설].
- **되돌리는 법**: 맥북 연결 모드로 켬.
- **선행 조건**: 3단계(NS1만 판). 네트워크 어댑터.

### 9단계 — 소리 준비를 맥북 없이

- **할 일**: `audio_module.py`의 준비 순서를 C로 옮기거나(ADMAC·MCA·NCO 코드를 새로), 드라이버가 직접 하게 바꿉니다. 소리 드라이버 자체가 CVP를 기다리는 중이므로 그 뒤에 정합니다.
- **실기기 검증**: S256과 같은 `READY` 줄을 C가 출력, Windows `ACPI\NWAU0001` 보임.
- **콘치님 손**: 소리 듣기.
- **위험**: 앰프 안전(볼륨 상한), 9월 기록의 MCA 클러스터 번호 엇갈림(`HANDOFF-2026-11-16-evening.md:291`).
- **되돌리는 법**: 소리 없는 펌웨어 묶음.
- **선행 조건**: 4단계, CVP.

---

## 4. 다시 쓸 수 있는 코드 (windows-on-m1의 맥북 없는 경로)와 라이선스

| 파일 | 하는 일 | 우리에게 쓸 곳 | 그대로 못 쓰는 이유 |
|---|---|---|---|
| `E/m1n1_windows/src/hv_autonomous_boot_runtime.c:79-158` | 2단계 안의 묶음 표식 확인, 3초 맥북 창, 맥북이 붙으면 프록시, 아니면 자동 진입 | 2·4단계 | 표식 형식(`ASIWINGU`)과 프로필 비트는 우리 묶음(게스트 m1n1 앞부분 포함)에 맞게 다시 정해야 함 |
| `E/m1n1_windows/src/hv_autonomous_profile.c:5-56` | 디버그 프로필(조용함/UART/전체/감시) 해석, 창 길이 | 2단계 | 그대로 쓸 만함 [가설] |
| `E/m1n1_windows/src/hv_autonomous_runtime.c:159-329, 347-492` | 보조 CPU RVBAR, `/arm-io` 연결, 부트 데이터, 2단계 주소 연결, vUART, 화면, 진입 | 4단계 | J313 고정 배치·내장 화면 전용 화면 준비(`display_prepare_guest_surface`), PCIe 주소 0x690000000은 J274에서 실제 PCIe와 겹침(`port-j274/README.md` 3번) |
| `E/m1n1_windows/src/hv_bootstrap.c:5-160` | xz 풀기·CRC·체인로드 | 1·2단계(선택) | 우리는 1단계를 따로 등록하므로 필수는 아님 |
| `E/m1n1_windows/src/main.c:150-188` | 실패하면 프록시로 떨어지는 순서 | 2단계 | 그대로 쓸 만함 |
| `E/standalone_image.py`, `E/bootstrap_image.py` | 묶음 만들기·검사 | 4단계 오프라인 도구 | 표식·배치 버전 다시 정함 |
| `E/scripts/install-esp.sh` | ESP `m1n1/boot.bin` 바꾸기(원본 한 번 보관, 임시 파일 → 이름 바꾸기, 지문 확인), 되돌리기 | 2~5단계를 macOS에서 할 때 | 기존 Asahi `boot.bin`이 있어야 진행(`INSTALL.md:179-185`). 우리 ESP에는 없음. 지문은 `shasum` 사용(macOS에는 있음, 13.5 복구에는 없음) |
| `E/documentation/INSTALL.md:179-213` | 첫 부팅·되돌리기 순서 | 3장 복구 문서 | 맥북 에어 기준 |

라이선스 [사실]:
- windows-on-m1 저장소 자체: MIT(Copyright 2026 paulsmir) — `E/LICENSE`.
- 그 안의 `m1n1_windows`: MIT(The Asahi Linux Contributors) — `E/m1n1_windows/LICENSE`. 우리 `m1n1-gpu-hv2`도 같은 m1n1 계열(MIT, `N/m1n1-gpu-hv2/LICENSE`).
- Asahi Linux 커널 `macsmc-reboot.c`는 GPL 계열이므로 **코드는 가져오지 않고 키 값(MBSE/off1)만 참고**합니다. [가설: 파일 머리의 SPDX 줄은 직접 보지 않음 — 가져다 쓰기 전에 확인]
- asahi-installer `step2.sh`(MIT) 바탕인 우리 `step2.sh` 사본은 이미 출처를 적어 둠(`stub-135/tool/step2/step2.sh:2`).

참조 프로젝트의 한계 [사실: `E/documentation/LIMITATIONS.md`]: J313 한 대에서만 확인, 맥북 없는 부팅에서도 약 20초 멈춤이 남음, 컨트롤러가 SSD 전체를 노출해 결함 시 다른 칸도 손상될 수 있음. 우리는 쓰기 구역 제한이 C에 있어 이 점은 더 안전합니다(`HV2/hv_vm.c` 쓰기 구역 검사, 11-15 조사 storage 보고).

---

## 5. 모르는 점과 먼저 확인할 공개 실패 사례

### 5.1 실기기에서만 알 수 있는 것

| 번호 | 모르는 점 | 어떻게 알아보나 | 어느 단계 |
|---|---|---|---|
| U1 | 스텁 1TR에서 `kmutil configure-boot`를 두 번째로 부르면 그대로 바뀌는가(정책 다시 만들기 요구, 인터넷 필요 여부) | 1단계 실행 결과 | 1 |
| U2 | 1단계 파일 크기 한도 | D4 이하로 만들어 피함 | 1 |
| U3 | m1n1 FAT 읽기가 4 KiB 블록 ESP에서 되는가 | 1단계 검증 2번 | 1 |
| U4 | 시동 옵션 "옵션"이 정말 스텁 짝 복구로 가는가(기본 부팅 = 스텁일 때) | `go1.sh`가 `Paired`·`one true recoveryOS`를 먼저 확인, 아니면 아무것도 안 바꿈 | 1 |
| U5 | 맥북 없이 화면이 나오는가(m1n1 C 화면 + 하이퍼바이저 DCP 서비스) | 4단계 | 4 |
| U6 | NS1만 판이 켜지는가 | 3단계 | 3 |
| U7 | 1.2 표 말고 다른 맥북 문의가 Windows 실행 중에 생기는가 | 5단계 규칙이 세는 카운터, 케이블 뽑기 시험(11-15 M5) | 3·5 |
| U8 | SSD 정리 없이 재시작해도 괜찮은가 | 5단계 연속 재시작 시험 | 5 |
| U9 | 하이퍼바이저가 SMC로 전원을 꺼도 되는가 | 5단계 | 5 |
| U10 | 게스트 RAM 안 예약 영역(L-8)이 C 실행기에서도 무해한가 | 지금 Python 판과 같은 범위이므로 같은 결과일 것 [가설], 4단계 ADT 비교 | 4 |
| U11 | Windows 장치 암호화(BitLocker)가 켜져 있는가 — 부팅 경로가 바뀌면 복구 키를 물을 수 있음 | `manage-bde -status`를 작업 통로로(읽기만) | 4 전 |
| U12 | WinRE가 구성돼 있는가(`reagentc /info`) — 맥북 없을 때 Windows 쪽 복구 | 작업 통로로 읽기 | 6 |

### 5.2 콘치님 손 단계 전에 다시 볼 공개 실패 사례

| 사례 | 내용 | 우리 대응 |
|---|---|---|
| asahi-installer #441 | macOS 26에서 미완성 스텁이 막힘(M4) | 우리 스텁은 11-16에 완성·부팅 확인. 다시 만들지 않음 [사실: `RESULT-20261116-morning.md` 5장] |
| asahi-installer #404·#407 | Tahoe에서 `bless --setBoot` Code=112 | 7단계 3번 전에 다시 확인. 실패하면 시동 옵션에서 고르는 것으로 충분(`stub-135/README.md:334`) |
| asahi-installer #425·#255 | 1TR 등록 뒤 "Local policy update" 재시동 반복 → cleanbp/DFU | 1단계 실패 안내에 포함. 이 경우 콘치님께 다시 치게 하지 않고 사진만 받음 |
| 11-16 실제 실패 | 13.5 복구에 `shasum` 없음, 부팅 정책 없음, GPT 속성 때문에 노란 화면 | `go1.sh`는 `md5`·`stat`만, 정책 확인 먼저, GPT는 건드리지 않음 |
| m1n1 초기 맥북 창 조건 | 화면이 켜져 있거나 SIP 값이 127이 아니면 안 열림 | 1단계는 이 창에 기대지 않음, 2단계 창은 조건 없이 [사실: `HV2/main.c:85`] |
| 기능 업데이트와 BCD | `testsigning` 잃음 경험담 | 6단계 기록 작업 |

Asahi 쪽 이슈 번호의 현재 상태(열림/닫힘)는 이번에 다시 열어 보지 않았습니다. 1단계 직전에 `gh issue view`로 다시 확인합니다.

---

## 6. 대략 크기

| 단계 | 크기 | 콘치님 손 |
|---|---|---|
| 1 새 1단계 등록 + ESP에 D4 | 작음(코드) / 손 1번 | 스텁 1TR 1번, 약 5분 |
| 2 2단계 하이퍼바이저 + 맥북 기다리는 창 | 작음 | 없음 |
| 3 부팅 중 문의 C 처리 + NVMe 스스로 켜기 + NS1만 | 중간 | 없음 |
| 4 C 게스트 실행기 | 큼 | 맥북 빼고 켜기·화면 보기 |
| 5 맥북 없는 예외 처리·기록·종료·재시작 | 중간 | 표시등 보기 |
| 6 Windows 쪽(GPU 다시 켜기·서명·업데이트) | 작음 | T1 결정 |
| 7 Macintosh HD 되살리기 | 작음(코드 없음) / 손 1번 | 1TR 1번 |
| 8 맥북 없는 모드 작업 통로(네트워크) | 중간 | 어댑터 꽂기 |
| 9 소리 준비 C | 중간~큼 | 소리 듣기 (CVP 뒤) |
