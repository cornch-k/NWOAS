# stub-135 — NWSTUB 자리를 macOS 13.5 부팅 항목으로 바꾸는 준비물 (2026-11-16)

> 표기: **[사실]** = 파일:줄, 명령 출력, URL로 확인한 것. **[가설]** = 확인하지 않은 추측.
> 이 폴더의 어떤 것도 맥미니에서 실행하지 않았습니다. 맥미니에 연결하지 않았고, `/dev/cu.*`도 열지 않았습니다.
> **준비물이 만들어졌다는 것은 부팅된다는 뜻이 아닙니다.** 부팅 여부는 콘치님 맥미니에서만 확인됩니다.

---

## 1. 결론

1. **macOS 26(Tahoe) 시스템 펌웨어 위에서 13.5 부팅 항목이 켜질 가능성은 높습니다. [가설, 근거 강함]**
   - Asahi 설치기는 M1에 일부러 13.5 펌웨어 묶음을 넣고, "시스템 펌웨어가 13.5보다 같거나 새것"만 요구합니다. 상한은 없습니다. [사실: asahi-installer v0.9.2 `src/main.py:112-118`(13.5 항목, 최소 SFR 22.7.74), `:587-623`(`min_sfr <= sys_sfr`)]
   - Apple 설계상 시스템 펌웨어는 앞으로만 올라가고, 각 OS가 자기 펌웨어를 따로 들고 다녀 옛 OS와 호환을 지킵니다. [사실: AsahiLinux/docs `docs/platform/introduction.md:104`, `docs/project/glossary.md:112` (커밋 ed6f9d6)]
   - 실제 보고: M1(j293, 같은 t8103 칩)에서 시스템 펌웨어 `mBoot-18000.161.10`(macOS 26.6.2) + 13.5 부팅 항목(`iBoot-8422.141.2`)으로 m1n1과 Linux가 부팅됨. [사실: github.com/AsahiLinux/m1n1/issues/733]
   - 실제 보고: M1·M1 Pro·M1 Air의 Tahoe 26.2 사용자들이 설치 오류를 고친 뒤 설치를 끝냄. [사실: github.com/AsahiLinux/asahi-installer/issues/404 댓글들]
   - "시스템 펌웨어가 26이라서 13.5 항목이 안 켜진다"는 보고는 GitHub에서 찾지 못했습니다(검색어 10여 개, 아래 7장). 다만 Tahoe에서 설치 **도중** 막히는 문제 3건이 열려 있습니다(4장 위험 1·2).
2. **맥미니에서 할 일은 9단계, 그중 콘치님이 맥미니 앞에서 직접 하셔야 하는 것은 6단계**입니다(3장). 1TR(전원 버튼 길게 누르기) 진입이 2번 있습니다.
3. **도구는 준비됐고, 맥북에서 할 수 있는 시험은 모두 통과했습니다.** GPT 계획·검사 시험 19개 통과, 묶음 파일 995개 해시 일치. [사실: `tool/tests/test_gptlib.py`, `bundle/SHA256SUMS`] 맥미니에서의 실제 동작은 확인 전입니다.
4. **이 일이 끝나도 지금 실행 스크립트(S242 등)는 그대로는 못 씁니다.** 두 가지 때문입니다.
   - 파티션 표 보호 검사가 슬롯 3이 바뀐 것을 보고 멈춥니다. → 고친 판을 미리 만들어 두는 도구를 준비했습니다(`tool/guard_update.py`, 적용은 안 함).
   - 13.5 펌웨어에서는 m1n1이 화면을 스스로 켜고, 맥북 쪽 Tahoe 화면 코드는 Tahoe 판이 아니면 멈춥니다. → 화면 갈래에서 13.5용 실행 스크립트를 새로 만들어야 합니다(5장).
5. 내려받은 양은 **약 747 MB**입니다(2장).

---

## 2. 내려받은 것 [사실]

모두 공개 자료이고 무료입니다. 계정·과금 없음. 내려받은 내용은 데이터로만 다뤘고, 내려받은 프로그램(asahi의 파이썬 실행 파일 포함)은 맥북에서 실행하지 않았습니다.

| 항목 | 출처 URL | 받은 크기(바이트) | sha256 | 위치 |
|---|---|---|---|---|
| asahi-installer 소스(git) | https://github.com/AsahiLinux/asahi-installer (main `e93743d`, 태그 v0.9.2 = `dffbb38`) | 약 925,000 | (git 커밋으로 고정) | `dl/asahi-installer/` |
| asahi-installer v0.9.2 공식 배포판(파이썬 3.13 포함) | https://cdn.asahilinux.org/installer/installer-v0.9.2.tar.gz | 22,215,466 | `484d999e53b4aef27d17c4478e49567e96842f8a6cb35c073a0df5bf0bbe8276` | `dl/asahi-installer-pkg/` |
| macOS 13.5 (22G74) 복원 이미지 중 J274 부팅 항목에 필요한 66개 파일만 | https://updates.cdn-apple.com/2023SummerFCS/fullrestores/032-69606/D3E05CDF-E105-434C-A4A1-4E3DC7668DD0/UniversalMac_13.5_22G74_Restore.ipsw (전체 12,893,195,726) | 713,985,486 (목록 읽기 포함) | 결과 파일 `9c315566b75ec744e3135d92f3925e4b01f65211641af765e4b84a76201ed419` | `dl/ipsw-13.5/` (파일별 해시는 `parts.json`) |
| AsahiLinux/docs(git, 조사용) | https://github.com/AsahiLinux/docs (`ed6f9d6`) | 약 10,000,000 | — | 조사 후 지움 |
| 합계 | | **약 747,000,000** | | |

