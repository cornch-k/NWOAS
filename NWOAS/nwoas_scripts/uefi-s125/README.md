# S125 내부 SSD의 UEFI 가시화 준비 (아직 실기 실행 전)

- S102 페이로드의 앞 1376256 바이트를 유지하고, 현재 Project Mu 소스에서 NWOAS_UEFI_NVME=1로 만든 FD를 결합.
- 이전 S99 계획의 non-discoverable NVMe 등록 경로. NvmExpressDxe는 이미 DSC/FDF에 포함됨.
- 소스 변경은 빌드 중에만 적용하고 원래 파일을 byte-for-byte 복원. SHA는 manifest.json.
- usb-s125-guest-test.sh는 S103 hypervisor, S124 저장 정책, S123 통신을 유지하고 S125 UEFI만 사용.
- NWESP.EXE는 FAT32/크기 범위/정확 offset 220524969984/300MiB/partition3/EFI GPT type 확인. ready/flush 전 식별 필수. ARM64 build와 API stub 15 cases 통과. 실기 확인 전.
- 01-inspect-esp.cmd는 설치완료 marker와 NTFS guard 후 disk GUID 유일일치 검사, partition3에 S: 할당, NWESP ready 확인. 포맷/GPT 변경/bootfile 쓰기 없음. 아직 실행 전.
- NWOS.EXE는 설치된 Windows용 개발 통신 수신기 후보, 아직 배치/실행 안 함.

현재 설치 완료를 기다리는 중이며 S125를 성공한 부팅 경로로 취급하면 안 된다.
