# NWOAS 인수인계 상태 검증 — 2026-09-06

> **최신 상태는 맨 아래 S6-D12~D17 절(23:34)입니다.** 아래 첫 자산 검증은 초기 상태의 역사 기록입니다.
> 현재 타깃 D4/Tahoe 유지, Windows USB 연결됨. 새 AFK로 HPD/모드 조회 성공, 안정 영상/Windows 미성공.
> AFK ep0x23 종료 완료, AP quiesce ACK 미수신. 이전 exact-state 하네스 재실행 금지.

콘치님 요청에 따른 첫 상태 복원입니다. 과거 기록의 실기기 결과와 현재 기기 상태를 구분합니다.

## [FACT] 확인한 자산

- 현재 `m1n1_windows/build/m1n1.bin`은 세션5 `m1n1-s5-cohkeep-v2-20260714.bin`과 동일합니다.
  - SHA-1: `d4986f8dcee82e8a0aed5d4de0f808020bc12fc2`
  - MD5: `1cec5689492700e8688e43eb292c6987`
  - SHA-256: `899f3b199ae095925ff99bfd3ba782bb47e90585b438b20be57c7acca5ae1aeb`
  - 핸드오프의 “md5 d4986f8d”는 해시 종류 표기 오류입니다. 파일 불일치가 아닙니다.
- postdiag SHA-1 `192787100723a1add5ba423cd9ce875e03fd2abc`, stormtame-v2 SHA-1
  `d2dfee3f2578e6ef7190d4244e90d9c88e6dcb49`도 문서의 접두사와 일치합니다.
- m1n1 HEAD: `59fb544` (2026-06-29). 추적 파일 19개 변경, diff 3088 additions / 114 deletions.
- Mu HEAD: `8dbbcad` (2026-05-29). 상위 추적 파일 16개 변경, dirty submodule 2개.
  `MU_BASECORE` 내부 9개, `Silicon/ARM/TIANO` 내부 3개 추적 파일 변경도 확인했습니다.
- 위 변경 파일의 mtime에 7월 15일 이후 항목은 없습니다. m1n1 최신은 `src/hv_vm.c` 7월 14일 21:20:49입니다.
  미커밋 변경은 다수 존재하지만, 날짜별 Git 스냅샷이 없어 “7월 14일 당시와 소스가 완전히 동일”까지 증명하지는 못합니다.
- 기존 로그 파일 247개 중 최신은 `chainload-norerun-2121.log` (7월 14일 21:30).
  이보다 늦은 기존 실행 로그는 없습니다. 이번 세션의 bootstrap-probe 로그는 별도로 새로 생성했습니다.
- 마지막 로그: 138행 `Proxy is alive again`, 1956행 No-Snoop 진단, 1958행 EVTDUMP,
  1981행 `DARTERR STALE`, 2013행 RS=1. 마지막 watcher 값은 kernel=329/FLC=30/EVTDUMP=1입니다.
  1933 로그에도 chainload 후 Macmini9,1/J274의 실제 초기화가 있습니다. chainload 경로는 과거 실기기 사용이 확인됩니다.
  `run-hv-chainload.sh`의 UNPROVEN 주석은 이 후속 하네스의 실측 이력과 구분해야 합니다.

## [FACT] USB와 현재 타깃

- 호스트에 USB(30.8GB 외장 물리 디스크의 APFS), WINARM2(15.4GB FAT32), X31(1TB)이 마운트돼 있습니다.
  디스크 번호는 재연결 시 바뀌므로 스테이징 직전에 다시 확인해야 합니다. Removable 속성 검증/USB 쓰기는 아직 하지 않았습니다.
- `/Volumes/USB/m1n1.bin`: 2097152 bytes, MD5 `203efaed54170d6733c1be4cd62b037b`,
  SHA-1 `4a425865a932150095b0c4744252b84f5d24f331`.
- `/Volumes/USB/m1n1-prewall2.bin`: MD5 `95ee3e567d9a4af6e796b9dc0dc545b6`,
  SHA-1 `cc4fce145bc5d6397bb3b8761db36aec085e6ace`.
- 두 USB 파일 모두 `usb-stick-backup-20260710/` 및 `m1n1_windows/` 바로 아래 파일들과 동일 해시가 없습니다.
  정확한 빌드 정체는 [UNVERIFIED]. 세션5 빌드나 known-good으로 간주하지 않습니다.
- known-good: `usb-stick-backup-20260710/m1n1-bare-GOOD.bin`, 같은 디렉터리의 prev,
  `m1n1_windows/m1n1-bare.bin` 모두 MD5 `b64269a0fe20e7118028b00e8b5b65f0`로 동일합니다.
- 콘치님이 DFU 초기화 완료를 알렸고, 21:21:44 스크린샷은 맥미니 화면 공유 창의 macOS 바탕화면입니다.
  현재는 m1n1 proxy 화면이 아닙니다. 이전 부트 오브젝트/보안 정책/펌웨어 환경의 존속은 미확인입니다.
- `/dev/cu.debug-console`은 존재합니다. 단일 NOP 진단은 호스트에서 `Operation not permitted`로 포트를 열지 못해,
  타깃에 NOP를 보내지 못했습니다. 이는 타깃 부팅 실패 증거가 아닙니다.
- 스크린샷으로 macOS 상태가 확인되어 NOP 재시도는 불필요해졌습니다. 새 빌드, 실기 부팅, kmutil, USB 쓰기는 하지 않았습니다.
- computer use는 콘치님이 허용했지만 이 세션에 직접 데스크톱을 조작하는 도구는 노출되어 있지 않습니다.

## [FACT]/[UNVERIFIED] 프론티어 정정

