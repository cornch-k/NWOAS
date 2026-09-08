# NWOAS 2026-09-09 현재 작업 (S123–S125)

## 최신 상태 — 사용자 장비 세팅 후 S129 SSD 부팅 재개

**최신 사용자 지시: 장비 세팅 완료 후 계속 진행 요청. X31 재연결 확인 후 동일 S129 부팅 재개. 강제 종료/강제 재시작은 하지 않으며, 기존 전원 유지 지시를 존중한다.**

- Mac mini J274 M1/Tahoe 유지. MacBook/X31 데이터 삭제 금지. WINARM2 USB는 맥미니에 그대로 있음.
- 활성 하네스 usb-s129-guest-test.sh, exec session42631, run_guest PID67002 (다음 작업 전 다시 확인).
- 현재 로그 nwoas_scripts/logs/usb-s129-20260909-014942.NGKlYl, 통신 같은 경로 .link/. 이전 OOBE 확인 로그는 005634.MuX4Ce, 최초 사용자 공간 연결은 003718.Mtc34K.
- S124 DISM 이미지 적용 100% exit0 및 flush PASS. 이후 전원 재가동 후 코어 파일/완료 표시/edition 조회 성공. 커널과 winload 해시는 host iw1/bw2tree와 정확히 일치.
- S126 RAM FAT 파티션 쓰기 지원 이후 여러 작업 완료 ACK 및 정상 원격 재부팅/자동 연결 PASS. 이전 긴 DISM 종료 지연의 근본 원인은 확정하지 않음.
- S128에서 UEFI NVMe 실제 I/O/GPT/ESP 읽기 성공. BCDBOOT exit0; ESP/NTFS flush PASS. 부팅 관리자 원본/두 ESP 사본의 전체 hash 일치.
- S129는 S128과 동일하되 임시 펌웨어의 기본 부팅 순서를 Internal Storage → USB로 변경. 첫 부팅에서 NWOS 사용자 공간 handshake 성공, OS 자체 재시작 후 **사용자가 초기 설정 화면(OOBE) 도달을 확인**. 데스크톱/전체 드라이버/벤치마크는 미검증.
- native-link-s125/NWOS.EXE와 specialize launcher를 SSD에 배치/hash검증/flush 완료. 첫 네이티브 세션이면 events.jsonl kind4 메시지가 **WinOS worker connected**여야 함. WinPE worker connected면 USB 설치 환경으로 돌아간 것이므로 설치 OS 성공 아님.
- 다음: WinOS 연결이면 native-link-s125/01-native-state.cmd로 SystemDrive C / MiniNT absent / OS build / setup상태 확인. 연결 없으면 로그로 loader/kernel/오류 구간부터 확인. 미완료 작업 덮어쓰기/임의 완료event 삽입 금지.
- S128은 USB 우선 복구 하네스로 보존. 모든 변경은 임시 chainload; 영구 kmutil boot object 변경 없음.

## 이번에 확인한 개선

S123: host RAM 전용 namespace2(FAT16 가상 드라이브+메일박스), Windows ARM64 NWAGENT.EXE. 맥북→CMD 전달, 출력/exit code 회수, 정상 재부팅→자동재연결까지 실기 PASS. USB 이동/사용자 명령 입력 없이 반복한 상태.

USB autounattend.xml Order7은 가상 드라이브의 NWAGENT를 검색/실행. 기존 파일 AUTOUNATTEND.PRE123.XML 백업. XML SHA256 e322d3b13f197bdf70c9a3e199a804684a07b6b750515b55fa2d699048f17a4c. S122.RUNNING은 남아 있으나 옛 설치 스크립트 자동 실행 경로는 제거됨. S100.TAG 절대 재무장하지 말 것.

S124: 기존 NVMe 중계의 VWC0/FUA무시/종료즉시완료 불일치 수정 시험. 별도 nvme-s124/에서 VWC1, Feature6 기본enabled, FUA/cacheoff write 후 physicalflush, cacheoff 전 flush, 정상종료 processing→flush→complete, 오류시 완료거짓응답 금지. GPT쓰기는 전부 거부, 기존 Windows zone만 허용. S122 매쓰기 flush 실험은 사용 안 함. 37 host tests PASS.

