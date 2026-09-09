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

## S130 저장장치 직렬 로그 병목 개선 (2026-09-09 10:19 KST)

S129 데스크톱 로그는 102,912 I/O doorbell에서 누적 `host_ms/db=3.7`, `guest_ms/db=15.4`, 약 52 IOPS였고 성공한 I/O 하나마다 CMD 및 여러 MMIO 줄을 115200 직렬 콘솔에 출력했다. 요청당 출력량으로 계산한 직렬 처리 상한이 관측 IOPS와 같은 규모이므로 첫 병목으로 확정했다.

`nvme-s130/quiet_log.py`와 `usb-s130-guest-test.sh`를 추가했다. S124 저장 의미론, GPT/Windows-zone 쓰기 가드, S126 전달 채널, S129 UEFI payload는 그대로 두고 성공 CMD와 routine MMIO만 샘플링한다. 오류·CFS·큐 생성·컨트롤러 상태 변경은 전부 보존한다. 새 샘플링 테스트 5개와 기존 NVMe 테스트 37개 PASS, shell/py_compile PASS.

첫 S130 실기 PID62883, 로그 `logs/usb-s130-20260909-101930.omjAtK`, session43943. 첫 실행에 남아 있던 요청별 INTMS/INTMC 두 줄 출력 상태에서도 초기 누적 지연이 약 19.1ms/I/O에서 5.5ms/I/O로 감소해 약 3.5배 개선, 이후 약 5.5ms/I/O 수준을 유지했다. 소스는 INTMS/INTMC도 샘플링하도록 추가 보완했고 테스트 PASS했으나 현재 실행은 시작 시 로드된 이전 함수이므로 최종 정책 실기 결과는 다음 정상 재부팅에서 확인해야 한다. 현재 실행은 중단하지 않았고 fatal marker 없음. 메모리 4.1GB는 OS-facing GetMemoryMap에서 4GB 이상 Conventional RAM을 숨기는 USB DMA 우회와 직접 연결됨. CPU 1개는 MADT가 AP1..7을 명시적으로 disabled한 결과이며, 두 제한은 아직 변경하지 않음.

- 관찰 2026-09-09T01:56:28+09:00: 재개 부팅 PID67002 활성. SSD 읽기·쓰기·flush와 로그 갱신 지속, 전체 현재 로그 fatal marker 없음, 새 NWOS 연결 없음. 강제 조작/별도 UART 접속/작업 큐 제출 없이 유지. 변화 알림 생략.

- 2026-09-09T02:00:09+09:00: 사용자 요청으로 5분 관찰 자동화 nwoas PAUSED. 실행 중인 Mac mini/guest 프로세스는 중단하지 않음.

## USB-C 재연결 실패 사용자 보고 — 2026-09-09T02:01:41+09:00

트랙패드 USB-C 분리/재연결 후 입력 복구 실패. 현재014942 로그에 CCS lost 및 PHY bringup 16회 시도 기록 있음. 해당 시점 cmd=0x2004(Run/Stop bit0=0). 이후 최근 USB bridge 200표본 모두 usbsts=0x1d,portsc=0x206e1,iman=2,pend=0. 포트 연결 비트는 복귀했으나 컨트롤러 HCHalted bit0가 남아 있음. 물리 연결 복귀와 Windows xHCI 재가동/재열거 구분 필요. 근본 원인 미확정; 게스트 소유 컨트롤러 강제 재시작이나 실행중 HV 수정 안 함. SSD I/O 지속. 기존 PHY 재시도는 자연 분리와 부팅 reset을 구분하지 않는 코드 경로여서 다음 후보에서 조사 필요.

## OOBE 오프라인 설정 명령 후 재부팅 — 2026-09-09T02:13:31+09:00
사용자 OOBE\BYPASSNRO 실행 보고. 기존 PID67002 자연 종료, Device not configured 확인. 동일 S129 하네스 한 번 재개: logs/usb-s129-20260909-021315.QCTzXY, exec session94705. readiness/chainload 통과 후 guest 시작 단계. 5분 자동 관찰은 PAUSED 유지. 오프라인 선택지 및 계정 생성 아직 미확인.
## S131 eight-core candidate prepared (2026-09-09)

