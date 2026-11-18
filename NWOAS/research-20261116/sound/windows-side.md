# 맥미니 내장 스피커 소리 — Windows·NWOAS 연결 쪽 조사

작성: 2026-11-16. 읽기 전용 조사입니다. 맥미니에 접속하지 않았고 가상머신 빌드도 돌리지 않았습니다.
하드웨어 레지스터(MCA·ADMAC·TAS5770L 설정값)는 다른 조사 담당이며, 여기서는 "Windows에 어떻게 보이고, 데이터가 어떻게 흘러가고, 무엇으로 만들고 어떻게 설치하는가"만 다룹니다.
표기: [사실] = 파일·로그로 확인, [가설] = 확인 안 된 추정.

## 결론

1. **Windows는 지금도 소리 장치 레지스터에 그대로 닿을 수 있습니다.** 하이퍼바이저(Python 쪽)가 `/arm-io` 범위 `0x200000000..0x300000000`을 통째로 통과 매핑하고, 소리 관련 블록(I2C1·DART-sio·SIO·ADMAC·MCA·PMGR·GPIO)은 모두 이 안에 있습니다. 가로채는 것은 UART0와 USB 쪽 PMGR 몇 칸뿐입니다 [사실].
2. **Windows가 장치를 알게 하려면 Mu 펌웨어를 다시 빌드해야 합니다.** ACPI 표는 Mu 펌웨어 안에 들어 있고, 펌웨어는 부팅마다 맥북에서 USB로 실려 갑니다. 하이퍼바이저가 ACPI를 고쳐 넣는 경로는 없습니다 [사실]. GPU 때처럼 새 SSDT 파일 하나를 S247 트리 사본에 더하는 방식이 그대로 쓰입니다.
3. **DMA는 "하이퍼바이저(런처)가 DART-sio의 2번 통로와 고정 버퍼를 맡고, Windows 드라이버는 그 버퍼에 복사만 하는" 방식이 가장 작고 안전합니다** [가설, 근거는 3절]. Windows 드라이버가 DART를 직접 다루는 방식도 가능은 하지만, Windows가 주는 물리 주소가 4 GiB 아래에서는 실제 주소와 다르다는 함정이 있습니다 [사실].
4. **드라이버 틀은 Microsoft `audio/simpleaudiosample`(PortCls WaveRT)가 가장 가깝습니다.** 스피커 하나·48 kHz·16비트·스테레오 고정이고, 1 ms 타이머가 버퍼를 읽어 파일로 쓰는 자리가 이미 있어서, 그 자리를 "DMA 고리 버퍼로 복사"로 바꾸면 됩니다 [사실: 샘플 코드]. ACX(KMDF) 샘플도 ARM64 빌드 설정이 있지만, 첫 소리에는 이점이 적습니다.
5. **빌드·서명·설치는 GPU 드라이버 경로를 그대로 재사용할 수 있습니다.** 같은 Parallels 가상머신(WDK 10.0.26100), 같은 SYSTEM 테스트 인증서(이미 맥미니 Windows가 신뢰), 같은 파일 전달(s243)과 `pnputil /add-driver ... /install` 작업 파일입니다. 새로 필요한 것은 런처 사본(새 펌웨어 해시) 하나와 작업 파일 두 개입니다.
6. **첫 단계는 Windows 없이 m1n1에서 소리가 나는지 보는 것**입니다. 업스트림에 맥미니용 `speaker_amp.py`가 있지만 지금 트리에서는 `DARTRegs`를 가져오지 못해 그대로는 안 돌아갑니다 [사실]. 한 줄 고쳐야 합니다.

---

## 1. 지금 Windows에 장치가 어떻게 보이는가

### 1.1 ACPI 표의 위치와 내용

| 항목 | 내용 | 근거 |
|---|---|---|
| 원본 트리 DSDT | `apple_silicon_platforms_mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl` (567줄). 장치: NVP0(소프트웨어 NVMe), XHC1(숨김 아님, 857), DISP(APPL0100), COM0(APPL8900), PCI0(숨김), SCB0/XHC0(FL1100 USB-A, 인터럽트 698), CPU 8개 | DSDT.asl:106, 136, 179, 188, 234, 345, 396 [사실] |
| 소리 장치 | **없음.** DSDT·FADT·IORT·DBG2·MCFG 어디에도 I2C·MCA·ADMAC 장치가 없음 | 같은 폴더 전체 grep [사실] |
| 지금 쓰는 펌웨어 | `fw-agx-s247-mu` (S237 + AGX0 SSDT). DSDT는 원본과 바이트가 같고, `DeviceAcpiTables.inf`에 `J274AppleAgxAbiAdmissionSsdt.asl` 한 줄만 더해짐 | `diff` 결과, `fw-agx-s247-mu/.../DeviceAcpiTables.inf:20` [사실] |
| 지금 런처 | `nwoas_scripts/agxdcp-s253-guest-test.sh`: HV `m1n1-g253K-agx-hv2-scaled.bin`, 펌웨어 `fw-agx-s247/out/s247/m1n1-payload-s247-agx.bin`(해시 고정 `fb2011f9…`) | 같은 파일 :24, :34, :55 [사실] |