- 배포판의 파이썬 파일은 git 태그 v0.9.2와 한 글자도 다르지 않습니다(stub.py, diskutil.py, osenum.py, system.py, util.py, step2.sh 등 25개 비교). [사실]
- 13.5 파일 고르기는 asahi `src/stub.py:248-399`가 읽는 목록과 같은 규칙입니다(`tool/fetch_ipsw_parts.py`). 받은 뒤 CRC와 sha256을 다시 확인했습니다(`fetch_ipsw_parts.py verify` → `VERIFY OK 66 members`). [사실]

주요 파일:

| 파일 | 크기(바이트) | sha256 앞 16자 |
|---|---|---|
| `022-15462-082.dmg` (13.5 복구 환경) | 1,815,115,416 | 12d11461f4a2726e |
| `kernelcache.release.mac13g` | 25,579,919 | 49847f11a6880e37 |
| `Firmware/dcp/ipad13dcp.im4p` (화면 펌웨어) | 9,445,401 | 0d9aff541ed946f4 |
| `Firmware/agx/armfw_g13g.im4p` (GPU 펌웨어) | 2,480,761 | 190207737e73a7e6 |
| `Firmware/all_flash/iBoot.j274.RELEASE.im4p` | 502,289 | c06a4dfdfc9d8f32 |

13.5 묶음에 들어 있는 OS 쪽 펌웨어(맥미니 J274) [사실: 받은 BuildManifest.plist의 j274ap 항목]:
ANE, AOP, AVE, DCP(화면), GFX(GPU, G13G), PMP(전원 관리), SIO, iBoot2, iBootData, DeviceTree, 커널.
**SSD(ANS)·SMC·USB 펌웨어는 이 목록에 없습니다.** 이것들은 지금처럼 Tahoe 시스템 펌웨어에서 옵니다. [사실: 목록에 없음] → SSD·USB 동작은 지금과 같을 것입니다. [가설]

---

## 3. 콘치님 절차 (맥미니 실기기)

### 한눈에 보기

| 단계 | 누가 | 기기·화면 | 하는 일 | 물리 조작 |
|---|---|---|---|---|
| 1 | 콘치님 | 맥미니, 1TR 복구 화면 | Macintosh HD에서 m1n1 등록을 빼서 macOS가 다시 켜지게 함 | 예 (전원 길게) |
| 2 | 콘치님 + Claude | 맥미니 macOS 로그인 화면 → 맥북 | 로그인, 준비물 폴더 복사 | 예 (로그인) |
| 3 | 콘치님 | 맥미니 macOS 터미널 | 미리 보기 실행(아무것도 안 바꿈) | 예 (맥미니 키보드) |
| 4 | 콘치님 | 맥미니 macOS 터미널 | 실제 실행: 슬롯 3을 APFS로, 13.5 파일 설치, 기본 부팅으로 지정 | 예 |
| 5 | Claude | 맥북 | 맥미니에 남은 기록(`out/`) 가져오기 | 아니오 |
| 6 | 콘치님 | 맥미니, 새 항목의 1TR 복구 화면 | m1n1(D4) 등록 마무리 | 예 (전원 길게) |
| 7 | Claude | 맥북 | 맥미니가 13.5 펌웨어로 켜졌는지 읽기만 하는 확인 | 아니오 |
| 8 | 콘치님 | 맥미니 고르기 화면 | macOS가 여전히 켜지는지 확인 | 예 |
| 9 | Claude | 맥북 | 보호 검사 갱신·13.5용 실행 스크립트(화면 갈래와 함께) | 아니오 |

비밀번호는 모두 맥미니 화면에 나오는 Apple 도구(`sudo`, `bless`, `bputil`, `kmutil`)에 콘치님이 직접 입력하십니다. 이 도구는 비밀번호를 받거나 저장하지 않습니다.

---

### 단계 1 — Macintosh HD를 다시 macOS로 (맥미니, 1TR)

지금 맥미니는 Macintosh HD를 고르면 macOS 대신 m1n1(D4)이 켜집니다. [사실: NWOAS-STATUS-2026-11-15.md:20, dualboot.md 2.1절] 4단계는 macOS 안에서 해야 하므로 먼저 되돌립니다.

1. 맥미니 전원을 끕니다. 10초 기다립니다.
2. 전원 버튼을 **누른 채로** "시동 옵션 로드 중"이 나올 때까지 놓지 않습니다.
3. **옵션(톱니바퀴)** → 계속 → (사용자 선택·암호) → 메뉴 막대 **유틸리티 → 터미널**.
4. 터미널에 입력:

```sh
bputil -g
```

- 예상: 여러 OS가 있으면 고르라고 묻고(지금은 Macintosh HD 하나뿐이라 묻지 않을 수 있음), 관리자 이름과 암호를 묻습니다. 끝나면 오류 없이 프롬프트로 돌아옵니다. [가설: 정확한 문구]
- 근거: `bputil -g`는 보안 정책을 "낮은 보안"으로 **처음부터 다시 만들어** 기존 사용자 커널 등록을 없앱니다. [사실: `man bputil` -g 항목, nwoas_scripts/macos-return-20260913/README.md]

