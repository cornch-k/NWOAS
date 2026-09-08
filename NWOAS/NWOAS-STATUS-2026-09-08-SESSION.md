# NWOAS 2026-09-08 S106–S109 진단 기록

## 상태
Windows 설치 완료/내장 SSD 부팅은 아직 미확인. Tahoe 펌웨어 유지.
맥미니 J274의 S103 m1n1 프록시에서 SSD 읽기 진단을 수행했다.
WINARM2 설치 USB는 맥북에 연결되어 있다. 맥북 데이터 삭제 없음.

## 확인된 결과
- USB install.swm: 3,987,720,636 bytes, SHA256 `8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c`.
- USB install2.swm SHA256 `62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963`.
- NTFS t1.swm 전체 읽기 완료(279.96초), SHA256 `e3734bc93c439dc28a0a695b5cc6f26f7d4f2fc6041ee5298b06f60e9ad880a1`. 원본과 불일치.
- MFT record 3969, single extent LCN 2054436, 973565 clusters, NTFS partition LBA 53920000, sector/cluster 4096B.
- 첫 불일치: file offset 1620725760, physical namespace LBA 56370121. 4096B 전체가 0.
- 단일/다중 블록 반복 읽기가 동일. 앞 16개 및 뒤 32개 블록은 원본과 일치.
- NTFS allocated=3987722240, real=initialized=3987720636, attribute flags=0. VDL 미초기화 구간으로 설명되지 않음.
- S107: high contiguous/scattered, low alias, mixed PRP mappings × 1/2/3/8/16 blocks × 8 repetitions = 160 direct-read commands. 일반 읽기와 모두 일치. 저장장치 쓰기 없음. 이 소규모 idle 테스트만으로 guest 실행 중 DMA/캐시 문제를 배제할 수 없음.
- NVMe Python suite 29 PASS (S106).
- S109 전체 블록 비교 완료: 305개 4KB 블록 불일치(그중 6개 전체 0), 294.26초. 전체 SHA256은 앞선 S106과 동일. S110에서 해당 305개를 각각 재읽기해 모두 동일한 해시임을 확인했다. 저장장치 쓰기 없음.

## 이전 문서의 해석 정정
S103에서 USB 원본을 NVMe에 복사한 뒤 DISM으로 적용해도 실패했다는 사실만으로 USB 경로가 배제되지는 않는다. 사본 자체가 원본과 다르기 때문이다. 저장된 내용 차이/호스트 읽기 경로/게스트 읽기 경로를 추가 분리해야 한다. 0 블록의 발생 원인은 아직 미확정.

## 다음 WinPE 진단 준비
`nwoas_scripts/nvme-s106/`의 NWHASH.EXE는 Windows ARM64, kernel32/bcrypt만 사용하는 CNG SHA256 유틸리티. 빌드/PE imports 확인, Windows 실행은 아직 미검증. 자체 abc SHA256 self-test 포함.
WINARM2 root autounattend.xml의 Order 7을 HASH106.CMD로 교체. 기존 XML은 logs/s106-usb-backup-20260908-202954에 백업.
S100.TAG는 없음: 기존 자동 포맷/설치 루틴은 무장되지 않음.
HASH106은 USB install.swm 2회, NVMe t1.swm 2회/t2.swm 1회 검사 후 USB NWOAS-SETUP-LOGS에 기록한다. 포맷/DISM/bcdboot 없음. NTFS 드라이브 문자만 필요한 경우 R로 할당.
현재 원본 사본 손상을 보존해 guest 해시와 host 해시를 비교한다. 원인 확정 전에 파일을 임의 수정하지 않음.

## 산출물
- read_paths_s107.py, ntfs_boundary_s108.py, ntfs_full_compare_s109.py (nwoas_scripts/ans2-s87)
- nwoas_scripts/logs/s106-s109-20260908/ : 완료 진단 로그와 JSON
- 현용 S103 binary SHA256 fe21c195bc4b601f6bd8e95bde74f4a83b5de7c8b040ca3caa5f210ef12e280a
- Windows 재부팅에는 S103 harness + S102 payload (NWOAS_EXCLUDE_WINDOW=1); HIDE_FULL=1 사용하지 말 것.

## S110 재검사
305개 모두 재현. 0 아닌 불일치 블록들도 블록 대부분(4061–4091 bytes)이 달라 부분 비트 손상 형태는 아님. S111 원본 전체 4KB 블록 해시 검색 완료. 원인 확정 전에는 기존 사본을 복구/덮어쓰지 않는다.

## S111 핵심 결과
305개 불일치 중 6개는 0 블록. 나머지 299개 중 298개가 원본 install.swm의 **더 이른 위치에 있던 다른 4KB 페이지**와 SHA256 일치.
예: 사본 offset 1646972928의 내용은 원본 offset 7426048과 일치(delta 1639546880). 다수 delta는 1MiB 배수.
임의 비트 오류보다는 재사용 버퍼/캐시/DMA 페이지의 오래된 내용 재전송 가설을 지지한다. USB 읽기, guest RAM, NVMe 쓰기 중 정확한 단계는 아직 미확정.
다음 S106 WinPE 자동 해시(USB 두 번, NVMe 두 번)로 경로를 분리한다. 둘째 읽기는 Windows 파일 캐시에 적중할 수 있으므로 독립적인 두 물리 읽기로 간주하지 않는다.
맥미니는 S103 proxy idle, ANS shutdown. 부팅하지 않음. USB 이동 후 nvme-s103-guest-test.sh로 시작하며 USB diagnostic TAG/포맷 TAG 부재를 사전 확인해야 한다.

## S112 Windows 안에서 해시 검사 (2026-09-08 20:36 시작)
사용자가 WINARM2를 맥미니로 이동 완료. S103 표준 하네스로 한 번 chainload/guest 실행.
로그: `nwoas_scripts/logs/nvme-s103-20260908-203600.pL1eaG`.
사용자 화면: 검은 설치 준비 화면과 빈 CMD. 출력은 파일로 리디렉션하므로 이 화면만으로 실패 판단 금지.
NVMe t1 extent 55974436 이후 순차 READ(16 blocks), direct fallback=0을 확인. 해시 도구가 사본을 읽는 흐름과 일치하지만 최종 해시 결과는 아직 미수집.
Windows 게스트 실행 중에는 다른 프로세스로 시리얼에 접근하지 말 것. 하네스는 exec session 87591에서 실행 중.
S106 진단 출력은 USB `NWOAS-SETUP-LOGS/s106-*`에 저장된다. 원본/사본 expected hash mismatch는 현재 사본 손상이 확인됐으므로 예상 가능한 결과이며, 실제 SHA256 값으로 host 결과와 비교할 것.

