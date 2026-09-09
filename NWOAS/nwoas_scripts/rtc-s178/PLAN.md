# S178 — RTC 정확성 읽기 전용 조사 및 단계별 계획 (2026-09-10)

대상: 콘치님 소유 Mac mini M1 (J274, Macmini9,1) NWOAS 프로젝트.
이 세션은 **조사 전용**입니다. 하드웨어 명령, SMC 쓰기, PMU offset 쓰기, 빌드, 활성 파일 수정은 하지 않았습니다.
이 디렉토리(`nwoas_scripts/rtc-s178/`) 밖의 파일은 읽기만 했습니다.

---

## 0. 결론 요약

1. **현재 UEFI 시계는 실제 RTC와 무관합니다.** 활성 라이브러리
   `AppleSiliconPkg/Library/VirtualRealTimeClockLib/VirtualRealTimeClockLib.c`는
   Year=2019, Month=1을 고정하고 Day/Hour/Min/Sec를 CNTPCT 업타임에서 만듭니다.
   부팅 직후 Day=0이므로 **EFI_TIME 규격 위반(Day는 1..31)**이며, SetTime은 EFI_UNSUPPORTED입니다.
   Windows가 매 부팅 2022-05-07을 보고하는 것은 이 잘못된 GetTime을 버리고 자체 기본값으로
   대체하기 때문이라는 것이 가장 단순한 가설입니다(미검증).
2. **실제 하드웨어 RTC 경로는 확정됐습니다(소스·ADT 근거).**
   - Linux `rtc-macsmc.c`: `SMC 키 "CLKM" 6바이트 + PMU nvmem "rtc_offset" 6바이트`,
     48비트 부호확장 후 `>> 15` = Unix 초(32768 Hz).
   - Linux `t8103.dtsi`: `spmi@23d0d9300` (`apple,t8103-spmi`), `pmic@f` (`apple,spmi-nvmem`),
     `rtc_offset: rtc-offset@d100 { reg = <0xd100 0x6>; }`, `rtc { compatible = "apple,smc-rtc"; }`.
   - 로컬 ADT 덤프(`experiments/tahoe-afk-20260906/current.adt`, J274AP):
     `/arm-io/nub-spmi` reg0 = 0x23d0d9300 len 0x100 (Linux와 동일),
     `/arm-io/nub-spmi/spmi-pmu` (compatible `pmu,spmi|pmu,sera`, is-primary=1, slave id 0xf):
     `info-rtc = 0xd002`, `info-rtc_scrpad = 0xd100`, `info-clock_offset = 0x6_0000_d100`(주소 0xd100, 길이 6),
     `info-rtc_alarm_ctrl = 0xd000`, `info-rtc_alarm_offset = 0xd008`, `info-rtc_alarm_event = 0xd00c`,
     `info-rtc_irq_mask_offset = 0xd00e`.
   - 즉 offset 셀(0xd100, 6바이트)은 Linux와 ADT가 정확히 일치합니다. 카운터는 Linux가 SMC 경유(CLKM)로
     읽고, ADT는 PMU 레지스터 0xd002를 `info-rtc`로 가리킵니다. **CLKM == PMU 0xd002 라는 것은 가설**이며
     하드웨어에서 양쪽을 읽어 비교해야 확정됩니다.
3. **부팅 시드(seed) 전달 경로가 코드 수정 없이 존재합니다.** run_guest.py 모듈(-m)은
   `hv.load_raw()` 이후, `hv.start()` 이전에 실행되고, `hv.adt`(Python 사본)는 start 시 게스트 메모리에
   직렬화되어 게스트 m1n1 `boot_args.devtree` → UEFI `PrePi/AdtParser.c`가 `PcdAdtPointer`(0x840004000)로 복사 →
   `AppleDTLib dt_get_prop()`로 읽힙니다. 오프라인 왕복 테스트로 `/chosen`에 32바이트 raw 속성을 추가·재직렬화·재파싱하는
   것을 확인했습니다(아래 5절).
4. **권고 경로:** (A) 호스트 Python 모듈이 SPMI **읽기**로 PMU 0xd002/0xd100을 스냅샷하고 CNTPCT와 함께 ADT `/chosen`에
   싣는다 → (B) 새 UEFI RealTimeClockLib이 그 시드 + CNTPCT 차분으로 **정직한 UTC GetTime**을 제공한다(SetTime은 계속
   UNSUPPORTED 또는 휘발성 보정) → (C) 완전 네이티브 RTC 드라이버(UEFI 런타임 SPMI 접근 또는 Windows 드라이버)는 별도 단계.
   (A)+(B)는 "부팅 시드"이지 "네이티브 RTC"가 아님을 문서·로그·Capabilities에 명시합니다.

