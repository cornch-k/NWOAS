# 와이파이 s258r — s258q 드라이버 + macOS 13.5 와이파이 펌웨어 (2026-11-19 10:56)

> 표기: **[사실]** = 파일·명령 출력으로 확인한 것, **[가설]** = 확인하지 않은 추측, **[빌드만]** = 만들고 검사까지만 했고 맥미니에서는 돌리지 않은 것.

## 결론

1. **macOS 13.5의 BCM4378 펌웨어는 18.20.383.15.7.8.150(2023-05-13 빌드, FWID 01-b37727a5)입니다 [사실].** 공개 기록에 남은 맥미니 M1 Linux 사용자(omacom/omarchy-mac 이슈 729)의 펌웨어 판·FWID와 글자 하나까지 같습니다. s258q 결과 문서 1-2절의 "18.20.383은 13.5 스텁에서 나온 것" 가설은 이제 사실입니다.
2. **새로 내려받은 것은 0바이트입니다 [사실].** Asahi 설치기가 펌웨어를 꺼내는 이미지(13.5 복구 이미지 `022-15462-082.dmg`)는 11-15에 스텁 작업으로 이미 받아 둔 묶음 안에 있었고, 해시가 그때 기록과 같았습니다.
3. **s258r 꾸러미를 만들었습니다 [빌드만].** `nwoa4378.sys`는 s258q와 한 바이트도 다르지 않고(드라이버 다시 빌드 안 함), 펌웨어 3개(`.trx`·`.clmb`·`.txcb`)만 13.5 것으로 바꿨습니다. NVRAM 2개는 13.5와 27.2.1이 똑같습니다. 카탈로그가 펌웨어까지 덮으므로 Parallels VM에서 카탈로그를 다시 만들고 같은 시험 인증서로 서명했습니다.
4. **INF 판을 1.0.1.8 → 1.0.1.9로 올렸습니다.** 같은 판이면 제자리 설치(`pnputil /add-driver /install`)가 s258q를 바꾸지 않을 수 있기 때문입니다. **그래서 다음 드라이버 빌드는 1.0.1.10 이상이어야 합니다** (1.0.1.9로 빌드하면 s258r 위에 설치되지 않고, 판 번호 확인도 잘못 통과합니다).
5. 맥미니에서는 아무것도 돌리지 않았습니다. 지금 맥미니에서 s258q 시험이 돌고 있으므로, 그 시험이 끝난 뒤 아래 4장 명령으로 돌립니다.

## 1. 가져온 것

결론: 받아 둔 묶음에서 복구 이미지 하나를 꺼내 읽기 전용으로 붙인 뒤, 우리 꾸러미가 쓰는 5개 파일만 복사했습니다.

| 항목 | 값 |
|---|---|
| 원본 주소 | https://updates.cdn-apple.com/2023SummerFCS/fullrestores/032-69606/D3E05CDF-E105-434C-A4A1-4E3DC7668DD0/UniversalMac_13.5_22G74_Restore.ipsw |
| 고른 이미지 | `022-15462-082.dmg` = 짝 복구 환경(Asahi가 `arm64eBaseSystem.dmg`로 복사해 `collect_firmware`에서 붙여 읽는 이미지, asahi-installer v0.9.2 `src/stub.py:356-364`, `:446-452`) |
| 가져온 곳 | `stub-135/dl/ipsw-13.5/UniversalMac_13.5_22G74_j274_stub-parts.ipsw` (11-15 부분 내려받기, 713,985,486바이트) |
| 오늘 새로 내려받은 양(바이트) | 0 |
| 꺼낸 dmg 크기(바이트) | 1,815,115,416 |
| 꺼낸 dmg sha256 | `12d11461f4a2726e5451da04cde0603ed9e4e3ba6ed138be0b9ad3c05a4ce4bb` (`parts.json` 기록과 같음) |
| 붙이기 | `hdiutil attach -readonly -nobrowse` → 복사 → 분리. 꺼낸 dmg는 지움 |
| 남긴 것 | `fw-wifi-135/fw/` 5개, `fw-wifi-135/SOURCE.txt`, `fw-wifi-135/SHA256SUMS.txt` |

복사 규칙은 `build-wifi.sh`와 같습니다: `atlantisb-X3.*`는 링크를 따라 실제 파일을 복사하고, NVRAM은 `P-atlantisb_M-RASP_V-{m,u}__m-*.txt`가 모두 같은지 확인한 뒤 첫 파일을 `P-atlantisb_M-RASP_V-{m,u}.txt`로 복사했습니다(13.5에서도 m 5개, u 5개가 모두 같았습니다).