S112 진행 추가: t1 두 번의 순차 읽기(끝 LBA 56937844 샘플 이후 파일 시작으로 복귀 관찰) 후 t2 영역 LBA 55022951–55118951 샘플 관찰. 직접 I/O 성공 카운터 132000 이상, fallback=0, WATCHDOG timeout=0, CFS=0. 이후 대용량 순차 요청은 멈춘 상태. 실제 SHA256은 아직 USB 로그 미회수. Windows 게스트는 실행 상태로 유지. 사용자가 WinPE를 종료한 후 USB를 맥북으로 옮겨야 로그를 직접 읽을 수 있다. 요청 완료 상태와 내용 무결성을 혼동하지 말 것.

## S112 로그 회수 결과 / S113 준비 (20:52 KST)
USB 로그 보관: nwoas_scripts/logs/s112-usb-20260908/.
- USB first: d61bb086817e60a75d1e0a323541e02aea9592d7862de5c57fd1ac9089c73b1f (3987720636 bytes), EXIT 1.
- USB second: a53f0272764894a41a14972bb0841bacd6c853c8ac8b05bccfa63a017ece5871 (동일 size), EXIT 1. 원본/서로 불일치.
- nvme-t1-second: e3734bc93c439dc28a0a695b5cc6f26f7d4f2fc6041ee5298b06f60e9ad880a1, 호스트 전체 읽기와 동일.
- nvme-t2: 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963, 원본과 동일.
- nvme-t1-first.txt는 t2 출력 + 후미 NUL로 바뀌어 있다. 정상 첫 번째 t1 결과로 인용하지 말 것. status의 t1_first=1과 파일 내용 EXIT 0이 모순. USB 쓰기/로그 저장 경로도 의심.
- USB를 맥북으로 옮겨 원본 두 파일을 재해시: 두 개 모두 기존 정상 hash. NWHASH.EXE도 로컬 빌드와 바이트 일치.
- WinPE GetTickCount64/시각은 호스트 wall time과 크게 다름. 출력 elapsed_ms를 실제 처리량/벤치마크 증거로 사용하지 말 것.

S113은 No Snoop bit(PCIe Device Control bit 11)만 끄는 단일 하드웨어 변수 실험. 캐시 유지 코드/Relaxed Ordering/DART/UEFI/NVMe 동작은 변경하지 않음.
현용 hv_vm.c에는 기본값 0인 NWOAS_FL_SNOOP_TEST 추가. kernel FL1100 doorbell 직전 PCI identity 0x11001b73, capability ID at 0x70 == 0x10을 확인한 뒤 offset 0x78에 16-bit write로 bit11만 clear. Device Status W1C에 쓰지 않음. 매번 readback 확인, S113 SNOOP before/after/verified 로그. guard 실패 시 변경하지 않고 SKIP. 런타임 적용/효과 아직 미검증.
PCI 표준 비트 참고: https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/pci_regs.h (PCI_EXP_DEVCTL_NOSNOOP_EN 0x0800). 이 비트 변경이 Apple PCI/DART를 coherent로 만든다는 보장은 없으며 실험 결과로 판단.

S113 binary: m1n1_windows/build/m1n1-s113-flsnoop.bin, 2129920 bytes, SHA256 02c94b4f81a5501ec692732c8452abf341314a3aa1bd25ff9430a762860f4d03.
빌드: make -C m1n1_windows -W src/hv_vm.c -j8 NAME=m1n1-s113-flsnoop EXTRA_CFLAGS=-DNWOAS_FL_SNOOP_TEST=1 build/m1n1-s113-flsnoop.bin.
기존 4개 unused-function warning 외 빌드 실패 없음. 실험 빌드 후 shared build/hv_vm.o는 EXTRA_CFLAGS=로 다시 컴파일하여 default macro 0으로 돌려놓음. 재빌드 시 -W src/hv_vm.c 필수(기존 Makefile은 플래그 변경을 추적하지 않음).
하네스: nwoas_scripts/usb-s113-guest-test.sh (S113 image hash 고정, 기존 S102 payload).

HASH113.CMD: ARM64 WinPE/정확한 TAG 확인. RAM X:\Windows\Temp에 결과를 모아 USB 원본 install.swm 두 번 검사. 최종 보고서를 USB에 한 번 저장한 뒤 15초 후 wpeutil shutdown. SSD 파일 읽기/복사/포맷/이미지 적용 없음. 출력 복사 실패 시 자동 종료하지 않고 pause. USB 진단 출력도 신뢰성 검증 대상이므로 원본 로그를 항상 보존.
USB root Order 7을 HASH113/S113.TAG로 변경. S100.TAG/S106.TAG는 없음. 기존 XML 백업: logs/s113-usb-backup-20260908-205208.

Mini 상태: WinPE shutdown 후 기존 guest 프로세스는 Device not configured로 끝남. CDC proxy NOP READY 확인. idle 상태에서 PCI init 후 config read는 링크 enumeration 전이라 guarded abort/0xabad1dea를 반환했음; PCI ID를 확인한 결과로 취급하지 않음. 추가 config write 없음. S113 guard는 Windows PCI enumeration 이후 doorbell 시점에만 실행.
다음: USB를 맥미니에 꽂은 뒤 S113 하네스 실행. verified=1 로그와 USB 두 hash의 정상 원본 일치 여부를 확인. 효과 미확인이므로 설치 재시도는 아직 하지 않음.

### S113 실제 부팅 (20:53:55 KST)
사용자가 USB 이동 완료. usb-s113-guest-test.sh 실행, host log `nwoas_scripts/logs/usb-s113-20260908-205355.MYmkr3`, exec session 5733.
실제 hardware 확인: `S113 SNOOP before=2810 after=2010 verified=1 fault=0`, 다음 3회 2010 유지. PCI identity/capability guard 통과. Windows NVMe 초기 식별/마운트 요청 성공.
효과 판단용 USB 해시와 자동 종료는 아직 대기 중. 로그 문자열 "No Snoop disabled experiment"만으로 효과를 주장하지 말고 실제 REPORT.TXT를 회수해야 한다.

