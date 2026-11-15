# 네트워크 갈래 계획 (2026-11-15)

표기: **[사실]** = 파일·행 번호나 URL로 확인된 내용. **[가설]** = 확인되지 않은 추측. 실제 맥미니에서 네트워크를 시험한 결과는 아직 하나도 없습니다.

---

## 1. 결론

1. **가장 빠른 길은 USB-A 포트에 꽂는 USB 유선 랜 어댑터입니다.** 1순위는 Realtek RTL8153 칩(USB ID `0BDA:8153`), 2순위는 ASIX AX88179 칩(`0B95:1790`)입니다. Windows에 드라이버가 들어 있으면 꽂기만 하면 되고, 없으면 맥북에서 드라이버를 받아 기존 파일 전달 통로로 넣은 뒤 `pnputil`로 설치합니다. 지금 Windows는 인터넷이 안 되므로 Windows Update로는 드라이버를 받을 수 없습니다.
2. **이 길 앞에는 아직 풀리지 않은 USB-A 데이터 손상 문제가 있습니다.** 처음 쓸 때는 반드시 "받은 파일의 해시를 원본과 비교하는 반복 시험"을 먼저 통과시켜야 합니다. 손상이 나오면 네트워크보다 USB-A 문제를 먼저 고쳐야 합니다.
3. **맥북을 거쳐 인터넷을 쓰는 임시 방법**(기존 맥북↔Windows 통신 통로에 인터넷 통신을 실어 나르는 방식)도 가능합니다. 다만 개발용일 뿐이고 맥북 없이는 안 되며, 속도는 재 본 적이 없습니다. USB 어댑터 길이 막힐 때만 쓸 만합니다.
4. **맥북 없는 듀얼부팅까지 가려면 맥미니 내장 유선 랜(Broadcom BCM57762) 드라이버를 직접 만들어야 합니다.** ARM64용 Windows 드라이버가 없기 때문입니다. 몇 주~몇 달 걸리는 큰 일입니다[가설]. 내장 Wi-Fi(BCM4378)는 Windows 드라이버가 아예 없고 필요한 일이 너무 많아 권하지 않습니다.
5. 리그 오브 레전드(Vanguard 안티치트)까지 생각하면, 직접 만든 드라이버는 정식 서명을 받아야 합니다. Vanguard는 Windows 시험 서명 모드를 거부한다는 보고가 많습니다[가설, 커뮤니티 보고]. 그래서 단기에는 시판 USB 어댑터(제조사 서명 드라이버)가 유리합니다.

| 길 | 맥북 필요 | 예상 기간 | 지금 막는 것 | 권장 |
|---|---|---|---|---|
| USB-A 유선 랜 어댑터 | 지금 구조에선 필요(맥북 없는 부팅이 되면 불필요) | 어댑터 확보 후 수일[가설] | USB-A 데이터 손상 문제 | 1순위 |
| 안드로이드 휴대폰 USB 테더링 | 위와 같음 | 수일[가설] | USB-A 문제 + 휴대폰 방식에 따라 자동 인식 여부 미확인 | 구매 없는 대안 |
| 맥북 경유 임시 통신 | 항상 필요 | 1~2주[가설] | 직접 만들어야 함, 속도 미측정 | 위 두 길이 막힐 때만 |
| 내장 유선 랜(BCM57762) 자체 드라이버 | 불필요 | 수주~수개월[가설] | ARM64 드라이버 없음, 서명 | 듀얼부팅 단계 |
| 내장 Wi-Fi(BCM4378) | 불필요 | 매우 김[가설] | Windows 드라이버 없음, 전원·펌웨어·보정 데이터 전부 필요 | 권하지 않음 |

---

## 2. 현재 상태

### 2.1 Windows 쪽