- The first S130 desktop run reduced observed host-relayed NVMe service time from roughly 19.1 ms/I/O at the S129 baseline to 3.7-5.7 ms/I/O, depending on workload. The Windows desktop later stopped responding after about 46,340 reads; the hypervisor stayed alive and no CFS, WHEA, BugCheck, SError, panic, or traceback marker appeared.
- CPU enumeration was then promoted to the next bottleneck. The current UEFI source explicitly disabled MADT GICC entries 1-7. S131 enables all eight M1 GICC entries while retaining S129 internal-SSD-first boot and the 4 GiB USB DMA safety window.
- `m1n1_windows/src/hv_psci.c` also repeatedly initialized `psci_cpu_data_array[0]` for every CPU power-domain node. S131 indexes that array by `node_index` so each AP starts with its own PSCI state.
- Candidate hypervisor: `m1n1_windows/build/m1n1-s131-psci-index.bin`, 2,146,304 bytes, SHA-256 `0c612607debfbf5524a0f2c1431e551b8604ea24d1ad2c59e2c68dfd7d518967`.
- Candidate payload: `m1n1_windows/m1n1-payload-s131-8cpu.bin`, SHA-256 `4e87a1d2bf362e309884a0539c6023c49c6da01bb77894a35109a88e430389b1`.
- Launcher: `nwoas_scripts/usb-s131-guest-test.sh`. The old S103/S129 pair remains the exact one-core rollback path. Hardware validation is still required; a successful build is not proof that Windows brings up all APs stably.
- Preflight found that the old PSCI path wrote the Windows entry point directly into `spin_table.target`, which both bypassed EL2/vGIC AP initialization and was rejected by the existing corrupted-target guard. The final S131 HV routes `CPU_ON` through `hv_start_secondary()`, keeps the asynchronous X0-X3 frame in persistent per-CPU storage, and fixes the guest-active mask to set `BIT(cpu)` for the AP rather than `BIT(smp_id())` for CPU0.

## S131 hardware result and S132 lifecycle repair

- S131 hardware boot proved UEFI started CPU1-CPU7 and all seven reached `HV: Entering guest secondary ... at 0x83e4c0000`, with per-core vGIC initialization and Apple system-register handling visible on cpu1-cpu7.
- The run later stopped at Windows `PSCI DEBUG: turning on CPU1` followed by a second `HV: Initializing secondary 1`. Root cause: UEFI AP shutdown used the old PSCI `CPU_OFF` implementation, which physically slept the AP but did not clear `hv_started_cpus[]` or `hv_cpus_in_guest`; Windows therefore hit a double-start deadlock.
- S132 routes guest `CPU_OFF` to `hv_exit_cpu(index)`. The normal EL2 exception-exit path now clears the guest-active state and returns the AP to m1n1's parking loop before Windows `CPU_ON` calls `hv_start_secondary()`.
- S132 HV: `m1n1_windows/build/m1n1-s132-psci-lifecycle.bin`, 2,146,304 bytes, SHA-256 `19fd219568a06c23cc6c73f08288a970d052e82ecc76d51f876c6eeeb332833c`.
- S132 retains the S131 UEFI payload SHA-256 `4e87a1d2bf362e309884a0539c6023c49c6da01bb77894a35109a88e430389b1`; launcher is `nwoas_scripts/usb-s132-guest-test.sh`. Hardware validation remains pending.
## S133 — single-owner 8-core bring-up (2026-09-09 10:59 KST)

- S131/S132의 CPU1 정지는 AP의 물리 동작 실패가 아니었다. Windows의 Apple AIC HAL이
  먼저 PMGR `CPU_START`를 써서 firmware RVBAR로 AP1-7을 진입시킨 뒤, 같은 Windows
  부팅에서 PSCI `CPU_ON`을 다시 호출해 이미 guest 안에 있는 CPU1을 재초기화하면서
  `HV: Initializing secondary 1`에서 교착했다.