S113 자동 종료 단계 관찰 완료: xHCI halt, NVMe queue 삭제 op00/op04, CC=0x464001(SHN 포함) 후 CDC 연결 해제. host session 5733 exit1/Device not configured는 이 종료 시퀀스 뒤 발생. 해시 성공/실패는 아직 미수집이고 exit1을 해시 실패 코드로 취급하면 안 된다. USB `NWOAS-SETUP-LOGS/s113-*/REPORT.TXT` 회수 필요. No Snoop readback verified1/fault0 4회, WATCHDOG timeout0, CFS0.

### S113 결과 회수 / S114
S113 REPORT.TXT: first 31a1d2c052ecf8855cc45e8a19fa1466ef73135b5dc9aa5a08af678d21b29ede; second 460d2235ee38798e3c625b697eea2ef1ff671eac2fea315b3523cbb13ca71103. 둘 다 3987720636 bytes, EXIT1. No Snoop 해제만으로는 해결되지 않음. RAM에서 조립한 단일 REPORT는 끝까지 정상 텍스트 형태로 회수됨.
맥북 재해시에서 USB install.swm/install2.swm 정상 원본 해시 유지. NWHASH.EXE도 빌드와 바이트 일치.

새 가설: legacy nwoas_fl_clean_cmd는 매 FL1100 doorbell에서 명령 링 시작으로부터 0x8000, DCBAA 시작으로부터 0x4000, output context에서 0x1000을 clean+invalidate한다. 할당 크기를 증명하지 않은 주변 영역 또는 device-authored output context까지 CPU의 dirty cache가 DMA 데이터에 덮어쓸 가능성이 있음. 원인 미확정이며 이번 실험으로 분리한다.
S114: 기본값 0인 NWOAS_FL_SKIP_LEGACY_CLEAN을 1로 빌드해 그 함수 호출만 제외. ERST/event/IRQ 관련 유지 동작, DART/UEFI/NVMe는 그대로. No Snoop 실험은 default0으로 되돌려 S103에 대한 단일 변수 비교. `S114 legacy doorbell cache sweep disabled` 로그로 적용 확인. 키보드/부팅이 회귀할 수도 있으므로 장치 인식 및 진단 실행 여부도 확인.
Binary m1n1_windows/build/m1n1-s114-nosweep.bin, 2129920 bytes, SHA256 0f09061bc8ecdba7cb6154b196303cd62611887032c05f2cef19799bb55d0450.
빌드 make -C m1n1_windows -W src/hv_vm.c -j8 NAME=m1n1-s114-nosweep EXTRA_CFLAGS=-DNWOAS_FL_SKIP_LEGACY_CLEAN=1 build/m1n1-s114-nosweep.bin.
5 unused-function warnings (기존4 + 실험에서 호출 제외한 nwoas_fl_clean_cmd), 에러 없음. 공유 hv_vm.o는 이후 default flags로 재컴파일함. S103/S113 파일 보존.
하네스 nwoas_scripts/usb-s114-guest-test.sh, USB HASH114.CMD/S114.TAG 준비. S113과 동일하게 RAM 보고서, USB 한 번 복사, 자동 종료. S100/S106/S113 TAG 부재. USB 이전 XML 백업 logs/s114-usb-backup-20260908-210103. 현재 S114 하드웨어 실행은 아직 안 함.

### S114 실기기 실행 시작 (2026-09-08 21:02:29 KST)
사용자 USB 연결 완료 후 usb-s114-guest-test.sh 실행. log: nwoas_scripts/logs/usb-s114-20260908-210229.a0hyTr, exec session 39649. Windows 초기 NVMe 읽기 요청 진행. 최종 USB 해시 미수집. 자동 종료/REPORT 회수까지 관찰할 것.

S114 실행 종료 관찰: legacy sweep disabled marker 확인, 원래 FL DEVCTL 0x2810(nosnoop1/relaxord1) 확인, S113 SNOOP marker 없음. WATCHDOG0/CFS0. NVMe CC=0x464001 후 Device not configured로 하네스 종료. 자동 종료 시퀀스와 일치. 해시는 아직 미회수이므로 성공 여부 미확정. 다음은 USB의 NWOAS-SETUP-LOGS/s114-*/REPORT.TXT 회수. 로그 회수 전 S114를 영구 채택하거나 설치를 재개하지 말 것.

### S114 결과 / S115 준비
S114 첫 해시 732962a25500cd37c901d5c177e8a3fd5564111253e15b7237b626027f1e6f31, 두 번째 4a8ef096a3aec7ec312a576ecdc58dd03285ca41acef569bc785ac95bf5e7a77. 둘 다3987720636 bytes, EXIT1. legacy cache sweep 호출 제외만으로는 해결되지 않음. 영구 채택하지 않는다. USB를 맥북에서 재해시한 두 원본은 기존 정상 hash 유지. 로그 보관 nwoas_scripts/logs/s114-usb-20260908/.