## 2. 펌웨어 판 비교

결론: 펌웨어 본체·CLM·txcb가 다르고, NVRAM 두 개는 같습니다.

| 파일 | s258q (맥북 macOS 27.2.1) | s258r (macOS 13.5 22G74) |
|---|---|---|
| `atlantisb-X3.trx` 판 | 18.20.511.0.7.8.207 | 18.20.383.15.7.8.150 |
| `atlantisb-X3.trx` 빌드 날짜 | 2026-08-11 16:28:45 | 2023-05-13 07:25:54 |
| `atlantisb-X3.trx` FWID | 01-2606388a | 01-b37727a5 |
| `atlantisb-X3.trx` 크기(바이트) | 1,497,470 | 1,377,067 |
| `atlantisb-X3.clmb` 판 / 날짜 | CLM 1.72.1, ClmImport 1.74.1 / 2026-08-11 | CLM 1.68.6, ClmImport 1.58.6 / 2023-05-13 |
| `atlantisb-X3.clmb` 크기(바이트) | 34,061 | 33,493 |
| `atlantisb-X3.txcb` 크기(바이트) | 304 | 304 (17바이트 다름) |
| NVRAM m / u | 같음 | 같음 |

| 파일 | s258q sha256 | s258r sha256 |
|---|---|---|
| `atlantisb-X3.trx` | `7230e9ea5a3ac10c9ba5ea9093f678ae813b0423bbf1f37f7c38ca12eebb1e06` | `c1c8e9ac706099da73b3657ccc2c44d5f3e42505979e0328923cc28940b90091` |
| `atlantisb-X3.clmb` | `ecee6770918bfd6daf3faf67cb8cf549015acd24acf0f3d2a5d5a0d8dea791d5` | `322fbc347d7ee69d8db0bb8483c9e7e8ac711390b1f4dd951c12c5ec153f4ab8` |
| `atlantisb-X3.txcb` | `61c650d5481c69fe03ec80bad0419d372cd007d4aef1850c2d3f17db45bd48ec` | `6b517281b2b87a57bb9489c55fd25eec8ea9ec6ef451b1f3da0a3ed0834da112` |
| `P-atlantisb_M-RASP_V-m.txt` | `5e8c0449664fa47d57f220aa75dd04ad6d5558adf157ddf20f447a9f4f5fb19e` | 같음 |
| `P-atlantisb_M-RASP_V-u.txt` | `417a6014f4dad80d92f160748f43d2c50314ba67eefc15fa5eace378d82d943e` | 같음 |

- 펌웨어 판은 파일 안 `<FW-TAG>` 문자열에서 읽었습니다. 드라이버는 실제로 올린 펌웨어의 판을 `NwoaFwVersion`에 남기므로, 맥미니에서 이 값이 `version 18.20.383.15.7.8.150`이면 13.5 펌웨어가 올라간 것입니다.
- 13.5 펌웨어가 우리 드라이버의 초기화 순서(iovar 설정 등)를 모두 받아 주는지는 모릅니다 [가설]. 거절되는 설정이 있으면 `NwoaInitIovSt` 등 기존 기록에 드러납니다.

## 3. 꾸러미 [빌드만]

결론: 드라이버 파일은 s258q 그대로이고, 펌웨어 3개·INF 판·카탈로그만 다릅니다. 서명·INF 검사·카탈로그 포함 검사를 모두 통과했습니다.

| 항목 | 값 |
|---|---|
| 출력 | `port-j274/wifi/bcm4378-wificx/out/20261119-1056-s258r/package` |
| 만든 방법 | `bash recat-fw.sh out/20261119-1020-s258q/package <NWOAS>/fw-wifi-135/fw 1.0.1.9 s258r` → VM에서 `build/recat-wifi-fw.ps1` (컴파일 없음) |
| INF | `DriverVer = 11/19/2026,1.0.1.9` (s258q와 이 한 글자만 다름), infverif 통과, inf2cat 오류·경고 없음 |
| 서명 | `.sys`(s258q 서명 그대로)·`.cat` 모두 시험 인증서 `13338B38CE7EAB6A2CCF678DBD0BB554ACD9DBFF` |
| 카탈로그 포함 | 8개 파일 모두 "File is signed in catalog" |