실기 PASS:
- 1MiB+3B 일반/비버퍼링 복사 두 개, 재부팅 전후 direct/buffered hash 모두 일치.
- 16MiB+123B SHAKE256 고유페이지 벡터 두 복사본, 재부팅 전후 8회 hash 일치.
- 실제 install2.swm /J재복사 + exact EOF/file flush/volume flush, **재부팅 뒤 564691107B 및 전체 hash 일치**. 기존 재부팅 후 rounded EOF/손상 증상은 이 경로에서 재현되지 않음.
- install.swm 3987720636B SHA8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c 확인.
- install2.swm SHA62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963 확인.
- C:실패설치 A/Windows/Program Files(3)/ProgramData/Users/Recovery/PerfLogs 및 old t1,t2 정리 후 free20013035520B. S117SRC 원본/시험벡터 보존. Apple 파티션/GPT 불변.

## 다음 작업

1. 현재 DISM job4 완료/exit0 및 kernel/loader/SYSTEM hive/flush를 확인. 실패하면 nvme-s124/10-apply-diagnostics.cmd로 X:S124-DISM.LOG 오류 회수. **작업 완료 전 새 job 큐잉/재부팅하지 말 것.**
2. 성공하면 현재 설치 결과 확인 후 정상재부팅으로 보존 확인. 통신 큐 도구:
   `python3 nwoas_scripts/transport-s123/queue_job.py SESSION_LINK SCRIPT.cmd`
   이전 job가 완료돼야 다음 job허용. reboot 스크립트는 실행중 disconnect로 완료event가 없을 수 있으며, CDC복귀/기존PID해제 확인 후 새하네스로 새세션 생성.
3. UEFI에서 내부 SSD BlockIO가 필요. S125 후보 빌드만 완료, **아직 하드웨어 미실행**:
   `m1n1_windows/m1n1-payload-s125-uefinvme.bin`,32342016B,SHA bf00b5a4bc9ed53abf7e512854ca5408e0352ead16f9511626937aaf1e9ab43a.
   S102 prefix + 현재FD를 NWOAS_UEFI_NVME=1로 빌드. 소스는 원래내용으로 byte-for-byte 복원. NvmExpressDxe + NonDiscoverable NVMe registration. `uefi-s125/build_candidate.py`,manifest.json,build.log.
   `usb-s125-guest-test.sh`: S103 HV + S125 UEFI + S124 backend + S123 통신. 설치가 끝난 뒤 별도시험할 것. baseline S124/S102 하네스 보존.
4. uefi-s125/01-inspect-esp.cmd 준비만 됨: disk GUID유일일치/partition3 확인 후 S:할당, NWESP guard. 포맷/bcdboot 안 함. NWESP.EXE ARM64 빌드+15 API stub tests PASS; 실기 미검증.
5. 부팅파일 생성/bcdboot 및 installed Windows 첫부팅은 아직 미작업. native-link-s125/NWOS.EXE는 SystemDrive C:용 수신기 후보(빌드/stub tests만PASS). S125 가상드라이브에 포함하나 현재OS에 아직 배치/자동시작 설정 안 함.

## 보호할 디스크 범위/실기 주의

mini GPT GUID898f172d-77bf-4007-b865-ac81a141eb3c, 251000193024B,4KiB LBA.
ISC6–128005/APFS128006–53838942/Recovery59968630–61279338 보존.
ESP53839104–53915903(300MiB),MSR53915904–53919999,Windows53920000–59968511.
Windows NTFS serial1CEA-E590,labelWindows,total24774701056B,partition5,offset220856320000,length24774705152.
ESP partition3 offset220524969984,length314572800,EFI GPTtype.
현재 C:Windows,D:NWOASLINK,E:WINARM2(serialB585-170F). 다음부팅에서도 도구/guard로 확인.

