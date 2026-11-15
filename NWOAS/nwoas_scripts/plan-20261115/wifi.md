# 내장 Wi-Fi 계획 (2026-11-15)

표기: **[사실]** = 파일·행 번호나 URL로 확인한 내용. **[가설]** = 확인하지 않은 추측. 이 문서의 어떤 단계도 아직 실제 맥미니에서 시험하지 않았습니다. 빌드·코드 작성이 끝나도 동작한다는 뜻이 아니며, 판정은 각 단계의 하드웨어 검증으로만 합니다.

이 문서는 `network.md`의 우선순위를 대신합니다. `network.md`는 USB 유선 랜 어댑터를 1순위로, 내장 Wi-Fi를 "권하지 않음"으로 적었지만, 콘치님 결정에 따라 이제 **내장 Wi-Fi가 1순위**입니다. `network.md`의 하드웨어 사실(2장)과 USB-A 손상 기록은 그대로 유효합니다.

---

## 1. 결론

1. **칩은 Broadcom BCM4378 B1(PCI ID `14e4:4425`)이고, 같은 칩에 블루투스(`14e4:5f69`)가 함께 들어 있습니다.** 맥미니 내부 PCIe 포트 0번(버스 1)에 붙어 있습니다. [사실]
2. **지금 Wi-Fi가 안 보이는 첫 원인은 전원입니다.** 이 칩의 전원은 SMC(맥의 전원 관리 칩)의 GPIO 13번으로 켜야 하는데, 우리 하이퍼바이저·UEFI·맥북 쪽 스크립트 어디에도 이 동작이 없습니다. 그래서 지금까지 모든 부팅 기록에서 버스 1이 비어 있었습니다. [사실] m1n1에는 SMC에 같은 방식으로 값을 쓰는 코드가 이미 있습니다(HDMI 전원용). [사실]
3. **Windows ARM64용 BCM4378 드라이버는 찾지 못했습니다.** Boot Camp 드라이버는 Intel 맥 전용이고, Windows ARM64는 x64 커널 드라이버를 아예 쓸 수 없습니다. [사실] 따라서 드라이버를 직접 만들어야 합니다.
4. **만들 드라이버의 바탕은 OpenBSD `bwfm`(ISC 라이선스)이 가장 알맞습니다.** BCM4378 PCIe를 지원하고, 암호 협상(WPA2 4단계 핸드셰이크)을 칩 펌웨어가 아니라 운영체제 쪽에서 하도록 되어 있어 Windows 방식과 맞습니다. 코어+PCIe 부분이 모두 합쳐 약 5,900줄입니다. [사실] Apple 전용 처리(OTP 읽기, 펌웨어 이름 고르기, 무작위 시드)는 Linux `brcmfmac`(역시 ISC)을 참고합니다. [사실] Windows 쪽 틀은 Microsoft WiFiCx(Windows 11용 Wi-Fi 드라이버 틀)입니다.
5. **가장 빠른 첫 확인은 Windows 없이 맥북에서 합니다.** ① SMC로 전원을 켜고 ② 버스 1에서 `14e4:4425`가 읽히는지 보고 ③ 칩에 펌웨어를 올려 "준비됨" 신호를 받는 것까지 모두 메모리 읽기·쓰기만으로 됩니다(DMA·인터럽트 불필요). 여기서 칩이 살아 있음을 확인한 뒤에 Windows 쪽으로 넘어갑니다.
6. **Wi-Fi 전원을 켜면 USB-A가 깨질 수 있는 위험이 있습니다.** UEFI가 PCI 장치 주소를 자동 배정하는데, 지금 USB-A 컨트롤러 주소 `0x6C0000000`은 하이퍼바이저·UEFI·ACPI 여러 곳에 고정값으로 박혀 있습니다. Wi-Fi가 새로 나타나면 이 주소가 밀릴 수 있습니다. [사실: 고정값 위치] [가설: 실제로 밀리는지] 첫 단계에서 반드시 확인합니다.

| 단계 | 내용 | 어디서 | 예상 기간 (주) [가설] | 통과 기준 |
|---|---|---|---|---|
| 1 | 전원 켜기 + 연결 확인 | 맥북 스크립트 | 0.5 | 버스 1에 `14e4:4425`, USB-A 주소 그대로 |
| 2 | 펌웨어 올리고 "준비됨" 받기 | 맥북 스크립트 | 1~2 | 칩 번호 4378 rev 3, 펌웨어 버전 문자열 |
| 3 | Windows에 장치로 보이기 | ACPI·UEFI | 0.5 | 장치 관리자에 새 장치 + 자원 목록 |
| 4 | 최소 Windows 드라이버 | Windows 드라이버 | 2~4 | Windows에서 펌웨어 버전·MAC 주소 읽힘 |
| 5 | 주변 AP 검색 | WiFiCx | 3~5 | `netsh wlan show networks`에 AP 목록 |
| 6 | WPA2 접속 + 데이터 | WiFiCx | 4~8 | IP 받음, 파일 해시 반복 시험 통과 |
| 7 | 속도·안정성·WPA3 | WiFiCx | 4 이상 | macOS 같은 자리 측정값과 비교 |
| 8 | 서명·정리 | 콘치님 결정 | 미정 | 서명 방식 결정 |

---

## 2. 하드웨어

### 2.1 칩과 위치