---

## 1. 현재 UEFI 런타임 사실 (빌드 리포트 2026-09-10 03:15 기준)

| 항목 | 사실 | 근거 |
|---|---|---|
| RTC DXE | `EmbeddedPkg/RealTimeClockRuntimeDxe` (RealTimeClock.efi) | `Build/.../BUILD_REPORT.TXT` 2852행 |
| RealTimeClockLib 오버라이드 | `AppleSiliconPkg/Library/VirtualRealTimeClockLib` (모듈 스코프 오버라이드) | `AppleSiliconPkg.dsc.inc` 610-613행; 전역 기본은 239행 EmbeddedPkg 버전 |
| GetTime 내용 | Year 2019/Month 1 고정, Day=uptime/86400(부팅 직후 0), TimeZone 0, Nanosecond 0 | 로컬 `VirtualRealTimeClockLib.c` |
| SetTime / Wakeup | 모두 EFI_UNSUPPORTED | 동일 파일 |
| 카운터 | `TimerLib=ArmArchTimerLib`, `GetPerformanceCounter()` = `ArmGenericTimerGetSystemCount()` = **CNTPCT_EL0**, 주파수 = **CNTFRQ_EL0** (PCD 0 → 레지스터 읽기) | `ArmArchTimerLib.c` 132-138행, `AppleArmGenericTimerPhyCounterLib`, `T810XFamilyPkg.dsc.inc` 11-12행 |
| 게스트 EL1의 CNTPCT 접근 | m1n1 HV가 ECV 유무 모두에서 `CNTHCTL_EL1PCTEN` 설정 → 트랩 없이 읽힘. `CNTPOFF_EL2` 사용 흔적 없음. `CNTVOFF_EL2 = stolen_time`은 **가상 카운터(CNTVCT)에만** 영향 | `m1n1_windows/src/hv.c` 124-133행, `hv_exc.c` 1820행 |
| NV 변수 | `PcdEmuVariableNvModeEnable=TRUE` → 부팅 간 **비영속**. 업스트림 EmbeddedPkg VirtualRealTimeClockLib(RtcEpochSeconds 변수+BUILD_EPOCH)로 바꿔도 매 부팅 빌드 시각으로 되돌아감 | `AppleSiliconPkg.dsc.inc` 94-96행 |
| 시간 유효성 | `TimeBaseLib IsTimeValid`: Year 2000..2099, Month 1..12, Day 유효, TZ -1440..1440 또는 2047 | `TimeBaseLib.c` 269-290행 |
| ADT 접근 | `AppleDTLib`(BASE, `FixedPcdGet64(PcdAdtPointer)`), DXE 드라이버 5종이 이미 `dt_get_prop` 사용 | `AppleDTLib.inf/.c`, `AppleSiliconPkg.dec` 70행 |

**런타임 주의:** `PcdAdtPointer` 영역은 부트 서비스 메모리이므로 RealTimeClockLib은 `LibRtcInitialize`(DXE 진입, ExitBootServices 이전)에서
값을 자체 전역으로 **복사**해야 하며, 런타임 GetTime에서 ADT 포인터를 역참조하면 안 됩니다. CNTPCT/CNTFRQ는 시스템 레지스터라
런타임(Windows 컨텍스트)에서도 읽을 수 있습니다.

## 2. m1n1 로컬 API 현황