5. 확인 후 재시작:

```sh
bputil -d | grep -i -E "security|coih"
reboot
```

- 예상: 낮은 보안(Reduced)을 뜻하는 줄과 `(coih): absent`. [가설: 줄 문구는 판마다 다를 수 있음] 재시작하면 **macOS 로그인 화면**이 나옵니다.

**되돌리기 1** (macOS 대신 예전처럼 D4로 돌아가고 싶을 때) — 같은 1TR 터미널에서:

```sh
bash "/Volumes/Macintosh HD - Data/Users/Shared/NWOAS-recovery-D4-20261115/install-in-recovery.sh"
```

[가설: 11-15에 콘치님이 이 위치의 스크립트로 D4를 다시 등록하셨다는 작업 지시 내용에 따름. 이 맥북에서는 그 파일을 볼 수 없어 내용은 확인하지 못했습니다.] Data 볼륨이 잠겨 있으면 디스크 유틸리티에서 먼저 마운트합니다.

---

### 단계 2 — 로그인과 폴더 복사

1. (콘치님) 맥미니 macOS에 로그인합니다. **4단계가 끝날 때까지 로그인 상태를 유지**합니다(Asahi 도구가 화면 앞 로그인 사용자를 요구함 [사실: asahi `src/main.py:981-987`]).
2. (Claude, 맥북에서) 준비물 폴더(727 MB, 파일 995개)를 맥미니 `/Users/Shared/stub-135-bundle/`로 복사합니다. 9월에 쓴 SSH 키 경로가 아직 살아 있으면 다음 명령입니다. [가설: 주소 `100.81.93.114`는 NWOAS-STATUS-2026-09-06.md:99의 기록이라 지금도 맞는지 미확인]

```sh
rsync -a --progress /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/bundle/ angigyeom@100.81.93.114:/Users/Shared/stub-135-bundle/
```

   **SSH가 안 되면(추천 경로)**: 준비물이 TrueNAS 공유 `media/nwoas/stub-135-bundle`에 이미 있습니다(지문 959개 확인, 2026-11-16). 맥미니 Finder에서 서버 연결로 `media`를 연 뒤 터미널에서 `cp -R /Volumes/media/nwoas/stub-135-bundle /Users/Shared/stub-135-bundle`.
   (2026-11-16 수정: asahi 압축본에 섞여 있던 macOS 메타 파일 `._*` 36개를 지우고 SHA256SUMS를 959개로 다시 만듦 — SMB 복사 때 이 파일들이 사라져 검사에 걸리기 때문.)
   그것도 안 되면 콘치님이 USB 저장장치로 `bundle` 폴더를 옮기고, 맥미니 터미널에서 `cp -R /Volumes/<USB이름>/bundle /Users/Shared/stub-135-bundle`.
   AirDrop으로 옮긴 경우에만 다운로드 표시를 지웁니다(표시가 있으면 포함된 파이썬이 실행 확인 창에 막힐 수 있음 [가설]):

```sh
xattr -dr com.apple.quarantine /Users/Shared/stub-135-bundle
```

**되돌리기 2**: `rm -rf /Users/Shared/stub-135-bundle` (맥미니에 다른 영향 없음).

---

### 단계 3 — 미리 보기 (맥미니 macOS 터미널, 아무것도 바꾸지 않음)

```sh
cd /Users/Shared/stub-135-bundle
sudo ./nwstub.sh
```

- `sudo`가 묻는 암호는 콘치님 macOS 로그인 암호입니다.
- 이 단계가 하는 일: `/dev/rdisk0`의 파티션 표 11블록을 **읽기만** 하고, `diskutil list/info`(읽기)를 부르고, Asahi 방식대로 Macintosh HD 컨테이너의 Preboot·Recovery 볼륨을 마운트해 관리자 목록을 읽습니다. 디스크에 쓰지 않습니다. [사실: `tool/nwstub.py`의 `--apply` 앞 부분]
- 예상 출력(순서대로):

```
[CHECK] Macmini9,1, disk0 internal, 4096-byte blocks, 61279344 blocks
[CHECK] raw GPT = S231 layout; slot 3 = NWSTUB (Basic Data, attrs 0xD000000000000000, LBA 38968576-39944703)
[CHECK] slot 3 = /dev/disk0s3 (content Microsoft Basic Data), not mounted      <- disk0s 번호는 다를 수 있음
[CHECK] bundle: 995 files match SHA256SUMS; boot.bin = D4; IPSW 13.5 22G74 j274ap
[CHECK] macOS 26.5.2 (25F84); system firmware mBoot-18000.121.3; SFR ...; boot VGID ...
[CHECK] running OS: Macintosh HD 26.5.2, admin users [...]
BEFORE (on disk now) ... AFTER (planned) ...      <- 아래 표와 같아야 함
[DRY-RUN] nothing was changed. Re-run with --apply to do it.
```

- `[STOP] ...`이 나오면 거기서 멈춥니다. 디스크는 바뀌지 않았습니다. 그 줄을 Claude에게 알려 주세요.
- `/dev/rdisk0`을 읽을 때 "Operation not permitted"가 나오면 터미널에 "전체 디스크 접근 권한"이 필요한 것입니다(시스템 설정 → 개인정보 보호 및 보안). [가설: Tahoe에서 root의 원시 디스크 읽기가 막히는지 미확인]

