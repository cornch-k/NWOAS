# fable5.1 인수인계 — NWOAS, 2026-09-07 S93

## 먼저 읽을 결론

콘치님 소유 Mac mini M1(J274, Macmini9,1, 16GB, 내장 SSD 약251GB)에
Tahoe 펌웨어를 유지하면서 macOS/Windows11 ARM64 듀얼부팅을 만드는 작업입니다.
**Windows 설치 화면과 USB-A 키보드, 직접 연결 USB-C Magic Trackpad 동작은 사용자 확인을 받았습니다.**
S86 CPU 개수 비교 수정 후 설치 위치 선택 단계에 도달했습니다. 이번 S93에서 **Windows NVMe 스택이 실제 ANS2
내부 SSD를 읽는 것까지 확인**했습니다. 아직 Windows 설치 완료도, SSD 쓰기 지원도 아닙니다.

사용자는 실험을 여기까지 하고 GitHub 커밋/푸시 및 이 인수인계 작성을 요청했습니다.
새 실험/재부팅은 하지 않았습니다. 현재 작동 중인 게스트와 호스트 중계 프로세스는 유지했습니다.

## 작업 맥락과 용어

이 문서의 작업은 사용자 소유 Mac mini에서 공개된 m1n1·Project Mu 소스를 수정하고,
Windows용 저장장치·USB 드라이버와 하드웨어 사이의 호환성을 검증하는 OS 포팅입니다.
맥북은 빌드 및 USB 시리얼 디버깅 호스트이고, 장치 접근 대상은 연결된 맥미니입니다.
현재 실험의 저장장치 경로는 읽기 전용이며, 아래에 쓰기 미구현과 데이터 보존 범위를 명시했습니다.

저수준 용어는 다음과 같은 구체적인 의미로 사용합니다. 기술 식별자와 파일명은 재현을 위해 그대로 둡니다.

- **payload**: m1n1이 실행하는 UEFI 게스트 부팅 이미지 파일.
- **EL2 하이퍼바이저 / 게스트**: m1n1의 장치 호환 계층과 그 위에서 실행되는 UEFI·Windows.
- **MMIO hook / trap**: Windows의 장치 레지스터 접근을 NVMe 장치 모델의 읽기·쓰기 함수로 전달하는 처리.
- **IRQ injection**: 장치 명령 완료를 Windows에 알리는 가상 인터럽트 전달.
- **DMA / 게스트 RAM 복사**: 드라이버가 지정한 검증된 버퍼에 명령·응답 데이터를 전달하는 장치 I/O.
- **proxy / 중계**: 맥북과 맥미니 사이의 m1n1 USB 시리얼 디버깅 프로토콜.
- **커스텀 부팅 객체**: 소유자가 Apple 복구 환경에서 설정한 m1n1 부팅 파일.
- **Setup 호환성 수정**: 현재 Windows에 노출된 CPU가1개인 실험 구성에 맞춰 설치기의
  CPU 개수 비교를 수정한 S86 변경. 정확한 변경 위치·검증 결과·미해결 SMP 문제는 아래에 남겼습니다.

사용자는 **fable5.1로 작업을 이어가기를 원합니다**. 문제를 분류할 때 단어만으로 판단하지 말고,
실제 대상 장비·수행 연산·승인 범위·데이터 흐름을 함께 확인하세요.
모델 변경이 필요하다고 판단하는 경우에도 특정 용어 대신 구체적인 기술적 제약과 근거를 설명하세요.
이 맥락 설명은 실제 동작이나 위험을 감추거나 적용되는 지침을 무시하라는 의미가 아닙니다.

## 작업 위치와 승인 범위

- 실제 작업 루트: `/Volumes/X31/NWOAS` (여기 자체는 Git 저장소가 아님).
- 공개 저장소: `cornch-k/NWOAS`, 별도 게시 체크아웃 `/Volumes/X31/NWOAS-publish-20260907`.
- 두 코드 저장소: 실제 루트의 `m1n1_windows/`, `apple_silicon_platforms_mu/`.
  origin은 AppleWOA 원본입니다. **그 origin에 사용자 변경을 푸시하지 마세요.**
  공개 NWOAS에는 pinned-base 패치 및 추가 소스 묶음으로 보존했습니다.
