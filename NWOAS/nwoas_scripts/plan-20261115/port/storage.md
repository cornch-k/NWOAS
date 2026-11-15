# 내장 SSD를 Windows가 직접 쓰게 하기 — windows-on-m1(J313) 방식을 우리(J274)에 옮길 때 (2026-11-15)

> 범위: 읽기만 했다. 아무것도 빌드·실행하지 않았고, 지금 돌고 있는 맥미니 게스트는 건드리지 않았다.
> 표기: **[사실]** = 파일:줄로 확인한 것, **[가설]** = 확인 안 된 추측, **[그쪽 기록]** = 상대 저장소 작성자가 남긴 기록(원본 증거 폴더 `.local/`는 받은 사본에 없어 우리가 재확인 못 함).

용어 짧게:
- **ANS**: M1 안의 애플 SSD 제어 칩. 그 안에서 작은 펌웨어(**RTKit**)가 돈다.
- **EL2/하이퍼바이저**: m1n1이 Windows 아래에서 도는 권한 단계. 지금 우리 SSD 읽기·쓰기는 전부 여기를 거친다.
- **DMA**: 장치가 CPU를 거치지 않고 메모리를 직접 읽고 쓰는 것.
- **LBA**: 디스크 블록 번호(우리 SSD는 블록 하나가 4096바이트).
- **EBS(ExitBootServices)**: UEFI가 Windows에게 하드웨어를 넘기는 순간.
- **GSIV**: Windows가 보는 인터럽트 번호.
- **브리지 방식**: 하이퍼바이저가 가짜 NVMe를 보여 주고 명령을 대신 처리하는 방식(우리 S231도 이 계열).
- **ANS-DIRECT**: 하이퍼바이저가 빠지고 Windows 드라이버(AppleAns)가 진짜 ANS를 직접 다루는 방식.

---

## 1. 결론

1. **옮기는 것은 하드웨어상 가능해 보인다.** 우리 맥미니(J274, macOS 26 Tahoe 펌웨어)에서 오늘 뽑은 ANS 주소·크기·인터럽트 값이, 그쪽 m1n1이 엄격히 검사하는 값과 똑같다 **[사실]**. 우리 m1n1은 이미 Tahoe에서 ANS를 켜서 Windows 설치·부팅까지 했다 **[사실]**.
2. **그대로 옮기면 지금의 디스크 보호가 전부 사라진다(가장 중요).** ANS-DIRECT에서는 EBS 뒤 하이퍼바이저가 저장장치에서 완전히 빠지고, 쓰기 범위 검사도 DMA 주소 검사도 하지 않는다 **[사실]**. 그러면 우리 S231의 Windows 쓰기 구역(LBA 39944960~59968629), GPT 블록 예외, 맥북 쪽 GptGuard가 모두 효력을 잃는다. macOS APFS·ISC·RecoveryOS·앞으로 만들 NWSTUB(부팅용 작은 파티션)까지 Windows가 쓸 수 있게 된다 **[코드에서 따라 나오는 가설]**.
3. **그쪽 주소 변환 상수는 우리 메모리 배치와 맞지 않아 반드시 바꿔야 한다** **[사실]**. 그대로 쓰면 정상 페이지를 거부하거나 엉뚱한 주소로 바꾼다.
4. 속도 이득은 크다 **[그쪽 기록]**: 순차 읽기가 브리지 방식 대비 약 6~7배(478 → 2811~3433 MB/s).
5. **추천 방향 [가설, 아직 아무도 만들지 않음]**: 순수 ANS-DIRECT가 아니라 "검사하는 직접 방식" — 데이터는 장치가 직접 옮기되, 명령을 넣는 순간(문 두드리기 레지스터 한 곳)만 하이퍼바이저가 가로채 LBA 범위와 메모리 주소를 검사 — 을 따로 설계한다. 아니면 그쪽 브리지 방식을 쓰거나, 콘치님이 전체 백업 뒤 위험을 받아들이기로 명시적으로 결정할 때만 순수 직접 방식을 쓴다.
6. 아무 단계도 실제 하드웨어 검증을 거치지 않았다. 빌드가 되더라도 부팅·저장이 된다는 뜻은 아니다.

