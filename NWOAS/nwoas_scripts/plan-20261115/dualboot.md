# 갈래 5 — 맥북 없는 부팅과 macOS/Windows 듀얼부팅 (2026-11-15)

> 이 문서는 읽기 전용 조사와 그 조사의 재검증을 합친 계획서입니다. 아무것도 빌드·실행하지 않았고, 시리얼 장치(/dev/cu.*)는 열지 않았습니다.
> 아래의 모든 절차는 **실제 맥미니에서 한 번도 해 보지 않았습니다.** "빌드됨"은 "부팅됨"이 아닙니다.
> 표기: **[사실]** = 파일:줄 또는 URL로 확인한 것, **[가설]** = 확인하지 않은 추측.

---

## 1. 결론

1. **시동 디스크 고르기 화면(전원 버튼을 길게 눌렀을 때 나오는 화면)에 "Windows" 항목을 따로 만드는 것은 가능해 보입니다.** Asahi Linux가 쓰는 방식, 즉 고르기 화면용 껍데기 macOS 볼륨(이하 "스텁")을 만들고 그 안에 m1n1을 등록하는 방식을 그대로 따르면 됩니다. [사실: 방식 자체는 Asahi가 macOS 12~26에서 바꾸지 않고 써 왔음 — asahilinux.org/2026/06/progress-report-7-1/] [가설: 우리 디스크 배치에서도 그대로 될 것]
2. **그러나 스텁만 만들어서는 맥북 없이 Windows가 켜지지 않습니다.** 지금의 S231 하이퍼바이저(이하 HV)는 맥북 쪽 파이썬이 USB로 명령을 보내야만 게스트를 띄웁니다. [사실: hv_init·hv_start는 프록시 호출로만 실행됨 — STANDALONE-CENSUS-20261115.md가 인용한 proxy.c:475-482] 스텁의 다음 단계로 지금 HV를 그대로 넣으면, 고르기 화면에 항목이 생기고 그 항목을 고르면 HV가 맥북을 기다리는 상태가 될 뿐입니다.
3. **따라서 순서는 두 덩어리입니다.**
   - (가) 고르기 화면 항목 만들기 + macOS 정상 부팅 되살리기 — 이 문서의 4장 절차. 지금 바로 시작할 수 있는 일.
   - (나) 맥북 없이 게스트를 띄우는 C 코드(맥북 파이썬이 하던 일을 m1n1 안으로 옮기는 일) — STANDALONE-CENSUS의 마일스톤 4~8. (가)가 끝나도 (나)가 끝나기 전에는 맥북 없는 Windows는 없습니다.
4. **(가)를 하는 동안에도 맥북 연결 개발 방식은 유지됩니다.** 스텁이 불러오는 다음 단계가 지금의 HV(USB로 맥북을 기다리는 m1n1)이기 때문입니다. [사실: S231 HV는 프록시 모드 m1n1] [가설: 스텁 경유로 불러와도 USB 연결이 지금처럼 잡힐 것]
5. **먼저 정해야 할 것 하나:** 스텁을 어느 macOS 판으로 만들지입니다(3.4절). 지금 macOS와 같은 26.x로 만들면 바뀌는 것이 하나뿐이지만 Asahi 도구가 그 판을 지원하는지 모르고, Asahi 기본값인 13.5로 만들면 m1n1의 화면 초기화 경로 자체가 바뀝니다. **추천은 26.x(같은 판)** 입니다. 이유: 지금까지의 화면·USB·NVMe 검증이 전부 26.x 펌웨어에서 나왔기 때문입니다.

---

## 2. 현재 상태

### 2.1 지금 맥미니가 부팅되는 방식 [사실]

| 항목 | 값 | 근거 |
|---|---|---|
| 1TR(전원 버튼 길게 → 옵션)에서 등록한 부팅 파일 | D4 `m1n1-bare.bin` | NWOAS-STATUS-2026-09-06.md:263, :478 |
| D4 크기(바이트) | 2,097,152 | 같은 곳 :478 |
| D4 MD5 | 7bec0225edc101857be3b7f195dd7357 | 같은 곳 :478, 파일 직접 해시 |
| 다음 파일을 이어 불러오는 기능(체인로드) | **없음** — "Chainloading files not supported in this build!" 문자열이 들어 있음 | `recovery-tahoe-proxy-20260906/m1n1-bare.bin`, `m1n1-layout-s231/src/chainload.c:140-146` |
| `layout-s231/out/m1n1-s231-layout80.bin`의 체인로드 | **없음** (같은 문자열) | 같은 방식으로 확인 |
| S231 HV 크기(바이트) | 2,162,688 | 파일 크기 |
| m1n1 이미지 최대 크기(바이트) | 67,108,864 (64 MiB) | `m1n1-layout-s231/m1n1.ld:9` |
| 등록에 쓴 명령 | `kmutil configure-boot -c m1n1-bare.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v "/Volumes/Macintosh HD"` | `recovery-*/install-in-recovery.sh` |
| 등록 전제 | 보안 수준을 "낮은 보안(Reduced Security)"으로 낮춰야 함 | NWOAS-STATUS-2026-09-06.md:126-136 |

