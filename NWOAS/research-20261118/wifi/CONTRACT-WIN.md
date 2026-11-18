# 내장 Wi-Fi Windows 드라이버 계약서 (S259, 2026-11-18)

표기: **[사실]** = 코드·실기 기록으로 확인한 것. **[가설]** = 확인하지 않은 것. 이 계약의 하이퍼바이저·펌웨어 쪽은 **빌드와 가짜 하드웨어 시험만 했고 맥미니에서 한 번도 돌리지 않았습니다.** 장치가 장치 관리자에 보여도 Wi-Fi가 된다는 뜻이 아닙니다.

구현: `nwoas_scripts/wifi-s259/` (하이퍼바이저 쪽), `nwoas_scripts/fw-wifi-s259/` (UEFI·ACPI), 실행기 `nwoas_scripts/agxdcp-s259-guest-test.sh`. 값이 바뀌면 이 문서, `nwoas_wifi_s259.py`의 상수, SSDT가 함께 바뀌어야 하며 `fw-wifi-s259/build.py`와 `test_wifi_hv.py`가 어긋남을 거절합니다.

## 0. 결론

- 드라이버는 **PCI 장치가 아니라 ACPI 장치 `ACPI\NWOA4378`(`\_SB.NWC0.WLN0`)에 붙습니다.** USB-A(`\_SB.SCB0.XHC0`)와 같은 방식입니다. PCI 루트(`PCI0`)는 Windows에 계속 숨겨져 있어 pci.sys도, PCI 설정 공간 함수도 없습니다.
- 드라이버가 시작할 때 칩은 **전원이 켜져 있고, PCIe 연결이 올라와 있고, BAR 두 개가 고정 주소에 배정되어 있고, 메모리 접근과 DMA(버스 마스터)가 켜져 있으며, 펌웨어는 올라가 있지 않습니다.** 펌웨어 올리기·메시지 링·검색은 모두 드라이버가 합니다(S257 `wifi_up.py`·`wifi_scan.py`가 하던 일).
- DMA 주소 규칙은 하나입니다: **칩에 주는 주소 = Windows가 알려 주는 물리 주소(=게스트 물리 주소).** 칩 쪽 DART는 UEFI가 이미 그렇게 맞춰 두었고 드라이버는 DART를 만지지 않습니다.
- PCI 설정 공간에서 드라이버가 바꿔야 하는 것(BAR0 창 레지스터 `0x80`, 명령, DevCtl, ASPM)은 **NWOAS 제어 페이지**(자원 2)를 거쳐 하이퍼바이저가 대신 씁니다.

## 1. Windows가 보는 것

| 항목 | 값 | 근거 |
|---|---|---|
| ACPI 경로 | `\_SB.NWC0.WLN0`, 부모 `NWC0` = `ACPI0004` 컨테이너(`_UID 0x11`) | `wifi-s259/acpi/NwoasWifiJ274.asl.inc` |
| 하드웨어 ID | `ACPI\NWOA4378`, 인스턴스 `ACPI\NWOA4378\0` | 같은 파일 `_HID`, `_UID 0` |
| PCI ID (제어 페이지로 읽음) | 기능 0 `14e4:4425`, 클래스 `0x028000`, 리비전 3 (BCM4378 B1). 기능 1(블루투스 `14e4:5f69`)은 BAR 없음·꺼둠 | [사실] S240 실기 `PASS_IDS`, S257 실기 `chip id reg=0x10034378` |
| 자원 0 (메모리) | BAR0 칩 레지스터: CPU `0x6E0800000`, 길이 `0x10000` (PCI 버스 `0xE0800000`) | [사실] S257 실기 되읽기 `0x10 = 0xe0800004` |
| 자원 1 (메모리) | BAR2 칩 메모리(TCM): CPU `0x6E0000000`, 길이 `0x800000` (PCI 버스 `0xE0000000`) | [사실] S257 실기 `0x18 = 0xe0000004` |
| 자원 2 (메모리) | NWOAS 제어 페이지: `0x348000000`, 길이 `0x4000` (하이퍼바이저가 가로채는 페이지, 3장) | `nwoas_wifi_s259.py` `CTRL_IPA` |
| 인터럽트 | `Interrupt(Level, ActiveHigh, Exclusive) {695}` = apcie 포트 0 INTx 묶음 줄. vGIC SPI 695 = AIC 695 | [사실] dts 3187행, DSDT `_PRT`, `hv_vgic.c` 426행(활성화 시 AIC 해제) |
| 자원 순서 | 위 순서대로지만, **길이로 구별하십시오**(64 KiB / 8 MiB / 16 KiB) | — |
| `_CCA` | 0 (캐시 일관 아님 — USB-A와 같은 선언). Windows DMA API가 캐시 정리를 합니다 | DSDT SCB0 348행과 같음 |
| `_DMA` | 장치 주소 `[0, 0xC_0000_0000)` = CPU 주소, 변환 0 | SCB0 `_DMA` 381행과 같음 |
| IORT | Named Component `\_SB_.NWC0.WLN0`(ID 매핑 0개, 일관성 0, 주소 한계 40비트) — XHC0 노드와 같은 값 | `fw-wifi-s259/build.py` IORT 편집, 빌드 산출물에 이름 들어감 확인 |
| `_STA` | `_INI` 전 0x0F. `_INI`에서 첫 전원 켜기가 성공하면 계속 0x0F, 실패하면 0(장치 사라짐) | SSDT |
| 전원 메서드 | `_INI`·`_PS0` → 제어 페이지 명령 1(켜기), `_PS3` → 명령 2(끄기) | SSDT |