계획된 전후 표 (실제 S231 파티션 표 사본으로 맥북에서 모의 실행한 결과 [사실: `nwstub.py --simulate-gpt`]):

| 슬롯 | 지금 종류 | 바뀐 뒤 종류 | 시작 LBA | 끝 LBA | 크기(GB) | 바뀌는가 |
|---|---|---|---|---|---|---|
| 1 | Apple_APFS_ISC | Apple_APFS_ISC | 6 | 128,005 | 0.524 | 아니오 |
| 2 | Apple_APFS (macOS) | Apple_APFS (macOS) | 128,006 | 38,968,337 | 159.090 | 아니오 |
| 3 | Microsoft Basic Data (NWSTUB, 속성 0xD000000000000000) | **Apple_APFS** | 38,968,576 | 39,944,703 | 3.998 | **종류만** (고유 GUID·속성·이름은 diskutil이 바꿀 수 있어 실행 뒤 다시 읽어 기록) |
| 4 | EFI (Windows ESP) | EFI | 39,944,960 | 40,076,031 | 0.537 | 아니오 |
| 5 | Microsoft Reserved | Microsoft Reserved | 40,076,032 | 40,080,127 | 0.017 | 아니오 |
| 6 | Microsoft Basic Data (Windows) | 같음 | 40,080,128 | 59,968,511 | 81.463 | 아니오 |
| 7 | Apple_APFS_Recovery | 같음 | 59,968,630 | 61,279,338 | 5.369 | 아니오 |

- 모든 칸의 LBA가 그대로이므로 하이퍼바이저의 Windows 쓰기 구역(39,944,960 ~ 59,968,629)과 상수는 바꿀 필요가 없습니다. [사실: 위 표, `m1n1-layout-s231/src/nvme.c:543-544`]

**되돌리기 3**: 없음(바뀐 것이 없음).

---

### 단계 4 — 실제 실행 (맥미니 macOS 터미널)

```sh
cd /Users/Shared/stub-135-bundle
sudo ./nwstub.sh --apply
```

진행 순서와 예상 출력:

| 순서 | 화면에 나오는 것 | 콘치님이 할 일 |
|---|---|---|
| ① | 3단계와 같은 `[CHECK]` 줄과 전후 표 | 확인 |
| ② | `Type NWSTUB to continue:` | `NWSTUB` 입력 후 Enter (다른 글자는 중단) |
| ③ | `[SAVED] GPT blocks before the change -> .../out/gpt-before-<시각>` | — |
| ④ | diskutil의 `Started erase on disk0sN ... Finished erase` | — |
| ⑤ | `[CHECK] GPT after diskutil: only slot 3 changed {...}` 와 `AFTER (re-read from disk)` 표 | — |
| ⑥ | Asahi 진행 표시: `Preparing target volumes...`, `Setting up System volume...`, `Setting up Recovery volume...`(복구 환경 1.8 GB 압축, 몇 분) | 기다림 |
| ⑦ | `[DONE] stub files installed: VGID ..., System /dev/diskXsY` | — |
| ⑧ | (관리자가 여럿이면 번호 선택) `[BLESS] bless --setBoot ...` 뒤에 bless가 암호를 물음 | macOS 관리자 암호 입력 |
| ⑨ | `[CHECK] stub hidden for step 2 ...` / `[NEXT] Shut down ...` | 6단계로 |

이 단계가 바꾸는 것 [사실: `tool/nwstub.py`]:
- GPT 슬롯 3의 종류(`diskutil eraseVolume APFS "Windows" disk0sN` — Asahi도 빈 칸을 APFS로 만들 때 같은 명령을 씀 [사실: asahi `src/diskutil.py:230`]).
- 그 안의 새 볼륨 4개: `Windows - Data`, `Windows`, `Preboot`, `Recovery`와 13.5 파일(asahi `stub.py`를 고치지 않고 그대로 사용).
- 기본 부팅 볼륨을 새 `Windows` 항목으로(`bless --setBoot`). 다음 1TR이 이 항목의 13.5 복구 환경으로 들어가게 하기 위함입니다(Asahi와 같은 순서 [사실: asahi `src/main.py:685-699`]).
- macOS(Macintosh HD)의 보안 정책, 다른 파티션, Windows 파일은 건드리지 않습니다.

`diskutil` 뒤 파티션 표를 원시 디스크에서 다시 읽어, 슬롯 3 말고 다른 칸이 1바이트라도 바뀌었거나 슬롯 3의 위치·크기가 바뀌었거나 디스크 GUID·헤더가 바뀌었으면 `[STOP]`으로 멈춥니다. 이 검사는 시험 15개로 확인했습니다. [사실: `tool/tests/test_gptlib.py` After 묶음]

**되돌리기 4**

| 멈춘 곳 | 디스크 상태 | 할 일 |
|---|---|---|
| ② 전 | 바뀐 것 없음 | 없음 |
| ④~⑤ `[STOP]` | 슬롯 3 종류가 바뀜(다른 칸까지 바뀌었을 수 있음) | **되돌리기 R2**로 파티션 표를 저장본대로 되씀. 그 전에 더 진행하지 않음 |
| ⑥~⑦ 도중 오류 | 슬롯 3에 APFS 컨테이너 | 컨테이너를 그대로 두어도 macOS·Windows에 영향 없음 [가설]. 다시 하려면 R2 후 3단계부터 |
| ⑧ bless 실패 | 항목은 설치됨, 기본 부팅은 그대로 | 4장 위험 1 참조. 6단계는 Macintosh HD 1TR에서 이 항목을 골라도 시작됨(아래 6단계의 "NOT-PAIRED") |