즉 지금은 **Macintosh HD 자리에 D4를 넣어 둔 상태**라서, 맥미니가 켜지면 macOS가 아니라 m1n1이 뜨고 맥북을 기다립니다. 정상 macOS는 지금 이 맥미니에서 바로 켜지지 않습니다.

- 참고: NWOAS-STATUS-2026-11-15.md:20에 "Reduced, coih absent"(낮은 보안, 사용자 커널 등록 흔적 없음)라는 기록이 있습니다. 예전에 정책을 처음부터 다시 만든 적이 있다는 기록과 맞지만, 왜 그렇게 됐는지는 적혀 있지 않습니다. [사실: 기록 있음 / 원인: 모름]

### 2.2 디스크 칸(파티션) 상태 [사실]

| 슬롯 | 이름·용도 | 시작 LBA(4096바이트 단위) | 비고 | 근거 |
|---|---|---|---|---|
| 1 | iBootSystemContainer | — | 보호 목록 | `layout-s231/guest_module.py:173-178` |
| 2 | macOS(Macintosh HD) | — | 보호 목록 | 같은 곳 |
| 3 | NWSTUB (Basic Data, 속성 0xD000000000000000) | 38,968,576 | 보호 목록, 약 4 GB | `layout-s231/gpt-plan/plan.json`, guest_module.py:176 |
| 4 | Windows ESP(부팅 파일 칸, FAT32) | 39,944,960 ~ 40,076,031 | PARTUUID a3e73003-c14a-487b-9f2e-8854fe119192, bcdboot 완료 | plan.json, `jobs/J3-format.cmd:3`, `jobs/J5-bcdboot-stage.cmd:4` |
| 5~6 | MSR, Windows | — | Windows 쓰기 허용 구역 안 | plan.json |
| 7 | RecoveryOS 컨테이너 | — | 보호 목록 | guest_module.py:173-178 |

- **비어 있는 4 GB 공간은 없습니다.** 슬롯 3의 NWSTUB이 그 자리를 차지하고 있습니다.
- Windows 쓰기 허용 구역: LBA 39,944,960 ~ 59,968,629. [사실: guest_module.py:142; HV 쪽 상수 `m1n1-layout-s231/src/nvme.c:543-544`, `hv_vm.c:1191-1192`]

### 2.3 파티션 표 보호 검사(GptGuard)가 막는 것 [사실: `nvme-s124/gpt_guard.py:26-77`, 핵심 :44-56]

| 검사 | 어기면 |
|---|---|
| 디스크 GUID·헤더 모양이 같을 것 | 거부 |
| 보호 목록 4칸(슬롯 1·2·3·7)의 128바이트가 한 글자도 다르지 않을 것(슬롯 번호는 안 봄) | 시작 단계에서 "protected partition modified/missing" — **런처가 막힘** |
| 보호 목록이 아닌 칸은 Windows 구역 안에 있을 것 | "outside Windows zone"로 거부 |

이 때문에 생기는 결과:
- 슬롯 3을 APFS로 바꾸면 보호 목록 3번째 값이 달라지므로, **바꾼 뒤에는 보호 목록을 다시 떠서 고쳐 넣기 전까지 런처를 쓸 수 없습니다.**
- 4 GB 근처에 FAT 칸을 새로 만들면 구역 밖이라 거부됩니다. 그래서 다음 단계 파일은 **새 칸을 만들지 않고 기존 Windows ESP(슬롯 4)에 같이 넣는** 쪽으로 계획했습니다.
- 칸의 위치(LBA)는 하나도 안 움직이므로 HV의 쓰기 구역 상수는 바꿀 필요가 없습니다.

### 2.4 Asahi 도구에서 확인한 것 [사실: github.com/AsahiLinux/asahi-installer]