### 1.2 GPU 장치(AGX0)를 넣은 방법 = 소리 장치도 같은 방법

`port-j274/gpu/acpi-j274/README.md`와 `nwoas_scripts/fw-agx-s247/README.md`의 절차 [사실]:

1. `fw-s237-mu`를 새 폴더로 APFS 복제(운영 트리는 건드리지 않음).
2. `.asl.inc` + 이를 감싸는 SSDT `.asl`을 `Platform/MacMini2020Pkg/AcpiTables/`에 복사.
3. `DeviceAcpiTables.inf`의 `[Sources]`에 한 줄 추가.
4. 필요하면 `MacMini2020.dsc`에 전처리 정의 추가.
5. `nwoas_scripts/fw-agx-s247/build.py`(fw-s237/build.py 복사본)로 빌드 → 새 페이로드(33669120바이트) 해시를 새 런처에 고정.

AGX0의 모양(`J274AppleAgxAbiAdmission.asl.inc`): `_HID "APPL0002"`, `_CCA One`, `_CRS`에 QWordMemory 4~5개 + Interrupt 1개, 하이퍼바이저 중개 페이지(0x300000000)를 OperationRegion으로 읽어 자원을 고름. 소리 장치는 이보다 훨씬 단순해도 됩니다(아래 5절).

### 1.3 ACPI 변경이 맥미니에 닿는 경로

| 질문 | 답 | 근거 |
|---|---|---|
| Mu를 다시 빌드해야 하나 | 예. ACPI AML은 Mu FD 안에 들어 있음 | fw-agx-s247/README.md "FD 안에 … 문자열" [사실] |
| 하이퍼바이저가 부팅 때 고치나 | 아니오. 런처가 싣는 Python 모듈 중 ACPI를 바꾸는 것은 없음(`layout-s231/guest_module.py`는 boot_args의 메모리 크기만 고침) | guest_module.py:41-53, 런처 :113-124 모듈 목록 grep [사실] |
| 어떻게 전달되나 | 부팅마다 `run_guest.py`가 맥북에서 USB로 페이로드(guest m1n1 + DTB + Mu FD)를 실어 보냄. 디스크에 설치하는 것이 아님 | agxdcp-s253-guest-test.sh:113-124 [사실] |
| 되돌리기 | 이전 페이로드를 쓰는 이전 런처로 부팅하면 장치 자체가 사라짐(=드라이버가 절대 올라오지 않음) | GPU 때 같은 방식, driver/BUILD-ATTEMPT-2-20261115.md:160 [사실] |
| 다른 방법 | Windows의 레지스트리 ACPI 표 덮어쓰기(`asl.exe /loadtable`, 테스트 서명 필요)도 있으나 ARM64용 `asl.exe` 유무·동작은 확인 못 함 [가설]. 이미 검증된 Mu 빌드 경로가 있으므로 쓸 이유가 적음 | — |

### 1.4 게스트(Windows)가 소리 블록 MMIO에 닿을 수 있는가

**닿습니다.** Python 하이퍼바이저가 ADT `/arm-io`의 모든 범위를 "HW"(통과)로 매핑합니다:
`m1n1_windows/proxyclient/m1n1/hv/__init__.py:1886-1889` [사실]. 실제 로그(`nwoas_scripts/logs/agxmed1g-s249-20261116-200058.Gp1CuN`)에 `Mapping MMIO range: 0x200000000 .. 0x300000000`이 찍혀 있습니다 [사실].

예외로 가로채는 것: UART0 가상화, `map_essential`의 PMGR 훅(UART0·ATC USB 전원 칸만, :1966-2026), PCIe·NVMe 에뮬레이션 범위(0x6c0000000, 0x681000000, 0x700000000), GPU 중개(0x204000000 GPU 영역 일부, 0x300000000 중개 페이지), C 하이퍼바이저의 UART TX 엿보기(`m1n1-gpu-hv2/src/hv_vm.c:4652`). C 하이퍼바이저 소스에서 0x235/0x236/0x238/0x23b/0x23c/0x23d 주소를 가로채는 코드는 UART 한 곳뿐입니다(grep) [사실].

