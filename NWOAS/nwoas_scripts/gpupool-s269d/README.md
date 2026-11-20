# S269 — 일상 구성 + GPU 풀 창 (2026-11-21) [준비만, 실행 안 함]

## 결론
- 풀에 필요한 것은 실행기 한 줄 `NWOAS_AGX_POOL_IPA=0xd00000000`(4 GiB 고정)뿐입니다. 하이퍼바이저·페이로드·ACPI·드라이버는 바꾸지 않습니다 [사실: S259와 S265 실행기의 차이가 이것뿐, RESULT-POOL-S265.md 1장, RESULT-2775.md 36장].
- 주소는 겹치지 않습니다(아래 표) [사실: 코드·ACPI 소스].
- 다만 이 조합은 처음 돌립니다 [사실]. 지금까지 풀은 g254L + S259 페이로드에서만 돌았습니다. 블루투스·전력 기본값·QD 32·S280·g262U와 함께 돈 적은 없습니다.

## 파일
- `agxdcp-s269-guest-test.sh` = S268 + 풀
  - `NWOAS_S269_POOL=1`이 기본이고, `NWOAS_POOL`도 같은 스위치로 받습니다. `0`이면 S268과 같습니다.
- `bt-s268/run-s269.sh` = `run-s268.sh` + boot-s265.sh의 판정들. 마지막 줄은 `S269 DONE … pool=<0|1> el2_exc=<n>`입니다.
- 둘 다 `make_s269.py`가 만듭니다. 각 치환 대상은 정확히 한 번 나와야 하고, 원본 sha256을 머리말에 적어 둡니다.

## 주소 확인 (풀 0xd00000000–0xdffffffff)
| 쓰는 것 | 주소 | 출처 |
|---|---|---|
| AGX 중개 쪽 | 0x300000000 +0x1000 | audio_module.py S256_C_HV_IPA, MemoryInitPeiLib.c |
| 소리 창 | 0x340000000 +0x40000 | audio_module.py |
| Wi-Fi 제어 쪽 / BT 제어 쪽 | 0x348000000 / 0x348004000 (+0x4000 각각) | nwoas_wifi_s259.py, nwoas_bt_s261.py |
| Wi-Fi BYPASS 창 | 0x6e0000000 +0x900000 (S261 창 끝 0x6e0ffffff) | nwoas_wifi_s259.py BAR_EMUL |
| BT fn1 BAR | 0x6e0900000 +0x8000, 0x6e0c00000 +0x400000 | nwoas_bt_s261.py BT_BAR_EMUL |
| AGX 로컬 예약 | 0xa00000000 +1 GiB (게스트 RAM 안) | 실행기 NWOAS_AGX_LOCAL_IPA |
| 게스트 RAM 끝 | 0xbe1000000 | 13.5 탐침 guest_ram_end |
| 화면 메모리 | 0xbe3f60000 +0x1500000 | 탐침 /vram |
| vGIC | 0xF00000000–0xF20010000 | audio_module.py S256_C_HV_IPA |
| S280 ACPI 표의 주소 | 가장 큰 값 0xc00000000 (0xd로 시작하는 범위 없음) | fw-btmadt-s280-mu AcpiTables 전체 grep |

- UEFI 메모리 지도: 풀은 게스트 RAM 밖이라 Mu가 예약할 필요가 없습니다. S280의 MemoryInitPeiLib.c는 S259와 같은 파일이고, S259 페이로드로 풀을 켠 부팅은 바탕화면까지 갔습니다.
- 하이퍼바이저는 풀 창이 게스트 RAM과 겹치면 거절합니다(RESULT-POOL-S265.md 3.2).
- g262U: g248C 풀 창 보호 코드가 있는 트리입니다. g254L 이후 패치(g260O·g262P·Q·R·T·U)는 풀 창 코드를 건드리지 않았습니다 [사실: 패치 대상 파일 목록]. g262U가 고친 "pool"은 화면 출력 쪽의 다른 것입니다.
