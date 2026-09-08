# S124 Windows SSD 캐시 계약과 재부팅 보존 시험

- S103/S102 바이너리 유지. S93 backend를 별도 복사, S122 매 쓰기 flush 정책 미사용.
- VWC=1, Feature 6 기본 enabled. 일반 쓰기는 캐시 허용, FUA 또는 캐시 disabled이면 물리 flush 후 성공.
- 캐시 비활성화 전 flush. 실패하면 enabled 유지. 정상 종료 processing → flush → complete.
- GPT 쓰기 금지. 기존 Windows 영역만 쓰기 허용. S123 메모리 통신 드라이브로 명령/로그/재부팅 진행.
- 37 tests PASS. 실기 검증 중이며 채택/설치 성공 아님.
- 1 MiB+3 바이트 파일을 일반/비버퍼링 두 방식으로 복사하고 각각 buffered/direct 해시 검증 및 볼륨 flush 통과. 재부팅 후 두 파일 4개 hash 검사도 PASS.
- 16 MiB+123 바이트 SHAKE256 벡터는 두 복사 방식의 재부팅 전후 8개 hash 검사 PASS. 페이지별 내용이 달라 이전 페이지 대체도 검출. 1 MiB 벡터는 반복 내용이라 이러한 대체 검출에는 한계가 있음.
- `01-write-probe.cmd` / `02-verify-probe.cmd` : 작은 파일
- `03-write-unique.cmd` / `04-verify-unique.cmd` : 페이지별 내용이 다른 파일
- 쓰기 스크립트는 이미 시험 폴더가 있으면 재실행 중단. NWGUARD로 대상 NTFS 식별 후 새 시험 파일만 생성. 설치 파일/기존 실패 설치 삭제 안 함.

기준: https://nvmexpress.org/wp-content/uploads/NVM-Express-1_3c-2018.05.24-Ratified.pdf Feature 06h / VWC / FUA / Flush.

실제 install2.swm(564691107B) /J 재복사, prefix/wholefile SHA256 및 file/volume flush PASS. 그 파일의 재부팅 후 검증 중. 설치/삭제 스크립트07–09는 준비했지만 아직 실행 전.

실기 추가 결과: install2 재부팅후exactlength/fullhashPASS, 두SWM전체hashPASS, 실패설치정리후20.013GB확보. **DISM Index2 CheckIntegrity Verify 100% / exit0 / kernel, loader, SYSTEM hive 존재 / volume flush PASS.** 결과log usb-s124-20260908-233303.q6Xu3y.link/job-4.log. installed OS firstboot는아직미검증.
