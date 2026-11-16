# S247 — GPU 2단계용 시험 페이로드 (S237 + J274 AGX0 ACPI + 예약 메모리)

**상태: 빌드만 했고 부팅하지 않았습니다.** 빌드 성공은 부팅 성공이 아닙니다. 운영 페이로드(S237)와 그 고정 파일은 건드리지 않았습니다.

| 항목 | 값 |
|---|---|
| 페이로드 | `out/s247/m1n1-payload-s247-agx.bin` |
| 페이로드 sha256 | `fb2011f96aea4ca30c69d5bd706a379a04010dc7895366b5c86e191444ee641c` |
| 페이로드 크기 | 33669120 바이트(S237과 같음, 같은 조립 방식) |
| FD sha256 (`out/s247/fd-agx.fd`) | `ebd3bb723e9ade72e7096eeadef85889a206672ed13c449664b805c6a086bd6a` |
| SSDT(빌드 트리 안 `J274AppleAgxAbiAdmissionSsdt.aml`) | 1069 바이트, sha256 `15347541…` = `port-j274/gpu/acpi-j274/out`의 단독 iasl 결과와 같음 |
| 런처 | `../agxmed-s247-guest-test.sh`(`tools/make_agxmed_launcher.py`로 1단계 런처에서 생성) |
| 함께 쓰는 HV | `port-j274/gpu/hv/out-hv2/m1n1-g247B-agx-hv2-j274hwdata.bin` |

## 바뀐 것 (S237 대비, 사실)

`build.py`는 `../fw-s237/build.py` 복사본입니다. S216→S237 편집 순서를 그대로 하고 아래만 더합니다(`out/s247/result-agx.json`의 `changed_sources`).

| 파일 | 내용 |
|---|---|
| `MemoryInitPeiLib.c` (`src/`, 차이: `s247-agx-local-reserve.patch`) | PEI에서 중개 페이지 +0xD00 영수증(판 3)을 읽고, 1:1·64 KiB 정렬·크기·게스트 고주소 RAM 안·FD 밖·PEI 할당과 안 겹침을 검사한 뒤 `ReserveMemoryRegion`으로 예약합니다. 중개 페이지 4 KiB를 Mu 페이지 표에 장치 메모리로 추가했습니다. 영수증이 없으면(LOG 모드) 예약하지 않고 `HVLOG: S247 … absent` 한 줄만 남깁니다 |
| `MemoryInitPeiLib.inf` | `IoLib` 추가 |
| `AcpiTables/J274AppleAgxAbiAdmission*.asl*` | `port-j274/gpu/acpi-j274`의 파일 그대로 |
| `AcpiTables/DeviceAcpiTables.inf` | `[Sources]`에 SSDT 한 줄 |
| `MacMini2020.dsc` | `*_*_*_ASLPP_FLAGS = -DAGX_LOCAL_RESERVE_V2` |

결과 확인: FD 안에 `S247 AGX local reserve` 문자열 2개가 있고(S237 FD에는 0개), S237·S216 기록 줄은 그대로 있습니다. 앞쪽 guest m1n1과 DTB(0x0..0x21c000)는 S237과 같고, 첫 차이는 FD 안(0x225178)입니다.
iasl 경고는 기존 DSDT의 1개뿐입니다(S237 빌드와 같음). 새 SSDT는 경고 0개입니다.

## 빌드 방법

```
cd /Users/angigyeom/Desktop/DEV/NWOAS
mkdir fw-agx-s247-mu; (cd fw-s237-mu && for e in .git* *; do [ "$e" = venv ] && continue; cp -Rc "$e" ../fw-agx-s247-mu/; done)
python3 nwoas_scripts/fw-agx-s247/build.py --tree fw-agx-s247-mu --variant agx --out nwoas_scripts/fw-agx-s247/out/s247
```

이 맥북의 실제 경로(`/Users/.../fw-agx-s247-mu`)에서 빌드합니다. S237처럼 `/Volumes/X31/NWOAS` 논리 경로를 만들지 않았습니다(X31 의존성을 되살리지 않기 위해). 그래서 S237과 바이트가 같은 판은 아니고, 디버그 정보 속 경로가 다릅니다.

재현성 [사실]: 같은 트리에서 `Build`를 옮겨 두고 `--rebuild-only`로 다시 빌드했습니다. 페이로드와 FD 모두 바이트 단위로 같았습니다(`out/s247-repeat/result-agx.json`; 중복 바이너리는 지웠습니다).

## 주의

- 이 페이로드는 **g247A/g247B(중개 페이지가 있는 HV)로만** 부팅해야 합니다. S233b처럼 중개 페이지가 없는 HV에서는 PEI가 0x300000D00을 읽다가 게스트가 멈출 수 있습니다 [가설, 실기기 미확인].
- LOG 모드에서는 영수증 값이 0이라 예약하지 않습니다. 이 경우 ACPI `_CRS`가 메모리 자원 4개만 내보내고, 드라이버는 시작을 거절합니다(의도된 안전 정지).
- 예약 주소 0xA00000000은 1단계 로그의 게스트 배치를 기준으로 골랐습니다(`port-j274/gpu/hv/README-hv2.md`). 배치가 달라지면 HV나 Mu가 거절합니다.