---

### 단계 5 — 기록 가져오기 (Claude, 맥북)

```sh
mkdir -p /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/results
rsync -a angigyeom@100.81.93.114:/Users/Shared/stub-135-bundle/out/ /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/results/
```

`results/`에 `gpt-before-<시각>/`, `gpt-after-<시각>/`(각 11블록 + sha256.json), `stub-<시각>.json`(VGID 등, 사용자 이름 제외), `nwstub-<시각>.log`가 생깁니다. 9단계와 되돌리기 R2가 이 파일들을 씁니다. SSH가 안 되면 USB로 옮깁니다.

---

### 단계 6 — m1n1 등록 마무리 (맥미니, 새 항목의 1TR)

1. 맥미니: Apple 메뉴 → 시스템 종료. **25초** 기다립니다.
2. 전원 버튼을 **한 번 길게, 놓지 않고** "시동 옵션 로드 중"이 보일 때까지 누릅니다. (짧게 눌렀다 다시 누르면 맞는 모드가 아님 [사실: asahi `src/main.py:744-756` 안내])
   - 이때 켜지는 복구 환경이 **13.5 복구 환경**입니다. 여기서 화면이 뜨면 "Tahoe 시스템 펌웨어 위에서 13.5 복구 환경이 켜진다"는 첫 실기기 증거입니다.
3. 고르기 화면에 `Windows`(Asahi 로고 아이콘)와 `Macintosh HD`, 옵션이 보입니다. **Windows**를 고르고 계속.
   - "복구할 볼륨을 선택"이 나오면 Macintosh HD를 고르고 macOS 관리자로 인증합니다. [사실: asahi `src/main.py:758-760` 안내]
4. 터미널 창이 자동으로 열리고 다음이 보입니다(영어 화면 — 뜻은 오른쪽):

| 화면 문구 | 뜻 | 할 일 |
|---|---|---|
| `CHECK boot.bin = NWOAS D4 m1n1 (MD5 7bec0225...)` | 등록할 파일이 D4가 맞음 | — |
| `CHECK paired 1TR for <VGID>` | 맞는 복구 환경에 제대로 들어왔음 | — |
| `Next, for THIS volume group only: 1. bputil -nc ... 2. kmutil configure-boot ...` | 이 항목에만 할 두 명령 안내 | Enter |
| bputil의 사용자 이름·암호 질문 | 이 항목만 "허용 보안"으로 낮춤 | macOS 관리자 이름·암호 |
| kmutil의 사용자 이름·암호 질문 | D4를 이 항목의 부팅 파일로 등록 | 같은 이름·암호 |
| `STEP2-DONE: policy recorded in .../stub135-bputil-after.txt` | 등록 끝(부팅 성공은 아님) | Enter → 재시작 |
| `NOT-PAIRED: ...` | 다른 복구 환경에 들어옴 | Enter → 암호 → 꺼지면 1번부터 다시 |
| `NOT-1TR: ...` | 전원 버튼을 끊김 없이 누르지 않았음 | Enter → 꺼지면 2번부터 다시 |
| `STOP: boot.bin ...` | 등록 파일이 바뀌었음 | 멈추고 Claude에게 알림 |

- 터미널이 자동으로 안 열리면 메뉴 유틸리티 → 터미널에서 직접:

```sh
sh "/Volumes/Windows/Finish Installation.app/Contents/Resources/step2.sh"
```

- 보안 수준: Asahi와 같게 `bputil -nc`(허용 보안 + 커널 메모리 잠금 끄기)를 **이 항목에만** 씁니다. 작업 지시에는 "낮은 보안"이라고 되어 있었지만, `-n`이 Asahi가 모든 설치에서 쓰는 검증된 순서라 그대로 두었습니다. Macintosh HD의 정책은 바뀌지 않습니다. [사실: asahi `src/step2/step2.sh:107`, `man bputil` -n/-c: "Paired only, 1TR"]
- 재시작 후 예상: 맥미니가 새 항목(기본 부팅)으로 켜지고 m1n1(D4)이 맥북 연결을 기다립니다. 13.5에서는 D4가 화면을 직접 켜므로 HDMI에 m1n1 로고가 보일 수 있습니다. [가설]

**되돌리기 6**
- 도중에 멈췄거나 반복 재부팅: 전원 길게 → 고르기 화면에서 **Macintosh HD**를 고르고, 옵션 키를 누른 채 "항상 사용"으로 기본 부팅을 macOS로 돌립니다. [가설: 버튼 문구는 판마다 다를 수 있음] 1TR 터미널이면 `bless --setBoot --mount "/Volumes/Macintosh HD"`.
- 등록을 지우려면(Macintosh HD의 1TR 터미널에서): `bputil -r <Windows 항목 VGID>` (VGID는 `results/stub-<시각>.json`). [사실: `man bputil` -r]
- "Local policy update is in progress"에서 반복 재부팅하면 오래된 정책 찌꺼기 문제입니다(4장 위험 2).

