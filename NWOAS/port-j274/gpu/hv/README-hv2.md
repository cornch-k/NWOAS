# GPU 하이퍼바이저 중개 2단계 (gpu-hv2)

**상태: 빌드와 이 맥북에서의 오프라인 시험만 했습니다. Mac mini에서는 한 번도 실행하지 않았습니다.**
빌드가 됐다고 GPU가 켜지는 것은 아닙니다. 지금 그대로 두 번째 단계(드라이버 연결)까지 가도 GPU 펌웨어는
**시작되지 않도록 막혀 있습니다**. 아래 "막힌 곳" 1번(J313용 전원·온도 값 표) 때문이고, 이것은 의도된 안전 정지입니다.

## 결론

1. windows-on-m1 하이퍼바이저의 GPU 중개 기능 전체를 새 트리 `m1n1-gpu-hv2`로 옮겼습니다. 옮긴 것: 드라이버 하이퍼콜(`hvc #0x4d31`, `#0x4d32`), 중개 페이지(0x300000000: 전원, 구성 정보, 펌웨어 주소표, Hwdata 확인, GPU 페이지 표 중개, 예약 메모리 영수증, 풀 창, 화면 창), GPU 영역(gpu-region) 보호, GPU 인터럽트 관찰.
2. `make NWOAS_AGX=1`일 때만 들어갑니다. 기본 빌드는 S233b와 **바이트 단위로 같습니다**(sha256 `d2eb62b5…` 재현, 배포본과 `cmp` 일치).
3. 새 빌드를 깔아도 **런처가 설정 명령을 보내지 않으면 아무것도 켜지지 않습니다**. 하는 일은 시작할 때 ADT를 읽어 기록하는 것뿐입니다.
4. Mu용 J274 GPU ACPI 장치는 `../acpi-j274/`에 있습니다(값은 13.5 실기기 ADT로 확인).

## 빌드 결과

| 바이너리 | 조건 | 크기(바이트) | sha256 |
|---|---|---|---|
| (보관 안 함) 기본 빌드 | NWOAS_AGX 없음, 태그 `…-s233B` | 2162688 | `d2eb62b53310dbd6c5b481a37bdbbf39aea5826f2bfb8d4a9c740cba3c529316` = S233b 배포본 |
| `out-hv2/m1n1-g247A-agx-hv2.bin` | `NWOAS_AGX=1`, 태그 `…-g247A` | 2260992 | `19740c6bc3d24b409673b2dd4fb4e06f264f9ea295339ad56bc6d4ea832fa95d` |

- 빌드 명령: `tools-hv2/buildvar.sh`(S233 안내서 (b)와 같은 명령, 매번 새 사본에서). GPU 빌드는 새 사본 두 개에서 각각 빌드해 결과가 같았습니다.
- 경고: 45개. S233b와 같은 목록입니다(새 코드에서 나온 경고 0개).
- 이미지가 S233b보다 96 KiB 큽니다(코드 + `root_owner` 24 KiB 등). 약 10 MB짜리 GPU 페이지 표 중개 상태는 이미지에 넣지 않고, 설치할 때 하이퍼바이저 힙에서 잡습니다.

## 각 부분이 하는 일과 출처

windows-on-m1 = `external/windows-on-m1`, `m1n1_windows` 하위 모듈 `882985fb`(파일에서 읽은 값), MIT.