ADT(`research-20261116/sound/adt-audio-nodes.txt`)의 실제 주소 [사실, 리틀엔디언 해독. 0x00 바이트는 덤프에서 `|`로 찍힘]:

| 블록 | ADT 노드 | 실제 주소(크기) | AIC 인터럽트 | 비고 |
|---|---|---|---|---|
| I2C1 | `/arm-io/i2c1` | **0x235014000** (0x4000) | 0x274 = 628 | 요청서의 0x2_3510_4000은 오타로 보임. Asahi `i2c@235014000`과 일치 |
| 스피커 앰프 | `/arm-io/i2c1/audio-tas5770L-speaker` | I2C 주소 0x31 | GPIO 182(0xb6), 부모 phandle 0x66 | `function-reset` = GPIO 181(0xb5) |
| DART-sio | `/arm-io/dart-sio` | 0x235004000 (0x4000) [덤프 글자 해석, 가설에 가까운 사실] | 0x27b = 635 | `dart,t8020`, 매퍼: sio=0, aes=1, **admac=2** |
| SIO(보조 프로세서) | `/arm-io/sio` | 0x236400000 (0x6c000), 0x236050000 (0x4000) | 0x281~0x283 | `pre-loaded = 1`, DART-sio 0번 통로를 씀 |
| ADMAC | `/arm-io/admac-sio` | **0x238200000** (0x34000), 0x23d2a8000 (0x8) | 0x272 = 626 | `iommu-parent` = mapper-admac(2번 통로), `#dma-channels` = 12(쌍) |
| MCA 스위치 | `/arm-io/mca-switch` | 0x238400000 (0x18000), 0x238300000 (0x30000), 0x23b0400d4 (0x18), 0x238500000 (0x4000) | — | 클러스터 6개 |
| MCA 클러스터 | `/arm-io/mca1` | 0x238404000, 0x238308000, 0x23830c000 (각 0x4000) | 0x26c = 620 | 하위 `mca1a/audio-tas5770L-speaker` |
| GPIO(AP) | (pinctrl_ap) | 181번 핀 = 0x23c1002d4 | — | `speaker_amp.py:104-105`가 바로 이 주소를 씀 |

확인이 필요한 점(하드웨어 담당에게 넘김): ADT의 `mca1`은 클러스터 1(0x238404000)인데, 업스트림 `speaker_amp.py:54`는 `cl_no = 0`(0x238400000)을 씁니다. 어느 쪽이 맞는지는 이 조사로 가리지 못했습니다.

### 1.5 인터럽트

C 하이퍼바이저는 게스트가 vGIC에서 켠 AIC 인터럽트를 **"게스트 번호 = AIC 번호"** 그대로 넘깁니다(`m1n1-gpu-hv2/src/nwoas_agx_broker.c:1092-1098` 주석, `port-j274/gpu/hv/README-hv2.md` 표의 인터럽트 줄) [사실]. 따라서 ACPI에 `Interrupt { 626 }`(ADMAC)처럼 적으면 별도 경로표 없이 들어옵니다 [가설: GPU 9개 줄과 같은 경로이지만 소리 번호로는 실기기 확인 안 함].
11-16 19시대에 고친 `hv_vgic3_get_priority()` 버그(모든 하드웨어 인터럽트 전달 경로에 있었음)는 g252J 이후 판에서 고쳐졌으므로, 소리 시험은 g253K 이후 하이퍼바이저로 해야 합니다 [사실: 상태 메모리].
첫 소리는 인터럽트 없이(타이머로) 할 수 있으므로 인터럽트는 뒤 단계로 미룹니다(5절).

### 1.6 전원(PMGR)·클록

- Windows에는 PMGR 드라이버가 없습니다. Mu에도 T8103 소리 블록을 켜는 코드가 없습니다(`fw-agx-s247-mu/Silicon/Apple` grep에서 admac·dart-sio·0x238 없음) [사실].
- guest m1n1은 MCA 클록 먹스(`clk_set_mca_muxes`, mca-switch reg[2] 0x23b0400d4)를 **Linux 부팅 경로(`kboot.c:2818`)에서만** 설정합니다. Windows 경로에서는 설정되지 않습니다 [사실].
- 그래서 전원·클록은 (가) 런처의 Python 모듈이 부팅 전에 켜 두거나(`p.pmgr_adt_clocks_enable(...)`, `speaker_amp.py:37-47`과 같은 일), (나) Windows 드라이버가 PMGR 레지스터를 직접 써야 합니다. PMGR 칸은 통과 매핑이라 (나)도 가능합니다. 첫 단계는 (가)가 작고 안전합니다 [가설].