---

### 단계 7 — 13.5 펌웨어로 켜졌는지 확인 (Claude, 맥북, 읽기만)

맥미니가 새 항목으로 켜져 m1n1이 기다리는 상태에서:

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONPATH=proxyclient \
  .venv-hv/bin/python3 -B /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/tool/probe_fw135.py
```

- 통과 조건(G-1): `os_firmware: iBoot-8422.141.2` 이고 `system_firmware: mBoot-18000...` → `G-1 PASS: 13.5 OS firmware on Tahoe system firmware`.
- m1n1 시작 기록에도 `OS FW version: 13.5 (iBoot-8422.141.2)`가 찍힙니다. [사실: `m1n1-layout-s231/src/firmware.c:138`]
- 이 확인은 ADT만 읽습니다. 하이퍼바이저·Windows는 띄우지 않습니다.

---

### 단계 8 — macOS가 그대로 켜지는지 (맥미니)

전원 길게 → 고르기 화면 → **Macintosh HD** → macOS 바탕화면이 나오면 통과. 다시 개발 모드로 가려면 재시작(기본 부팅이 Windows 항목) 또는 고르기 화면에서 Windows.

- 기본 부팅을 Windows 항목으로 둔 이유: Windows/WinPE가 재시작하면 맥미니 전체가 재부팅되는데 [사실: NWOAS-STATUS-2026-11-15.md "Known problems"], 그때 m1n1으로 돌아와야 개발 반복이 이어집니다. macOS를 기본으로 원하시면 macOS의 시스템 설정 → 시동 디스크에서 바꾸시면 됩니다.

---

### 단계 9 — 보호 검사 갱신과 실행 스크립트 (Claude, 맥북)

```sh
python3 -I -B /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/tool/guard_update.py \
  --guest-module /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/layout-s231/guest_module.py \
  --before /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/results/gpt-before-<시각> \
  --after  /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/results/gpt-after-<시각> \
  --scripts /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts \
  --out /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/guard-update
```

- 만드는 것: 고친 `guest_module.py`(원본 다음에 한 덩어리만 끼움), 차이 파일, 새 sha256, 그리고 **실행하지 않은** `apply-guard-update.sh`(원본 보관 → 새 파일 설치 → 옛 해시를 고정한 실행 스크립트 18개와 manifest.json의 해시를 바꿈).
- 고친 판은 슬롯 3이 **NWSTUB 원래 값이거나, 기록된 새 APFS 값일 때만** 통과시킵니다. 그래서 되돌리기 R2 뒤에도 같은 실행 스크립트가 그대로 동작합니다. 둘 다 아니면 보호 검사 전에 멈춥니다.
- 시험: 실제 `nvme-s124/gpt_guard.py`로 "지금 디스크 통과 / 고치기 전 판은 바뀐 디스크 거부 / 고친 판은 바뀐 디스크 통과"를 확인했습니다. [사실: `test_real_gptguard_accepts_patched_protected`]
- 원본 `guest_module.py`는 바꾸지 않았습니다(sha256 `e85591e9...` 그대로). [사실]

---

### 되돌리기 R2 — NWSTUB 자리를 원래 파티션 표 그대로 되돌리기

슬롯 3을 4단계 전 바이트 그대로(Basic Data, 속성 0xD000000000000000, GUID 8d17c4ed-...) 되돌립니다. 11-15에 파티션 표를 쓴 것과 같은 m1n1 경로를 씁니다. [사실: `nwoas_scripts/layout-s231/gpt_partition_s231.py` restore 모드, 저장 형식 `lba-N.bin` + `sha256.json`이 4단계 저장본과 같음]

1. (맥미니) m1n1이 맥북을 기다리는 상태로 켭니다: Windows 항목이 동작하면 그것으로, 아니면 **되돌리기 1**로 Macintosh HD에 D4를 다시 등록.
2. (Claude, 맥북) 하이퍼바이저·게스트를 띄우지 않은 상태에서:

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONPATH=proxyclient \
  .venv-hv/bin/python3 -B ../nwoas_scripts/layout-s231/gpt_partition_s231.py restore \
  /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/results/gpt-before-<시각>
```

   - 예상: `RESTORE: wrote LBA ...` 줄들 뒤 `RESTORE PASS: wrote [...]; GPT blocks equal the saved originals`. [사실: 해당 파일 250-264줄] [가설: 실행 형식은 런처의 환경 변수를 따른 것]
   - 4단계 저장본이 없을 때의 대체: 11-15 파티션 표 그대로인 `nwoas_scripts/layout-s231/gpt-plan/new-lba-*.bin`(파일 이름만 `lba-*.bin`으로 바꾼 사본 + sha256.json)도 같은 내용입니다. [사실: 시험의 기준 자료]
3. (맥미니, 1TR 터미널, 선택) 사라진 항목의 정책 정리: `bputil -r <Windows 항목 VGID>`.
4. 기본 부팅이 사라진 항목을 가리키면 고르기 화면에서 Macintosh HD를 "항상 사용"으로.

R2 뒤 상태: 파티션 표는 4단계 전과 같고, 슬롯 3 안의 옛 APFS 흔적은 남지만 종류가 Basic Data라 macOS·Windows가 쓰지 않습니다. [가설]

---

## 4. 위험