| API | 상태 | 비고 |
|---|---|---|
| `src/smc.c` C 드라이버 | `smc_init / smc_write_u32 / smc_shutdown`만 존재. **읽기 함수 없음** | dcp.c가 HDMI 전원 GPIO 쓰기에만 사용 후 shutdown. 읽기를 추가하려면 `SMC_READ_KEY(0x10)` + 크기(비트 23:16) + 결과 ≤4바이트는 `VALUE` 필드, 그 이상은 shmem에서 복사(proxyclient `fw/smc.py` `read()`와 동일 로직) |
| `src/` SPMI C 드라이버 | **없음** (`usb.c`의 M3 경로는 hpm 노드 존재 확인만) | T8103은 `cpufreq.c`의 `pmgr_power_on("SPMI")` 대상도 아님(T8012/T8015 한정) |
| `proxyclient/m1n1/hw/spmi.py` | 컨트롤러 MMIO(STATUS 0x00, CMD 0x04, REPLY 0x08) 기반 `read(slave, reg, size)` = `CMD_EXT_READL(0x38)`; Linux `spmi-apple-controller.c`와 동일 프로토콜 | 읽기 명령 발행 시 컨트롤러 CMD 레지스터에 명령어 word를 씀(버스 트랜잭션은 READ). PMU 레지스터 쓰기는 없음 |
| `proxyclient/m1n1/hw/pmu.py` | `find_primary_pmu()`가 `nub-spmi*/… compatible pmu,spmi + is-primary` 탐색 → J274에서 `/arm-io/nub-spmi/spmi-pmu` | `reset_panic_counter()`는 **쓰기**이므로 사용 금지 |
| `proxyclient/m1n1/fw/smc.py` | `SMCClient` + `read(key,size)`; `tools/smccli.py` 예제 | 호스트에서 RTKit SMC 엔드포인트를 띄우는 방식. 게스트 m1n1(dcp.c)도 SMC를 초기화/종료하므로 **동시 사용 충돌 위험** → 1차 경로로 비권장 |
| `u.mrs("CNTPCT_EL0")`, `u.mrs("CNTFRQ_EL0")` | `.venv-hv`에서 `sysreg_parse` 확인 (3,3,14,0,1)/(3,3,14,0,0) | 호스트 EL2에서 읽는 물리 카운터. 게스트 EL1 CNTPCT와 동일 값(오프셋 없음) |
| 게스트 ADT 편집 | `hv.adt[...]._properties[name] = bytes` 후 `adt.build()` 왕복 성공 | `usb-s174/guest_map.py`처럼 -m 모듈에서 실행 가능 |

페이로드 구조(확인): `m1n1-payload-*.bin` = 게스트 m1n1(0x0..) + FDT(0x140000, 64 KiB, `apple-j274-padded.dtb`) + UEFI FD(0x150000, ARM64 Image 헤더).
게스트 m1n1 `payload_run()` → `kboot_boot()` → x0 = FDT, x4 = `&cur_boot_args`(Apple boot_args, devtree = hv가 직렬화한 ADT).
UEFI `ModuleEntryPoint.S`는 x4를 boot args로 받아 `AdtParser.c`가 ADT를 복사합니다.

## 3. 참조 소스 (이 디렉토리에 curl로 내려받음, 읽기 전용)

`linux-ref/`: `rtc-macsmc.c`, `apple-spmi-nvmem.c`, `spmi-apple-controller.c`, `macsmc.c`, `macsmc.h`,
`t8103.dtsi`, `t8103-jxxx.dtsi`, `t8103-j274.dts` (torvalds/linux master).
OpenBSD `aplpmu.c`(PMU 직접 RTC 읽기 구현으로 알려짐)는 GitHub 미러·cvsweb 모두 404로 **내려받지 못했습니다**.
따라서 "PMU 0xd002 직접 읽기"는 ADT `info-rtc` 값과 Linux 설정 셀 주소 일치에 근거한 가설로 남깁니다.

## 4. 날짜 변환 요구사항 정리

- RTC 값 → Unix 초: `sext48(ctr + off) >> 15` (`rtc_math.py`로 오프라인 검증: set_time 역연산, 48비트 wrap, 음수 offset).
- UEFI: Unix 초 → `EpochToEfiTime()`(TimeBaseLib, 1970 기준, 윤년 처리) + Nanosecond = 나머지 tick × 1e9 / CNTFRQ.
- `TimeZone = EFI_UNSPECIFIED_TIMEZONE(2047)` 또는 0(UTC). 업스트림 EmbeddedPkg는 2047 기본. Windows는 EFI 시각을 UTC/로컬 중
  레지스트리 `RealTimeIsUniversal` 정책으로 해석하므로 **UTC 기준 초를 그대로 주고 TZ=2047**가 가장 덜 놀랍습니다.
- `Capabilities`: Resolution=1, Accuracy=0(또는 수정 진동자 급 50000 ppb 정도의 정직한 값), SetsToZero=FALSE.
- 유효 범위: Year 2000..2099. 시드 부재 시 **현행 동작 유지**(회귀 없음)가 보수적 선택입니다. 향후 Day=0 문제만 별도 수정 가능.
- CNTFRQ는 M1에서 24 MHz. 64비트 CNTPCT는 수천 년 뒤 wrap → 무시 가능. 크리스털 드리프트(수십 ppm)는 수 시간 세션에서 초 미만.

## 5. 오프라인 검증 완료 항목 (이 세션)