| 항목 | 내용 | 파일 |
|---|---|---|
| 스텁 크기 | 2.5 GB | src/main.py `STUB_SIZE` |
| 지원 최저 macOS | 13.5 (상한 없음) | src/main.py `MIN_MACOS_VERSION` |
| M1(j274)이 기본으로 받는 스텁 판 | 13.5 (14.8.3은 M3 계열만 해당) | src/main.py 후보 목록 |
| "기존 빈 컨테이너에 설치" 기능 | `if parts_empty_apfs and is_gpt and False:`로 **꺼 둠** | src/main.py |
| 빈 공간 설치 | `dutil.addPartition(free, "apfs", name, STUB_SIZE)` — 새 칸을 추가함 → 우리 보호 검사에 걸림 | src/main.py |
| 스텁 볼륨 구성 | Data(D), System(S), Preboot(B), Recovery(R) | src/stub.py `prepare_volume` |
| 스텁에 넣는 것 | IPSW/OTA의 BuildManifest, 커널캐시, OS 짝 펌웨어 등 + 복구용 BaseSystem | src/stub.py |
| 마무리 | 버전 이름 끝에 "(stub)", 아이콘, `fsctl_set_bootable` | src/stub.py `install_files` |
| 부팅 파일 변수 | `chainload=<ESP UUID>;<다음 파일>` | src/osinstall.py |
| 복구 화면에서 하는 2단계 | `bputil -d`로 "Paired"·"one true recoveryOS" 확인 → `bputil -nc` → `kmutil configure-boot ... boot.bin` → 페어링이 안 됐으면(`! grep -q ': Paired'`) `bless --setBoot` 후 재시작 안내 | src/step2/step2.sh |
| macOS 27 | 부팅 가능 플래그가 없으면 고르기 화면에서 스텁이 숨음, "Fix macOS 27 boot picker compatibility" 옵션 추가 | progress-report-7-1, src/util.py |

- **결론: Asahi 설치 도구는 그대로 쓸 수 없습니다.** 컨테이너 설치는 꺼져 있고, 기본 경로는 새 칸을 추가하는데 그것은 우리 보호 검사에 걸립니다. 스텁을 만드는 부분(`stub.py`)만 가져다 고쳐 써야 합니다.

### 2.5 m1n1이 다음 파일을 이어 불러오는 방식 [사실]

- `chainload=<칸 UUID>;<경로>` 변수를 읽음: `src/payload.c:200-201`
- 불러오기 호출: `src/payload.c:312-313`
- 순서: NVMe 켜기 → Rust로 FAT에서 파일 읽기 → NVMe 끄기 → 넘겨주기: `src/chainload.c:118-133`
- 체인로드는 `CHAINLOADING=1`로 빌드해야만 들어감: `src/chainload.c:115-146`, `Makefile:70-71`

### 2.6 조사 보고서에서 고친 점

| 원래 주장 | 고친 내용 | 근거 |
|---|---|---|
| RELEASE+CHAINLOADING로 빌드하면 USB를 5초 기다려 맥북 개발 방식이 남는다 | **틀림.** 5초 대기는 SIP가 완전히 꺼져 있고(sip0=127) iBoot가 화면을 넘겨주지 않았을 때만 일어남. 개발 방식이 남는 진짜 이유는 다음 단계가 프록시 모드 HV이기 때문. 1단계에 RELEASE를 넣을 이유 없음 | `src/main.c:85` `if (!cur_boot_args.video.display && lp_sip0 == 127)` |
| step2.sh는 맞는 복구 환경이 아닐 때만 bless | 부정확. 실제 조건은 "Paired가 아닐 때" | step2.sh |
| 4 GB 빈 공간에 넣는다 | 빈 공간 없음, NWSTUB 자리 | plan.json |
| "가능하다" | 고르기 화면 항목에 한해서만 맞음. 맥북 없는 Windows는 별도 작업 필요 | proxy.c:475-482 |
| D4 해시 출처 STATUS-09-06:478 | 재검증에서 :117이라고 했으나, 다시 찾아보니 **:478(그리고 :263)이 맞음** | `grep 7bec0225 NWOAS-STATUS-2026-09-06.md` |

---

## 3. 해야 할 일

### 3.1 큰 순서