| 번호 | 위험 | 근거 | 대비 |
|---|---|---|---|
| 1 | Tahoe에서 `bless --setBoot`가 `BYErrorDomain Code=112 "Preserved restore bundle in preboot is missing"`로 실패 | [사실] asahi-installer #404(열림)·#407, M1 계열 26.2 보고 다수. 원인은 본 macOS의 복구 판 불일치로 추정됨(보고자 추정) | 실패해도 항목은 설치된 채로 멈추고 다른 것을 바꾸지 않음. 6단계를 Macintosh HD 1TR에서 시작하면 step2가 복구 환경에서 bless를 다시 시도. 그래도 안 되면 보고된 해결책은 macOS 갱신 또는 DFU 복구인데, 둘 다 시스템 펌웨어를 바꿔 지금 Tahoe 화면 코드(판 `mBoot-18000.121.3` 고정)에 영향 → **콘치님 결정 사항** |
| 2 | 6단계에서 "Local policy update is in progress" 반복 재부팅 | [사실] asahi-installer #425(M1, macOS 26.5), #255. 해결: 1TR에서 `curl -L https://alx.sh/cleanbp \| sh`(사라진 OS의 정책 폴더를 지움) | 지우는 동작이므로 필요할 때 따로 승인받고 실행. 스크립트 내용은 읽어 확인함(iSCPreboot 아래 현재 없는 VGID 폴더만 지움) |
| 3 | diskutil이 슬롯 3 말고 다른 것도 바꿈 | [가설] 확인 전 | 도구가 원시 표를 다시 읽어 멈춤 → R2 |
| 4 | 13.5에서 화면 경로가 바뀜 | [사실] D4는 OS 펌웨어 문자열이 `mBoot-18000.121.3`일 때만 화면 초기화를 미룸(`m1n1-layout-s231/src/main.c:169-172`, D4 바이너리에 같은 문구와 `iBoot-8422.141.2` 판 표 있음) | 5장 |
| 5 | 지금 실행 스크립트가 멈춤 | [사실] 보호 검사(슬롯 3 바이트 비교, `gpt_guard.py:46-62`)와 Tahoe 화면 훅(`display-s241/tahoe_dcp_guest_hook.py:125`의 판 검사) | 9단계 + 5장. 둘 다 "멈춤" 쪽이라 디스크를 망가뜨리는 방향은 아님 |
| 6 | 13.5의 PMP(전원 관리)·SIO·AOP 펌웨어로 바뀌어 CPU 속도·전원 관련 맥북 쪽 모듈(cpufreq-s193, pmgr_gate 등) 동작이 달라질 수 있음 | [사실] 2장 목록에 PMP·SIO·AOP 포함 / [가설] 영향 | 첫 실행에서 해당 로그 확인 |
| 7 | 1~6단계 사이에는 맥북 연결 개발을 못 함 | [사실] 1단계에서 Macintosh HD의 D4를 뺌 | 급하면 되돌리기 1 |
| 8 | 새 항목이 4 GB에 다 들어가는가 | [사실] Asahi는 같은 내용을 2.5 GB 칸에 넣음(`src/main.py:11`) | 여유 있음 |
| 9 | 포함된 파이썬(Asahi 배포판, python.org 3.13)을 맥미니에서 실행 | [사실] Asahi 공식 설치기가 쓰는 그 파일, 해시 고정 / [가설] AirDrop 시 실행 확인 창 | 2단계 `xattr` |
| 10 | 고르기 화면 아이콘이 Asahi 로고 | [사실] asahi `logo.icns` 사용 | 외형만의 문제 |
| 11 | macOS 27로 올리면 고르기 화면에서 항목이 숨을 수 있음 | [사실] asahi 2026-06 보고(dualboot.md 4.1), 도구가 부팅 가능 표시를 확인함 | 맥미니는 Tahoe 유지(D1) |

---

## 5. 13.5에서 바뀌는 m1n1 화면 경로와 실행 흐름

| 항목 | 지금(Tahoe OS 펌웨어) | 13.5 항목 |
|---|---|---|
| 첫 m1n1(D4)의 화면 | 초기화를 미루고 `NWOAS-DCP-DEFER` 출력 [사실] | `display_init()` 실행 → 외장 화면 분기에서 `display_configure` [사실: main.c:169-172, display.c:640-650] / 실제로 켜지는지는 [가설] |
| 화면 펌웨어 판 | Tahoe(맥북 쪽 Tahoe 전용 코드로만 다룸) | 13.5 — m1n1 C 코드가 원래 지원하는 판 [사실: firmware.c V13_5] |
| 맥북 쪽 화면 훅(S241/S242) | 필수 | **쓸 수 없음**: 판 검사에서 멈춤 [사실: hook:125] |
| 하이퍼바이저(S233b, 맥북이 USB로 실음) | 같은 지연 코드 포함 [사실: 바이너리에 문구 1회] | 실린 하이퍼바이저도 스스로 화면 초기화 [가설] |
| Windows가 받는 화면 | 훅이 맞춘 1080p/720p 프레임버퍼 | m1n1이 맞춘 프레임버퍼를 펌웨어(UEFI)가 그대로 씀 [가설, 미확인] |
| GPU 펌웨어 | Tahoe판 — 공개 드라이버가 받지 않음 | 13.5 G13G — Asahi·windows-on-m1이 받는 판 [사실: gpu.md 2.3] |