---

## 2. 다른 장치들이 DMA를 어떻게 처리하는가

### 2.1 게스트 주소와 실제 주소의 관계 [사실]

`nwoas_scripts/layout-s231/guest_module.py:37-74`(`GuestMemory.pa`)와 `usb-s174/guest_map.py:11-15`:

| 게스트(Windows) 물리 주소 | 실제 주소 | 비고 |
|---|---|---|
| 0 .. 4 GiB (저주소 창) | `LOW_BACKING + 주소`, LOW_BACKING = RAM 끝 − 4 GiB − 0x34000 | 0x34000 어긋남("skew") 포함. 이 4 GiB는 Windows의 일반 RAM으로 쓰임 |
| phys_base .. LOW_BACKING (고주소) | **같은 주소(1:1)** | 런처 주석 "identity guest RAM, region-id-24"(agxdcp-s253:27) |

그러므로 **Windows 드라이버가 4 GiB 아래 버퍼의 물리 주소를 장치에 그대로 주면 틀린 곳으로 DMA가 갑니다.** SimpleAudioSample은 버퍼를 `AllocatePagesForMdl(highAddress = 0xFFFFFFFF)`로 4 GiB 아래에 잡습니다(`minwavertstream.cpp:501-503, 737-739`) [사실]. 직접 DMA를 하려면 이 상한을 풀고 하한을 고주소로 주거나(`AllocateContiguousPagesForMdl`), 주소를 변환해야 합니다.

### 2.2 장치별 처리 방식 [사실]

| 장치 | DART 주인 | 주소 처리 | 근거 |
|---|---|---|---|
| NVMe | 진짜 장치를 게스트에 안 보임. Python이 소프트웨어 NVMe를 흉내 내고, 게스트 PRP 주소를 `GuestMemory.pa`로 바꿔 직접 복사 | IPA→PA를 호스트가 계산 | guest_module.py:1-3, 62-74 |
| FL1100 USB-A (xHCI) | 하이퍼바이저가 apcie DART를 1:1로 유지("identity-map maintainer"), Windows가 DART를 다시 초기화하면 다시 씀 | ACPI `_DMA`로 Windows가 고주소 RAM에만 DMA 버퍼를 잡게 함(48 GiB 창, `_TRA=0`) | hv_vm.c:2463-2480, DSDT.asl:345-395 |
| GPU | 하이퍼바이저가 GPU·화면 DART를 맡음(`svc_map`/`svc_unmap`). 드라이버는 중개 페이지·하이퍼콜로 요청 | 하이퍼콜 `HVC #0x4d31`로 게스트 주소 64쪽씩 실제 주소로 변환. 단 GPU 중개 모드(`NWOAS_AGX_MODE=mediate`)일 때만 응답 | hv_guest_ipa_pa.h:20-43, nwoas_agx_broker.c:1040-1088, hv_exc.c:1888-1900 |

### 2.3 ADMAC(DART-sio 뒤)에 대한 선택지

