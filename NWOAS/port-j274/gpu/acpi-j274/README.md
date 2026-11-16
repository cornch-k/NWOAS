# J274 GPU ACPI 장치(AGX0) — Mu에 넣는 방법

**상태: ASL 작성·컴파일 시험을 했고, 별도 시험용 Mu 페이로드(`nwoas_scripts/fw-agx-s247`)에 넣어 빌드했습니다. 실기기에서는 쓰지 않았습니다.**
운영용 Mu(S237) 소스·결과물은 건드리지 않았습니다.

## 파일

| 파일 | 내용 |
|---|---|
| `J274AppleAgxAbiAdmission.asl.inc` | AGX0 장치(`APPL0002`). windows-on-m1 J313 원본(MIT)에서 보드 값만 바꿈 |
| `J274AppleAgxAbiAdmissionSsdt.asl` | 위 파일을 담는 별도 SSDT(`J274ABI`) |
| `out/J274AgxAbiSsdt-test.aml` | 시험 컴파일 결과(1069바이트, sha256 `15347541e59d46a36cb2e6b1953b55f52e0b01d7baa6484b8795add06fcf240a`) |
| `out/J274AgxAbiSsdt-test.disasm.dsl` | 위 결과를 `iasl -d`로 되돌린 것 |

## 값과 근거

모든 주소는 **13.5 실기기 ADT**(`nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin`,
sha256 `32e63706…`)에서 읽었고, 저장된 Tahoe ADT와도 같습니다(`../hv/out-hv2/hosttest-adt135.txt`).

| 자원 | 값 | 근거 |
|---|---|---|
| GPU 레지스터(sgx reg[0]) | 0x204000000, 0x4000000 | 13.5 ADT [사실]. gfx-asc reg[0] 0x206400000+0x6c000이 이 안에 들어감 [사실] |
| GPU 영역(gpu-region) | 0xBFFFB8000, 0x4000 | 13.5 ADT [사실] (J313은 0x9FFFB8000) |
| 핸드오프(gfx-handoff) | 0xBFFF70000, 0x4000 | 13.5 ADT [사실] |
| 중개 페이지 | 0x300000000, 0x1000 | ADT 값이 아니라 드라이버·하이퍼바이저 약속 주소. hv2가 설치 때 /arm-io 범위·게스트 RAM과 겹치지 않는지 검사 |
| 예약 메모리 | `_CRS`가 중개 페이지 +0xD00 영수증(판 3)을 읽어 채움 | 영수증이 없으면 메모리 자원 4개만 내보내 드라이버가 거절(의도된 안전 정지) |
| 인터럽트 | 889, Edge, 1개 | 가짜(화면 중개용) 번호. 13.5 ADT의 어느 장치도 889를 쓰지 않음 [사실]. hv2는 이 번호를 AIC로 넘기지 않음 |

GPU 실제 인터럽트 9개(sgx 563 564 565 566 579, gfx-asc 576 575 578 577, 13.5 ADT [사실])는 **넣지 않았습니다.**
드라이버가 "메모리 5개 + 인터럽트 정확히 1개"가 아니면 시작을 거절하기 때문입니다
(`external/windows-on-m1/drivers/apple-agx/render-admission/src/backend_platform_windows.c`의 자원 검사) [사실].

새 OS 펌웨어 스텁을 쓸 때마다 위 [사실] 값을 다시 확인해야 합니다. 하이퍼바이저는 ADT가 다르면 스스로 거절하지만,
이 표는 고정값이라 스스로 거절하지 않습니다.

## 시험 컴파일 (이 맥북, 실행 없음)

EDK2는 `Include()`를 먼저 펼친 뒤 전처리합니다. 같은 순서로 `.inc`를 SSDT에 펼친 파일을 만들어
`iasl 20260408 -DAGX_LOCAL_RESERVE_V2`로 컴파일했습니다. 오류 0, 경고 0. 되돌린 결과에서 `APPL0002`,
`0x0000000300000D00` 영수증 영역, 위 네 주소 범위, 인터럽트 0x379(=889)를 확인했습니다.
`AGX_LOCAL_RESERVE_V2` 없이 컴파일하면 `#error`로 멈추는 것도 확인했습니다.

## Mu에 넣는 방법 (시험용 별도 페이로드에서만)

운영용 `fw-s237-mu`가 아니라 **새 사본**에서 합니다.

1. 사본 만들기: `fw-s237/README.md` 4절과 같은 방법으로 `fw-s237-mu`를 새 폴더(예: `fw-agx-s247-mu`)로 APFS 복제.
2. 두 파일을 `Platform/MacMini2020Pkg/AcpiTables/`에 복사.
3. `Platform/MacMini2020Pkg/AcpiTables/DeviceAcpiTables.inf`의 `[Sources]`에 `J274AppleAgxAbiAdmissionSsdt.asl` 한 줄 추가.
4. `Platform/MacMini2020Pkg/MacMini2020.dsc`의 `[BuildOptions]`에
   `*_*_*_CC_FLAGS = -DAGX_LOCAL_RESERVE_V2`, `*_*_*_ASLPP_FLAGS = -DAGX_LOCAL_RESERVE_V2` 추가
   (windows-on-m1 `MacBookAirMid2020.dsc:53-54`와 같음).
5. **MemoryInitPeiLib 변경이 반드시 함께 필요합니다.** 없으면 Windows가 예약 메모리를 일반 메모리로 써 버립니다.
   windows-on-m1 `mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c:562-606, 692-693`과
   `Include/Library/AgxLocalReserveValidation.h`를 참고해, S237 `MemoryInitPeiLib.c`에
   - 중개 페이지 0x300000000(4 KiB)을 Mu 페이지 표에 장치 메모리로 추가,
   - +0xD00 영수증(magic 0x4C584741, 판 3, valid 1)을 읽어 크기·정렬·RAM 안·기존 HOB와 안 겹침을 검사,
   - 통과하면 기존 `ReserveMemoryRegion(base, bytes)`(S237이 PCIe 창에 쓰는 것과 같은 함수)로 예약,
   - 실패하면 예약하지 않고 `HVLOG:` 한 줄을 남기고 계속.
6. 빌드는 `fw-s237/build.py`를 새 출력 폴더(`fw-agx-s247/out`)로. 고정 해시가 걸린 기존 파일은 그대로 둡니다.

### 시험용 페이로드 (2026-11-16 갱신: 빌드함)

위 1~6번을 `nwoas_scripts/fw-agx-s247/build.py`가 그대로 합니다. 결과와 해시는 `nwoas_scripts/fw-agx-s247/README.md`에 있습니다.
빌드 트리 안 SSDT AML(1069바이트, sha256 `15347541…`)은 `out/J274AgxAbiSsdt-test.aml`과 바이트가 같습니다.
이 페이로드는 중개 페이지가 있는 HV(g247A/g247B)로만 부팅해야 합니다. 부팅은 하지 않았습니다.