---

## 2. 그쪽(J313)에서 된 것

### 2-1. 구조 [사실, 그쪽 소스]

| 부분 | 하는 일 | 출처 |
|---|---|---|
| m1n1 `hv_map_ans_broker` | 칩이 T8103인지, `/arm-io/ans` reg0(ASC)·reg3(NVMe)와 `/arm-io/sart-ans` 크기가 0x6c000/0x60000인지, 우편함 주소가 ASC+0x8000인지, 인터럽트 590이 정확히 한 번 있는지 검사. 590을 레벨 방식으로 게스트에 전달 등록. 컨트롤러·ASC·SART·우편함을 게스트에게서 숨김 | `m1n1_windows/src/hv_ans_platform.c:417-418, 423-466` |
| 같은 함수의 추가 조건 | 브로커(브리지)용 인자(irq == 960, 브로커 주소·크기, ABI 주 버전)와 `hv_nvme_recovery_geometry`도 요구. 직접 방식 전환 함수가 브로커 서비스에 등록되는 구조라, 직접 방식만 옮겨도 브로커 준비 코드가 따라온다 | `hv_ans_platform.c:424, 445-446` |
| m1n1 `direct_release` | EBS 순간 한 번 실행. 브리지를 멈추고 비우고 정지한 뒤(`hv_ans_service.c:104-111`) ANS를 리셋 → RTKit을 제한 시간 안에 부팅 → BOOT_STATUS == 0xde71ce55 확인 → 컨트롤러를 꺼진 상태(CC.EN=0, RDY=0)로 두고 확인 | `hv_ans_platform.c:199-219`, `nvme.c:309-427, 552`, `hv_ans_direct.h:19-24` |
| 이어서 | 컨트롤러 0x27bcc0000+0x40000, 우편함 0x277408000+0x4000을 같은 주소 그대로 게스트에 연결. ASC CPU 페이지와 SART는 계속 숨김. RTKit·SART 버퍼는 게스트가 도는 동안 EL2에 남겨 둠 | `hv_ans_platform.c:206-207`, `nvme.c:422-425` |
| Windows 드라이버 AppleAns | StorPort(Windows 저장장치 드라이버 형식) 미니포트. `ACPI\APPL0003`에 붙고 부팅 시작 드라이버(StartType=0) | `drivers/storage/appleans/windows/AppleAns.inf:15, 36-42` |
| 큐 크기 | 관리 큐 2, 입출력 큐 64, 태그 = 슬롯 번호 | `common/ans_direct.h:69-70` |
| 쓰기 제한 | `Namespace.read_only = 0`, 주석 "Windows decides which sectors it writes" | `windows/direct.c:166-167` |
| 주소 변환 | 게스트 주소 → 실제 주소 상수가 8GB MacBook Air 배치에 고정: 낮은 창 0x100000 → 0x8a0100000(크기 0x3ff00000), 그대로 쓰는 범위 0x818000000~0xa00000000 | `common/ans_direct.h:63-67` |
| 드라이버 안의 주소 검사 | `ansd_translate`/`ansd_translate_prps`가 위 범위 밖 페이지를 거부(Windows 안의 소프트웨어 검사일 뿐 강제 장치는 아님) | `common/ans_direct.c:36-65`, `windows/native.c:470-473` |
| ACPI | `_HID APPL0003`, `_CCA 1`, 메모리 범위 2개, `Interrupt 960 Level`, `ANSN` 값으로 켜고 끔 | `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAnsDirect.asl.inc:7-19` |
| 실제 쓰는 GSIV | 960 | `m1n1_windows/src/hv_ans_broker_abi.h:29` |
| 충돌 이력 | 처음 쓴 889가 GPU(AGX) 가짜 인터럽트와 겹쳐 0x7B(부팅 장치 못 찾음) 발생 | `investigation/EXPERIMENTS.md:60372-60377` |
| 덤프 | 충돌 덤프 모드는 거부 → 직접 방식에서는 블루스크린 덤프가 남지 않음 | `windows/driver.c:66-69` |
| 복구 경로 | 같은 C:를 EL2 가짜 NVMe(stornvme)로 부팅 | `investigation/ANS_DIRECT_PLAN.md` |
| 두 방식 오가기 | Windows는 그 부팅에서 안 쓴 부팅 드라이버에 `StartOverride=3`을 적어, 다른 방식으로 부팅하면 0x7B. SYSTEM 작업 `AnsStartOverrideGuard`가 시작 시와 5분마다 둘 다 지움 | `tools/ans-dual-boot.ps1:1-17, 50` |
| 서명 | 기존 인증서로 서명(`sign-offline.ps1`), 설치 시 전용 인증서를 LocalMachine Root·TrustedPublisher에 영구 추가. Microsoft 정식 커널 정책 검사는 통과 못 함 | `install-appleans.ps1:36-40`, `ANS_FIRST_TEST_RU.md:128-131` |