| 검증 | 결과 |
|---|---|
| `adt_rtc_probe.py`(독립 ADT 파서)로 `current.adt` 파싱 | `/arm-io/nub-spmi/spmi-pmu` `info-rtc=0xd002`, `info-rtc_scrpad=0xd100`, `info-clock_offset=0x60000d100`, sid 0xf 확인 |
| `.venv-hv` m1n1 `load_adt → /chosen 속성 추가(32 B) → build() → load_adt` | 원본 376832 B(패딩 포함) → 재구성 363628 B, 속성 바이트 동일(`eq True`) |
| `u.mrs` 레지스터 이름 파싱 | `CNTPCT_EL0`, `CNTFRQ_EL0` 인코딩 정상 |
| `rtc_math.py` 셀프테스트 | 통과 (실행 결과는 세션 보고 참조) |
| `rtc_snapshot_module.py` | 문법 컴파일만 확인. **어떤 런처에도 연결하지 않음.** 기본 DRY RUN |

## 6. 단계별 계획 (경계 명시)

### Phase A — 읽기 전용 프리부트 RTC 스냅샷 (호스트 Python 모듈, m1n1/UEFI C 변경 없음)

파일: `rtc_snapshot_module.py`(초안 작성됨). 실행 조건: `NWOAS_RTC_SNAPSHOT=1`일 때만 하드웨어 접근.

1. 호스트 ADT에서 `/arm-io/nub-spmi` reg0, `/arm-io/nub-spmi/spmi-pmu` reg0(sid), `info-rtc`, `info-rtc_scrpad`를 읽어 하드코딩을 피합니다.
2. `CNTPCT_EL0` → SPMI `EXT_READL` 6 B @0xd002 → 6 B @0xd100 → `CNTPCT_EL0`. 읽기 창(두 CNTPCT 차)을 기록합니다.
   - 성격 명시: PMU/SMC 쓰기 없음. 컨트롤러 CMD FIFO에 읽기 명령 word를 쓰는 것이 유일한 MMIO 쓰기입니다.
   - 사전 조건: nub-spmi는 Linux DT에 power-domains가 없는 상시 전원 도메인(가정). RX FIFO 잔여물은 `SPMI.read()`가 먼저 비웁니다.
3. `rtc_math.rtc_to_epoch` → 2024-01-01..2036-01-01 창 검사. 불합격이면 ADT 미변경 + 로그.
4. 합격 시 `/chosen "nwoas,rtc-snapshot"` = `<u32 magic 0x4E525443, u32 ver 1, u64 epoch, u64 cntpct_mid, u32 cntfrq, u32 flags>` 32 B.
5. 항상 `rtc-snapshot.json`(raw hex, 두 CNTPCT, 계산 UTC, 호스트 UTC, 차이 초)을 남깁니다.

**하드웨어 검증 방법(콘치님 실행, 미래 세션):**
- 1차: `NWOAS_RTC_SNAPSHOT=1`로 기존 게스트 런처에 `-m rtc-s178/rtc_snapshot_module.py`만 추가한 **제어 실행**.
  판정: `delta_vs_host_s`가 ±2초 이내(맥북 시계가 NTP 동기라는 전제). 상수 차이(예: 정확히 시간대 오프셋)가 나면 0xd002 해석 가설을 재검토.
- 2차(선택, 읽기 전용): `smccli.py`로 `CLKM` 6 B를 읽어 0xd002와 비교 → "CLKM == PMU 0xd002" 가설 확정/기각. 게스트가 SMC를 쓰지 않는 시점(게스트 시작 전)에만.
- 부팅·Windows 동작에 어떤 변화도 기대하지 않습니다(UEFI 소비자가 아직 없음). 회귀 여부만 확인.

### Phase B — 스냅샷 기반 정직한 UEFI GetTime (새 라이브러리, 후보 빌드에서만)

새 디렉토리 예: `AppleSiliconPkg/Library/NwoasSeedRealTimeClockLib/` (후보 빌드 시 s131_builder 방식으로 파일 교체 후 복원; 활성 트리 직접 수정 금지).

- `LibRtcInitialize`: `dt_get_prop("/chosen", "nwoas,rtc-snapshot")` → magic/ver/flags 검증 → 전역에 복사, `DEBUG` 출력
  (`epoch`, `cntpct`, `cntfrq`, 현재 CNTPCT). 시드 없음/무효 → **현행 VirtualRealTimeClockLib 동작을 그대로 수행**(회귀 방지).