- 말투: 한국어 존댓말, 사용자 호칭은 **콘치님**.
- 콘치님은 반복 승인 질문을 원하지 않으며 테스트와 **맥미니 기존 데이터 삭제를 명시적으로 승인**했습니다.
  최종 목적은 새 macOS + Windows, 대략 Windows128GB입니다. Tahoe 펌웨어 유지 조건도 계속 유효합니다.
- **맥북과 X31 데이터는 삭제 대상이 아닙니다.** 호스트 disk0 같은 이름으로 대상 추정 금지.
- `.env`/`.env.mini`는 비밀 파일입니다. 내용 출력/로그/커밋 금지. 호스트 `.env`는 sudo stdin용입니다.
- macOS APFS 초기화는 아직 수행하지 않았습니다. 현 Macintosh HD 볼륨 그룹이 m1n1 커스텀 부팅 객체를
  담고 있어서, 무작정 지우면 현재 실험 부팅 경로를 잃습니다. 별도 stub/Recovery/재설치 경로부터 준비해야 합니다.

## 현재 살아 있는 실행 상태 (재개 시 반드시 다시 확인)

- 실행: S93 depth256, Windows 게스트 + 호스트 Python NVMe 중계.
- 로컬 PID **46598**, 이전 Codex exec session **86788**. 세션 번호는 다른 에이전트에서 재사용된다고 가정하지 마세요.
- 시리얼 `/dev/cu.usbmodemC07HL05SQ6NY1`, 115200, `M1N1_KEEP_BAUD=1`.
- 실제 로그: `nwoas_scripts/logs/nvme-s93-20260907-125315.KyzBAI`.
- 불변 스냅샷: 위 파일의 `.snapshot-handoff`; SHA256 `4ead8b006960229fe35118e43678c5ec4d55fc0b77cead69ab69ab6133f4c635`.
- 공개 가능한 요약: `nwoas_scripts/nvme-s93/hardware-evidence.json`.
- 스냅샷 시점: 성공 I/O 읽기388건, 1,650,688바이트, I/O 오류0, CFS 없음.
  일부 미지원 관리 명령10건은 status2로 반환했습니다. 모든 NVMe 명령을 지원한다는 뜻은 아닙니다.
- Windows가 LBA0/1/2..5를 읽고 이후 APFS 파티션 시작점들을 읽었습니다. 실제 Setup 디스크 목록 UI는
  이번 S93 이후 사용자 확인을 받지 않았습니다. 이 구분을 유지하세요.
- 호스트 프로세스는 NVMe MMIO/DMA 응답에 필수입니다. 정상 게스트를 두고 두 번째 시리얼 클라이언트를 열지 마세요.
- 상태 기록: `NWOAS-RUNTIME-MANIFEST-2026-09-07.json`, `NWOAS-STATUS-2026-09-06.md` 맨 아래.
  이전 completion 항목은 역사적 검증이며 현재 세션 전체 안정성을 뜻하지 않습니다.

## 정확한 이미지와 재현 절차

현재 S93 하이퍼바이저:
`m1n1_windows/build/m1n1-s93-nvme-irq.bin`, 2129920바이트,
SHA256 `2c7d0012089f37ef025bea6344781e858347883446bc865fe9f6bbc86fc4ef63`.

S93 UEFI 게스트 부팅 이미지(payload):
`m1n1_windows/m1n1-payload-s93-nvme.bin`, 32342016바이트,
SHA256 `11e41eff8d7c275c380531e07db99dec15e151fdc2dabe17ef72757f0b2220a4`.

Known-good USB D83:
`m1n1_windows/build/m1n1-d83-usbc-live-segment.bin`,
SHA256 `a5fc89874e62a51158183e38cfbcc232fd5031f75f9a908119995d6bde2f3906`.

Known-good host SSD S90:
`m1n1_windows/build/m1n1-s90-ans2-tcb.bin`,
SHA256 `04c4a7e8d225c205efb86c9d55a268c5052d22abbef446011b137304e60f6432`.