### 1.1 드라이버 시작 시 칩 상태

| 항목 | 상태 | 근거 |
|---|---|---|
| 전원 | 켜짐 (SMC `gP0d` = `0x800001`, 그 뒤 SMC 연결은 닫음 — m1n1 `dcp.c` HDMI 전원과 같은 방식) | [사실: 순서] `nwoas_wifi_s259.py` `_bring_up`. [가설] SMC 연결을 닫아도 전원이 유지됨(HDMI 선례만 있음) |
| PCIe 연결 | 올라옴 (S257 실기: Gen2 ×1) | [사실] S257 `rootport0_pcie` |
| 루트 포트 0 | 버스 0/1/1, 메모리 창 `0xE0000000~0xE08FFFFF`(`0x20` = `0xE080E000`), 명령 MEM+BME | [사실: 값] S257 실기 되읽기, 이 판의 하이퍼바이저가 같은 코드로 씀 |
| 끝점 명령 | `0x0006` (메모리 + 버스 마스터, INTx 허용) | `EP_CMD_UP` |
| DevCtl | No Snoop·Relaxed Ordering 끔(S257 실기 `0x2c10` → 끈 값) | `_bring_up` DevCtl 단계 |
| BAR0 창(`0x80`) | `0x18000000`(chipcommon) — 하이퍼바이저가 칩 번호를 읽고 그대로 둠 | `_bring_up` chip id |
| 칩 CPU(CR4)·TCM | 전원 켠 직후 상태. **펌웨어 없음, TCM 내용 정의 안 됨, CR4 멈춤 여부 모름** | [사실] 하이퍼바이저는 코어를 건드리지 않음 |
| 칩 인터럽트 마스크 | 칩 기본값(드라이버가 켜기 전까지 INTx를 내지 않을 것으로 봄) | [가설] brcmfmac 순서 기준 |

## 1.2 BAR 접근 방식 (2026-11-18 오후 수정)

