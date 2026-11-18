# 기기별 가정 목록 (INVENTORY) — windows-on-m1 GPU 드라이버 vs 우리 Mac mini(J274)

결론: **드라이버 안의 기기별 가정은 크게 넷입니다. ① 8 GB 기준 메모리 주소 3개, ② J313 ADT에서 떠 온 GPU 전원·성능 값, ③ 내장 패널 2560x1600 고정 화면, ④ 그쪽 하이퍼바이저와 주고받는 약속(중개 페이지·HVC·인터럽트 889).** ①은 이번 패치로 J274 값(가설)을 넣었고, ②는 J274 실기기에서 ADT를 읽어야 하며, ③은 HDMI용으로 새로 짜야 하고, ④는 우리 하이퍼바이저에 아직 없습니다.

- 경로 약어: `AGX/` = `external/windows-on-m1/drivers/apple-agx/`(원본), `WOM/` = `external/windows-on-m1/`, `LOG/` = `nwoas_scripts/logs/`
- [사실] = 파일:줄로 확인, [가설] = 확인 못 한 추측. "모름" = 실기기에서 읽어야 함.
- 드라이버에 쓰인 구성: 정식 커널 드라이버는 `AGX/render-admission`(AppleAgxRenderAdmission.sys), 사용자 영역 드라이버는 `AGX/render-admission/umd` + `AGX/d3d12` + `AGX/mesa`. `AGX/windows`(G2), `AGX/admission`은 옛 개발용입니다.

## 1. 보드·칩·펌웨어

| 항목 | J313 값(원본 위치) | Mac mini J274 값 | 구분 |
|---|---|---|---|
| 보드 번호 | 0x26. 드라이버 안에는 검사 없음(grep 결과 없음). 검사는 하이퍼바이저 `WOM/m1n1_windows/src/hv_agx_retained_platform.c:454`, 프로필 도구 `WOM/tools/generate_agx_hwdata_profile.py:120` | 0x22 (`LOG/appark-chainload-1558.log:49`) | [사실] |
| 칩 | T8103 (`AGX/shared/include/apple_agx_hwdata_profile.h:25` `r->Chip!=0x8103`) | T8103 (Chip-ID 0x8103, 같은 로그 :50) | [사실] |
| GPU 펌웨어 판 | V13_5 / G13 (`AGX/shared/include/j313_agx_g2.generated.h:7-8`), 초기 설정 판 번호 4개(:31-34) | 결정 D1로 Windows 칸은 13.5 묶음 예정. 7월 13.5 부팅 기록 `LOG/appark-chainload-1558.log:86`. 같은 칩·같은 macOS 판이면 GPU 펌웨어도 같을 것 | [사실] 7월 기록 / [가설, 높음] 펌웨어 동일 |
| ACPI 장치 이름 | `ACPI\APPL0002` (`AGX/render-admission/AppleAgxRenderAdmission.inf:26`) | 우리 MacMini2020Pkg DSDT에 없음(`APPL0100`, `APPL8900`만, `DSDT.asl:180,189`) | [사실] — ACPI 담당이 추가해야 함 |

## 2. 메모리 주소·크기