| 항목 | 상태 | 근거 |
|---|---|---|
| 네트워크 | 없음 | [사실] `NWOAS-STATUS-2026-11-15.md` 13행 |
| 원격 명령 통로 | "NWOAS Agent" 시작 작업(worker-s177 `NWOS.EXE`, SYSTEM 권한) | [사실] 같은 문서 14행 |
| Windows 재시작 시 | 맥미니 전체가 리셋되어 맥북 쪽 실행 스크립트를 다시 띄워야 함 | [사실] 같은 문서 36행 |

### 2.2 맥미니 내부 PCIe 배치

맥미니 내부의 PCIe(고속 내부 연결) 포트 세 개에 다음 장치가 붙어 있습니다.

| 포트 | 버스 | 장치 | 칩 | 근거 |
|---|---|---|---|---|
| 0 | 1 | Wi-Fi + 블루투스 | BCM4378 (`pci14e4,4425`), 블루투스 `pci14e4,5f69` | [사실] `nwoas_scripts/dtb-s221/recovered-j274.dts` 3181행부터의 PCIe 블록, 3216~3231행 |
| 1 | 2 | USB-A 컨트롤러 | FL1100 | [사실] 같은 블록 |
| 2 | 3 | 유선 이더넷 | BCM57762 (`14e4:1682`), Broadcom 제조사 MAC 주소 | [사실] 같은 파일 3249~3268행, 별칭 `ethernet0`은 22행 |

- **유선 이더넷 칩은 PCIe 연결이 잡힙니다.** 맥북 쪽 스캔 로그에 `NWFL scan bus3 dev0: VID=0x14e4 DID=0x1682 … CMD=0x0000` 줄이 있습니다. `capture-walk.*.log` 파일 37개에서 68번, logs 폴더 전체에서 71번 나옵니다. [사실] 직접 검색. `0x1682`가 BCM57762라는 점은 `apple_silicon_platforms_mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl` 320행 주석과도 맞습니다.
- 스캔 시점에 이 칩의 동작 허용 값(`CMD`)은 0이었습니다. 이것은 스캔한 그 순간의 값일 뿐이고, 그 뒤 UEFI가 칩을 켰는지는 확인하지 않았습니다. **"이더넷 칩이 한 번도 켜지지 않았다"는 [가설]입니다.**
- **Wi-Fi는 전원이 들어가지 않습니다.** Wi-Fi 포트는 전원을 SMC(맥의 전원·센서 관리 칩)의 GPIO로 켜야 하는데(`pwren-gpios = <0xb5 0x0d 0x00>`, dts 3216행, `0xb5`는 2648~2655행의 `apple,smc-gpio`), 하이퍼바이저 소스(`m1n1-layout-s231/src`)와 UEFI 소스에 이 전원 제어 코드가 없습니다(검색하면 관계없는 T600X용 dts 하나만 나옴). 로그에도 버스1 장치 줄이 한 번도 없습니다. [사실]
- **Windows에는 PCIe 버스 자체를 숨겨 두었습니다.** `DSDT.asl` 225~241행에서 PCI0을 꺼 두었고(`_STA=0`), 이유로 "Windows가 버스는 읽지만 장치를 켜지 않는다"는 점과 "Windows에 Apple DART(장치가 메모리를 직접 읽고 쓸 때 주소를 바꿔 주는 장치) 드라이버가 없다"는 점을 적어 두었습니다. 대신 USB-A 컨트롤러는 PCIe 장치가 아닌 일반 플랫폼 장치(`SCB0/XHC0`, `PNP0D10`, 인터럽트 698)로 보여 주고, `_INI`에서 메모리 접근과 DMA를 직접 켭니다(396~425행). [사실]
- 이더넷 포트용 인터럽트 번호 701이 DSDT 319행에 적혀 있습니다. [사실]
- UEFI의 DART 드라이버는 PCIe 포트 세 개의 DART를 모두(이더넷 포트의 `0x683008000` 포함) **같은 주소 변환 표**로 설정합니다. `AppleDartIoMmuDxe.c` 433~435행, 720~739행. 넓은 범위 그대로 매핑 기본값은 80~81행. [사실]