| 부분 | 우리 파일 | 출처 | 하는 일 / 바꾼 점 |
|---|---|---|---|
| ADT 확인 | `src/nwoas_agx.c` | NWOAS(hv1) | 시작 시 ADT만 읽음. hv2에서 "DRAM 안", "카브아웃 하나 안에 온전히", "gfx-asc가 sgx 창 안", "인터럽트 중복 없음" 검사를 실제 거절 조건으로 바꿈(hv1 리뷰 결함 1) |
| 설정 명령 | `src/nwoas_agx_broker.c`, `src/proxy.[ch]` | NWOAS | 프록시 명령 `0xc38`로 런처가 512바이트 설정(동작 방식, 힙 한계, 게스트 RAM 설명, 예약 메모리 후보 주소 등)을 보냄 |
| 설치 | `src/nwoas_agx_broker.c`, `src/hv.c` | `hv_agx_power_mmio.c`의 `hv_agx_g2_resources_map` | `hv_start`에서 Python의 마지막 페이지 표 갱신 뒤에 설치. 게스트 시작 주소·힙 여유·중개 주소 겹침을 검사하고 하나라도 틀리면 거절 |
| 하이퍼콜 0x4d31/0x4d32 | `src/hv_guest_ipa_pa.[ch]`(원본 그대로), `src/hv_exc.c` | `hv_guest_ipa_pa.c`, `hv_exc.c` | 게스트 주소→실제 주소 변환(최대 64쪽). 원본처럼 ELR을 더하지 않음. RAM 범위는 런처가 준 게스트 RAM으로 좁힘 |
| 주소 변환 | `src/hv_vm.c` 끝에 추가 | `hv_vm.c`의 같은 함수 | 게스트가 준 주소를 먼저 범위 검사(원본은 검사 없이 assert 가능) |
| 전원 | `src/hv_agx_power_broker.c`, `src/hv_agx_power_platform.c`(원본 그대로) | 같음 | `/arm-io/gfx-asc` → `/arm-io/sgx` 순서로 켜고 반대로 끔. 13.5 ADT: pmgr GFX(99) 아래 GFX-ASC(316), GFX-SGX(314) |
| 구성 정보 창 | `src/hv_agx_config_snapshot.c` | 같음 | J313 고정값 비교를 구조 검사로 바꿈. J274 ADT에는 `gpu-perf-base-pstate`가 없어서, 런처가 명시적으로 값을 주지 않으면 거절 |
| 펌웨어 루트·주소표·Hwdata·페이지 표 중개 | `src/hv_agx_retained_platform.c` 외 | 같음 | 보드 검사를 J274(0x22)로, J313 실행 계약을 런처의 게스트 RAM 설명으로 바꿈. 큰 상태는 힙에 할당 |
| 예약 메모리(384 MiB, 영수증 판 3) | `src/hv_agx_local_reserve.c`(원본 그대로) | 같음 | 후보 주소는 런처가 줌. 정상 DRAM 카브아웃(13.5 ADT에서는 region-id-24) 안, 다른 카브아웃·GPU 영역·하이퍼바이저 메모리와 겹치지 않음, 1:1 매핑 확인 |
| 풀 창(4 GiB) | `src/hv_agx_pool_window.[ch]`(원본 그대로) | 같음 | 주소를 런처가 줄 때만 켬(0이면 꺼짐) |
| 화면 창 | `src/hv_agx_scanout_broker.[ch]`(원본 그대로) | 같음 | J313 내장 패널용 화면 서비스는 **옮기지 않음**. 능력 비트를 0으로 두어 레지스터만 응답 |
| GPU 영역 보호 | `hv_agx_retained_gpu_region` | 같음 | GPU 영역 16 KiB를 훅으로 바꿔 허용된 칸만 쓰게 함 |
| 인터럽트 | `src/hv_exc.c`, `src/hv_vgic.c` | NWOAS | 9개 GPU 인터럽트는 우리 vGIC가 이미 "게스트 번호 = AIC 번호"로 넘김(사실). 새로 한 것은 9개 도착 횟수 기록(제한 있음)과, 가짜 화면 인터럽트 번호(889)를 AIC로 보내지 않게 막은 것 |

기본 빌드가 같게 유지되도록, 기존 파일(hv_exc.c, hv_vgic.c)의 호출은 같은 줄에서 매크로로 바꿨고(`src/nwoas_agx_hooks.h`), hv_vm.c는 파일 끝에만 덧붙였습니다. assert가 줄 번호를 기록하기 때문입니다.

## 동작 방식(설정 명령의 mode)

| mode | 하는 일 | 하지 않는 일 |
|---|---|---|
| 0 / 명령 없음 | ADT 확인 기록만 | 그 밖에 아무것도 |
| 1 LOG | 중개 페이지를 "없는 장치"로 설치(읽으면 0, 쓰기 무시, 접근 64줄까지 기록). 하이퍼콜은 INVALID로 답하고 기록. GPU 인터럽트 횟수 기록 | GPU 전원, GPU 영역 훅, 예약 메모리 |
| 2 MEDIATE | 위 표 전체 | 화면 출력(옮기지 않음) |