| 항목 | J313 값(원본 위치) | J274 값 | 구분 |
|---|---|---|---|
| GPU 레지스터(sgx) | 0x204000000, 0x4000000 (`j313_agx_g2.generated.h:84-85`) | 같은 칩이라 같을 것. 정적 FDT `nwoas_scripts/dtb-s221/recovered-j274.dts:521`도 0x204000000 | [가설, 높음] |
| GPU 보조 CPU(gfx-asc) | 0x206400000, 0x6c000 (:88-89) | 같을 것(같은 FDT :521) | [가설, 높음] |
| GPU 영역(gpu-region) | 0x9fffb8000, 0x4000 (:86, :92; ACPI `WOM/mu/.../J313AppleAgxAbiAdmission.asl.inc:33`; 구성 검사 `AGX/shared/src/apple_agx_config_snapshot.c:47`, `apple_agx_regionc.c:131`; ADT 입력 `apple_agx_hwdata_profile.generated.h:1597-1599`; HwDataB 0xa78) | **0xbfffb8000** — m1n1이 같은 크기 영역을 잡음(`LOG/appark-chainload-1558.log:108`, Tahoe에서도 `LOG/usb-s159-20260909-234847.C3i6zw:68`) | 주소 [사실] / 용도 [가설, 높음] |
| 핸드오프(gfx-handoff) | 0x9fff70000, 0x4000 (:96-97) | **0xbfff70000** (같은 로그 :109, :69) | 주소 [사실] / 용도 [가설, 높음] |
| 공유 영역(gfx-shared-region) | 0x9fff78000, 0x40000 (:94-95) | 0xbfff78000로 둠 | [가설, 중간] 로그에 없음. 모름 — ADT 읽기 필요 |
| 중개 페이지(전원·구성·화면·예약 메모리 창) | 0x300000000, 0x1000 (:90-91), 예약 메모리 창 +0xD00 (`apple_agx_local_reserve_abi.h:6`) | 우리 하이퍼바이저(`m1n1-usb-s233`)에 없음(`grep hv_agx\|0x300000000` 결과 없음) | [사실] — 하이퍼바이저 담당 |
| 예약 메모리(GPU 전용 덩어리) | 하이퍼바이저가 크기를 정해 알려 줌. 64 MiB+72 MiB ~ 최대 1 GiB (`apple_agx_local_reserve_abi.h:13-25`) | RAM 크기에 고정되지 않음. 16 GB라 넉넉함 | [사실] 고정값 아님 |
| RAM 크기 8 GB vs 16 GB | 드라이버에는 RAM 끝 주소가 위 3개 영역 말고는 고정돼 있지 않음. 메모리 할당 비율은 실행 중 계산(`render-admission/tests/render_pool_demand_test.c:372` 주석) | 16 GB(RAM 0x800000000~, 로그 `Top of normal RAM: 0xbe1000000`) | [사실] |
| GPU 주소 공간 | 16 KiB 페이지, 40비트, 문맥 64개 (`j313_agx_g2.generated.h:13-29`) | 같은 GPU라 같음 | [가설, 높음] |
| 펌웨어 IO 매핑 10개(SoC 레지스터) | `apple_agx_firmware_io.h:33-38` (0x204d00000, 0x23b104000 등) | 같은 칩이라 같을 것. 모름 — ADT로 확인 | [가설, 높음] |

## 3. 인터럽트

| 항목 | J313 값(원본 위치) | J274 값 | 구분 |
|---|---|---|---|
| 정식 드라이버가 받는 인터럽트 | **딱 1개**, 화면 중개용 가짜 번호 889, Edge (`WOM/mu/.../J313AppleAgxAbiAdmission.asl.inc:43-44`; 검사 `AGX/render-admission/src/backend_platform_windows.c:2662`, `:75`) | 우리 vGIC는 "게스트 번호 = AIC 실제 번호" 일대일(`m1n1-usb-s233/src/hv_vgic.c:537`). 889가 실제 AIC 줄과 겹치지 않는지 하이퍼바이저 담당 확인 필요 | [사실] / 겹침 여부 모름 |
| GPU 실제 인터럽트 9개 | 563~566, 575~579 → 게스트 880~888 (`j313_agx_g2.generated.h:148`). 정식 드라이버는 안 받음. 옛 G2 ACPI(`J313AppleAgx.asl.inc:22-39`)만 씀 | 메일박스 4개 575~578은 같음(정적 FDT `recovered-j274.dts:565`, 0x23f~0x242). 563~566, 579는 모름 | 일부 [사실] / 나머지 모름 |
| SSD 중개 인터럽트 | 960 (하위 모듈 커밋 882985f 설명) | GPU와 무관 | [사실] |

## 4. 하이퍼바이저와의 약속(중개·HVC)

| 항목 | 원본 위치 | J274 상태 |
|---|---|---|
| 게스트 주소 → 실제 주소 변환 HVC `#0x4d31` (한 번에 최대 64쪽, 4 KiB 단위, 2단계 16 KiB) | `AGX/render-admission/include/hv_guest_ipa_pa_abi.h:7-14`, `src/arm64_hypercalls.asm:13` | 우리 하이퍼바이저에 없음 [사실, grep] |
| GPU 주소 준비 완료 HVC `#0x4d32` | 같은 파일 :8, asm :8 | 없음 |
| 전원 켜기/끄기 레지스터(중개 페이지 0x0~0x38, 명령 0 조회/1 켜기/2 끄기) | `j313_agx_g2.generated.h:122-141`, `CAP_FIXED_J313_DOMAINS` :124 | 없음. 그쪽은 ADT 이름 `/arm-io/sgx`, `/arm-io/gfx-asc`로 전원을 켬(`port/gpu.md` 2.1 인용) → J274에서도 거의 그대로일 것 [가설] |
| 구성 정보 창(성능 단계·GPU 영역 주소) | `AGX/shared/src/apple_agx_config_snapshot.c:15-75` | 없음. 값은 O1(아래 5절) |
| GPU 페이지 표 중개(v5 우편함 128/64바이트) | `AGX/shared/include/apple_agx_gpuva_broker_v5.h`, 하이퍼바이저 `hv_agx_gpuva_v5*.c` | 없음 |
| 화면 중개(+0x400, 능력 비트 `FIXED_J313` 필수) | `AGX/shared/include/apple_agx_scanout.h:12-38` | 없음. HDMI용 새 설계 필요 |