S103 HV SHA fe21c195bc4b601f6bd8e95bde74f4a83b5de7c8b040ca3caa5f210ef12e280a. S102 UEFI/EXCLUDE_WINDOW=1 유지. HIDE_FULL S104 사용 금지.
영구 bootstrap에서 nvme_init 금지: 항상 S103 chainload 먼저. SIGINT 금지, 중단필요시 검증한 run_guest에 SIGTERM. .env 비밀번호 출력/커밋 금지.
S124 첫 부팅 한 회는 Windows 이전 UEFI PCffffffffffffffff 예외, 원격복귀 후 재시도 성공. 이후 사용자 전원/USB조작 없이 여러 재부팅 성공. 대규모 전력차단 안정성을 증명한 것은 아님.

## S124 이미지 적용 성공 (00:xx KST)
job4 DISM100%, 'The operation completed successfully.', exit0. ntoskrnl/winload/SYSTEM hive존재검사통과후NWVFLUSH volume flushPASS. 최종 'S124 IMAGE APPLY PASS - INSTALLED OS BOOT UNTESTED'. free9193902080B(9.19GB). 현재firstboot는미검증. 다음postapplyfingerprint/부팅설정조회후S125UEFI가시화시험예정.

## S126 통신 RAM FAT 수정 및 복구 시도 (00:20 KST)

- 이전 세션은 DISM exit0와 volume flush PASS 후 kind3 완료 응답이 멈춤. NS2 root directory LBA265 쓰기 거부가 반복됨. FAT 지연 메타데이터 쓰기가 raw mailbox I/O를 지연시킨다는 가설이며 인과관계 확정 아님.
- transport-s123/transport.py: namespace2 FAT partition LBA256..8447 쓰기를 RAM bytearray에만 반영. MBR/gap/메일박스 중첩 쓰기 거부 유지. 실제 SSD backend 참조 없음. host tests 11 PASS; 실기 아직 미검증.
- 검증한 기존 PID34986에 SIGTERM, 새 단일 UART owner NOP 성공, p.reboot() 호출 성공. 사용자 USB 이동 없이 원격 재부팅 시도.
- 새 S124/S102 기준 UEFI 하네스: logs/usb-s124-20260909-002010.njaNPt, exec session86615. 현재 chainload 응답 대기; 아직 연결/설치 보존 성공 아님.

- 00:22 KST: 원격 p.reboot 이후 CDC 장치는 남았으나 NOP timeout. chainload PID44683은 초기 응답 대기에서 SIGTERM으로 종료(이미지 전송 전). macvdmtool reboot 한 번도 VDM 응답 없음. 사용자에게 USB 이동 없이 맥미니 전원 껐다 켜기 요청; 대기 중. 현재 활성 guest/chainload 없음. 다음 하네스는 10초 bounded NOP 사전 검사 후에만 chainload 진행하도록 보강.

## S126 재부팅 후 설치 조회 PASS (00:25 KST)

사용자 전원 재가동 후 S124/S102 + S126 RAM FAT 수정으로 logs/usb-s124-20260909-002259.I8WlXy, session39383 자동연결. job1 12-after-reboot.cmd exit0 완료 event 회수. C:정확한 partition/serial 확인. APPLY-PASS marker 보존, kernel/loader/SYSTEM 존재. DISM offline edition 조회 성공 (이미지10.0.22621.525). kernel10162000B SHA7c357da8f62a93bd71e2f4c79eb8cedb396f61d78144def95c2eb4db435904b0, loader2102720B SHA5990158f0165f65cb6af14ec4750855ecb833b65d8cdd2e97716cb634b6ea78a. 비교 원본 해시 없이 기록용 sentinel mismatch를 사용했으므로 전체 OS 무결성 검증이라고 과장하지 말 것. S126에서 작업 완료 ACK까지 정상 회수. 직전 긴 DISM과 동일 부하는 아직 재현하지 않았으므로 프리징 원인 확정 아님. job2 wpeutil reboot 큐잉, 다음 S125 UEFI NVMe 노출 실기 시험 예정.

## S125 실기 / S127 후보 (00:30 KST)

S125 logs/usb-s125-20260909-002525.QQCLhm session87404 WinPE 연결 및 ESP 조회 PASS. job1 ESP FAT32 serial88754FEF,total310378496,free310374400,part3offset220524969984,len314572800,EFI GPTtype 모두 PASS. 비어 있음. 부팅파일 아직 안 씀. job2 USB marker 이름 오타로 exit20 무변경; 수정 job3 미디어 BCD 조회 exit0. PE BCD는 기본 detecthal yes, WinPE yes; debug/testsign/nointegrity/numproc 특별 설정 없음.