참고: `direct_release` 위 주석(`hv_ans_platform.c:196`)과 ANS011-R1 로그에는 아직 "590→889"라고 적혀 있다. 실제 값은 960이다. 옮길 때는 주석·로그가 아니라 ABI 헤더(`hv_ans_broker_abi.h:29`)를 기준으로 한다 **[사실]**.

### 2-2. 우편함(ANS 펌웨어 메시지) 처리 [사실]

| 메시지 | 드라이버 반응 | 출처 |
|---|---|---|
| 알 수 없는 메시지 | 개수만 셈(멈추지 않음) | `common/ans_direct.c:275-276` |
| syslog·ioreport 응답(echo) 실패 | 고장 처리(fault) | `ans_direct.c:262` |
| crashlog 요청 | 충돌·고장 상태로 고정 | `ans_direct.c:268-272` |

그쪽 계획 문서는 "알 수 없는 메시지도 고장으로 고정"이라 적어(`ANS_DIRECT_PLAN.md:104`) 코드와 다르다 **[사실]**.

### 2-3. 기록된 결과 [그쪽 기록]

| 단계 | 내용 | 출처 |
|---|---|---|
| ANS001~006 | 드라이버 없는 장치 노출 → 읽기까지 단계별 통과 | `investigation/ANS_CURRENT_STATE.md:66-84` |
| ANS008/009 | EL2 쓰기 창 안에서 NTFS 쓰기 | 같은 파일 |
| ANS011-R1 | ANS-DIRECT 첫 성공: EBS 뒤 하이퍼바이저 저장 작업 0회, 검사 해시 브리지와 일치, chkdsk 0 | `EXPERIMENTS.md:60254` 이후 |
| ANS012-R3 | 원인 발견 회차: StorPort가 넘긴 주소 목록에 메모리 관리자의 "빈 페이지"가 여러 번 반복되는데, 이를 거부하자 페이지 읽기가 깨짐. **이 회차 자체는 고장난 상태**(디스크 Event 51 3235건, 페이지 읽기 실패, WMI 고장, 로그인 안 됨) | `EXPERIMENTS.md:60470-60490` |
| ANS012-R4 | 수정 후 정상 확인, Event 51 0건 | `EXPERIMENTS.md:60527-60536` |
| ANS013-R1 | 전용 영구 서명 인증서로 표준 드라이버 설치, 정상 | `EXPERIMENTS.md:60540-60562` |

성능(정상 회차만, ANS012-R3은 고장 상태라 뺐음):

| 측정 | ANS011-R1 직접 | ANS011-R1 브리지 | ANS012-R4 직접 | ANS013-R1 직접 |
|---|---|---|---|---|
| 순차 읽기 1M QD8 (MB/s) | 2811 | 478 | 3182 | 3433 |
| 순차 쓰기 1M (MB/s) | 2147 (QD1) | 927 (QD1) | 2328 (QD8) | 2381 (QD1) |
| 무작위 읽기 4K QD32 (IOPS) | 87378 | 12835 | 85061 | 94453 |
| 무작위 쓰기 4K QD32 (IOPS) | 85489 | 63840 | 39594 | 58061 |