### 2.3 USB-A 데이터 손상 문제 (네트워크 1순위 길의 전제)

| 읽기 방식 (WinPE 26100, USB-A) | 결과 | 근거 |
|---|---|---|
| 1 MiB씩 한 번에 하나만 요청하며 읽기 (2회) | 정상 | [사실] `nwoas_scripts/layout-s231/USB-READ-CORRUPTION-20261115.md` 표 |
| 버퍼를 거치는 일반 읽기 | 정상 | 같은 표 |
| robocopy `/J` (버퍼 없이 복사), 133 MB/s | 정상 | 같은 문서 |
| `copy`, DISM 이미지 풀기 | 손상(DISM 오류 1392, 같은 위치 5회 반복) | 같은 표 |

- 손상은 **WinPE 26100**에서 관측됐습니다. 지금 쓰는 설치된 Windows(26200)에서도 생기는지는 아직 관측되지 않았습니다. [사실]
- 원인은 미해결입니다(상태 문서 34행). 그 문서의 [가설]은 "크거나 동시에 여러 개 걸린 대용량 읽기"입니다.

### 2.4 드라이버 사정

| 대상 | Windows ARM64 드라이버 | 근거 |
|---|---|---|
| CDC-NCM 방식 USB 랜 | Windows 11 내장(`UsbNcm.sys`, 클래스 0Dh) | [사실] https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/supported-usb-classes |
| RNDIS 방식 USB 랜 | Windows 10/11 내장(`Rndismp.sys`, 클래스 EF/04/01) | [사실] 같은 페이지 |
| CDC-ECM 방식 USB 랜 | 내장 드라이버 없음 | [사실] 같은 페이지(목록에 없음) |
| Realtek RTL8153 계열 | ARM64 INF(`rtucx22arm64.inf`, `rtu53cx22arm64.inf` 등)가 있다고 함 | 드라이버 모음 사이트만 근거, Realtek 공식 페이지로는 미확인 |
| ASIX AX88179 | ARM64 Windows 11 패키지 3.22.3.0이 있다고 함 | 드라이버 모음 사이트만 근거, 미확인 |
| 내장 BCM57762 | x64/x86 패키지만 보이고 ARM64는 찾지 못함 | 드라이버 모음 사이트만 근거(약한 근거) |
| 내장 BCM4378 Wi-Fi | Windows 드라이버 없음, Linux `brcmfmac`만 있음 | https://lore.kernel.org/linux-acpi/20211226153624.162281-1-marcan@marcan.st/ |

- 참고: BCM57762는 Apple Thunderbolt 기가비트 이더넷 어댑터에 쓰인 칩입니다([사실] NetBSD 커밋 기록). 그래서 Boot Camp용 x64 드라이버가 있을 수 있지만 확인하지 않았습니다[가설]. 있다 해도 ARM64가 아니므로 "직접 만들어야 한다"는 결론은 같습니다.
- FreeBSD의 `bge` 드라이버(BSD 라이선스)가 BCM57762(`0x1682`)를 지원합니다. [사실] https://lists.freebsd.org/pipermail/freebsd-amd64/2013-November/015680.html . 자체 드라이버를 만들 때 참고할 수 있습니다. Linux `tg3`는 GPL이라 참고만 가능합니다.
- 설치된 Windows 26200 ARM64의 드라이버 저장소에 Realtek·ASIX 드라이버가 기본으로 들어 있는지는 **확인하지 않았습니다.** 아래 1단계가 이것을 직접 확인합니다.

### 2.5 맥북→Windows 파일 전달 공간의 제약