S125 UEFI NVMe 등록 Success이나 첫 MMIO write는 EBS 이후 Windows에서만 나옴. 장치 등록만으로 UEFI 드라이버가 연결되지 않음. S127은 DeviceBootManagerBeforeConsole에서 NVMe GUID인 비발견 장치만 recursive ConnectController 후 원래 부팅 옵션 등록 실행하도록 추가한 후보. 빌드 성공, 원본 3파일 byte-for-byte 복원. payload m1n1-payload-s127-uefinvme-connect.bin,32342016B SHA21a975e3961f7831146f72b96efb78873192654a00e774f9651a4f911d7babc0. usb-s127-guest-test.sh 준비, 실기 미검증. S125 job4 정상 재부팅 요청.

추가: iw1/ntoskrnl.exe와iw1/winload.efi의 host SHA가 재부팅 후 SSD에서 읽은 두 해시와 정확히 일치. 저장된 코어 2파일 비교 확인.

## S128 UEFI 저장소 경로 PASS / SSD 첫 부팅 준비 완료 (00:37 KST)

S127 connect Success이나 I/O 없음: Mu NonDiscoverable PCI VendorId=ffff, NvmeControllerInit이 VIDffff를 장치 부재로 반환하는 코드 발견. S128은 NVMe type+BAR700100000만 VID1234/DID0010(기존 emulator와 일치) 지정. payload feb51551eb4653c7bd6cb1f5d80370418f663d80012842e3e84d9d289aa23000. 실기 logs/usb-s128-20260909-003243.dPSbr1 session80459에서 EBS 이전 admin/IO queue depth2, Identify/read 성공 및 GPT/ESP LBA53839104 읽기 확인. Windows 재연결 PASS.

- job1 ESP mount/guard PASS. job2 native-link-s125/stage-native-worker.cmd PASS: C:Windows/System32/NWOS.EXE SHAda2f0072d1159cdc866851781116909824eaafcbe7548f2ef0424c8ba1b78df8, Panther/unattend.xml SHA4dc55f43056bc8b47194a72cbb61bd1072e557e2b1f2883d87fee2cb4f28de2f, Setup/Scripts/NWOAS127.cmd SHA340096de83c400f4b8462883f51aacde8c8e2cac8c9caa3711efa0e5d45c3494. 전체 hash+flushPASS. XML specialize RunSynchronous launches short CMD that start /b NWOS and returns. User/account/productkey/disk layout settings not added. Autostart native still untested.
- job3 bcdboot C:Windows /s S: /f UEFI /v exit0. bootmgfw,BCD,BOOTAA64 verified present; ESP+NTFSflushPASS.
- job4 fingerprint comparison C:Windows/Boot/EFI/bootmgfw.efi = S:EFI/Microsoft/Boot/bootmgfw.efi = S:EFI/Boot/BOOTAA64.EFI; all1790304B SHA b219c6648564ead2c3ebd3ddf9ba34a2d5f3815996074cd660dabe70a631b303. BCD default device/osdevice partitionC:,winload.efi,Windows11. Offline SYSTEM Setup CmdLine=oobe\windeploy.exe,SetupType1,SystemSetupInProgress1. Hive unloaded+flushPASS.
- job5정상 reboot requested. Next S129 candidate registers Internal Storage before USB (S128 recovery candidate keepsUSBfirst). payload m1n1-payload-s129-ssd-first.bin,32342016B,SHAf317a251a193c804a4ef6a0e7c142885c9365eebcc8200157083618b45f6273a. Build succeeded,original5sources restored. No permanent boot object changes. Installed OS firstboot still untested.

## S129 초기 실행 관측 (00:40 KST)

S129 logs/usb-s129-20260909-003718.Mtc34K PID50515 session19628. UEFI SSD-first 마커, SSD 경로 reads 증가, EBS 이후 Windows kernel주소 실행/USB초기화/실제 SSD writes+flush 진행. 사용자 화면은 검은 배경에 하얀 회전 표시. 00:40 기준 fatal exception/bugcheck 없음; NWOS 연결은 아직 없음. 이 단계만으로 OOBE/데스크톱 성공이라고 결론 내리지 말 것. 입출력이 진행 중이므로 강제 재부팅하지 않고 관찰 중.