| 파일 | 크기(바이트) | sha256 | s258q와 |
|---|---|---|---|
| `nwoa4378.sys` | 178,584 | `5d14651a62ae4a95de86c91e3d68bc65bf3dc1a0755bc29249c31c4e25facfcf` | 같음 |
| `nwoa4378.inf` | 3,120 | `f796521d4641a4cb0aa6f044f2a8f18dde245b3662eeee19212204cd6668e6c5` | 판 한 글자 |
| `nwoa4378.cat` | 5,413 | `01322a18fe65663246fd8cc951a763fd42f7e716c5ffe3e4db665e2e87e5b534` | 다시 만듦 |
| `atlantisb-X3.trx` / `.clmb` / `.txcb` | 1,377,067 / 33,493 / 304 | 2장 표 | 13.5 것 |
| NVRAM 2개·`nwoas-bcm4378-board.bin` | 7,784 / 7,723 / 2,124 | s258q와 같음 | 같음 |

| 전달·작업 파일 | 크기(바이트) | sha256 |
|---|---|---|
| `file-delivery-s243/manifests/wifi-j274-20261119-1056-s258r.json` (12개, 실제 파일과 대조 통과) | — | `0a36fcdee0de4e663cc2627dc5e0fdb849a9b854712ee987fb571aaa641a2090` |
| `file-delivery-s243/jobs/wifi-j274-20261119-1056-s258r.cmd` (드라이버 9개) | 3,620 | `ec93da820eaf800923eb30a0a5c3478f18774d8179797081e0129834c8171f3f` |
| `file-delivery-s243/jobs/wifi-j274-20261119-1056-s258r-scripts.cmd` (W17·W18·W19 ps1) | 2,020 | `ac2aa3d2be7112a72368010f86f45e0ad6ba21773d903a955e14cfc39563a89a` |
| `wifi-s258/jobs/W1-install-wifi-s258r.cmd` (제자리 설치, W9 아님. s258q 값 45개 + `FirmwareDir` 지움) | 2,023 | `f0631f60127aeaaa0f072a74658269c891cc8b1a0ef62ec510f691d7a9aaf0e1` |
| `wifi-s258/jobs/W17-etw-wlan-start-s258r.cmd` | 471 | `350ee4da8ce23ab5a823b66dac9b7a48ee086d9029bac98b72eb7275f4654611` |
| `wifi-s258/jobs/W18-etw-wlan-stop-s258r.cmd` | 439 | `8207ed8aad480f2f6ec439436de8c3062a2a57acd72b38e32a5ae14832730e87` |
| `wifi-s258/jobs/W19-ping-start-s258r.cmd` | 773 | `55a5a612caa3124f310e3d5fe525bd03dddf300620a94a7b7a3effb976bf4df3` |
| `wifi-s258/jobs/W30-base-on.cmd` (`TestConnScanSec=120`, 모양 0만) | 1,031 | `9e5657fecfba10d9a89fe7ca355263c5ae4b25da8c738243f338ba9c4c76ab3d` |
| `wifi-s258/jobs/W31-rollback-s258q-fw.cmd` (5장) | 1,742 | `43f956a145b77bd70f2ea0f2fc2ccc5e0216c7c8e523970e4723f8d129c454a4` |
| `wifi-s258/run-s258r.sh` | 11,413 | `17e2b7926b64963b89630627d2529f9df863c23617f8e581db069ed33c753fee` |

- `W1-install-wifi-s258r.cmd`가 `FirmwareDir`도 지우는 까닭: 드라이버는 이 값이 있으면 그 폴더의 펌웨어를 먼저 읽습니다(`driver/fw.cpp ReadFw`). 꾸러미 안 펌웨어가 확실히 쓰이게 하려는 것입니다.
- 판정 코드는 지난 s258q 실행 기록 사본에 13.5 판 줄을 붙여 맥북에서 돌려 보았고, `CHECK FW`·`CHECK RESULT` 줄이 나오는 것까지 확인했습니다 [사실]. 맥미니에서의 동작은 확인 전입니다.

## 4. 맥미니에서 돌리는 명령

결론: 지금 돌고 있는 s258q 시험이 끝난 뒤, 맥북에서 아래 하나를 실행합니다. 콘치님 손이 필요한 일은 없습니다.