- S133은 MADT 8-core와 C PSCI 경로를 그대로 유지하면서
  `nwoas_scripts/smp-s133/pmgr_gate.py`로 Python PMGR dispatch만 차단한다. AP는 m1n1
  spin table에 남고, 이후 PSCI가 전달한 실제 Windows AP entry에서 한 번만 시작된다.
- `hv_start_secondary()`가 재시작 전에 `hv_should_exit[cpu]`를 지우도록 보강했다.
  PMCC 진단은 600줄에서 32줄로 제한했다.
- 실기기 S133 결과: `NWOAS-S133 gate PMGR AP start` 뒤 CPU1-7 각각에 대해
  `PSCI DEBUG: turning on`, `HV: Initializing secondary`, `HV: Entering guest secondary`
  순서가 모두 완료됐다. 각 AP 진입은 정확히 1회였고 S131/S132의 CPU1 교착은 재현되지
  않았다.
- 5분 시점까지 evtdump heartbeat가 CPU0, CPU6, CPU7에서 이어졌고 WHEA, BugCheck,
  SError, panic, traceback, CPU exit 표식은 없었다. NVMe 누적 I/O는 27,648 doorbell까지
  진행했고 마지막 누적치는 host 1.8ms/I/O, guest 1.8ms/I/O였다. 화면/작업 관리자상의
  8 logical processors 확인은 사용자가 돌아온 뒤 필요하다.
- 증거 스냅샷:
  `nwoas_scripts/logs/usb-s133-20260909-105937.8cpu-5min.log`, SHA-256
  `cc3046f6c6db371ec6be599342ac0e142b06d3b15c4b8e8ce3bb78b10f3b1734`.
- 이 실기기 실행에는 시작 시 로드된 S130 로깅 정책이 적용되어 `S97`이 64 I/O마다,
  `S93`이 128 blocks마다 출력됐다. 다음 실행용 소스는 각각 초기 milestone 이후
  4096 간격으로 낮추고 FLUSH도 표본화했다. 로직 변경 없이 직렬 출력만 줄인 변경이며
  NVMe S124 37개 + S130 5개 테스트가 통과했다.
- S133 HV:
  `m1n1_windows/build/m1n1-s133-single-owner-smp.bin`, 2,146,304 bytes,
  SHA-256 `e8b9590d0ec3b55e710623e53d735a70d1a353ba5d929de024a005cb2e2ff153`.
- 동일 실행은 10분 시점까지 연속 생존했다. 마지막 heartbeat는 600초였고 CPU0-7이
  NVMe 처리에 참여한 로그가 남았다. 이 시점에도 WHEA, BugCheck, SError, panic,
  traceback, CPU exit 표식은 없었다. 화면의 응답성과 작업 관리자 8 logical processors는
  사용자가 돌아온 뒤 확인해야 한다.
- `nwoas_scripts/smp-s133/summarize_log.py`는 실행 중인 장치를 건드리지 않고 S133 로그의
  PMGR gate 수, CPU1-7 단일 진입, 마지막 heartbeat/NVMe 통계, fatal marker를 요약한다.

## 다음 병목/드라이버 진단 준비

- `nwoas_scripts/s134-windows-inventory/run-as-admin.cmd`를 준비했다. Windows에서 한 번
  실행하면 `C:\NWOAS\S134-Windows-inventory.zip`에 CPU/core/clock 메타데이터, CPU별
  3초 표본, PnP 문제 장치와 hardware ID, network/storage/driver/power/BCD 정보를 모은다.
  시스템 설정은 바꾸지 않는다. 현재 WINARM USB가 맥북에 연결되어 있지 않아 전송은
  아직 하지 않았다.