| 항목 | 값 | 근거 |
|---|---|---|
| 파일 전달 가상 디스크(namespace 2) 크기 | 33 MiB (맥북 Python 메모리 안의 FAT 디스크) | [사실] `nwoas_scripts/transport-s123/README.md` 8행 |
| 이미 들어 있는 실행 파일 | 약 20개 | 검증 결과 |
| 다음 세션에 남는가 | 남지 않음 | [사실] 같은 문서 35행 |
| `memory-s180/link_module.py`의 `_files`(23~46행)를 고치면 | 실행 스크립트가 sha256 불일치로 시작을 멈춤 | [사실] `layout-s231-guest-test.sh` 37행 |

---

## 3. 해야 할 일 (순서·담당·검증 방법)

담당 표기: **Claude** = 코드·문서 작업(실행은 콘치님 승인 후), **콘치님** = 실제 하드웨어 조작·구매·확인.

### 1단계. Windows에 USB 랜 드라이버가 이미 있는지 확인 (구매 불필요)

- 담당: Claude가 명령 준비, 콘치님 승인 후 실행
- 할 일: "NWOAS Agent" 원격 명령 통로로 `pnputil /enum-drivers`와 `dir /b C:\Windows\System32\DriverStore\FileRepository`를 실행해 결과를 맥북으로 가져옵니다. 찾을 이름: `usbncm`, `rndiscmp`/`rndismp`, `rtu*`/`rtucx*`, `netax*`/`ax88179*`.
- 주의: WinPE 26100에는 `findstr`이 없어서 없는 명령으로 파이프를 걸면 작업 전체가 멈춘 기록이 있습니다([사실] 상태 문서 35행). 설치된 Windows의 이 통로에서 `findstr`이 되는지는 확인되지 않았으므로, **처음에는 파이프 없이 결과 전체를 파일로 받아 맥북에서 검색**합니다.
- 검증: 맥북에 받은 작업 기록에 INF 이름 목록이 있음 → "내장됨"과 "직접 넣어야 함" 중 어느 쪽인지 결정.

### 2단계. 어댑터 고르기와 칩 확인

- 담당: 콘치님(구매·확인)
- 할 일: USB-A RTL8153 어댑터 구매(2순위 AX88179). 같은 제품명이라도 칩이 바뀌는 경우가 있으므로 상표보다 USB ID가 중요합니다. 쓰기 전에 맥북에 꽂고 "시스템 정보 → USB"에서 ID를 확인합니다(macOS 버전에 따라 메뉴 위치가 다를 수 있음).
- 맥미니 USB-A 포트 두 개는 키보드·트랙패드가 쓰고 있어서 허브가 필요할 가능성이 큽니다. 허브 사용 여부, USB 2.0 포트 사용 여부 자체가 시험 조건이므로 기록합니다.
- 구매 없는 대안: 안드로이드 휴대폰 USB 테더링. 다만 최근 순정 안드로이드(Pixel 6 이후)는 RNDIS가 아니라 CDC-NCM 방식으로 바뀌었다는 보고가 있습니다[가설, 2차 출처]. 그 경우 내장 `UsbNcm`이 붙을 것으로 봅니다. RNDIS 휴대폰은 클래스 `E0/01/03`을 쓰는 경우가 많은데, Microsoft 표에는 `EF/04/01`만 있어 자동으로 붙는지 확인되지 않았습니다.
- 검증: 어댑터(또는 휴대폰)의 VID:PID와 USB 방식을 기록.

### 3단계. 드라이버 설치

- 담당: Claude가 파일 준비, 콘치님 승인 후 실행
- 드라이버가 내장돼 있으면: 꽂기만 합니다.
- 없으면:
  1. 맥북에서 Realtek(또는 ASIX) **공식 사이트**의 Windows 11 패키지를 받고 해시를 기록합니다(내려받기는 콘치님 승인 필요).
  2. ARM64 INF와 그에 딸린 파일만 골라 33 MiB 안에 들어가는지 확인합니다.
  3. 전달은 NWGET(`file-delivery-s168`)으로 합니다. `link_module.py`의 `_files`에 넣는 방식은 실행 스크립트의 고정 해시를 함께 고쳐야 하므로(위 2.5) 피하거나, 그렇게 할 경우 해시 갱신까지 한 번에 합니다.
  4. Windows에서 `pnputil /add-driver <폴더>\*arm64*.inf /subdirs /install`.