| 방식 | 하는 일 | 장점 | 문제 |
|---|---|---|---|
| **A. 런처가 고정 버퍼를 맡음 (추천)** | 런처 Python 모듈이 (1) 소리 블록 전원 켜기, (2) 프록시 힙(게스트 RAM 밖)에 버퍼 할당, (3) DART-sio **2번 통로만** 그 버퍼로 매핑, (4) 같은 버퍼를 게스트의 고정 주소(IPA)에 `hv.map_hw`로 붙임(`usb-s174/guest_map.py:15`와 같은 방식). ACPI `_CRS`에 그 IPA를 메모리 자원으로 적음. Windows 드라이버는 그 창에 소리 데이터를 복사하고 ADMAC 기술자에 **고정 IOVA**만 씀 | Windows가 DART·주소 변환을 전혀 몰라도 됨. 버퍼가 게스트 RAM 밖이라 Windows가 다른 용도로 쓸 수 없음. 드라이버는 이 창을 캐시 없이 매핑(MmMapIoSpaceEx)하므로 캐시 비우기도 필요 없음. 하이퍼바이저 C 코드·Mu 메모리 예약 변경 없음 | 런처 모듈 하나(해시 고정) 추가. 고정 IPA는 `0x300000000..0x380000000` 빈틈(중개 페이지 0x300000000 제외) 안에서 골라야 함, 예: 0x301000000 [가설: 겹침 검사 필요]. 복사 한 번(48 kHz·2채널·16비트 = 초당 192 KB라 부담 없음) |
| B. Windows 드라이버가 DART-sio를 직접 프로그램 | 드라이버가 2번 통로 페이지 표를 만들고 TTBR/TCR을 씀 | 일반 리눅스 방식과 같음 | 페이지 표·버퍼의 실제 주소가 필요 → 고주소 RAM에서만 할당하거나 `HVC #0x4d31`(GPU 중개 모드 의존)을 써야 함. DART-sio는 SIO 보조 프로세서(0번)·AES(1번)와 같이 쓰므로 전역 레지스터·전체 무효화를 건드리면 안 됨. m1n1 `DART.initialize()`는 **모든 통로를 초기화**함(`m1n1/hw/dart8020.py:261-275`) — 이것을 그대로 옮기면 위험 [가설] |
| C. DART 우회(bypass) | 2번 통로 TCR을 BYPASS로 두고 ADMAC이 실제 주소를 바로 씀 | 페이지 표 없음 | ADT에 `bypass`, `bypass-address` 속성이 있으나 뜻을 확정 못 함. ADMAC이 낼 수 있는 주소 폭도 모름. 하드웨어 담당 확인 전에는 쓰지 않음 [가설] |

A를 추천하는 이유: GPU에서 이미 "하이퍼바이저가 DART를 맡고 Windows는 요청만 한다"는 구조가 통했고, NVMe·USB도 하이퍼바이저/호스트 쪽이 주소를 책임집니다. Windows에는 DART 드라이버가 없다는 것도 PCI0 주석에 적혀 있습니다(DSDT.asl:226-232) [사실].

---

## 3. Windows 소리 드라이버 방식 비교

대상 Windows: 11 Pro 25H2, ARM64 [사실: 상태 메모리]. 빌드 도구: VS 2022 + WDK/SDK 10.0.26100 [사실: `/Volumes/media/nwoas/setup/install-all.cmd`, GPU `build-driver.ps1:152-159`].

| 항목 | (a) PortCls WaveRT — `audio/simpleaudiosample` | (b) ACX — `audio/Acx/Samples/AudioCodec` |
|---|---|---|
| 모델 | WDM + PortCls, C++ | KMDF + ACX 1.1, C++ |
| 끝점 | 스피커 1개 + 마이크 배열 1개(마이크는 빼면 됨) | 렌더·캡처 회로 |
| 형식 | 스피커 48000 Hz 고정, 16비트 고정, 최대 2채널 (`speakerwavtable.h:19-25`) | 형식 목록을 드라이버가 정함 |
| 하드웨어 흉내 | 1 ms `ExSetTimer` 타이머가 시간으로 위치를 계산하고 `ReadBytes()`가 버퍼를 읽어 파일로 저장 (`minwavertstream.cpp:1261-1268, 1302-1449`) | `Common/StreamEngine.cpp`가 비슷하게 흉내 |
| 고칠 곳 | `ReadBytes()`의 `m_SaveData.WriteData(...)`를 "16비트 스테레오 → 앰프 형식으로 바꿔 DMA 창에 복사 + ADMAC 기술자 보충"으로 교체. `StartDevice`에서 `ResourceList`의 메모리 자원을 매핑(지금은 `UNREFERENCED_PARAMETER`, `adapter.cpp:704`) | 같은 일을 `StreamEngine`에서 |
| ARM64 빌드 설정 | 있음 (`Main.vcxproj:4-10`) | 있음 (`AudioCodec.vcxproj:12-18`) |
| 설치 ID | `ROOT\SimpleAudioSample` (`SimpleAudioSample.inx:29`) → `ACPI\<새 HID>`로 바꿈 | `ROOT\AudioCodec` (`AudioCodec.inf:36`) → 같음 |
| OS 조건 | 없음(오래된 모델) | ACX 1.1, KMDF 1.31 이상. Microsoft 문서는 "Windows 10 2004부터 1.1" (https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/acx-version-overview). 25H2에 `Acx01000.sys`가 있는지는 실기기에서 확인 안 함 [가설] |
| 기타 | GPU 드라이버도 WDM(`AppleAgxRenderAdmission.vcxproj:16`)이라 같은 빌드 방식. 자료가 가장 많음 | Microsoft가 새 개발에 권장. "ACX는 WaveRT 스트리밍만 지원" (https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/acx-audio-class-extensions-overview) |