출처: `EXPERIMENTS.md:60271-60283`(직접), `:60293-60300`(브리지), `:60529-60534`(R4), `:60555-60560`(R13).

- README의 "읽기 3.4 GB/s, 94k IOPS"(`README.md:20, 60-62`)는 ANS013-R1 기록과 맞는다 **[사실: 기록끼리 일치]**. 원본 증거는 여전히 없다.
- 무작위 쓰기는 회차마다 39594~85489 IOPS로 크게 흔들린다. 이 줄은 믿을 만한 값으로 보지 않는다.

### 2-4. 그쪽도 못 푼 것 [사실, `ANS_DIRECT_PLAN.md:117-121`]

| 문제 | 상태 |
|---|---|
| 가끔 첫 완료 신호가 사라짐(R3/R7) | 미해결 |
| 우편함을 인터럽트 처리기 + 50ms 타이머로만 돌보는 것이 충분한지 | 미확인 |
| EBS부터 드라이버 초기화 사이 빈 시간이 무해한지 | 미확인 |

---

## 3. 우리(J274)와 다른 점

| 항목 | 그쪽 J313 | 우리 J274 | 판정 |
|---|---|---|---|
| 칩 / 호환 문자열 | T8103, ANS2 | T8103, `apple,t8103-nvme-ans2` (`nwoas_scripts/dtb-s221/recovered-j274.dts:2934-2944`) | [사실] 같음 |
| `/arm-io/ans` reg0 | 0x277400000 + 0x6c000 | 0x277400000 + 0x6c000 | [사실] 같음 (`nwoas_scripts/ans2-s87/run-20261115-174016/metadata.json`) |
| `/arm-io/ans` reg3 | 0x27bcc0000 + 0x60000 | 0x27bcc0000 + 0x60000 | [사실] 같음 |
| `sart-ans` | 0x27bc50000 + 0x10000 | 0x27bc50000 + 0x10000, `sart,coastguard` | [사실] 같음 |
| ANS 인터럽트 | 590 사용 | 584, 583, 586, 585, 590 | [사실] 590 있음 |
| 디스크 크기 | "APPLE SSD AP0256", 4 KiB 블록 | 61279344블록 × 4096 B | [사실] |
| 메모리 / 배치 | 8 GB, 낮은 1 GiB 창 → 0x8a0100000 | 16 GB, 낮은 4 GiB 창을 RAM 맨 위에서 빌려 쓰고 실행 중 계산 (`layout-s231/guest_module.py:44, 59-66`) | [사실] **상수 안 맞음** |
| 시스템 펌웨어 | macOS 15.x (분석 대상 15.7.8, `ANS_DMA_ADMISSION_CONTRACT.md:6`) | macOS 26 Tahoe | [사실] 다름 |
| RTKit 허용 버전 | 11~12 | 11~12 (`m1n1-layout-s231/src/rtkit.c:70-71`) | [사실] 같음 |
| TCB_STAT 레지스터 주소 | 직접 방식 0x28120, 복구 방식 0x29120 (`m1n1_windows/src/nvme.c:66-67, 253, 258-260`; 드라이버 `ans_direct.h:37`도 0x28120) | 0x29120 (`m1n1-layout-s231/src/nvme.c:58`) | [사실] 다름. 그쪽은 참고용 읽기로만 씀(ANS004-R1, ANS006-R2/R3 근거). `NATIVE_ANS_STORAGE.md:113-115`의 "미결" 메모는 옛 기록 |
| 부팅 후 ANS 펌웨어 메시지 종류 | syslog, ioreport, 전원 응답, crashlog | Tahoe에서는 모름 | [가설] 다를 수 있음. 모르는 메시지는 세기만 하니 멈춤 위험이 크지만, syslog/ioreport 응답 실패나 crashlog면 고장 처리로 간다 |
| 인터럽트 전달 | 590 → 960을 명시 등록, 게스트가 끝냈다고 알릴 때까지 가림 | 우리 vGIC는 INTID = AIC 번호 그대로 (`hv_vgic.c:116, 537`). 하드웨어 레벨 인터럽트 전달은 원래 약한 곳 | 확인 필요 |
| Windows 저장 드라이버 | AppleAns (ACPI APPL0003) | stornvme, 가짜 PCI NVMe BAR 0x700100000 (`hv_vm.c:1188`) | [사실] |
| 데이터 경로 | 장치가 직접 DMA, EL2 없음 | EL2 빠른 경로 + m1n1 `nvme_rw_guest`, 명령당 최대 256블록(S231 빌드 설정) | [사실] |
| 관리 큐 | 드라이버 | 맥북의 Python 모델 | [사실] |
| 쓰기 제한 | 없음 | 구역 + GPT 블록 + 맥북 GptGuard | [사실] |
| 맥북 없이 부팅 | 넘기기가 C 코드(m1n1 + Mu EBS 관문) | 맥북에 의존 (`plan-20261115/standalone.md`) | [사실] |
| 충돌 덤프 | 거부 | stornvme 경유(여기서는 확인 안 함) | 미확인 |