## 오버나이트 관찰 설정

사용자 과거 오버나이트 요청과 최신 전원 유지 지시에 따라 현재 대화 heartbeat 자동화 NWOAS 첫 부팅 관찰(id nwoas), 5분 간격 ACTIVE 생성 완료. 전원/재시작/프로세스 종료/새 하네스 실행 금지. 기존 로그와 events.jsonl 읽기 관찰, WinOS 최초 연결시 미완료 작업이 없는 경우에만 native-link-s125/01-native-state.cmd 한 번으로 읽기 진단. 변화 없으면 알림 없이 관찰 기록 갱신, 의미 있는 진전/치명적 오류/필수 사용자 조작만 알림.

## S129 installed Windows 사용자 공간 연결 확인 (2026-09-09T00:55:40)

사용자가 커맨드 프롬프트 하나가 뜬 뒤 다시 시작하는 중 화면을 보고함. events.jsonl에 job0 kind4 exit0 `WinOS worker connected` 확인. SSD에 배치한 C:SystemDrive 전용 NWOS가 실제 실행/가상 디스크 식별/왕복 handshake에 성공했음. 설치된 Windows 사용자 공간 도달 증거이며 OOBE/데스크톱 완료나 완전한 네이티브 드라이버/성능의 증거는 아님. 사용자 보고 전/이후 agent가 재시작 명령을 보낸 적 없음. job1 native-link-s125/01-native-state.cmd 읽기 진단 큐잉. 현재 응답 대기, 중복 큐잉 금지. run_guest PID50515는 아직 활성이고 SSD I/O 지속. 사용자 전원 유지 지시 준수.

## Windows 자체 재시작 후 두 번째 SSD 부팅 (00:56 KST)

이전 run_guest PID50515는 USB Device not configured로 자연 종료됨. 종료 명령/재부팅 명령을 보내지 않음. job1 읽기 진단은 연결 종료 이전 실행 응답을 받지 못함; 완료로 취급하지 말 것. Windows 자체 재시작 후 다음 부팅을 이어가기 위해 동일 usb-s129-guest-test.sh 실행: logs/usb-s129-20260909-005634.MuX4Ce, exec session78604. 현재 재부팅은 사용자 금지한 강제 전원 조작이 아니라 OS 자체 초기 설정 재시작임. NWOS는 specialize에만 배치한 것으로 다음 OOBE 부팅에서 자동 재연결이 보장되지 않음. OOBE/데스크톱 미확인.

- 관찰 2026-09-09T01:03:23+09:00: 두 번째 SSD 부팅 PID57070 활성, 로그 즉시 갱신/읽기·flush 지속. 새 NWOS 연결 없음, 로그 fatal marker 0개. 전원·재시작·UART·작업 큐 조작 없음. 변화 알림 생략.

## S129 OOBE 도달 — 사용자 화면 확인 (2026-09-09T01:07:24+09:00)

콘치님이 Windows 첫 설치 시 나타나는 초기 설정 화면이 떴다고 직접 보고함. S129 첫 SSD 부팅 NWOS handshake → Windows 자체 재시작 → 동일 S129 두 번째 SSD 부팅 → 사용자 OOBE 확인 순서. 설치된 Windows가 초기 설정 단계에 도달한 실기 증거. 사용자는 새 포터블 모니터와 Thunderbolt 독 배송도 언급했으나 연결/교체를 요청하지 않았으며 장치 연결 변경 안 함. 현재 전원 유지 지시 계속 적용. OOBE 입력, 네트워크, 사용자 계정, 데스크톱, GPU/전체 USB-C/Thunderbolt 독 호환성, 모든 코어, 성능 목표는 아직 확인되지 않음.

- 2026-09-09T01:19:04+09:00: 사용자 장비 세팅/종료 의사를 반영해 heartbeat nwoas를 PAUSED로 변경. Mac mini에 명령이나 UART 접속 없음. OOBE 도달 기록 보존, 사용자 재개 대기.

## 사용자 세팅 후 재개 (2026-09-09T01:50:13+09:00)