| 순서 | 명령 (`nwoas_scripts`에서) | 하는 일 | 관찰 시간(초) |
|---|---|---|---|
| 1 | `bash wifi-s258/run-s258r.sh shapes <지난 .out> <지난 .link>` | Windows 다시 시작 → s258r 전달 → W1 설치 → 1.0.1.9 확인 → W2로 `NwoaFwVersion`이 18.20.383인지 확인(아니면 멈춤) → W29 모양 6가지 순환 → W17·W19 → 300초마다 W2 → W18·W19 정지·W15 → 판정 | 1800 |
| 2 (빠른 비교) | `bash wifi-s258/run-s258r.sh rebase <1의 .out> <1의 .link>` | s258r이 이미 깔린 Windows에 W30(모양 0만, 120초마다) → Windows 다시 시작 → 같은 관찰 | 900 |
| (1 없이 비교만) | `bash wifi-s258/run-s258r.sh base <지난 .out> <지난 .link>` | 전달·설치 후 W30 | 900 |

- 결과 파일: `wifi-s258/runs/s258r-<모드>-<시각>.txt`. 마지막 `CHECK TABLE` 줄을 s258q 결과의 같은 모양 줄과 나란히 비교합니다. 특히 q0 base 줄의 `alive`(펌웨어가 스스로 다시 읽음)와 `stuck`(멈춤) 수가 판을 가르는 값입니다.
- `CHECK RESULT`에 `fw=18.20.383` 조건을 더했고, base 모드는 검색 횟수 기준을 관찰 시간에 맞췄습니다(900초면 5회 이상).
- 13.5 펌웨어에서 멈춤이 사라지면 H2(펌웨어 빌드 차이)를 지지하는 근거가 되고, 그대로 멈추면 펌웨어 빌드는 원인에서 멀어집니다. 어느 쪽도 이번 한 번으로 단정하지 않습니다 [가설].

## 5. s258q 펌웨어로 되돌리기

결론: `W31-rollback-s258q-fw.cmd` 하나로 s258r 꾸러미만 지우고 s258q(1.0.1.8)로 돌아갑니다. W9는 쓰지 않습니다.

| 단계 | 하는 일 |
|---|---|
| 1 | 드라이버 저장소에 `nwoa4378.inf` 1.0.1.9가 정확히 1개, 1.0.1.8이 1개 이상 있는지 확인. 아니면 아무것도 하지 않고 멈춤 |
| 2 | 1.0.1.9 꾸러미(`oemNN.inf`)만 `pnputil /delete-driver oemNN.inf /uninstall` (`/force` 없음) |
| 3 | `pnputil /scan-devices` → 30초 뒤 장치 판 확인 |
| 4 | 판이 1.0.1.8이 아니고 `C:\NWOAS\drop\wifi-j274-20261119-1020-s258q\nwoa4378.inf`가 있으면 그것을 `pnputil /add-driver /install` |
| 5 | `NwoaFwVersion`이 `18.20.511.0.7.8.207`인지 출력 |

- 맥북에서: `python3 -I transport-s123/queue_job.py <링크> wifi-s258/jobs/W31-rollback-s258q-fw.cmd` (실행 스크립트의 `job` 함수와 같은 방식).
- 2단계에서 Windows가 남은 꾸러미 중 가장 나은 것(1.0.1.8)을 장치에 다시 붙인다는 것은 Microsoft 문서의 동작 설명을 따른 것이고, 이 맥미니에서 해 본 적은 없습니다 [가설]. 그래서 4단계를 붙였습니다.
- 되돌리기 대신 다음 드라이버 빌드(1.0.1.10 이상, `build-wifi.sh`가 맥북 27.2.1 펌웨어를 넣음)를 설치해도 18.20.511로 돌아갑니다.

## 6. 남긴 것·지운 것

| 항목 | 상태 |
|---|---|
| 꺼낸 13.5 복구 이미지(1,815,115,416바이트) | 지움 |
| 붙였던 이미지 | 분리함 (`hdiutil info`에 남은 것 없음) |
| `stub-135/dl/ipsw-13.5/` 원래 묶음 | 건드리지 않음 |
| `fw-wifi-135/fw/` 펌웨어 5개(약 1.4 MB)와 기록 2개 | 남김 (Apple 파일이라 공개 저장소에 넣지 않음) |
| 기존 `out/` 꾸러미(s258q 등) | 건드리지 않음 |
| 공유 폴더 `/Volumes/media/nwoas/wifi-build/out/20261119-1056-s258r` | 기존 빌드와 같이 남김 |