## 런처에서 바꿔야 할 것 (새 런처 복사본, 기존 런처는 그대로)

| 항목 | 기존(fw135-s244) | 새 런처 |
|---|---|---|
| HV 크기 고정 | 2162688 | 2260992 |
| HV 해시 고정 | `d2eb62b5…` | `19740c6b…` |
| `-m` 모듈 | — | `port-j274/gpu/hv/tools-hv2/agx_hv2_module.py`를 `layout-s231/guest_module.py`·`layout-s231/module.py` 뒤에 추가하고 그 해시도 고정 |
| 환경 변수 | — | `NWOAS_AGX_MODE=log`(1단계) / `mediate`(2단계), 2단계는 `NWOAS_AGX_LOCAL_IPA` 필수 |
| 페이로드 | S237 | 1단계: S237 그대로. 2단계: ACPI AGX0 + 예약 메모리 처리가 든 시험용 Mu(`../acpi-j274/README.md`) |

`agx_hv2_module.py`는 **초안**입니다(문법 검사만 함). 게스트 RAM 끝·저주소 창 계산은 `layout-s231/guest_module.py`를 따랐지만 실기기에서 확인하지 않았습니다.

## 실기기 단계 계획 (각 단계는 앞 단계 통과 뒤에만)

1. **기록만 하는 단계(mode 1, 페이로드 S237 그대로, GPU 드라이버 설치 안 함).**
   기대 로그: `NWOAS-AGX: probe OK: gpu-region 0xbfffb8000+0x4000 … 5 sgx + 4 gfx-asc irqs, DRAM owner region-id-24`,
   `NWOAS-AGX: config accepted: mode=1 …`, `NWOAS-AGX: LOG mode armed …`.
   통과 기준: Windows가 S233b 때와 똑같이 바탕화면까지 감(USB·NVMe·화면 동일), `LOG broker` 줄 0개(아직 드라이버가 없으므로), `REFUSED` 줄 없음.
   여기서 다르면 이미지가 96 KiB 커진 영향이거나 설치 단계 문제입니다.
2. **드라이버 연결 단계(mode 2, 시험용 Mu, 펌웨어 시험 빌드 드라이버).**
   기대: `local reserve v3 IPA=… bytes=0x18000000`, `retained Hwdata input profile matched=0`, `MEDIATE armed …`.
   드라이버는 장치에 붙고 전원 요청(`power receipt … state=3`)까지 갈 수 있지만, **펌웨어 시작은 거절됩니다**(아래 막힌 곳 1). 이것이 이 단계의 정상 결과입니다.
3. J274용 Hwdata 값 표를 13.5 ADT로 다시 만든 드라이버·하이퍼바이저 쌍이 생긴 뒤에야 펌웨어 시작(`matched=1`)을 시험합니다.

## 오프라인 시험 (이 맥북, 실기기 없음)

`tools-hv2/hosttest/run.sh <adt.bin> <출력 파일>`: hv2의 원본 그대로인 순수 논리 파일을 이 맥북에서 컴파일해 실제 ADT에 돌립니다.
결과: `out-hv2/hosttest-adt135.txt`(13.5 실기기 ADT), `out-hv2/hosttest-adt-tahoe.txt`(Tahoe ADT). 둘 다 **전부 통과**, 두 결과는 정상 DRAM 카브아웃 한 줄만 다릅니다.