- 과거 기록상 커널 진입 및 Windows Setup 화면 렌더까지 도달했습니다. 현재 DFU 후 환경에서 재현한 결과는 아닙니다.
- 마우스 커서 이동은 미달입니다. 네이티브 Windows 드라이버는 문서/헤더 단계라는 07-12 상태도 유지합니다.
- `FLAG=0`인 DART 오류 값을 유효한 FL1100 fault로 사용하면 안 됩니다.
  [Asahi m1n1 dart.c](https://raw.githubusercontent.com/AsahiLinux/m1n1/main/src/dart.c)의 FLAG 및 fault 비트 정의와 일치합니다.
  stale 값은 truncation의 증거를 무효화하지만, truncation이 모든 조건에서 불가능함을 증명하지는 않습니다.
- 고주소 DMA-write 실패는 유력한 실험 가설이지 유일 원인으로 확정된 사실은 아닙니다.
  CPU page-table walk 성공은 장치의 실제 DMA 성공과 다릅니다. 초기화 통과만으로 ERST DMA-read 성공도 증명되지 않습니다.
- 특히 2121 로그의 EVTDUMP는 RS=1 이전 한 번, 첫 세그먼트의 TRB 12개입니다.
  ERSTSZ=4인데 네 세그먼트 전체와 모든 실행 시간을 관측한 것은 아닙니다. “모든 링에 이벤트가 전혀 없음”으로 확대하지 않습니다.
- UEFI와 Windows의 대비에는 주소 외에 ERSTSZ(1 대 4), 초기화 시점/순서, ERDP 등이 달라지는 교란 요인이 있습니다.
  기존 CMDW/FORK의 ERDP 출력은 하위 32비트뿐이므로 그 값으로 high-half 정상 여부를 판단하지 않습니다.
- 공개 README는 로컬 루트에 없고 GitHub/raw URL 조회도 실패했습니다. 존재하지 않는 README 내용을 읽었다고 주장하지 않습니다.

## [DESIGN] 경로 B seed의 네 가지 위험 리뷰

1. **hv 버퍼 PA**: `src/memory.c`는 RAM과 m1n1에 identity mapping을 설정하고 기존 DART L2 풀도 포인터를 PA로 씁니다.
   16KB 정렬 정적 버퍼 방식은 타당합니다. 다만 실행 시 주소/정렬/RAM 범위/게스트와의 비중첩을 검증해야 합니다.
   ERST와 이벤트 링은 별도 캐시라인 또는 페이지에 두고, DMA 전 초기화/clean 및 DMA 후 invalidate 순서를 분리해야 합니다.
2. **저IOVA 충돌**: `0x40000000`은 게스트 0..4GB 창 내부입니다. “미사용 예시”만으로 예약되지 않습니다.
   flat mapping의 실제 용도와 다른 stream의 L1 공유를 확인하고, 이전 leaf/소유권/복원 조건을 확보해야 합니다.
   단순히 stream1 TTBR을 골랐다고 공유 테이블의 다른 stream 영향이 사라지지는 않습니다.
3. **ERSTBA 경합**: 현 코드는 게스트 값을 실 BAR에 먼저 쓴 뒤 latch합니다. 사후 override만 넣으면 일시적 고주소 노출과
   32비트 절반 write 경합이 생깁니다. ERSTBA뿐 아니라 ERSTSZ/ERDP, HCRST 세대 변경을 한 상태기계로 처리해야 합니다.
   readback 생략은 제한된 seed 관측용으로만 검토하며 full proxy 구현으로 간주하지 않습니다.
4. **실 DART 도달성**: 기존 `nwoas_dart_alias_low`는 저IOVA를 PA 하위32비트에서 유도하므로 임의 IOVA→PA에 그대로 쓸 수 없습니다.
   명시적인 IOVA/PA 인자, leaf 원본 저장/재확인, 캐시 정리, invalidate 완료의 bounded 확인이 필요합니다.
   실제 cycle-valid TRB와 물리 레지스터를 함께 기록해야 하며 PTE readback만으로 성공 처리하지 않습니다.

Seed PASS는 “hv 소유 저IOVA 링에서 실제 이벤트를 수신”입니다. 주소 원인을 좁히려면 동일한 ERST/초기화 구성의
고IOVA 대조 실험을 별도로 수행해야 합니다. 미수신은 매핑/설정/관측 창이 검증된 경우에만 음성 증거가 됩니다.
커서 성공에는 이후 게스트 링 소비/미러링, interrupt 처리와 실제 HID transfer가 추가로 필요합니다.

## [DESIGN] 첫 구현 단계 하나

**기존 관측의 빈틈을 메우는 read-only 계측부터 권장합니다.** ERSTSZ에 따라 유효 ERST entry 전체를 bounded dump하고,
각 segment 범위/크기와 ERDP 상하위32비트, 실행 중 cycle-valid 이벤트를 기록합니다.
이것으로 네 세그먼트 구성과 ERDP 일관성을 확인한 뒤 저IOVA override 하나를 독립 변수로 추가합니다.
현 hv 소스는 변경하지 않았습니다. 경로 B 전체는 HARDWARE-UNVERIFIED입니다.

호스트 측 준비로 `nwoas_scripts/bootstrap-probe-test.sh`를 작성하고 `bash -n` 검사를 통과했습니다.
재부팅/chainload 없이 115200에서 단일 NOP만 확인하며 실패 시 재시도하지 않습니다.

실기 전제는 DFU 후 bootstrap 복구입니다. `recovery-bootstrap-20260906/`에 known-good과 복구 사본을 준비합니다.
kmutil 실행은 핸드오프 §6.2의 명시적 승인 조건에 따라 아직 수행하지 않습니다.

## 승인 이후 복구 준비 (2026-09-06 후속)

- [FACT] 콘치님이 부트 오브젝트 복구와 맥미니의 필요한 작업/테스트를 명시적으로 승인했습니다.
  같은 작업의 승인을 다시 요청하지 않습니다. 실행환경의 샌드박스 허용은 별도 시스템 제약입니다.
- [FACT] 화면 공유의 실제 접속 상대 `100.81.93.114`에 `angigyeom` 계정으로 SSH 키 인증 성공.
  `Macmini9,1`, M1 16GB, macOS 26.5.2(25F84), firmware/OS loader 18000.121.3입니다.
- [FACT] 콘치님이 `.env.mini`를 제공했고 권한을 0600으로 제한했습니다. 비밀번호는 표준입력으로만 전달했고
  내용은 출력하지 않았습니다. sudo 인증 및 bputil 정책 조회 성공.
- [FACT] 대상 APFS group=`DC3ADD29-9026-4938-A572-AF920AA73B79`, System volume UUID=
  `DA8498CD-7EF3-468C-84E7-F38B1C2333EE`. Full Security, CustomKC/fuOS coih absent.
  bputil의 현재 macOS 환경 표기 `Not Paired`를 Recovery 자체가 없다는 뜻으로 해석하지 않습니다.
- [FACT] USB UUID/RemovableMedia/물리 USB 30.8GB 검증 후 세 known-good 사본과 설치 스크립트를 스테이징했습니다.
  기존 `m1n1.bin`/`m1n1-prewall2.bin`은 보존했습니다. 각 복사본 해시 일치.
- [FACT] 맥미니 `/Users/Shared/NWOAS-bootstrap-20260906/`에도 복사 완료.
  `install-in-recovery.sh --check`로 모델/대상 시스템 UUID/바이너리 해시를 실제 맥미니에서 검증했습니다.
  확인 시 system volume은 `/System/Volumes/Update/mnt1`에 마운트돼 있었습니다.
- [FACT] 무암호 sudo shutdown은 권한 부족으로 실행되지 않았습니다. 이어 macOS System Events의 정상 종료 요청은
  SSH에서 exit=0으로 수락됐습니다. kmutil과 실험 부팅은 아직 수행하지 않았습니다.
- [DESIGN] 다음은 콘치님의 물리 전원 버튼 길게 누르기 → Options → paired 1TR Recovery Terminal입니다.
  일반 macOS의 SSH/화면 공유는 이 환경으로 이어지지 않습니다. 복구 터미널에서 아래 명령을 실행합니다:

```sh
bash "/Volumes/Macintosh HD - Data/Users/Shared/NWOAS-bootstrap-20260906/install-in-recovery.sh"
```

Data 볼륨이 마운트되지 않으면 디스크 유틸리티에서 해당 볼륨을 마운트합니다.
대안은 준비한 USB를 맥미니에 연결하고 `bash /Volumes/USB/install-in-recovery.sh` 실행입니다.
스크립트는 시스템 볼륨 UUID가 다르면 중단하고, kmutil이 1TR 및 소유자 인증 조건을 검사합니다.
kmutil 완료 뒤 임의 반복 재부팅 없이 호스트의 단일 serial 부팅으로 이어갑니다.

### 1TR 설치 시도: Reduced Security 선행 조건

- [FACT, 콘치님 보고] 복구 스크립트의 kmutil 실행에서 Boot Policy Error:
  "You must move to Reduced Security (using Startup Security Utility) before installing custom boot objects."
- 설치 완료가 아닙니다. Full Security에서 Reduced Security로 먼저 전환하는 안내가 누락됐습니다.
- 다음: 복구의 유틸리티 → 시동 보안 유틸리티 → 해당 Macintosh HD → 보안 정책 → Reduced Security를 선택하고
  소유자 인증 후, 복구 터미널에서 같은 설치 스크립트를 다시 실행합니다. 추가 커널 확장 허용 체크는 이 작업에 불필요합니다.

### S6-P1: bootstrap 실제 부팅 PASS / S6-P2: 기존 baseline 재현 중

- [FACT, 콘치님 보고] Reduced Security 변경 후 설치 스크립트의 최종 성공 문구까지 도달했습니다.
- [FACT] `bootstrap-serial-test.sh`가 serial capture를 먼저 시작한 뒤 `reboot serial`을 정확히 1회 실행했습니다.
  4406 bytes 배너, Macmini9,1/J274, EL2, `Running proxy...`, checksum-validated NOP@115200 확인.
  로그: `nwoas_scripts/logs/bootstrap-serial-20260906-214131-998982.log` 및 `.serial.bin`.
- [FACT] OS/System firmware는 모두 unknown(mBoot-18000.121.3).
  `AFK: ring buffer size mismatch`, DCP assert 및 iBoot display init 실패가 있습니다.
  framebuffer는 실제 modeset 성공 값이 아닌 640x1136 dummy입니다. proxy 성공과 화면 성공을 구분합니다.
- [FACT] `dfu-baseline-test.sh`는 재부팅 없이 SHA-1 d4986f8d hv를 한 번 chainload했습니다.
  업로드 완료, `Proxy is alive again`, 별도 NOP의 `CHAINLOAD-PROXY-VERIFIED` 모두 확인했습니다.
  두 번째 m1n1에서도 DCP HELLO 실패. 현재 동일 guest payload(6b85efe1...)를 업로드하는 중입니다.
  로그: `nwoas_scripts/logs/dfu-baseline-20260906-214305.log.dPXIcU`.
- [FACT, 콘치님 보고] Windows USB는 실험 시작 당시 아직 미연결이었습니다. 화면 연결은 맥미니 USB-C→DP입니다.
  키보드를 사용할 수 있다고 안내했고, Windows USB와 유선 키보드를 USB-A로 연결하도록 안내했습니다.
  연결 완료는 아직 확인하지 못했습니다. 최종 커서 기준은 유지하되 키보드 반응은 별도 HID 검증으로 기록합니다.
- [DESIGN] HDMI 직접 연결을 권장했습니다. 현재 NWOAS에서 USB-C DP 출력은 검증되지 않았고,
  Asahi M1 지원표도 DP Alt Mode=WIP입니다: https://asahilinux.org/docs/platform/feature-support/m1/
- [FACT, 과거 기록] `golden-dcp-pairing.md`의 당시 화면 해법은 13.5 펌웨어와 짝지은 별도 stub OS 부팅이었습니다.
  초기 복구 계획이 이 전제를 빠뜨렸습니다. 현재 Tahoe 시스템에 m1n1만 설치한 것은 proxy를 복원하지만
  과거 화면 펌웨어 조건을 복원하지 않습니다. 이 누락을 콘치님께 알렸습니다.
- [SUPERSEDED] 13.5 stub/UEFI-only 환경 복원 검토는 아래 Tahoe 유지 지시로 폐기했습니다.
  현재 run_guest 업로드는 중단하지 않고 실측 결과를 남깁니다. 경로 B seed/커널 USB 원인 판정은 아직 하지 않습니다.

### 테스트 주변기기 정정

- [FACT, 콘치님 보고] 모니터는 USB-C→DP로 실제 연결돼 있습니다. 무모니터 환경으로 분류하지 않습니다.
- [DESIGN] 키보드로 USB HID 기능을 시험할 수 있습니다. UEFI 키 입력과 Windows 키 입력은 별개 단계이며,
  LED만 켜지는 것은 HID 입력 성공으로 세지 않습니다. 마우스가 준비되기 전에는 커서 이동 성공을 주장하지 않습니다.
- [UNVERIFIED] USB-C→DP의 NWOAS 출력은 미검증이며 Asahi 표의 WIP는 Linux 지원 상태입니다.
  이를 NWOAS의 지원 여부 자체를 증명하는 자료로 확대하지 않습니다. 로컬 M1 display 설정은 dcp/disp0를 선택합니다.
  HDMI 직접 연결을 현재 실험의 권장 경로로 안내했습니다. DCP 펌웨어 오류는 이와 별도로 존재합니다.
- [FACT, 콘치님 후속 보고] HDMI→HDMI로 변경 완료. 해상도/주사율(4K30/60) 최적화는 후순위로 명시했습니다.
  현재 화면은 나오지 않습니다. 변경 전 발생한 DCP 초기화 실패가 있고 dummy framebuffer 상태이므로
  검은 화면을 정상 화면 부팅 성공으로 보고하지 않았습니다. 게스트는 시리얼상 보조 CPU 초기화를 진행하고 있습니다.


### Tahoe 유지 지시 및 S6-D1 AFK 진단

- [FACT, 콘치님 지시] 현재 macOS Tahoe OS/System firmware를 그대로 유지합니다. 다운그레이드나 13.5 stub 교체를 진행하지 않습니다.
- [FACT] S6-P2는 업로드와 Windows canonical VA 로그까지 도달했으나 guest synchronous exception 반복으로 종료했습니다.
  업로드가 모두 끝난 뒤 PID 28490만 SIGTERM 처리했습니다. Setup/화면/HID 성공은 미확인입니다.
- [FACT] C AFK는 헤더를 3×64 bytes로 고정합니다. 로컬 Python `fw/afk/rbep.py`는
  `(size-bufsz)/3`으로 블록 크기를 계산하며 128-byte 예제도 있습니다.
- [UNVERIFIED] Tahoe의 실제 링 크기는 아직 측정 전입니다. unknown firmware enum과 별개로 AFK geometry를 먼저 관찰합니다.
- [DESIGN] S6-D1은 AFK 로그와 읽기 범위 검사만 추가하며 기존 프로토콜은 유지합니다.
  기존 bootstrap의 DCP crash가 후속 HELLO를 가리는 문제 때문에 업로드 직후, chainload 직전에
  공개 proxy의 `pmgr_reset(0, 'DISP0_CPU0')`을 1회 호출합니다. 전체 기기 재부팅과 구분합니다.
- [FACT] `gmake -j4 build/m1n1.bin` 성공, 재컴파일 afk.o 하나 및 relink.
  진단 SHA1=dd8a02bf19ed25e6d8cb33392e56aea4fb58306f. HARDWARE-UNVERIFIED.
  소스/기존 d4986f8d 바이너리는 `experiments/tahoe-afk-20260906/`에 보존했습니다.
- [FACT] S6-D1용 전체 reboot serial 1회 후 bootstrap NOP 성공. 추가 reboot 없음.
  로그 `bootstrap-serial-20260906-215607-871307.log`.
  진단 chainload 로그 `tahoe-afk-diag-20260906-215745.WRVPMB` 기록 중.


### S6-D1 결과: 초기 DCP 진단 부트 오브젝트 준비 필요

- [FACT] 기존 bootstrap에서 DCP가 먼저 충돌하므로 진단 chainload에서도 HELLO 이전에 실패했습니다.
  실제 AFK geometry 로그는 아직 없습니다. 가변 블록 크기는 여전히 가설이며 수정 완료로 주장하지 않습니다.
- [FACT] 진단 dd8a02bf 코드는 실제 실행되어 배너와 Running proxy까지 도달했습니다.
  최초 NOP는 프레임 문제로 실패했지만 다음 연결에서 표준 bootstrap_port 및 proxy 호출 성공.
  주 로그: `tahoe-afk-diag-20260906-220425.VRAYmv`, `tahoe-afk-diag-20260906-220557.8nY0Pc`.
- [FACT] DCP CPU stop + PMGR reset 후 ASC control 0x10→0 확인했지만 여전히 did not receive HELLO.
  `display_start_dcp` 반환 0에도 내부 로그는 초기화 실패입니다. 반환값만으로 성공 판정하지 않습니다.
- [FACT] 진단 하네스 결함을 실행 전 검증으로 검출했습니다. chainload.py의 stub은 할당 영역 바로 뒤에 배치됩니다.
  추가 pmgr_reset 문자열 인수가 stub 첫 11 bytes를 `DISP0_CPU0\0`으로 덮었습니다.
  나머지 stub 및 이미지 시작 일치 확인 후 원래 코드 복원/캐시 동기화하여 한 번 점프했습니다.
  재부팅/재전송으로 우회하지 않았습니다. ADT push 후 기존 C ADT 참조가 유효하다고 가정해서도 안 됩니다.
- [FACT] 하네스의 당시 코드는 `experiments/tahoe-afk-20260906/runner-history.sh.txt`에 기록용 보존했습니다.
  재실행할 수 있는 기본 러너는 명시적으로 중단하도록 잠갔습니다. 다음은 1TR에서 최초 부트 오브젝트를 교체해야 합니다.
- [FACT] `/Volumes/USB`가 다시 호스트에서 보입니다. UUID/Removable/USB 물리 장치 확인 후
  새 `NWOAS-Tahoe-D1` 폴더에 진단, GOOD, prev, 설치 스크립트 및 README 스테이징.
  기존 USB 루트의 known-good 파일을 덮어쓰지 않습니다.
- [DESIGN] 다음 필요한 사용자 물리 동작: USB를 안전하게 추출해 맥미니로 이동하고, 전원 종료 후
  전원 버튼 길게 누르기 → Options → Recovery Terminal. 아래 명령을 실행한 후 재부팅은 호스트가 1회 수행합니다.

```sh
bash /Volumes/USB/NWOAS-Tahoe-D1/install-in-recovery.sh
```

Tahoe OS/System firmware를 바꾸지 않으며 기존 Macintosh HD의 custom boot object만 진단용으로 교체합니다.
kmutil 설치 권한은 이미 승인됐습니다. 물리적인 1TR 진입은 현재 제공된 호스트 도구로 대신할 수 없습니다.
다음 로그의 NWOAS-AFK-GEOMETRY가 확보되기 전 128-byte 가정으로 펌웨어 지원을 확정하지 않습니다.

### Recovery shasum 누락 수정

- [FACT, 콘치님 보고] D1 설치 스크립트 line 14에서 shasum이 없어 Diagnostic hash mismatch로 중단. kmutil 전이므로 이번 호출은 부트 오브젝트를 교체하지 않았습니다.
- [FACT] 로컬 스크립트를 기존 Recovery에서 사용한 md5 -q로 수정했습니다. 진단 MD5=24138aed6985ac1f54b249878c58e22a.
- [FACT] 사용자 전달용 sed 변환이 수정본과 바이트 단위 일치함과 bash 문법, 바이너리 MD5를 호스트에서 확인했습니다. Recovery 실행 결과는 아직 미확인입니다.
- [DESIGN] USB가 맥미니에 있으므로 이동하지 않고 같은 디렉터리에 install-md5.sh를 생성해 실행합니다. HERE 경로 및 대상 UUID/GOOD/prev 검사를 유지합니다. 원본 USB 스크립트는 보존합니다.

- [FACT] 콘치님이 USB를 다시 호스트에 연결했습니다. --repair-tahoe-md5로 기존 UUID/이동식 USB 검증 후 정확한 기존 스크립트 SHA256을 확인하고, before-md5 백업을 남겨 원래 install-in-recovery.sh 경로에 수정본을 원자적으로 교체했습니다. USB 재읽기/소스 일치/bash 문법/진단 MD5 및 GOOD/prev 해시 검증 완료. 실제 Recovery 재실행은 대기 중입니다.

### D1 설치 성공 후 최초 부팅 (22:16)

- [FACT, 콘치님 보고] md5 수정본 kmutil 설치 성공. Windows USB 교체는 아직 불필요하다고 안내했습니다.
- [FACT] reboot serial 1회, 진단 m1n1 배너와 NOP 검증 성공. 로그 bootstrap-serial-20260906-221600-710942.log.
- [FACT] 이번에는 AFK 이전에 DCP HELLO 실패. DCP segment #2 0x3028000→0x803028000 (0x24000) 추가 매핑 및 DISP0_CPU0 reset 로그가 선행합니다. 실제 ring geometry는 아직 측정되지 않았습니다. OS/System firmware 모두 mBoot-18000.121.3 유지.
- [UNVERIFIED] 단순 전원 잔류로 단정하지 않습니다. 기존 GOOD의 mapping/reset 경로와 비교가 필요합니다. Windows/화면 성공 아님.

### S6-D2: 저속 시리얼/늦은 응답 구분 (22:19~22:21)

- [FACT] 사용자 상태 질문에 직전 답변 이후 실행 중인 테스트는 없었다고 알리고 검사를 시작했습니다.
- [FACT] `tahoe-dcp-late-response-test.sh`: 115200에서 checksum NOP latency 0.009초.
  DCP CPU control=0x10. A2I=0x100101, I2A=0x20001. 30.27초간 58개 샘플 모두 수신함 empty.
  로그 `tahoe-dcp-late-20260906-221937.xg9yeM`.
- [LIMIT] 이는 앞선 HELLO timeout 이후의 관찰입니다. 새 초기화 요청부터 잰 30초 대기 실험이 아니며,
  시리얼 대량 전송이 빠르다는 뜻도 아닙니다. 지연 수신이 이번 관찰에서 없었다는 사실만 확인합니다.
- [FACT] 재부팅/리셋/메모리 쓰기 없이 실제 현재 ADT를 읽어 보존했습니다.
  `tahoe-dcp-late-20260906-222055.K2gj5O.adt` (0x5c000 bytes), m1n1 base=0x802e28000.
  진단 부팅에서 추가 매핑한 segment #2는 __OS_LOG입니다:
  phys/remap=0x803028000, original IOVA=0xa01000, size=0x22000 (매핑 크기 0x24000).
- [FACT] 이전 GOOD 부팅 로그에는 Mapping segment 및 pmgr reset 로그가 없고 AFK mismatch까지 도달했습니다.
- [UNVERIFIED] __OS_LOG 추가 매핑이 유발한 reset 경로가 초기 HELLO 실패의 원인인지는 아직 검증 전입니다.
  링 geometry는 여전히 미관찰, Windows/HDMI 성공 아님. 두 관찰 프로세스 모두 정상 종료했습니다.

### S6-D3 주소 변환 관찰 / S6-D4 첫 초기화 지연 준비

- [FACT] DCP DART stream0 TCR=0x80, TTBR0=0x80bfff0c. ERROR=0x02000400은 FLAG=0, STREAM=2로
  현재 유효한 stream0 오류가 아닙니다. 오류 레지스터를 clear하지 않았습니다.
  로그 `tahoe-dcp-mapping-20260906-222407.odx2cC`. 처음 두 스크립트 호출은 호스트 backend API 오류로
  매핑 읽기 전 중단됐습니다. 최종 backend는 모든 register write를 거부합니다.
- [FACT] __OS_LOG original IOVA=0xa01000은 0xbe5c25000으로, remap IOVA=0x3028000은
  0x803028000으로 변환됩니다. 기존 매핑과 새 remap의 주소가 다릅니다. 원인 확정은 아닙니다.
- [DESIGN] 다음 부트 오브젝트는 정확한 M1(0x8103)/mBoot-18000.121.3에서만 main의 자동 display_init을
  미루고 `NWOAS-DCP-DEFER`를 출력합니다. 공개 display proxy API 및 다른 펌웨어 분기는 유지합니다.
  이 단계에서는 AFK 프로토콜을 변경하지 않습니다. 목적은 호스트가 첫 DCP 통신부터 관찰하는 것입니다.
- [FACT] main.o 하나 재컴파일 및 relink 성공. 바이너리 `experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin`,
  MD5=7bec0225edc101857be3b7f195dd7357. HARDWARE-UNVERIFIED. 현재 맥미니에는 여전히 이전 D1이 설치돼 있습니다.
- [DESIGN] `tahoe-dcp-first-handshake-test.sh`는 새 바이너리 marker를 읽어 검사하고,
  기존 Python DART/RTKit로 handshake를 최대30초 기다린 뒤 AFK 첫 링 헤더만 읽고 큐 시작 전에 종료합니다.
  DCP reset/chainload/reboot 없음. RTKit용 메모리 할당/매핑은 수행합니다. 같은 부팅에서 한 번만 실행할 것.
- [FACT] 현재 D1에서는 marker 불일치로 DCP 접근 없이 중단됨을 실측했습니다.
  `tahoe-dcp-first-20260906-222826.SWbHzp`. 호스트 shell/Python 문법 검증 완료.
- [FACT] `recovery-tahoe-proxy-20260906/`에 새 바이너리, GOOD/prev, md5 기반 설치 스크립트를 준비했습니다.
  USB는 현재 호스트에 없습니다. 다운로드용 tar만 별도 디렉터리에 두고 LAN HTTP 서비스 시작:
  http://192.168.1.235:8766/NWOAS-Tahoe-D4.tar (서버 tool session 38258).
  HTTP로 다시 읽은 archive가 로컬과 일치하며 각 파일 내용도 검증했습니다. .env/로그는 서비스 대상에 없습니다.
- [BLOCKING PHYSICAL STEP] 새 부트 오브젝트 적용에는 맥미니의 paired 1TR 진입이 필요합니다.
  기존 설치 권한은 충분합니다. 그러나 현재 도구로 물리 전원 버튼 길게 누르기를 대신할 수 없습니다.
  다음은 1TR Terminal에서 tar를 /tmp로 받아 풀고 포함된 install-in-recovery.sh를 실행하는 것입니다.
  완료 후 호스트가 `bootstrap-serial-test.sh`로 reboot serial 1회, DEFER 배너/NOP 확인 후 first-handshake runner 1회.
  Tahoe 펌웨어를 바꾸지 않습니다. 화면 및 Windows는 여전히 미검증입니다.

### S6-D4 실측 성공: Tahoe AFK block=128 bytes

- [FACT, 콘치님 보고] D4 패키지 설치 완료. 22:33 reboot serial 1회에서
  NWOAS-DCP-DEFER 및 NOP PASS를 확인했습니다. 자동 DCP 초기화/리셋 로그 없음.
  `bootstrap-serial-20260906-223313-261832.log`.
- [FACT] 첫 호스트 호출은 StandardASC.boot의 고정1초 대기 wrapper에 들어가 실제 시작 메시지 전에 중단됐습니다.
  base ASC.boot를 명시 호출하도록 수정 후, 추가 reboot 없이 최초 시작 메시지를 보냈습니다.
- [FACT] `tahoe-dcp-first-20260906-223503.7WRHW3`: RTKit version 12, 관리 endpoint handshake 성공.
  AFK ep0x23 total=0x4000, bufsz=0x3e80, unk=0x70006. overhead=0x180 → block=0x80 (128 bytes).
  AFK 큐를 시작하기 전에 측정만 하고 종료했습니다. 화면 성공 아님.
- [FACT] 자동 DCP 경로를 미룬 상태에서 최초 HELLO 이후 단계까지 실제 도달했습니다.
  이것이 모든 Tahoe DCP 호환 문제가 해결됐다는 뜻은 아닙니다.

### S6-D5 진행: AFK transport 수정 및 mode query

- [FACT] Python AFKRingBuf의 legacy write32 중복을 제거했습니다. block=128일 때
  update_wptr가 legacy +0x80에도 써서 실제 rptr를 덮는 결함이었습니다.
- [FACT] 호스트 regression tests: 64/128 byte 링에서 포인터 독립성 및 200개 메시지 wrap/왕복 검증 PASS.
- [DESIGN] 새 실험으로 22:38 reboot serial 1회 후 동일 D4 bootstrap NOP PASS.
  `tahoe-dcp-modes-test.sh`는 수정 Python AFK로 HPD/모드만 조회하고 DCP를 quiesce합니다.
  `tahoe-dcp-modes-20260906-224009.jjM1D0` 실행 중.
- [FACT] C AFK도 header geometry를 계산하고 rptr/wptr/data 위치와 메시지 정렬에 동적 block을 사용하도록 수정했습니다.
  invalid extent/geometry/pointer 및 수신 메시지 범위 검사를 추가하고 실패한 endpoint init을 호출자에게 전파합니다.
  mailbox SIZE/OFFSET 단위인 BLOCK_SHIFT=6은 유지합니다.
- [FACT] C 빌드 성공. geometry tests (legacy/Tahoe/잘못된 값/불변식) ASan+UBSan PASS.
  C 수정본은 로컬 빌드만 완료한 HARDWARE-UNVERIFIED입니다. 맥미니 설치본은 D4 그대로입니다.
- C artifact SHA256=d7453aab1a1e4f52365f679c1e079caf2a2f623ba9e728494d5d1f155b636725


### S6-D5 결과: 128-byte 전송 성공, 새 서비스 알림 형식 발견

- [FACT] D5는 RTKit 관리 handshake 및 AFK 큐 시작까지 성공했으나 EPICHeader parser가 expected2/actual4로 중단했습니다.
  따라서 HDMI mode query는 실행되지 않았고 계획했던 quiesce에도 도달하지 못했습니다.
- [FACT] 원시 RX/TX 링을 재부팅 없이 읽어 `experiments/tahoe-afk-20260906/d5-afk-rings.bin`에 보존했습니다.
  RX rptr=wptr=0x100, 첫 QE payload size=0x74. RX가 소비한 첫 메시지는 저장된 링에 남아 있었습니다.
  별도 첫 프레임 `d5-disp0-announce.bin` (132 bytes).
- [FACT] 프레임의 payload 시작은 04 0b 03 00 6c 00 00 00입니다. 기존 EPICHeader 위치의 첫 바이트가4이지만,
  전체 새 wire specification은 미확정입니다. 이를 기존 EPIC subheader version4와 혼동하지 않습니다.
- [FACT] 수신 name=disp0-service, OSSerialize properties={interface-id:3, ep-has-desc-mgr:true}.
  record의 추가32비트 값=4, tag=0xdc010011. tag/앞쪽 short fields의 의미는 미확정입니다.
- [FACT] `decode_tahoe_announce.py`는 관찰된 announce 형식만 받는 오프라인 decoder입니다.
  실측 fixture, 모든 truncation, 잘못된 header/length 변형 거부를 검증했습니다. 송신 encoder 아님.
- [FACT] 현 upstream Asahi m1n1 EPICHeader도 Const(2)를 사용합니다:
  https://raw.githubusercontent.com/AsahiLinux/m1n1/main/proxyclient/m1n1/fw/afk/epic.py
  GitHub issue 검색에서는 이 새 구조의 구현을 찾지 못했습니다. 검색 실패가 구현 부재의 증명은 아닙니다.
- [FACT] C AFK에는 unsupported header version을 명시적으로 거부하는 guard를 추가했습니다.
  동적 링 지원만으로 새 envelope가 지원된다고 주장하지 않습니다. 최신 C artifact는 로컬 HARDWARE-UNVERIFIED.

### S6-D6 진행: 기존 조회 요청 수용 여부 1회 확인

- [DESIGN] 관찰된 새 announce를 실험 하네스 안에서만 서비스로 등록한 뒤, 알려진 기존 getModeCount 요청을
  정확히 한 번 보냅니다. 새 응답은 파일로 보존하고 미확인 형식이면 중단합니다. modeset/Windows 없음.
- [FACT] D6용 reboot serial 1회: bootstrap-serial-20260906-224806-562700.log, DEFER/NOP 성공.
  `tahoe-dcp-compat-20260906-224851.R8x5nC` 실행 중. 각 실험의 reboot는1회이며 retry reboot 없음.
- 현재 C artifact SHA256=3b43a527ad309b34090c98080f76fca5e032bca945a1ed0b4fb45785d9877dbe

### S6-D6~D11 결과 (23:20 갱신): Tahoe 모드 조회/설정 응답 성공, 실제 화면 미확인

- [FACT] D6 최초 하네스는 announce 두 번째 바이트 0x4b 때문에 명령 송신 전에 중단했습니다.
  이전 0x0b와 다른 값도 허용한 뒤, 정확한 링에 재접속하여 기존 EPIC getModeCount를 **한 번** 보냈습니다.
  `tahoe-dcp-compat-resume-20260906-225300.5OsGSf`: 30초 응답 없음.
  `d6-postquery-state.txt`: TX rptr=wptr=128, RX rptr=wptr=256. 요청 소비는 됐지만 응답 없음.
- [FACT] 새 direct USB CDC 포트 `/dev/cu.usbmodemC07HL05SQ6NY1`에서 NOP/base 및 메모리 읽기 성공.
  VID vendor Asahi Linux, target serial C07HL05SQ6NY, Macmini9,1. Nominal115200 및 M1N1_KEEP_BAUD=1 유지.
  64KiB 읽기 약0.012초, DCP text 7,344,128bytes 읽기 약1.17초. VDM baud 승격 아님.
  `/dev/cu.usbmodem...3`는 시험하지 않았습니다. 자동 포트 선택으로 다른 장치에 연결하지 마세요.
- [FACT] D4 다운로드 HTTP 서버는 설치 후 SIGTERM으로 종료했습니다. 기존 URL은 현재 서비스하지 않습니다.
- [FACT] 공개 분석 심볼 자료를 로컬 보존: hack-different/apple-knowledge의 afk_symbols.txt,
  blacktop/symbolicator AppleFirmwareKit.json, ipsw-diffs의 AppleFirmwareKit 차이 목록.
  공개 ipsw v3.1.713 CLI archive를 배포 checksums.txt와 SHA256 대조 후 로컬 tools 디렉터리에 압축 해제했습니다.
- [FACT] 소유 장치의 DCP text 및 호스트 kernelcache/AppleFirmwareKit를 읽기 전용으로 분석했습니다.
  새 전송 필드의 상호운용성 분석용 로컬 자료이며, Apple 구현 코드를 프로젝트에 복사하거나 공개하지 않았습니다.
  `d6-dcp-text.bin` SHA256=e7e3dea7a243a048ac573c989c568782e84197fff61454f0aa3a0f4c01fefdfd.
- [CORRECTION] 첫 payload byte4는 EPIC version4로 확정된 적 없습니다. 새 전송에서는 순번으로 보이며
  후속 실측 응답에서5,6,...로 변합니다. 두 번째 byte는 reserved로 추정, +2 u16=interface3,
  +4 u32=message length. 그 뒤16byte: timestamp/cookie u64, kind u8, category u8, flags u8, 나머지 padding.
  기존 `record_version=4` 명칭도 의미 미확정인 관찰값입니다. 범용 프로토콜 문서로 쓰지 마세요.
- [FACT] D7 새 내부 OPEN report(type0x12/category0/internal1)를 한 번 송신: TX 소비, 응답 없음.
  `tahoe-dcp-open-20260906-230956.Cysg8e`. OPEN 필요 여부는 아직 분리 검증하지 않았습니다.
- [FACT] D8 새 in-band command: interface3, kind0xc0, category1; 8byte command header 뒤 기존 iBoot op3.
  응답 category2, status0, iBoot op3/len20, **HPD=1, timing45개, color35개**.
  `tahoe-dcp-inband-20260906-231104.5s8uVh`, fixture `d8-response.bin`.
  하네스가 syslog endpoint2를 먼저 받아 중단했지만 RX에 실제 응답이 있었으며 별도 수신으로 보존했습니다.
- [FACT] `tahoe_afk_link.py`는 정확한 D6 부팅/링에만 재접속하는 실험용 링크입니다.
  generic EPIC 대체 아님. 단편화/OOB/다중 인터페이스 미지원. 응답 seq/category/status/length 검사.
  syslog는 기존 공개 ASC 규칙대로 읽고 ACK, OSLog type2는 공개 Linux rtkit처럼 기록만 합니다.
  Reference: https://github.com/torvalds/linux/blob/master/drivers/soc/apple/rtkit.c
- [FACT] D9 getModeCount/getTimingModes/getColorModes 세 요청 모두 성공. 목록 개수 일치.
  `tahoe-dcp-list-20260906-231314.Gn6G8b.json` 및 원시 msg1/2/3.bin.
  decode_packet은 D8 fixture와 모든 truncation 거부 확인. AFK 링 기존 regression64/128은 PASS.
- [FACT] VRAM PA0xbe3f60000,size0x1500000, DVA0x13dc000. dart-disp0/dart-dcp 양쪽에
  1080p framebuffer 전체 매핑이 연속하며 일치함을 읽기로 검증했습니다. 새 메모리/DART 매핑 없음.
- [FACT] D10 VRAM 앞8,294,400bytes를 `d10-vram-before.bin`으로 백업하고 8색 막대를 써서 cache clean.
  기존 power op2는 status0/empty response로 성공했으나 호스트가 빈 setter 응답을 거부했습니다.
  setter만 빈 응답을 허용하도록 수정 후 **power 재송신 없이** 계속했습니다.
  `tahoe-dcp-pattern-20260906-231515.D0pgKH`.
- [FACT] D10 모드 설정 op6도 한 번만 송신. 1920x1080@60Hz RGB SDR, 원시 mode entry padding 보존.
  OSLog type2 수신으로 호스트가 중단했다가 ACK 대기만 재개했습니다. 모드 재송신 없음.
  `tahoe-dcp-pattern-resume-20260906-231550.oS4x5s`, `tahoe-dcp-surface-20260906-231703.emuqQC`.
  DCP 로그: mode_set_gated 1920x1080@60 Hz, link1. 이는 HDMI 영상 관찰 성공과 다릅니다.
- [FACT] D10 164byte surface op1은 status0였으나 `inSize is lesser than expected` 및
  `swallowed swap ... fControllerPowerState is 0 / timings are not enabled` 로그.
  [DESIGN] 기존 공개 v13.3 layer 확장과 로컬 검증상 8byte 추가가 필요하다는 가설.
- [FACT] D11 유일 변경: surface 뒤 zero8bytes 추가(172bytes). 크기 경고는 사라졌고 status0.
  그러나 controller power0/timings disabled로 swap을 삼켰다는 로그는 여전히 남았습니다.
  `tahoe-dcp-surface172-20260906-231918.bOBJ5u`.
  **색상 막대가 모니터에 출력됐다는 증거 없음. Windows도 시작하지 않았습니다.**
- [FACT] D7~D11은 D6 동일 부팅에서 진행, 추가 reboot/reset/chainload/kmutil 없음.
  타깃 설치본은 D4 그대로이며 OS/System mBoot-18000.121.3 유지.
- 현재 live base0x803a84000, shared0x80d224000, TX/rptr=wptr1536, RX/rptr=wptr3200.
  아직 DCP를 quiesce하지 않았습니다. 후속은 controller power/timings 상태 확인이며 반복 재부팅 금지.

### S6-D12~D17 최종 갱신 (23:34): 영상 유지 실패, AFK 종료 후 AP quiesce 미완료

- [FACT, 콘치님 보고] Windows USB를 맥미니에 연결했습니다. 진단 USB 교체를 허용했으며,
  Thunderbolt/HDMI는 유지하도록 안내했습니다. 모니터가 잠깐 깨어났다가 신호 없음으로 다시 절전 진입.
  **색상 막대의 실제 관찰 성공은 보고되지 않았습니다.**
- [FACT] D12 모드 설정 뒤 power-on, D13 화면 전원 off/on 1회도 동일한 swallowed-swap 로그였습니다.
  `tahoe-dcp-postpower-20260906-232144.p9V05S`, `tahoe-dcp-powercycle-20260906-232228.zlTL1Y`.
  기기 재부팅/전원 재부팅 아님. DCP 화면 전원 명령만 보냈습니다.
- [FACT] D14는 목록에 있는 다른 1920x1080@60 항목으로 교체(뒤32비트 값64→0).
  두 항목의 flag 의미는 미확정이며 interlaced/progressive로 단정하지 않습니다.
  `tahoe-dcp-modeflag-20260906-232433.Slxaas`: controller 상태1, run mode0→1 및 surface 처리까지 도달.
  이후 IOAVVideoInterface terminated → HPD removed → 재발행/HPD asserted, 링크0.
  단순히 계속 controller0인 이전 실험과 구별됩니다. 원인 분리는 아직 불충분합니다.
- [FACT] D15 읽기 전용 HPD 안정성 조회5회는 모두(True,45,35).
  `tahoe-dcp-hpd-stability-20260906-232625.log`. 별도 사용자 허가가 필요한 작업 없음.
- [FACT] D16은 목록의 1280x720@60/flag0/RGB32 SDR로 낮춰 시험했습니다.
  `tahoe-dcp-720p-20260906-232728.5XQ7wf`: 설정/표면 ACK 후 동일한 HPD 해제/재감지.
  따라서 1080p에서만 발생하는 증상으로 설명할 수 없습니다. 영상 유지 성공 아님.
- [FACT] D17 동일720p 화면 설정 후150ms 동안 메시지를 처리하고, 공개 AFK/RTKit 종료 순서를 시도했습니다.
  `tahoe-dcp-quiesce-20260906-233115.G2js6t`:
  AFK ep0x23의 SHUTDOWN0xc0 → ACK0xc1 성공.
  그 다음 관리 ep0 AP state0x10 요청에5초 내 ACK 없음. **IOP state0x10 요청은 보내지 않았습니다.**
  화면은 이때에도 HPD 해제/재감지 로그. 전체 quiesce 성공 아님.
- [FACT] 최종 읽기 전용 확인 `experiments/tahoe-afk-20260906/d17-final-state.txt`:
  direct USB NOP PASS, base0x803a84000, ring TX4224/4224 RX5248/5248,
  CPU control0x10/status0x2d, mailbox inbox0x28801/outbox0x22201(둘 다 empty).
  호스트의 serial 소비 프로세스 없음. DCP는 RTKit AP 정지 요청의 미완료 상태이며,
  **이전 exact-state 하네스들을 다시 실행하지 마세요. AFK ep0x23도 이미 종료됐습니다.**
- [FACT] C guard 추가: 관찰된 Tahoe 펌웨어의 새 메시지 순번2를 기존 EPIC version2로 오인하지 않도록,
  이 펌웨어의 C 수신 경로를 명시 차단했습니다. 실제 새 프로토콜 구현은 실험용 Python 링크에만 있습니다.
  C 재빌드/`git diff --check`, Python64/128 링 regression PASS.
  `m1n1-afk-dynamic-guard.bin` SHA256=1c3c4308b0491908fe7ebb95618526c56071390c18e3c656068c3563bedfd027.
  C 수정본 HARDWARE-UNVERIFIED, 타깃은 D4 그대로입니다.
- [UNRESOLVED] 다음 핵심은 Tahoe의 출력 활성화 후 HDMI 링크 종료 원인 및 AP quiesce 변화입니다.
  AFK 링/새 command wire transport의 모드 조회는 실기 검증됐지만, 안정적인 framebuffer handoff는 안 됐습니다.
  현재 Windows USB는 준비됐으나 이번 DCP 실험 중 Windows/UEFI guest는 한 번도 시작하지 않았습니다.
  새 실험이 필요하면 별도 가설/로그를 준비하고 reboot serial은 해당 실험당1회로 제한합니다.

### 계속 진행 S6-D18~D24 (23:46)

- [FACT] D18 새 부팅1회 `bootstrap-serial-20260906-233629-364148.log` NOP/DEFER 성공.
  `tahoe-dcp-fresh-20260906-233709.c1iSXB`: 하나의 Python 프로세스로 관리 handshake,
  새 AFK announce, 내부 OPEN, 모드 조회/720p 설정/172byte 표면 및15초 메시지 처리.
  동일한 영상 활성화 뒤 HPD 해제/재발행 발생. D6의 오래된 호스트 상태만으로 설명되지 않음.
  이 부팅의 base/shared/syslog는 `.state.json`에 저장. D6 하드코딩 주소는 더 이상 유효하지 않습니다.
- [FACT] D19 ep0x24 AV 서비스 열거 성공. `tahoe-dcp-av-enumerate-20260906-233925.U0K1hN`.
  총 AFK 버퍼0x4000, TX/RX 각0x2000, block128. interface5=dcpav-controller-epic,
  interface7=dcpdp-controller-epic, EPICUnit0/External/AppleDCPDPTXController.
  추가 host heap은32MiB 예약 뒤 할당, 새로운 IOVA0x82000000..0x83000000에서 매핑. 부팅/리셋 없음.
- [FACT] D20 AV getPower(group8/cmd9)는 명시 오류0xe0000001.
  `tahoe-dcp-av-power-20260906-234045.6xdZ4P`. 성공으로 취급하지 않았습니다.
- [FACT] D21 표준 service open(group4/cmd6)은 성공(status0, command seq1).
  `tahoe-dcp-av-open-20260906-234207.u0eCkE.msg3.bin`.
  앞선 두 category0 비동기 report 때문에 호스트가 중단했지만 세 프레임을 이미 파일에 저장했으므로
  원시 세 번째 응답에서 확인했습니다. 처음 하네스 pointer 사전검증 실패는 송신 전이며 reboot 없음.
  비동기 report는 별도 보존하고 응답과 분리하도록 실험 링크 수정.
- [FACT] D22 service open 뒤에도 getPower는0xe0000001.
  `tahoe-dcp-av-power-opened-20260906-234351.bhgwXk`.
- [FACT] D23 공개 AV wakeDisplay(group8/cmd10)는 status0.
  `tahoe-dcp-av-wake-20260906-234416.7kUjzO`, 상태 JSON 저장.
- [FACT] D24 wake 후 iBoot HPD=False/count0. 캐시된720p 모드 요청은0xe00002bc로 거절됨.
  `tahoe-dcp-av-display-20260906-234526.xZ8ImO`. 표면은 보내지 않았습니다.
  표준 AV open은 기존 IOAVVideoInterface를 해제하는 로그를 동반하므로, 단순 wake 호출은 해결책이 아닙니다.
- [DESIGN] D25: 새로운 부팅1회 후 D18과 같은 경로에서 **내부 AFK OPEN report만 생략**합니다.
  내부 OPEN이 필요하다는 사실은 입증되지 않았습니다. 이 호출이 소유권/출력 상태를 바꾸는지 분리합니다.
  `tahoe-dcp-no-open-test.sh` 준비 완료. D18~D24 중 Windows/UEFI guest는 시작하지 않았습니다.

### S6-D25 시동 복구 화면으로 전환 (최신 상태)

- [FACT] `bootstrap-serial-20260906-234710-398857.log`: reboot serial 1회, 연결/serial 설정 성공이나 banner 0 bytes 및 NOP timeout. 재부팅 재시도 없음.
- [FACT, 사용자 보고] “선택한 디스크에 설치된 버전의 macOS는 다시 설치해야 합니다” 및 시동 디스크/복구 버튼 표시.
- [UNVERIFIED] Apple 시동 단계 실패 원인은 아직 미확정. macOS 손상이나 LocalPolicy 오류로 단정하지 않음.
- [FACT] D25 no-open 실험은 실행하지 못함. 이전 부팅의 주소/링 상태를 재사용하지 말 것. Windows/UEFI 부팅 성공 아님.
- [NEXT] 복구 Terminal에서 현재 정책/볼륨 상태를 읽기 전용으로 확인. Tahoe 유지, 재설치/초기화/추가 reboot 없음. 복구 UI의 직접 원격 제어는 현재 연결에서 제공되지 않아 사용자에게 복구 → 유틸리티 → 터미널 진입 안내 필요.

### D25 Recovery 사진 확인

- [FACT, IMG_3795.JPG] bputil: one true recoveryOS, Paired, Pairing Integrity Valid, Security Mode Permissive, CustomKC/fuOS Image4 Hash present. SIP/SSV/Kernel CTRR/Boot Args Filtering Enabled. 이는 파일 자체의 무결성이나 부팅 성공을 입증하지 않습니다.
- [FACT, IMG_3796.JPG] APFS disk3 볼륨 그룹 DC3ADD29-9026-4938-A572-AF920AA73B79, Data disk3s1 172.4GB, System disk3s3 17.1GB UUID DA8498CD-7EF3-468C-84E7-F38B1C2333EE. 기존 식별자 일치. 파일시스템 무결성 검사는 아직 하지 않았습니다.
- [NEXT] Recovery NVRAM의 부팅 관련 항목과 마운트 경로 조회. 보안 정책을 다시 낮추거나 macOS 재설치할 근거는 아직 없음.

- [FACT, IMG_3797.JPG] failboot-breadcrumbs = `<BOOT> 20022 20028(1) 401d0002 20024(65) <COMMIT>`. 코드 의미 미확정. boot-volume/upgrade-boot-volume은 기존 VGID DC3ADD29-9026-4938-A572-AF920AA73B79를 가리킴.
- [FACT] /Volumes에 Macintosh HD, Macintosh HD - Data, Preboot, WINARM2, macOS Base System 표시. Preboot custom kernelcache 파일 확인 예정. NVRAM 삭제 없음.

### 2026-09-07 Recovery 부트 파일 수집 준비

- [FACT, IMG_3798.JPG] Preboot에 custom kernelcache 3개 존재. 현재 coih 9D03F7...5411B93A와 일치하는 이름의 파일 2.0MB, Sep6 13:33. 파일 내용 무결성은 아직 미확인.
- [FACT] D4 원본 2097152 bytes, MD5 7bec0225edc101857be3b7f195dd7357, SHA384 3aaf1a4550a4e481d59d39d49929bc3f3d0f4e737664e39d2741f576685c1a8dcd2a56ce7b11c0cbb969043664566725. 포장된 custom 파일과 raw 파일의 해시는 다를 수 있음.
- [PREPARED] recovery-check-20260907/server.py: 192.168.1.235:8766, GET /check.sh 및 고정 토큰 PUT 부트 파일 하나만 허용, 최대8MiB. 스크립트는 Macmini9,1/정확한 VGID/coih 파일 존재 확인 후 읽어서 맥북에 업로드. 정책/타깃 디스크 변경 없음. bash -n PASS. 서버 session74891, 수집 후 종료할 것.

### 2026-09-07 Recovery 복구 및 D25~D26

- [FACT] Preboot에서 수집한 IMG4는 2,100,076 bytes. 내장 IM4P payload 2,097,152 bytes가 D4 원본과 바이트 단위로 일치하며 manifest SHA-384가 현재 LocalPolicy coih `9D03F7...5411B93A`와 일치했습니다.
- [FACT] 동일 D4 raw payload를 1TR에서 `kmutil configure-boot`로 다시 등록했고 사용자 보고 `REPAIR COMPLETE`. Tahoe 시스템 재설치/초기화 없음.
- [FACT] 재등록 뒤 serial 부팅 1회 `bootstrap-serial-20260907-000907-298714.log`: D4 marker, Running proxy 및 NOP PASS. 복구 완료.
- [FACT] D25 `tahoe-dcp-no-open-20260907-001000.9TkJf4`: AFK 내부 OPEN을 생략해도 mode/surface ACK 뒤 IOAV 종료, HPD 제거/재발행이 반복됐습니다. OPEN 원인 가설 기각. 최종 링 포인터 TX 896/896, RX 2944/2944.
- [FACT] D26 `tahoe-dcp-close-sleep-20260907-001310.gQlBbd`: 현재 커널의 AFKEPInterfaceV2 disassembly와 일치하는 CLOSE report 0x13/4-byte zero를 송신했고 TX 1024/1024로 소비됨. AFK endpoint shutdown ACK 성공. 이후 RTKit AP quiesce는 5초 timeout으로 이전과 동일. CLOSE 단독 해결 가설 기각; IOP sleep 요청은 보내지 않음.
- [DESIGN] 다음 단일 변수는 deprecated setSurface(op1) 대신 공개 iBoot swapBegin(op15)/swapSetLayer(op16)/swapEnd(op18) 경로입니다. Tahoe가 controller 활성화 뒤 AP-ready surface commit을 이 경로에서만 완결하는지 확인합니다.
- [FACT] D27 첫 실행 `tahoe-dcp-swap-20260907-001645.Hfx9MH`: mode ACK 및 swapBegin 성공. Tahoe op15 응답은 실제 28 bytes이나 rlen=0x11c로 예약 버퍼 크기를 표기해 호스트 strict parser가 중단. 원시 응답에서 swap_id=2 확인; 명령 실패가 아님.
- [FACT] 같은 부팅에서 파서만 수정해 op15를 재송신하지 않고 op16/op18을 이어간 `tahoe-dcp-swap-resume-20260907-001815.XH2ipI`는 두 ACK 성공. 그러나 37.5초 동안 호스트 수정 전에 HPD가 이미 해제되어 swap ID2가 controller power0/timings disabled로 swallowed됨. swap 방식의 유효 판정 아님.
- [DESIGN] D28은 수정된 op15 길이 처리로 mode 직후 op15/16/18을 중단 없이 실행하는 깨끗한 재현입니다.
- [FACT] D28 `tahoe-dcp-swap-20260907-001941.gmcIYE`: mode 직후 swapBegin/setLayer/end 모두 ACK, swap_id2. 그래도 IOAV 종료/HPD 제거·재발행. setSurface 대신 swap 사용 가설 기각.
- [FACT] D29 `tahoe-dcp-swap-20260907-002214.t5baCN`: 살아 있는 링크에서 swap 직후 interface CLOSE 및 RBEP shutdown. RBEP ACK 성공 직후 IOAV 종료/HPD 제거. AP quiesce는 timeout, IOP sleep/CPU stop/PMGR reset 미실행. CLOSE가 링크 종료를 유발함.
- [DESIGN] D30은 CLOSE만 생략하고 실제 m1n1 C 순서인 RBEP shutdown → AP quiesce → IOP sleep을 mode+swap 직후 실행합니다.
- [FACT] D30 bootstrap `bootstrap-serial-20260907-002309-050740.log`: reboot serial 1회 수락/재연결됐으나 banner 0 bytes, NOP timeout. 재시도 없음. D30 display harness는 실행하지 못함.
- [FACT] 호스트 USB 상태는 Apple Mac mini idProduct 6401/UsbDeviceFunction7, direct m1n1 CDC 포트 없음. 앞선 시동 복구 상태와 같은 USB 형태. 정책 손상으로 단정하지 않으며 먼저 Startup Disk에서 기존 Macintosh HD를 다시 선택하는 비파괴 복귀를 시도합니다.
- [FACT, 사용자 보고] Startup Disk에서 재시동 후 부팅음/검은 화면. 호스트 direct CDC 1/3 생성, NOP PASS base0x803c9c000. kmutil 재등록 없이 D4 복귀.
- [FACT] D30 최초 `tahoe-dcp-swap-20260907-002528.otJ9ds`는 read-only mode 목록 후 기존 exact 720p60 flag0 항목 부재로 StopIteration. power/mode/surface 미송신. 현 목록에는 720p59.94 flag0 존재.
- [FACT] exact-state resume `tahoe-dcp-handoff-noclose-resume-20260907-002704.bjOAhx`: live HPD True, 720p59.94 mode+swap ACK. CLOSE 없이 RBEP shutdown ACK 직후 IOAV 종료/HPD 제거, AP quiesce timeout. IOP sleep/CPU stop/reset 미실행. Tahoe에서는 RBEP shutdown 자체가 영상 interface를 해제함.
- [DESIGN] D31은 display/AFK가 살아 있을 때 AP quiesce → IOP sleep을 먼저 요청하는 순서 변경입니다. ACK 뒤에만 CPU stop/reset.

### S6-D31 결과 및 D32 방향 (2026-09-07 00:29)

- [FACT] D31 새 부팅1회 `bootstrap-serial-20260907-002819-264678.log`: D4 marker, Running proxy, direct USB CDC 및 NOP PASS.
- [FACT] `tahoe-dcp-swap-20260907-002857.SBtHK7`: 1280x720@60/flag0 선택, mode 및 swapBegin/setLayer/end 모두 status0, swap_id1.
- [FACT] AFK shutdown보다 먼저 AP quiesce(관리 ep0, state0x10)를 요청했지만 즉시 IOAVVideoInterface terminated, HPD removed/reasserted가 발생했고 5초 내 AP ACK가 없었습니다. IOP sleep/CPU stop/reset은 보내지 않았습니다.
- [CONCLUSION] Tahoe에서는 CLOSE, RBEP shutdown뿐 아니라 AP quiesce 선행도 활성 display interface를 유지하는 handoff가 되지 않습니다. 현재 공개 m1n1 종료 순서를 그대로 적용해 HDMI를 넘길 수 있다는 가설은 기각합니다.
- [FACT, 사용자 보고] 이후 시동 디스크에서 재시동을 눌렀고 부팅음 뒤 검은 화면. 호스트에 direct CDC `/dev/cu.usbmodemC07HL05SQ6NY1` 및 `/dev/cu.usbmodemC07HL05SQ6NY3`가 다시 나타났습니다. D4 부트 객체가 실행된 상태와 일치하며 아직 Windows 화면 성공 증거는 아닙니다.
- [DESIGN] D32는 새 DCP 세션에서 720p mode/swap 직후 DCP/AFK/RTKit 종료 명령을 전혀 보내지 않고, 기존 Windows/UEFI guest를 즉시 체인로드하는 경로입니다. 체인로드할 hv가 DCP를 재초기화하거나 framebuffer를 종료하는지 먼저 코드와 artifact를 확인한 뒤 1회만 실행합니다.
- [FACT] D32 `tahoe-dcp-guest-20260907-003548.uNvU99`: 추가 reboot/chainload 없이 설치된 D4에서 기존 Windows payload 업로드, HV/PCIe/ADT 준비까지 성공. 게스트 진입 직전 처음 DCP를 부팅하려 했으나 `dcp.mgmt.wait_boot(30)` timeout으로 중단됐습니다. mode/swap 및 guest jump는 실행되지 않았습니다.
- [CONCLUSION] DCP를 최초 기동하는 시점은 `hv_init()` 이전이어야 합니다. D32가 payload와 ADT를 먼저 준비한 접근 자체는 유효하나 DCP boot 순서가 늦었습니다.
- [DESIGN] D33은 DCP 관리/AFK만 `hv_init()` 전에 시작해 동일 힙에서 보존하고, mode/swap은 여전히 guest jump 직전 수행합니다. D32가 남긴 DCP/USB 상태는 재사용하지 않고 새 부팅1회 뒤 실행합니다.

### S6-D33 실기 결과 (2026-09-07 00:44)

- [FACT] 새 부팅1회 `bootstrap-serial-20260907-003805-306882.log`: D4 marker/Running proxy/NOP PASS.
- [FACT] D33 `tahoe-dcp-guest-20260907-003852.YYbti8`: DCP 관리/AFK를 `hv_init()` 전에 시작했고, 기존 Windows payload/ADT/PCIe 준비가 모두 끝난 뒤 1280x720@60 mode 및 swapBegin/setLayer/end status0, swap_id2. framebuffer shutdown과 DCP/AFK/RTKit shutdown을 모두 생략하고 guest entry `0x83caf4800`로 진입했습니다.
- [FACT] UEFI/guest가 FL1100을 초기화하고 USB 전송 완료를 122회 소비했습니다. D33 시작 뒤 300초까지 `Guest exception`/`Exception taken`/canonical Windows ELR은 0회입니다. DCP syslog에서 guest jump 전 IOAV termination/HPD removal은 없었습니다. 물리 HDMI 화면 관찰 결과는 아직 별도 확인되지 않았습니다.
- [FACT] 약 초기 전송 이후 더 진행하지 않고 동일 미완료 URB에서 반복 대기: `USBSTS=0x18` 관찰 시 controller HCH=0/HSE=0, endpoint slot1/dci5 EPState=1(RUNNING), 64-byte single TRB `urbTrbStart=0xADE961000`, DataPhy=0x1dc000→host0xADE894000. 이벤트 dequeue에는 다른 transfer event `TRBPtr=0x10bb90`가 보이며 현재 URB와 일치하지 않습니다. 5분 동안 새 consume 수는122에서 증가하지 않았습니다.
- [CONCLUSION] Tahoe DCP를 종료하지 않는 즉시 guest handoff는 실행 경로로 성립했고, D32의 늦은 DCP boot timeout도 해결됐습니다. D33의 다음 병목은 display가 아니라 FL1100의 특정 USB 전송 무완료입니다. Windows kernel 진입/Setup 표시 성공으로 과장하지 않습니다.
- [NEXT] 현재 guest를 보존해 물리 화면 관찰 여지를 남깁니다. 다음 깨끗한 실행에서는 slot1/dci5의 device/route/endpoint context와 doorbell을 함께 기록해 설치 USB 전송인지 다른 USB 장치 전송인지 먼저 분리합니다.

### S6-D34 준비 — FL1100 미소비 입력장치 TRB one-shot doorbell 재전송

- [FACT] D33의 설치 미디어는 slot2/dci3·4(USB3 root port 4)이며 블록 읽기와 상태 전송이 계속 성공했습니다. 최종 정지는 별도 USB2 root port 1의 slot1/dci5, endpoint 0x02 OUT, 64-byte 전송입니다. 따라서 설치 USB 읽기 정지가 아니라 입력장치 전송 정지입니다.
- [FACT] 정지 시 xHC는 RUNNING(HCH=0/HSE=0), endpoint는 EPState=1(Running), HW dequeue는 현재 TRB 시작(0x105001)에 머물렀고 해당 TRB completion은 게시되지 않았습니다. 즉 FL1100이 새 작업을 fetch하지 않은 형태입니다.
- [CHANGE] XhcExecTransfer에 100ms 미완료 동기 전송용 one-shot doorbell 재전송을 추가했습니다(`NWOAS_RERING_STUCK_TRANSFER=1`). 같은 slot/DCI에 한 번만 쓰므로 반복 storm은 만들지 않으며 route/rootport/speed/address를 직렬 로그에 남깁니다.
- [BUILD] Project Mu DEBUG_CLANGPDB 빌드 및 95개 PE/COFF 이미지 검증 성공. FD SHA-256 `bf1acd90e278a508d56411ae2b72ca7b71532fc67c27960a02ef16a982f3c82e`.
- [ARTIFACT] `m1n1_windows/m1n1-payload-iort-noleafdma-rering-v1.bin`, 32,342,016 bytes, SHA-256 `bbc8dfc751f3a3d7011765381ee4748a6cb46706b8407aca50fabc4cabcd0258`; 기존 payload의 앞 1,376,256 bytes와 byte-identical임을 확인했습니다.
- [NEXT] D33을 통제 종료하고 fresh serial reboot 1회 후, DCP-preserving hook과 이 D34 payload로 실기기 부팅합니다. 판정은 `NWOAS-XHC rering` 뒤 completion/Windows ELR/물리 HDMI 화면입니다.

### S6-D34 실기 결과 / S6-D35 경로 B seed 준비 (2026-09-07 00:57)

- [FACT] D34 `tahoe-dcp-guest-20260907-005145.KNxMpn`: DCP mode/swap 후 UEFI USB 완료 122회를 소비하고 Windows canonical kernel VA `0xfffff800...`에 진입했습니다. `Guest exception`/`Exception taken` 0회. D33의 slot1/dci5 정지는 재현되지 않아 one-shot rering은 발화하지 않았습니다.
- [FACT] Windows usbxhci 단계에서 10.1초 주기의 `RS=1 -> RS=0/HCRST -> RS=1`이 반복됐습니다. 고주소 Windows ERST `0xae0fc9000`의 이벤트 링은 all-zero, IMAN.IP=0이며 DART live fault는 없습니다. 이는 2026-07-14의 확인된 reset-storm/empty-high-ring 현상과 같습니다.
- [CHANGE] outer `hv_vm.c`에 `NWOAS_LOW_EVENT_SEED=1`을 구현했습니다. IOVA `0xffffc000` 16KB leaf를 EL2-owned aligned page에 매핑하고, ERST=`0xffffc000`, event segment=`0xffffd000`(256 TRB)을 구성합니다. Windows ERSTBA 완료/ERDP write/RUN 직전에 물리 FL1100 interrupter register만 저링으로 재지정합니다.
- [SAFETY] 저링은 guest에 미러하지 않는 진단 seed입니다. 장치가 쓰는 event page는 invalidate-only로 관찰하고, 매 HCRST 세대마다 zero-init합니다. Windows guest의 원래 고링·메모리는 수정하지 않습니다.
- [BUILD] outer m1n1 build 성공(기존 unused-function warning 3개만 존재). `m1n1_windows/build/m1n1.bin` 2,113,536 bytes, SHA-256 `7eaadc41cef3b9a48ad22d4bf5f54a496d04977ebd36e59942238d53866abf97`; DCP-DEFER/EVTSEED/DARTERR 마커 확인.
- [HARNESS] `nwoas_scripts/tahoe-dcp-chainload-guest-test.sh`: fresh D4 proxy에서 reboot 없이 새 outer m1n1을 1회 chainload하고, D34 UEFI+DCP-preserving hook을 실행합니다. hook은 `NWOAS_HV_IMAGE`로 chainloaded binary의 defer marker offset을 검증합니다.
- [PASS CRITERION] `EVTSEED POSTED ... type=34`이면 FL1100 저주소 event DMA-write 성공 및 고주소 write 경로 병목 확증입니다. 이 단계만으로 Windows USB/커서 성공이라 판정하지 않습니다.

### S6-D36~D45 경로 B 정리 — 32-bit DMA + runtime DART (2026-09-07 01:00~02:00)

- [FACT] 저주소 seed에서 확인한 controller-authored Port Status Change Event를 guest 경로로 전달하기 위한 실험을 순차 수행했습니다. 각 실험은 fresh bootstrap 1회, 115200 고정, 변수 1개 원칙으로 실행했습니다.
- [FACT] `NWOAS_FORCE_FL32=1`이 guest-visible FL1100 HCCPARAMS1.AC64만 0으로 바꾸자 Windows가 ERST/DCBAA/command ring을 4GB 아래 IOVA에 할당했습니다. 물리 컨트롤러는 두 개의 type-34 PSCE를 기록했고 Windows ERDP가 실제로 전진했습니다. 즉 FL1100의 event DMA와 guest consume 경로가 처음으로 연결됐습니다.
- [CHANGE] Project Mu의 AppleDartIoMmuDxe가 만드는 DART page-table 메모리를 runtime-reserved로 유지하는 payload를 만들었습니다. `m1n1-payload-iort-noleafdma-rering-runtime-dart-v1.bin`, 32,342,016 bytes, SHA-256 `e0bcd7b06fccfed2f487d22afc4a6eb1bb90c017143f163d592580791056cc3b`.
- [FACT] runtime DART + FL32 조합에서 실 PSCE 2개와 ERDP 전진이 반복 확인됐습니다. DART error register는 매번 FLAG=0, FL1100이 아닌 stream, fault bit 없음인 stale latch였고 live DMA fault는 없었습니다.
- [CONCLUSION] 7월의 “고주소 event DMA가 절대 불가능”이라는 표현은 더 좁혀야 합니다. 이 하드웨어/펌웨어 조합에서는 Windows가 FL1100 DMA 구조를 4GB 아래에 두도록 하면 controller-authored event write와 Windows consume이 모두 동작합니다. 다음 벽은 그 뒤 발생한 WHEA 0x124입니다.

### S6-D46~D49 WHEA 원인 포착 (2026-09-07 02:00~02:35)

- [FACT] DCP hook에 stable asserted-HPD 수용 경로를 추가했습니다. 모니터 전원을 꺼도 8초 뒤 1280x720 mode/swap ACK를 확정하고 guest에 진입합니다. D46 이후 headless 실행에서 동일 USB/WHEA 현상을 재현했습니다.
- [FACT] D46 common bugcheck probe가 `0x124`, parameter1=`0x12`, parameter2=비영 주소를 포착했습니다. Microsoft 문서상 0x124/Arg1 0x12는 Arm SError Interrupt입니다.
- [FACT] D47은 bugcheck 시 ESR/FAR/DISR/ISR/HCR, Apple L2 error registers, ICH_HCR/VMCR/MISR/EISR/ELRSR/LR0..7을 추가했습니다. 로그 `tahoe-dcp-fl32-runtime-dart-serror-state-20260907-021255.bT4bw1`은 10분30초 동안 WHEA 없이 유지되었고, 실 event/ERDP 전진과 LR698 회수를 확인했습니다. 이 실패가 확률적임도 확인했습니다.
- [FACT] D48은 ERDP write 다음 USBSTS read에서 한 번만 event/command/DART/DCBAA를 덤프했습니다. 로그 `tahoe-dcp-fl32-runtime-dart-postconsume-20260907-022505.cA06ma`: type-34 PSCE 두 개, ERDP 전진, 당시 command ring all-zero. 이어 WHEA 0x124/0x12가 발생했습니다. bugcheck 시 LR0=ACTIVE INTID18 priority0x80, LR1=ACTIVE INTID698 priority0x00, MISR=0, EISR=0, DART live fault=0.
- [CHANGE] D49는 WHEA CPER record를 최대 4KB로 bounded dump하는 parser를 `src/nwoas_stage8.inc`에 추가했습니다. unaligned read는 byte 단위 safe reader를 사용합니다.
- [FACT] D49 로그 `tahoe-dcp-fl32-runtime-dart-whea-cper-20260907-022952.6AVCxr`: CPER signature `CPER`, 1 section, fatal, length 0xd4. section은 Windows `WHEA_SEI_SECTION`; raw payload를 공식 packed layout `ULONG Esr; ULONG64 Far`로 읽으면 원래 guest ESR=`0xbf40100b`, FAR=`0x0000002a020af4b0`입니다. ESR EC=0x2f SError, IDS=1 implementation-defined syndrome입니다.
- [CONCLUSION] WHEA는 DART fault를 포장한 오류가 아니라 guest가 받은 실제 Arm SError입니다. 실패 순간의 유일한 비정상 상태는 timer18 ACTIVE(priority0x80) 중 FL1100 IRQ698을 강제 최고 priority0x00으로 선점시켜 두 LR가 동시에 ACTIVE였던 것입니다. M1/M2 vGIC의 illegal-state SEI 경로와 일치하는 강한 근거입니다.

### S6-D50 WHEA 수정 — IRQ698 정상 priority (2026-09-07 02:35~02:47)

- [CHANGE] `src/hv_exc.c`의 두 IRQ698 inject 경로가 강제 `0x00` 대신 guest GICD 설정값 `hv_vgic3_get_priority(698)`을 사용하도록 변경했습니다. 실측값은 `0x80`입니다. 이 한 변수만 변경했습니다.
- [BUILD] outer m1n1 2,097,152 bytes, SHA-256 `7f80370b092c9bc90615fed6414025bc8f2dc5066055ef9af7574b7761de1055`. 보존본 `build/m1n1-d50-prio80.bin`. 하네스 `tahoe-dcp-fl32-runtime-dart-prio80-guest-test.sh` SHA-256 `ed3791c7c3ce05815ffc0d525d05b9a8f7afa6d8d4e7937204a86fabd0cdd650`.
- [FACT] 로그 `tahoe-dcp-fl32-runtime-dart-prio80-20260907-023617.ko5vas`: controller-authored type-34 두 개, ERDP `+0x20`(16-byte TRB 두 개 consume), IRQ698 priority0x80 및 LR 회수 확인. 600초 동안 WHEA/CPER/guest exception/reset 0회.
- [CONCLUSION] forced priority0x00이 WHEA Arm SError의 작동 원인이었다는 실기기 양성 수정 결과입니다. WHEA 수정과 Windows UI/USB 최종 성공은 구분합니다.

### S6-D51 빈 IRQ698 storm 제거 (2026-09-07 02:48~02:59)

- [FACT] D50에서 Windows가 두 event를 모두 소비하고 IMAN.IP를 0으로 내린 뒤에도 USBSTS.EINT가 1로 남았습니다. 기존 bridge의 `IP || EINT` 조건은 이 summary bit만으로 빈 IRQ698을 무한 재주입했습니다.
- [CHANGE] bridge pending 조건을 xHCI interrupter의 실제 line-pending인 `IMAN.IP` 하나로 제한했습니다. IRQ priority0x80 수정은 유지합니다. 이 한 변수만 변경했습니다.
- [BUILD] outer m1n1 2,097,152 bytes, SHA-256 `f29a72feef41dba19bdc5e01354f2763967ba169303a2bbb509a7a362325abc3`. 보존본 `build/m1n1-d51-prio80-iponly.bin`. 하네스 SHA-256 `173fcb7b050e1a52e2781b4e9ca63a730478bba1c918459512e0e5cdf2a5fc59`.
- [FACT] 로그 `tahoe-dcp-fl32-runtime-dart-iponly-20260907-024848.ARUhcc`: 두 실 PSCE consume 후 IMAN.IP=0에서 LR698 outstanding=0을 유지했습니다. 600초 동안 WHEA/CPER/guest exception/reset 0회. guest PC는 ntoskrnl canonical VA에서 계속 실행했고 후반에는 known idle 경로(rva 약 0x434b68)와 timer/DPC 경로를 오갔습니다.
- [UNVERIFIED] 한 번 관찰된 low VA `0x7ffabebdf938`은 user-mode일 수 있으나 UEFI runtime call일 수도 있으므로 user-mode/Setup UI 성공 증거로 단정하지 않습니다. late command ring 및 물리 커서는 아직 확인하지 않았습니다.
- [CONCLUSION] D51은 WHEA 없이 안정적인 Windows kernel idle까지 도달한 현재 최선의 runtime입니다. 최종 §3.2 판정(실 USB HID/커서)은 아직 미달입니다.

### S6-D52 framebuffer 관측 시도 (2026-09-07 03:00)

- [DESIGN] D51 바이너리/runtime을 바꾸지 않고 SIGINT 시 preserved 1280x720 BGRA framebuffer를 읽는 `proxyclient/tools/run_guest_fbsnapshot.py`와 stdlib PNG converter `nwoas_scripts/bgra_to_png.py`를 추가했습니다.
- [FACT] D52 로그 `tahoe-dcp-fl32-runtime-dart-iponly-fbsnapshot-20260907-030012.f7Ozmo`는 Windows 진입 전 Project Mu가 PC=`0xffffffffffffffff`, ESR=`0x8a000000`(pc misaligned)로 조기 실패하고 target reset했습니다. framebuffer read 단계에는 도달하지 않았습니다.
- [CONCLUSION] D52는 관측 실패로 보존하며 같은 실험 내 재시도하지 않았습니다. D51 600초 성공을 반증하지 않습니다. cooldown 뒤 새 실험 번호로 동일 observational capture를 1회 재검증합니다.

### 현재 재개점 (2026-09-07 03:02)

- [FACT] 현 `build/m1n1.bin`과 보존본 `m1n1-d51-prio80-iponly.bin` SHA-256은 `f29a72feef41dba19bdc5e01354f2763967ba169303a2bbb509a7a362325abc3`입니다.
- [FACT] 모니터는 HDMI 케이블을 연결한 채 전원만 꺼도 됩니다. stable-HPD path가 headless guest entry를 반복 확인했습니다. 화면 확인이 필요할 때만 전원을 켜면 됩니다.
- [NEXT] D53: 충분한 cooldown 뒤 D51 unchanged + framebuffer snapshot runner를 새 bootstrap 1회로 재검증. 성공 시 raw BGRA를 PNG로 변환해 UI 내용을 판독합니다. 실패 시 D51 표준 runner로 돌아가 late command/event/context one-shot dump를 추가하되 runtime 변수는 바꾸지 않습니다.

### S6-D53~D56 Windows Setup DCP scanout 입증 (2026-09-07 03:04~03:32)

- [FACT] D53 `tahoe-dcp-fl32-runtime-dart-iponly-fbsnapshot2-20260907-030426.7i4cvW`는 D52 조기 reset 뒤 새 bootstrap 1회로 Windows까지 330초 실행했으나, pending `HV_START` 중 외부 SIGINT가 Python proxy를 빠져나오지 못했습니다. SIGTERM으로 통제 종료했고 framebuffer read는 실행되지 않았습니다.
- [FACT] D54 `tahoe-dcp-fl32-runtime-dart-iponly-fbsnapshot-auto-20260907-031145.QdX7jO`는 SIGALRM으로 `hv.start()`를 끊었지만 남은 HV_START reply/TTY frame과 direct readmem reply가 충돌해 `UartCMDError expected 0x02aa55ff got 0x00aa55ff`가 발생했습니다. 이 proxy full-framebuffer 방식은 폐기했습니다.
- [CHANGE] D55는 guest kernel 관측 120초 뒤 EL2에서 preserved DCP framebuffer 0xbe3f60000의 전체 1280x720 BGRA 표면을 한 번 invalidate/read/hash하고, guest 직전 8색 bar baseline과 전 픽셀을 비교했습니다. guest/MMIO write 없음.
- [FACT] D55 `tahoe-dcp-fl32-runtime-dart-iponly-fbprobe-20260907-031809.48HEHC`: `fnv=0x763dc68643d9ebd4`, `bar_mismatch=921600/921600`, `bbox=0,0-1279,719`, 모든 표본은 `ff180052`. Windows가 guest 전 test bars를 전 화면 dark-purple 표면으로 완전히 교체했습니다.
- [CHANGE] D56은 같은 probe에 4x4 OR downsample 320x180 non-background mask 출력만 추가했습니다. outer binary SHA-256 `fb0f7a678ac29109f92cc4cd4953d899c169049b50c6808b09ad10dcc39549fc`.
- [FACT] D56 `tahoe-dcp-fl32-runtime-dart-iponly-fbmask-20260907-032344.NK1GxR`: `bg=ff180052`, `nonbg=68115`, non-background bbox `330,131-948,588`, light pixels 39712, complete 180-row `FBMASK`와 END를 기록했습니다. WHEA/CPER/reset 없음.
- [ARTIFACT] decoded mask `...NK1GxR.preview.png` SHA-256 `ff90e550f75e6a84b8e3ad17cf0b9c3d89a0c7c6fa6ee58a46b05029589a4074`; 4x preview `...NK1GxR.preview-4x.png` SHA-256 `a023b68ed8efb88e578120b04fb1babc4cd0220fbacf7fcaf1373fdf0aca40b1`.
- [CONCLUSION] mask에는 Windows logo/title, 세 개의 긴 dropdown, 하단 설명, 우하단 버튼이 있는 중앙 Windows Setup 첫 언어 선택 대화상자가 명확히 보입니다. Tahoe DCP를 보존한 native guest가 Windows Setup UI를 실제 scanout buffer에 렌더링한 증거입니다. 물리 모니터는 전원이 꺼져 있었고 USB HID/커서 동작은 아직 별도 검증 대상입니다.
- [DESIGN] D57은 D56 runtime을 유지하고 동일 +120초 시점에 command/event ring, DCBAA, controller-authored output device context를 한 번 읽습니다. Enable Slot/Address Device와 live interrupt endpoint를 통해 FL1100의 Windows-side USB/HID 열거 여부를 판정합니다.

### S6-D57 FL1100 Windows-side USB/HID 열거 성공 (2026-09-07 03:33~03:38)

- [BUILD] outer `m1n1-d57-lateusb.bin` 2,129,920 bytes, SHA-256 `3760f5ac18706fb0d25f4660f4afa545e935b1e78b16853f1e131b7afa67abca`. 하네스 `tahoe-dcp-fl32-runtime-dart-iponly-lateusb-guest-test.sh` SHA-256 `dded9f6a964b4469dab6a42b00d0ff14fecff8f11737395385185571ec3de9b9`.
- [FACT] fresh bootstrap 1회 `bootstrap-serial-20260907-033257-237085.log`에서 115200 NOP PASS. D57 로그 `tahoe-dcp-fl32-runtime-dart-iponly-lateusb-20260907-033349.hyV9bA`는 동일 Setup framebuffer fingerprint/mask를 재현했고, 270초까지 WHEA/CPER/guest exception/reset 0회였습니다.
- [FACT] late command ring에는 유효 구간 index 0..31 안에 Configure/Evaluate, Reset Endpoint, Set TR Dequeue, Enable Slot(type9), Address Device(type11), 마지막 Link(type6)가 남아 있었습니다. D57 원시 출력의 index32 이후는 Link 뒤의 무관 메모리이므로 command로 세지 않습니다.
- [FACT] event ring에는 controller-authored PSCE(type34), Command Completion(type33), Transfer Event(type32)가 다수 존재했습니다. DCBAA에는 nonzero output device context 두 개가 있고 두 슬롯 모두 Addressed/Configured state3입니다.
- [FACT] SLOT1은 speed4 SuperSpeed/rootport5/address1이며 control + bulk-IN/OUT endpoint가 Running으로, Windows 설치 USB 대용량 저장장치 형태입니다.
- [FACT] SLOT2는 speed1 Full-speed/rootport2/address2, context entries10이며 DCI3/5/9 interrupt-IN(type7, max packet 32/64/64), DCI10 interrupt-OUT(type3, max packet64)이 모두 EPState1 Running입니다. 복합 USB 입력장치의 주소 할당과 endpoint configure가 완료된 상태입니다.
- [CONCLUSION] Windows Setup이 FL1100을 초기화한 뒤 실제 설치 USB와 복합 입력장치를 열거하고 HID interrupt endpoint를 실행 상태로 만들었습니다. 물리 마우스 이동/키 입력에 따른 report completion과 화면 커서 변화는 사용자가 자는 동안 입력하지 않으므로 아직 별도 판정입니다.
- [DESIGN] D58은 D57과 같은 runtime에서 command parsing을 Link에서 정확히 끝내고, event ring 전체의 slot/endpoint completion 수와 SLOT2 interrupt transfer ring/report buffer를 bounded read합니다. HID driver가 실제 IN transfer를 queue했는지 확인하는 관측 변수 하나입니다.

### S6-D58 Windows HID transfer queue 실기 확인 (2026-09-07 03:42~03:48)

- [BUILD] outer `m1n1-d58-hidrings.bin` 2,129,920 bytes, SHA-256 `183a73f9eb45eaec202fab9a28ffe7f77ebdb1a35ffef84dae8e0a03a8798d76`. 하네스 `tahoe-dcp-fl32-runtime-dart-iponly-hidrings-guest-test.sh` SHA-256 `2178f409e1442186d54f6a5b179ea1ce29017facca4bfb6799876af9b482ae42`.
- [FACT] fresh bootstrap `bootstrap-serial-20260907-034151-676011.log` NOP PASS. D58 `tahoe-dcp-fl32-runtime-dart-iponly-hidrings-20260907-034231.cqAXte`는 동일 Setup framebuffer fingerprint를 재현하고 330초까지 WHEA/CPER/guest exception/reset 0회였습니다.
- [FACT] 유효 command ring은 Link TRB index31에서 정확히 종료되며 nonzero 32개입니다. event ring 256개 중 controller-authored Transfer Event는 SLOT1 173개, SLOT2 44개입니다. SLOT2의 기존 완료는 모두 control endpoint DCI1로, 실제 descriptor/configuration transaction이 완료된 증거입니다.
- [FACT] SLOT2 HID interrupt-IN DCI3/5/9 각각 EPState1 Running. Windows가 각 endpoint에 두 개의 Normal TRB와 Event Data TRB를 queue했습니다: DCI3 report length21, DCI5 length8, DCI9 length33. 이는 mouse/keyboard/vendor report 형태의 복합 입력장치 polling입니다. interrupt-OUT DCI10도 Running이나 idle이라 queued TRB는 0개입니다.
- [FACT] 여섯 IN report buffer의 관측값은 모두 0입니다. 사람이 입력하지 않은 상태에서 endpoint가 NAK/대기하는 정상 idle과 일치합니다. 따라서 physical report completion/커서 변화로 과장하지 않습니다.
- [CONCLUSION] Tahoe DCP 화면, Windows Setup, FL1100 설치 USB, Windows-side 복합 HID 열거, Running interrupt endpoints, 실제 HID receive queue까지 실기 검증했습니다. 남은 한 항목은 물리 키/마우스 동작으로 새 SLOT2 DCI3/5/9 completion을 만드는 것입니다.
- [DESIGN] D59는 D58 baseline 뒤 controller-authored event ring을 초당 1회 조용히 읽고, 새 SLOT2 non-control Transfer Event만 기록합니다. 첫 실제 HID report 시 scanout hash도 한 번 기록하며, 모니터는 계속 꺼 둘 수 있습니다.

### S6-D59 overnight physical HID watcher 가동 (2026-09-07 03:50~)

- [BUILD] outer `m1n1-d59-hidwatch.bin` 2,129,920 bytes, SHA-256 `6bb61f7a6367073397790fe1f1f668f526d9847652ae76629f0adca1b5b03277`. 하네스 `tahoe-dcp-fl32-runtime-dart-iponly-hidwatch-guest-test.sh` SHA-256 `3170815ec2ea9e48f73f88ef7b47be64be2136415a73f1dc152b1d827be31cad`.
- [FACT] fresh bootstrap `bootstrap-serial-20260907-035008-866369.log` NOP PASS. live log `tahoe-dcp-fl32-runtime-dart-iponly-hidwatch-20260907-035048.CHOGAO`.
- [FACT] 동일 Windows Setup framebuffer fingerprint, 두 USB slot 및 세 HID interrupt-IN queue를 재현했고 `LATEUSB END live_slots=2 hid_watch=1`로 watcher가 활성화됐습니다. baseline 시점까지 WHEA/CPER/guest exception/reset 0회.
- [LIVE] Python runner PID 83912가 direct serial `/dev/cu.usbmodemC07HL05SQ6NY1`을 소유하고 guest를 계속 실행 중입니다. 모니터는 HDMI 연결/전원 OFF 그대로 둡니다. 새 SLOT2 DCI3/5/9 Transfer Event가 생기면 `HIDACT PHYSICAL-REPORT`와 framebuffer hash를 한 번 기록합니다.
- [LIMIT] 현재 report buffer는 idle zero이고 사용자가 자는 동안 물리 입력을 만들지 않습니다. 따라서 최종 커서 이동 판정은 실제 마우스 이동 또는 키 입력이 들어오는 순간 watcher 결과와 물리 화면을 함께 확인해야 합니다.
- [STABILITY] D59는 최소 630초까지 WHEA/CPER/guest exception/reset/HIDACT 0회로 안정 유지. `HIDACT` 0회는 실패가 아니라 물리 입력이 전혀 없었던 idle 결과입니다. IRQ698 priority0x80, LR outstanding=0, IMAN.IP=0 상태를 유지합니다.
- [SUPERVISOR] read-only `nwoas_scripts/overnight_hidwatch_supervisor.py`를 unified session PID 85364로 가동했습니다. serial을 열거나 target을 제어하지 않고 live log만 읽어 `nwoas_scripts/logs/overnight-hidwatch-state.json`을 30초마다 atomic update합니다. 확인 state: runner alive, Setup framebuffer=true, watcher armed=true, WHEA/CPER/bugcheck/guest exception=0, physical HID report=false, alive=780s.
- [OVERNIGHT CHECK 04:06] guest session 53110과 supervisor session 37375를 직접 폴링해 둘 다 live임을 확인했습니다. supervisor state alive=930s, Setup framebuffer=true, HID watch armed=true, WHEA/CPER/bugcheck/guest exception=0, physical HID report=false. target 상태 변경/추가 reboot 없음.

### S6-D60 post-HID scanout 관측 정확도 개선 — staged only (2026-09-07 04:08)

- [DESIGN] D59 live guest는 그대로 두고, 다음 재현용 watcher만 physical HID completion 뒤 즉시 hash하지 않고 +1/+2/+3초에 scanout을 검사하도록 변경했습니다. Windows가 report를 소비하고 software cursor를 그릴 시간을 확보하는 관측 변수 하나입니다.
- [BUILD] `m1n1-d60-hidwatch-delay.bin` 2,129,920 bytes, SHA-256 `696e3de7704d4165caf1e23f55e60cd0f03bfd89f7be96112651c7dac4d14442`. 하네스 `tahoe-dcp-fl32-runtime-dart-iponly-hidwatch-delay-guest-test.sh` SHA-256 `44e5c8eb1495f1f8eec7d110bb189dbc8eb8664a1c38ceffbed58f05aa305a71`. make/git diff --check/bash -n/marker 검증 PASS.
- [UNVERIFIED] D60은 staged only이며 실기기 실행하지 않았습니다. 현재 target은 검증된 D59 binary를 유지하고 추가 reboot 없음.
- [OVERNIGHT CHECK 04:08] live D59 guest/supervisor 핸들 재폴링 PASS, alive=1050s, 오류 0회, HIDACT 0회.
- [SUPERVISOR v2 04:12] live guest를 건드리지 않고 supervisor만 교체했습니다(session 60852, PID 86421). JSON에 `verdict`와 원시 `hidact_lines`를 추가하고, 실제 report가 오면 `overnight-hidwatch-evidence.txt`를 atomic 생성합니다. synthetic fixture로 `PHYSICAL_HID_PASS`/evidence 생성 검증 PASS. 실제 live state는 `WAITING_FOR_PHYSICAL_INPUT`, alive=1260s, 오류 0회입니다.
- [OVERNIGHT STABILITY 04:27] 동일 D59 guest session 53110을 연속 직접 폴링해 1800초(30분) 도달을 확인했습니다. Setup framebuffer/HID watcher armed 유지, WHEA/CPER/bugcheck/guest exception 0회. 물리 입력이 없어서 HIDACT 0회이며, 추가 reboot/target 변경 없음.

### Known-good runtime manifest (2026-09-07 04:23)

- [ARTIFACT] `NWOAS-RUNTIME-MANIFEST-2026-09-07.json`에 D50/D51/D57~D60 outer binaries, runtime-DART Project Mu payload, Tahoe DCP hook, FL1100 module, D59/D60 harness, supervisor, Setup preview 및 D49~D59 결정 로그를 역할/검증수준/크기/SHA-256과 함께 묶었습니다.
- [CORRECTION] 최초 manifest 검증에서 계속 append되는 live D59 log의 hash가 생성 직후 달라지는 결함을 발견했습니다. live log는 mutable monitor 경로로 분리하고, 생성 시점 442,062 bytes를 `...CHOGAO.snapshot-20260907-042317`로 불변 보존했습니다(SHA-256 `52ef66a6a4ffc0ba0da345df96775650acec003dd3da543557882aa8c852cfa0`).
- [VERIFY] 수정 manifest SHA-256 `4eac36c48d864f5c2a875aef6d8142495cf123facac09d1e685025fc93e7094a`, 7,828 bytes. 포함된 불변 artifact/evidence 21개를 독립 재해시해 size/SHA-256 모두 PASS.
- [OVERNIGHT CHECK] live guest/supervisor 직접 재폴링 PASS, alive=1950s, verdict=`WAITING_FOR_PHYSICAL_INPUT`, 오류 0회/HIDACT 0회. snapshot 생성 외 target 변경 없음.
- [VERIFY TOOL] `nwoas_scripts/verify_runtime_manifest.py`를 추가해 manifest의 schema, 모든 immutable file의 존재/크기/SHA-256, cursor→physical-HID completion invariant, mutable live-log 존재를 재검증하도록 했습니다. py_compile PASS, 실제 manifest 검증 `PASS runtime manifest: 22 immutable files`.
- [MANIFEST] verifier 자체를 포함한 갱신본은 8,095 bytes, SHA-256 `e4190b36c89d5e1b2004f441cc11dc5535664a9ee9f0eac2814d02458dace9cb`입니다.
- [OVERNIGHT CHECK 04:25] live guest/supervisor 직접 재폴링 PASS, alive=2040s, verdict=`WAITING_FOR_PHYSICAL_INPUT`, 오류 0회/HIDACT 0회. target 변경 없음.
- [OVERNIGHT STABILITY 04:36] 동일 D59 guest session을 연속 직접 폴링해 2730초(45분30초) 도달. Setup/HID watch armed 유지, WHEA/CPER/bugcheck/guest exception/HIDACT 0회. additional reboot/target 변경 없음.
- [OVERNIGHT STABILITY 04:52] 동일 D59 guest session 53110과 supervisor session 60852를 직접 재폴링하고 state/log/process를 교차 확인해 3690초(1시간1분30초) 연속 실행을 확정했습니다. Setup framebuffer=true, HID watcher armed=true, verdict=`WAITING_FOR_PHYSICAL_INPUT`, WHEA/CPER/bugcheck/guest exception 0회, runner PID 83912 live입니다. 사람이 입력하지 않아 HIDACT 0회이며 추가 reboot/target 변경 없음. 모니터는 HDMI 연결/전원 OFF 상태를 유지합니다.
- [JOURNAL 04:54] live state JSON이 30초마다 교체되는 점을 보완하기 위해 read-only `nwoas_scripts/overnight_hidwatch_journal.py`를 추가하고 session 68815로 가동했습니다. serial/target/process에 신호를 보내지 않고 `overnight-hidwatch-state.json`만 읽어 30분 경계와 상태 전환을 `overnight-hidwatch-journal.jsonl`에 append합니다. py_compile 및 synthetic terminal-state fixture PASS. script SHA-256 `c71bada07b3ca7d2e4d8e9367aeb90d797a7726bfc1cea72b2c40fe6f32217f8`; 첫 실제 기록 alive=3810s, verdict=`WAITING_FOR_PHYSICAL_INPUT`, 오류 0회.

### S6-D61 post-HID tile-localized scanout observer — staged only (2026-09-07 04:59)

- [DESIGN] D60의 +1/+2/+3초 표본 시점과 HID 판정은 유지하고, stable Setup surface를 16x16 픽셀 타일 80x45=3600개의 FNV 해시로 한 번 보존합니다. physical HID completion 뒤 전체 FNV와 함께 changed-tile 수 및 pixel bbox를 출력해 작은 software-cursor 변화와 넓은 UI redraw를 구분합니다. framebuffer/USB/MMIO/guest memory write는 없습니다.
- [BUILD] `m1n1-d61-hidwatch-tilebbox.bin` 2,179,072 bytes, SHA-256 `b1991c5b9555d2a336faf53f524a92f789493d8d2c3f0851eb14846eb5556182`. 하네스 `tahoe-dcp-fl32-runtime-dart-iponly-hidwatch-tilebbox-guest-test.sh` SHA-256 `89c4125f86d00bea37faa0a689333dc8b7e4244bdc56fc929042b1bddc2fd9e6`. `gmake -j4`, `git diff --check`, `bash -n`, marker 검증 PASS.
- [UNVERIFIED] D61은 staged only/HARDWARE-UNVERIFIED입니다. 현재 검증된 D59 target을 교체하거나 재부팅하지 않았습니다. software cursor가 DCP framebuffer가 아닌 별도 hardware plane이면 tile hash가 그대로일 수 있으므로, 실제 HID completion은 여전히 독립적인 1차 증거입니다.
- [MANIFEST] D61 binary/harness와 read-only journal을 immutable artifacts에 추가했습니다. 갱신 manifest 9,067 bytes, SHA-256 `07f20b481568a467ec6e41545cc529b447b1efeb8018da7617f64441269dce43`; verifier `PASS runtime manifest: 25 immutable files`.
- [OVERNIGHT CHECK 04:59] D59 guest/supervisor/journal 실제 session 53110/60852/68815 재폴링 PASS. alive=4080s, verdict=`WAITING_FOR_PHYSICAL_INPUT`, WHEA/CPER/bugcheck/guest exception/HIDACT 0회. target 변경 없음.
- [HOST VERIFY 05:04] D61 ELF에서 baseline/sample tile 배열은 각각 `0x7080`=28,800 bytes, 합계 57,600 bytes로 확인했습니다. 큰 표본 배열은 stack이 아닌 `.bss`에 있고 전체 `.bss`는 1,158,640 bytes, 끝 주소는 약 `0x1eadf0`입니다. 전체 framebuffer 복사본 2개(약 7.0 MiB)를 보존하지 않는 compact observer 설계와 일치합니다. 같은 시점 live D59는 alive=4410s, 오류/HIDACT 0회였습니다.
- [OVERNIGHT STABILITY 05:21] 동일 D59 guest session 53110을 1시간30분 연속 직접 폴링해 5400초 경계를 통과했고 이후 5430초까지 생존을 확인했습니다. journal session 68815가 `elapsed-5400s`를 자동 기록했으며 supervisor session 60852, process PID 83912/86421도 live입니다. Setup framebuffer/HID watch armed 유지, WHEA/CPER/bugcheck/guest exception/HIDACT 0회, verdict=`WAITING_FOR_PHYSICAL_INPUT`. 추가 reboot/target 변경 없음, monitor HDMI 연결/전원 OFF 유지.

### S6-D62 physical HID report-buffer evidence — staged only (2026-09-07 05:25)

- [DESIGN] D61의 event 및 tile-localized scanout 판정은 유지하고, D58 late enumeration에서 Windows가 queue한 SLOT2 interrupt-IN Normal TRB의 report IPA/length를 endpoint당 최대 4개만 보존합니다. 첫 physical completion 시 해당 device-authored buffer를 invalidate/read하고 최대 32 bytes, nonzero byte 수를 기록합니다. USB/MMIO/report/guest memory write는 없습니다.
- [RATIONALE] 기존 Windows Transfer Event는 Event Data bit가 설정되어 parameter가 `0xffff8002...` cookie이며 Normal TRB 주소를 직접 가리키지 않습니다. D58에서 이미 확인한 stable queue buffer를 보존하는 방식이 bounded하며, 새 completion과 실제 nonzero mouse/keyboard report를 함께 판정할 수 있습니다.
- [BUILD] `m1n1-d62-hidwatch-reportbuf.bin` 2,179,072 bytes, SHA-256 `f6477d63368ea6c87cef1db015433f9faf977a38c813579989f2092025efa4ad`. 하네스 `tahoe-dcp-fl32-runtime-dart-iponly-hidwatch-reportbuf-guest-test.sh` SHA-256 `b4971a7c4d57a7748758ea25b266273abfa6486f3934e5786f2d98cf0a51c643`. `gmake -j4`, `git diff --check`, `bash -n`, ELF symbol-size/marker 검사 PASS.
- [UNVERIFIED] D62는 staged only/HARDWARE-UNVERIFIED입니다. 현재 D59 target을 교체하거나 재부팅하지 않았습니다. queued buffer가 이후 Windows에 의해 교체됐다면 stale zero일 수 있으므로 controller-authored endpoint completion이 1차 증거이고 buffer 내용은 보강 증거입니다.
- [MANIFEST] D62 binary/harness 추가 후 27 immutable files 검증 PASS. manifest 9,696 bytes, SHA-256 `8b752e1e2668050c67321eb5c2ad279687e2c290355fdf9945c3af5d3d131d18`.
- [OVERNIGHT CHECK 05:25] D59 guest/supervisor/journal 실제 핸들 재폴링 PASS, alive=5640s, 오류/HIDACT 0회, target 변경 없음.
- [OVERNIGHT STABILITY 05:51] 동일 D59 guest session 53110을 2시간 연속 직접 폴링해 7200초 경계를 통과했고 7230초까지 생존을 확인했습니다. journal session 68815가 `elapsed-7200s`를 자동 기록했고 supervisor session 60852, runner PID 83912도 live입니다. Setup framebuffer/HID watch armed 유지, WHEA/CPER/bugcheck/guest exception/HIDACT 0회, verdict=`WAITING_FOR_PHYSICAL_INPUT`. 추가 reboot/target 변경 없음, monitor HDMI 연결/전원 OFF 유지.
- [ARTIFACT 05:54] 계속 append되는 D59 live log에서 2시간 시점 985,902 bytes를 `...CHOGAO.snapshot-20260907-0552-2h`로 불변 보존했습니다. SHA-256 `b2235a4d129727524b528029ced791cb9329f1d52639ca6f89b210fa8fac2d7e`; `alive t=7200s` 존재 및 HIDACT/WHEA/CPER/BUGCHECK/guest exception 0건 재검증 PASS. manifest에 evidence로 추가 후 28 immutable files PASS, manifest 10,007 bytes SHA-256 `f29030b93b1d5c10f75be30943cc7de5b27a06eab1d0e4606ae2eacc13ff6dce`. live D59는 같은 시점 7380s까지 정상 유지.
- [HOST VERIFIER 05:58] `nwoas_scripts/verify_hidwatch_evidence.py`를 추가해 D59-D62 로그의 controller physical completion, saved report nonzero bytes, post-HID framebuffer/tile 변화, 오류를 JSON으로 독립 분류합니다. 실제 live D59는 `WAITING_FOR_PHYSICAL_INPUT`, alive=7620s, 오류 0회로 판정했고, synthetic completion+nonzero report+4-tile bbox fixture는 `PHYSICAL_HID_WITH_NONZERO_REPORT` PASS. localized scanout 변화도 supporting evidence로만 표시하며 `physical_cursor_movement_observed=false`를 강제해 물리 화면 판정을 과장하지 않습니다. verifier SHA-256 `d48d6c24121c09c1de1153351b03a78b5863b55bc71bee8e94ba3bd68810bbbc`. manifest 29 immutable files PASS, 10,368 bytes SHA-256 `d2bc954fe933e24f198196088a9256e33228ad0a334fc831c6eb90c1ab0be4da`. 현재 D59 실제 핸들 재폴링 PASS, alive=7650s.
- [OVERNIGHT STABILITY 06:21] 동일 D59 guest session 53110을 2시간30분 연속 직접 폴링해 9000초 경계를 통과했고 9030초까지 생존을 확인했습니다. journal session 68815가 `elapsed-9000s`를 자동 기록했고 supervisor session 60852, runner PID 83912도 live입니다. Setup framebuffer/HID watch armed 유지, WHEA/CPER/bugcheck/guest exception/HIDACT 0회, verdict=`WAITING_FOR_PHYSICAL_INPUT`. 추가 reboot/target 변경 없음, monitor HDMI 연결/전원 OFF 유지.
- [OVERNIGHT STABILITY 06:52] 동일 D59 guest session 53110을 3시간 연속 직접 폴링해 10800초 경계를 통과했고 원시 로그에서 10860초까지 생존을 확인했습니다. journal session 68815가 `elapsed-10800s`를 자동 기록했고 supervisor session 60852, runner PID 83912도 live입니다. Setup framebuffer/HID watch armed 유지, WHEA/CPER/bugcheck/guest exception/HIDACT 0회, verdict=`WAITING_FOR_PHYSICAL_INPUT`. 추가 reboot/target 변경 없음, monitor HDMI 연결/전원 OFF 유지.
- [ARTIFACT 06:53] D59 live log의 3시간 구간 1,340,924 bytes를 `...CHOGAO.snapshot-20260907-0652-3h`로 불변 보존했습니다. SHA-256 `579d0ac327a8a883c773865eca62b8e03a2de6c355f8e55636660d6993cf28f8`; `alive t=10800s` 존재 및 HIDACT/WHEA/CPER/BUGCHECK/guest exception 0건 재검증 PASS. manifest에 evidence로 추가 후 30 immutable files PASS, manifest 10,683 bytes SHA-256 `10bc6e76c6f39383ba4270396e7492190e8297385c1d9324f2eb766e92d0bf3b`. live D59 실제 세 핸들 재폴링 PASS, alive=10890s.

- [HOST TOOL 07:04] `nwoas_scripts/snapshot_hidwatch_at.py`를 추가했습니다. live supervisor state만 읽고 지정 uptime에 관측된 log byte prefix를 temporary file+fsync+atomic rename으로 보존하며 기존 snapshot overwrite를 거부합니다. py_compile, immediate-copy, byte identity, no-overwrite synthetic 검증 PASS. SHA-256 `67a45dc9be5377ca4c804b956a10118fc047ccf1e2366ae637e040eb831e31d9`. 3시간30분(12600s) snapshot waiter session 85875/PID 4452 가동 중이며 target/serial 접근과 reboot 없음. 같은 시점 live D59 alive=11580s, 오류/HIDACT 0회입니다.
- [HOST TOOL 07:08] `snapshot_hidwatch_on_terminal.py`를 추가해 supervisor가 physical HID 또는 WHEA/CPER/bugcheck/guest exception/runner stop을 판정하는 즉시 원시 log prefix를 atomic snapshot으로 보존하도록 session 96782를 가동했습니다. physical-HID synthetic fixture/py_compile PASS, SHA-256 `33de486fc9dfbd3e274660650999fa24aa83731d1c7020f91e89c4506948eac2`. target/serial 접근과 reboot 없음. live D59 alive=11970s, 오류/HIDACT 0회.
- [OVERNIGHT STABILITY 07:21] 동일 D59 guest가 3시간30분(12600s) 경계를 통과했고 journal의 `elapsed-12600s` 기록을 확인했습니다. 자동 prefix snapshot `...CHOGAO.snapshot-20260907-3h30m`은 1,513,242 bytes, SHA-256 `ebb0e086842ecf7155605431873b286ec7d8f3aff878d57021ec242fc1d9d2f9`; `alive t=12600s` 1건, HIDACT/WHEA/CPER/BUGCHECK/guest exception/SError/PANIC/reset 0건 독립 검증 PASS. live guest는 12630s까지 정상, 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT NEXT 07:23] 4시간(14400s) 자동 snapshot waiter session 60607/PID 6505를 시작했습니다. physical-HID-or-terminal watcher session 96782/PID 5331도 유지 중입니다. 두 프로세스는 supervisor state/live log read-only이며 target/serial/reboot 동작 없음. live D59 12780s, 오류/HIDACT 0회.
- [OVERNIGHT STABILITY 07:36] 동일 D59 guest가 3시간45분(13500s) 경계를 통과했고 13530s까지 live입니다. guest/supervisor/journal/physical-HID-or-terminal/4h-snapshot 실제 세션 재폴링 PASS, WHEA/CPER/bugcheck/guest exception/HIDACT 0회. 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT STABILITY 07:51] 동일 D59 guest가 4시간(14400s) 경계를 통과했습니다. 자동 snapshot `...CHOGAO.snapshot-20260907-4h`은 1,694,988 bytes, SHA-256 `b656898169c0901cae0be9794847202d9748eece4ec8c7d1423d9f6559b63cee`; `alive t=14400s` 1건, HIDACT/WHEA/CPER/BUGCHECK/guest exception/SError/PANIC/reset 0건 독립 검증 PASS. journal `elapsed-14400s` 확인, 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT STABILITY 08:06] 동일 D59 guest가 4시간15분(15300s) 경계를 통과했고 15330s까지 live입니다. guest/supervisor/journal/physical-HID-or-terminal/4h30m-snapshot 실제 세션 재폴링 PASS, WHEA/CPER/bugcheck/guest exception/HIDACT 0회. 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT STABILITY 08:21] 동일 D59 guest가 4시간30분(16200s) 경계를 통과했습니다. 자동 snapshot `...CHOGAO.snapshot-20260907-4h30m`은 1,875,425 bytes, SHA-256 `9c9cc90b20e8e6b754556bae1765e456607aa327e3bc8fe900c6a152707af66c`; `alive t=16200s` 1건, HIDACT/WHEA/CPER/BUGCHECK/guest exception/SError/PANIC/reset 0건 독립 검증 PASS. journal `elapsed-16200s` 확인, 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT STABILITY 08:36] 동일 D59 guest가 4시간45분(17100s) 경계를 통과했습니다. guest/supervisor/journal/physical-HID-or-terminal/5h-snapshot 실제 세션 재폴링 PASS, WHEA/CPER/bugcheck/guest exception/HIDACT 0회. 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT STABILITY 08:52] 동일 D59 guest가 5시간(18000s) 경계를 통과했습니다. 자동 snapshot `...CHOGAO.snapshot-20260907-5h`은 2,057,437 bytes, SHA-256 `b0f0e9889f410bd614accf0e8da2b5a32144ac6ee93eb55bec9c8da811a546a8`; `alive t=18000s` 1건, HIDACT/WHEA/CPER/BUGCHECK/guest exception/SError/PANIC/reset 0건 독립 검증 PASS. journal `elapsed-18000s` 확인, runner는 18030s까지 live이며 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT STABILITY 09:08] 동일 D59 guest가 5시간15분(18900s) 경계를 통과했고 19020s까지 live입니다. guest/supervisor/journal/physical-HID-or-terminal/5h30m-snapshot 감시를 유지하며 WHEA/CPER/bugcheck/guest exception/HIDACT 0회입니다. 추가 reboot/target 변경 없음, monitor 전원 OFF 유지.
- [OVERNIGHT STABILITY 09:21] 동일 D59 guest가 5시간30분(19800s) 경계를 통과했습니다. 자동 snapshot `...CHOGAO.snapshot-20260907-5h30m`은 2,237,571 bytes, SHA-256 `4488db17efe1a1114531fe7756a51c2aab2f6ff63a75a05990e6755b75e10545`; `alive t=19800s` 1건, HIDACT/WHEA/CPER/BUGCHECK/guest exception/SError/PANIC/reset 0건 독립 검증 PASS. journal `elapsed-19800s` 확인, 추가 reboot/target 변경 없음. 사용자가 monitor 전원을 켰으나 물리 HDMI는 검은 화면으로, 다음 D62에서 power-on sink 초기화부터 검증합니다.
- [D62 POWER-ON DISPLAY RUN 09:25] 5h30m D59 보존 뒤 observer와 guest를 SIGTERM 종료하고 bootstrap reboot 정확히 1회/NOP 115200 PASS. D62는 powered-on HDMI sink에서 HPD True→False→True, post-hotplug 1280x720 mode/swap ACK, link=1/controller power-on 및 5초 color-bar hold까지 도달했습니다. 직후 Project Mu가 `PC=0xffffffffffffffff`, ESR `0x8a000000`으로 조기 실패해 Windows/HID에는 도달하지 못했습니다. 과거 D52와 같은 비결정적 early-entry failure이며 WHEA 재발 아님. runner SIGTERM 종료, 연속 reboot 없음. main log SHA-256 `4c46015232a0c196174e0f9bbc4368d864f42871e091ec25df2a54dbfb9973b3`. 물리 color-bar 관찰 여부는 사용자 확인 대기.
- [PHYSICAL HDMI PROOF 09:28] 콘치님이 D62 power-on run에서 정확한 8색 test bars(흰/노랑/하늘/연두/핑크/빨강/파랑/회색), 직렬 로그 및 Asahi logo의 실제 모니터 출력을 보고했습니다. DCP 로그의 HPD True→False→True, 1280x720 mode/swap ACK, link=1/controller power-on과 결합해 Tahoe 물리 HDMI scanout 성공을 확정합니다. 이후 정지는 Project Mu early-entry failure이며 display 송출 실패가 아닙니다.
- [D63 PHYSICAL SETUP/HID BOTTLENECK 09:38] exact D59 binary + powered monitor로 Windows Setup 물리 표시 성공(콘치님 관찰), 450s 이상 WHEA/CPER/bugcheck/guest exception 0회. 키보드는 UI 무반응이지만 late probe의 DCI3 interrupt-IN buffer에 HID usage `0x4f`(Right Arrow와 일치), DCI9에도 nonzero report가 기록됐습니다. 반면 새 SLOT2 non-control Transfer Event/HIDACT는 0, IMAN.IP=0. 입력은 device→FL1100→report DMA까지 도착하고 transfer completion 게시가 막힌 상태로 판정. D64는 유일 변경으로 doorbell 전에 각 endpoint transfer ring 첫 0x100 bytes를 clean하며, 관측 report buffers와 무중첩 검증 PASS. D63 SIGTERM 종료, log SHA-256 `7f0ce9647c739f81fe8339b5b2510bcefe74842298d0af8513b49b63edfa13c8`.
- [D64 TRANSFER CLEAN 09:46] powered monitor/Windows Setup, HID watch armed, 300s WHEA-free. DCI3 report `0x2c`(Space와 일치)가 DMA buffer에 나타났으나 HIDACT/Transfer Event 0회로 Windows UI 무반응. D64 로그에서 DCBAA[0] scratchpad를 slot처럼 순회한 결함을 발견해 결과를 최종 양성으로 쓰지 않음. 경계검사로 invalid high pointer clean은 skip됐으나 clean log cap을 소진. D65에서 loop start를 1로 교정, build/hash/diff/bash 검증 PASS. D64 SIGTERM 종료, log SHA-256 `f852ba97ded134a860b8856a45653d190de820247e77646dd081363fdc6e0aed`.
- [D65/D66 09:51] D65는 corrected DCBAA slot1+ traversal이나 Project Mu `PC=-1` early-entry failure로 Windows 전 미판정, SIGTERM 종료, log SHA-256 `5c102d4ed9cff22d6275c1a9ee0dfb62aaa6edf9cf8d3c6625b1648be9a9b72b`. D66은 D61/D62 extended tile/report arrays를 compile-out하여 hardware-proven D59와 동일한 2,129,920 bytes로 복귀하고, D65의 slot1+ bounded transfer-ring clean만 유지. build/diff/bash/marker 검증 PASS, SHA-256 `31d5925cee0adab99a19d2b5d6955ebc0ff7a9dd8eff15540798ca6db2be7efb`.