실행 흐름에 주는 뜻:
- 13.5 항목으로 개발하려면 `display-s242-guest-test.sh`에서 `--pre-init-script .../tahoe_dcp_guest_hook.py`와 그 해시 고정 2줄을 뺀 13.5용 실행 스크립트가 필요합니다. 화면 해상도 선택(1080p 우선)은 m1n1 쪽에서 다시 해야 합니다. → 화면 갈래 작업으로 넘깁니다(이 폴더에서는 만들지 않음).
- Tahoe 화면 실험을 계속하려면 Macintosh HD에 D4를 다시 넣어야 하는데, 그러면 macOS가 켜지지 않습니다. 두 가지를 동시에 쓸 수 없습니다. [사실: 1단계 설명]

---

## 6. 도구 상태

| 파일 | 하는 일 | 확인한 것 |
|---|---|---|
| `tool/nwstub.py` | 맥미니에서 미리 보기(기본)·`--apply` 실행, `--simulate-gpt`로 맥북 모의 실행 | 모의 실행 통과, 문법 검사(파이썬 3.9·3.14), 묶음 무결성 검사 통과. **맥미니 실행은 미확인** |
| `tool/gptlib.py` | 파티션 표 읽기·현재 상태 확인·실행 뒤 검사 | 시험 19개 모두 통과 |
| `tool/step2/step2.sh` | 새 항목 1TR에서 D4 등록(Asahi step2 바탕) | `sh -n` 문법 통과, 빈칸 채움 시험 통과. **실기기 미확인** |
| `tool/guard_update.py` | 보호 검사 고친 판·해시 갱신 스크립트 생성(적용 안 함) | 실제 gpt_guard.py로 시험 통과 |
| `tool/probe_fw135.py` | 7단계 읽기 전용 확인 | 실행 안 함(맥미니 필요) |
| `tool/fetch_ipsw_parts.py` | 13.5 필요 파일만 받기·검증 | 받기·검증 완료 |
| `tool/make_bundle.sh` | `bundle/` 만들기(복사만) | 완료, 995개 파일 |
| `tool/tests/test_gptlib.py` | 위 시험 | `python3 -I -B tool/tests/test_gptlib.py` → `Ran 19 tests ... OK` |
| `bundle/` | 맥미니로 옮길 폴더(727 MB) | `SHA256SUMS`로 실행 때마다 다시 검사 |

`bundle/`의 IPSW 파일은 `dl/ipsw-13.5/`의 같은 파일에 대한 하드 링크라 디스크를 두 번 쓰지 않습니다.

---

## 7. 조사 출처

GitHub 검색(`gh search issues`, AsahiLinux 조직 전체): "Tahoe", "macOS 26", "26.0", "26.5", "26.6", "system firmware", "firmware newer", "system firmware newer", "13.5 stub", "OS firmware 13.5", "sfr 26", "stub Tahoe", "mBoot-18000", "macOS Tahoe boot", "j274 HDMI", "Mac mini HDMI", "j274 tahoe".

| 출처 | 내용 |
|---|---|
| github.com/AsahiLinux/m1n1/issues/733 | j293(M1), 시스템 펌웨어 mBoot-18000.161.10(26.6.2) + 13.5 항목, m1n1 1.6.1로 부팅·Linux 동작 (닫힘, 템플릿 미사용 사유) |
| github.com/AsahiLinux/asahi-installer/issues/404, /407 | Tahoe 26.2 bless Code=112, M1 계열 사용자들이 macOS 재설치·갱신 뒤 설치 완료 |
| github.com/AsahiLinux/asahi-installer/issues/425, /255 | M1 MBA 26.5 step2 반복 재부팅 → cleanbp 또는 DFU |
| github.com/AsahiLinux/asahi-installer/issues/441 | M4(지원 안 되는 기종) 26.5.1에서 항목 1TR 진입 실패 — 우리 기종과 다름 |
| github.com/AsahiLinux/linux/issues/638 | M1 Pro, OS 펌웨어 13.5로 동작 중인 보고 |
| asahi-installer v0.9.2 `src/main.py:11,97-124,587-623,637-699`, `src/stub.py:51-399`, `src/diskutil.py:216-237`, `src/step2/step2.sh` | 판 선택 규칙, 항목 구성, eraseVolume 사용, 2단계 |
| AsahiLinux/docs `docs/platform/introduction.md:104`, `docs/project/glossary.md:112`, `docs/sw/partitioning-cheatsheet.md:5-9,54-67` (ed6f9d6) | OS별 펌웨어·SFR 전진 원칙, 복구 파티션 금지 |
| `man bputil`, `man diskutil` (이 맥북 macOS 27.2.1) | -g/-n/-c/-r 동작, eraseVolume. 맥미니(26.5.2)의 판과 같은지는 미확인 |
| `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/layout-s231/gpt-plan/plan.json`, `guest_module.py:169-186`, `nvme-s124/gpt_guard.py:26-77` | 현재 파티션 표·보호 목록 |
| `/Users/angigyeom/Desktop/DEV/NWOAS/recovery-tahoe-proxy-20260906/m1n1-bare.bin` | D4: 2,097,152바이트, MD5 7bec0225edc101857be3b7f195dd7357, sha256 80a13ca257fcf1d04676e08355b78ffec57cf82fbbbf3b3395f3524daa187e48 |