- **BAR0·BAR2 접근은 모두 하이퍼바이저가 대신 합니다.** 게스트 주소 `0x6E0000000~0x6E08FFFFF`에 BYPASS 추적기(`wifi_module.py`, USB-A BAR `0x6c0000000`과 같은 방식)를 걸어, 읽기·쓰기마다 EL2로 넘어가고 C 하이퍼바이저가 같은 크기로 접근합니다(`hv_vm.c` `hv_emulate_rw_aligned` → `hv_pa_read`/`hv_pa_write`).
- 이유 [사실: 코드·ADT, 가설: SError 원인]: Windows `MmMapIoSpace`는 Device-nGnRnE로 매핑하고, 2단계 표는 게스트 속성을 그대로 둡니다(`hv_vm.c` `PTE_MEMATTR_UNCHANGED`, `HCR_EL2.FWB` 꺼짐). PCIe BAR에 nGnRnE로 쓰면 SError가 납니다(Asahi 문서, 실기 `L2C_ERR_ADR 0x6e0800120` 등). m1n1은 이 범위를 Device-nGnRE로 매핑합니다(`memory.c` `mmu_remap_ranges`, ADT `pmap-io-ranges` `0x6c0000000+1 GiB` 플래그 `0x80000027`). S257 프록시 스크립트가 같은 쓰기를 문제없이 한 이유입니다.
- **드라이버 규칙:** BAR에는 일반 레지스터 8·16·32·64비트 읽기·쓰기만 씁니다(`READ/WRITE_REGISTER_UCHAR/USHORT/ULONG/ULONG64`, `_BUFFER_` 판은 같은 크기의 단순 반복). **`memcpy`·`RtlCopyMemory`·`memset`·NEON·`LDP/STP`(q 레지스터)·비정렬 접근을 BAR에 쓰지 마십시오.** 하이퍼바이저 에뮬레이터가 해석하지 못하는 명령은 하이퍼바이저 예외로 끝납니다(지원 목록: `hv_vm.c` `emulate_load`/`emulate_store`). 접근은 크기에 맞게 자연 정렬합니다(한 번의 접근이 16 KiB 페이지 경계를 넘으면 하이퍼바이저 assert, `hv_vm.c` 3842행).
- 비용 [가설: 값]: 접근 한 번마다 EL2 왕복(수 µs). 펌웨어 1.5 MB를 32비트로 쓰면 약 37만 번, 64비트면 약 19만 번 → 1초 안팎. 링 위치·도어벨 접근도 한 번씩 왕복합니다. 링 본체는 DMA(게스트 RAM)라 영향 없습니다. 이 방식은 맥북 Python을 거치지 않습니다(C 하이퍼바이저만).
- 2단계에서 nGnRE로 바꾸는 길(FEAT_S2FWB)은 쓰지 않았습니다: FWB가 꺼진 지금은 2단계가 게스트 속성보다 느슨하게 만들 수 없고, FWB를 켜면 모든 게스트 메모리 속성 해석이 바뀌어 USB-A·GPU에 영향이 갑니다.

## 2. DMA 규칙

| 규칙 | 내용 |
|---|---|
| 주소 | 칩 링·버퍼 주소 = Windows가 준 논리 주소(WDF 공통 버퍼 `LogicalAddress`, `_TRA 0`이라 물리 주소와 같음). 64비트 주소를 그대로 씁니다 |
| 왜 되는가 [사실: 코드] | UEFI `SetupApcieDarts`가 apcie DART 3개(포트 0 `0x681008000` 포함)의 스트림 16개를 모두 같은 L1 표로 가리킴(`AppleDartIoMmuDxe.c` 435·737·740행), ExitBootServices 처리기가 그 표를 채움: IOVA `[0, 4 GiB)` → 게스트 낮은 창의 실제 메모리(1143행), 높은 RAM은 IOVA = 물리 주소(1166행). 실기 기록 `WIDE-DART identity [0x83BED8000..0xAE0FCC000] ptes=693309`. USB-A가 이 표로 동작 중 |
| 하이퍼바이저 확인 | 버스 마스터를 켜기 전에 포트 0 DART의 TTBR·TCR이 포트 1(USB-A) DART와 같은지 비교. 다르면 포트 1 값을 그대로 복사(제어 페이지 `DART_CHECK` = 1 같음 / 2 복사함). 포트 1 DART에는 쓰지 않음 |
| 권장 | **버퍼는 4 GiB 위에 두십시오**(`PHYSICAL_ADDRESS` 하한 `0x1_0000_0000`인 공통 버퍼). 낮은 창에는 PCIe 다리 창·MSI 주소가 겹치는 구간이 있습니다 |
| 금지 구간 | 버스 주소 `0xC0000000~0xC00FFFFF`(USB-A 다리 창, S237 예약), `0xE0000000~0xE0FFFFFF`(Wi-Fi 다리 창, S259 예약), `0xFFFFF000` 페이지(apcie MSI 주소 — 예약되지 않음 [가설: 여기에 쓰면 MSI로 먹힘]). 앞의 둘은 UEFI가 메모리 지도에서 빼므로 Windows가 내주지 않습니다 |
| 캐시 | `_CCA 0`이므로 WDF 공통 버퍼는 캐시 안 됨으로 잡힙니다. 패킷 버퍼(스캐터/개더)는 WDF DMA 트랜잭션을 쓰거나 `KeFlushIoBuffers`로 정리. 칩이 캐시 일관(DT `dma-coherent`)이어도 해가 없음 |
| 격리 없음 | DART 표가 게스트 RAM 전체를 열어 둡니다. **잘못된 주소를 칩에 주면 Windows나 USB-A 메모리를 덮습니다.** 드라이버가 주소를 검사하십시오 |