### S6-D66 물리 키보드 입력 성공 / Windows Setup 다음 관문 (2026-09-07 09:59~10:00)

- [CHANGE] D66은 D59와 동일한 outer image 크기(2,129,920 bytes)를 유지하면서, DCBAA slot 1부터 각 endpoint transfer ring의 첫 0x100 bytes를 Windows xHCI doorbell 직전에 bounded clean합니다. scratchpad인 DCBAA[0]은 제외했습니다.
- [FACT] live log `tahoe-dcp-d66-d59lite-xferclean-hidwatch-20260907-095242.RJeiD6`에서 slot2 DCI3의 controller-authored Transfer Event 4건을 확인했습니다. `cc=13`은 interrupt-IN 요청 길이보다 짧은 HID report가 완료된 정상 Short Packet이고 residual=9입니다. 이어 `HIDACT PHYSICAL-REPORT slot=2 ep=3 baseline=0 now=4`가 기록됐습니다.
- [FACT, 사용자 물리 관찰] Windows Setup에서 키보드 Enter가 실제로 반영되어 오류 대화상자를 닫았고, “지금 설치” 재진입 후 제품 키 입력 화면으로 진행했습니다. 따라서 D64/D65에서 추가한 transfer-ring coherence fix가 FL1100 키보드 입력 경로를 실사용 가능 상태로 만든 양성 결과입니다.
- [EVIDENCE] 360초 prefix snapshot `...RJeiD6.snapshot-physical-keyboard`, 270,478 bytes, SHA-256 `8adea934d65231087a4989561de8f746fa08ee6d736ae50876d8aea673fec9c3`; independent verifier verdict=`PHYSICAL_HID_COMPLETION`, WHEA/CPER/bugcheck/guest exception 모두 0.
- [FACT] WINARM2 제작 기록상 설치 이미지는 `sources/install.swm` 분할 형식입니다. [UNVERIFIED] 최초 `install.wim` 없음/[OSImage] 메시지는 USB에 남은 응답 파일 또는 Setup의 이미지 경로 판정이 분할 이미지 구성과 맞지 않아 생겼을 가능성이 큽니다. 재진입은 수동 UI 경로로 진행됐습니다.
- [FACT] 이어 “이 PC에서는 Windows 11을 실행할 수 없음”이 표시됐습니다. Project Mu 환경에서 TPM/Secure Boot 요구사항 판정이 실패한 것으로, 현재 WinPE의 `HKLM\\SYSTEM\\Setup\\LabConfig`에 TPM/SecureBoot/CPU/RAM/Storage bypass 값을 넣어 같은 세션에서 재시도하도록 안내했습니다.
- [LIMIT] 물리 마우스 커서 이동은 아직 사용자 직접 관찰 전이므로 최종 cursor criterion은 false로 유지합니다. MacBook 사용자 데이터에는 변경이 없습니다.