- 전송용 묶음은 `nwoas_scripts/NWOAS-S134-Windows-inventory.zip`, SHA-256
  `4eb683af5ae1c3317c35a5e5dbf23704ad3aae7bb16cac967381549eb387904c`이다.
- 로컬 AppleWOA 원격 refs를 fetch만 했다. `apple_silicon_platforms_mu`의 새
  `feature/dart_updates` 브랜치는 이름과 달리 현재 Apple DART 완성본이 아니라 Microsoft
  SMMUv3 코드를 가져온 템플릿 단계다. 현 S133에 병합할 수 있는 즉시 사용 가능한
  PCIe/DART 해결책으로 보지 않는다.
- 내장 Wi-Fi/BT는 PCIe port0, BCM57762 Ethernet은 PCIe port2 아래에 있으나 현재
  DSDT의 `PCI0._STA`가 0이라 Windows에 보이지 않는다. 먼저 정상 동작하는 USB 경로로
  네트워크를 확보하고, 전체 RAM과 내장 네트워크는 PCIe DART/Windows DMA 소유권을
  해결하는 별도 단계로 다룬다.
- `0.04 GHz`의 명백한 펌웨어 입력 문제도 찾았다. SMBIOS Type 4에서 `MaxSpeed=3228`
  MHz이지만 `CurrentSpeed=0`이었다. ACPI `_CPC`나 PMGR 제어를 추가하지 않고
  `CurrentSpeed=3228`만 넣은 S135 payload를 별도로 빌드했다:
  `m1n1_windows/m1n1-payload-s135-8cpu-speed.bin`, 32,342,016 bytes, SHA-256
  `73f9623d6cf8060c387e2a87119391ed66a49f93ec70b4cd628dc40a8df93e11`.
  안정 기준 S131 payload는 빌드 전후 SHA-256
  `4e87a1d2bf362e309884a0539c6023c49c6da01bb77894a35109a88e430389b1`로 보존됐다.
  실행기는 `nwoas_scripts/usb-s135-guest-test.sh`이며 S134 WMI 결과를 먼저 수집한 뒤
  다음 정상 재부팅에서 시험한다.
- 최신 `m1n1_windows` 원격 refs도 fetch만 했다. `origin/bugfix/vgic_fixes`의
  `1b3f004b`가 guest `GICD_ICENABLER` 처리에서 `aic_set_mask(..., true)`를 쓰는 동일한
  극성 수정을 포함한다. 현재 소스에 이미 default-off gate로 준비돼 있던 그 한 변경만
  켠 S136 후보를 빌드했다: `m1n1_windows/build/m1n1-s136-vgic-mask.bin`, 2,146,304
  bytes, SHA-256 `b4a2f0d65e2a784da8b4ca2d944d5a2e2300df2c546a48d55f9ea5a9118130be`.
  빌드 후 `src/hv_vgic.c`가 원래 SHA-256
  `f5ad1c11d3de4308256af12b81038ad302c2ec43000c737088b5e63c617f18e6`로 복구됐음을
  확인했다. `nwoas_scripts/usb-s136-guest-test.sh`는 S131 payload와 115200 baud를
  유지해 이 변경만 A/B한다. 현재 S133이 화면에서도 정상이라면 시험하지 않는다.
- 다음 부팅부터 `uefi-s125/link_module.py`가 host-RAM `NWOASLINK` 디스크에
  `S134.CMD`와 `COLLECT.PS1`을 함께 싣는다. 따라서 S134 진단 전달에는 물리 WINARM
  USB를 맥북/맥미니 사이에서 옮길 필요가 없다. 현재 실행은 모듈을 이미 메모리에
  로드했으므로 이 파일 추가가 보이지 않으며, 다음 실행부터 적용된다.
  34,603,008-byte FAT 이미지 생성 검사와 transport-s123 11개 단위 테스트가 통과했다.

## 11:29-11:36 KST — S136 회귀 및 S137 관찰 빌드