- 검증: `Get-NetAdapter`에서 상태 Up, 1 Gbps / `pnputil /enum-devices /class Net /connected`에 나옴 / 장치 관리자에 오류 코드 10·28 없음.

### 4단계. 데이터 무결성 반복 시험 (필수, USB-A 문제 때문)

- 담당: Claude가 시험 절차·스크립트 준비, 콘치님 승인 후 실행, 결과 확인은 함께
- 왜 필요한가[가설]: 랜 드라이버는 16~32 KB 정도 묶음으로 여러 개의 받기 요청을 동시에 걸어 둡니다. USB-A 손상이 의심되는 바로 그 형태입니다. 그리고 손상이 USB에서 Windows 메모리로 옮기는 도중에 생기면, 어댑터가 이미 "체크섬 정상"이라고 표시한 뒤라 Windows가 손상된 데이터를 그대로 받아들일 수 있습니다.
- 할 일:
  1. 같은 공유기에 연결된 맥북에서 2~4 GB 무작위 파일을 `python3 -m http.server`로 제공합니다.
  2. Windows에서 내장 `curl.exe`로 10번 이상 받고 `certutil -hashfile … SHA256`으로 원본과 비교합니다.
  3. 어댑터의 "받기 체크섬 오프로드(IPv4/TCP/UDP Checksum Offload Rx)"를 켠 상태와 끈 상태 둘 다 합니다(끄면 Windows가 직접 체크섬을 검사).
  4. `netstat -s`의 오류 받기 수, iperf3 속도(x64판이 에뮬레이션으로 돎), 시스템 로그의 USBXHCI·Kernel-PnP 이벤트를 기록합니다.
- 통과 기준:

| 지표 | 기준 |
|---|---|
| 받은 총량 (GB) | 20 이상 |
| 해시 불일치 (회) | 0 |
| 장치 리셋 (회) | 0 |
| `netstat -s` 오류 받기 증가 (개) | 증가 없음 |

- 불일치가 한 번이라도 나오면 USB-A 데이터 손상을 다시 최우선 문제로 올립니다. 이때 이 시험은 손상을 깔끔하게 재현하는 방법으로도 쓸 수 있습니다.
- 체크섬 오프로드를 끄는 것과 HTTPS(TLS)에 기대는 것은 시험 중의 안전장치일 뿐, 원인 해결을 대신하지 않습니다.

### 5단계. 4단계 통과 뒤에만: Windows Update, 정품 인증, Store

- 담당: 콘치님
- Windows를 재시작하면 맥미니 전체가 리셋되므로 맥북 쪽 실행 스크립트를 다시 띄워야 합니다.
- 검증: 업데이트 설치 후 다시 부팅해 네트워크가 유지되는지 확인.

### (보조) 맥북 경유 임시 통신 — 위 길이 막힐 때만