S115는 기존 S103/S102 바이너리로 복귀(실험용 No Snoop/skip-sweep 미사용)하고 사용자 모드 파일 읽기를 분리한다.
새 NWREAD.EXE(ARM64 CNG SHA256)는 VirtualAlloc으로 1MiB 버퍼 할당. buffered와 direct 모드 모두 동일한 버퍼를 사용. direct는 FILE_FLAG_NO_BUFFERING을 추가(0x28000000 vs buffered0x08000000), 64KiB 주소 정렬 및 볼륨 logical sector가 65536 이하인2의거듭제곱/1MiB 배수인지 검사. 파일 오프셋/읽기 요청은1MiB 단위. 마지막 partial read 후 파일 길이만큼 읽으면 종료. 0 또는 파일 크기초과 반환은 오류. 입력파일쓰기 없음.
공식 근거: https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering 및 https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile . FILE_FLAG_NO_BUFFERING은 파일 캐시 우회이며 장치 자체 캐시/USB DMA를 제거하는 것이 아님.
5 cases: 기존 NWHASH HeapAlloc buffered 1회, NWREAD VirtualAlloc buffered 2회, NWREAD VirtualAlloc direct 2회. 각 CASE label/hash/size/exit code를 RAM 보고서에 남김. 마지막에 USB 한 번 복사,15초 후 자동 종료. 'FIVE CASES FINISHED'는 성공 표시가 아니며5개 CASE_RC를 개별 판독. 첫 selftest는 NWHASH이며 NWREAD도 매회 자체abc SHA256을 검증한다.
NWREAD.EXE SHA256 c59c5ce12005485be6afc5973383c44d6219355eb164ff812121021a433de83e. 빌드 sh nwoas_scripts/nvme-s115/build.sh, 경고를에러로처리한 C컴파일 및 ARM64 PE imports 확인 완료. Windows 실행은 아직 미검증.
하네스 nwoas_scripts/usb-s115-guest-test.sh (S103 binary hash 고정). USB HASH115.CMD/NWREAD.EXE/S115.TAG 준비. XML Order7 변경백업 logs/s115-usb-backup-20260908-211005. 다른 자동실행TAG(S100/106/113/114)는 없음.
다음: USB를 맥미니로 이동 후 S115 하네스 실행. 가능하면 어느모드에서 내용이틀리는지/끝까지읽지못했는지구분해서 원인을좁힐 것. 아직설치재개안함.

### S115 실기기 부팅 및 종료
2026-09-08 21:11:37 KST 실행. log nwoas_scripts/logs/usb-s115-20260908-211137.oY43cF, exec session32636. 기존S103 hash 확인, S113/S114 적용marker없음. Windows부팅/NVMe초기요청 정상. 끝에NVMe CC=0x464001 이후 CDC Device not configured로종료해 자동종료시퀀스와일치. WATCHDOG0/CFS0. 5개 CASE가모두실행/성공했는지는 아직USB REPORT미회수라단정불가.
다음: USB NWOAS-SETUP-LOGS/s115-*/REPORT.TXT 회수해 LEGACY1/ALIGNED BUFFERED2/ALIGNED DIRECT2 해시/size/에러를확인. 새NWREAD의direct alignment/ReadFile오류를데이터불일치와구분할것.
반복USB이동을줄이기위한중계기사용방안은S116-REPORT-TRANSPORT-PLAN.md에검토만기록. 아직구현/활성화하지않았음.


### S115 결과 회수: 캐시 우회 읽기 성공
원본 로그: `nwoas_scripts/logs/s115-usb-20260908/s115-12973-17752/REPORT.TXT`.
모든 case가 3987720636 bytes를 읽었고 SHA256 자체 검사 통과.
- Legacy buffered: f6d14ce49f17fc8100b58c5dd49b4c672c086496eca2dac133defaf6b1b46b6c, EXIT1.
- Aligned buffered1: a40e0c7266291891fcb54e939275d7e6188f242a33352a1260411637579c0608, EXIT1.
- Aligned buffered2: e343ed1544c9a2ebf762be1496c84403d1497cdb6e884bb2f80d66b77ed4e0d7, EXIT1.
- Aligned direct1/direct2: 둘 다 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c, EXIT0. 정상 원본과 정확히 일치.
[FACT] 파일 캐시 우회 읽기는 이 실험 두 번에서 성공. [UNVERIFIED] USB 드라이버 근본 원인/동시 SSD 쓰기 경로의 무결성은 아직 미확정.
S117은 WinPE 내장 xcopy /J로 기존 NTFS 안의 새 디렉터리에 두 SWM을 복사한 뒤 direct/buffered SHA256 검증. 기존 손상 t1.swm의 알려진 전체 해시를 볼륨 식별 조건으로 사용하며, 입력/기존 파일을 덮어쓰지 않는다. 포맷/DISM/파티션 수정은 이 단계에 포함하지 않는다.
Microsoft xcopy /J reference: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/xcopy . WinPE 이미지에 xcopy.exe 존재 확인.

S117 USB staging completed; XML backup `nwoas_scripts/logs/s117-usb-backup-20260908-212403`.
COPY117.CMD requires a single candidate outside USB with exact t1.swm length and full known S106 SHA256 before SSD writes. Existing S117SRC is refused; old files are preserved. Disk full stops copy and retains partial files. No format/DISM/GPT changes.
USB identity/external-device flag, source sizes, NWREAD binary hash and staged byte equality checked. S117.TAG armed; S100/106/113/114/115.TAG absent. S84 prerequisite tag remains.
HARDWARE-UNVERIFIED: xcopy /J has not run yet. Next: move USB to mini, run bash nwoas_scripts/usb-s117-guest-test.sh, observe auto shutdown, recover REPORT.TXT.

### S117 실기기 실행 시작
2026-09-08 21:24:27 KST 사용자 USB 연결 완료 후 usb-s117-guest-test.sh 실행. 로그 nwoas_scripts/logs/usb-s117-20260908-212427.SiyxqF, exec session43485. S103/S102 그대로 사용, chainload 한 번. 복사/검증 결과는 아직 미수집.

S117 진행: 대상 t1 전체 범위 순차 DIRECT R 이후 ANS WRITE 진행. 146688 blocks(약600MB) copy backend 쓰기 관찰, direct counter는약62000에서계속증가하지않음. writable_namespace._try_direct는 PRP의4KB정렬조건불충족등에서copy backend로우회함; 이번실제PRP거절이유는미계측. 호스트copy경로사용은관찰됐지만정확한원인은단정금지. 실험중코드수정/시리얼추가접근없음. PASS는USB최종REPORT필요.

S117 추가진행: ANS WRITE count1111552(약4.553GB) 이후 새extent LBA57239295부터 DIRECT R 시작. 기존t1extent55974436과다름. 두SWM 복사 뒤 검증단계진입과일치하나최종SHA값은미회수. WATCHDOG/CFS 없음. 누적writecount에는FSmetadata포함,파일별완료증거는REPORT에서확인할것.