| 항목 | 값 | 근거 |
|---|---|---|
| Wi-Fi | BCM4378, `pci14e4,4425`, PCIe 버스 1 장치 0 기능 0 | [사실] `nwoas_scripts/dtb-s221/recovered-j274.dts` 3218~3224행, 별칭 18행 |
| 블루투스 | 같은 칩의 기능 1, `pci14e4,5f69` | [사실] 같은 파일 3226~3231행, 별칭 12행 |
| 맥 쪽 기록 | `/product wifi-chipset = "4378"`, `/arm-io/apcie/pci-bridge0/wlan` compatible `wlan-pcie,bcm4378` | [사실] 맥미니 ADT 덤프 `experiments/tahoe-afk-20260906/current.adt` (target-type `J274`, `Macmini9,1` 확인) |
| 칩 리비전 | B1 (리비전 번호 3) | [사실] Linux `brcmfmac` 표 `BRCMF_FW_ENTRY(BRCM_CC_4378_CHIP_ID, 0x0000000F, 4378B1) /* revision ID 3 */`. 맥 펌웨어 폴더 중 `atlantisb` 파일은 `C-4378__s-B1`에만 있음(아래 2.4) |
| 보드 이름 | `atlantisb` (DT `brcm,board-type = "apple,atlantisb"`, ADT `module-instance`) | [사실] dts 3223·3230행, ADT `/arm-io/wlan` |
| 안테나 종류 | `X3` | [사실] ADT `wifi-antenna-sku-info` 16바이트 중 8번째부터 `58 33` = "X3". m1n1이 이 위치를 읽는 코드: `m1n1-layout-s231/src/kboot.c` 988~1001행 |
| 보정 데이터 | ADT `wifi-calibration-msf`, 2,096바이트 | [사실] ADT 덤프. m1n1은 이것을 `brcm,cal-blob`으로 넘김(kboot.c 1004~1012행) |
| 규격 | 802.11ax(Wi-Fi 6), 블루투스 5.0 | [사실] https://support.apple.com/kb/SP823 |

MAC 주소와 보정 데이터는 기기마다 다른 값이라 이 문서에 적지 않았습니다. 드라이버는 실행할 때 ADT에서 읽어 씁니다.

### 2.2 PCIe 연결과 주소

| 항목 | 값 | 근거 |
|---|---|---|
| PCIe 루트 | `pcie@690000000`, 설정 공간(ECAM) `0x690000000` | [사실] dts 3181행 |
| Wi-Fi 설정 공간 주소 | `0x690100000` (버스 1, 장치 0, 기능 0), 블루투스 `0x690101000` | [사실] 주소 공식 `base + (bus<<20) + (dev<<15) + (fn<<12)`: `m1n1-layout-s231/nwoas_capture_hook.py` 150~151행. USB-A는 같은 공식으로 `0x690200000`(DSDT 408행) |
| 포트 0 최고 속도 | Gen2 | [사실] ADT `pci-bridge0/maximum-link-speed = 2`, 부팅 기록 `pcie: Port 0 max speed = 2` 387회 |
| 장치 메모리 창 | 32비트 창: PCI `0xC0000000` → CPU `0x6C0000000`, 1 GB / 64비트 창: `0x6A0000000`, 512 MB | [사실] DSDT 244~270행 |
| 칩이 쓰는 메모리 영역(BAR) 크기 | 모름 | 1단계에서 설정 공간을 읽어 확인 |

### 2.3 전원·리셋·클록·인터럽트

