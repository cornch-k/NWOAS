# 1TR 카드(PLAN-4-1 8장) 명령 대조 (2026-11-21 01:3x)

> 기록·소스만 읽음. 조사: 지휘 세션의 Opus 에이전트. 표기 [사실]/[가설].

## 0. 결론
카드의 큰 줄기는 그대로 써도 되고, 막다른 줄 3곳과 미리 확인할 줄 1개를 채워야 했습니다. 네 곳 모두 8장에 반영했습니다.
1. 4) 두 마운트가 모두 실패할 때 → 종료·사진 줄 추가
2. 3) disk0s4가 ESP가 아닐 때 → 칸 UUID로 장치 찾기, 안 되면 `diskutil list`(거르기 없음)
3. R2에서 kmutil이 실패할 때 → Macintosh HD(Tahoe 펌웨어 D4)로 켜고 맥북은 S243 계열로 확인. R3에도 같은 설명
4. 4-1) 스텁 `step2.sh` 사본에 `shasum`이 없는지(`grep -c` = 0) 1TR에서 먼저 확인

## 1. 명령 판정
판정 기준:
- **돎**: 이 기기 1TR에서 실제로 돈 기록이 있음.
- **목록**: 13.5 복구 이미지 실행 파일 목록에 있음. 목록 파일은 `research-20261116/C4-recovery-13.5-commands.txt`이고, BaseSystem을 읽기 전용으로 열어 뽑았습니다.

| 명령 | 판정 | 근거 / 대안 |
|---|---|---|
| `sh`, `stat -f %z`, `md5 -q`, `grep -q`, `bputil -d/-nc -v`, `kmutil configure-boot`, `/tmp` 쓰기, `reboot`, `mount -u -w`, `sed -i ''` | 돎 (13.5 스텁 1TR, 11-16) | `HANDOFF-2026-11-16.md` 00:44·4398, `RESULT-20261116-morning.md:52` |
| `sed -n`, `cp`, `command -v`, 글롭 (go1) | 목록 | 실패하면 go1이 STOP, 바뀐 것 없음 |
| `diskutil info/mount/list`, `mount_msdos -o rdonly`, `mkdir -p`, `ls /Volumes` | 목록. 옵션(`readOnly`, `internal`, `-o rdonly`)이 13.5에서 되는지는 화면에서 보지 못함 | 다음 줄 추가함(0장 1·2) |
| `shutdown -h now` | 목록. 13.5에서는 돈 적 없음. 26.x Macintosh HD 1TR에서는 돎 | 안 되면 Apple 메뉴 → 시스템 종료, 그래도 안 되면 전원 10초 |
| `cleanbp-local.sh` (`diskutil apfs listVolumeGroups`, `rm -rf`, `read`) | 목록. 이 기기에서는 돈 적 없음 | 기본은 목록만 보여 주고, 지우기는 그 자리 승인 |

## 2. 되돌리기의 바탕
- Macintosh HD에 D4가 있다는 것은 마지막 기록 기준으로 사실입니다. 11-16 오전 다시 등록(`kmutil returned success`)했고, 그 뒤 바꾼 기록이 없습니다. 오늘 화면으로 다시 본 것은 아닙니다.
- Macintosh HD로 켜면 Tahoe 펌웨어입니다. 그래서 13.5용 실행기의 탐침은 실패하고, 맥북은 S243 계열로만 다룰 수 있습니다.
- 같은 스텁에 kmutil을 두 번째로 거는 것, 이미 낮춘 정책에 `bputil -nc`를 거는 것은 한 번도 해 보지 않았습니다 [사실]. 둘 다 실패했을 때 갈 줄을 카드에 넣었습니다.

## 3. watch-after-go1.sh
- 멈추는 구조가 없습니다. 기다리는 곳은 장치 180 s, 탐침 3번, ST1-S268 줄 최대 50분이고, 래퍼는 떼어 띄우고 DONE 줄로 판정합니다.
- 손 없이 시험할 수 있습니다. D4 대기 상태에서 `--rollback`을 돌리면 D4-BACK이, 그냥 돌리면 KEEP이 나와야 합니다.

## 4. 등록 파일 = 리허설 파일
stage1.bin `8879def8…4778`(2,097,152 B, md5 `d01ab86b…`)는 리허설·빌드 출력·ESP 되읽기·go1.sh 확인값이 모두 같습니다 [사실].

## 5. 리허설과 실제 차이 (iBoot에서 바로 1단계)
| 항목 | 판정 | 내용 |
|---|---|---|
| 1단계 시작 코드가 iBoot 바로 뒤에 처음 돎 | 가설(중간) | 멈추면 감시가 ROLLBACK을 냄(180 s 안에 프록시 없음, 또는 대체 프록시) |
| iBoot 직후 ANS 준비 시간 | 가설 | `nvme_init` 시간 제한 → ESP 읽기 실패 → 대체 프록시 → ROLLBACK |
| 부팅 인자를 iBoot가 직접 줌 | 사실(구조) / 차이는 가설 | D4의 G-2 검사(phys_base·mem_size·화면)가 잡음 |
| NVMe 첫 켜기, USB, 화면 | 사실 | 리허설과 같은 조건(D4는 일상 경로에서 NVMe를 안 켬) |