| 시험 | 결과 |
|---|---|
| sgx·gfx-asc 주소, gfx-asc가 sgx 창 안 | 통과(0x204000000+0x4000000 안에 0x206400000+0x6c000) |
| GPU 인터럽트 9개 | 통과(sgx 563 564 565 566 579, gfx-asc 576 575 578 577) |
| 889·900이 ADT 어느 장치에도 없음 | 통과 |
| GPU 영역 3개가 각각 카브아웃 하나 안 | 통과(1/1/1) |
| 구성 정보 창: 값 없이 거절, 값 1을 주면 성립, GPU 영역이 다르면 거절 | 통과. 성능 단계 7개·표 1·최대 6·주기 8 ms(J313과 같음) |
| Hwdata 입력 66개 중 J313과 다른 것 | 3개: `perf-states`(값 다름), `gpu-region-base`(주소 다름), `gpu-perf-base-pstate`(없음) → 펌웨어 시작 거절 확인 |
| 하이퍼콜 0x4d31(성공, 65쪽 거절, 정렬 틀림, 매핑 없음, RAM 아님), 0x4d32 | 통과 |
| 전원 켜기·끄기 순서 | 통과(gfx-asc 켜기 → sgx 켜기 → sgx 끄기 → gfx-asc 끄기) |
| 예약 메모리 영수증(magic, 판 3, 1:1), 겹침 거절 | 통과 |
| 화면 창 능력 0, 인터럽트 상태 0 | 통과 |
| 설정 구조체 배치(512바이트, 필드 위치) | m1n1 대상 컴파일러로 `_Static_assert` 통과(`tools-hv2/layout_check.c`) |
| ACPI SSDT 컴파일 | iasl 오류 0(`../acpi-j274/README.md`) |
| 바이너리 | 기본 빌드 = S233b, GPU 빌드 두 번 동일, 새 함수 링크 확인(`out-hv2/symbols-g247A.txt`) |

## 막힌 곳과 위험 (중요한 순서)

1. [사실] g247A는 Hwdata 값 표가 J313용이라 펌웨어 시작이 막힙니다(J274 ADT와 3개 다름). **g247B는 J274 표를 써서 이 막힘이 풀립니다**(오프라인 66개 일치). 드라이버도 같은 J274 표로 빌드돼야 합니다.
2. [사실] `gpu-perf-base-pstate`가 13.5 ADT에도 없습니다. 하이퍼바이저는 런처가 값을 줘야만 구성 정보를 내놓고, 드라이버 쪽 해독기(J313용)는 1을 기대합니다. 올바른 J274 값은 모릅니다 [모름].
3. [가설] 이미지가 96 KiB 커진 것과 mode 2 설치 때의 힙 사용이 S231 배치와 충돌하지 않는다. 힙 사용(이 맥북에서 구조체 크기로 계산): 풀 창 없이 약 14 MiB(GPU 페이지 표 중개 상태 10.0 MiB + 여유 4 MiB), 풀 창을 켜면 약 47 MiB(+ 페이지 표 22.8 MiB, 창 표 4.1 MiB). m1n1은 자기 힙으로 128 MiB를 남겨 두지만 이미 쓰고 있는 양은 모릅니다. 설치 전에 런처가 준 한계로 여유를 검사하고 모자라면 거절합니다. 1단계 기록으로 확인합니다.
4. [가설] 예약 메모리 후보 주소를 Mu가 실제로 예약해 Windows가 쓰지 않는다. Mu 변경은 시험용 페이로드(`nwoas_scripts/fw-agx-s247`)로 빌드했지만 부팅하지 않았습니다.
5. [가설] 중개 페이지·GPU 영역 훅이 Python의 이후 페이지 표 갱신으로 덮이지 않는다. `hv_start`에서 설치하므로 시작 전 갱신은 안전하지만, 실행 중 모듈이 새로 매핑하면 덮일 수 있습니다.
6. [사실] 화면 서비스는 옮기지 않아 GPU로 그린 화면은 나오지 않습니다. 정식(전체) 드라이버는 2560x1600 패널 검사에서 멈춥니다(드라이버 갈래 문서).
7. [가설, 원본 저자 가설] VMID 0 때문에 GPU 주소 변환 캐시가 자주 비워져 느려질 수 있음.

## 정리

- 설치한 것 없음. 빌드 사본·시험 실행 파일은 스크래치 폴더에서 지웠습니다.
- 파일 지문: `MANIFEST-hv2.sha256`, hv1과의 차이: `out-hv2/src-hv1-vs-hv2.diff`, `out-hv2/changed-files-hv1-vs-hv2.txt`.