## 5. J313 ADT에서 떠 온 값 (J274는 모름 — 실기기에서 읽어야 함)

| 항목 | 원본 위치 | J274 |
|---|---|---|
| 성능 단계 7개, 기본 1, 최대 6, 측정 주기 8 ms | `apple_agx_config_snapshot.c:44-48`, `apple_agx_regionc.c:127-140` | 모름. 참고: 출처가 불분명한 정적 FDT에는 `apple,perf-base-pstate = <3>`(`recovered-j274.dts:531`) — J313의 1과 다름. 실제 ADT 값인지 확인 안 됨 [가설] |
| 전원·온도 제어 입력 66개, HwDataA/B, 프로필 ID | `apple_agx_hwdata_profile.generated.h` (ID :1505, 입력 :1611) | 모름. Mac mini는 팬이 있어 전원 한도가 다를 가능성 [가설] |
| RegionC 초기값 표, 지문 | `apple_agx_regionc.c:5-6`, `:54~` | 모름 |
| 전원 구역 30000/100/6875 | `apple_agx_hwdata_profile.generated.h:1600-1608` | 정적 FDT에도 같은 값(`recovered-j274.dts:548` `apple,power-zones`) — 출처 불분명 [가설] |

## 6. 화면(DCP)

| 항목 | J313(원본 위치) | J274 |
|---|---|---|
| 패널 | 내장 eDP 2560x1600, 줄 간격 10240, 화면 16 MiB (`apple_agx_scanout.h:44-50`) | HDMI 외부 모니터(`LOG/appark-chainload-1558.log:117` "Display is external") |
| UEFI 화면 넘겨받기 | 2560x1600x10240이 아니면 시작 실패 (`apple_agx_post_display_route.h:22-25` → `render-admission/src/lifecycle.c:465-475`) | 우리 UEFI 화면 1280x720(`plan-20261115/gpu.md:29`) → **정식 빌드는 여기서 실패** [사실, 코드 경로] |
| 모드 목록 | 패널 고정 모드 + 축소 모드(`render-admission/src/display.c:243-269`, `apple_agx_fixed_panel_modes.h`) | EDID 기반으로 새로 |
| 사용자 영역 쪽 | 2560x1600 관련은 추적·조건 비트 정도(`render-admission/umd/src/umd.c:165,1102`, `mesa/winsys/agx_d3d10_windows.cpp:1329`) | GPU 자체는 같아 대부분 그대로 [가설] |

## 7. 레지스트리·INF 기대

| 항목 | 원본 위치 | J274 |
|---|---|---|
| 대상 OS | `NTarm64.10.0...22000` (`AppleAgxRenderAdmission.inf:23-25`) | Windows 11 25H2 ARM64 — 맞음 |
| D3D 사용자 드라이버 이름(D3D9 칸은 `<>`, D3D9On12로) | `:70-71` | 그대로 |
| 32비트(WOW64) D3D·OpenGL 파일 필수 | `:53-62`, `:85-87`; 빌드 스크립트가 없으면 중단(`render-admission/scripts/build-driver.ps1`) | 첫 J274 시험에는 필요 없음. ARM64만 담은 INF 변형을 P9로 만듦(`/p:AppleAgxArm64Only=true`) |
| 64 KB 시스템 메모리 페이지 사용 | `HKR,MemoryManager,Supports64KBPages,1` (`:93`) | 그대로 |
| 테스트 서명 | `PnpLockdown=1`, 카탈로그 서명 필요 | 우리 Windows는 테스트 서명 모드 사용 중(`plan-20261115/gpu.md:158`) |