## 3. NWOAS 제어 페이지 (자원 2, 32비트 읽기·쓰기만)

모든 접근이 하이퍼바이저로 넘어가 맥북 Python이 처리하므로 한 번에 수 ms 걸립니다 [가설: 값]. 자주 읽는 경로에 두지 마십시오.

| 오프셋 | 이름 | R/W | 뜻 |
|---|---|---|---|
| `0x00` | MAGIC | R | `0x4657574E` ('NWWF'). 다르면 S259 실행기가 아님 → 드라이버 시작 거절 |
| `0x04` | VERSION | R | 1 |
| `0x08` | STATE | R | 0 꺼짐(처음), **1 켜짐(드라이버 사용 가능)**, 2 요청으로 꺼짐, 3 실패 |
| `0x0C` | ERROR | R | 0 없음. 그 밖은 `nwoas_wifi_s259.py` `ERRORS` (예 8 `LINK_DOWN`, 14 `DART_MISMATCH`, 1 `DISABLED`) |
| `0x10` | CMD | W | **ACPI 전용**(1 켜기, 2 끄기). 드라이버는 쓰지 마십시오 |
| `0x14` | SEQ | R | 끝난 명령 수 |
| `0x18` | AVAIL | R | 1 = 첫 켜기 성공(`_STA`가 씀) |
| `0x1C` | IDS | R | 마지막 켜기 때 읽은 `0x442514E4` |
| `0x20` | CFG_OFF | R/W | 기능 0 설정 공간 오프셋(4바이트 정렬, `< 0x1000`) |
| `0x24` | CFG_DATA | R/W | 읽기 = 그 오프셋의 32비트 값(켜짐이 아니면 `0xFFFFFFFF`). 쓰기 = 아래 허용 표대로만 |
| `0x28` | CFG_ERR | R | 0 성공, 1 켜짐 아님, 2 허용 안 됨, 3 오프셋 틀림, 4 예외 |
| `0x2C`/`0x30` | BAR0/BAR2 | R | 켜짐일 때 버스 주소 `0xE0800000` / `0xE0000000`, 아니면 0 |
| `0x34` | LINKSTS | R | 켤 때 읽은 포트 0 연결 상태 |
| `0x38` | DEVCTL | R | 현재 DevCtl(하이퍼바이저가 쓴 값) |
| `0x3C` | EPCMD | R | 현재 끝점 명령 |
| `0x40` | DART_TTBR | R | 포트 0·1 DART가 함께 가리키는 TTBR 값 |
| `0x44` | DART_CHECK | R | 1 같았음, 2 복사해서 맞춤 |
| `0x48` | CHIPID | R | BAR0로 읽은 칩 번호 레지스터(`0x10034378` 기대) |
| `0x4C` | BAR0WIN | R/W | 설정 공간 `0x80`(BAR0 창) 바로 읽기·쓰기 — 한 번의 접근으로 창을 바꿈 |
| `0x50` | CMD_COUNT | R | 받은 명령 수 |
| `0x60`/`0x64` | MAC_LO/MAC_HI | R | ADT `/arm-io/wlan` `local-mac-address` 6바이트(작은 끝부터, HI는 하위 2바이트). 상태와 무관 |
| `0x68` | CAL_LEN | R | ADT `wifi-calibration-msf` 길이(실기 ADT 2,096) |
| `0x6C` | ANTENNA | R | 안테나 종류 ASCII 4바이트(`"X3"` + 0) |
| `0x70`~`0x7C` | BOARD | R | 보드 이름 ASCII 16바이트(`"atlantisb"` + 0) |
| `0x1000`~`0x1FFF` | CAL | R | 보정 데이터 바이트(CAL_LEN까지, 나머지 0). `calload`에 그대로 보냄 |