### D66 Setup 호환성 차단 지속 — 원인 미확정

- [FACT, 사용자 보고] LabConfig 안내 후에도 “이 PC에서는 Windows 11을 실행할 수 없음”이 지속됩니다. 실제 레지스트리 값과 Setup 로그는 아직 수집하지 못했습니다.
- [CORRECTION] 앞선 TPM/Secure Boot 원인 확정 표현은 철회합니다. 해당 문구만으로 차단 항목을 특정할 수 없습니다. `install.wim` 오류의 원인도 미확정입니다.
- [FACT] D66 live log 재판독: alive=540s, physical HID completion 확인, WHEA/CPER/bugcheck/guest exception=0. 추가 재부팅이나 guest 변경 없음.
- [NEXT] Shift+F10에서 LabConfig 조회와 X:\Windows\Panther의 setuperr/setupact 확인. 이 WIM 제작 기록에는 findstr.exe가 없으므로 find.exe를 사용합니다. 현재 호스트에서 WinPE 명령을 실행하는 원격 채널은 확보되지 않았습니다.

### D67 USB-C initial PHY recovery — 실기 음성 결과 / D68 PD 수집

- [CHANGE] D66 기반 hv_exc.c: host mode, RS=1/HCRST=0, Windows canonical PC 관찰, IMAN.IE=1이 2초 유지되는 첫 미연결 상태에서 usb_phy_bringup(1)을 딱 한 번 호출. 기존의 prior-CCS 의존을 이 첫 시도에서만 제거. 두 PORTSC를 기존 주기의 로그에 기록.
- [BUILD] m1n1-d67-usbc-initial-phy.bin 2129920 bytes SHA256 32cd11528bdd5c1e73e99c31448afff511f354c708879fca4a203293e6e118a0. build/diff-check/bash syntax PASS. 기존 printf format warning 존재.
- [FACT] 사용자 Magic Trackpad를 비디버그 USB-C 포트에 연결. bootstrap 103535 NOP PASS, D67 guest 103614.3fhVQg 실행. initial-phy ret=0, cmd=0x2005/gctl=0x30c11004, PORTSC before/after 0x2a0/0xe0002a0. CCS는 0 유지. PHY 재설정 단독으로 해결되지 않음.
- [EVIDENCE] D67 snapshot-end 203066 bytes SHA256 5aff5cefc24a0da05da7151566f5376d55f40d88ddce0350974b09b813c19d7f. D66 snapshot-before-d67 SHA256 91f575e0e53cbdbbaf86071fb1f9c3175e62a0a73b55c3208c40e1a1dcb472e9.
- [NEXT] D68: fresh proxy에서 ADT의 hpm1 주소 0x3f를 검증한 뒤 PD status/config를 읽기. PD 명령/설정 쓰기 없음, debug HPM0 접근 없음. D67 SIGTERM 종료 후 새 실험 bootstrap 1회.