S117 자동 종료 관찰 완료: 새 사본 전체 영역을두번순회한뒤 NVMequeue삭제(op00/op04), MMIO CC14=464001, CDC Device not configured. session43485 exit1은종료뒤통신해제로,해시실패코드가아님. WATCHDOG0/CFS0. direct누적202000이상. USB NWOAS-SETUP-LOGS/s117-*/REPORT.TXT 미회수: 최종PASS/FAIL미확정. 다음은USB를맥북으로이동해 REPORT의COPY1/2_RC와VERIFY1/2_direct/buffered_RC,해시,여유공간확인. 기존SSD사본손상증거보존. 새S117SRC존재하므로같은스크립트재무장시안전중단됨.


### S117 결과: SSD 설치 원본 확보 성공
보고서 nwoas_scripts/logs/s117-usb-20260908/REPORT.TXT (원본CP949), REPORT-utf8.txt.
COPY1_RC=0, COPY2_RC=0; VERIFY1_direct/2_direct/1_buffered/2_buffered_RC 모두0.
C:\S117SRC\install.swm = 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c, 3987720636 bytes.
C:\S117SRC\install2.swm = 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963, 564691107 bytes.
각각direct/buffered양쪽정확히일치. PASS S117 BOTH COPIES MATCH ORIGINAL IN BOTH READ MODES.
Windows volume serial1CEA-E590; total24774701056, beforefree11283267584, afterfree6730850304 bytes.
Root에는 A 및실패한 Windows/Program Files/Program Files (Arm)/Program Files (x86)/ProgramData/Users/Recovery/PerfLogs가 남아있음.
[FACT] S103/S102에서 xcopy /J로USB→NVMe복사 후저장사본검증통과. [UNVERIFIED] DISM이미지적용/설치Windows첫부팅/USB캐시문제근본수정.
다음S118: 재부팅후정상사본전체해시재확인, Windows시험볼륨만식별후실패한설치폴더/옛t1,t2삭제해공간확보, S117SRC에서DISM /Index:2 /CheckIntegrity /Verify 적용. GPT/ESP/Apple볼륨/영구bootobject변경없음.


### S118 준비 완료 (실행 전)
`nwoas_scripts/nvme-s118/APPLY118.CMD`, `NWGUARD.EXE`, `usb-s118-guest-test.sh`.
NWGUARD read-only checks: NTFS label Windows, serial0x1ceae590, volume total24774701056,
GPT partition5 offset220856320000 length24774705152; ready mode >=19838091490 available bytes.
Windows ABI IOCTL GET_PARTITION_INFO_EX offsets0/8/16/24를조회. 확인실패시정리전중단.
두S117SRC파일전체SHA256재확인후만실패한9개설치디렉터리와옛t1/t2삭제.
삭제명시목록 A,Windows,Program Files,Program Files (Arm),Program Files (x86),ProgramData,Users,Recovery,PerfLogs.
상위reparsepoint중단,readonly속성정리,필요시해당경로에만takeown/icacls후rmdir재시도;안지워지면DISM중단.
S117SRC보존. /Index:2 /CheckIntegrity /Verify /ScratchDir:S118SCRATCH로루트적용.
DISM rc0 + kernel/loader/SYSTEM hive존재시에만PASS. ESP/bcdboot/UEFI첫부팅별도미검증.
S118-APPLY-STARTED marker 있으면자동재시도중단. RAM보고서를변경한SSD와USB에저장후shutdown.
NWGUARD ARM64 PE build -Wall -Wextra -Werror 통과; SHA256 9b0278846c676e4327e0d7a7397e0bd95a0372a3597d86ab3b790a35a302ffa0.
호스트stubAPI guard tests12 PASS (Windows PE 실행검증과구별). bash -n harness PASS.
USB외장UUID확인및복사후바이트검증. XMLbackup `nwoas_scripts/logs/s118-usb-backup-20260908-220745`.
S118.TAG만새실험무장(S84기존전제TAG유지); S100/106/113/114/115/117.TAG없음.
다음사용자가USB를미니로이동하면 bash nwoas_scripts/usb-s118-guest-test.sh 실행. S103/S102그대로.

### S118 실기기 실행 시작
2026-09-08 22:08:10 KST 사용자 USB 이동완료 후 usb-s118-guest-test.sh 실행. log nwoas_scripts/logs/usb-s118-20260908-220810.pwARVo, exec session53174. S103/S102 유지. 최종guard/해시/cleanup/DISM결과 아직미수집.

S118 이번실행조기자동종료. 대용량SWM순차읽기샘플없음, 초기DIRECT R샘플4개뿐. NVMe CC=464001 뒤CDCdisconnect, session53174 exit1. 초기candidate/NWGUARD 등의조건에서멈춘것으로추정되나USB REPORT미회수라정확원인미확정. DISM시작증거없음. 같은스크립트재무장/조건완화전에USB s118-*/REPORT.TXT 확인필수. NWGUARD의실제Windows오프셋/볼륨조회결과를확인하고필요한부분만수정할것.


### S118 보고서 회수 / S119 수정
S118 REPORT는 BEGIN/FAIL/END뿐. NWGUARD 출력/dir출력도없어 DATA 미선택(candidates0)으로초기종료한증거. guard 실패로단정하지말것.
S119는 표시TXT 의존제거, 두실제SWM존재를12번대기검사,시도별로그. 자동마운트실패시디스크GUID898f172d-77bf-4007-b865-ac81a141eb3c + partition5 byteoffset220856320000 + BasicData타입을diskpart출력에서모두확인한뒤비어있는R:만할당. 이후NWGUARD실제volume정보와두전체해시조건동일.
읽기전용호스트MFT검사추가: ntfs_inspect_s119.py. 처음4MiB로는새파일못찾음; MFT자체runlist를따라111149056bytes전체검사.
root S117SRC record57830, 그하위install.swm record67791 size3987722240(원본3987720636보다1604bytes큼). install2.swm/VERIFIED-S117.TXT 이름의활성MFT기록은미발견.
이것은S117 WinPE 보고서와불일치. NTFS journal replay/checkpoint 상태/메타데이터영속성/장치읽기등추가분리필요. rawMFT만으로데이터소실단정금지. S119에서정상mount후전체hash재검증필수,불일치시cleanup금지.
첫probe는permanent부트m1n1의nvme_init에서EL2exception/disconnect. 이후S103으로한번chainload후read-only검사성공. 종료후nvme_shutdown완료,proxyidle. guestrun없음. 관련로그 s119-mft-inspect.log,s119-probe-chainload.log,s119-mft-full-s103.log,s119-mft-inspect.json.