설정 공간 쓰기 허용 표 (그 밖은 거절, CFG_ERR 2):

| 오프셋 | 허용 | 하이퍼바이저가 바꾸는 것 |
|---|---|---|
| `0x80` (BAR0 창) | 4 KiB 정렬 값만 | 없음 |
| `0x04` (명령) | BME(bit 2), INTx 끔(bit 10) | 메모리 접근(bit 1)은 항상 켬, 상태 절반은 0으로 씀 |
| DevCtl (`cap+8`, S257 실기 `0xB4`) | 하위 16비트 | No Snoop·Relaxed Ordering은 항상 끔 |
| `0xBC` (LinkCtl) | ASPM 두 비트만 | 나머지는 현재 값 유지 |

## 4. 인터럽트

- [사실: 코드] 장치 인터럽트는 하이퍼바이저 일반 경로로 들어갑니다: AIC가 올리면 그 줄을 막고 게스트에 넣고(`hv_exc.c` 2257행), 게스트가 EOI하면 다시 엽니다. Windows가 SPI 695를 켜면 AIC 695를 엽니다(`hv_vgic.c` 426행).
- [사실: Linux `pcie-apple.c`] 포트 INTx는 **수준형**이고 포트 상태 레지스터를 따로 지우지 않습니다. 칩이 INTx를 내리면 줄도 내려갑니다. 따라서 **ISR이 칩의 메일박스 인터럽트 상태(PCIe2 코어 `MAILBOXINT`, 64비트 판 `0xC30`)를 지워야 EOI 뒤에 다시 들어오지 않습니다.**
- [사실: 코드] UEFI가 포트 0 INTx 마스크를 풀어 둡니다(`AppleSiliconPciPlatformDxe.c` 367행, 연결 실패해도 실행됨). 하이퍼바이저가 켤 때 다시 확인합니다.
- [가설] USB-A(698)는 이 일반 경로만으로 되지 않아 하이퍼바이저가 대신 넣는 코드가 있습니다(`hv_exc.c` 675행~). 695도 그럴 수 있습니다. **처음에는 폴링(타이머로 링 위치 확인)으로 시작하고, 인터럽트는 두 번째 단계로 켜십시오.**
- MSI는 없습니다(ITS 없음). MSI 능력을 켜지 마십시오.

## 5. 전원