- S133 장기 정지 로그를 141,785바이트, SHA-256
  `28bc974b22ce3e5d2f44619b0973b2821b23b1d490d805e4f518af60b9a75336`로
  보존했다. 정확한 PID 69602만 SIGTERM으로 종료한 뒤 단일
  `macvdmtool reboot serial`과 NOP 검증이 통과했다.
- S136은 UEFI 진입 직후 `pc misaligned` 예외와 guest HVC#0을 거쳐 EL2
  shell로 빠졌다. 이 한 번의 결과만으로 vGIC polarity 변경이 원인이라고
  확정하지 않는다. 후속 S137 trace에서 유효한 `[spi-dis]` 전이가 아직
  하나도 나오지 않아 전송/부팅 비결정성도 남아 있다.
- S137은 S133 AIC 동작을 그대로 두고 유효한 GICD_ICENABLER SPI 전이만
  `[spi-dis]`로 출력한다. 빌드는
  `m1n1_windows/build/m1n1-s137-vgic-trace.bin`, 2,146,304바이트,
  SHA-256 `a1d8f34381283082ecebbf40004f3f0b1146a2fe46c0896ec8e3acdf90fc6dc6`다.
- 현재 실행 로그는
  `nwoas_scripts/logs/usb-s137-20260909-113313.1KXG5s`다. PMGR 직접 시작
  7개는 모두 gate됐고 CPU0-7 모두 NVMe relay 작업을 수행했으며 IRQ
  900/857/698 enable을 관찰했다. fatal marker는 없다.
- 화면은 Windows 자동 복구(`Windows가 제대로 로드되지 않은 것 같습니다`)
  로 진입했다. 연속 강제 재부팅 이력과 일치한다. 현재 run을 유지하고
  `고급 복구 옵션 보기 -> 계속(Windows 11로 계속)`으로 정상 부팅을 한 번
  끝까지 완료해 자동복구 카운터를 끊는 것이 다음 단계다.
- 사용자가 `계속`을 선택한 뒤 S137을 추가 전원 재부팅 없이 다시
  chainload했다. CPU0-7 NVMe activity와 32,768+ I/O를 확인했으나 약 3분
  40초 후 사용자가 `DPC_WATCHDOG` 블루스크린을 확인했고 guest가 자체
  재시작했다. 직전에는 NVMe queue 재생성과 2건의 write가 완료됐다.
  따라서 8-core startup 자체는 성공하지만 Windows DPC/interrupt progress가
  로그인 무렵 watchdog 제한을 넘는 것이 현재 핵심 실패다.
- 블루스크린 재시작으로 돌아온 proxy에서 전원 재부팅 없이 안정 기준
  S130(S103 HV + S129 1-core SSD-first payload)을 chainload했다. 실행 로그는
  `nwoas_scripts/logs/usb-s130-20260909-114146.q380wQ`다. 210초 시점
  32,768 I/O, USBSTS `0x18`, fatal marker 없음으로 진행 중이다. 바탕화면에
  도달하면 S134 inventory와 crash dump/event log를 먼저 회수한다.

## S138 crash dump 회수 및 S139 8-core NVMe DPC 수정 (2026-09-09 12:36 KST)

- S138은 S103 HV와 one-core SSD-first UEFI를 사용하고 ACPI XHC1을 숨겨 USB-A
  FL1100만 Windows에 노출한 복구 구성이다. Windows 바탕화면과 `NWOS.EXE`
  연결을 확인했고, 약 37분 동안 139,264 I/O doorbell을 처리한 뒤 job 5
  `shutdown /s /t 0`으로 정상 종료했다.
- S138에서 S137의 `C:\Windows\Minidump\050722-5031-01.dmp`를 host link로
  회수했다. 228,759 bytes, SHA-256
  `df6934636e259ba8c0475a765c64b680e3fa4ee0cf62c2ce78637b9d3729f01c`.
  dump header의 processor count는 8이며 bugcheck는 `0x133`, parameter는
  `0, 0x501, 0x500, 0xfffff802feb19338`이다. 따라서 S137은 모든 CPU를 시작한
  뒤 단일 DPC/ISR watchdog을 초과했다.