### D68–D71 USB-C PD 활성화와 키보드 회귀 (2026-09-07 10:44)

- [FACT] D68 ADT hpm1 address=0x3f 검증. PD APP, system power state=7, status=0x10000000, power-status=0. 진단 도구의 API 이름/짧은 status 응답 파싱 오류를 같은 proxy 부팅에서 교정 후 수집 성공(추가 reboot 없음).
- [FACT] D69 SSPS(power state 0) 한 번 ACK. 상태=0으로 바뀌고 settle 후 status=0x100248ef, power-status=1로 연결 감지됨. 데이터는 logs/usbc-d69-after-settle.log에 보존.
- [CHANGE] D70 usb.c 두 HPM index 계산의 `name[3] - 30`을 `name[3] - '0'`으로 교정. D67 실패 PHY-first 변경은 원복하여 D66 기반. D70 binary 2129920 bytes SHA256 31faf3a63bf8374b4270a2685630b80052f7a4e2b20e489e76cf9a375597c3b3.
- [FACT] D70은 D69로 PD가 이미 깨어난 부팅에서 chainload하였으므로 index correction 단독 양성 실험이 아님. HPM C I2C read timeout도 발생함. Windows 단계 PORTSC=0x603/0x200603, IMAN=3으로 연결/활성/pending이 확인됐으나 소비가 계속 진행된 증거는 없음.
- [FACT, 사용자 보고] D70에서는 Trackpad뿐 아니라 USB-A 키보드도 동작하지 않음. 따라서 USB-C 사용 가능 판정 FAIL. 전기적 연결 성공과 실제 입력 성공을 구분.
- [STAGED ONLY] D71 i2c.c: rev>=6의 CTL.EN을 init에서 설정(호스트 Python I2C가 끝에서 EN을 내리는 것과의 상호작용 가설). hv_exc.c USB-C ring register read-only 3회 계측 추가. 2129920 bytes SHA256 a5794ad9e2a95a9612729a149f8f51a31b53e182c84e886a70ece73e343e1634. 빌드만 완료, 배포/실기 검증 안 함.
- [RECOVERY] D70 SIGTERM 종료, snapshot-keyboard-regression 190588 bytes SHA256 7ea615555863ab92c3e0d6e3f0babc5b8c1a56f9d97d2759a01f98e1a34300fd. USB-C Trackpad 분리 안내. fresh bootstrap 104359 한 번으로 exact D66 복구 시작. 설치 LabConfig 차단 사유 진단은 아직 미완료.