### 3-1. 지금 우리 보호 장치 [사실]

| 단계 | 내용 | 출처 |
|---|---|---|
| 1 | EL2 `nvme_write_allowed`: LBA 39944960~59968629 밖 쓰기 거부, 단 앞·뒤 GPT 블록은 예외 | `m1n1-layout-s231/src/nvme.c:536-571` |
| 2 | 빠른 경로에서도 같은 구역 강제 | `m1n1-layout-s231/src/hv_vm.c:1188-1192` |
| 3 | 맥북 쪽 GptGuard와 PROTECTED 스냅샷 검사로 GPT 내용 확인 | `nwoas_scripts/layout-s231-guest-test.sh` 계열 |

순수 ANS-DIRECT는 세 단계를 모두 없앤다. 그쪽 계획서도 안전은 "now rests on Windows partitioning and the driver's capacity check"(`ANS_DIRECT_PLAN.md:114`)라고 적었다.

---

## 4. 옮기는 순서 (담당·검증 방법)

원칙: 모든 단계는 콘치님 승인 뒤, 지금 돌고 있는 게스트와 겹치지 않을 때 한다. 담당 표기는 프로젝트 규칙을 따른다 — 하이퍼바이저·부팅 넘기기·메모리 연결 변경은 Opus(시스템 핵심부라 Sonnet으로 부족), 단순 비교·문서는 Sonnet, 실제 기기 시험은 콘치님.
새 m1n1 판 번호는 USB 수정 계획(S232)과 겹치지 않게 따로 정한다.