S119 USBステージ完了: backup nwoas_scripts/logs/s119-usb-backup-20260908-221542. UUID/tool hashes/staged bytes verified, S119.TAG armed and old experiments disarmed. Next USB to mini, bash nwoas_scripts/usb-s119-guest-test.sh. Hardware-unverified.

### S119 실기기 실행
2026-09-08 22:16:30 KST 사용자USB연결완료. usb-s119-guest-test.sh 실행, log nwoas_scripts/logs/usb-s119-20260908-221630.OCXdcB, exec session85157. S103/S102유지. 자동mount/재검증결과미수집.

S119 자동종료: S117SRC영역전체4GB읽기 관찰, DIRECT R 마지막 lba58223343 ok62000. 이후NVMeCC=464001/CDCdisconnect로session85157 exit1. WATCHDOG0/CFS0, ANS WRITE0. 초기discovery/NWGUARD는통과한것으로추정(소스파일검증읽기증거), 최종hash/어느파일에서중단했는지 REPORT미회수. cleanup/DISM시작증거없음. USB s119-*/REPORT.TXT 회수후원인판정필수.


### S119 REPORT 회수: install2 길이 불일치
첫탐색 candidates1 C:, NWGUARD 실제하드웨어PASS (serial485156240, NTFS total24774701056, partition5 start220856320000 length24774705152).
install.swm 재부팅후SHA8118bfe... 전체3987720636 정상.
install2.swm 실제564695040 bytes (+3933), SHA1d82445d7bbda4bf920af15d61c563f002c84b34bb43dcc84aeefadbb3661203, 실패. cleanup/DISM 실행안함.
Host original install2는564691107 bytes,62512ee... 정상. 여기에3933 zero를붙이면SHA7c194ce8cf6374c696919a6a07d2b75035be544a0bbe2007d1a84b3161684f59로실제와다름. 원본prefix일치여부는아직모름.
rawMFT당시큰파일size불일치는S119 Windows에서정상으로해석됐으므로 journal replay/파일시스템상태반영차이가능. 메타데이터영속성원인확정아님.
S120 NWTRIM: 원본길이prefix해시정확히일치할때만 EOF를564691107로수정+fileflush. 크기원본/4KiBceil외거부. fixed path/hash/mode조건. hash후 buffered write-through exclusivehandle로재개방(unalignedEOF에NO_BUFFERING seek불가). prefix불일치(rc1)때만install2 xcopy/J한번재복사후다시검증/수정;API에러는즉시중단.
NWVFLUSH: 동일NTFS시리얼/크기/partition확인후volumeFlushFileBuffers. 적용전두전체hash재검증, 성공때만기존cleanup/DISM절차.
NWTRIM호스트APIstub10경우PASS: 정상/ceil/잘못된크기·경로·hash·read·seek·EOF·flush실패에서mutation순서/실패코드검사. 실제Windows실행아직미검증.

S120 USB staging verified, backup nwoas_scripts/logs/s120-usb-backup-20260908-222734. NWTRIM SHA63e9bb3c3fe4b3135657619987805ac135dc4b87ddec7ea4199456b100bf1466, NWVFLUSH SHAd116236fc2eec128c0b24203112efeb457f2adbb9f9fbb202527820c7c057a18. Only S120 new TAG armed. Next USB to mini and bash nwoas_scripts/usb-s120-guest-test.sh. Hardware-unverified.

### S120 실기기 실행 시작
2026-09-08 22:27:57 KST 사용자USB연결완료, usb-s120-guest-test.sh 실행. log nwoas_scripts/logs/usb-s120-20260908-222757.Z2Ms7C, exec session56689. S103/S102유지. 결과미수집.

S120実行終了: install2範囲の約565MB再書込後、その範囲read、続けてinstall1全体とinstall2再read。最終CC=464001/CDCdisconnect、session56689 exit1。WATCHDOG0/CFS0。修復/volumeflush条件後の全体検査に進んだ形だが、最終REPORT未回収。cleanup/DISM開始証拠なし。
S120 SSDreport回収をread_report_s120.pyで試行: 既存permanentからS103へ一度chainload、MFT全111149056bytesのlive記録にS120-REPORT/STARTED/PASS見つからず。storage writesなし、nvme_shutdown完了、proxyidle。NTFS journal状態差の可能性あり、報告保存失敗/データ消失と断定不可。USB s120-*/REPORT.TXTが必要。ログs120-report-chainload.log/s120-report-read.log/s120-report-mft.json。


### S120 보고서 회수 / S121 수정
S120 install2 원래길이prefix SHA003786709907ca36634f9282d4c1088a79a725430ac14a40c1c8bafce3393b30 불일치. 길이뿐아니라내용도달랐음.
xcopy/J 재복사rc0후NWTRIM 원본hash62512ee.../564691107 일치, EOF/fileflush PASS. volumeflush PASS.
이후install1 8118bfe.../3987720636,install2 62512ee.../564691107 두전체hashPASS.
바로FAIL, root에S120-APPLY-STARTED.TXT47bytes존재; CLEANUP/DISM마커없음. 마지막volumeflush0.
[원인] fsutil reparsepoint query는일반폴더에nonzero반환. 뒤ECHO마커작성은ERRORLEVEL을0으로갱신하지않으므로if errorlevel1이stale값으로실패. 도구가아닌내batch제어흐름결함.
S121은FINDSTR로실제marker내용검증, scratch는존재검증, TAGrename도결과파일검증. S120은marker만만들고끝났다는보고서근거로그marker는보존하되S121실행허용. 다른이전STARTED/S121자체STARTED는중단유지.
첫install.swm은S119/S120두부팅에서전체hashPASS후변경없으므로4GB반복검사생략,정확길이+DISM CheckIntegrity/Verify유지. install2 NWTRIM검사/필요시한번복사와flush는유지.

S121 USB 준비완료, XMLbackup nwoas_scripts/logs/s121-usb-backup-20260908-224211. UUID/기존tool3개SHA/복사byte검증통과. bash -n 및stale-status정적검사PASS. S121 TAG만새실험무장. 다음USB미니이동후 bash nwoas_scripts/usb-s121-guest-test.sh. 실제실행미검증.