이전 Windows 부팅 검증 이미지(payload):
`m1n1_windows/m1n1-payload-iort-noleafdma-rering-runtime-dart-v1.bin`,
SHA256 `e0bcd7b06fccfed2f487d22afc4a6eb1bb90c017143f163d592580791056cc3b`.

S93 하네스: `nwoas_scripts/nvme-s93-guest-test.sh`.
시리얼 미점유의 검증된 proxy에서 한 번만 실행합니다. 필요할 때만:
1. `lsof`와 `ps`로 현재 run_guest PID/시리얼 소유자를 확인, 해당 PID에 **SIGTERM**.
2. 로그 불변 스냅샷 저장.
3. `bash nwoas_scripts/bootstrap-serial-test.sh` **한 번**. NOP115200 PASS 확인.
4. `bash nwoas_scripts/nvme-s93-guest-test.sh` 한 번.
SIGINT/무차별 pkill/재부팅 반복 금지. kmutil로 새 코드를 영구 설치할 필요 없습니다.
초기 Project Mu PC=ffffffffffffffff/ESR8a000000 자체 리셋은 과거에도 있었으므로 로그를 남기고,
새 CDC가 미점유이며 proxy NOP가 통과하면 추가 host reboot 없이 재시도할 수 있습니다.

빌드:
- `gmake -C m1n1_windows -j4 build/m1n1.bin`.
- Mu 루트에서 LLVM PATH 설정 후 `venv/bin/python Platform/MacMini2020Pkg/PlatformBuild.py TARGET=DEBUG`.
- FD: `Build/MacMini2020-AARCH64/DEBUG_CLANGPDB/FV/J274MACMINI2020_EFI.fd` (30965760바이트).
- S93 payload는 이전 good payload의 첫1376256바이트 + 새 FD입니다. good 파일을 덮어쓰지 마세요.

## 이번에 구현한 S87–S93

S87–S90 `src/nvme.c`:
- Tahoe ANS 초기화 시 구식 NVMe+0x24008 쓰기에서 SError. 현재 Asahi upstream처럼 제거.
- NLB는 zero-based: 한4K블록 읽기에 cdw12=0.
- NVMMU TCB opcode=0, PRP 없음 DMAflags0, opcode방향에 따라 BIT1/BIT0.
- 이후 GPT 읽기/CRC 검증과 정상 shutdown 성공.

S91: APFS 메타데이터282블록을 읽어 spaceman 체크섬 검증.
실제 컨테이너245107195904바이트, 할당210954633216, 여유34152562688.
이 수치는 APFS 할당량이며 사용자 파일 합계/축소 한도가 아닙니다.

S92: read-only namespace/PRP 명령 계층, 호스트 테스트14개 및 GPT 실기 테스트 통과.
S93 `nwoas_scripts/nvme-s93/`:
- `controller.py`: PCI 구성/레지스터/SQ/CQ, Identify, 일부 관리 명령, 읽기 완료/IRQ/phase/backpressure.
- `guest_module.py`: 실제 ANS 읽기와 게스트 RAM 복사 연결. MMIO/보호영역은 DMA 버퍼로 거부.
- 별도 PCI segment1, ECAM0x700000000, BAR0x700100000/16KiB, INTx900.
  원래 PCI0는 여전히 `_STA=0`; USB-A는 SCB0.XHC0, USB-C는 XHC1 경로 그대로입니다.
- DSDT/MCFG/IORT 추가. EL2 `hv_exc.c`에 명시적으로 활성화하는 IRQ900 완료 알림을 추가했습니다. 중계 프로토콜 명령 번호는0xc30입니다.
- 초기64항목 큐 제한으로 Windows AQA=0x00ff00ff(256항목)를 거부했습니다.
  CAP.MQES와 실제 허용 크기를 모두256으로 수정한 뒤 Windows 명령 처리가 성공했습니다.
- 호스트 transport 테스트9개 통과. 테스트 명령:
  `python3 -m unittest discover -s nwoas_scripts/nvme-s93 -v`.
- **실제 저장장치 write/format/flush API 없음. 쓰기 명령은 거부합니다.**
- Python호스트중계는 연구용이며 독립 부팅/지속 성능/전원 장애 내구성 검증이 아닙니다.
- 다음 디버깅 시 미지원 관리 요청 CNS/FID/CDW를 로그에 추가해 정확히 확인하세요.