| 순서 | 할 일 | 담당 | 검증 방법 | 통과 기준 |
|---|---|---|---|---|
| P0-1 | 우리 Mu(`loader-s223`)에 EBS 관문이 있는지 읽기(지금까지 못 봄). 없으면 오류를 돌려줄 수 있는 관문 추가 | Opus | 소스 읽기, 빌드 후 바이너리 비교 | 관문이 실패하면 Windows로 넘어가지 않음 |
| P0-2 | 그쪽 `nvme_direct_boot_disabled`(제한 시간 RTKit 부팅, 제한 시간 끄기, 상태 확인)와 단계2 직접 연결을 새 m1n1 판에 옮김. 우리 `nvme.c`에는 `nvme_shutdown`·`pmgr_reset`만 있고 이 함수들이 없음. 브로커 준비 코드(`hv_ans_platform.c:424, 445-446`)가 따라온다는 점 고려 | Opus | 빌드, 정적 분석, 그쪽 함수와 줄 단위 대조 | 빌드 성공(부팅 성공 아님) |
| P0-3 | `ANSD_LOW_*`/`ANSD_RAM_*` 고정 상수를 펌웨어가 알려 주는 표로 바꿈(예: 읽기 전용 ACPI 영역이나 `_DSD`에 낮은 창 위치·RAM 범위). 드라이버 안 주소 검사도 같은 표 사용 | Opus(설계), Sonnet(시험 코드) | `guest_module.py` 계산값과 맞추는 단위 시험 | 우리 배치의 모든 게스트 페이지가 올바른 실제 주소로 바뀜 |
| P0-4 | DSDT에서 겹치지 않는 GSIV 고르기. 우리 vGIC는 번호를 그대로 쓰니 590 후보, 충돌 검사 | Sonnet | DSDT 전체 인터럽트 번호 목록 비교 | 중복 0개 |
| P0-5 | TCB_STAT: 그쪽처럼 직접 방식은 0x28120, 복구는 0x29120으로 나눌지 결정(참고용 읽기라 위험 낮음) | Opus | 우리 기존 ANS 기록과 대조 | 결정 기록 |
| P0-6 | 보호 방식 결정 — (가) 그쪽 브리지 방식 유지(명령당 약 164 µs, `ANS_DIRECT_PLAN.md:12`), (나) 검사하는 직접 방식(아래 5-2), (다) 전체 백업 뒤 순수 직접 | **콘치님 결정** | — | 결정 기록 |
| P1 | RAM 디스크로 WinPE 부팅. APPL0003 장치가 드라이버 없이 보이고 가짜 NVMe는 사라짐 | 콘치님(기기), Claude(로그 판독) | 하이퍼바이저 로그에서 넘기기 줄, **Tahoe에서** CC.EN=0과 BOOT_STATUS 정상 | 세 가지 모두 보임 |
| P2 | 드라이버 붙이고 관리 큐·Identify | 콘치님, Claude | 디스크 크기와 모델 문자열 비교 | 61279344블록, 모델 일치 |
| P3 | macOS 쪽 LBA를 **읽기만**. 같은 블록을 S231 브리지로 읽은 해시와 비교. 우편함 카운터(`mbox_unknown` 등)로 Tahoe 메시지 종류 기록 | 콘치님, Claude | 해시 비교, 카운터 기록 | 해시 일치, 고장 상태 없음 |
| P4 | Windows 구역 안에 따로 만든 시험 파티션에만 쓰기. (가) 또는 (나)의 보호가 켜진 상태에서 구역 밖 쓰기가 거부되는지 증명, 브리지로 다시 읽어 확인 | 콘치님, Claude | 거부 로그, 읽기 해시 | 구역 밖 쓰기 0건, 시험 파티션 데이터 일치 |
| P5 | C: 직접 방식 부팅. **첫 직접 부팅 전에** `AnsStartOverrideGuard`와 같은 장치를 먼저 설치(그쪽 ANS011-R1에서 직접 부팅 한 번 뒤 stornvme가 `StartOverride 0:3`으로 밀려남, `EXPERIMENTS.md:60285`. 없으면 다음 S231 부팅이 0x7B). 복구 경로는 바꾸지 않은 S231 실행 스크립트 | 콘치님, Claude | SSH·에이전트 접속, 브리지로 검사 해시, chkdsk, GPT 블록과 비-Windows 파티션 앞·뒤 1MiB를 사전 스냅샷과 바이트 비교 | 전부 일치 |
| P6 | 콜드 부팅, 재부팅, 장시간 부하, 첫 완료 신호 분실 대비 타이머 | 콘치님, Claude | 반복 시험 기록 | 오류 0건. 이것도 아직 "맥북 없이 부팅"은 아님(C 코드만으로 넘기기가 따로 필요) |

참고:
- P5에서 화면이 검게 나오면 화면 경로보다 **저장장치 오류(디스크 Event 51)를 먼저** 본다. 그쪽 ANS011·ANS012-R3의 검은 화면은 페이지 읽기 실패 때문이었다(`EXPERIMENTS.md:60536`) **[그쪽 기록]**.
- 서명: 그쪽은 전용 인증서를 Root·TrustedPublisher에 넣었다. BCD 테스트 서명(testsigning)을 켜야 하는지는 그쪽 기록에서 확인되지 않았다 **[가설]**. 우리 BCD의 testsigning 상태도 확인 안 했다 **[모름]**.