| 순서 | 할 일 | 끝나면 되는 것 |
|---|---|---|
| A | 고르기 화면에 스텁 항목 만들기 + macOS 되살리기 (3.2절 0~6단계) | 전원 버튼 길게 → macOS 또는 "Windows"(스텁) 선택. 스텁을 고르면 지금처럼 맥북 연결 개발 |
| B | 맥북 없이 게스트를 띄우는 코드 (STANDALONE-CENSUS 마일스톤 4~8) | 스텁을 고르면 맥북 없이 HV가 UEFI와 Windows를 띄움 |
| C | 다음 단계 파일에 UEFI(약 33.6 MB)까지 넣어 ESP에서 불러오기 | 맥북 없는 듀얼부팅 완성 |

A와 B는 서로 기다리지 않고 따로 진행할 수 있습니다. C는 A와 B가 모두 끝나야 합니다.

### 3.2 A 단계 절차 (전부 [가설] — 실제로 해 보기 전까지)

| # | 누가·어디서 | 할 일 | 위험 | 되돌리기 | 검증 방법 |
|---|---|---|---|---|---|
| 0 | 맥북(빌드만) | ① 1단계 파일: m1n1을 `CHAINLOADING=1`로 빌드하고 변수 `chainload=a3e73003-c14a-487b-9f2e-8854fe119192;m1n1/boot.bin`을 붙임. RELEASE는 넣지 않음. ② 2단계 파일: 지금 S231 HV 그대로. ③ Asahi 설치 도구를 복제해 컨테이너 설치를 다시 켜고, 칸·OS 없이 스텁만 만들도록 고치고, 부팅 파일을 ①로, 판은 3.4절 결정대로 | 빌드만 하므로 낮음 | 해당 없음 | `strings`로 "not supported" 문자열이 **없고** 체인로드 경로 문자열이 있는지, sha256 고정 기록 |
| 1 | 콘치님, 맥미니 1TR | Macintosh HD 되살리기: `diskutil apfs list`로 볼륨 그룹 ID를 **새로 읽고**(예전 값 DC3ADD29…가 남아 있다고 가정하지 않음) `bputil -g -v <ID>` → 인증 → 재시작 | 5단계 전까지 맥북 연결 개발 방식이 잠시 안 됨 | 기존 `install-in-recovery.sh`로 D4 재등록(09-07, 11-15에 해 본 적 있음) | macOS 바탕화면이 뜸, `bputil -d`에 사용자 커널 등록 흔적 없음 |
| 2 | 콘치님, 맥미니 macOS(sudo) | ① 파티션 표 블록 0~5와 61,279,339~61,279,343을 읽기만 하는 `dd`로 백업 ② disk0s3의 시작이 38,968,576 × 4096바이트, 크기 약 4 GB인지 확인 ③ `diskutil apfs createContainer disk0s3` | 다른 칸을 잘못 고름, 디스크 도구가 다른 칸이나 디스크 GUID를 다시 써 버림 | 백업 블록 되써넣기 | `diskutil apfs list`에 빈 새 컨테이너, 슬롯 1·2·4~7이 plan.json과 한 바이트도 다르지 않음 |
| 3 | 맥미니 macOS | 고친 설치 도구로 그 컨테이너에 D/S/B/R 볼륨 생성, System 볼륨 이름 예: "Windows"(고르기 화면에 이 이름이 나온다는 것도 [가설]), 아이콘, 부팅 가능 플래그 | 처음 쓰는 고친 도구, 26.x 판 처리 | 볼륨 삭제(컨테이너는 남음) | 4개 볼륨 존재, System 볼륨 이름에 "(stub)" |
| 4 | 맥미니 macOS 또는 Windows | 슬롯 4(ESP)를 붙여 `m1n1` 폴더를 만들고 2단계 파일을 `m1n1/boot.bin`으로 복사 | ESP를 Windows와 같이 써서(Asahi는 OS마다 ESP 하나를 권장) Windows 업데이트가 건드릴 수 있음 | 파일 삭제 | sha256 일치 |
| 5 | 콘치님, 스텁 항목으로 1TR | 고르기 화면 → 스텁 → 스텁의 복구 화면 → Finish Installation(step2.sh: 스텁에만 `bputil -nc`와 `kmutil`) → 원하면 시동 디스크를 Macintosh HD로 되돌림 | 반복 재부팅(Asahi 안내: 이때 고르기 화면에서 macOS를 고르라고 함) | `bputil -r <스텁 볼륨 그룹 ID>` | 고르기 화면에 Macintosh HD와 스텁 둘 다 보임, macOS는 계속 부팅됨 |
| 6 | 맥북 | 스텁으로 부팅하며 시리얼 기록. 파티션 표를 읽기만 해서 다시 뜨고(S87 방식) `guest_module.py:176`의 보호 목록 3번째 값을 바꾸고, 고정 해시·목록 갱신 | 보호 검사 파일 수정 | 파일 되돌리기 | 시리얼에 1단계 시작 문구 → **"File size:"·"File read successfully"(chainload.rs:66,74)** → S231 판 표시 → USB 대기, GptGuard 시작 통과 |