- 담당: Claude 구현, 실행은 콘치님 승인 후
- 바탕[사실]: 맥북↔Windows 통신 공간(namespace 2)의 LBA 128에 우편함 영역이 있고, CRC32·세션 토큰·순번 확인·4 KiB 버퍼 없는 읽기쓰기를 씁니다(`transport-s123/README.md`). 이 통로는 USB CDC 연결로 오갑니다(`layout-s231-guest-test.sh`).
- 방법 두 가지:
  - 프록시 방식(드라이버 불필요): Windows에서 작은 프로그램이 `127.0.0.1:1080`(SOCKS5/HTTP)으로 연결을 받고, 그 내용을 새 우편함으로 맥북 Python에 넘기면 맥북이 실제 인터넷 연결을 엽니다. 브라우저 프록시와 `netsh winhttp set proxy`를 설정합니다. 웹, 패키지 내려받기, 어쩌면 Windows Update까지 됩니다[가설].
  - 가상 랜카드 방식: 서명된 ARM64 드라이버가 있는 Wintun(https://www.wintun.net/)으로 가상 랜카드를 만들고, IP 패킷을 우편함으로 맥북에 넘겨 맥북에서 주소 변환(NAT)으로 내보냅니다.
- [가설] 속도는 USB CDC 연결과 명령마다 맥북을 왕복하는 시간에 묶여 초당 몇 MB 수준일 것으로 봅니다. 재 본 적은 없습니다. 일상 사용용은 될 수 없습니다.
- 검증: 프록시를 거친 `curl` + 4단계와 같은 SHA256 비교, 속도(MB/s) 측정.

### (장기) 내장 유선 랜(BCM57762) — 맥북 없는 듀얼부팅 단계

순서대로 필요한 일:

1. 하이퍼바이저나 UEFI가 BCM57762의 메모리 영역을 할당하고 메모리 접근·DMA를 켬(`CMD=0x6`, USB-A 컨트롤러의 `XHC0._INI`가 `DSDT.asl` 396~425행에서 하는 것과 같은 방식).
2. ACPI에 USB-A 컨트롤러처럼 플랫폼 장치 항목을 추가(예: 이름 `NWOA5776`, 메모리 영역, 인터럽트 701, DMA 정보).
3. 이더넷 포트 인터럽트가 막혀 있지 않게 함.
4. Windows ARM64용 랜 드라이버(NetAdapterCx/NDIS)를 작성하고, Vanguard를 생각하면 정식 서명까지 받음.

- [가설] 이더넷 포트는 이미 USB-A와 같은 DART 주소 변환 표를 쓰므로, 2번까지 하면 DMA와 인터럽트는 대체로 해결될 것으로 봅니다. 하지만 UEFI가 이 칩의 메모리 영역을 할당하는지, 인터럽트가 열려 있는지는 모릅니다.
- [가설] 같은 이유로, USB-A 손상의 원인이 DART 주소 변환에 있다면 내장 유선 랜도 같은 문제를 물려받을 수 있습니다.
- 담당: 1~3은 Claude(하이퍼바이저·UEFI 수정, 실기 확인은 콘치님), 4는 Claude 구현 + 서명 절차는 콘치님.
- 검증: 1~3 뒤 Windows 장치 관리자에 새 장치가 오류 없이 나타남 → 4 뒤 2단계 이후와 같은 시험(링크 Up, 4단계 무결성 시험).

---

## 4. 위험과 모르는 점

| 항목 | 내용 | 확인 방법 |
|---|---|---|
| USB-A 데이터 손상 | 원인 미해결. 랜 어댑터의 동시 받기 형태가 손상 조건과 겹칠 수 있음[가설] | 4단계 |
| 설치된 Windows에서도 손상되는가 | WinPE 26100에서만 관측, 26200에서는 미관측 | 4단계 |
| 드라이버 내장 여부 | 26200 ARM64 드라이버 저장소에 Realtek·ASIX가 있는지 모름 | 1단계 |
| 드라이버 출처 | Realtek·ASIX ARM64 패키지 정보는 드라이버 모음 사이트만 근거 | 공식 사이트에서 직접 확인 |
| 휴대폰 테더링 자동 인식 | NCM 방식이면 내장 드라이버로 될 것[가설], RNDIS 클래스 `E0/01/03`은 자동 인식 미확인 | 실제 연결 후 장치 관리자 확인 |
| RTL8153이 내장 NCM으로 붙는가 | Windows는 보통 USB 첫 번째 구성(제조사 전용)을 고르므로 붙지 않을 것[가설] | 1·3단계 |
| USB 포트 부족 | 허브 필요 가능성, 허브·USB 2.0 자체가 변수 | 2단계에서 구성 기록 |
| 파일 전달 공간 | 33 MiB, 세션마다 사라짐, `_files` 수정 시 실행 스크립트 정지 | 3단계에서 NWGET 사용 |
| 재시작 시 전체 리셋 | Windows Update 재부팅마다 맥북 쪽 재실행 필요 | 5단계 |
| Vanguard | 시험 서명 드라이버 거부 보고[가설, 커뮤니티 보고] | 자체 드라이버 단계 전에 확인 |
| 내장 이더넷 활성 여부 | 스캔 뒤 UEFI가 칩을 켰는지 모름 | 하이퍼바이저 로그 또는 UEFI 로그 추가 |
| 맥북 경유 통신 속도 | 미측정 | 보조 단계의 측정 |

---

## 5. 출처

프로젝트 파일 (경로는 `/Users/angigyeom/Desktop/DEV/NWOAS/` 기준)

- `NWOAS-STATUS-2026-11-15.md` 13~14행, 34~36행
- `nwoas_scripts/dtb-s221/recovered-j274.dts` 22행, 2648~2655행, 3181~3274행
- `apple_silicon_platforms_mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl` 225~241행, 319~320행, 345~457행(XHC0 `_INI`는 396~425행)
- `apple_silicon_platforms_mu/.../AppleDartIoMmuDxe.c` 80~81행, 433~435행, 720~739행
- `m1n1-layout-s231/src/pcie.c` (전원·GPIO·SMC 처리 없음)
- `nwoas_scripts/logs/capture-walk.*.log` (bus3 스캔 줄 68회, logs 전체 71회)
- `nwoas_scripts/layout-s231/USB-READ-CORRUPTION-20261115.md`
- `nwoas_scripts/transport-s123/README.md` 8행, 35행
- `nwoas_scripts/layout-s231-guest-test.sh` 37행
- `nwoas_scripts/memory-s180/link_module.py` 23~46행

웹

- Microsoft 내장 USB 클래스 드라이버: https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/supported-usb-classes
- Microsoft NCM 드라이버 소스: https://github.com/microsoft/NCM-Driver-for-Windows
- Surface USB-C 이더넷 어댑터 드라이버(자동 설치 안내, 내장 증거는 아님): https://support.microsoft.com/help/4344996
- ARM64에서 제3자 USB 랜 칩에 제조사 ARM64 패키지가 필요한 사례: https://support.etcconnect.com/HES/Consoles/Hog/Networking/How_to_make_Gig_Hog's_Ethernet_Ports_work_on_a_Windows_Arm64_Computer
- Realtek USB 랜 패키지 목록(드라이버 모음 사이트, 약한 근거): https://treexy.com/products/driver-fusion/database/network-adapters/realtek/gaming-usb-gbe-family-controller/
- ASIX ARM64 패키지 목록(드라이버 모음 사이트, 약한 근거): https://oemdrivers.com/network-asix-3-22-3-0
- Broadcom NetXtreme 패키지 목록(드라이버 모음 사이트, 약한 근거): https://treexy.com/products/driver-fusion/database/network-adapters/broadcom/netxtreme-gigabit-ethernet-controller/
- FreeBSD bge의 BCM57762 지원: https://lists.freebsd.org/pipermail/freebsd-amd64/2013-November/015680.html
- BCM57762가 Apple Thunderbolt 이더넷 어댑터 칩이라는 NetBSD 기록: https://mail-index.netbsd.org/source-changes/2012/09/17/msg037343.html
- BCM4378 Linux 지원과 Apple 보정 데이터 요구: https://lore.kernel.org/linux-acpi/20211226153624.162281-1-marcan@marcan.st/
- 안드로이드 테더링 방식 변경 보고(2차 출처): https://nothing.community/d/57757 , https://postmarketos.org/edge/2023/10/29/rndis-ncm
- Wintun: https://www.wintun.net/