| 상황 | 일어나는 일 |
|---|---|
| 부팅 (Windows ACPI 초기화) | `_INI` → 하이퍼바이저가 켬(1~2초 정도 그 CPU가 멈춤 [가설: 시간]). 실패하면 `_STA`=0으로 장치가 안 보임 |
| 장치 D0 진입 | `_PS0` → 이미 켜져 있으면 아무것도 안 함, 꺼져 있으면 처음부터 다시 켬(PERST#부터. **칩 상태 전부 초기화, 펌웨어 다시 올려야 함**) |
| 장치 D3 / 사용 안 함 / 종료·재시작 | `_PS3` → 버스 마스터 끔 → PERST# → 전원 끔. [가설] Windows가 종료·재시작 때 `_PS3`를 부름 |
| Windows 재시작·종료 자체 | 하이퍼바이저 PSCI가 m1n1 `reboot()`(`hv_psci.c` 1523~1541행) → 맥미니 SoC 재부팅. SMC GPIO가 유지되는지 모름 [가설] → 다음 실행기가 UEFI 전에 **무조건 끔**(`boot_force_off`) |
| 드라이버가 칩을 되살리고 싶을 때 | 장치를 끄고 켜기(D3 → D0) = 전원 순환. 칩 감시 타이머 리셋은 S257 실기에서 BAR가 남았음(`config_lost.watchdog = False`) |

## 6. 드라이버가 할 일 / 하면 안 되는 일

해야 할 일:
1. MAGIC·VERSION·STATE(=1)·BAR0/BAR2 값을 먼저 확인하고, 다르면 **BAR를 건드리지 말고** 시작 실패로 끝냅니다(연결이 내려간 BAR를 읽으면 버스 오류가 날 수 있음 [가설: USB-A BAR에서 EL2 데이터 중단 사례]).
2. BAR0·BAR2를 `MmMapIoSpaceEx(PAGE_READWRITE | PAGE_NOCACHE)`로 매핑합니다.
3. 칩 쪽 순서는 S257 `wifi_up.py`·`wifi_scan.py`(OpenBSD bwfm 순서)를 따릅니다: 칩 번호·EROM → CR4 정지 → 감시 타이머 리셋 → TCM 크기·OTP → 펌웨어·NVRAM·무작위 시드 → CR4 시작 → 공유 영역 → 링·`host_cap` → CLM·TxCap·보정 데이터 → `UP`. BAR0 창 바꾸기는 제어 페이지 `BAR0WIN`(0x4C)으로 합니다.
4. ADT 값(MAC, 보정 데이터, 안테나 `X3`, 보드 `atlantisb`)은 제어 페이지 `0x60`~`0x7C`·`0x1000`에서 읽습니다(실행기가 부팅 때 ADT에서 읽어 둠; MAC은 기록에 남기지 않음). 펌웨어 파일(`C-4378__s-B1/`의 `.trx`·NVRAM·`.clmb`·`.txcb`)은 드라이버 쪽에서 구합니다. Apple 파일이라 배포하지 않습니다. NVRAM 파일은 OTP의 `M`·`V`·`m` 값으로 고릅니다(S257 실기에서 쓴 방식).
5. DMA 버퍼는 4 GiB 위, 버퍼 끝을 넘는 DMA가 없게 크기를 정확히.
6. 종료(`EvtDeviceD0Exit`) 때 칩 DMA를 멈추고(링 정지, 필요하면 `CFG 0x04`에서 BME 끔) 그 다음 전원이 꺼진다고 가정합니다.

하면 안 되는 일:
- DART(`0x681008000`·`0x682008000`), apcie 포트 레지스터(`0x681000000`대), GPIO, SMC, ECAM(`0x690000000`대)을 직접 매핑하지 않습니다. USB-A가 같은 표·같은 컨트롤러를 씁니다.
- 제어 페이지 `CMD`(0x10)에 쓰지 않습니다(ACPI 전용).
- 블루투스 기능(1번)·MSI·No Snoop·Relaxed Ordering을 켜지 않습니다.
- `0xFFFFF000` 페이지와 4 GiB 아래 다리 창 주소를 칩에 주지 않습니다.
- 칩이 쓰는 버퍼를 해제하기 전에 칩 DMA를 멈춥니다(격리가 없으므로 해제 뒤 DMA는 다른 메모리를 덮음).

## 7. 모르는 점

| 항목 | 언제 알 수 있나 |
|---|---|
| 게스트 실행 중(ACPI `_INI`) 맥북 Python이 SMC를 다시 열어 써도 되는가 | S259 첫 실기, `[s259] READY` 또는 `FAIL SMC_*` |
| UEFI가 연결 실패로 남긴 포트 0 상태에서 S240 순서로 연결이 다시 올라오는가 | 같은 실기, `FAIL LINK_DOWN` 여부 |
| SMC 연결을 닫은 뒤에도 `gP0d` 전원이 유지되는가 | 같은 실기 + W1 작업의 장치 상태 |
| 인터럽트 695가 일반 경로로 들어오는가 | 드라이버가 인터럽트를 켠 뒤 |
| Windows가 종료·재시작 때 `_PS3`를 부르는가 | 하이퍼바이저 기록의 `CMD 0x2` 줄 |
| Windows가 드라이버 없는 장치를 D3로 내리는가(그러면 W1에서 STATE 2) | 같은 실기 |