**판단:** 첫 소리에는 (a)를 씁니다. 필요한 형식(48 kHz·16비트·스테레오)이 이미 고정돼 있고, 1 ms 타이머가 "가상 하드웨어"로 버퍼를 읽는 자리가 바로 우리가 DMA 고리에 복사할 자리입니다. 끝점·토폴로지·볼륨 노드도 이미 있어 Windows 소리 설정에 "스피커"가 바로 뜹니다. (b)는 KMDF·ACX 버전 의존이 하나 더 늘 뿐 첫 소리에 줄여 주는 일이 없습니다. 나중에 실기기 수준 드라이버로 다듬을 때 다시 볼 만합니다 [가설: 판단].

샘플 위치: https://github.com/microsoft/Windows-driver-samples (확인한 커밋 `d49bb7d7…`), `audio/simpleaudiosample`, `audio/Acx/Samples`, `audio/sysvad`. 로컬 저장소에는 아직 없습니다(`external/`에 windows-on-m1만 있음) [사실].

### 3.1 가상머신에서 빌드·서명하는 방법

GPU 빌드 흐름(`scratchpad/build-p21.sh`, `/Volumes/media/nwoas/agx-build/scripts/09-driver.ps1`, `10-collect.ps1`) [사실]:

- 맥에서 `rsync`로 소스를 `/Volumes/media/nwoas/...`(VM에서는 `\\Mac\media\nwoas\...`)로 보냄 → `prlctl exec "Windows 11" powershell -File \\Mac\...\스크립트.ps1`(SYSTEM 권한으로 실행) → VM 안 `C:\...`에서 `msbuild` → 결과를 `\\Mac\media\nwoas\out\...`로 모음 → `infverif`, `signtool verify`, SHA256SUMS.
- MSBuild: `C:\VS2022Community\MSBuild\Current\Bin\arm64` (09-driver.ps1:7).
- 서명: vcxproj에 서명 설정이 없어 WDK 기본값(테스트 서명)이 쓰였고, 결과는 `WDKTestCert SYSTEM,134392251612905395`(SHA1 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF`, 만료 2036-11-15) (`port-j274/gpu/driver-p15/BUILD-ATTEMPT-2-20261115.md:92`, 3차 :22·:48에서 같은 인증서). 맥미니 Windows에는 테스트 모드와 이 인증서가 이미 설정됨 [사실: 상태 메모리 "테스트 모드·WDK 테스트 인증서는 콘치님이 설정함"].

소리 드라이버에 그대로 쓰는 방법:

1. `/Volumes/media/nwoas/audio-build/`에 샘플 사본을 두고, `msbuild SimpleAudioSample.sln /p:Configuration=Release /p:Platform=ARM64 /p:WindowsTargetPlatformVersion=10.0.26100.0`을 돌리는 `.ps1` 하나를 같은 방식(`prlctl exec`)으로 실행.
2. 같은 VM·같은 SYSTEM 계정이라 WDK가 기존 `WDKTestCert SYSTEM` 인증서를 다시 쓸 가능성이 높습니다 [가설: GPU 빌드끼리는 재사용 확인, 다른 프로젝트는 미확인]. 빌드 뒤 `signtool verify /v` 결과의 "Issued to"와 SHA1이 위 값과 같은지 반드시 확인하고, 다르면 그 인증서 지문을 명시해 다시 서명합니다(새 인증서를 맥미니에 넣는 일은 하지 않음).
3. 산출물: `.sys`, `.inf`, `.cat` 세 개. PortCls 드라이버는 UMD가 없어서 GPU보다 단순합니다.

---

## 4. 설치 경로 (GPU 2757과 같은 방식)

GPU 2757 설치 흐름(`nwoas_scripts/agx-s248/run-install-2757.sh`) [사실]:

1. 지금 돌고 있는 Windows에 `layout-s231/jobs/W4-restart.cmd`(재시작) 작업을 넣음 → 런처 종료 대기 → USB 장치 두 개 대기.
2. `NWOAS_DELIVER_MANIFEST=<매니페스트.json>`을 붙여 런처(`agxdcp-s253-guest-test.sh`) 실행.
3. 작업 통로가 붙으면(`events.jsonl`의 kind 4) 전달 작업(`file-delivery-s243/jobs/agx-j274-2757.cmd`): `NWGET243.EXE`로 파일을 `C:\NWOAS\drop\…`에 쓰고 SHA-256 재확인(`S243 READBACK MATCH`).
4. 설치 작업(`agx-s248/jobs/I19-install-2757.cmd`): 레지스트리 값 넣기 → `pnputil /add-driver X.inf /install` → 장치 상태 확인 → `reg query`로 드라이버가 남긴 값 읽기.

소리 드라이버에 필요한 것:

| 항목 | 내용 |
|---|---|
| 매니페스트 | `python3 -I s243_manifest.py manifests/snd-j274-NNNN.json <패키지 폴더>` (파일 3개) |
| 전달 작업 | `python3 -I make_job.py manifests/snd-j274-NNNN.json 'C:\NWOAS\drop\snd-j274-NNNN' jobs/snd-j274-NNNN.cmd` |
| 설치 작업 | I19을 본뜬 `.cmd`: `pnputil /add-driver SimpleAudioSample.inf /install` → `Get-PnpDevice -InstanceId 'ACPI\<HID>\0'` → 드라이버가 남긴 레지스트리 값(`Device Parameters`) 읽기. 작업 통로 제한: ASCII `.cmd`, 4032바이트 이하 |
| 런처 | `agxdcp-s253-guest-test.sh`의 사본. 바뀌는 것: 새 페이로드 경로·크기·해시, (방식 A면) 소리 준비 모듈 `-m .../sound-sNNN/module.py`와 그 해시 고정. GPU 설정(NWOAS_AGX_*)은 그대로 두어 화면이 계속 나오게 함 |
| 펌웨어 | `fw-agx-s247/build.py`의 사본에 소리 SSDT 한 줄을 더함(AGX0 SSDT·MemoryInitPeiLib 변경은 유지). 방식 A는 게스트 RAM 안을 예약하지 않으므로 MemoryInitPeiLib 변경이 더 필요 없음 [가설] |
| 판 교체 | GPU처럼 실행 중 `pnputil /restart-device`보다 Windows 재시작이 안전(GPU는 실패 0xC0000483). 소리 드라이버는 GPU 정리 문제와 무관하지만 같은 재시작 절차를 기본으로 씀 |

---

## 5. 작게 나눈 순서와 각 단계의 실기기 확인 방법

| 단계 | 하는 일 | 바뀌는 것 | 실기기 확인 방법 | 실패하면 |
|---|---|---|---|---|
| **0-1. Windows 없이 m1n1에서 소리** | D4 m1n1 프록시(체인로드 전)에서 `speaker_amp.py` 방식으로 짧은 저음량 사인파(예: 1 kHz, 2초). 고칠 점: `from m1n1.hw.dart import DART, DARTRegs`는 지금 트리에 `DARTRegs`가 없어 실패(`m1n1/hw/dart.py` import 목록) → `DART.from_adt(u, "/arm-io/dart-sio")`로 바꾸고(`tools/admac_stream.py`처럼), `dart.initialize()` 대신 2번 통로만 초기화. 소리 크기는 데이터에서 크게 줄이고(업스트림 예시 sox `gain -63`), 앰프 이득 레지스터 0x03 = 0 유지 | 맥북 쪽 스크립트만. 하이퍼바이저·펌웨어·Windows 그대로 | 콘치님 귀로 소리 확인(콘치님 손이 필요한 유일한 확인). 로그: I2C 응답(TAS 레지스터 0x02 되읽기), ADMAC 보고 개수, DART 오류 레지스터 0 | 하드웨어 담당 결과로 클러스터 번호(0/1)·GPIO·NCO 값 대조 |
| 0-2. 하이퍼바이저 아래에서도 소리 | 같은 일을 `run_guest.py`의 `-m` 모듈로 Windows 시작 **직전**에 실행. 끝나면 앰프를 소프트웨어 종료(0x02 = 0x0e)로 두고 블록 전원은 켜 둠. 방식 A의 고정 버퍼·DART 2번 통로 매핑·`hv.map_hw` 창도 여기서 준비 | 런처 사본 + 모듈 하나 | 부팅 중 짧은 소리, 그 뒤 Windows 바탕화면·GPU 화면이 전과 같이 나오는지 | 모듈을 빼고 원래 런처로 부팅(되돌리기 즉시) |
| 1. ACPI 장치 + 드라이버 올라오기 | 소리 SSDT(예: `_HID "NWAS0001"`, 이름은 임의) — `_CRS`: ADMAC 0x238200000+0x34000, MCA 클러스터·스위치 칸, I2C1 0x235014000+0x4000, 방식 A의 고정 창. 인터럽트는 아직 넣지 않음. 드라이버: SimpleAudioSample INF ID만 바꾸고, `StartDevice`에서 자원을 매핑해 몇 레지스터를 읽어 레지스트리에 기록(ADMAC 채널 상태, TAS 0x02 값은 아직 안 읽음) | 펌웨어 빌드, 드라이버 빌드, 설치 작업 | 설치 작업 출력의 장치 상태 `OK`, 레지스트리 값, Windows 소리 설정에 "스피커"가 뜨는지(아직 소리는 안 남 — 샘플이 버퍼를 버림) | 이전 런처로 부팅하면 장치가 사라짐 |
| 2. 타이머로 DMA 고리에 복사 → 소리 | `ReadBytes()`를 교체: 16비트 스테레오를 앰프 형식(하드웨어 담당 결과, 업스트림 예시는 32비트 칸·모노·24비트)으로 바꿔 고정 창의 고리 버퍼에 쓰고, ADMAC 기술자가 비면 보충. `KSSTATE_RUN`에서 MCA 직렬부 켜기·앰프 깨우기(I2C로 0x02), `STOP`에서 음소거·종료. I2C는 m1n1 `hw/i2c.py`(251줄)의 쓰기 부분만 옮긴 폴링 코드 | 드라이버만 | 작업 통로로 `C:\Windows\Media`의 짧은 WAV 재생 명령 → 소리 확인. 레지스트리 카운터(보충한 기술자 수, 비어 버린 횟수, ADMAC 오류) | 드라이버 제거(`pnputil /delete-driver oemNN.inf /uninstall`) 또는 이전 런처 |
| 3. 진짜 위치·인터럽트 | 시간 계산 대신 ADMAC 보고로 재생 위치를 알림. 필요하면 `_CRS`에 `Interrupt { 626 }` 추가(게스트 번호 = AIC 번호) | 드라이버(+펌웨어) | 긴 재생(예: 10분)에서 끊김·밀림 카운터 0, 인터럽트 횟수 기록 | 타이머 방식으로 되돌림 |
| 4. 볼륨·안전 | Windows 볼륨은 먼저 소프트웨어로, 최대 크기에 상한. 재생 정지·Windows 종료·절전 때 앰프 종료. 타이머가 멈추면(복사가 끊기면) 앰프를 음소거하는 감시. 스피커 보호 값(`speaker-protection` 속성)이 없으면 낮은 상한 유지 | 드라이버 | 최대 볼륨에서도 상한 이상 안 커지는지(콘치님 귀), 절전·재시작 뒤 앰프 꺼짐(TAS 0x02 되읽기) | — |

각 단계는 앞 단계 확인 뒤에만 넘어갑니다. 빌드 성공은 소리가 나는 것이 아니며, 소리 확인은 실기기에서 콘치님 귀로만 됩니다.

---

## 6. 확인하지 못한 것

| 항목 | 이유 |
|---|---|
| 스피커가 MCA 클러스터 0인지 1인지 | ADT `mca1` = 0x238404000, 업스트림 예시 `cl_no = 0` — 서로 다름. 하드웨어 담당 몫 |
| DART-sio 초기 상태(잠금 여부, iBoot가 남긴 표, SIO 펌웨어가 0번 통로를 쓰는 중인지) | 실기기 레지스터를 읽지 않음. 0-1단계에서 2번 통로 TCR/TTBR과 CONFIG를 먼저 읽어 기록해야 함 |
| `bypass`, `bypass-address` 속성의 뜻 | 문서·코드 근거를 찾지 못함 |
| guest m1n1·Mu·Windows가 런처가 켜 둔 소리 블록 전원을 끄는지 | 코드상 끄는 곳은 못 찾았지만 실기기 확인 없음. 1단계에서 드라이버가 PMGR 상태를 읽어 기록하면 확인됨 |
| 고정 창 IPA(예: 0x301000000)가 다른 하이퍼바이저 훅과 겹치지 않는지 | `/arm-io` 범위(로그)와 중개 페이지만 확인. 모듈에서 설치 시 검사하도록 만들어야 함 |
| 25H2에 ACX 런타임(`Acx01000.sys`) 존재 여부, ARM64용 `asl.exe` 유무 | 맥미니 Windows를 보지 않음 |
| WDK가 다른 프로젝트에서도 같은 `WDKTestCert SYSTEM`을 쓰는지 | 빌드를 돌리지 않음(빌드 뒤 `signtool verify`로 확인) |
| 소리 인터럽트(626 등)가 실제로 vGIC로 들어오는지 | GPU 줄은 같은 경로지만 소리 번호로는 시험 안 함 |