---

## 5. 위험과 모르는 점

### 5-1. 순수 직접 방식의 위험 [가설, 단 코드에서 바로 따라 나옴]

| 위험 | 내용 |
|---|---|
| 다른 파티션 덮어쓰기 | 디스크 관리 "초기화/변환", diskpart, MBR2GPT, Windows 설치·WinRE의 파티션 재구성, BitLocker, 잘못된 ESP를 향한 `bcdboot`, 엉뚱한 볼륨 chkdsk, 외부 도구. APFS·애플 파티션은 Windows에 "알 수 없는 파티션"으로 보일 뿐 숨겨지지 않는다 |
| NWSTUB | 속성 0xD000000000000000(숨김·드라이브 문자 없음·읽기 전용)은 Windows 볼륨 관리자에게 주는 힌트일 뿐 강제가 아니다. NWSTUB가 m1n1 `boot.bin`을 담게 되면(`plan-20261115/dualboot.md` 2~4단계) 망가질 때 부팅 자체가 안 된다 |
| 메모리 아무 곳에나 DMA | 하이퍼바이저가 DMA 주소를 검사하지 않으니 드라이버 버그나 다른 커널 코드가 EL2(m1n1) 메모리, m1n1이 남겨 둔 RTKit 버퍼, 다른 장치의 주소 변환표까지 덮을 수 있다. 드라이버 안 주소 검사(`ans_direct.c:36-65`)는 Windows 안의 소프트웨어라 막는 장치가 아니다. SART가 데이터 DMA까지 막는지는 확인 안 했다(Asahi 쪽 이해로는 펌웨어 버퍼용) **[가설]** |
| 부분 보호의 함정 | 그쪽 ANS009 교훈: Windows가 쓸 수 있다고 믿는 볼륨의 쓰기를 거부하면 Windows가 멈춘다. 그래서 보호를 하려면 구역 밖을 거부만 하지 말고 숨기거나 읽기 전용으로 보여야 한다 **[그쪽 기록]** |
| 덤프 없음 | 직접 방식은 덤프 모드를 거부해 블루스크린 원인 기록이 안 남는다 **[사실, `driver.c:66-69`]** |

### 5-2. 줄이는 방법 [가설, 아무도 만들지 않음]

| 안 | 방법 | 비용 | 남는 위험 |
|---|---|---|---|
| (가) 그쪽 브리지 방식 | EL2가 명령마다 LBA 창 검사 | 명령당 약 164 µs [그쪽 기록] | 속도. 그쪽 하드웨어에서만 증명 |
| (나) 검사하는 직접 방식 | 게스트의 명령 줄(SQ)·TCB·주소 목록(PRP) 페이지를 게스트가 못 쓰는 메모리에 두고, 입출력 큐 문 두드리기 레지스터(0x24910)만 가로챔. 가로챌 때 명령과 TCB를 EL2 사본으로 **복사한 뒤** 명령 종류·LBA 구역·모든 PRP 페이지가 게스트 RAM 안(예약 영역 제외)인지 검사하고 진짜 레지스터를 울림. 데이터는 복사 없이 장치가 직접 옮김 | 명령당 가로채기 1회 | 설계·시험이 새로 필요. 사본 없이 검사하면 다른 CPU가 검사 뒤 내용을 바꿀 수 있어 사본은 필수 |
| (다) 순수 직접 | 그쪽 그대로 | 없음 | 5-1 전부. 전체 백업과 GPT 블록 이미지 뒤 콘치님이 명시적으로 결정할 때만 |

### 5-3. 모르는 점

