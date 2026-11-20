# bench-cb26 — Cinebench 2026 측정 (2026-11-20 연쇄 추가) [준비만, 실행 안 함]

- `run-cb-chain.sh`: 일상 구성(S268 + g262U + QD 32 + 블루투스)에서 설정마다 새로 부팅해 Cinebench 멀티·싱글을 잽니다.
  - 설정: base · wfi · sectick · wfi_sectick · madt · madt_all
  - 부팅마다 Wi-Fi 상태(W2), SSD 되읽기(Q2), NVMe 안전 계수기, 하이퍼바이저가 보고한 게스트 예외 수를 함께 모읍니다.
  - 마지막 줄은 `CBCHAIN DONE`이고, 그 앞에 표가 붙습니다. 끝나면 평소 부팅으로 한 번 더 켭니다.
- `launch-s268-cb.sh`: `CB_MADT=1`이면 일상 실행기를 읽어 페이로드 두 줄만 S280으로 바꿉니다. 일상 실행기는 고치지 않습니다.
- S280 페이로드: `../fw-btmadt-s280/out/s280/m1n1-payload-s280-btmadt.bin`
  - S261 블루투스 페이로드 + MADT EfficiencyClass 1(CPU4~7)
  - 호스트 검사 `../fw-btmadt-s280/verify_vs_s261.py` PASS: 소스 차이는 `MADT_Static.aslc` 하나, ACPI 표 14개 중 MADT 4바이트만 다름, S260 MADT와 같음
  - 같은 트리에서 다시 빌드해 같은 지문이 나왔습니다.
- 지문: `SHA256SUMS`