- [D66 RECOVERY CHECK 10:47] exact D66 recovery log `tahoe-dcp-d66-d59lite-xferclean-hidwatch-20260907-104447.byyaQP`: alive150s, FBMASK END/LATEUSB END live_slots=2 hid_watch=1, WHEA/CPER/bugcheck/guest exception=0. 현재 boot physical completion=0, 사용자 Tab 반응 확인 대기. USB-C 입력은 미해결. D71 source/build는 staged only이고 running binary는 exact D66입니다.

- [FACT, 사용자 확인] 복구 D66 USB-A 키보드 정상 작동 확인. 사용자 지시로 USB-C 해결을 설치 진행보다 우선. D71 자동 PD 초기화/이벤트 레지스터 진단 실기 준비.

- [D71 FACT] fresh boot에서 I2C rev6 CTL0x904, HPM0/1 powerup OK 자동 초기화 성공. USB-C CCS1. Windows IRQ857 enable 후 ERSTBA=0xadea51000/ERDP=0xadea52008/DCBAA=0xadea58000, IMAN pending 지속. 사용자 키보드/Trackpad 모두 무반응 확인. D72는 guest GICD ISENABLER를 존중하도록 SPI857 injection gate만 추가.

- [D72 FACT, 사용자 확인] USB-A 키보드 정상, Trackpad 무반응. SPI857 GICD mask gate로 키보드 회귀 해소. Windows USB-C sts=0x1d(HSE), cmd=0x2004 후 IE0으로 장치 중단. D73은 같은 runtime에서 HSE 최초 1회 ERST/event/DART 오류를 경계 검사 후 수집하는 관측 변경.