### S121 실행 시작
2026-09-08 22:42:46 KST, log nwoas_scripts/logs/usb-s121-20260908-224246.c8oXtj, session19456. 부팅후install2영역 ANS WRITE 관찰, 재복사경로추정. 이전flush성공반환만으로재부팅후영속성입증안됨. 최종보고서미회수.

S121 종료: install2영역재복사약565MB와검증읽기후CC=464001/CDCdisconnect. session19456 exit1. WATCHDOG0/CFS0. REPORT미회수로정확중단점미확정,cleanup/DISM증거없음.

### S122 별도 backend 수정 준비 (미실행)
읽기코드검토에서 writable_namespace.py가FUA비트를명시적으로무시하고, controller.py가CC.SHN을backend flush없이즉시SHST완료로응답함을발견. Identify VWC0(volatile cache없음). 재부팅후변경증상과관련가능하지만인과미검증.
별도 nwoas_scripts/nvme-s122/에기존모듈복사해수정,원래nvme-s93/실행중S121미변경.
일반Windows영역write는direct/copy양쪽물리flush이후만성공응답. flusherror WRITE_ERROR. shutdown processing→flush→complete, 실패CFS이며SHST완료금지. duplicate종료는추가flush안함. GPT검증거래의기존pending구조는유지되어GPT durability증명아님.
36tests PASS(기존29+durability7). volatile-cache-loss모의테스트/순서/FUA/direct/copy/오류/종료완료지연검증. 실제재부팅영속성미검증.
참고 https://nvmexpress.org/wp-content/uploads/2013/04/NVM_10e_specification.pdf .
하네스usb-s122-guest-test.sh는별도모듈경로,동일S103/S102. 아직USB스테이징하지않음/실행금지: 먼저S121REPORT를회수해다음USB스크립트와재시도범위를정할것.


### S121 결과와 추가 원인 정정
보고서회수: 두번째파일재복사/해시/EOF/fileflush/volumeflush는PASS. SOURCE READY 이후 'FAIL marker content could not be verified'. CLEANUP/DISM실행안함.
[확정] boot.wim index2 System32 목록에findstr.exe가없고find.exe는있음. S121에사용한FINDSTR가실행되지않음(출력nul처리로보고서에서누락). 이번marker검사실패는marker데이터손상증거가아니라내WinPE의존명령확인누락.
S122는marker를기존실기검증NWHASH로검사하고출력보고서에보존. diskpart fallback식별문자열검색도FIND로교체. reg/ping/fsutil/attrib/takeown/icacls/dism/wpeutil/diskpart/find/xcopy 전부WinPE존재확인, USB tool4개존재확인.
S121report는cleanup전중단이므로marker를보존하고S122진행허용. S122자체STARTED/S118/S119는기존중단규칙유지. 파일영속성backend결함수정은별도의검증대상이며이번명령누락과혼동금지.

S122 physical flush preflight PASS: S103 chainload後J274確認、nvme_init→nvme_flush(1)=1(host0.000597秒)→shutdown。blockwriteなし。log s122-flush-preflight.log。これは単発command成功で、guest/再起動durability検証とは別。
S122 USB staged, UUID/NWHASH hash/byte comparison PASS. Backup nwoas_scripts/logs/s122-usb-backup-20260908-225240. OnlyS122newTAGarmed. Next USB to mini, bash nwoas_scripts/usb-s122-guest-test.sh (separate nvme-s122 backend). 原nvme-s93未変更。

### S122 실기기 실행
2026-09-08 22:53:23 KST, log nwoas_scripts/logs/usb-s122-20260908-225323.1aE9LN, session52519. 별도nvme-s122backend실행. 초기Windows쓰기에서 S122 ANS FLUSH completed count1..8 관찰. 실제매writeflush동작확인, 최종durability/설치결과미확정.


### S122 프리징 / 복구 필요
사용자화면: 설치프로그램시작중 + CMD S122 메세지, 트랙패드움직이지않음. host ANS WRITE는lba58264735 count54272(약222MB)에서멈춤; hostalive150/180/210/240/270s계속. WATCHDOG/CFS로그없음. 매write물리flush실험은현재채택금지; 인과관계/guestinterruptstall여부미확정.
검증한run_guest PID26013에SIGTERM한번, session52519 exit143. 다른프로세스일괄종료안함.
macvdmtool reboot serial 한 번 시도(log s122-single-recovery.log). 'Did not get a reply to VDM', 재부팅성공미확인.
이후CDC NOP검사 UartTimeout, proxy준비되지않음. 원격복구재시도하지않음. 사용자물리powercycle필요.
USB는맥미니에있음. S122.TAG는RUNNING으로소비됐을수있고부분재복사본존재가능. 자동재무장/설치진행하지말고상태확인필요.
다음방향: baseline복귀 또는FUA요청/정상종료시만flush하는설계검토. VWC advertisement/Feature6설정과일관성맞춰야함; 단순히매writeflush제거후VWC0유지는잘못된durability계약이므로주의. 기존S122hardware미통과기록보존.


### S123 USB 이동 없는 작업/로그 통신 구현 및 실기 부팅 시작
사용자 물리 전원 재시작 후 CDC NOP READY, J274/Tahoe 확인. USB는 맥미니에 그대로.
새 nwoas_scripts/transport-s123/: host RAM 전용 namespace2 FAT16 전달 드라이브+LBA128 메일박스, ARM64 NWAGENT.EXE foreground 작업 수신기, CRC/token/seq ACK, 작업 출력 스트리밍, 중복 재실행 방지. 실제 SSD namespace1은 이번 시험에서 읽기 전용으로 강제. S122 every-write-flush 미사용, baseline S103/S102 유지.
프로토콜/namespace tests9 PASS, C client API stub tests PASS, FAT fsck PASS, ARM64 build PASS. 호스트 검사는 Windows 실기 성공의 증거가 아님.
실행 log nwoas_scripts/logs/usb-s123-20260908-231027.SjoPCG, exec session74892, run_guest PID28905. namespace2 armed 로그, 아직 events.jsonl 없음(클라이언트 연결 미확인). 사용자에게 USB 이동 없이 ShiftF10 명령 한 줄로 가상 드라이브 NWAGENT 시작 안내. 연결되면 01-inspect.cmd 큐잉 후 USB autounattend 자동연결 항목 마련 예정. 아직 USB XML 변경 안 했고 설치/삭제 안 함.