| 항목 | 상태 |
|---|---|
| Tahoe 펌웨어에서 ANS가 부팅 뒤 보내는 메시지 종류 | 모름 → P3에서 기록 |
| 우리 vGIC로 레벨 인터럽트 590을 안정적으로 전달할 수 있는지 | 모름(프로젝트 프론티어와 같은 약점) |
| 우리 Mu에 EBS 관문이 있는지 | `loader-s223`에서 못 봄, 확인 필요 |
| 첫 완료 신호 분실, EBS~초기화 빈 시간 | 그쪽도 미해결 |
| BCD testsigning 필요 여부와 우리 현재 상태 | 모름 |
| 그쪽 성능 수치의 원본 증거 | 사본에 없음(`.local/` 없음) |

### 5-4. 우리 USB 문제와의 관련 [가설, 우선순위 낮음]

그쪽 ANS012-R3 원인은 "같은 실제 페이지가 주소 목록에 두 번 나오는 것"을 드라이버가 거부한 것이었다. 우리 USB-A 문제(`plan-20261115/usb.md`)도 캐시 읽기에서만 생기지만, 그쪽은 **입출력 오류와 Event 51**이 났고 우리는 **오류 없이 960 KiB가 0으로** 온다. 모양이 달라 관련은 약하다. 우리 xHCI/DART 연결 경로(S174 `guest_map.py`, USB 주소 변환)가 같은 페이지 두 번을 잘못 처리하는지는 나중에 볼 만한 후보 정도다.

---

## 6. 출처

그쪽(읽기 전용, 모두 `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1/` 아래):

| 파일 | 줄 |
|---|---|
| `investigation/ANS_DIRECT_PLAN.md` | 12, 104, 114, 117-121 |
| `investigation/ANS_CURRENT_STATE.md` | 66-84 |
| `investigation/EXPERIMENTS.md` | 60254 이후, 60271-60300, 60372-60377, 60470-60490, 60527-60562 |
| `investigation/ANS_DMA_ADMISSION_CONTRACT.md` | 6 |
| `README.md` | 20, 60-62 |
| `m1n1_windows/src/hv_ans_platform.c` | 196, 199-219, 417-418, 423-466 |
| `m1n1_windows/src/hv_ans_service.c` | 104-111 |
| `m1n1_windows/src/hv_ans_direct.h` | 19-24 |
| `m1n1_windows/src/hv_ans_broker_abi.h` | 29 |
| `m1n1_windows/src/nvme.c` | 66-67, 253, 258-260, 309-427, 552 |
| `drivers/storage/appleans/common/ans_direct.h` | 37, 63-70 |
| `drivers/storage/appleans/common/ans_direct.c` | 36-65, 245-279 |
| `drivers/storage/appleans/windows/direct.c` | 166-167 |
| `drivers/storage/appleans/windows/native.c` | 470-473 |
| `drivers/storage/appleans/windows/driver.c` | 66-69 |
| `drivers/storage/appleans/windows/AppleAns.inf` | 15, 36-42 |
| `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAnsDirect.asl.inc` | 7-19 |
| `tools/ans-dual-boot.ps1` | 1-17, 50 |
| `install-appleans.ps1` | 36-40 |
| `ANS_FIRST_TEST_RU.md` | 128-131 |
| `NATIVE_ANS_STORAGE.md` | 113-115 (옛 메모) |

우리(`/Users/angigyeom/Desktop/DEV/NWOAS/` 아래):

| 파일 | 줄 |
|---|---|
| `m1n1-layout-s231/src/nvme.c` | 58, 536-611 |
| `m1n1-layout-s231/src/hv_vm.c` | 1188-1192 |
| `m1n1-layout-s231/src/hv_vgic.c` | 116, 537 |
| `m1n1-layout-s231/src/rtkit.c` | 70-71 |
| `nwoas_scripts/layout-s231/guest_module.py` | 44, 59-66 |
| `nwoas_scripts/layout-s231/manifest.json` | — |
| `nwoas_scripts/ans2-s87/run-20261115-174016/metadata.json` | — (오늘 뽑은 ADT 값) |
| `nwoas_scripts/dtb-s221/recovered-j274.dts` | 2934-2944 |
| `nwoas_scripts/plan-20261115/usb.md`, `dualboot.md`, `standalone.md` | — |