---

## 갱신 2026-11-16 오후: 1단계 결과와 2단계 준비

### 1단계 결과 (실기기, 코디네이터 보고) [사실]

런처 `nwoas_scripts/agxlog-s247-guest-test.sh`(g247A, LOG). 로그 `nwoas_scripts/logs/agxlog-s247-20261116-120236.DbjqNp`:
`config mode=1 ret=1`, `install: mode=1 entry=0x83dec0800`, `LOG mode armed`, Windows 바탕화면 도달.

`probe OK` 줄이 없었던 이유 [사실]: 설치 함수는 ADT 확인이 통과하지 않으면 거절하므로 `LOG mode armed`가 찍혔다는 것 자체가 통과를 뜻합니다.
ADT 확인은 `hv_init`에서 도는데, 그때는 Python이 진단 출력 통로(VUART)를 아직 켜지 않아 출력이 버려집니다(같은 때 찍히는 vGIC 준비 줄도 두 로그에 모두 없음).
g247B부터는 설치할 때 `NWOAS-AGX: probe OK (hv_init): …` 요약을 다시 찍습니다.

### 2단계용 새 하이퍼바이저 g247B

| 바이너리 | 바뀐 점 | 크기(바이트) | sha256 |
|---|---|---|---|
| `out-hv2/m1n1-g247B-agx-hv2-j274hwdata.bin` | J274 Hwdata 값 표(`port-j274/gpu/hwdata-j274`, sha256 `ebf8e666…`) 사용, 설치 때 확인 결과 다시 출력 | 2260992 | `2d70d7f15ee405235b8d0141d6d1ebf16a1f83b2d68963c226e4f9fec890c20e` |

- 새 사본 두 개에서 빌드해 같음. 기본 빌드는 여전히 S233b와 같음(`d2eb62b5…`). g247A 파일과 1단계 모듈은 바꾸지 않았습니다(1단계 런처 고정값 유지).
- g247B 안에는 J274 프로필 ID만 있고 J313 ID는 없습니다(바이너리 검색, 사실). 오프라인 시험에서 J274 표는 13.5 ADT·Tahoe ADT와 66개 모두 일치합니다(`out-hv2/hosttest-adt135.txt`).

### gpu-perf-base-pstate = 3 [사실, 출처]

| 출처 | 값 |
|---|---|
| Asahi Linux `arch/arm64/boot/dts/apple/t8103-j274.dts`(asahi 브랜치 `ada43326…`) | `&gpu { apple,perf-base-pstate = <3>; }` — Mac mini 전용 덮어쓰기 |
| Asahi `t8103.dtsi`(같은 커밋) | SoC 기본 `<1>`(J313 등) |
| Asahi 드라이버 `drivers/gpu/drm/asahi/hw/mod.rs`(`1340031e…`) | DT 값이 없을 때 기본 1 |
| m1n1 `proxyclient/m1n1/fw/agx/initdata.py:704` | ADT 값이 없을 때 기본 3 |
| J313 ADT(windows-on-m1 fixture) | 1 |

J274 ADT에는 이 값이 없습니다. 2단계 런처는 `NWOAS_AGX_BASE_PSTATE=3`을 줍니다.
J313용 드라이버 해독기는 1을 기대하므로 J274 드라이버도 3을 받아들여야 합니다(드라이버 갈래 확인 필요).

### 예약 메모리 위치: `NWOAS_AGX_LOCAL_IPA=0xA00000000`

| 근거(1단계 로그, 사실) | 값 |
|---|---|
| 게스트 고주소 RAM | 0x83CE10000..0xAE0FCC000 |
| 저주소 창 뒷받침 메모리 | 0xAE0FCC000..0xBE0FCC000 |
| 핸드오프 복사본 | 0x840000000 + 0x5C000 |
| 화면 버퍼 | 0xBE3F60000(게스트 RAM 밖) |
| 정상 DRAM 카브아웃 | region-id-24 0x801224000 + 0x3dfddc000 |