- 같은 Windows 인스턴스의 `stornvme.sys`와 `storport.sys` 및 Microsoft symbol
  server PDB를 회수해 triage stack을 해석했다. 정확한 경로는
  `stornvme!NVMeCompletionDpcRoutine` ->
  `storport!StorPortWriteRegisterUlong` -> `storport!RaidpAdapterDpcRoutine`이다.
  `StorPortWriteRegisterUlong+0x18`은 `dsb sy` 뒤 CQ head doorbell MMIO를 쓴다.
- 기존 S124 controller는 그 CQ-head acknowledgement 처리 안에서 `process()`를
  호출해 물리 SSD I/O와 새 CQE 생성을 동기 수행했다. Windows completion DPC가
  CQ를 비울 때 즉시 다시 채우고 IRQ를 재assert하므로 DPC가 끝나지 않을 수 있다.
- `nwoas_scripts/nvme-s139/controller.py`는 CQ-head doorbell을 CQE retire와 IRQ
  level 갱신만 하는 경로로 바꿨다. 물리 I/O는 SQ-tail doorbell에서만 수행한다.
  회귀 테스트 3개와 py_compile이 통과했다. S130의 기본 controller 선택은 그대로고,
  `NWOAS_CONTROLLER_DIR`이 있을 때만 S139 구현을 선택한다.
- S139 payload는 S131 MADT8/SSD-first 구성에 XHC1 `_STA=0`을 합친
  `m1n1_windows/m1n1-payload-s139-8cpu-usba.bin`, 32,342,016 bytes,
  SHA-256 `5dc5b108f5b8cc2a6cc80f53881b27696d5b695732683979e5b6948f5c18d953`다.
  S131 원본 payload는 SHA-256
  `4e87a1d2bf362e309884a0539c6023c49c6da01bb77894a35109a88e430389b1`로 복구됐다.
- 실기 S139 로그는 `nwoas_scripts/logs/usb-s139-20260909-123635.ARYdmj`,
  run_guest PID 87561이다. CPU1-7이 각각 정확히 한 번 PSCI로 시작됐고 CPU0-7이
  NVMe trap 처리에 참여했다. S137 실패점인 32,768 I/O를 넘어 40,960 I/O까지
  진행했으며 fatal/bugcheck/CFS/traceback은 없다. 콘치님이 Windows 바탕화면
  도달을 직접 확인했다. `NWOS.EXE` 재연결 후 Windows가 보고하는 processor 수와
  부하 안정성을 수집하는 것이 다음 단계다.

## S139 두 번째 실기와 S140 CPU P-state 수정 (2026-09-09 13:22 KST)

- 첫 S139은 Windows가 SQ1-4 네 개를 하나의 256-entry CQ1에 연결했다. CQ ack에서
  물리 I/O를 실행하지 않도록 바꾼 뒤에도 CQ가 꽉 차면 네 SQ backlog를 다시 진행할
  경로가 없어 약 49,152 I/O에서 멈췄다. S139 controller가 I/O queue pair를 하나만
  광고하도록 `MAX_Q=1`로 제한했고 회귀 테스트 3개가 통과했다.
- 두 번째 S139은 CQ1/SQ1 하나로 데스크톱과 NWOS 링크에 도달했고 Windows는
  8 cores/8 logical processors를 보고했다. 그러나 `CurrentClockSpeed=30`,
  `MaxClockSpeed=30`이었고 WinSAT 읽기 부하에서 S137과 같은 0x133 p1=0 단일 DPC
  watchdog이 재현됐다. PowerShell Start-Job CPU 시험은 worker StackOverflowException
  때문에 유효하지 않으며 PASS로 취급하지 않는다.