- 6단계의 파일 읽기 문구를 첫 검증 항목으로 둔 이유: ESP는 4096바이트 섹터 디스크 위에 Windows의 diskpart로 만든 FAT32인데, m1n1의 Rust FAT 읽기가 이 볼륨(칸 시작 위치 계산, 클러스터 크기)을 읽는지 확인된 적이 없습니다. Asahi는 ESP를 `newfs_msdos`로 만들어서 근거가 되지 않습니다. [미확인]
- 1단계를 하면 Macintosh HD 자리의 D4가 빠집니다. 5단계를 마치기 전까지는 맥북 연결 개발을 하려면 D4를 다시 넣어야 합니다.

### 3.3 2단계 파일을 굳이 나누는 이유 [사실 + 가설]

- 이미지 크기는 이유가 아닙니다. 최대 64 MiB이고, D4가 정확히 2 MiB인 것은 16 KiB 정렬(128 × 16 KiB) 때문입니다. [사실: m1n1.ld:9, :122-170] HV(2.16 MB)를 kmutil이 바로 받아들일 것이라는 점은 [가설]입니다.
- 나누는 진짜 이유:
  - kmutil로 다시 등록하려면 매번 1TR에 직접 들어가 소유자 인증을 해야 합니다. ESP에 둔 2단계 파일은 macOS나 Windows에서 그냥 바꿔 끼울 수 있습니다. [사실: 등록 절차]
  - 나중에 C 단계에서 UEFI(약 33.6 MB)를 같이 실어야 합니다. [가설: 그만한 크기를 불러올 메모리 여유가 있는지 미확인]

### 3.4 먼저 정할 것: 스텁의 macOS 판

| 선택지 | 바뀌는 것 | 좋은 점 | 나쁜 점 |
|---|---|---|---|
| A. 26.x (Macintosh HD와 같은 판, STATUS-09-06:101의 26.5.2) | 없음(같은 펌웨어) | 지금까지의 검증 결과를 그대로 쓸 수 있음 | Asahi 도구가 26.x IPSW를 허용하는지(`supported_fw` 필터) 미확인, 26.x 스텁이 4 GB에 들어가는지 미확인 |
| B. 13.5 (Asahi 기본) | 화면 펌웨어 등 OS 짝 펌웨어 전체, **m1n1의 화면 초기화 경로** | Asahi가 지원하는 판 | 아래 설명대로 코드 경로가 달라져 화면·USB 관련 호스트 코드를 전부 다시 검증해야 함 |

- B의 코드 경로 변화 [사실]: `src/main.c:169-172`는 iBoot 판이 `mBoot-18000.121.3`(지금 26.x)일 때만 화면 초기화를 맥북 쪽에 미룹니다. 13.5 스텁이면 판 문자열이 달라져 m1n1이 직접 `display_init()`을 실행합니다.
- 그 결과 맥북 쪽 `display-s190/tahoe_dcp_guest_hook.py` 설정과 충돌할 수 있습니다. [가설]
- 근거 [사실]: 화면 펌웨어 같은 OS 짝 펌웨어는 스텁 안에 들어가고 iBoot가 불러오며, 판끼리 호환되지 않습니다(asahilinux.org/docs/platform/quirks/, open-os-interop).
- **추천: A.** 바뀌는 것이 하나뿐이어서, 문제가 생기면 원인을 좁히기 쉽습니다. 0단계에서 Asahi 도구가 26.x를 받지 않으면 그때 다시 정합니다.

### 3.5 B 단계 (맥북 없는 게스트 띄우기)

- 자세한 내용은 `nwoas_scripts/layout-s231/STANDALONE-CENSUS-20261115.md`, `M8-NOTES-20261115.md`에 있습니다.
- 이 문서와의 연결점: B가 끝나면 3.2절 4단계의 `m1n1/boot.bin`을 그 결과물로 바꾸기만 하면 됩니다. 고르기 화면 쪽은 다시 손댈 필요가 없습니다. [가설]
- 검증 방법: 맥북을 뺀 상태에서 스텁으로 부팅 → 맥미니 화면에 Windows 로그인 화면이 뜨는지 콘치님이 직접 확인. 시리얼을 볼 수 있으면 HV 시작·UEFI 넘김 문구도 확인.