| 신호 | 값 | 근거 |
|---|---|---|
| 전원 켜기 | SMC GPIO 13번 (`pwren-gpios = <0xb5 0x0d 0x00>`, `0xb5` = SMC GPIO 장치) | [사실] dts 3215행, 2650~2658행. Asahi 원본 DT도 같음: `dtb-s221/upstream-68.pre.dts` 2758~2760행 |
| SMC에 쓰는 값 | 키 `gP0d`에 `0x800001`(켜기) [가설: 값] | [사실] 키 이름 규칙 `'gP' + 두 자리 16진수`와 "Enable wifi/bluetooth, RFKILL_PIN = 13" 주석: `m1n1-layout-s231/proxyclient/experiments/smc.py` 20~29행. [사실] m1n1 C 코드가 HDMI 전원 GPIO에 `0x800001`을 쓰는 선례: `m1n1-usb-s232/src/dcp.c` 50~53행, 쓰기 함수 `smc.c` 173행 |
| 리셋(PERST#) | AP GPIO 152(`0x98`), 낮을 때 리셋 | [사실] dts 3206행, ADT `pci-bridge0/function-perst` |
| 클록 요청 | AP GPIO 150(`0x96`) | [사실] ADT `function-clkreq`, dts `pcie-pins` 2415행 |
| 깨우기 신호 | ADT `/arm-io/wlan` 인터럽트 = `/arm-io/gpio`의 196번(`0xc4`) | [사실: 값] [가설: Wi-Fi가 호스트를 깨우는 선이라는 해석] |
| 포트 0 일반 인터럽트(INTx) | AIC 695 (`0x2b7`), 높은 수준 유지형 | [사실] dts 3187행, DSDT 292·299~307행 |
| 포트 0 DART 인터럽트 | AIC 696 (`0x2b8`) | [사실] dts 3154행 |
| MSI | AIC 704~735(32개), 포트 0 몫 8개 | [사실] dts 3190행, ADT `#msi-vectors = 8`, `msi-vector-base = 0` |
| 전원 영역 | `apcie_gp`, 항상 켜짐 | [사실] dts 1915~1924행 |
| 기타 | ADT `pci-bridge0`에 `manual-enable`이 있음(포트 1에는 없음) | [사실: 속성 존재] [가설: macOS도 이 포트를 Wi-Fi 드라이버가 따로 켠다는 뜻] |

- **전원이 실제로 원인이라는 근거**: 부팅 기록 전체에서 포트 0 다리(bridge)는 하위 버스를 1번으로 잡았지만(`pri=0 sec=1 sub=1`, 46회), 버스 1의 장치 줄은 한 번도 없습니다. 같은 기록에 버스 2(USB-A, 69회)·버스 3(유선 랜, 71회)은 있습니다. [사실] `nwoas_scripts/logs` 검색. 단, "전원만 켜면 연결이 잡힌다"는 [가설]입니다.
- OpenBSD에서도 맥미니 M1의 Wi-Fi가 SMC 드라이버(`aplsmc`) 문제로 인식되지 않았다가 고쳐진 보고가 있습니다. [가설, 커뮤니티 보고] https://reddit.birdcat.cafe/r/openbsd/comments/1igc0an/wifi_not_recognized_mac_mini_m1_openbsd_76
- 지금 우리 쪽에서 포트를 켜는 곳은 두 군데입니다. 맥북 Python의 `p.pcie_init()`(m1n1 `pcie.c` 725~740행, 리셋 해제는 포트 레지스터로만 함)과 UEFI `AppleSiliconPciPlatformDxe.c`(PERST# GPIO 조작 300행, 연결 시작 322행, 연결 대기 1000회 330행, INTx 열기 355~367행). 둘 다 SMC 전원은 건드리지 않습니다. [사실]

### 2.4 필요한 펌웨어 파일

맥북(macOS 27.2.1)의 `/usr/share/firmware/wifi/C-4378__s-B1/`에서 확인했습니다. [사실]

| 종류 | 파일 | 크기 (바이트) | 비고 |
|---|---|---|---|
| 칩 펌웨어 | `atlantisb-X3.trx` → `atlantisb.trx` → `honshu.trx` (바로가기) | 1,497,470 | [사실] |
| 무선 규제표(CLM) | `atlantisb-X3.clmb` | 약 34,061 (`atlantisb.clmb` 기준) | [사실] |
| 송신 출력 한계표(TxCap) | `atlantisb-X3.txcb` | 약 304 (`atlantisb.txcb` 기준) | [사실] |
| 보드 설정(NVRAM) | `P-atlantisb-X3_M-RASP_V-<제조사>__m-<버전>.txt` | 약 7,723 (`..._V-u__m-6.5.txt` 기준) | [사실] 후보: 제조사 `u` 버전 3.7·4.1·6.1·6.3·6.5, 제조사 `m` 버전 6.1·6.3·6.9·6.11·7.1 |
| 블루투스 펌웨어 | `/usr/share/firmware/bluetooth/BCM4378B1_24.2.551.4114_PCIE_macOS_AtlantisB_OS_MUR_20260715.bin` 또는 `..._USI_...bin` | 미확인 | [사실: 파일 존재] |

- NVRAM 파일의 제조사·버전은 칩 안의 OTP(한 번만 쓰는 메모리)에서 읽은 값으로 고릅니다. `brcmfmac`은 OTP의 `M`(모듈)·`V`(제조사)·`m`(버전) 항목을 읽습니다(`brcmf_pcie_parse_otp`). [사실] 파일 이름의 `M-RASP`, `V-u`/`V-m`, `m-6.5`와 대응한다는 것은 [가설]입니다. 2단계에서 OTP를 직접 읽어 확정합니다.
- 펌웨어를 올릴 때 256바이트 무작위 시드(표시값 `0xfeedc0de`)를 함께 넣습니다. [사실] `brcmfmac` `BRCMF_RANDOM_SEED_MAGIC`, `BRCMF_RANDOM_SEED_LENGTH 0x100`.
- 맥미니 자체 macOS(26 Tahoe)의 펌웨어 버전과 맥북 macOS 27.2.1의 버전이 다를 수 있습니다. 맥미니 쪽 파일을 쓰는 것이 안전합니다[가설]. 두 판 다 시험해 볼 수 있습니다.
- `.trx` 파일을 변환 없이 그대로 칩에 올리면 되는지는 [가설]입니다(Asahi·OpenBSD가 macOS 파일을 그대로 쓰는 것으로 압니다). 2단계에서 확인합니다.
- 펌웨어는 Apple 파일이므로 저장소나 공개 장소에 올리지 않고, 콘치님 맥에서 꺼내 씁니다.

---

## 3. 드라이버 선택지

### 3.1 이미 있는 드라이버 — 없음

| 찾은 것 | 결과 | 근거 |
|---|---|---|
| BCM4378/4377/4387용 Windows ARM64 드라이버 | 찾지 못함 | 웹 검색 여러 번(칩 이름+Windows ARM64, GitHub 저장소 검색 `bcm4378` 결과 0건). 제 검색 도구에서는 이 검색어들로 나오지 않았다는 뜻입니다 |
| Broadcom Windows Wi-Fi 드라이버(`bcmwl63`, `bcmpciedhd63`, `bcmwdidhdpcie`) | x86·x64판만 보임 | 드라이버 모음 사이트(약한 근거): https://treexy.com/products/driver-fusion/database/network-adapters/broadcom/802.11ac-wireless-pcie-full-dongle-adapter/ |
| Boot Camp | Intel 맥 전용 | [사실] https://support.apple.com/102622 ("Intel-based Mac") |
| x64 드라이버를 ARM64 Windows에서 쓰기 | 불가 | [사실] https://learn.microsoft.com/windows/arm/faq (커널 드라이버는 ARM64로 빌드해야 함) |
| Raspberry Pi용 Windows의 Broadcom Wi-Fi(같은 계열 칩) | "No driver available" | [사실] https://github.com/worproject/RPi-Windows-Drivers |
| 참고 프로젝트 `external/windows-on-m1`(J313) | Wi-Fi 작업 없음, 미지원으로 명시 | [사실] `documentation/LIMITATIONS.md` 18~19행. 저장소 전체에서 Wi-Fi 관련 코드·계획 없음 |

### 3.2 바탕 코드 후보

| 후보 | 라이선스 | 크기 (줄) | BCM4378 | 암호 협상 위치 | 판단 |
|---|---|---|---|---|---|
| OpenBSD `bwfm` (`sys/dev/ic/bwfm.c` + `sys/dev/pci/if_bwfm_pci.c`) | ISC | 3,324 + 2,596 = 5,920 | 지원(`4378b1`/`4378b3`, OTP 읽기 포함) | 운영체제 쪽(`sup_wpa`를 0으로 꺼서 펌웨어 협상 끔) | **주 바탕** |
| Linux `brcmfmac` | ISC | 미측정, `bwfm`보다 훨씬 큼 [가설] | 지원, Apple 전용 처리가 가장 완전 | 둘 다(WPA3는 펌웨어 쪽만) | Apple 전용 부분 참고 |
| Linux `pcie-apple.c` | GPL-2.0 | — | — | — | 읽고 동작만 참고(코드 복사 안 함) |
| Linux `hci_bcm4377`(블루투스) | GPL-2.0-only OR MIT | — | 4378 블루투스 | — | 블루투스 단계에서 MIT 쪽으로 참고 |
| Microsoft WiFiCx 예제 | 저장소 라이선스 확인 필요 | — | — | — | Windows 틀 참고 |

근거 [사실]:
- OpenBSD 파일 크기·ISC·4378·OTP: https://github.com/openbsd/src/blob/master/sys/dev/pci/if_bwfm_pci.c , https://github.com/openbsd/src/blob/master/sys/dev/ic/bwfm.c (주석: 펌웨어 협상을 끄고 "our stack"이 처리). `bwfm(4)` 지원 목록에 BCM4378 PCI: https://man.openbsd.org/bwfm.4
- OpenBSD가 2021년 "Apple M1 SoC의 BCM4378" 기본 지원을 넣었고, M1 MacBook Air에서 잘 된다는 사용자 보고: [가설, 2차 출처] https://hn.jakechampion.name/reader/http://kernelpanic.life/hardware/openbsd-m1-macbook-air.html
- `brcmfmac` ISC, 링 이름, 메일박스 레지스터, MSI 사용: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c
- `pcie-apple.c` GPL-2.0: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/drivers/pci/controller/pcie-apple.c
- `hci_bcm4377` 라이선스: https://code.woboq.org/linux/linux/drivers/bluetooth/hci_bcm4377.c.html
- WiFiCx 예제가 들어간 Microsoft 저장소 PR(2026-06-19 병합): https://github.com/microsoft/Windows-driver-samples/pull/1394

ISC는 저작권 표시만 남기면 고쳐 쓰고 배포할 수 있는 허용형 라이선스입니다. 드라이버 소스 맨 위에 OpenBSD·Broadcom 저작권 표시를 그대로 둡니다.

### 3.3 드라이버 구조

BCM4378은 "풀맥(FullMAC)" 칩입니다. 무선 처리 대부분을 칩 안의 펌웨어가 하고, 호스트 드라이버는 펌웨어에 명령을 보내고 이더넷 형태의 데이터를 주고받습니다. [사실] https://wireless.docs.kernel.org/en/latest/en/users/drivers/brcm80211.html

```
Windows (WLAN 서비스: 검색·접속·WPA2 협상)
   │  WiFiCx 명령(WDI)            NetAdapterCx 송수신 큐
   ▼                                   ▼
[우리 드라이버 NWOA4378.sys, KMDF]
   ├ 장치 연결부: BAR 매핑, INTx 695 인터럽트, DMA 공용 버퍼
   ├ 칩 제어부: 코어 목록 읽기, ARM 코어 리셋, 칩 내부 메모리(TCM)에 펌웨어·NVRAM·시드 쓰기
   ├ 메시지 링: 공용 링 5개 + 송신 링(목적지·우선순위별)
   ├ 명령 통로: 펌웨어 설정 읽기/쓰기(iovar), 펌웨어 이벤트 받기
   └ WiFiCx 연결부: 아래 표의 명령 ↔ 펌웨어 명령 번역
   ▼
BCM4378 펌웨어
```

- 공용 링 5개 [사실, brcmfmac]: 호스트→칩 명령 제출, 호스트→칩 수신 버퍼 제공, 칩→호스트 명령 완료, 칩→호스트 송신 완료, 칩→호스트 수신 완료.
- 칩이 호스트를 부르는 방법: 메일박스 인터럽트 레지스터(`BRCMF_PCIE_PCIE2REG_MAILBOXINT 0x48`, 신형 `0xC30`). [사실] BCM4378이 어느 쪽을 쓰는지는 [가설] — 2단계에서 확인.
- 펌웨어 올리기는 BAR을 통한 메모리 쓰기만으로 됩니다(DMA 불필요). [가설, brcmfmac·bwfm 동작 방식 기준] 이 덕분에 2단계를 Windows 없이 할 수 있습니다.

WiFiCx 명령과 펌웨어 명령 대응 [가설 — 설계안]:

| Windows가 보내는 것 | 드라이버가 펌웨어에 하는 것 |
|---|---|
| 장치 능력 묻기(`OID_WDI_GET_ADAPTER_CAPABILITIES`) | 펌웨어 `cap`·`ver` 읽기, 지원 대역·암호 방식 알림 |
| 검색(`OID_WDI_TASK_SCAN`) | `escan` 시작 → 검색 결과 이벤트를 BSS 목록 알림으로 전달 |
| 접속(`OID_WDI_TASK_CONNECT`) | 암호 방식(`wsec`, `wpa_auth`) 설정 → `join` → 연결 이벤트 알림 |
| WPA2 4단계 협상 | 펌웨어 협상 끔(`sup_wpa 0`), EAPOL 프레임을 Windows와 그대로 주고받음 |
| 키 설치 | `wsec_key` |
| 연결 끊기 | `disassoc` |
| WPA3-SAE | Windows가 SAE 프레임을 만들고 드라이버는 보내고 받기만 함(WDI 1.1.8 이상). [사실] https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/wificx-wpa3-sae-authentication . Apple 펌웨어가 호스트가 만든 SAE 프레임 송신을 허용하는지는 모름(6장) |

- WiFiCx는 Windows 11 전용 KMDF 틀이고, NetAdapterCx 드라이버이기도 합니다. [사실] https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/wifi-wdf-class-extension-wificx
- Linux의 Apple 칩 WPA3는 펌웨어 쪽 처리로만 됩니다("driver only supports offload mode"). [사실, Fedora 버그] https://bugzilla.redhat.com/show_bug.cgi?id=2226569 . 그래서 WPA2를 먼저 하고 WPA3는 7단계로 미룹니다.

---

## 4. 우리 쪽 준비 작업

### 4.1 전원 켜기 (지금 없음)

- 1단계(시험): 맥북 Python 모듈에서 손님 OS 시작 전에 SMC 키 `gP0d`에 켜기 값을 씁니다. 지금 `p.pcie_init()`을 부르는 모듈(`nwoas_scripts/usb-s174/guest_map.py` 7행)과 같은 자리입니다. 반드시 UEFI가 포트를 올리기 **전**이어야 합니다.
- 맥북 없는 부팅 단계: 하이퍼바이저 C 코드에서 `smc_init()` + `smc_write_u32()`(이미 있음, `dcp.c` 50~53행 선례)로 옮깁니다.
- [가설] 이미 돌고 있는 SMC에 m1n1이 다시 연결을 맺어도 안전한지 모릅니다. 화면(DCP) 초기화 경로가 SMC를 이미 쓰는지 먼저 확인합니다.
- Windows에는 SMC 드라이버가 없으므로, Windows 드라이버는 칩 전원을 껐다 켤 수 없습니다. 펌웨어가 멈췄을 때 되살리는 방법은 PCIe 기능 리셋 정도로 제한됩니다[가설].

### 4.2 PCI 주소 고정 (USB-A 보호)

- UEFI에는 PCI 장치를 자동으로 열거하고 주소를 배정하는 표준 드라이버가 들어 있습니다(`PciHostBridgeDxe`, `PciBusDxe`). [사실] `apple_silicon_platforms_mu/Platform/MacMini2020Pkg/MacMini2020.fdf` 160~161행.
- USB-A 컨트롤러 주소 `0x6C0000000`은 고정값으로 쓰입니다: DSDT 366~372·416~422행, `AppleDartIoMmuDxe.c` 1083행, 하이퍼바이저 `hv_exc.c` 689행, 맥북 모듈 `usb-s174/guest_map.py` 8행. [사실]
- [가설] Wi-Fi 칩이 나타나면 UEFI가 큰 영역부터 배정하면서 USB-A 주소가 밀릴 수 있습니다. 1단계 검증에 "USB-A 주소 그대로"를 넣었습니다. 밀리면 UEFI 플랫폼 코드에서 Wi-Fi 영역을 USB-A 뒤(예: `0x6C0100000` 이후)에 고정 배정합니다.

### 4.3 Windows에 장치 보이기

- PCI 버스 전체(PCI0)는 계속 숨겨 둡니다(`_STA=0`, DSDT 241행). 숨긴 이유는 Windows가 PCI 장치를 시작하지 않고, Apple DART 드라이버도 없기 때문입니다. [사실] DSDT 225~233행.
- USB-A와 같은 방식으로 **ACPI 플랫폼 장치**를 하나 추가합니다(DSDT 345~457행의 `SCB0/XHC0`이 본보기). [가설 — 설계안]
  - 위치: `SCB0` 안(그래야 `SCB0`의 `_DMA` 48 GB 범위를 물려받음, DSDT 381~395행).
  - 이름: 예 `_HID "NWOA4378"`. 참고 프로젝트도 자체 장치 번호 `ACPI\APPL0001`에 ARM64 KMDF 드라이버를 붙였습니다. [사실] `external/windows-on-m1/documentation/APPLE_INPUT.md` 49~51행.
  - `_INI`: 설정 공간 `0x690100000`에 명령 레지스터 `0x0006`(메모리 접근 + DMA 허용). USB-A `_INI`(408~413행)와 같은 방식.
  - `_CRS`: Wi-Fi BAR 영역들 + `Interrupt(Level, ActiveHigh) { 695 }`. `SCB0`의 `_CRS`(366~379행)에 Wi-Fi 영역도 내어 주도록 넓혀야 합니다. 넓히지 않으면 Windows가 자원을 배정하지 않습니다. [사실] 같은 문제가 USB-A에서 있었음(DSDT 357~365행 주석).
  - 블루투스 기능(1번)은 처음엔 넣지 않습니다.

### 4.4 DART(장치용 주소 변환)

- 포트 0의 DART는 `0x681008000`입니다. [사실] dts 3149~3157행.
- UEFI는 이미 이 DART까지 포함해 PCIe DART 3개의 16개 스트림 모두를 같은 주소 변환표로 설정합니다. [사실] `AppleDartIoMmuDxe.c` 433~435행, 720~739행. 높은 메모리 영역도 그대로(같은 주소로) 보이게 하는 설정이 켜져 있습니다(`NWOAS_WIDE_DART 1`, 70~81행).
- Linux DT 기준 Wi-Fi(요청자 번호 `0x100`)는 DART `0xb0`의 스트림 1을 쓰고, 마스크 `0xff00` 때문에 블루투스(기능 1)도 같은 스트림입니다. [사실] dts 3191~3192행.
- 맥북 모듈이 DART 영역 `0x681000000`~`0x684000000`을 감시(BYPASS) 범위로 걸어 둡니다. [사실] `usb-s174/guest_map.py` 9행. Wi-Fi에 영향이 있는지는 [가설] — 영향 없을 것으로 봅니다.
- USB-A 컨트롤러는 실제로 주소 상위 비트를 잘라 버리는 문제가 있었습니다(`hv_vm.c` 2540~2558행). BCM4378이 64비트 DMA 주소를 제대로 쓰는지는 [가설] — 4단계에서 높은 주소 버퍼로 확인합니다.

### 4.5 인터럽트

- MSI는 쓰지 않습니다. Apple에는 Windows가 쓰는 GIC ITS가 없고, DSDT도 일부러 INTx만 쓰도록 했습니다. [사실] DSDT 217~218·274~275행. `brcmfmac`도 MSI가 안 되면 일반 인터럽트로 동작합니다[가설: `pci_enable_msi` 반환값을 확인하지 않는 구조로 봄].
- UEFI는 포트가 올라온 경우 포트 0의 INTx도 엽니다. [사실] `AppleSiliconPciPlatformDxe.c` 355~367행.
- 하이퍼바이저의 일반 경로는 수준형 장치 인터럽트를 받으면 원래 선을 막고 게스트에 넣은 뒤, 게스트가 끝내면 다시 엽니다. [사실] `hv_exc.c` 2227~2245행.
- **주의**: USB-A(698)는 이 일반 경로로 인터럽트가 게스트에 도착하지 않아, 하이퍼바이저가 장치 레지스터를 직접 읽어 대신 넣어 주는 별도 코드를 만들어야 했습니다. [사실] `hv_exc.c` 675~690행. 695도 같을 수 있습니다[가설]. 그러면 같은 방식으로 BCM4378 메일박스 인터럽트 레지스터를 읽어 695를 넣어 주는 코드를 추가합니다.

### 4.6 USB-A 손상에서 얻은 교훈 (캐시 일관성)

- USB-A 컨트롤러는 PCIe 장치 제어 레지스터에 No Snoop(캐시를 확인하지 않는 DMA)과 Relaxed Ordering(순서 완화)이 켜져 있었고, 이것이 파일 손상의 가장 유력한 원인입니다(아직 [가설], S232 미검증). [사실: 레지스터 값 `0x2810`] `nwoas_scripts/usb-s232/README.md`, `plan-20261115/usb.md`.
- Wi-Fi 칩도 전원을 켠 뒤 같은 레지스터를 읽어 기록하고, 우리 드라이버가 DMA를 켜기 전에 두 비트를 직접 끄고 되읽어 확인합니다. 우리 드라이버이므로 이 처리를 드라이버 안에 넣을 수 있습니다.
- DT는 이 PCIe를 캐시 일관(`dma-coherent`, dts 3200행)으로 적지만, 우리 ACPI의 `SCB0`은 `_CCA=0`(일관 아님)으로 선언되어 있습니다(DSDT 348행). Wi-Fi 장치도 처음에는 `_CCA=0`으로 시작합니다(Windows가 캐시 정리를 직접 함). [가설: 이 선택이 맞는지]
- 6단계 데이터 시험에서는 받기 체크섬 오프로드를 끄고 켜고 둘 다, 파일 해시 반복 비교를 합니다(`network.md` 4단계와 같은 기준).

---

## 5. 단계별 계획 (담당·검증)

담당: **Claude** = 코드 작성(서브에이전트 기본 Sonnet, 하이퍼바이저·인터럽트·DMA·부팅 흐름 변경은 Opus), 실행은 콘치님 승인 후. **콘치님** = 실제 맥미니 부팅·관찰·AP 준비·결정.
모든 단계는 "빌드 성공 ≠ 동작"입니다. 통과 판정은 각 단계의 실기 검증으로만 합니다.

### 1단계. 전원 켜기와 연결 확인 (Windows 수정 없음)

- 담당: Claude(맥북 Python 모듈, 약 50줄[가설]), 콘치님(부팅)
- 할 일: 손님 OS 시작 전 SMC `gP0d` 켜기 → 평소처럼 UEFI가 포트를 올림 → 맥북에서 설정 공간 `0x690100000`, `0x690101000`과 USB-A `0x690200000`을 읽어 기록. 처음에는 Windows까지 가지 않고 UEFI 화면에서 멈춘 상태로 읽어도 됩니다.
- 통과 기준:

| 지표 | 기준 |
|---|---|
| 버스 1 기능 0 장치 번호 | `0x442514e4` |
| 버스 1 기능 1 장치 번호 | `0x5f6914e4` |
| USB-A BAR | 그대로 `0xC0000000`(CPU `0x6C0000000`) |
| Wi-Fi BAR 크기·주소 | 기록(합격 기준 아님) |
| Wi-Fi 장치 제어 레지스터(No Snoop·Relaxed Ordering) | 기록 |
| USB 키보드·USB-A 동작 | 이전과 같음 |

- 실패하면: SMC 응답 값, 포트 0 연결 상태 레지스터, UEFI 기록의 포트 0 메시지를 봅니다. 전원 켠 뒤 기다리는 시간을 늘려 봅니다.

### 2단계. 펌웨어 올리고 "준비됨" 받기 (Windows 없음, 진단용 실행)

- 담당: Claude(맥북 Python, `bwfm`·`brcmfmac`의 칩 제어·펌웨어 올리기 부분을 옮김, 600~900줄[가설]), 콘치님(부팅)
- 할 일: Windows를 띄우지 않는 진단용 실행에서 BAR 메모리 읽기·쓰기만으로: 칩 번호 읽기 → 내부 코어 목록 읽기 → OTP 읽기(제조사·버전) → 칩 펌웨어·NVRAM·무작위 시드를 칩 메모리에 쓰기 → ARM 코어 시작 → 펌웨어가 공유 영역 주소를 쓰는지 기다림 → 펌웨어 콘솔 문자열 읽기. 끝나면 칩을 리셋해 둡니다.
- 통과 기준:

| 지표 | 기준 |
|---|---|
| 칩 번호 / 리비전 | 4378 / 3 (B1) |
| OTP 모듈·제조사·버전 | 읽힘, NVRAM 파일 하나로 정해짐 |
| 펌웨어 시작 후 공유 영역 주소 | 0이 아닌 값, 제한 시간 안 (기준 시간은 `brcmfmac` 값을 따름) |
| 펌웨어 콘솔 | 버전 문자열 보임 |

- 이 단계로 펌웨어 파일·NVRAM 선택·보정 데이터 문제를 Windows 드라이버 없이 걸러 냅니다.

### 3단계. Windows에 장치로 보이기

- 담당: Claude(DSDT 약 60줄 + 필요 시 UEFI 주소 고정), 콘치님(부팅)
- 할 일: 4.3의 ACPI 장치 추가, `SCB0` 자원 범위 확장.
- 검증(NWOAS Agent 원격 명령, 결과를 파일로 받아 맥북에서 검색): `pnputil /enum-devices /problem`, `pnputil /enum-devices /ids` 결과에 `ACPI\NWOA4378`이 있고, 드라이버 없음(코드 28)이 정상. 자원(메모리 영역·인터럽트 695)이 배정됐는지 확인. 기존 USB-A·키보드가 그대로 동작.

### 4단계. 최소 Windows 드라이버 (칩이 Windows에서 살아 있음을 확인)

- 담당: Claude(KMDF, 2단계 Python을 C로 옮김 + 명령 링 + 인터럽트, 3,000~4,000줄[가설]; 인터럽트·DMA 부분은 Opus), 콘치님(시험 서명 모드 결정·설치 승인·부팅)
- 할 일: 펌웨어 올리기 → 첫 DMA(공용 링을 Windows 메모리에 잡음) → 펌웨어 "준비" 이벤트 → `ver`, `cur_etheraddr` 읽기. 기록은 파일로 남겨 원격 명령으로 가져옵니다.
- 설치: 시험 서명(`bcdedit /set testsigning on`)과 `pnputil /add-driver`. 참고 프로젝트도 같은 방식으로 설치했습니다. [사실] `APPLE_INPUT.md` 111~119행. 시험 서명 모드는 콘치님 승인이 필요한 부팅 설정 변경입니다.
- 통과 기준:

| 지표 | 기준 |
|---|---|
| 펌웨어 버전 문자열 | 2단계와 같음 |
| MAC 주소 | ADT 값과 같음(문서에는 적지 않음) |
| 인터럽트 695 수신 | 0보다 큼 (안 오면 4.5의 대신 넣어 주는 코드 추가) |
| 높은 메모리(4 GB 이상) 버퍼로 명령 왕복 | 성공 |
| 부팅 반복 (회) | 5회 연속 같은 결과 |

### 5단계. 주변 AP 검색

- 담당: Claude(WiFiCx 골격 + 검색, 약 3,000줄[가설]), 콘치님(부팅, 같은 자리에서 맥 검색 결과와 비교)
- 검증: `netsh wlan show networks mode=bssid`에 AP 목록이 나오고, 맥북에서 같은 시간에 본 목록과 대부분 겹침. 2.4 GHz·5 GHz 둘 다 나옴.

### 6단계. WPA2 접속과 데이터

- 담당: Claude(접속·키 설치·송수신 큐, 3,000~5,000줄[가설]), 콘치님(AP 정보 입력은 Windows 화면에서 직접)
- 검증:

| 지표 | 기준 |
|---|---|
| 접속 | `netsh wlan show interfaces` 상태 connected |
| IP | DHCP로 받음 |
| 받은 총량 (GB) | 20 이상 |
| SHA256 불일치 (회) | 0 |
| 드라이버·펌웨어 재시작 (회) | 0 |

- Wi-Fi 비밀번호는 콘치님이 Windows 화면에서 직접 넣습니다. Claude는 입력하지 않습니다.

### 7단계. 속도·안정성·WPA3

- 담당: Claude, 콘치님
- 같은 자리·같은 AP에서 macOS로 먼저 재 둔 속도를 기준으로 비교합니다(목표는 그 값의 비율로 정하고, 숫자는 측정 뒤 정함). 장시간 연결, 절전 끈 상태 → 켠 상태, WPA3-SAE 시도.

### 8단계. 서명

- 시험 서명 모드는 Vanguard가 거부한다는 보고가 있습니다[가설, 커뮤니티 보고, `network.md` 1장]. 정식 서명(Microsoft 증명 서명)은 EV 인증서와 Partner Center 계정이 필요합니다[가설: 비용·조건은 확인 필요] https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-attestation . 콘치님 결정 사항입니다.

### 전체 규모 [가설]

| 항목 | 값 |
|---|---|
| 새로 쓸 코드 (줄) | 약 10,000~15,000 |
| 참고: OpenBSD 전체 드라이버 (줄) | 5,920 [사실] (Windows는 무선 표준 처리를 WiFiCx에 맞춰 다시 짜야 해서 더 늘어남) |
| 1~6단계 기간 (주) | 약 11~20 |
| 블루투스 | 별도 작업, 이 계획 밖 |

---

## 6. 위험과 모르는 점

| 항목 | 내용 | 확인 방법 |
|---|---|---|
| SMC 쓰기 안전성 | 전원 관리 칩에 잘못 쓰면 전원·열 관리에 영향. 키 `gP0d`·값 `0x800001`은 DT·m1n1 선례 기준이지만 실측 전 | 1단계, 한 키만 쓰고 되읽기 |
| SMC 재연결 | 이미 돌고 있는 SMC에 다시 연결해도 되는지 | 1단계 기록 |
| 전원만으로 연결이 되는가 | 클록 요청·`manual-enable` 등 다른 조건이 있을 수 있음 | 1단계 |
| USB-A 주소 밀림 | Wi-Fi가 나타나면 UEFI 자동 배정이 바뀔 수 있음 | 1단계 표 |
| 인터럽트 695 전달 | USB-A처럼 일반 경로로 안 올 수 있음 | 4단계 |
| 캐시 일관성 | USB-A 손상과 같은 문제 가능 | 1단계 레지스터 기록, 6단계 해시 시험 |
| 64비트 DMA | 칩이 높은 주소를 제대로 쓰는지 | 4단계 |
| 펌웨어 판 | 맥미니 macOS 26판과 맥북 27.2.1판 중 무엇을 쓸지, `.trx` 그대로 되는지 | 2단계 |
| NVRAM 선택 | OTP 값과 파일 이름 대응 | 2단계 |
| 보정 데이터 | 올리는 명령(`calload`)과 시점 | 2단계·4단계 |
| WiFiCx 데이터 형식 | WiFiCx 송수신 큐가 이더넷 형식인지 802.11 형식인지 확인 안 함(풀맥 칩은 이더넷 형식) | 5단계 전에 문서 확인 |
| WPA3 | Apple 펌웨어가 호스트가 만든 SAE 프레임을 보내 주는지 모름. Linux는 펌웨어 쪽 처리만 씀 | 7단계 |
| 펌웨어 멈춤 회복 | Windows에서 칩 전원을 다시 켤 방법 없음 | 4단계 이후 설계 |
| 블루투스와 같이 쓰기 | 블루투스 드라이버 없이 Wi-Fi만 써도 되는지 | 5~6단계 관찰 |
| 재시작 | Windows 재시작 때 맥미니 전체가 리셋되므로 매번 전원 켜기부터 다시 함 [사실] `NWOAS-STATUS-2026-11-15.md` 36행 | 매 부팅 |
| 서명·Vanguard | 시험 서명 거부 보고 | 8단계 |
| 검색 범위 | 기존 드라이버 없음은 제 검색 도구 결과 기준 | 새 소식 생기면 다시 확인 |

---

## 7. 출처

프로젝트 파일 (경로는 `/Users/angigyeom/Desktop/DEV/NWOAS/` 기준)

- `nwoas_scripts/dtb-s221/recovered-j274.dts` 12·18·22행(별칭), 1915~1924행(전원 영역), 2415행(PCIe 핀), 2650~2658행(SMC GPIO), 3149~3157행(포트 0 DART), 3181~3231행(PCIe·Wi-Fi·블루투스)
- `nwoas_scripts/dtb-s221/upstream-68.pre.dts` 2758~2765행(Asahi 원본 `pwren-gpios`)
- `nwoas_scripts/dtb-s221/upstream-devicetrees/Bindings/net/wireless/brcm,bcm4329-fmac.yaml` 84~107행(`brcm,cal-blob`, `brcm,board-type`, `apple,antenna-sku`)
- `experiments/tahoe-afk-20260906/current.adt` (J274 ADT: `/arm-io/wlan`, `/arm-io/apcie/pci-bridge0`, `/arm-io/dart-apcie0`, `/product`)
- `apple_silicon_platforms_mu/Platform/MacMini2020Pkg/AcpiTables/DSDT.asl` 215~241행(PCI0 숨김), 244~270행(메모리 창), 292~307행(포트 0 인터럽트), 345~457행(SCB0/XHC0)
- `apple_silicon_platforms_mu/Platform/MacMini2020Pkg/MacMini2020.fdf` 160~161행
- `apple_silicon_platforms_mu/Silicon/Apple/AppleSiliconPkg/Drivers/AppleSiliconPciPlatformDxe/AppleSiliconPciPlatformDxe.c` 300·322·330·355~367행
- `apple_silicon_platforms_mu/Silicon/Apple/AppleSiliconPkg/Drivers/AppleDartIoMmuDxe/AppleDartIoMmuDxe.c` 70~81·433~435·720~739·1083행
- `m1n1-usb-s232/src/pcie.c` 465~740행, `smc.c` 123~180행, `dcp.c` 50~53행, `hv_exc.c` 675~690·2227~2245행, `hv_vm.c` 2540~2558행
- `m1n1-layout-s231/src/kboot.c` 734~757·974~1015행, `m1n1-layout-s231/proxyclient/experiments/smc.py` 20~29행, `m1n1-layout-s231/nwoas_capture_hook.py` 150~200행
- `nwoas_scripts/usb-s174/guest_map.py` 7~9행, `nwoas_scripts/usb-s232/README.md`, `nwoas_scripts/plan-20261115/usb.md`, `nwoas_scripts/plan-20261115/network.md`
- `nwoas_scripts/logs/` (포트 0 하위 버스 1 기록 46회, 버스 1 장치 0회, 버스 2 69회, 버스 3 71회)
- `NWOAS-STATUS-2026-11-15.md` 13·36행
- `external/windows-on-m1/documentation/LIMITATIONS.md` 18~19행, `documentation/APPLE_INPUT.md` 49~51·111~119행 (MIT, 읽기만 함)
- 맥북 macOS 27.2.1: `/usr/share/firmware/wifi/C-4378__s-B1/`, `/usr/share/firmware/bluetooth/`

웹

- 맥미니 M1 사양: https://support.apple.com/kb/SP823
- Boot Camp는 Intel 맥 전용: https://support.apple.com/102622
- Windows ARM64는 ARM64 커널 드라이버만: https://learn.microsoft.com/windows/arm/faq
- WiFiCx 소개: https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/wifi-wdf-class-extension-wificx
- WiFiCx 드라이버 작성: https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/writing-a-wificx-client-driver
- WiFiCx WPA3-SAE: https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/wificx-wpa3-sae-authentication
- NetAdapterCx: https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/
- WiFiCx 예제 PR: https://github.com/microsoft/Windows-driver-samples/pull/1394
- 증명 서명: https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-attestation
- OpenBSD `bwfm`: https://man.openbsd.org/bwfm.4 , https://github.com/openbsd/src/blob/master/sys/dev/ic/bwfm.c , https://github.com/openbsd/src/blob/master/sys/dev/pci/if_bwfm_pci.c
- OpenBSD M1 사용자 보고(2차 출처): https://hn.jakechampion.name/reader/http://kernelpanic.life/hardware/openbsd-m1-macbook-air.html
- OpenBSD 맥미니 M1 Wi-Fi와 SMC 문제(커뮤니티): https://reddit.birdcat.cafe/r/openbsd/comments/1igc0an/wifi_not_recognized_mac_mini_m1_openbsd_76
- Linux `brcmfmac` PCIe: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c
- Linux `pcie-apple.c`: https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/drivers/pci/controller/pcie-apple.c
- Linux `hci_bcm4377`: https://code.woboq.org/linux/linux/drivers/bluetooth/hci_bcm4377.c.html
- `brcmfmac` 개요: https://wireless.docs.kernel.org/en/latest/en/users/drivers/brcm80211.html
- Apple 맥용 `brcmfmac` 패치 묶음: https://lore.kernel.org/linux-acpi/20211226153624.162281-1-marcan@marcan.st/
- Apple 칩 WPA3가 펌웨어 쪽 처리만 된다는 Fedora 버그: https://bugzilla.redhat.com/show_bug.cgi?id=2226569
- Raspberry Pi용 Windows Wi-Fi 미지원: https://github.com/worproject/RPi-Windows-Drivers
- Broadcom Windows 드라이버 목록(약한 근거): https://treexy.com/products/driver-fusion/database/network-adapters/broadcom/802.11ac-wireless-pcie-full-dongle-adapter/