- 원인은 UEFI raw chainload가 Linux payload 경로의 `cpufreq_init()` 호출을 건너뛰는
  것이었다. S140 모듈은 기존 m1n1 T8103 구현을 guest 진입 전에 호출하고 두 cluster
  레지스터를 검증한다. 첫 실기에서 E cluster는 P5를 유지했고 P cluster는 P1에서
  P7로 바뀌었다. rc=0, 두 busy bit=0이었으며 CPU1-7도 모두 한 번씩 시작됐다.
- S140 Windows 네이티브 ARM64 검증은 `active=8`, 8 threads, 총 2억 회 연산을
  143,122 us에 완료했다. 실행 파일은 `cpufreq-s140/CPUSTRES.EXE`, SHA-256
  `e2f64d3934f1ba3641603ca0816016a754f3accfea6a4de91e4e1b8f58267a8c`다.
- 8개 native thread의 no-buffering NTFS 읽기도 64 MiB를 3,497,229 us에 실패 없이
  완료했다. `DISKREAD.EXE` SHA-256은
  `a1e67c80dc1a5324441eda162091d361df0fa1d9367f584f011a4dea00d0b5f9`다.
  약 19.2 MiB/s이므로 CPU 수정 뒤의 다음 병목은 동기 ANS relay다.
- 그 직후 0x133이 다시 발생했지만 subtype은 p1=1(누적 DISPATCH_LEVEL 시간)이었고,
  직전 로그에서 FL1100 IRQ698이 PENDING/outstanding 상태로 반복됐다. 사용자가 같은
  시점에 WINARM2 USB를 분리했으므로 hot-unplug USB DPC가 원인일 가능성이 크다.
  디스크 시험 완료와 crash를 같은 원인으로 단정하지 말고 새 minidump stack으로
  구분해야 한다. 첫 S140 로그는
  `nwoas_scripts/logs/usb-s140-20260909-130523.89pff5`다.
- S141은 Windows MDTS와 m1n1 direct PRP 상한을 64 KiB/16 pages에서
  1 MiB/256 pages로 함께 올리는 격리 후보다. HV는
  `m1n1_windows/build/m1n1-s141-1m-nvme.bin`, SHA-256
  `e3be86394ccc2d9c779147244d45bf9068d9118a3e2d93a22ad3268b8f9dbdfd`다.
  기존 default 64 KiB는 유지했고 nvme-s124 37 tests가 통과했다. S140 USB 분리 crash
  dump를 회수하고 USB가 빠진 S140에서 재현 여부를 확인하기 전에는 S141을 부팅하지 않는다.
- 현재 두 번째 S140은 `nwoas_scripts/logs/usb-s140-20260909-132239.ecSHRl`에서
  부팅했고 P1->P7 검증과 CPU1-7 시작을 다시 확인했다. Windows 바탕화면에서
  `D:\NWOS.EXE`를 실행한 직후 UAC가 나타나기 전에 화면과 입력이 정지했다.
  NWOS link의 `events.jsonl`은 생성되지 않았으므로 agent는 시작되지 않았다.
- 두 번째 S140 정지 시점의 I/O는 28,672건이다. FL1100은 `outst=0`, LR698 없음으로
  USB-A 인터럽트가 원인이라는 증거는 없었다. 반면 Apple 내장 xHCI는
  `IMAN=0x3`, `USBSTS=0x1019`, `pending=1`을 반복했다. 마지막 timer-injection
  guest PC는 `0xfffff8007a6b964c`, `0xfffff8007a4faf60`이며 그 뒤 HV 로그와
  exception trap이 함께 끊겼다. `run_guest` 프로세스는 살아 있으므로 host 도구
  종료가 아니라 guest timer/vGIC 또는 CPU progress 정지로 분류한다.
- Microsoft symbol server에서 이 Windows kernel용 `ntkrnlmp.pdb`를 회수했다.
  GUID는 `{80ED6679-0B46-EB0C-0BAD-A234C31EBC45}`이고 로컬 파일 SHA-256은
  `eb1db249fbfcb3194d4d648628dd961eac5b1cbf19a47a2fde7cdcbe6cf075d8`이다.
  다음 세션은 마지막 PC의 symbol resolution, one-core rescue로 새 0x133 dump 회수,
  NWOS 자동 시작 설치 순서로 재개한다. S141 1 MiB NVMe 후보는 이 정지 원인을
  분리하기 전까지 보류한다.