---

## 4. 위험과 모르는 점

### 4.1 위험

| 위험 | 대비 |
|---|---|
| 맥미니를 macOS 27로 올리면 고르기 화면에서 스텁이 사라질 수 있음 | 부팅 가능 플래그(`APFSIOC_VOL_BOOTABLE` 0xc0044a57)가 설정됐는지 먼저 확인, 그 전에는 올리지 않음 |
| `bless --setBoot`가 스텁을 기본 부팅 디스크로 만듦 | 5단계 끝에 시동 디스크를 Macintosh HD로 되돌림 |
| 2단계에서 디스크 도구가 다른 칸이나 디스크 GUID를 바꿈 | 미리 백업한 블록 되써넣기, 끝난 뒤 바이트 비교 |
| ESP를 Windows와 같이 씀 | Windows 업데이트 뒤 `m1n1/boot.bin` 해시 확인 |
| 보호 검사 수정(6단계) 실수 | 읽기 전용으로 뜬 값만 넣고, 바꾸기 전 파일 보관 |
| 1~5단계 사이에 맥북 연결 개발이 안 됨 | 급하면 D4 재등록 |

### 4.2 모르는 점 (모두 [가설], 실제 하드웨어 결과 없음)

| 모르는 점 | 언제 알게 되나 |
|---|---|
| kmutil이 2.16 MB 이미지를 바로 받는지 | 나눠 쓰기로 했으므로 당장은 영향 없음 |
| 26.x IPSW로 만든 스텁이 4 GB에 들어가는지 | 3단계 |
| Asahi `supported_fw`가 26.x를 허용하는지 | 0단계 |
| `diskutil apfs createContainer`가 디스크 GUID와 다른 칸을 그대로 두는지 | 2단계 바이트 비교 |
| NVMe 펌웨어(ANS)도 OS 짝 펌웨어인지 | 스텁 부팅 후 시리얼·NVMe 동작 |
| Rust FAT 읽기가 diskpart로 만든 4096바이트 섹터 FAT32를 읽는지 | 6단계 시리얼 "File read successfully" |
| 13.5 스텁을 고를 경우 화면 경로가 어떻게 될지 | 선택지 B를 고를 때만 |
| SIP 설정에 따라 1단계 m1n1의 USB 동작이 달라지는지(main.c:85) | 6단계 |
| 33.6 MB급 2단계 파일을 불러올 메모리 여유 | C 단계 |
| 스텁 경유로 Windows 바탕화면까지 가는지 | 이 문서의 어느 단계도 증명하지 못함. 시리얼 기록과 콘치님 화면으로만 확인 가능 |

---

## 5. 출처

### 프로젝트 안 파일 (읽기만 함)
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/main.c` :85, :169-172
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/chainload.c` :115-146
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/payload.c` :200-201, :312-313
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/nvme.c` :543-544, `hv_vm.c` :1191-1192
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/m1n1.ld` :9, :122-170; `Makefile` :70-71; `config.h` :47-52
- `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/` 의 chainload.rs :66, :74
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/layout-s231/guest_module.py` :142, :173-178
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/nvme-s124/gpt_guard.py` :26-77
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/layout-s231/gpt-plan/plan.json`
- `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/layout-s231/STANDALONE-CENSUS-20261115.md`, `M8-NOTES-20261115.md`
- `/Users/angigyeom/Desktop/DEV/NWOAS/recovery-tahoe-proxy-20260906/m1n1-bare.bin`, `recovery-*/install-in-recovery.sh`
- `/Users/angigyeom/Desktop/DEV/NWOAS/NWOAS-STATUS-2026-09-06.md` :101, :126-136, :263, :478
- `/Users/angigyeom/Desktop/DEV/NWOAS/NWOAS-STATUS-2026-11-15.md` :20
- `jobs/J3-format.cmd:3`, `jobs/J5-bcdboot-stage.cmd:4`
- `man bputil` (맥북에서 확인. 맥미니 1TR의 bputil과 같은지는 미확인)

### 웹
- https://github.com/AsahiLinux/asahi-installer (src/stub.py, src/main.py, src/osinstall.py, src/util.py, src/step2/step2.sh)
- https://asahilinux.org/2026/06/progress-report-7-1/
- https://asahilinux.org/docs/platform/open-os-interop/
- https://asahilinux.org/docs/project/glossary/
- https://asahilinux.org/docs/platform/quirks/
- https://appleinsider.com/articles/26/06/10/macos-27-has-broken-the-boot-drive-selector-for-some
