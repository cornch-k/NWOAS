# 제3자 소스 안내 (THIRD_PARTY)

이 폴더의 `drivers/apple-agx/`는 paulsmir/windows-on-m1 저장소의 `drivers/apple-agx/`를 그대로 복사한 뒤, `NWOAS-J274 PATCH` 표시가 붙은 부분만 고친 것입니다.

| 항목 | 값 |
|---|---|
| 원본 저장소 | https://github.com/paulsmir/windows-on-m1 |
| 복사 원본 위치 | `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1/drivers/apple-agx` (읽기 전용 사본) |
| 원본 커밋 | 상위 저장소 `6c65673` (얕은 클론, main 가지). `m1n1_windows` 하위 모듈 `882985f` |
| 라이선스 | MIT, `Copyright (c) 2026 paulsmir` — 전문은 `LICENSE.windows-on-m1` |
| 함께 복사한 파일 | `Directory.Build.props`, `packages.config` (저장소 최상위, MSBuild가 NuGet WDK/SDK를 찾는 데 필요), ACPI 참고본 `upstream-acpi-reference/J313AppleAgxAbiAdmission*.asl*` |

파일마다 붙어 있는 원래 머리글은 지우지 않았습니다. 그 밖의 출처:

| 출처 | 라이선스 | 예 |
|---|---|---|
| Asahi Linux 기여자 (m1n1 생성 바이트) | MIT | `shared/include/apple_agx_hwdata_profile.generated.h` 머리글 |
| Mesa (Asahi 컴파일러 등 덧씌운 파일) | 파일 머리글을 따름(대부분 MIT) | `mesa/windows-overlay/agx_pack.c` |
| Microsoft 예제 | MIT | `windows/icd/agx_d3d12_probe_cs_nbody.hlsl` |

Mesa 본체(커밋 `9aa1215f878b504f66159dd2ead4c7973142126e`)와 dxil-spirv 등은 이 폴더에 들어 있지 않습니다. 내려받기 전에 승인이 필요합니다(README.md의 "내려받기 목록").

새로 만든 파일(우리 것): `apple_agx_board.h`, `render-admission/src/arm64_hypercalls_gnu.S`(원본 `.asm`을 옮겨 쓴 것이라 원본 MIT 표기 유지), `nwoas-tests/board_selftest.c`, `acpi-draft/J274AppleAgxAbiAdmission.asl.inc`(원본 생성 파일에서 주소 두 개만 바꾼 초안), 문서 파일들.