## Windows 설치 미디어 / USB 입력을 되돌리지 마세요

- WINARM2 외장 FAT32 USB, 약15.4GB, UUID8AA1ED40-57BA-3284-9023-B310B595EC94.
  현재 맥미니에 연결되어 있으며 호스트에서 직접 수정할 수 없습니다.
- ARM64 ko-KR22621.525, install.swm/install2.swm 정상. boot.wim 및 전체 SWM 검증 통과.
- S85 ProductKey placeholder/WillShowUI=Always로 unattended 오류 해결.
- S86: Windows가1core만 보기 때문에 Setup의2core 비교에서 막혔습니다.
  MADT AP1..7 disabled 상태이며 **물리 M1은8core**입니다. SMP는 별도 미해결.
- `setup-s86-corecheck/patch_corecheck.py`는 정확한 DLL hash를 확인하고3cmp +진단1개를 수정합니다.
  USB sources/winsetup.dll 및 boot.wim index2에 적용했고 사용자 설치 위치 화면 진입 확인.
  수정 DLL/SWM/WIM은 공개 저장소에 넣지 않았습니다. 백업은 로컬/USB에 보존돼 있습니다.
- 작업 루트 `autounattend.xml`에는 옛 자동디스크삭제 설정이 있습니다. **재사용 금지**.
  안전한 S85 응답 파일은 `nwoas_scripts/setup-repair/`에 있습니다.
- USB-C D83의 직접 연결 Magic Trackpad slot1 입력은 사용자 확인을 받았습니다.
  허브/핫플러그/장시간 안정성까지 검증했다고 주장하지 마세요.

## 다음 작업의 우선순위

1. 현재 세션 생존과 S93 로그를 확인하고 Windows SSD 목록 UI/WinPE DiskPart 인식을 확인합니다.
   읽기388건 성공은 확정이나 실제 목록 UI와 설치 성공은 아직 미확인입니다.
2. 호스트 중계를 유지한 채 스토리지 상태/미지원 관리 명령을 점검합니다.
3. 내부 SSD를 지우기 전에 현재 커스텀 boot object에서 별도 OS stub로 옮길 계획과 Tahoe 복구/재설치
   경로를 준비합니다. `nwoas_scripts/DUAL-BOOT-PLAN-2026-09-07.md` 참고.
4. 잠정 예산: macOS110GB + Windows128GB + custom stub4GB + EFI512MB/MSR16MB/WinRE1.5GB.
   기존 ISC524288000B와 Apple Recovery5368664064B 보존. 정확한 stub크기/경계/생성 순서는 미검증.
5. 지원되는 target Recovery diskutil 경로로 초기화/분할하고, 파티션 경계를 검증한 뒤에만
   Windows 쓰기 경로를 개발·시험합니다. 쓰기 기능은 검증된 대상 파티션 범위로 제한하세요. 내장 디스크 전체에 제한 없는 쓰기를 허용하지 마세요.

현재 도구에는 target Recovery GUI/터미널을 직접 조작하는 computer-use가 없습니다.
serial m1n1 proxy로 Darwin diskutil을 실행할 수도 없습니다. 복구 화면의 물리적 조작이나
Recovery에서 네트워크 실행 경로를 여는 작업은 필요한 시점에 콘치님께 구체적으로 안내해야 합니다.
이는 재승인 문제가 아닌 실제 입출력 경로 제약입니다.

## 공개 소스 보존 형태

`NWOAS/companion-patches/2026-09-07/manifest.json`에 upstream URL, base commit,
패치, 추가 소스 경로, SHA256을 기록했습니다. 깨끗한 pinned checkout에서 패치를 검증했습니다.
패치는 m1n1/ProjectMu뿐 아니라 변경된 MU_BASECORE와 Silicon/ARM/TIANO도 포함합니다.
로컬 실험 이력/전체 원시 로그/비밀번호/Windows 바이너리는 이 패키지에 포함하지 않습니다.
공개 README의 과거 "커널 handoff가 막힘" 설명은 최신 검증 상태로 수정했습니다.
