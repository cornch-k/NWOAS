# S123 USB 이동 없이 작업/로그 전달

현재 단계: **Windows 실기 연결 및 작업/로그 왕복 성공.** 첫 상태 조회와 USB 자동 시작 설정 작업 모두 exit 0. **맥북에서 요청한 재부팅 → 사용자 조작 없는 자동 연결 → 새 작업 실행/출력 회수도 성공.**

- 실행: `bash nwoas_scripts/usb-s123-guest-test.sh`
- S103 m1n1 / S102 UEFI / D81 USB 유지. S122 매 쓰기 flush 실험은 사용하지 않음.
- namespace 1은 기존 맥미니 SSD를 **읽기 전용**으로 노출. 설치/삭제 불가.
- namespace 2는 맥북 Python 메모리에 생성한 33 MiB 가상 디스크. 실물 디스크 함수 참조가 없음.
- MBR/FAT16(4 KiB sector)에 `NWAGENT.EXE`, `START.CMD`, 읽기 진단 도구를 제공.
- 파티션 외부 LBA 128은 가상 메일박스. 실제 SSD의 LBA 128과 무관.
- Windows 클라이언트는 WinPE X: 환경, 정확한 가상 디스크 크기, MBR 서명, 프로토콜 식별자 및 세션 토큰 확인 후에만 가상 포트에 씀. 4 KiB 단위, 64 KiB 정렬된 버퍼, NO_BUFFERING 사용.
- CMD 작업 최대 4032 바이트, CRC32, 세션 토큰, 순번 ACK. 출력은 파이프로 받아 실행 중 맥북 로그에 전송. 완료 exit code 별도 전송.
- 한 세션에 클라이언트 한 번만 연결. 같은 작업의 암묵적 재실행/미완료 작업 덮어쓰기 금지. 연결 실패 시 중단.
- 첫 실행은 WinPE 명령창에서 `for %d in (C D E F G H I J) do @if exist %d:\NWAGENT.EXE %d:\NWAGENT.EXE`.
- USB `autounattend.xml` Order 7을 가상 드라이브 NWAGENT 자동 검색/실행으로 변경 완료. 기존 설정 `AUTOUNATTEND.PRE123.XML` 백업. XML 1774 bytes SHA256 `e322d3b13f197bdf70c9a3e199a804684a07b6b750515b55fa2d699048f17a4c` RAM/USB 임시본/최종본 모두 일치. 재부팅 후 자동 실행까지 확인 완료.

세션 경로는 `logs/usb-s123-날짜.랜덤.link/`. `events.jsonl` kind4가 연결, kind3가 완료. `job-N.log`가 작업 출력.

```sh
python3 nwoas_scripts/transport-s123/queue_job.py SESSION_DIR nwoas_scripts/transport-s123/01-inspect.cmd
```

검사: Python namespace/프로토콜 9개 통과. 실제 C 코드의 Windows API stub 검사(잘못된 디스크 크기/서명/메일박스에서 쓰기 0회, 정상 handshake, ACK 실패 중단, CRC32 기준값) 통과. `/sbin/fsck_msdos -n` FAT 구조 검사 통과. ARM64 PE 빌드 및 KERNEL32 import 확인.

이 기능은 설치 성공이나 네이티브 SSD 드라이버 완성을 뜻하지 않는다. S122 프리징과 재부팅 후 install2 변경 문제는 별도 미해결 상태다.

API 근거: [비버퍼링 정렬](https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering), [자식 프로세스 출력 전달](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output).

실기 증거: 첫 세션 `logs/usb-s123-20260908-231027.SjoPCG.link/`, 재부팅 후 `logs/usb-s123-20260908-231513.PkriJv.link/`. 두 번째 세션도 kind4 연결 및 job1 exit0. 현재 실행 session45647 / PID29963. NWAGENT SHA256 `c316a5780ef4eb2815677dd0d20c63e227b17566a3fc263defc4c7d78ac61f84`.

## S124/S126 후속 변경

S124 하네스는 namespace1 Windows 시험 영역 쓰기를 허용하며 별도 VWC/FUA/flush 처리를 사용한다. 기존 S123 읽기 전용 하네스와 구분할 것. S124에서 설치 원본 재부팅 hash 검증과 DISM Apply-Image exit0 및 volume flush PASS를 확인했다. installed Windows 첫 부팅은 아직 검증하지 않았다.

S126은 namespace2 FAT 파티션의 메타데이터/일반 쓰기를 RAM에만 반영한다. 기존 root directory 쓰기 거부와 작업 완료 응답 지연의 연관성을 시험하기 위한 변경이다. MBR/gap 및 메일박스에 걸친 쓰기 거부, 메일박스 토큰/CRC/순번 검증은 유지된다. host tests 11개 통과. 실기 검증 대기. 이 RAM 드라이브의 변경은 다음 세션에 보존되지 않는다.