- [D73 FACT] Windows USB-C ERST high IPA=PA=0x840252000, ring0=0x840253000, ERDP=0x840253370. ERST segments4 x256 entries. First8 entries include successful PSCE/type33 command and ep1 transfer completions. DART0/1 TCR=0x1100 bypass, ERROR=0x060f0400 FLAG=0 (stale, not live fault). HSE direct cause pending last-event capture. D74 extends first segment snapshot to64 TRBs; no behavioral change. User keyboard stays working, Trackpad not.

- [D74 FACT 11:02] HSE snapshot: ERDP ring0+0x370, exactly55 events (0..54), followed by zero TRBs. Control EP1 success/short-packet plus2 recovered STALLs; no Host Controller Event. Last completed command ring+0x490. D75 bounded read-only last/next command and input-context capture staged to identify bus address causing HSE; no behavioral fix claimed. D74 SIGTERM/snapshot-hse preserved, one new bootstrap.

- [D75 FACT] Last completed command type12 Configure Endpoint, add flags0x389. No next queued command. HCC.CSZ=1, context64 bytes; ERST supports16 segments so Windows4 is legal. HSE occurs after configure completion, not malformed command-ring startup.
- [D76 FACT] USB-C slot1 EP7 Normal TRBs reference low IPAs 0xfe9d75a0 and0xb3620, length0x56c; USB DART TCRs remain0x1100 bypass. Guest low IPAs are backed by high physical RAM, so these DMA addresses are wrong under bypass. EP9 buffers are high identity; control descriptors previously used high common-buffer RAM. This explains why enumeration succeeds before HSE. Direct physical Trackpad input remains unverified.
- [D77 DESIGN] Before Windows writes xHC RS=1 while HCH=1, use the validated FL1100 runtime-reserved T8020 page tables for non-debug USB1 DART0/SID0 and DART1/SID1 (Asahi t8103.dtsi topology). Check two low-window leaves against EL2 stage2; check both DART locks before writes. Program TTBRs/enable/TCR0x80 and bounded TLB flush. No FL1100 table writes and no debugUSB0 access. Dedicated module maps USB1 first16KB via EL2 SPTE_MAP. One behavior variable: DMA translation; diagnostics preserved. Binary2129920 SHA256 1f9ff8a7bf90e7e261ff824213f937d15be436b2e66fa858f5208e849cd29dae. Build/diff/bash/AST checks PASS; hardware pending.

- [D77 NEGATIVE / CORRECTION] HSE persisted. Source audit found USB1 DART instance stride coded0x8000 instead of0x80000 (0x502f00000 vs0x502f80000). Earlier D73-D77 DART1 diagnostic actually read mirror of DART0, so claims about second DART state are withdrawn. D77 programmed DART0 SID0/1, leaving real DART1 bypassed. D78 corrects all3 stride occurrences (probe/lock/program) as only change. Physical Trackpad still unresolved.

- [D78 FACT] Correct DART0/SID0 and DART1/SID1 both TTBR80ae0180 TCR80 flush0. Windows USB-C cmd2005/sts18, CCS1, IMAN pending clears repeatedly; former HSE absent. User keyboard works, Trackpad still not. D79 adds read-only GET_DESCRIPTOR device VID/PID capture and one 60s ring/endpoint snapshot to separate driver binding from transport. No behavioral change.

- [D79 FACT / LIMIT] Healthy sts18 alone is NOT input transport success: late snapshot shows guest event ring all zero, DCBAA slot1 zero. Two PSCE bytes appeared at low-window backing0 (diagnostic erroneously translated lastcmd=0; no valid command). Suggests stale ERST zero DMA pointer under translation; driver diagnosis premature. No VID/PID captured. D80 adds bounded ERST clean before Run + device event ring invalidate-only before IRQ. ARM64 driver archive staged locally only, not installed.

- [D80 NEGATIVE] Bounded ERST clean + event invalidate did not restore posting; ring0 allzero, slot1 null, sts18. Shared-table translation path not validated. D81 returns to D76 DART bypass baseline and aliases only non-immediate Normal/DataStage low payload pointers to stage2-backed PA before selected slot1 endpoint doorbell. Keep opaque EventData intact, high pointers unchanged. Full USB1 MMIO64KB routing covers doorbells. D79 descriptor/late diagnostics retained; null pointer diagnostic dereference corrected.

- [D81 FIRST PHYSICAL CURSOR] User observed brief Trackpad cursor motion. Bypass baseline with low payload alias posted non-control slot1 EP7 Transfer Events CC13; ERDP advanced to0x480 (72events) before HSE. First-ring-only32TRB traversal misses subsequent Link target; ~15 input reports fit first segment. D82 follows Link TRBs through up to8 distinct segments, aliases low Link addresses too, stops on invalid type0/bounds/duplicate. Device context state1, actual low payloads mapped to backing RAM confirmed. This is partial input proof, NOT stable input success.

- [D82 PARTIAL] Link traversal reached segment1 and patched2 further buffers; ERDP0x4a0 (74 events) then HSE. User brief cursor motion then stop. Output-context EP7 DQ still points at original segment, but that memory now allzero (retired by Windows). D83 remembers last observed linked segment per endpoint and resets cache when context/DQ changes. Keeps bounded8segment/256TRB/cycle/invalid guards. Addresses dynamic software ring retirement, not new driver installation.

### D83 USB-C Magic Trackpad 실제 커서 동작 확인 (2026-09-07 11:33)

- [FACT, 사용자] “어 이제 잘 움직인다.” D81/D82의 잠깐 이동 후 멈춤을 넘어서 D83에서 정상 커서 이동을 직접 확인했습니다.
- [CHANGE] D76의 PD 활성화/GIC mask/bypass 기반에, USB1 slot1 Normal/Data TRB의 low IPA payload를 stage2-backed PA로 변환. opaque EventData와 immediate payload는 제외. D82 Link traversal에 더해 D83은 현재 linked segment를 기억하므로 Windows가 최초 segment를 retire/zero해도 진행합니다. context/DQ 변경 시 추적값을 재설정합니다.
- [FACT] D83 로그에서 링크 순환/새 구간 접근과 low buffer 변환, Windows USBSTS0x18(HSE0, HCH0), IRQ pending 처리가 유지됩니다. 별도 Trackpad driver 설치 없이 커서 동작했습니다.
- [ARTIFACT] build/m1n1-d83-usbc-live-segment.bin, 2129920 bytes, SHA256 a5fc89874e62a51158183e38cfbcc232fd5031f75f9a908119995d6bde2f3906. Harness nwoas_scripts/tahoe-dcp-d83-usbc-live-segment-hidwatch-guest-test.sh; module pcie_emul-d81-usbc-payload.py. Current session42471, log tahoe-dcp-d83-usbc-live-segment-hidwatch-20260907-113132.MRpxIf. DO NOT reboot a functioning session unnecessarily.
- [LIMIT] Validation covers the current directly attached USB-C Trackpad (slot1). USB-C hubs/multiple devices, unplug/replug and long-duration stability are not yet verified. Windows11 Setup compatibility block remains separate and unresolved. MacBook user data untouched.
- [REPRO] If a later reboot is needed: verified bootstrap-serial-test.sh once, then D83 harness once from ready proxy. Fixed115200. Do not run harness while serial is owned. Stop guest with verified PID/SIGTERM only. No kmutil/firmware rewrite required.

### S84 Windows Setup hardware-block automation prepared

- User requested fixing repeated "This PC cannot run Windows 11" and automation.
- D83 guest remains running; no target reboot/change during preparation.
- Workspace-root legacy autounattend.xml has DiskConfiguration/WillWipeDisk=true. It is not reused. Actual WINARM2 answer file is not accessible while the USB remains on the target; inspect when connected to host.
- New package nwoas_scripts/setup-repair: safe ARM64 windowsPE answer, verified LabConfig write/readback, before/after Panther captures, optional explicit split-SWM interactive retry guarded against existing Setup process. No disk target/format/image apply directives.
- stage-setup-repair.py validates exact WINARM2 external USB FAT32/12-20GB and ARM64 boot files, split-set headers/GUID/parts, backs up replaced files and copies only the package; no WIM patch.
- Host checks PASS (8 wrong-device cases, missing/wrong SWM parts, XML forbidden directives, hashes, CRLF/ASCII). WinPE execution and bypass success UNVERIFIED. Preferred next run is fresh exact D83 after staging to avoid cached legacy answers. Waiting only for physical WINARM2 transfer to MacBook.
- Details: nwoas_scripts/SETUP-REPAIR-S84.md. Current USB input fix unchanged.

### S84 USB staging and image verification completed

- Actual host media WINARM2 was disk14s1, external USB FAT32 ~15.4 GB; no original root autounattend.xml. The unsafe workspace legacy answer was not copied.
- Final eight package files copied and SHA256/size readback verified; CMD/VBS ASCII CRLF verified. Added bounded WATCH11 background Panther/registry capture (~10 minutes), guarded by WinPE and exact media marker.
- Backups NWOAS-S84-BACKUP-20260907-114343 and NWOAS-S84-BACKUP-20260907-114522. Staging report logs/setup-s84-staging.json.
- Full wimlib verify PASS: boot.wim and complete install.swm/install2.swm set; ARM64 ko-KR 22621.525. Logs setup-s84-boot-verify.log and setup-s84-install-verify.log. No image modification or disk formatting.
- D83 session42471 left running. Target script execution and resolution of Windows 11 compatibility block remain UNVERIFIED. USB must return to target before one fresh D83 boot.

### S84 media returned; fresh D83 boot 11:49

- User confirmed WINARM2 returned. Verified old run_guest PID36137 and serial owner, SIGTERM only, snapshot-before-s84 saved.
- Exactly one bootstrap-serial-20260907-114911-534310.log: checksum-validated NOP115200 PASS. No firmware/boot-policy changes.
- Exact D83 harness started once, session30544, log tahoe-dcp-d83-usbc-live-segment-hidwatch-20260907-114950.jDAmwi. S84 answer-file execution, physical input this boot, and compatibility-block outcome pending.

- S84 first attempt 114950.jDAmwi failed in early Project Mu before Windows: PC=ffffffffffffffff ESR=8a000000, then self-reset/direct CDC disconnect. Same signature as previous D52/D62. Not proof of S84 answer-file failure or WHEA recurrence.
- Re-enumerated direct CDC had no owner and passed checksum NOP115200. No additional host reboot. One retry from ready proxy: D83 115049.FyPfss, session62037.

### S85 ProductKey correction prepared after S84 failure

- User reports Setup cannot read ProductKey from unattended answer. S84 second run reached Windows kernel/USB-C EP7 progression, no observed HSE, then serial disconnected after alive60s; runner62037 exited1. No claim that LabConfig executed or compatibility block cleared.
- S84 omitted UserData/ProductKey. S85 adds only Setup UserData/ProductKey Key=00000-00000-00000-00000-00000 and WillShowUI=Always to both XML files; this interactive placeholder is used by upstream cschneegans/unattend-generator modifier/ProductKey.cs. Microsoft documents Always to show key UI. No edition selection, activation, disk configuration or image apply added.
- Host guard now permits only this exact placeholder/Always pair in Setup windowsPE; forbids other ProductKey directives. Eight hashes, XML guards and unchanged six commands PASS. Original package archived logs/setup-s84-package-before-productkey.
- S85 HOST-VALIDATED ONLY; WINARM2 still on target. Next: move installer USB to host, retrieve any NWOAS-SETUP-LOGS first, stage corrected package, eject, return, one fresh boot. Do not re-run old S84 answer.