X31이 처음에는 USB/디스크 목록 모두에 없었으나 사용자 재연결 후 /Volumes/X31 정상 접근, external physical disk6 1TB APFS 확인. 기존 guest/chainload 없음. 동일 usb-s129-guest-test.sh 실행, bounded proxy readiness 및 S103 chainload 후 run_guest PID67002/session42631 실행. 로그014942.NGKlYl. 새 모니터/독 호환성은 이번 실행으로 확인할 예정. 사용자에게 전원/USB 재이동 요청 안 함.

- 재개 후 HDMI HPD stable, DCP1280x720@60 mode/swap ACK 확인. S129 SSD-first 등록 확인. heartbeat nwoas ACTIVE 재개 완료. 새 독의 입력장치/Thunderbolt 기능 및 이번 OOBE 재진입은 아직 미확인.

## Windows 11 OOBE 완료 및 데스크톱 도달 (2026-09-09 02:45 KST)

콘치님이 OOBE 네트워크 단계에서 `OOBE\\BYPASSNRO`를 실행한 뒤 로컬 초기 설정을 완료했고, Windows 11 데스크톱과 작업 관리자가 정상 표시되는 것을 사진으로 확인했다. 설치된 Windows가 내부 SSD에서 사용자 데스크톱까지 부팅한 첫 확정 실기 성공이다.

작업 관리자 표시 기준 현재 성능 제약은 CPU 소켓 1, 코어 1, 논리 프로세서 1, 속도 0.04GHz, 메모리 4.1GB 인식이다. CPU 사용률은 100%였다. 이 표시는 클럭 산정 오류 가능성도 있으므로 실제 40MHz 실행으로 단정하지 않는다. 다음 세션은 현재 성공 기준을 보존한 채 CPU/메모리 노출 및 저장장치 중계 병목 계측과 개선을 먼저 진행하고, 이후 USB-C 재연결과 네트워크 등 장치 드라이버를 진행한다.

사용자 취침으로 작업 중단. 5분 heartbeat는 PAUSED 상태를 유지한다. 종료할 경우 Windows 메뉴에서 정상 종료하며 강제 전원 차단은 피한다.

- 관찰 2026-09-09T01:56:28+09:00: 재개 부팅 PID67002 활성. SSD 읽기·쓰기·flush와 로그 갱신 지속, 전체 현재 로그 fatal marker 없음, 새 NWOS 연결 없음. 강제 조작/별도 UART 접속/작업 큐 제출 없이 유지. 변화 알림 생략.

- 2026-09-09T02:00:09+09:00: 사용자 요청으로 5분 관찰 자동화 nwoas PAUSED. 실행 중인 Mac mini/guest 프로세스는 중단하지 않음.

## USB-C 재연결 실패 사용자 보고 — 2026-09-09T02:01:41+09:00

트랙패드 USB-C 분리/재연결 후 입력 복구 실패. 현재014942 로그에 CCS lost 및 PHY bringup 16회 시도 기록 있음. 해당 시점 cmd=0x2004(Run/Stop bit0=0). 이후 최근 USB bridge 200표본 모두 usbsts=0x1d,portsc=0x206e1,iman=2,pend=0. 포트 연결 비트는 복귀했으나 컨트롤러 HCHalted bit0가 남아 있음. 물리 연결 복귀와 Windows xHCI 재가동/재열거 구분 필요. 근본 원인 미확정; 게스트 소유 컨트롤러 강제 재시작이나 실행중 HV 수정 안 함. SSD I/O 지속. 기존 PHY 재시도는 자연 분리와 부팅 reset을 구분하지 않는 코드 경로여서 다음 후보에서 조사 필요.

## OOBE 오프라인 설정 명령 후 재부팅 — 2026-09-09T02:13:31+09:00
사용자 OOBE\BYPASSNRO 실행 보고. 기존 PID67002 자연 종료, Device not configured 확인. 동일 S129 하네스 한 번 재개: logs/usb-s129-20260909-021315.QCTzXY, exec session94705. readiness/chainload 통과 후 guest 시작 단계. 5분 자동 관찰은 PAUSED 유지. 오프라인 선택지 및 계정 생성 아직 미확인.