- `LibGetTime`: `delta = GetPerformanceCounter() - seed.cntpct`; `sec = seed.epoch + delta / cntfrq`; `EpochToEfiTime`; ns 계산;
  `TimeZone = EFI_UNSPECIFIED_TIMEZONE`, `Daylight = 0`; Capabilities Resolution 1 / SetsToZero FALSE.
  주파수는 시드의 cntfrq가 아니라 게스트 `CNTFRQ_EL0`를 우선(동일해야 하며 다르면 DEBUG 경고).
- `LibSetTime`: Phase B에서는 **EFI_UNSUPPORTED 유지**(부팅 시드임을 정직하게 표시). 대안으로 휘발성 RAM 보정만 허용하는 옵션을
  플래그로 남기되 기본 off. PMU 0xd100 쓰기(Linux set_time 방식)는 Phase C 이전에는 절대 하지 않습니다.
- `LibRtcVirtualNotifyEvent`: 포인터 없음(전역 값만) → 변환 불필요. ADT 포인터를 런타임에 보관하지 않습니다.
- Wakeup: UNSUPPORTED 유지.

**빌드/검증(미래 세션):** 후보 FD를 `rtc-s178/build/`에 별도 생성(기존 페이로드 앞 0x150000 바이트 + 새 FD, s131_builder 패턴), 해시 고정.
하드웨어 판정: (1) UEFI 시리얼 로그의 LibRtcInitialize DEBUG 라인, (2) Windows에서 기존 작업 채널로 `(Get-Date).ToUniversalTime()`이
호스트 UTC ±수 초, (3) 재부팅 후 2022-05-07 복귀 없음, (4) `bench-session-s176` 절차의 Authenticode 검사 통과(NotTimeValid 소멸).
성공 기준은 "Windows가 부팅 시각 기준 올바른 UTC를 보고" 이며, **RTC 네이티브 지원 완료를 주장하지 않습니다**
(전원 끈 뒤 Windows가 시각을 유지하는 것은 PMU가 하는 일이고, Windows→PMU 기록은 없음).

### Phase C — 완전 네이티브 RTC 드라이버 (별도 과제, 이번 계획 밖)

- 선택지 1: UEFI 런타임 드라이버가 SPMI 컨트롤러 MMIO를 런타임 메모리로 등록하고 GetTime마다 0xd002/0xd100을 직접 읽음.
  난점: 런타임(Windows 커널 컨텍스트)에서의 MMIO 접근·가상주소 변환, Windows 측 SPMI/SMC 드라이버와의 동시 접근, 트레이서/HV 영향.
- 선택지 2: Windows 드라이버(`windows_drivers/smc-driver.md` 2.3절의 "별도 RTC 드라이버")가 SMC CLKM/nvmem을 소유하고 SetTime 시 0xd100을 기록.
- 어느 쪽이든 **PMU offset 쓰기**가 포함되므로 별도 승인·설계·리뷰가 필요합니다.

## 7. 미확정 사항 / 위험

1. `CLKM == PMU 0xd002` 가설(위 검증 2차로 확정). 틀려도 Phase A는 실패를 로그로 드러낼 뿐 ADT를 바꾸지 않습니다.
2. 호스트 m1n1 아래에서 nub-spmi 컨트롤러가 응답한다는 가정(iBoot/macOS 경로상 상시 전원). 무응답 시 `SPMI.read()`가 busy-wait에 갇힐 수 있으므로
   실제 실행 전 모듈에 **타임아웃**(Linux는 10 ms × 5)을 추가해야 합니다(초안에는 없음, TODO).
3. Windows의 2022-05-07 기원 가설(무효 EFI_TIME → 기본값)은 검증되지 않았습니다. Phase B 로그로 간접 확인됩니다.
4. `hv.setup_adt()`는 `/arm-io/nub-spmi-a0/hpm%d`만 지우므로 J274 `/arm-io/nub-spmi`는 게스트 ADT에 남습니다. 게스트(UEFI/Windows)가 현재
   SPMI를 쓰지 않는다는 점은 소스 grep으로만 확인했습니다.
5. 빌드 리포트는 오늘 03:15 빌드 기준이며 활성 페이로드(S172/S174/S179)는 건드리지 않았습니다.

## 8. 이 디렉토리 파일

- `PLAN.md` — 본 문서
- `adt_rtc_probe.py` — 독립 ADT 파서(읽기 전용), `python3 adt_rtc_probe.py <adt.bin>`
- `rtc_math.py` — 변환 수식 + 셀프테스트, `python3 rtc_math.py`
- `rtc_snapshot_module.py` — Phase A 초안(기본 DRY RUN, 미연결)
- `linux-ref/` — Linux 원본 참조 소스
- `openbsd-ref/` — 비어 있음(다운로드 실패 기록)