### S85 staged on WINARM2 11:56; S84 WinPE logs recovered

- Confirmed same external disk14s1 WINARM2 UUID8AA1ED40-57BA-3284-9023-B310B595EC94. Recovered 12 diagnostic files to logs/setup-s85-recovered-20260907-115637 before writes.
- FACT: previous S84 auto commands executed in WinPE; all five LabConfig writes exit0 and FIX11 summary LABCONFIG_READBACK_PASS. Actual media C:, install.swm found. WATCH11 captured subsequent ProductKey error.
- FACT: setupact.log uses default WillShowUI=OnError, then Callback_Productkey_Validate_Unattend fails hr0x80070002. This supports S85 explicit interactive UI correction; no hardware-block bypass outcome yet. Guest log timestamps 2022 are target clock, not host date.
- S85 applied with backup NWOAS-S84-BACKUP-20260907-115650; all eight file hashes/lengths independently verified on USB. Report logs/setup-s85-staging.json. WIMs unchanged, previously fully verified. Await media return to target for S85 test; no target reboot during staging.

### S85 runtime 11:58

- User go-ahead after USB return instruction. Serial unowned, ready proxy NOP115200 PASS; no host reboot required. Exact D83 harness once, session24757, log tahoe-dcp-d83-usbc-live-segment-hidwatch-20260907-115818.dOcSrs. S85 interactive ProductKey and hardware-block outcome pending.

- S85 physical result USER CONFIRMED: ProductKey error no longer appears, but Windows11 cannot-run compatibility block persists. Keep current guest running. Need current Panther logs plus COLLECT11 read-only disk inventory; prior S84 five key readbacks prove setting execution only, not bypass success. Host has no live access to WinPE filesystem/UI, so user must run the staged collector and return WINARM2 for log retrieval.

### S86 confirmed core-count blocker / experimental minimum-one patch

- Fresh capture13267-2068: LabConfig all5 DWORD1; VerifyProcessorSupported number of cores INSUFFICIEMT [1] vs[2]. RAM8192MB passes. DiskPart only14GB WINARM2, no internal SSD. S85 ProductKey error cleared.
- Existing MADT disables AP1..7 for uniprocessor boot. Physical M1 still8cores; actual Windows multi-core support remains unresolved.
- Exact ARM64 winsetup.dll SHA5368c4f724a56f89c7cb907bc4580ef6884ede01e7d19a3f063e8344b1de7189 disassembled: active count comparison requires2. S86 only4 instructions (3cmp thresholds plus logged minimum) change2->1; recalculated PE checksum. Patched SHA969b606fa99191efa42a6e29ebd5d3a77ed220025e3f604d9061ea06c02ace21. No claim of loader acceptance: modified Authenticode signature no longer valid.
- Local boot.wim index2 sources/winsetup.dll updated; full WIM verify and embedded DLL readback PASS. Host originals saved setup-s86-corecheck/original. Guarded stage.py copies boot.wim and sources/winsetup.dll only, with USB backup and hashes. Installation SWMs unchanged. Runtime D83 and firmware unchanged. See setup-s86-corecheck/README.md and patch-manifest.json.

- S86 staging completed, both USB replacements hash-readback PASS. USB backup /Volumes/WINARM2/NWOAS-S86-BACKUP-20260907-120646. Script exit0; transfer delay was active USB I/O (~14MB/s sampled), not failed staging. Target test remains pending.

### S86 runtime 12:10

- User go-ahead after media return instruction. Previous verified run_guest PID39266 SIGTERM, serial released, snapshot-before-s86 saved. One bootstrap120935-437177: NOP115200 PASS.
- Exact D83 started once: log tahoe-dcp-d83-usbc-live-segment-hidwatch-20260907-121028.2XlEK1, session99172. Only media winsetup core minimum changed; firmware, CPU topology and USB runtime unchanged. Loader acceptance and hardware-check UI outcome pending.

### S86 SUCCESS: physical Setup disk selection; internal SSD next blocker

- USER CONFIRMED: Windows install-location selection reached, no SSD shown. This validates modified DLL loader acceptance and clearing the prior core-count block. Does NOT prove Windows installation or SMP.
- Existing captured DiskPart shows only14GB USB. Local Windows ANS2 material in windows_drivers is design/skeleton, no built ANS2 .sys found. MacMini2020.fdf lines164-167 explicitly TODO/comments NVMe; platform DSDT search has no ANS/NVMe device. Current EL2 module exposes FL1100/USB1, no ANS2 controller emulation. Merely rescan/format cannot provide the missing storage stack.
- UEFI AppleNANDStorageDxe source exists but UEFI support alone does not give Windows storage I/O. m1n1 has nvme_init and single-block nvme_read primitives; next low-level proof should be isolated read-only namespace/GPT access, then a Windows-facing controller/driver path. Keep current session99172 for physical Setup evidence.

### S87-S90 ANS2 read access and dual boot preference

- User requests macOS+Windows on internal256GB, roughly128GB Windows. Preserve macOS, ISC, Recovery; no deletion to force requested split. No partition writes authorized as a guessed layout. Plan nwoas_scripts/DUAL-BOOT-PLAN-2026-09-07.md.
- S86 PID40746 stopped SIGTERM, immutable disk-selection snapshot. One bootstrap121646-679374 NOPPASS. S87 host script root ADT accessor typo fixed before hardware init, same proxy/no extra reboot.
- S87 NVMe init fault FAR0x27bce4008 (legacy NVMe+0x24008) ESR0x96000018, self-reset. No disk read/write occurred. Actual ADT ANS NVMe0x27bcc0000, ASC0x277400000, SARTv2 0x27bc50000, IRQ584/583/586/585/590.
- S88 removes legacy clear32 as current AsahiLinux/m1n1 src/nvme.c does. Init succeeds; readLBA0 fails status2, shutdown delete SQ/CQ status2. Verified fresh ready proxy and chainload; no reboot loop.
- S89 fixes zero-based NLB1->0, matching current upstream. Namespace1 4K reads succeed; primary header/entries CRC and backup-header CRC match. Disk251000193024 bytes, macOS APFS245107195904, ISC524288000, Recovery5368664064. GPT full, no large unallocated gap. Shutdown delete SQ/CQ still status2; controller reset completed.
- S90 adopts upstream TCB opcode0 and direction/no-data DMA flags. Read proof repeats and shutdown has no command errors. logs/ans2-s90-probe.log and ans2-s87/run-20260907-* metadata/raw GPT blocks preserve evidence. Only GPT metadata read; no file content or storage writes.
- Current target idle proxy running S90, ANS shut down. Windows not currently running. This is host-side SSD access proof, NOT Windows SSD driver or install success. Runtime D83 artifact unchanged; build/m1n1.bin nowS90.
- Windows128GB leaves at most~117GB for macOS before new boot/EFI/recovery overhead (roughly110GBs in practice). GPT allocation is not APFS usage. Need macOS/recovery diskutil APFS usage and resize limits before partitioning.

### S91 APFS allocation verified read-only

- On idle S90 proxy, read282 APFS metadata blocks only; latest NX checkpoint XID2008561 and matching checkpoint map resolve spaceman. Referenced block checksums PASS, blockcount matches container.
- Container245107195904B, free34152562688B, allocated210954633216B. Allocation includes metadata/snapshots; not user-file size or shrink minimum. Roughly100GB reclaim needed before128GB Windows allocation plus boot overhead. No filesystem/partition/storage writes.
- Probe logs/ans2-s91-space.log, result nwoas_scripts/ans2-s87/space-20260907-122535/result.json. User informed that current128/128 split cannot fit without reclaiming space. No file deletion performed. Controller cleanly shut down, idle S90 proxy.

### Authorization: erase existing Mac mini data for fresh dual boot

- User explicitly: "그거 싹 밀어도 되는데." Applies to measured Mac mini existing APFS data; host MacBook/X31 protected. Prior macOS+Windows dual-boot and Tahoe constraints persist. No repeated wipe permission needed.
- Concrete preliminary budget: macOS110GB, Windows128GB, custom-OS stub4GB, EFI512MB/MSR16MB/WinRE1.5GB; existing ISC+AppleRecovery preserved; remaining~1.079GB alignment/reserve. Final stub/partition requirements unverified.
- No erase performed: present Macintosh HD hosts custom boot object, so deleting it removes experiment boot route. Need target Recovery/reinstall/bootstrap preparation and Windows storage support before executing wipe. m1n1 currently offers only proven storage reads, not APFS format.

### S92 read-only NVMe command engine prototype

- Added nvme-s92/readonly_namespace.py (Identify + bounded Read only) and prp.py (validated bounded guest buffer lists). No storage-write API. Initial serial-field length test failure corrected;14 tests PASS, including all255 non-read I/O opcodes never call backend, bounds/short-read and invalid/MMIO PRP addresses.
- Hardware test ran on idle S90 proxy, no reboot/guest: realANS2 GPT LBAs0/1/61279343 read through new command engine, expected GUID/headerCRC match, all mutating requests rejected before backend. Clean shutdown. Log nvme-s92-hardware.log.
- This is a transport-independent prototype, NOT Windows .sys or visible disk. PCI/ACPI, SQ/CQ, interrupts and cache-coherent guest DMA remain. No formatting, partition writes or deletion. User wipe authorization retained; dualboot110GBmacOS/128GBWindows preliminary plan unchanged.

### S93 first Windows NVMe enumeration, queue initialization failure

- Added isolated PCI segment1/ECAM0x700000000/BAR0x700100000, INTx900 gate, read-only SQ/CQ transport. Physical PCI0 and D83 USB paths retained. Both builds and8 transport tests PASS. Backups/artifact hashes in nvme-s93/.
- Hardware log nvme-s93-20260907-124920.RlFxMC: target GPT identity rechecked; Windows kernel enumerated synthetic PCI device and programmed NVMe controller. This advances beyond host-only S92.
- Controller rejected admin queue setup as invalid address/size (CSTS.CFS=1). No namespace read commands observed; disk visibility NOT proven. PCI enumeration logging exhausted initial budget, so exact queue parameters missing.
- Preserved immutable log, verified PID46020 SIGTERM. One bootstrap125039 for follow-up with detailed queue/register logging. No disk writes/erase/firmware installation.

- S93 diagnostic125122.GYU0YH proved Windows programs AQA=0x00ff00ff (256 entries), ASQ0xade65c000/ACQ0xade660000; both addresses validated guest RAM. First prototype capped64 entries. Raised advertised CAP.MQES and accepted bounded depth together to256; replay test added (9 PASS). Same firmware/CIRQ; host transport only changed. Verified PID46323 SIGTERM, snapshot, one bootstrap125232 before retry.

### S93 Windows internal SSD reads PASS; publication/handoff requested

- Depth256 run125315.KyzBAI: Windows enables admin queue, identifies controller/namespace, creates CQ1 and SQ1/SQ2, then issues actual internal ANS2 reads. Snapshot-handoff records388 successful I/O reads/1,650,688 bytes, zero I/O errors, no CFS. Includes GPT LBA0/1/2..5 and APFS partition starts.10 unsupported admin responses remain; not full NVMe conformance.
- Actual Setup disk-list UI not yet user-verified; no storage writes, partition changes or Windows installation. S93 current host-mediated device is read-only.
- User explicitly stopped further experiments and requested gh commit/push plus fable5.1 handoff. Existing guest/host runner PID46598/session86788 retained; do not terminate needed NVMe callbacks just to publish. No further reboot.
- Handoff NWOAS-HANDOFF-FABLE5.1-2026-09-07.md; public export checkout /Volumes/X31/NWOAS-publish-20260907. Four companion patchsets checked against exact base indexes; original source repos/worktree states retained.

### S94 (2026-09-07 ~16:00) Windows Setup disk-list UI shows internal SSD — USER VERIFIED

- Prior S93 guest (PID46598) ended in a fully black display after ~3h idle; log showed no SError/CFS, only periodic SMART Get Log Page polling. Cause of black screen NOT determined (display/DCP suspected, guest itself looked alive). Log snapshot `.snapshot-s94-pre-reboot`.
- SIGTERM 46598 → bootstrap-serial-test.sh once (NOP PASS 15:50:37) → nvme-s93-guest-test.sh once. Log `nwoas_scripts/logs/nvme-s93-20260907-155044.uaslhM`. Same code as S93 except controller.py now logs nsid/dw10/dw11 on rejected admin commands.
- Rejected admin commands identified: Identify CNS=6 (NVM cmd-set ctrl), Identify CNS=3 nsid=1 (NS ID descriptors, asked 3×), Get Features FID 0xD0/0x0C/0x7F, Get Log Page LID 0xC1. All optional; Windows proceeds to reads regardless.
- **User confirmed on Mac mini screen: Setup "설치 위치 선택" lists 드라이브0 파티션1 500MB / 파티션2 228.3GB / 파티션3 5.0GB, 종류 "주", 사용가능 0.0MB.** Matches real GPT (ISC / APFS container / Apple Recovery). physical_install_disk_selection_verified is now TRUE.
- Still NOT: any storage write, partition change, Windows installation. Writes remain rejected. Do not proceed with Setup "다음/삭제/포맷" on this build.

### S95 (2026-09-07 16:06) Recovery-mode APFS shrink + WINTEST partition — GPT re-verified read-only

- User ran in Mac mini Recovery Terminal: `diskutil apfs resizeContainer disk0s2 220g ExFAT WINTEST 0` (~10 min). diskutil reported disk0s2 220.0GB, WINTEST 25.1GB as disk0s5. Restarted into m1n1 proxy.
- Host: `nwoas_scripts/s95-gpt-verify.sh` (chainload S93 hv + ans2-s87/probe.py, NWOAS_ANS_EXPERIMENT=S95). Log `logs/ans2-s95-gpt-20260907-160639.log`, raw blocks `ans2-s87/run-20260907-160641`. PRIMARY_GPT_AND_BACKUP_HEADER_CRC_PASS.
- New on-disk GPT (4096-byte LBAs), slots:
  1 iBootSystemContainer 6–128005 (524288000 B) unchanged
  2 APFS container 128006–53838942 (219999997952 B) — shrunk from 245107195904
  3 **WINTEST** Microsoft Basic Data ebd0a0a2-… guid 4684a45c-e252-4132-8a97-0576545e9a3b, **LBA 53839104–59968511**, 25106055168 B
  4 RecoveryOSContainer 59968630–61279338 (5368664064 B) unchanged (moved slot 3→4)
- Only WINTEST LBA 53839104..59968511 is a legitimate future write target. Nothing else may be written. No writes performed yet; macOS + custom boot object intact (mini booted normally).

### S96 (2026-09-07 16:23) NVMe write path, WINTEST-only — host-side hardware round-trip PASS

- m1n1: new `nvme_write(nsid,lba,buf)` + proxy `P_NVME_WRITE`(0xf04). C-level guard refuses nsid!=1 or lba outside 53839104..59968511 (WINTEST). dc_cvac_range + dma_wmb before submit; TCB dma_flags BIT(1) via existing opcode&1 logic. Image `build/m1n1-s96-nvme-write.bin` sha256 f63512503f6bef4a06e2b3e9155fcb5ada134043204dd0e26015b88044a5caf5 (2129920 B).
- Python relay: `nvme-s93/writable_namespace.py` WindowWritableNamespace (Write 0x01/Flush 0x00 inside window only; straddle → 0x182; other mutators → 0x182/INVALID_OPCODE; NSATTR write-protect cleared for CNS0). controller passes guest memory to ns.io; guest_module backend_write re-checks range and re-verifies GPT slot3 before arming. 16 unit tests PASS.
- Opus critical review: SHIP-WITH-FIXES; applied F1 (C guard), F2 (__debug__), F5 (dc_cvac_range), F10 (cdw13 DSM hint), F12 (WRITE_ERROR 0x280), F13 (stale read-only comments), F14 (guards T±1, backup GPT). Not applied yet: F8 (DNR bit on rejections), F4 (TCB len semantics, watch if writes misbehave).
- Hardware: `s96-write-probe.sh` → `ans2-s87/write-20260907-162330/result.json`: target LBA 56903807 (orig all-zero), pattern_roundtrip=true, restored=true, guards_unchanged=true (53838942, 53839103, 53839104, 56903806, 56903808, 59968511, 59968630, 61279343). Log `logs/ans2-s96-write-20260907-162327.log`.
- This is ONE 4 KiB block written twice via host proxy. NOT Windows-side writes, NOT sustained throughput, NOT install. Per review F7, Windows Setup cannot complete an install on this layout (GPT/ESP/MSR writes are outside the window and refused by design); the Windows-side S96 goal is NTFS format of WINTEST from Setup.

### S96 run2 (2026-09-07 17:12–17:4x) Windows Setup formatted WINTEST to NTFS through the relay — USER VERIFIED

- Run1 (162531): user pressed through Setup; one Windows write to LBA 53839104 (identical ExFAT boot sector rewrite, readback sha f7bff6e9 unchanged), then Setup cancelled with 0x80070003 D:\Sources\install.swm (hypothesis: drive-letter shift after mounting WINTEST; not an NVMe error). Mini rebooted into m1n1 proxy (black screen = proxy idle, expected).
- Run2 (171249): user chose partition 3 → 포맷. Log: sequential multi-block writes from 53839104, MFT zone ~54617005+, last logged: [cpu0] [S96] ANS WRITE lba=53839496 count=14848; Windows write commands=991; refusals/out-of-window=0; no CFS/fault. Throughput ≈40 KiB/s (host round-trip per 4 KiB block; up to 16 blocks per command).
- **User confirmed Setup UI: partition 3 now 전체 23.4GB / 사용가능 23.3GB (NTFS).** Setup then reports Windows 11 requires ≥52 GB system drive — expected for the 25 GB test partition; install was never the goal of S96.
- Facts: Windows-side NVMe write path works end-to-end inside the WINTEST window; nothing outside was written (C guard + relay guard; zero REFUSED lines). Not verified: sustained reliability, power-loss, throughput adequate for install.

### S97 (2026-09-07 17:33–17:50) Multi-block NVMe transport — format time 10 min → ~10 s (user-observed)

- m1n1: `nvme_rw()` generalizes read/write to 1..16 blocks (PRP2 / PRP list in a static 4K page), proxy `P_NVME_READ_N`(0xf05)/`P_NVME_WRITE_N`(0xf06); WINTEST guard now covers `lba+count-1`. Image `build/m1n1-s97-nvme-multi.bin` sha256 8ac6097b3b03373b29e3d3178eb26c7361605cc98d26bd03ccb85035457ad3e7.
- Host probe `ans2-s87/multi_probe_s97.py`: 16-block read == 16 singles, 2-block ok, 16-block write round-trip in WINTEST + restore + guards PASS. Proxy round trip: 1.0 ms per 4 KiB single (4.2 MB/s), 6.0 ms per 64 KiB (10.6 MB/s) → proxy transfer was never the bottleneck.
- Relay: namespace read/write now one backend call per command; guest_module timing STAT every 64 I/O doorbells (host_ms = inside trap handlers, guest_ms = between handlers).
- Windows run (log nvme-s97-20260907-173530.1QOhiu): Setup formatted WINTEST again; format phase ≈14 ms host + 2.5 ms guest per command, ~5 traps/command, 970 write commands / 14,848 blocks, 0 refusals. **User: format took ~10 s ("평범한 포맷 수준")** vs ~10 min in S96 → ≈5.8 MB/s effective.
- Remaining host cost is guest-RAM copy per PRP page (3 proxy calls each). Possible next step (deferred): pass validated guest PRPs straight to ANS2 (zero-copy) → est. 20 MB/s.
- Side observation: USB-C (D83) Magic Trackpad stopped responding mid-session; USB-A keyboard kept working. Long-run USB-C stability remains unverified (known limitation).