S123 실기 왕복 성공: events kind4 WinPE worker connected. job1 상태조회 exit0, Windows10.0.22621.525, D:NWOASLINK, E:WINARM2 serialB585-170F. SSD는읽기전용이므로현재C:미마운트. S122.RUNNING 확인, REPORT없음. job2 USBautostart설정 exit0. E:autounattend.xml 기존본 AUTOUNATTEND.PRE123.XML 백업, Order7은가상드라이브 NWAGENT검색실행으로변경. XML1774B SHAe322d3b13f197bdf70c9a3e199a804684a07b6b750515b55fa2d699048f17a4c RAM/USB임시/최종본3회일치. 사용자추가USB이동없음. job3 wpeutil reboot 요청, 자동시작검증중.

S123 재부팅 자동 연결까지 실기 PASS (23:16 KST). 첫세션 job3 wpeutil reboot로 CDC해제/exec74892 exit1(재부팅과동반된DeviceNotConfigured). NOP READY AFTER WINDOWS REBOOT 확인, 물리전원/USB이동 없이 동일S123하네스 재실행. 새log nwoas_scripts/logs/usb-s123-20260908-231513.PkriJv, exec45647/PID29963. 새.link/events.jsonl kind4 자동연결, 새job1 상태조회 exit0+출력회수. USBautounattend 저장내용은재부팅후실제실행으로확인됨.
[현재] Windows worker는새세션에서다음작업대기. SSD읽기전용(C:마운트안됨),D:가상통신드라이브,E:WINARM2. USB를옮길필요없이queue_job.py로CMD전달/출력회수/정상재부팅가능. S122프리징/SSD영속성/Windows설치성공은여전히미해결. 다음스토리지실험은새backend의write정책을명시해별도로진행; 읽기전용래퍼를설치성공으로오해금지. 모든새하네스에는S123guest_module또는동등통신namespace를추가해야USB자동연결이유지됨.

### S124 저장 정책 정합성 시험 시작
별도 nvme-s124/ baseline복사. Identify VWC1, Feature6 enabled기본, cacheoff변경전flush, cacheoff모든writeflush, cacheon일반write캐시허용/FUAwrite완료전flush. 정상종료SHSTprocessing→physicalflush→complete; 실패CFS,완료거짓응답안함. GPTwrite전부거부,기존Windows영역만쓰기허용. CachedPair가S123통신namespace를유지하고flush/cache명령을primary로전달. broadcastFLUSH도primary전달. 37tests PASS(새cache8+기존보호/큐검사, GPTtest는쓰기거부정책에맞춰수정).
첫실기 usb-s124-20260908-232038.KvFwNq exec15755는Windows/쓰기시험전UEFI초기PCffffffffffffffff정렬예외후재부팅. NOPREADY확인후한번재부팅. 현재 usb-s124-20260908-232130.1sgj6S exec35801, namespace2+S124armed및ANS FLUSH관찰. 프로브시험아직미실행.

S124 작은파일 재부팅검증 PASS: 232130.1sgj6S job1 1MiB+3B 두복사/4hash+flush pass, 정상원격재부팅후 232340.f18lU3(exec15544) job1 두복사모두exactlength+4hash PASS. 같은두번째세션 job2 16MiB+123B SHAKE256 고유페이지파일두복사/4hash+flush PASS. uniqueSHA140963987cad885fff805f56f9e49e7bf963c57543223265e5f734b86fe97604. 현재job3 원격재부팅으로16MiB사후검증예정. 설치는미실행.

S124 232638.UmedL2(exec80362): job1 unique16MiB두복사 재부팅후4hash PASS. job2 install2 USB→SSD /J재복사 완료,564691107B SHA62512ee... prefix+wholefile2회PASS,NWTRIMexactEOF/fileflush+NWVFLUSH성공. 이전S122정지지점222MB넘어565MB완료. job3정상재부팅 요청,실제install2사후검증대기. Windows설치아직미실행. 준비스크립트07전체source확인/08실패설치정리/09DISM은아직큐잉하지않음.

S124 주요 재현 문제 통과: 233303.q6Xu3y(exec37103) 자동연결 job1 install2 재부팅후 전체SHA62512ee...및564691107B PASS. 이전S119/S121에서재부팅후564695040B/손상hash였던문제를이번VWC/FUA/flush정합성수정경로에서는재현안함. 한회실기통과이지전력차단/모든부하안정성증명아님. 현재job2 07-verify-full-source.cmd로install1및install2전체SHA확인중;08cleanup/09apply미실행.

S124 233303.q6Xu3y job2 fullsourcePASS: install1 3987720636B SHA8118bfe.../install2 564691107B SHA62512ee...둘다PASS. X:S124-SOURCE-VERIFIED.TAG 생성. job3 08-clean-failed-install.cmd큐잉: NWGUARD고정trialNTFS확인,topreparse거부,실패설치9디렉터리와옛t1/t2만정리. S117SRC/시험probe/Apple파티션보존. DISM은아직시작전.

S124 233303.q6Xu3y job3 cleanup PASS. C:Windows trial정확식별후실패설치9디렉터리/old t1,t2제거. free20013035520B(20.013GB),NWGUARD ready/volumeflushPASS. S117SRC보존. job4 09-apply-image.cmd 큐잉: /Index2 /CheckIntegrity /Verify, C:S124SCRATCH, X:S124-DISM.LOG,호스트stdout실시간회수. 아직적용결과없음.
S125 UEFI NVMe후보빌드완료(미실행): m1n1-payload-s125-uefinvme.bin32342016B SHA bf00b5a4bc9ed53abf7e512854ca5408e0352ead16f9511626937aaf1e9ab43a. 현재sourceNWOAS_UEFI_NVME0을빌드때만1로변경후byte-for-byte복원확인. 기존S102prefix+새FD. uefi-s125/build_candidate.py/manifest/build.log,usb-s125-guest-test.sh준비. 실기검증되지않음/현재S124실행에미사용.

2026-09-09 00:00 이후 현재 상태는 NWOAS-STATUS-2026-09-09-SESSION.md 참조. S124 DISM적용61%진행, 아직완료아님.