0xA00000000..0xA18000000(384 MiB): 64 KiB 정렬, region-id-24 안, 위 영역 어디와도 겹치지 않음, 뒷받침 메모리보다 약 3 GiB 아래.
하이퍼바이저가 1:1 매핑·일반 메모리 속성·카브아웃을 다시 검사하고, 시험용 Mu가 PEI 단계에서 이 범위를 예약해 Windows가 쓰지 못하게 합니다.
[가설] 게스트 시작 주소(Python 힙 끝)가 부팅마다 같다. 1단계 값 기준이며, 다르면 하이퍼바이저나 Mu가 거절합니다.

### 2단계 로그 판정

| 단계 | 통과 줄 | 실패(멈춤) 줄 |
|---|---|---|
| 설정 | `HVLOG: NWOAS-AGX config mode=2 ret=1 … local=0xa00000000 … vintid=889` | `ret=0` 또는 `config REFUSED` |
| 설치 | `NWOAS-AGX: probe OK (hv_init): gpu-region 0xbfffb8000+0x4000 …`, `local reserve v3 IPA=0xa00000000 PA=0xa00000000 bytes=0x18000000`, `retained Hwdata input profile matched=1`, `retained-root broker v4 root=0xbfff78000 size=0x40000`, `MEDIATE armed: … config=1 local=1 pool=0` | `install REFUSED`, `local reserve unavailable`, `matched=0`, `retained-root broker unavailable`, `config=0` 또는 `local=0` |
| Mu | `HVLOG: S247 AGX local reserve reserved base=a00000000 bytes=18000000`, 기존 `S237 reserved PCIe bridge window`, `S216 reserved handoff` | `S247 … absent`(MEDIATE인데 영수증 못 읽음), `S247 … REFUSED` |
| Windows | 바탕화면 도달(1단계와 같게). 장치 관리자에 `ACPI\APPL0002`가 보임 | 바탕화면 전 멈춤 → 2단계 중단, 1단계 런처로 되돌림 |
| 드라이버 설치 뒤 | `power receipt seq=… cmd=1 state=3 result=0`, `retained op=1 … status=0`(PREPARE), `retained root ACTIVE`, `GPUVA broker v5 init=0` | `broker REFUSED`/`gpu-region REFUSED` 줄 반복, `power receipt … result=4`, `retained op=1 … status≠0` |

드라이버가 없을 때 2단계 통과 기준은 "설정·설치·Mu 줄이 통과이고, Windows가 1단계처럼 바탕화면까지 감"입니다.
펌웨어 시작 성공 여부는 드라이버(`Wom1FirmwareQualification*` 레지스트리 기록)와 위 마지막 줄로만 판단합니다. 하이퍼바이저 빌드 성공은 그 판단 근거가 아닙니다.

### g248C — 풀 창 경로에서 하이퍼바이저가 죽던 것 고침 (2026-11-16)

| 바이너리 | 바뀐 점 | 크기(바이트) | sha256 |
|---|---|---|---|
| `out-hv2/m1n1-g248C-agx-hv2-poolguard.bin` | `hv_agx_gpuva_v5.c` `backing_find`: 색인 표가 NULL이면 NULL 반환(`hv2-g248C-poolguard.patch`) | 2260992 | `684cb6277680150531aa713b5833273395cab17270ad82bd047ec3300e972175` |

- [사실] g247B + 풀 창(`agxmedp-s247`)에서 GPU 장치를 끈 직후 하이퍼바이저가 PC 0x580a4(`backing_find`, `handle_broker_locked`에 인라인)에서 NULL 역참조로 죽음. 직전 로그는 `power receipt seq=3 cmd=2 state=0`, 레지스터상 색인 포인터 0, slot bits 0. AGX_RR_CLOSE가 `gpuva_v5` 전체를 0으로 지운 뒤 풀 창 명령이 `hv_agx_retained_platform_page_busy` → `backing_find`를 부름.
- 빌드: `tools-hv2/buildvar.sh … v1.0.2-1474-gbddf7f06-g248C NWOAS_AGX=1`. 실행 스크립트 `nwoas_scripts/agxmedq-s248-guest-test.sh`(풀 창 사용). **실기기 미검증.**
