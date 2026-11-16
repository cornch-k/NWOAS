# S246 — J274(Mac mini M1) GPU 펌웨어 첫 관문 (G0·G1)

paulsmir/windows-on-m1의 J313 G0·G1 관문(`documentation/AGX_BRINGUP.md`, `tools/agx_m1n1_backend.py`, MIT)을 J274로 옮긴 것입니다. **그리기·Windows GPU는 다루지 않습니다.** 펌웨어를 켜고, 관리 응답을 받고, 끄는 것까지만 합니다.

## 결과 (2026-11-16, 실기기)

| 단계 | 결과 | 근거 |
|---|---|---|
| G0 장치 정보 | 13.5 부팅(iBoot-8422.141.2 → V13_5/G13)의 ADT 덤프, J274 기준값 `evidence/g0-contract-20261116/j274-agx.json` (sha256 `732cae8b…`) | `evidence/g0-adt-20261116-live1/` |
| G1 10회 | **10/10 통과.** 회차마다 프록시 재부팅, m1n1 적재 주소와 `/chosen` 난수 씨앗이 매번 달라짐(새 부팅 증명) | `evidence/g1-20261116/cycle01~10.json` |

| 회차 | 관리 응답(ms) | m1n1 적재 주소 |
|---|---|---|
| 1 | 1.71 | 0x802bac000 |
| 2 | 1.83 | 0x8024dc000 |
| 3 | 1.81 | 0x802b38000 |
| 4 | 1.74 | 0x802fb0000 |
| 5 | 1.88 | 0x803d3c000 |
| 6 | 1.71 | 0x802204000 |
| 7 | 1.80 | 0x802e24000 |
| 8 | 1.89 | 0x803a34000 |
| 9 | 1.82 | 0x802b48000 |
| 10 | 1.83 | 0x802d84000 |

모든 회차: ASC 실행 중, IOP·AP 전원 상태 32, 펌웨어 halted/halt_count/resume 0, RegionC 오류 기록 0, SGX 인터럽트 카운터 0, 정지와 UAT 루트 정리 완료.

## 파일

| 파일 | 하는 일 |
|---|---|
| `j274_agx_common.py` | 기준값 검증, ADT 해석, 메모리 겹침 검사, 시간 제한, 프록시 연결(m1n1.setup 없이) |
| `adt_dump.py` | 읽기 전용 ADT 덤프 |
| `make_contract.py` | 덤프 → J274 기준값(오프라인) |
| `g1_cycle.py` | G1 1회: 기준값 일치·메모리 겹침 확인 → V13_5 지정 → gfx-asc·sgx 클럭 → `AGX.start()` → Pong 1초 제한 → 상태 기록 → 정지·UAT 정리 |
| `g1_run.sh` | 회차 사이 `tools/reboot.py`로 재부팅, 새 부팅 증명, 첫 실패에서 멈춤 |

## 실행

```sh
cd /Users/angigyeom/Desktop/DEV/NWOAS
# 맥미니가 D4 m1n1 "Running proxy..." 상태(Windows 실행 스크립트가 돌고 있지 않음)일 때
bash nwoas_scripts/agx-g1-s246/g1_run.sh nwoas_scripts/agx-g1-s246/evidence/<새 폴더> 1 10
```

맥미니에서 건드리는 것: GPU 클럭(pmgr), GPU 펌웨어 RAM 영역과 m1n1 파이썬 힙, SGX MMIO 1곳(`poke_sgx`), 재부팅. NVMe·화면·디스크는 건드리지 않습니다.

## 한계

- 펌웨어 켜기/끄기만 확인했습니다. 그리기 작업, Windows 커널 드라이버, 하이퍼바이저 GPU 중개는 이 단계에 없습니다.
- 클럭을 켠 뒤에는 재부팅으로만 원래 상태가 보장됩니다(paulsmir와 같은 규칙).