## S144-S148 — USB CDC 분리, 1 MiB NVMe 정합, 진단 핫패스 제거 (2026-09-09 19:42 KST)

- 직접 연결은 물리 UART가 아니라 DWC3 USB CDC bulk endpoint다. `CDC_SET_LINE_CODING`은
  descriptor 값을 저장할 뿐 endpoint 전송률을 바꾸지 않는다. NOP 왕복 중앙값은
  115200 표기에서 0.162479 ms, 1,500,000 표기에서 0.187209 ms였다. 결과는
  `nwoas_scripts/logs/baud-s144-nop-20260909-184557.json`에 있다. 따라서 표기 baud를
  올리는 방식은 성능 개선 수단이 아니다.
- `M1N1_SPLIT_CONSOLE=1`은 NY1을 UART proxy 전용, NY3을 console 전용으로 분리한다.
  S144의 raw guest UART 전달은 수 초 만에 784 KiB를 만들었고, S145부터 NY3에는
  `HVLOG:`만 전달한다. 전체 guest UART tail은 target의 128 KiB ring에 남는다.
- S141은 Windows/Python에 1 MiB MDTS를 광고했지만 target C의
  `NVME_MAX_BLOCKS=16` 때문에 실제 256-block 요청을 거부했다. `src/nvme.c`에
  `NWOAS_NVME_MAX_BLOCKS` 빌드 상한을 추가했고, S147은 target/Python/MDTS 모두
  256 blocks로 맞췄다. S147은 기존 실패 지점인 1 MiB bootloader read를 통과해
  Windows 사용자 공간과 8 cores/8 logical processors에 도달했다.
- S147 검증은 CPU 90,724 us, 64 MiB 읽기 88,224 us였다. S146 64 KiB 기준의
  85,839 us와 실질 차이가 없었다. 이 64 MiB 도구 결과는 캐시 영향 가능성이 있으므로
  원시 SSD 처리량으로 단정하지 않는다.
- S147 워치독 직전 별도 진단 채널에는 `NWOAS-TINJ`, `NWOAS-ISRPC`,
  `[usb-bridge]`, `[fl1100-bridge]` 상태가 반복됐다. S148은
  `NWOAS_HOTPATH_LOG=0`에서 이 네 출력과 불필요한 FL1100 상태 read를 컴파일 제외한다.
  bugcheck, CFS, USB PHY 재연결 같은 오류·복구 출력은 유지한다.
- S148 HV는 `m1n1_windows/build/m1n1-s148-perf-quiet-nvme-1m.bin`, 2,146,304 bytes,
  SHA-256 `8628b608e8b65299ae6ad6162377e19525f8731c2bea1e833a1b2b77a5e019c0`다.
  launcher는 `nwoas_scripts/usb-s148-guest-test.sh`, 실기 로그는
  `nwoas_scripts/logs/usb-s148-20260909-192343.nbUp64`이다.
- S148은 Windows worker까지 부팅했고 반복 핫패스 출력 네 종류는 모두 0건이다.
  검증은 8 cores/8 logical processors, CPU 90,645 us, 64 MiB 읽기 87,477 us,
  실패 0건이었다. 이번 부팅 뒤 새 Event 1001은 없었다.
- 출력 제거 뒤에도 누적 NVMe host 처리시간은 약 1.2 ms/I/O doorbell로 S147과 같다.
  로그/baud 가설은 기각됐다. 남은 주 병목은 guest MMIO trap -> MacBook Python ->
  USB proxy request -> target ANS2의 동기 왕복이다. 다음 단계는 부팅·admin 경로를
  Python에 남긴 채 Windows가 구성한 I/O SQ/CQ만 EL2 C fast path로 인계하는 격리
  실험이다.
